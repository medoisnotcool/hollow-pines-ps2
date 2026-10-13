EE_BIN  = hollow_pines.elf
EE_OBJS = main_ps2.o gfx_ps2.o game.o render.o
EE_INCS += -I. -I$(PS2DEV)/gsKit/include
EE_LDFLAGS += -L$(PS2DEV)/gsKit/lib
EE_CFLAGS += -O2 -G0 -ffast-math -fsingle-precision-constant -Wall -Wno-misleading-indentation
EE_LIBS = -lgskit -ldmakit -lpad -lc -lkernel -lm

all: $(EE_BIN)

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
