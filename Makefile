# _____     ___ ____     ___ ____
#  ____|   |    ____|   |        | |____|
# |     ___|   |____ ___|    ____| |    \    PS2DEV Open Source Project.
#-----------------------------------------------------------------------
# Copyright 2001-2004, ps2dev - http://www.ps2dev.org
# Licenced under Academic Free License version 2.0
# Review ps2sdk README & LICENSE files for further details.

EE_BIN = iperf.elf
EE_OBJS = main.o smap_irx.o netman_irx.o dev9_irx.o iomanX_irx.o fileXio_irx.o poweroff_irx.o
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

smap_irx.c: $(PS2SDK)/iop/irx/smap.irx
	$(BIN2C) $< $@ SMAP_irx

netman_irx.c: $(PS2SDK)/iop/irx/netman.irx
	$(BIN2C) $< $@ NETMAN_irx

dev9_irx.c: $(PS2SDK)/iop/irx/ps2dev9.irx
	$(BIN2C) $< $@ DEV9_irx

iomanX_irx.c: $(PS2SDK)/iop/irx/iomanX.irx
	$(BIN2C) $< $@ IOMANX_irx

fileXio_irx.c: $(PS2SDK)/iop/irx/fileXio.irx
	$(BIN2C) $< $@ FILEXIO_irx

poweroff_irx.c: $(PS2SDK)/iop/irx/poweroff.irx
	$(BIN2C) $< $@ POWEROFF_irx

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
