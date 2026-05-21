# _____     ___ ____     ___ ____
#  ____|   |    ____|   |        | |____|
# |     ___|   |____ ___|    ____| |    \    PS2DEV Open Source Project.
#-----------------------------------------------------------------------
# Copyright 2001-2004, ps2dev - http://www.ps2dev.org
# Licenced under Academic Free License version 2.0
# Review ps2sdk README & LICENSE files for further details.

EE_BIN = pad_example.elf
EE_OBJS = pad.o iomanX_irx.o filexio_irx.o mcman_irx.o mcman_old_irx.o mcserv_irx.o mcserv_old_irx.o sio2man_irx.o sio2man_old_irx.o dev9_irx.o extflash_irx.o xfromman_irx.o poweroff_irx.o
EE_LIBS = -lpoweroff -lmc -lc -ldebug -lpatches -lfileXio

all: $(EE_BIN)

clean:
	rm -f $(EE_BIN) $(EE_OBJS) *_irx.c

run: $(EE_BIN)
	ps2client execee host:$(EE_BIN)

reset:
	ps2client reset

BIN2C=$(PS2SDK)/bin/bin2c

iomanX_irx.c: $(PS2SDK)/iop/irx/iomanX.irx
	$(BIN2C) $< $@ iomanX_irx

filexio_irx.c: $(PS2SDK)/iop/irx/fileXio.irx
	$(BIN2C) $< $@ filexio_irx

mcman_irx.c: $(PS2SDK)/iop/irx/mcman.irx
	$(BIN2C) $< $@ mcman_irx

mcman_old_irx.c: $(PS2SDK)/iop/irx/mcman-old.irx
	$(BIN2C) $< $@ mcman_old_irx

mcserv_irx.c: $(PS2SDK)/iop/irx/mcserv.irx
	$(BIN2C) $< $@ mcserv_irx

mcserv_old_irx.c: $(PS2SDK)/iop/irx/mcserv-old.irx
	$(BIN2C) $< $@ mcserv_old_irx

sio2man_irx.c: $(PS2SDK)/iop/irx/sio2man.irx
	$(BIN2C) $< $@ sio2man_irx

sio2man_old_irx.c: $(PS2SDK)/iop/irx/sio2man-old.irx
	$(BIN2C) $< $@ sio2man_old_irx

dev9_irx.c: $(PS2SDK)/iop/irx/ps2dev9.irx
	$(BIN2C) $< $@ dev9_irx

extflash_irx.c: $(PS2SDK)/iop/irx/extflash.irx
	$(BIN2C) $< $@ extflash_irx

xfromman_irx.c: $(PS2SDK)/iop/irx/xfromman.irx
	$(BIN2C) $< $@ xfromman_irx

poweroff_irx.c: $(PS2SDK)/iop/irx/poweroff.irx
	$(BIN2C) $< $@ poweroff_irx

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
