.SUFFIXES:
override OUTPUT := myos

TOOLCHAIN := 
TOOLCHAIN_PREFIX := 
ifneq ($(TOOLCHAIN),)
	ifeq ($(TOOLCHAIN_PREFIX),)
		TOOLCHAIN_PREFIX := $(TOOLCHAIN)-
	endif
endif

ifneq ($(TOOLCHAIN_PREFIX),)
	CC := $(TOOLCHAIN_PREFIX)gcc
else
	CC := cc
endif

LD := $(TOOLCHAIN_PREFIX)ld

ifeq ($(TOOLCHAIN),llvm)
	CC := clang
	LD := ld.lld
endif

CFLAGS := -g -O2 -pipe

CPPFLAGS :=

NASMFLAGS := -g

LDFLAGS := 

override CC_IS_CLANG := $(shell ! $(CC) --version 2>/dev/null | grep -q '^Target: '; echo $$?)

ifeq ($(CC_IS_CLANG),1)
	override CC += \
		-target x86_64-unknown-none-elf
endif

override CFLAGS += \
	-Wall \
	-Wextra \
	-std=gnu11 \
	-ffreestanding \
	-fno-stack-protector \
	-fno-stack-check \
	-fno-lto \
	-fno-PIC \
	-ffunction-sections \
	-fdata-sections \
	-m64 \
	-march=x86-64 \
	-mabi=sysv \
	-mno-80387 \
	-mno-mmx \
	-mno-sse \
	-mno-sse2 \
	-mno-red-zone \
	-mcmodel=kernel 


override CPPFLAGS := \
	-I src \
	$(CPPFLAGS) \
	-DLIMINE_API_REVISION=4 \
	-MMD \
	-MP

override NASMFLAGS := \
	-f elf64 \
	$(patsubst -g,-g -F dwarf,$(NASMFLAGS)) \
	-Wall

override LDFLAGS += \
	-m elf_x86_64 \
	-nostdlib \
	-static \
	-z max-page-size=0x1000 \
	--gc-sections \
	-T linker.lds

override SRCFILES := $(shell find -L src -type f 2>/dev/null | LC_ALL=C sort)
override CFILES := $(filter %.c,$(SRCFILES))
override ASFILES := $(filter %.S,$(SRCFILES))
override NASMFILES := $(filter %.asm,$(SRCFILES))
override OBJ := $(addprefix obj/,$(CFILES:.c=.c.o) $(ASFILES:.S=.S.o) $(NASMFILES:.asm=.asm.o))
override HEADER_DEPS := $(addprefix obj/,$(CFILES:.c=.c.d) $(ASFILES:.S=.S.d))

.PHONY: all
all: bin/$(OUTPUT)

-include $(HEADER_DEPS)

bin/$(OUTPUT): GNUmakefile linker.lds $(OBJ)
	mkdir -p "$(dir $@)"
	$(LD) $(LDFLAGS) $(OBJ) -o $@

obj/%.c.o: %.c GNUmakefile
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

obj/%.S.o: %.S GNUmakefile
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

obj/%.asm.o: %.asm GNUmakefile
	mkdir -p "$(dir $@)"
	nasm $(NASMFLAGS) $< -o $@

IMAGE_FILE := image.hdd

limine/limine:
	@echo "Checking for Limine..."
	@if [ ! -d "limine" ]; then \
		echo "Cloning Limine..."; \
		git clone https://codeberg.org/Limine/Limine.git limine --branch=v10.x-binary --depth=1; \
	else \
		echo "Limine directory already exists, skipping clone."; \
	fi
	@echo "Building Limine utility..."
	@$(MAKE) -C limine

.PHONY: image
image: bin/$(OUTPUT) limine/limine limine.conf
	@echo "Creating disk image $(IMAGE_FILE)..."
	@dd if=/dev/zero bs=1M count=64 of=$(IMAGE_FILE)
	@PATH=$(PATH):/usr/sbin:/sbin sgdisk $(IMAGE_FILE) -n 1:2048 -t 1:ef00 -m 1
	@./limine/limine bios-install $(IMAGE_FILE)
	@mformat -i $(IMAGE_FILE)@@1M
	@mmd -i $(IMAGE_FILE)@@1M ::/EFI
	@mmd -i $(IMAGE_FILE)@@1M ::/EFI/BOOT
	@mmd -i $(IMAGE_FILE)@@1M ::/boot
	@mmd -i $(IMAGE_FILE)@@1M ::/boot/limine
	@echo "Copying files to image..."
	@mcopy -i $(IMAGE_FILE)@@1M bin/$(OUTPUT) ::/boot
	@mcopy -i $(IMAGE_FILE)@@1M limine.conf limine/limine-bios.sys ::/boot/limine
	@mcopy -i $(IMAGE_FILE)@@1M limine/BOOTX64.EFI ::/EFI/BOOT
	@mcopy -i $(IMAGE_FILE)@@1M limine/BOOTIA32.EFI ::/EFI/BOOT
	@echo "Image $(IMAGE_FILE) created successfully."


.PHONY: run
run: image
	@echo "Booting $(IMAGE_FILE) with QEMU..."
	@qemu-system-x86_64 -hda $(IMAGE_FILE)

.PHONY: debug
debug: image
	@echo "Booting $(IMAGE_FILE) with QEMU for GDB debugging..."
	@echo "Waiting for GDB to connect on port 1234 (run: gdb -ex 'target remote :1234' bin/myos)"
	@qemu-system-x86_64 -hda $(IMAGE_FILE) -S -s


.PHONY: clean
clean:
	@echo "Cleaning build artifacts..."
	@rm -rf bin obj $(IMAGE_FILE)
	@echo "Cleaning Limine directory..."
	@rm -rf limine