#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <inttypes.h>


#define lo(i) ((i) & 0xff)
#define hi(i) (((i) & 0xff00) >> 8)

#define get16(p) ((uint16_t)((*(uint8_t*)(p)) | ((*(((uint8_t*)(p))+1)) << 8)))


const uint8_t bit_reverse[] = {0x00,0x80,0x40,0xC0,0x20,0xA0,0x60,0xE0,0x10,0x90,0x50,0xD0,0x30,0xB0,0x70,0xF0,0x08,0x88,0x48,0xC8,0x28,0xA8,0x68,0xE8,0x18,0x98,0x58,0xD8,0x38,0xB8,0x78,0xF8,0x04,0x84,0x44,0xC4,0x24,0xA4,0x64,0xE4,0x14,0x94,0x54,0xD4,0x34,0xB4,0x74,0xF4,0x0C,0x8C,0x4C,0xCC,0x2C,0xAC,0x6C,0xEC,0x1C,0x9C,0x5C,0xDC,0x3C,0xBC,0x7C,0xFC,0x02,0x82,0x42,0xC2,0x22,0xA2,0x62,0xE2,0x12,0x92,0x52,0xD2,0x32,0xB2,0x72,0xF2,0x0A,0x8A,0x4A,0xCA,0x2A,0xAA,0x6A,0xEA,0x1A,0x9A,0x5A,0xDA,0x3A,0xBA,0x7A,0xFA,0x06,0x86,0x46,0xC6,0x26,0xA6,0x66,0xE6,0x16,0x96,0x56,0xD6,0x36,0xB6,0x76,0xF6,0x0E,0x8E,0x4E,0xCE,0x2E,0xAE,0x6E,0xEE,0x1E,0x9E,0x5E,0xDE,0x3E,0xBE,0x7E,0xFE,0x01,0x81,0x41,0xC1,0x21,0xA1,0x61,0xE1,0x11,0x91,0x51,0xD1,0x31,0xB1,0x71,0xF1,0x09,0x89,0x49,0xC9,0x29,0xA9,0x69,0xE9,0x19,0x99,0x59,0xD9,0x39,0xB9,0x79,0xF9,0x05,0x85,0x45,0xC5,0x25,0xA5,0x65,0xE5,0x15,0x95,0x55,0xD5,0x35,0xB5,0x75,0xF5,0x0D,0x8D,0x4D,0xCD,0x2D,0xAD,0x6D,0xED,0x1D,0x9D,0x5D,0xDD,0x3D,0xBD,0x7D,0xFD,0x03,0x83,0x43,0xC3,0x23,0xA3,0x63,0xE3,0x13,0x93,0x53,0xD3,0x33,0xB3,0x73,0xF3,0x0B,0x8B,0x4B,0xCB,0x2B,0xAB,0x6B,0xEB,0x1B,0x9B,0x5B,0xDB,0x3B,0xBB,0x7B,0xFB,0x07,0x87,0x47,0xC7,0x27,0xA7,0x67,0xE7,0x17,0x97,0x57,0xD7,0x37,0xB7,0x77,0xF7,0x0F,0x8F,0x4F,0xCF,0x2F,0xAF,0x6F,0xEF,0x1F,0x9F,0x5F,0xDF,0x3F,0xBF,0x7F,0xFF};


enum
{
    CTX_CODE = 0,
    CTX_DATA,
    CTX_WRITE,
    CTX_DPCM_DONE,
};


typedef struct
{
    /* same as $4012/$4013 */
    uint8_t addr;
    uint8_t len;
    /* bank config of $c000-$ffff when played */
    uint8_t bank_cfg[4];
} sample_t;

int samplec;
sample_t *samplev = NULL;


uint8_t system_ram[0x800];
uint8_t prg_ram[0x2000];
uint8_t mmc5_ram[0x400];
uint8_t *banked_data = NULL;
uint8_t *banked_data_ctx = NULL;

uint8_t bank_cfg[10];
int max_bank;

uint8_t expansions;


uint8_t main_code[] = { /* loaded at $3f00 */
    /* jsr play */
    0x20, 0x00, 0x00,
    /* sta sample_log_register */
    0x8d, 0x00, 0x3f,
    /* jmp back */
    0x4c, 0x00, 0x3f
};


uint8_t read_4015_cnt = 0;
uint8_t new_samp_flag = 0;

uint8_t samp_addr = 0;
uint8_t samp_len = 0;
uint8_t samp_valid = 0;


unsigned int get_banked_data_index(uint16_t addr)
{
    uint8_t bank = bank_cfg[(addr >> 12) - 6] % max_bank;
    return (bank << 12) | (addr & 0xfff);
}


uint8_t read(uint16_t addr, int ctx)
{
    if (addr < 0x2000)
    {
        return system_ram[addr & 0x7ff];
    }
    else if (addr >= 0x3f00 && addr < 0x3f09)
    {
        return main_code[addr & 0xff];
    }
    else if (addr == 0x4015)
    { /* fool nsfs that use the frame irq flag for timing */
        return (read_4015_cnt++ & 4) ? 0x40 : 0;
    }
    else if ((expansions & 8) && addr >= 0x5c00 && addr < 0x6000)
    {
        return mmc5_ram[addr & 0x3ff];
    }
    else if (!(expansions & 4) && addr >= 0x6000 && addr < 0x8000)
    {
        return prg_ram[addr & 0x1fff];
    }
    else if (addr >= 0x6000)
    {
        unsigned int i = get_banked_data_index(addr);
        banked_data_ctx[i] |= 1 << ctx;
        return banked_data[i];
    }
    return hi(addr);
}


