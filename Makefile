# _____     ___ ____     ___ ____
#  ____|   |    ____|   |        | |____|
# |     ___|   |____ ___|    ____| |    \    PS2DEV Open Source Project.
#-----------------------------------------------------------------------
# Copyright 2001-2004, ps2dev - http://www.ps2dev.org
# Licenced under Academic Free License version 2.0
# Review ps2sdk README & LICENSE files for further details.

EE_BIN = pad_example.elf
EE_OBJS = pad.o poweroff_irx.o
EE_LIBS = -lpad -lc -ldebug -lpatches -lpoweroff

all: $(EE_BIN)

clean:
	$(MAKE) -C poweroff clean
	rm -f $(EE_BIN) $(EE_OBJS) *_irx.c

run: $(EE_BIN)
	ps2client execee host:$(EE_BIN)

reset:
	ps2client reset

BIN2C=$(PS2SDK)/bin/bin2c

poweroff/poweroff.irx: poweroff
	$(MAKE) -C $<

poweroff_irx.c: poweroff/poweroff.irx
	$(BIN2C) $< $@ poweroff_irx


include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
