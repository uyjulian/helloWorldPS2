# _____     ___ ____     ___ ____
#  ____|   |    ____|   |        | |____|
# |     ___|   |____ ___|    ____| |    \    PS2DEV Open Source Project.
#-----------------------------------------------------------------------
# Copyright 2001-2004, ps2dev - http://www.ps2dev.org
# Licenced under Academic Free License version 2.0
# Review ps2sdk README & LICENSE files for further details.

EE_BIN = pad_example.elf
EE_OBJS = pad.o poweroff_irx.o ps2dev9_irx.o atad_irx.o mymodule_irx.o
EE_LIBS = -lc -ldebug -lpatches -lpoweroff

all: $(EE_BIN)

clean:
	rm -f $(EE_BIN) $(EE_OBJS) poweroff_irx.c ps2dev9_irx.c atad_irx.c mymodule_irx.c
	$(MAKE) -C poweroff clean

run: $(EE_BIN)
	ps2client execee host:$(EE_BIN)

reset:
	ps2client reset

BIN2C=$(PS2SDK)/bin/bin2c

poweroff_irx.c: $(PS2SDK)/iop/irx/poweroff.irx
	$(BIN2C) $< $@ poweroff_irx

ps2dev9_irx.c: $(PS2SDK)/iop/irx/ps2dev9.irx
	$(BIN2C) $< $@ ps2dev9_irx

atad_irx.c: $(PS2SDK)/iop/irx/ps2atad.irx
	$(BIN2C) $< $@ atad_irx

poweroff/poweroff.irx: poweroff
	$(MAKE) -C $<

mymodule_irx.c: poweroff/poweroff.irx
	$(BIN2C) $< $@ mymodule_irx



include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
