# Hollow Pines for PlayStation 2  --  needs the PS2DEV toolchain (PS2SDK + gsKit).
#   docker run -it --rm -v "$PWD":/src ps2dev/ps2dev:latest sh -c "cd /src && make"
EE_BIN  = hollow_pines.elf
EE_OBJS = src/main_ps2.o src/gfx_ps2.o src/game.o src/render.o
EE_INCS += -Isrc
EE_CFLAGS += -O2 -G0 -ffast-math -fsingle-precision-constant -Wall -Wno-misleading-indentation
EE_LIBS = -lgskit -ldmakit -lpad -lc -lkernel -lm

all: $(EE_BIN)

clean:
	rm -f $(EE_BIN) $(EE_OBJS)

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
