EE_BIN  = hollow_pines.elf
EE_OBJS = main_ps2.o gfx_ps2.o game.o render.o arena.o mp.o net_ps2.o dev9_irx.o netman_irx.o smap_irx.o
EE_INCS += -I. -I$(PS2DEV)/gsKit/include
EE_LDFLAGS += -L$(PS2DEV)/gsKit/lib
EE_CFLAGS += -O2 -G0 -ffast-math -fsingle-precision-constant -Wall -Wno-misleading-indentation
EE_LIBS = -lgskit -ldmakit -lpad -lps2ip -lnetman -lc -lkernel -lm

all: $(EE_BIN)

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal

# ---- network drivers (IRX files from the PS2SDK) are packed into the .elf ----
BIN2C ?= $(PS2SDK)/bin/bin2c
ifeq (,$(wildcard $(BIN2C)))
BIN2C = bin2c
endif
DEV9_IRX   := $(firstword $(wildcard $(PS2SDK)/iop/irx/ps2dev9.irx $(PS2SDK)/iop/irx/dev9.irx))
SMAP_IRX   := $(firstword $(wildcard $(PS2SDK)/iop/irx/smap.irx $(PS2SDK)/iop/irx/ps2smap.irx))
NETMAN_IRX := $(firstword $(wildcard $(PS2SDK)/iop/irx/netman.irx))

dev9_irx.c: $(DEV9_IRX)
	$(BIN2C) $< $@ dev9_irx
netman_irx.c: $(NETMAN_IRX)
	$(BIN2C) $< $@ netman_irx
smap_irx.c: $(SMAP_IRX)
	$(BIN2C) $< $@ smap_irx
