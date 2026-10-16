# Build both PRU images against TI's PRU Software Support Package.
# PRU_CGT and PSSP are set by beagle-linux/scripts/build.sh.
# pru/pru0 and pru/pru1 each have their own main.c and linker script.
# -DPRU_CORE only hides the host CRC in pb2_shm.h. It does not pick a source file.

ifeq ($(PRU_CGT),)
$(error PRU_CGT is not set)
endif
ifeq ($(PSSP),)
$(error PSSP is not set)
endif

ROOT := $(abspath ..)
BUILD := build
CFLAGS := -v3 -O2 --display_error_number --endian=little --hardware_mac=on
LFLAGS := --reread_libs --warn_sections --stack_size=0x200 --heap_size=0x100
INCLUDE := --include_path=$(PRU_CGT)/include \
	--include_path=$(PSSP)/include \
	--include_path=$(PSSP)/include/am62x \
	--include_path=$(ROOT)/zephyr/include

.PHONY: all clean core
all: $(BUILD)/am62x-pru0-fw $(BUILD)/am62x-pru1-fw

$(BUILD)/am62x-pru0-fw: pru0/main.c pru0/AM62x_PRU0.cmd
	$(MAKE) core CORE=0 SRC=pru0/main.c OUT=$@ \
		LINK=pru0/AM62x_PRU0.cmd \
		EXDIR=$(PSSP)/examples/am62x/PRU_RPMsg_Echo_Interrupt0 \
		EXTRA_LIBS=--library=$(PSSP)/lib/rpmsg_lib.lib

$(BUILD)/am62x-pru1-fw: pru1/main.c pru1/slip_tx.asm pru1/AM62x_PRU1.cmd
	$(MAKE) core CORE=1 SRC=pru1/main.c OUT=$@ \
		LINK=pru1/AM62x_PRU1.cmd \
		EXDIR=$(PSSP)/examples/am62x/PRU_RPMsg_Echo_Interrupt1 \
		EXTRA_LIBS= \
		ASM=pru1/slip_tx.asm

core:
	mkdir -p $(BUILD)/core$(CORE)
	$(PRU_CGT)/bin/clpru --include_path=$(PRU_CGT)/include $(INCLUDE) \
		--include_path=$(EXDIR) $(CFLAGS) -DPRU_CORE=$(CORE) \
		--obj_directory=$(BUILD)/core$(CORE) \
		-fe $(BUILD)/core$(CORE)/main.object $(SRC)
	$(if $(ASM),$(PRU_CGT)/bin/clpru $(CFLAGS) \
		-fe $(BUILD)/core$(CORE)/slip_tx.object $(ASM),)
	$(PRU_CGT)/bin/clpru $(CFLAGS) -z -i$(PRU_CGT)/lib -i$(PRU_CGT)/include \
		$(LFLAGS) -o $(OUT) $(BUILD)/core$(CORE)/main.object \
		$(if $(ASM),$(BUILD)/core$(CORE)/slip_tx.object,) \
		-m$(OUT).map $(LINK) --library=libc.a $(EXTRA_LIBS)

clean:
	rm -rf $(BUILD)