void write(uint16_t addr, uint8_t v)
{
    if (addr < 0x2000)
    {
        system_ram[addr & 0x7ff] = v;
    }
    else if (addr == 0x3f00)
    {
        if (new_samp_flag)
        {
            new_samp_flag = 0;
            
            uint16_t actual_samp_addr = samp_addr * 0x40;
            uint16_t samp_end = actual_samp_addr + samp_len * 0x10;
            
            uint8_t samp_start_bank = actual_samp_addr >> 12;
            uint8_t samp_end_bank = samp_end >> 12;
            
            int found = 0;
            for ( ; found < samplec; found++)
            {
                if (samp_addr != samplev[found].addr) continue;
                if (samp_len != samplev[found].len) continue;
                uint8_t *check_bank_cfg = samplev[found].bank_cfg;
                for (int bank = 0; bank < 4; bank++)
                {
                    if (bank >= samp_start_bank && bank <= samp_end_bank)
                    {
                        if (check_bank_cfg[bank] != bank_cfg[bank - 6]) continue;
                    }
                }
                break;
            }
            if (found < samplec) return;
            
            samplev = realloc(samplev, (samplec+1)*sizeof(*samplev));
            samplev[samplec].addr = samp_addr;
            samplev[samplec].len = samp_len;
            memcpy(samplev[samplec++].bank_cfg, bank_cfg+6, 4);
        }
    }
    else if (addr == 0x4012)
    {
        samp_addr = v;
        samp_valid |= 1;
    }
    else if (addr == 0x4013)
    {
        samp_len = v;
        samp_valid |= 2;
    }
    else if (addr == 0x4015)
    {
        if ((v & 0x10) && samp_valid == 3)
        {
            new_samp_flag = v & 0x10;
        }
    }
    else if ((expansions & 8) && addr >= 0x5c00 && addr < 0x5ff6)
    {
        mmc5_ram[addr & 0x3ff] = v;
    }
    else if (addr >= 0x5ff6 && addr < 0x6000)
    {
        bank_cfg[addr - 0x5ff6] = v;
    }
    else if (!(expansions & 4) && addr >= 0x6000 && addr < 0x8000)
    {
        prg_ram[addr & 0x1fff] = v;
    }
    else if ((expansions & 4) && addr >= 0x6000)
    {
        unsigned int i = get_banked_data_index(addr);
        banked_data_ctx[i] |= 1 << CTX_WRITE;
        banked_data[i] = v;
    }
}


/******************************
     6502 emulation stuff
******************************/

enum
{
    MODE_ZP = 0,
    MODE_ZP_X,
    MODE_ZP_Y,
    MODE_ABS,
    MODE_ABS_X,
    MODE_ABS_Y,
    MODE_IND,
    MODE_IND_X,
    MODE_IND_Y,
};

uint16_t reg_pc;
uint8_t reg_s;
uint8_t reg_p;

uint8_t reg_a;
uint8_t reg_x;
uint8_t reg_y;


uint8_t set_nz(uint8_t v)
{
    reg_p &= ~0x82;
    if (!v) reg_p |= 2;
    if (v & 0x80) reg_p |= 0x80;
    return v;
}

uint8_t set_c(uint8_t v)
{
    reg_p &= ~1;
    if (v) reg_p |= 1;
    return v;
}

uint8_t set_v(uint8_t v)
{
    reg_p &= ~0x40;
    if (v) reg_p |= 0x40;
    return v;
}


uint8_t get_c() { return reg_p & 1; };
uint8_t get_z() { return reg_p & 2; };
uint8_t get_v() { return reg_p & 0x40; };
uint8_t get_n() { return reg_p & 0x80; };


void push(uint8_t v) { write(0x0100 | (reg_s--), v); }
uint8_t pull() { return read(0x0100 | (++reg_s), CTX_DATA); }


uint8_t get_code_byte() { return read(reg_pc++, CTX_CODE); }
uint16_t get_code_word() { return get_code_byte() | (get_code_byte() << 8); }

uint16_t get_code_addr(int mode)
{
    switch (mode)
    {
        case MODE_ZP:
            return get_code_byte();
        case MODE_ZP_X:
            return (get_code_byte() + reg_x) & 0xff;
        case MODE_ZP_Y:
            return (get_code_byte() + reg_y) & 0xff;
        case MODE_ABS:
            return get_code_word();
        case MODE_ABS_X:
            return get_code_word() + reg_x;
        case MODE_ABS_Y:
            return get_code_word() + reg_y;
        case MODE_IND:
        {
            uint16_t addr = get_code_word();
            return read(addr, CTX_DATA) | ((read((addr & 0xff00) | ((addr + 1) & 0xff), CTX_DATA)) << 8);
        }
        case MODE_IND_X:
        {
            uint8_t alo = get_code_byte() + reg_x;
            uint8_t ahi = alo + 1;
            return read(alo, CTX_DATA) | (read(ahi, CTX_DATA) << 8);
        }
        case MODE_IND_Y:
        {
            uint8_t alo = get_code_byte();
            uint8_t ahi = alo + 1;
            return (read(alo, CTX_DATA) | (read(ahi, CTX_DATA) << 8)) + reg_y;
        }
    }
}

uint8_t get_code_op(int mode) { return read(get_code_addr(mode), CTX_DATA); }



uint8_t adc(uint8_t v)
{
    uint16_t sum = reg_a + v + get_c();
    set_v((reg_a ^ sum) & (v ^ sum) & 0x80);
    set_c(sum > 0xff);
    return set_nz(reg_a = sum & 0xff);
}

uint8_t and(uint8_t v)
{
    return set_nz(reg_a &= v);
}

uint8_t asl(uint8_t v)
{
    set_c(v & 0x80);
    return set_nz(v << 1);
}

