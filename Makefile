# _____     ___ ____     ___ ____
#  ____|   |    ____|   |        | |____|
# |     ___|   |____ ___|    ____| |    \    PS2DEV Open Source Project.
#-----------------------------------------------------------------------
# Copyright 2001-2004, ps2dev - http://www.ps2dev.org
# Licenced under Academic Free License version 2.0
# Review ps2sdk README & LICENSE files for further details.

IOPMODULES_PS2SDK = \
	iomanX \
	fileXio \
	bdm \
	bdmfs_fatfs \
	poweroff \
	ps2dev9 \
	ps2atad \
	ps2hdd \
	dvrdrv \
	dvrfile \
	usbd \
	usbmass_bd

PKGCONFIG_DEPS = libarchive

EE_BIN = hddrework7z.elf
EE_OBJS = ps2ip.o

EE_LIBS = -lpoweroff -lfileXio -ldebug -lpatches -lc -ldebug -lpatches
EE_INCS = -I$(PS2SDK)/ports/include
EE_LDFLAGS = -L$(PS2SDK)/ports/lib

EE_OBJS += $(patsubst %,ps2sdk_%_irx.o,$(IOPMODULES_PS2SDK))

all: $(EE_BIN)

clean:
	rm -f $(EE_BIN) $(EE_OBJS) *_irx.c

run: $(EE_BIN)
	ps2client execee host:$(EE_BIN)

reset:
	ps2client reset

BIN2C=$(PS2SDK)/bin/bin2c

ps2sdk_%_irx.c: $(PS2SDK)/iop/irx/%.irx
	$(BIN2C) $< $@ $(subst -,_,$(subst .,_,$(notdir $<)))

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal

EE_LIBS += $(shell $(EE_TOOL_PREFIX)pkg-config --libs $(PKGCONFIG_DEPS))
EE_CFLAGS += $(shell $(EE_TOOL_PREFIX)pkg-config --cflags $(PKGCONFIG_DEPS))
