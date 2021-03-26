ifdef COMSPEC
DOTEXE:=.exe
else
DOTEXE:=
endif
EXENAME:=dpcmtool$(DOTEXE)

.PHONY: all
all: $(EXENAME)

$(EXENAME): dpcmtool.c
	$(CC) $^ -Wall -Ofast -s -o $@