void bit(uint8_t v)
{
    reg_p &= 0x3d;
    reg_p |= (v & 0xc0);
    if (!(reg_a & v)) reg_p |= 2;
}

void cmp(uint8_t v1, uint8_t v2)
{
    v2 = ~v2;
    uint16_t sum = v1 + v2 + 1;
    set_c(sum > 0xff);
    set_nz(sum & 0xff);
}

uint8_t dec(uint8_t v)
{
    return set_nz(--v);
}

uint8_t eor(uint8_t v)
{
    return set_nz(reg_a ^= v);
}

uint8_t inc(uint8_t v)
{
    return set_nz(++v);
}

uint8_t ld(uint8_t *dest, uint8_t v)
{
    return set_nz(*dest = v);
}

uint8_t lsr(uint8_t v)
{
    set_c(v & 1);
    return set_nz(v >> 1);
}

uint8_t ora(uint8_t v)
{
    return set_nz(reg_a |= v);
}

uint8_t rol(uint8_t v)
{
    uint8_t old_c = get_c();
    set_c(v & 0x80);
    return set_nz((v << 1) | old_c);
}

uint8_t ror(uint8_t v)
{
    uint8_t old_c = get_c();
    set_c(v & 1);
    return set_nz((v >> 1) | (old_c ? 0x80 : 0));
}


uint8_t rmw_mem(uint8_t (*func)(uint8_t), uint16_t addr)
{
    uint8_t v = read(addr, CTX_DATA);
    v = (*func)(v);
    write(addr, v);
    return v;
}


void dcp(uint16_t a)
{
    cmp(reg_a, rmw_mem(dec, a));
}

void isb(uint16_t a)
{
    adc(~rmw_mem(inc, a));
}

void rla(uint16_t a)
{
    and(rmw_mem(rol, a));
}

void rra(uint16_t a)
{
    adc(rmw_mem(ror, a));
}

void slo(uint16_t a)
{
    ora(rmw_mem(asl, a));
}

void sre(uint16_t a)
{
    eor(rmw_mem(lsr, a));
}




/******************************
        main routines
******************************/

