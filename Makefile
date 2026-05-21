# _____     ___ ____     ___ ____
#  ____|   |    ____|   |        | |____|
# |     ___|   |____ ___|    ____| |    \    PS2DEV Open Source Project.
#-----------------------------------------------------------------------
# Copyright 2001-2004, ps2dev - http://www.ps2dev.org
# Licenced under Academic Free License version 2.0
# Review ps2sdk README & LICENSE files for further details.

EE_BIN = pad_example.elf
EE_OBJS = ps2ip.o dev9_irx.o iomanX_irx.o fileXio_irx.o poweroff_irx.o atad_irx.o hdd_irx.o dvrdrv_irx.o dvrfile_irx.o usbd_irx.o bdm_irx.o bdmfs_fatfs_irx.o usbmass_bd_irx.o
EE_LIBS = -lpoweroff -lfileXio -lnetman -lps2ip -ldebug -lpatches -lc -ldebug -lpatches
EE_INCS = -I$(PS2SDK)/ports/include
EE_LDFLAGS = -L$(PS2SDK)/ports/lib

all: $(EE_BIN)

clean:
	rm -f $(EE_BIN) $(EE_OBJS) *_irx.c

run: $(EE_BIN)
	ps2client execee host:$(EE_BIN)

reset:
	ps2client reset

BIN2C=$(PS2SDK)/bin/bin2c

dev9_irx.c: $(PS2SDK)/iop/irx/ps2dev9.irx
	$(BIN2C) $< $@ DEV9_irx

iomanX_irx.c: $(PS2SDK)/iop/irx/iomanX.irx
	$(BIN2C) $< $@ IOMANX_irx

fileXio_irx.c: $(PS2SDK)/iop/irx/fileXio.irx
	$(BIN2C) $< $@ FILEXIO_irx

poweroff_irx.c: $(PS2SDK)/iop/irx/poweroff.irx
	$(BIN2C) $< $@ POWEROFF_irx

atad_irx.c: $(PS2SDK)/iop/irx/ps2atad.irx
	$(BIN2C) $< $@ ATAD_irx

hdd_irx.c: $(PS2SDK)/iop/irx/ps2hdd.irx
	$(BIN2C) $< $@ HDD_irx

dvrdrv_irx.c: $(PS2SDK)/iop/irx/dvrdrv.irx
	$(BIN2C) $< $@ DVRDRV_irx

dvrfile_irx.c: $(PS2SDK)/iop/irx/dvrfile.irx
	$(BIN2C) $< $@ DVRFILE_irx

usbd_irx.c: $(PS2SDK)/iop/irx/usbd.irx
	$(BIN2C) $< $@ USBD_irx

bdm_irx.c: $(PS2SDK)/iop/irx/bdm.irx
	$(BIN2C) $< $@ BDM_irx

bdmfs_fatfs_irx.c: $(PS2SDK)/iop/irx/bdmfs_fatfs.irx
	$(BIN2C) $< $@ BDMFS_FATFS_irx

usbmass_bd_irx.c: $(PS2SDK)/iop/irx/usbmass_bd.irx
	$(BIN2C) $< $@ USBMASS_BD_irx

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
