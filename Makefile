# _____     ___ ____     ___ ____
#  ____|   |    ____|   |        | |____|
# |     ___|   |____ ___|    ____| |    \    PS2DEV Open Source Project.
#-----------------------------------------------------------------------
# Copyright 2001-2004, ps2dev - http://www.ps2dev.org
# Licenced under Academic Free License version 2.0
# Review ps2sdk README & LICENSE files for further details.

EE_BIN = pad_example.elf
EE_OBJS = pad.o iomanX_irx.o filexio_irx.o usbd_irx.o bdm_irx.o bdmfs_fatfs_irx.o usbmass_bd_irx.o ps2mouse_irx.o rmman_irx.o rmman2_irx.o
EE_LIBS = -lpad -lmouse -lc -ldebug -lpatches -lfileXio -lrm

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

usbd_irx.c: $(PS2SDK)/iop/irx/usbd.irx
	$(BIN2C) $< $@ usbd_irx

bdm_irx.c: $(PS2SDK)/iop/irx/bdm.irx
	$(BIN2C) $< $@ bdm_irx

bdmfs_fatfs_irx.c: $(PS2SDK)/iop/irx/bdmfs_fatfs.irx
	$(BIN2C) $< $@ bdmfs_fatfs_irx

usbmass_bd_irx.c: $(PS2SDK)/iop/irx/usbmass_bd.irx
	$(BIN2C) $< $@ usbmass_bd_irx

ps2mouse_irx.c: $(PS2SDK)/iop/irx/ps2mouse.irx
	$(BIN2C) $< $@ ps2mouse_irx

rmman_irx.c: $(PS2SDK)/iop/irx/rmman.irx
	$(BIN2C) $< $@ rmman_irx

rmman2_irx.c: $(PS2SDK)/iop/irx/rmman2.irx
	$(BIN2C) $< $@ rmman2_irx

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