void dohelp()
{
    puts(   "dpcmtool - nsf dpcm ripper/bit reversal tool by karmic\n"
            "Usage: dpcmtool [options] nsfname...\n"
            "\n"
            "Options:\n"
            "  -r      Output samples\n"
            "  -rb     Output reversed samples\n"
            "  -b      Output NSF with reversed samples\n"
            "  -i num  Set amount of instructions to execute (default 10000000)\n"
            "\n"
            "If you do not specify any output mode, the default is -r."
        );
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
    /* get options */
    if (argc < 2) dohelp();
    
    enum
    {
        MODE_RIP = 0,
        MODE_RIP_REVERSE,
        MODE_NSF_REVERSE,
    };
    
    uint8_t cpudebug = 0;
    uint8_t mode = 0;
    uintmax_t instructions = 10000000;
    char** infilev = NULL;
    int infilec = 0;
    
    for (int i = 1; i < argc; i++)
    {
        char* arg = argv[i];
        if (!strcmp(arg, "-?") || !strcmp(arg, "--help")) dohelp();
        else if (!strcmp(arg, "--cpu-debug"))
        {
            cpudebug = 1;
        }
        else if (!strcmp(arg, "-r"))
        {
            mode |= 1 << MODE_RIP;
        }
        else if (!strcmp(arg, "-rb"))
        {
            mode |= 1 << MODE_RIP_REVERSE;
        }
        else if (!strcmp(arg, "-b"))
        {
            mode |= 1 << MODE_NSF_REVERSE;
        }
        else if (!strcmp(arg, "-i"))
        {
            if (++i == argc)
            {
                puts("-i requires an argument");
                return EXIT_FAILURE;
            }
            instructions = strtoumax(argv[i], NULL, 0);
            if (!instructions)
            {
                puts("Invalid argument for -i");
                return EXIT_FAILURE;
            }
        }
        else if (arg[0] == '-')
        {
            printf("Unknown option %s\n", arg);
            return EXIT_FAILURE;
        }
        else
        {
            infilev = realloc(infilev, (infilec+1)*sizeof(*infilev));
            infilev[infilec++] = argv[i];
        }
    }
    
    if (!infilec)
    {
        puts("No input files defined");
        return EXIT_FAILURE;
    }
    if (!mode)
    {
        mode = 1 << MODE_RIP;
    }
    
    /* process each nsf */
    for (int file = 0; file < infilec; file++)
    {
        uint8_t *nsfbuf = NULL;
        
        /* try reading file */
        char* fnam = infilev[file];
        FILE *f = fopen(fnam, "rb");
        if (!f)
        {
            printf("Could not open %s: %s\n", fnam, strerror(errno));
            goto fail;
        }
        printf("Reading %s...\n", fnam);
        fseek(f, 0, SEEK_END);
        size_t nsfsize = ftell(f);
        if (nsfsize < 0x81)
        {
            fclose(f);
            puts("File too short to be an NSF");
            goto fail;
        }
        nsfbuf = malloc(nsfsize);
        rewind(f);
        fread(nsfbuf, 1, nsfsize, f);
        fclose(f);
        
        /* try reading header */
        if (memcmp(nsfbuf, "NESM\x1a", 5))
        {
            puts("Not an NSF");
            goto fail;
        }
        if (*(nsfbuf+5) != 1)
        {
            puts("Only version 1 NSF files are supported");
            goto fail;
        }
        uint8_t songs = *(nsfbuf+6);
        if (!songs)
        {
            puts("Invalid amount of songs");
            goto fail;
        }
        uint8_t region = *(nsfbuf+0x7a) & 1;
        expansions = *(nsfbuf+0x7b);
        uint16_t init = get16(nsfbuf+0x0a);
        memcpy(main_code+1, nsfbuf+0x0c, 2);
        
        /* setup initial bank config */
        uint8_t default_bank_cfg[10];
        uint16_t load = get16(nsfbuf+8);
        uint16_t load_bank_offs = load & 0xfff;
        size_t datasize = nsfsize - 0x80;
        max_bank = ((load_bank_offs + (datasize-1)) / 0x1000) + 1;
        if (*(uint64_t*)(nsfbuf+0x70))
        { /* bankswitched */
            memcpy(default_bank_cfg, nsfbuf+0x76, 2);
            memcpy(default_bank_cfg+2, nsfbuf+0x70, 8);
        }
        else
        { /* non-bankswitched */
            if (((expansions & 4) && load < 0x6000) || (!(expansions & 4) && load < 0x8000))
            {
                puts("Invalid load address");
                goto fail;
            }
            for (int bank = (expansions & 4) ? 6 : 8; bank < 0x10; bank++)
            {
                int bankn = bank - (load >> 12);
                default_bank_cfg[bank-6] = (bankn >= max_bank || bankn < 0) ? 0 : bankn;
            }
        }
        
        /* setup nsf-global stuff */
        banked_data = malloc(max_bank * 0x1000);
        banked_data_ctx = malloc(max_bank * 0x1000);
        memset(banked_data_ctx, 0, max_bank * 0x1000);
        samplec = 0;
        samplev = NULL;
        
        /* execute each subsong */
        for (uint8_t song = 0; song < songs; song++)
        {
            printf("Processing song #%u...\n", song+1);
            new_samp_flag = 0;
            samp_valid = 0;
            
            memset(system_ram, 0, 0x800);
            memset(prg_ram, 0, 0x2000);
            memset(mmc5_ram, 0, 0x400);
            
            memcpy(bank_cfg, default_bank_cfg, 10);
            
            memset(banked_data, 0, max_bank * 0x1000);
            memcpy(banked_data+load_bank_offs, nsfbuf+0x80, datasize);
            
            reg_pc = init;
            system_ram[0x1ff] = 0x3e;
            system_ram[0x1fe] = 0xff;
            reg_s = 0xfd;
            reg_a = song;
            reg_x = region;
            
            /* do cpu execution */
            for (uintmax_t i = 0; i < instructions; i++)
            {
                uint8_t opcode = get_code_byte();
                if (cpudebug)
                {
                    printf("A=$%02X  X=$%02X  Y=$%02X  S=$%02X  %c%c%c%c    $%04X: $%02X\n",
                        reg_a, reg_x, reg_y, reg_s,
                        get_n() ? 'N' : 'n',
                        get_v() ? 'V' : 'v',
                        get_z() ? 'Z' : 'z',
                        get_c() ? 'C' : 'c',
                        reg_pc-1, opcode
                        );
                }
                switch (opcode)
                {
                    /* ADC */
                    case 0x69:
                        adc(get_code_byte());
                        break;
                    case 0x65:
                        adc(get_code_op(MODE_ZP));
                        break;
                    case 0x75:
                        adc(get_code_op(MODE_ZP_X));
                        break;
                    case 0x6d:
                        adc(get_code_op(MODE_ABS));
                        break;
                    case 0x7d:
                        adc(get_code_op(MODE_ABS_X));
                        break;
                    case 0x79:
                        adc(get_code_op(MODE_ABS_Y));
                        break;
                    case 0x61:
                        adc(get_code_op(MODE_IND_X));
                        break;
                    case 0x71:
                        adc(get_code_op(MODE_IND_Y));
                        break;
                    
                    /* AND */
                    case 0x29:
                        and(get_code_byte());
                        break;
                    case 0x25:
                        and(get_code_op(MODE_ZP));
                        break;
                    case 0x35:
                        and(get_code_op(MODE_ZP_X));
                        break;
                    case 0x2d:
                        and(get_code_op(MODE_ABS));
                        break;
                    case 0x3d:
                        and(get_code_op(MODE_ABS_X));
                        break;
                    case 0x39:
                        and(get_code_op(MODE_ABS_Y));
                        break;
                    case 0x21:
                        and(get_code_op(MODE_IND_X));
                        break;
                    case 0x31:
                        and(get_code_op(MODE_IND_Y));
                        break;
                    
                    /* ASL */
                    case 0x0a:
                        reg_a = asl(reg_a);
                        break;
                    case 0x06:
                        rmw_mem(asl, get_code_addr(MODE_ZP));
                        break;
                    case 0x16:
                        rmw_mem(asl, get_code_addr(MODE_ZP_X));
                        break;
                    case 0x0e:
                        rmw_mem(asl, get_code_addr(MODE_ABS));
                        break;
                    case 0x1e:
                        rmw_mem(asl, get_code_addr(MODE_ABS_X));
                        break;
                    
                    /* BIT */
                    case 0x24:
                        bit(get_code_op(MODE_ZP));
                        break;
                    case 0x2c:
                        bit(get_code_op(MODE_ABS));
                        break;
                    
                    /* BRK */
                    case 0x00:
                        push(hi(reg_pc+1));
                        push(lo(reg_pc+1));
                        push(reg_p);
                        
                        reg_pc = read(0xfffe, CTX_DATA) | (read(0xffff, CTX_DATA) << 8);
                        break;
                    
                    /* conditional branches */
                    case 0x10:
                    {
                        int8_t o = get_code_byte();
                        if (!get_n()) reg_pc += o;
                        break;
                    }
                    case 0x30:
                    {
                        int8_t o = get_code_byte();
                        if (get_n()) reg_pc += o;
                        break;
                    }
                    case 0x50:
                    {
                        int8_t o = get_code_byte();
                        if (!get_v()) reg_pc += o;
                        break;
                    }
                    case 0x70:
                    {
                        int8_t o = get_code_byte();
                        if (get_v()) reg_pc += o;
                        break;
                    }
                    case 0x90:
                    {
                        int8_t o = get_code_byte();
                        if (!get_c()) reg_pc += o;
                        break;
                    }
                    case 0xb0:
                    {
                        int8_t o = get_code_byte();
                        if (get_c()) reg_pc += o;
                        break;
                    }
                    case 0xd0:
                    {
                        int8_t o = get_code_byte();
                        if (!get_z()) reg_pc += o;
                        break;
                    }
                    case 0xf0:
                    {
                        int8_t o = get_code_byte();
                        if (get_z()) reg_pc += o;
                        break;
                    }
                    
                    /* CLx */
                    case 0x18:
                        reg_p &= ~1;
                        break;
                    case 0xb8:
                        reg_p &= ~0x40;
                        break;
                    
                    /* CMP */
                    case 0xc9:
                        cmp(reg_a, get_code_byte());
                        break;
                    case 0xc5:
                        cmp(reg_a, get_code_op(MODE_ZP));
                        break;
                    case 0xd5:
                        cmp(reg_a, get_code_op(MODE_ZP_X));
                        break;
                    case 0xcd:
                        cmp(reg_a, get_code_op(MODE_ABS));
                        break;
                    case 0xdd:
                        cmp(reg_a, get_code_op(MODE_ABS_X));
                        break;
                    case 0xd9:
                        cmp(reg_a, get_code_op(MODE_ABS_Y));
                        break;
                    case 0xc1:
                        cmp(reg_a, get_code_op(MODE_IND_X));
                        break;
                    case 0xd1:
                        cmp(reg_a, get_code_op(MODE_IND_Y));
                        break;
                    
                    /* CPX */
                    case 0xe0:
                        cmp(reg_x, get_code_byte());
                        break;
                    case 0xe4:
                        cmp(reg_x, get_code_op(MODE_ZP));
                        break;
                    case 0xec:
                        cmp(reg_x, get_code_op(MODE_ABS));
                        break;
                    
                    /* CPY */
                    case 0xc0:
                        cmp(reg_y, get_code_byte());
                        break;
                    case 0xc4:
                        cmp(reg_y, get_code_op(MODE_ZP));
                        break;
                    case 0xcc:
                        cmp(reg_y, get_code_op(MODE_ABS));
                        break;
                    
                    /* DEx */
                    case 0xc6:
                        rmw_mem(dec, get_code_addr(MODE_ZP));
                        break;
                    case 0xd6:
                        rmw_mem(dec, get_code_addr(MODE_ZP_X));
                        break;
                    case 0xce:
                        rmw_mem(dec, get_code_addr(MODE_ABS));
                        break;
                    case 0xde:
                        rmw_mem(dec, get_code_addr(MODE_ABS_X));
                        break;
                    case 0xca:
                        reg_x = dec(reg_x);
                        break;
                    case 0x88:
                        reg_y = dec(reg_y);
                        break;
                    
                    /* EOR */
                    case 0x49:
                        eor(get_code_byte());
                        break;
                    case 0x45:
                        eor(get_code_op(MODE_ZP));
                        break;
                    case 0x55:
                        eor(get_code_op(MODE_ZP_X));
                        break;
                    case 0x4d:
                        eor(get_code_op(MODE_ABS));
                        break;
                    case 0x5d:
                        eor(get_code_op(MODE_ABS_X));
                        break;
                    case 0x59:
                        eor(get_code_op(MODE_ABS_Y));
                        break;
                    case 0x41:
                        eor(get_code_op(MODE_IND_X));
                        break;
                    case 0x51:
                        eor(get_code_op(MODE_IND_Y));
                        break;
                    
                    /* INx */
                    case 0xe6:
                        rmw_mem(inc, get_code_addr(MODE_ZP));
                        break;
                    case 0xf6:
                        rmw_mem(inc, get_code_addr(MODE_ZP_X));
                        break;
                    case 0xee:
                        rmw_mem(inc, get_code_addr(MODE_ABS));
                        break;
                    case 0xfe:
                        rmw_mem(inc, get_code_addr(MODE_ABS_X));
                        break;
                    case 0xe8:
                        reg_x = inc(reg_x);
                        break;
                    case 0xc8:
                        reg_y = inc(reg_y);
                        break;
                    
                    /* JMP */
                    case 0x4c:
                        reg_pc = get_code_word();
                        break;
                    case 0x6c:
                        reg_pc = get_code_addr(MODE_IND);
                        break;
                    
                    /* JSR */
                    case 0x20:
                        push(hi(reg_pc+1));
                        push(lo(reg_pc+1));
                        reg_pc = get_code_word();
                        break;
                    
                    /* LDA */
                    case 0xa9:
                        ld(&reg_a, get_code_byte());
                        break;
                    case 0xa5:
                        ld(&reg_a, get_code_op(MODE_ZP));
                        break;
                    case 0xb5:
                        ld(&reg_a, get_code_op(MODE_ZP_X));
                        break;
                    case 0xad:
                        ld(&reg_a, get_code_op(MODE_ABS));
                        break;
                    case 0xbd:
                        ld(&reg_a, get_code_op(MODE_ABS_X));
                        break;
                    case 0xb9:
                        ld(&reg_a, get_code_op(MODE_ABS_Y));
                        break;
                    case 0xa1:
                        ld(&reg_a, get_code_op(MODE_IND_X));
                        break;
                    case 0xb1:
                        ld(&reg_a, get_code_op(MODE_IND_Y));
                        break;
                    
                    /* LDX */
                    case 0xa2:
                        ld(&reg_x, get_code_byte());
                        break;
                    case 0xa6:
                        ld(&reg_x, get_code_op(MODE_ZP));
                        break;
                    case 0xb6:
                        ld(&reg_x, get_code_op(MODE_ZP_Y));
                        break;
                    case 0xae:
                        ld(&reg_x, get_code_op(MODE_ABS));
                        break;
                    case 0xbe:
                        ld(&reg_x, get_code_op(MODE_ABS_Y));
                        break;
                    
                    /* LDY */
                    case 0xa0:
                        ld(&reg_y, get_code_byte());
                        break;
                    case 0xa4:
                        ld(&reg_y, get_code_op(MODE_ZP));
                        break;
                    case 0xb4:
                        ld(&reg_y, get_code_op(MODE_ZP_X));
                        break;
                    case 0xac:
                        ld(&reg_y, get_code_op(MODE_ABS));
                        break;
                    case 0xbc:
                        ld(&reg_y, get_code_op(MODE_ABS_X));
                        break;
                    
                    /* LSR */
                    case 0x4a:
                        reg_a = lsr(reg_a);
                        break;
                    case 0x46:
                        rmw_mem(lsr, get_code_addr(MODE_ZP));
                        break;
                    case 0x56:
                        rmw_mem(lsr, get_code_addr(MODE_ZP_X));
                        break;
                    case 0x4e:
                        rmw_mem(lsr, get_code_addr(MODE_ABS));
                        break;
                    case 0x5e:
                        rmw_mem(lsr, get_code_addr(MODE_ABS_X));
                        break;
                    
                    /* ORA */
                    case 0x09:
                        ora(get_code_byte());
                        break;
                    case 0x05:
                        ora(get_code_op(MODE_ZP));
                        break;
                    case 0x15:
                        ora(get_code_op(MODE_ZP_X));
                        break;
                    case 0x0d:
                        ora(get_code_op(MODE_ABS));
                        break;
                    case 0x1d:
                        ora(get_code_op(MODE_ABS_X));
                        break;
                    case 0x19:
                        ora(get_code_op(MODE_ABS_Y));
                        break;
                    case 0x01:
                        ora(get_code_op(MODE_IND_X));
                        break;
                    case 0x11:
                        ora(get_code_op(MODE_IND_Y));
                        break;
                    
                    /* PHx/PLx */
                    case 0x48:
                        push(reg_a);
                        break;
                    case 0x08:
                        push(reg_p);
                        break;
                    case 0x68:
                        set_nz(reg_a = pull());
                        break;
                    case 0x28:
                        reg_p = pull();
                        break;
                    
                    /* ROL */
                    case 0x2a:
                        reg_a = rol(reg_a);
                        break;
                    case 0x26:
                        rmw_mem(rol, get_code_addr(MODE_ZP));
                        break;
                    case 0x36:
                        rmw_mem(rol, get_code_addr(MODE_ZP_X));
                        break;
                    case 0x2e:
                        rmw_mem(rol, get_code_addr(MODE_ABS));
                        break;
                    case 0x3e:
                        rmw_mem(rol, get_code_addr(MODE_ABS_X));
                        break;
                    
                    /* ROR */
                    case 0x6a:
                        reg_a = ror(reg_a);
                        break;
                    case 0x66:
                        rmw_mem(ror, get_code_addr(MODE_ZP));
                        break;
                    case 0x76:
                        rmw_mem(ror, get_code_addr(MODE_ZP_X));
                        break;
                    case 0x6e:
                        rmw_mem(ror, get_code_addr(MODE_ABS));
                        break;
                    case 0x7e:
                        rmw_mem(ror, get_code_addr(MODE_ABS_X));
                        break;
                    
                    /* RTI */
                    case 0x40:
                        reg_p = pull();
                        reg_pc = pull() | (pull() << 8);
                        break;
                    
                    /* RTS */
                    case 0x60:
                        reg_pc = (pull() | (pull() << 8)) + 1;
                        break;
                    
                    /* SBC */
                    case 0xe9:
                    case 0xeb:
                        adc(~get_code_byte());
                        break;
                    case 0xe5:
                        adc(~get_code_op(MODE_ZP));
                        break;
                    case 0xf5:
                        adc(~get_code_op(MODE_ZP_X));
                        break;
                    case 0xed:
                        adc(~get_code_op(MODE_ABS));
                        break;
                    case 0xfd:
                        adc(~get_code_op(MODE_ABS_X));
                        break;
                    case 0xf9:
                        adc(~get_code_op(MODE_ABS_Y));
                        break;
                    case 0xe1:
                        adc(~get_code_op(MODE_IND_X));
                        break;
                    case 0xf1:
                        adc(~get_code_op(MODE_IND_Y));
                        break;
                    
                    /* SEC */
                    case 0x38:
                        reg_p |= 1;
                        break;
                    
                    /* STA */
                    case 0x85:
                        write(get_code_addr(MODE_ZP), reg_a);
                        break;
                    case 0x95:
                        write(get_code_addr(MODE_ZP_X), reg_a);
                        break;
                    case 0x8d:
                        write(get_code_addr(MODE_ABS), reg_a);
                        break;
                    case 0x9d:
                        write(get_code_addr(MODE_ABS_X), reg_a);
                        break;
                    case 0x99:
                        write(get_code_addr(MODE_ABS_Y), reg_a);
                        break;
                    case 0x81:
                        write(get_code_addr(MODE_IND_X), reg_a);
                        break;
                    case 0x91:
                        write(get_code_addr(MODE_IND_Y), reg_a);
                        break;
                    
                    /* STX */
                    case 0x86:
                        write(get_code_addr(MODE_ZP), reg_x);
                        break;
                    case 0x96:
                        write(get_code_addr(MODE_ZP_Y), reg_x);
                        break;
                    case 0x8e:
                        write(get_code_addr(MODE_ABS), reg_x);
                        break;
                    
                    /* STY */
                    case 0x84:
                        write(get_code_addr(MODE_ZP), reg_y);
                        break;
                    case 0x94:
                        write(get_code_addr(MODE_ZP_X), reg_y);
                        break;
                    case 0x8c:
                        write(get_code_addr(MODE_ABS), reg_y);
                        break;
                    
                    /* register transfer */
                    case 0xaa:
                        ld(&reg_x, reg_a);
                        break;
                    case 0xa8:
                        ld(&reg_y, reg_a);
                        break;
                    case 0xba:
                        ld(&reg_x, reg_s);
                        break;
                    case 0x8a:
                        ld(&reg_a, reg_x);
                        break;
                    case 0x9a:
                        ld(&reg_s, reg_x);
                        break;
                    case 0x98:
                        ld(&reg_a, reg_y);
                        break;
                    
                    /* single-byte NOPs */
                    case 0xea:
                    case 0x1a:
                    case 0x3a:
                    case 0x5a:
                    case 0x7a:
                    case 0xda:
                    case 0xfa:
                    case 0xd8: /* CLD */
                    case 0x58: /* CLI */
                    case 0xf8: /* SED */
                    case 0x78: /* SEI */
                        break;
                    /* double-byte NOPs */
                    case 0x80:
                    case 0x82:
                    case 0x89:
                    case 0xc2:
                    case 0xe2:
                    case 0x04:
                    case 0x44:
                    case 0x64:
                    case 0x14:
                    case 0x34:
                    case 0x54:
                    case 0x74:
                    case 0xd4:
                    case 0xf4:
                        get_code_byte();
                        break;
                    /* triple-byte NOPs */
                    case 0x0c:
                    case 0x1c:
                    case 0x3c:
                    case 0x5c:
                    case 0x7c:
                    case 0xdc:
                    case 0xfc:
                        get_code_word();
                        break;
                    
                    
                    /* ----- illegals ----- */
                    
                    /* ANC */
                    case 0x0b:
                    case 0x2b:
                        set_nz(reg_a &= get_code_byte());
                        set_c(reg_a & 0x80);
                        break;
                    
                    /* ASR */
                    case 0x4b:
                    {
                        uint8_t v = get_code_byte();
                        and(v);
                        reg_a = lsr(reg_a);
                        break;
                    }
                    
                    /* DCP */
                    case 0xc7:
                        dcp(get_code_addr(MODE_ZP));
                        break;
                    case 0xd7:
                        dcp(get_code_addr(MODE_ZP_X));
                        break;
                    case 0xcf:
                        dcp(get_code_addr(MODE_ABS));
                        break;
                    case 0xdf:
                        dcp(get_code_addr(MODE_ABS_X));
                        break;
                    case 0xdb:
                        dcp(get_code_addr(MODE_ABS_Y));
                        break;
                    case 0xc3:
                        dcp(get_code_addr(MODE_IND_X));
                        break;
                    case 0xd3:
                        dcp(get_code_addr(MODE_IND_Y));
                        break;
                    
                    /* ISB */
                    case 0xe7:
                        isb(get_code_addr(MODE_ZP));
                        break;
                    case 0xf7:
                        isb(get_code_addr(MODE_ZP_X));
                        break;
                    case 0xef:
                        isb(get_code_addr(MODE_ABS));
                        break;
                    case 0xff:
                        isb(get_code_addr(MODE_ABS_X));
                        break;
                    case 0xfb:
                        isb(get_code_addr(MODE_ABS_Y));
                        break;
                    case 0xe3:
                        isb(get_code_addr(MODE_IND_X));
                        break;
                    case 0xf3:
                        isb(get_code_addr(MODE_IND_Y));
                        break;
                    
                    /* LAX */
                    case 0xa7:
                        reg_x = ld(&reg_a, get_code_op(MODE_ZP));
                        break;
                    case 0xb7:
                        reg_x = ld(&reg_a, get_code_op(MODE_ZP_Y));
                        break;
                    case 0xaf:
                        reg_x = ld(&reg_a, get_code_op(MODE_ABS));
                        break;
                    case 0xbf:
                        reg_x = ld(&reg_a, get_code_op(MODE_ABS_Y));
                        break;
                    case 0xa3:
                        reg_x = ld(&reg_a, get_code_op(MODE_IND_X));
                        break;
                    case 0xb3:
                        reg_x = ld(&reg_a, get_code_op(MODE_IND_Y));
                        break;
                    
                    /* RLA */
                    case 0x27:
                        rla(get_code_addr(MODE_ZP));
                        break;
                    case 0x37:
                        rla(get_code_addr(MODE_ZP_X));
                        break;
                    case 0x2f:
                        rla(get_code_addr(MODE_ABS));
                        break;
                    case 0x3f:
                        rla(get_code_addr(MODE_ABS_X));
                        break;
                    case 0x3b:
                        rla(get_code_addr(MODE_ABS_Y));
                        break;
                    case 0x23:
                        rla(get_code_addr(MODE_IND_X));
                        break;
                    case 0x33:
                        rla(get_code_addr(MODE_IND_Y));
                        break;
                    
                    /* RRA */
                    case 0x67:
                        rra(get_code_addr(MODE_ZP));
                        break;
                    case 0x77:
                        rra(get_code_addr(MODE_ZP_X));
                        break;
                    case 0x6f:
                        rra(get_code_addr(MODE_ABS));
                        break;
                    case 0x7f:
                        rra(get_code_addr(MODE_ABS_X));
                        break;
                    case 0x7b:
                        rra(get_code_addr(MODE_ABS_Y));
                        break;
                    case 0x63:
                        rra(get_code_addr(MODE_IND_X));
                        break;
                    case 0x73:
                        rra(get_code_addr(MODE_IND_Y));
                        break;
                    
                    /* SAX */
                    case 0x87:
                        write(get_code_addr(MODE_ZP), reg_a & reg_x);
                        break;
                    case 0x97:
                        write(get_code_addr(MODE_ZP_Y), reg_a & reg_x);
                        break;
                    case 0x8f:
                        write(get_code_addr(MODE_ABS), reg_a & reg_x);
                        break;
                    case 0x83:
                        write(get_code_addr(MODE_IND_X), reg_a & reg_x);
                        break;
                    
                    /* SBX */
                    case 0xcb:
                    {
                        uint16_t diff = (reg_x & reg_a) - get_code_byte();
                        set_c(!(diff > 0xff));
                        reg_x = set_nz(diff & 0xff);
                    }
                    
                    /* SLO */
                    case 0x07:
                        slo(get_code_addr(MODE_ZP));
                        break;
                    case 0x17:
                        slo(get_code_addr(MODE_ZP_X));
                        break;
                    case 0x0f:
                        slo(get_code_addr(MODE_ABS));
                        break;
                    case 0x1f:
                        slo(get_code_addr(MODE_ABS_X));
                        break;
                    case 0x1b:
                        slo(get_code_addr(MODE_ABS_Y));
                        break;
                    case 0x03:
                        slo(get_code_addr(MODE_IND_X));
                        break;
                    case 0x13:
                        slo(get_code_addr(MODE_IND_Y));
                        break;
                    
                    /* SRE */
                    case 0x47:
                        sre(get_code_addr(MODE_ZP));
                        break;
                    case 0x57:
                        sre(get_code_addr(MODE_ZP_X));
                        break;
                    case 0x4f:
                        sre(get_code_addr(MODE_ABS));
                        break;
                    case 0x5f:
                        sre(get_code_addr(MODE_ABS_X));
                        break;
                    case 0x5b:
                        sre(get_code_addr(MODE_ABS_Y));
                        break;
                    case 0x43:
                        sre(get_code_addr(MODE_IND_X));
                        break;
                    case 0x53:
                        sre(get_code_addr(MODE_IND_Y));
                        break;
                    
                    default:
                        printf("Unsupported opcode $%02X encountered at $%04X\n", opcode, reg_pc-1);
                        goto fail;
                }
            }
            
        }
        
        /* export samples */
        if (samplec)
        {
            size_t fnamlen = strlen(fnam);
            char *outnambuf = malloc(fnamlen + 0x20);
            memcpy(outnambuf, fnam, fnamlen);
            
            for (int s = 0; s < samplec; s++)
            {
                memcpy(bank_cfg+6, samplev[s].bank_cfg, 4);
                uint8_t addr = samplev[s].addr;
                uint16_t actual_addr = addr * 0x40 + 0xc000;
                uint8_t len = samplev[s].len;
                uint16_t actual_len = len * 0x10 + 1;
                
                printf("Found sample %i at $%02X:$%04X\n", s, bank_cfg[(actual_addr >> 12) - 0xc + 6], actual_addr);
                
                FILE* sample_f = NULL;
                FILE* rev_sample_f = NULL;
                
                if (mode & (1 << MODE_RIP))
                {
                    sprintf(outnambuf+fnamlen, "-%i.dmc", s);
                    sample_f = fopen(outnambuf, "wb");
                }
                if (mode & (1 << MODE_RIP_REVERSE))
                {
                    sprintf(outnambuf+fnamlen, "-%i-reversed.dmc", s);
                    rev_sample_f = fopen(outnambuf, "wb");
                }
                
                for (uint16_t a = actual_addr; a != actual_addr+actual_len; a++)
                {
                    unsigned int i = get_banked_data_index(a);
                    uint8_t sb = banked_data[i];
                    if (mode & (1 << MODE_RIP)) fputc(sb, sample_f);
                    if (mode & (1 << MODE_RIP_REVERSE)) fputc(bit_reverse[sb], rev_sample_f);
                    
                    /* reverse sample in nsf data for potential later export */
                    if (!banked_data_ctx[i])
                    {
                        unsigned int nsfindex = i - load_bank_offs;
                        if (nsfindex < datasize)
                        {
                            nsfbuf[nsfindex + 0x80] = bit_reverse[nsfbuf[nsfindex + 0x80]];
                        }
                    }
                    
                    banked_data_ctx[i] |= 1 << CTX_DPCM_DONE;
                }
                
                fclose(sample_f);
                fclose(rev_sample_f);
            }
            
            /* export nsf if requested */
            if (mode & (1 << MODE_NSF_REVERSE))
            {
                sprintf(outnambuf+fnamlen, "-reversed.nsf");
                FILE *f = fopen(outnambuf, "wb");
                fwrite(nsfbuf, 1, nsfsize, f);
                fclose(f);
            }
            
            free(outnambuf);
        }
        else
        {
            puts("No samples found");
        }
        
fail:   free(banked_data);
        free(banked_data_ctx);
        free(samplev);
        free(nsfbuf);
        if (file != infilec-1) fputc('\n', stdout);
    }
    
    
    return EXIT_SUCCESS;
}



