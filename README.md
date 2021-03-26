# dpcmtool

DPCM sample ripper/bit reversal tool for NSF files.

Enter `dpcmtool -?` on the command line for usage information.

The tool works by emulating the 6502 code of the NSF, and logging attempts to play a sample on the DMC channel. With the appropriate command line switches, it can output the following:

* `-r`: The raw DPCM sample data of all samples.
* `-rb`: The bit-reversed DPCM sample data of all samples.
* `-b`: A copy of the NSF with all identified sample bytes bit-reversed.

All output files will be placed in the same directory as the source file, with appropriate suffixes to the file name(s). You can specify multiple inputs and outputs for one run of the program.

Using the `-i num` switch, you can specify how many instructions of the NSF program code to execute before moving onto the next song. The default is 10,000,000, and you can decrease it if the process is too slow for you, but keep in mind that doing this will result in less of each song being analyzed, possibly missing samples. Likewise, if you are aware that a song is very long, you can increase it until you capture all of the samples.

## About bit-reversal

Let's say we have a triangle waveform that looks like this:

```
   /\      /
  /  \    /
 /    \  /
/      \/
```

PCM audio simply encodes the current level of the waveform at a specific interval. If the lowest level is 0, and the highest level is 4, we could encode the above waveform in PCM like this:

`0 1 2 3 4 3 2 1 0 1 2 3 4`

Unlike PCM, DPCM audio encodes the _difference_ between the previous level and the next level. So, in DPCM, this waveform would be encoded like this:

`<initialize to 0> 1 1 1 1 -1 -1 -1 -1 1 1 1 1`

The NES DPCM channel encodes the samples as 1-bit DPCM, where each bit of the data chooses between moving the current level up and down. However, there were some developers which mistakenly encoded the bits within each byte in the wrong order, causing lower sample quality. This problem was brought to attention when [an emulator developer](http://forums.nesdev.com/viewtopic.php?f=2&t=20308) played Double Dribble on a faulty emulator, which accidentally fixed the issue. When the bit order of each byte is reversed, the samples sound much clearer.

## Technical information

The 6502 emulator supports not only the standard opcodes but also most illegals, aside from the "unstable" ones. So, it should work with practically all NSFs out there.

When a sample is found, it displays the location as `$xx:$yyyy`, where $xx is the 4k NSF bank where the sample data was found, and $yyyy is the start address of the sample in the CPU address space. The actual location of the sample in the NSF file can be found with the following formula:

`NSFoffset = ($xx * $1000) + (($yyyy - NSFload) & $fff) + $80`

The 6502 emulator also logs which bytes in the NSF data are code, data, and rewritten (if the FDS sound is used). If you export an NSF, the program will not touch any byte that is used by the program, even if it is in range of one of the samples.

The 6502 emulator operates in instructions, not cycles, so unfortunately it is not possible to support NSF2 files, which can use a cycle timer and IRQs.

Since the CPU is already emulated anyway, this tool could pretty easily be modified to support a number of different features, like outputting a textual code/data log and cropping out unused bytes. I may do it if there is demand, or I find a use for it.