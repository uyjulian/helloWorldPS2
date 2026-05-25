IOP_BIN		= dvrrelay.irx
IOP_OBJS	= dvrrelay.o imports.o

all: $(IOP_BIN)

run: all
	ps2client -h 192.168.0.10 -t 1 execiop host:$(IOP_BIN)
reset: clean
	ps2client -h 192.168.0.10 reset

clean:
	rm -f -r $(IOP_OBJS_DIR) $(IOP_BIN) *.elf

include $(PS2SDK)/Defs.make
include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.iopglobal

