# Build both PRU images against TI's PRU Software Support Package.
# PRU_CGT and PSSP are set by beagle-linux/scripts/build.sh.

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
	--include_path=$(ROOT)/zephyr-m4/include

.PHONY: all clean core
all: $(BUILD)/am62x-pru0-fw $(BUILD)/am62x-pru1-fw

$(BUILD)/am62x-pru0-fw: main.c
	$(MAKE) core CORE=0 OUT=$@ \
		LINK=$(PSSP)/examples/am62x/PRU_RPMsg_Echo_Interrupt0/AM62x_PRU0_intc_rscTbl.cmd \
		EXDIR=$(PSSP)/examples/am62x/PRU_RPMsg_Echo_Interrupt0

$(BUILD)/am62x-pru1-fw: main.c AM62x_PRU1.cmd
	$(MAKE) core CORE=1 OUT=$@ \
		LINK=AM62x_PRU1.cmd \
		EXDIR=$(PSSP)/examples/am62x/PRU_RPMsg_Echo_Interrupt1

core:
	mkdir -p $(BUILD)/core$(CORE)
	$(PRU_CGT)/bin/clpru --include_path=$(PRU_CGT)/include $(INCLUDE) \
		--include_path=$(EXDIR) $(CFLAGS) -DPRU_CORE=$(CORE) \
		--obj_directory=$(BUILD)/core$(CORE) \
		-fe $(BUILD)/core$(CORE)/main.object main.c
	$(PRU_CGT)/bin/clpru $(CFLAGS) -z -i$(PRU_CGT)/lib -i$(PRU_CGT)/include \
		$(LFLAGS) -o $(OUT) $(BUILD)/core$(CORE)/main.object \
		-m$(OUT).map $(LINK) --library=libc.a \
		$(if $(filter 0,$(CORE)),--library=$(PSSP)/lib/rpmsg_lib.lib,)

clean:
	rm -rf $(BUILD)
