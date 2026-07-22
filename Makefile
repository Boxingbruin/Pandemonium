BUILD_DIR=build

include $(N64_INST)/include/n64.mk
include $(T3D_INST)/t3d.mk

SRCDIR = src
INCDIR = include
ASSDIR = assets
OBJDIR = build
FILESYSTEMDIR = filesystem

TARGET = pandemonium

# Keep in sync with N64_ROM_TITLE below (ROM display name / Advanced Homebrew header).
ROM_TITLE := Pandemonium

# Read version from VERSION file for versioned builds
VERSION := $(strip $(shell cat VERSION))

# Find all subdirectories in src and add them to include paths
INCLUDES = $(shell find $(SRCDIR) -type d)
N64_CFLAGS += -std=gnu2x $(foreach dir,$(INCLUDES),-I$(dir)) -I$(INCDIR)

# Find all asset files (excluding unwanted extensions)
asset_files = $(shell find $(ASSDIR) -type f \
	! -name '*.blend' \
	! -name '*.blend1' \
	! -name '*.psd' \
	! -name '*.txt' \
	! -name '*.log' \
	! -name '*.zip')

# Pattern rules for each asset type, preserving subdirectory structure
assets_xm = $(filter %.xm,$(asset_files))
assets_png = $(filter %.png,$(asset_files))
assets_gltf = $(filter %.glb,$(asset_files))
assets_wav = $(filter %.wav,$(asset_files))
assets_ttf = $(filter %.ttf,$(asset_files))
assets_bin = $(filter %.bin,$(asset_files))
assets_h264 = $(filter %.h264,$(asset_files))
assets_mp4 = $(filter %.mp4,$(asset_files))

# Convert asset paths to filesystem output paths, preserving subdirs
ASSETSCONV = $(patsubst $(ASSDIR)/%.png,$(FILESYSTEMDIR)/%.sprite,$(assets_png)) \
	$(patsubst $(ASSDIR)/%.xm,$(FILESYSTEMDIR)/%.xm64,$(assets_xm)) \
	$(patsubst $(ASSDIR)/%.ttf,$(FILESYSTEMDIR)/%.font64,$(assets_ttf)) \
	$(patsubst $(ASSDIR)/%.glb,$(FILESYSTEMDIR)/%.t3dm,$(assets_gltf)) \
	$(patsubst $(ASSDIR)/%.wav,$(FILESYSTEMDIR)/%.wav64,$(assets_wav)) \
	$(patsubst $(ASSDIR)/%.bin,$(FILESYSTEMDIR)/%.bin,$(assets_bin)) \
	$(patsubst $(ASSDIR)/%.h264,$(FILESYSTEMDIR)/%.h264,$(assets_h264)) \
	$(patsubst $(ASSDIR)/%.mp4,$(FILESYSTEMDIR)/%.h264,$(assets_mp4))

# Selective mipmaps
# Only these PNGs will be converted with mksprite --mipmap BOX.
#
MIPMAP_PNGS := \
	$(ASSDIR)/boss_room/floor4.i8.png \
	#$(ASSDIR)/boss_room/floor_ornate10.i4.png \
	$(ASSDIR)/boss_room/carpet_border7.ci8.png \
	$(ASSDIR)/boss_room/floor_debris_pile4.i4.png

MIPMAP_SPRITES := $(patsubst $(ASSDIR)/%.png,$(FILESYSTEMDIR)/%.sprite,$(MIPMAP_PNGS))

$(MIPMAP_SPRITES): MKSPRITE_FLAGS += --mipmap BOX

# TODO:
CODEFILES   =  $(shell find $(SRCDIR) -name "*.c" ! -path "$(SRCDIR)/objects/boss.c")
CODEOBJECTS = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(CODEFILES))

AUDIOCONV_FLAGS ?=

# Collision exporter Python venv (avoids macOS PEP 668 "externally managed" pip errors)
COLLISION_VENV := tools/.venv
COLLISION_PY := $(COLLISION_VENV)/bin/python3
COLLISION_DEPS := tools/requirements-collision.txt
COLLISION_STAMP := $(COLLISION_VENV)/.collision_deps_installed

all:
# Measure build time
	@START_MS=$$(python3 -c 'import time; print(int(time.time()*1000))'); \
	$(MAKE) pandemonium.z64; \
	END_MS=$$(python3 -c 'import time; print(int(time.time()*1000))'); \
	ELAPSED_MS=$$((END_MS - START_MS)); \
	if [ $$ELAPSED_MS -gt 0 ]; then \
	  SEC=$$((ELAPSED_MS / 1000)); \
	  MS=$$((ELAPSED_MS % 1000)); \
	  printf "Build duration: %d.%03d seconds\n" $$SEC $$MS; \
	fi

# Versioned build target
versioned: pandemonium.z64
	@echo "Creating versioned ROM: Pandemonium-$(VERSION).z64"
	@cp pandemonium.z64 Pandemonium-$(VERSION).z64

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(N64_CFLAGS) -c $< -o $@

# Asset conversion rules, preserving subdirectory structure
$(FILESYSTEMDIR)/%.sprite: $(ASSDIR)/%.png
	@mkdir -p $(dir $@)
	@echo "    [SPRITE] $@ $(MKSPRITE_FLAGS)"
	$(N64_MKSPRITE) $(MKSPRITE_FLAGS) -o $(dir $@) "$<"

$(FILESYSTEMDIR)/%.font64: $(ASSDIR)/%.ttf
	@mkdir -p $(dir $@)
	@echo "    [FONT] $@"
	$(N64_MKFONT) $(MKFONT_FLAGS) -o $(dir $@) "$<"

$(FILESYSTEMDIR)/%.t3dm: $(ASSDIR)/%.glb
	@mkdir -p $(dir $@)
	@echo "    [T3D-MODEL] $@"
	$(T3D_GLTF_TO_3D) "$<" $@
	$(N64_BINDIR)/mkasset -c 2 -o $(dir $@) $@

$(COLLISION_STAMP): $(COLLISION_DEPS)
	@echo "    [PY-VENV] $(COLLISION_VENV)"
	@python3 -m venv $(COLLISION_VENV)
	@$(COLLISION_PY) -m pip install --upgrade pip >/dev/null
	@$(COLLISION_PY) -m pip install -r $(COLLISION_DEPS)
	@touch $(COLLISION_STAMP)

$(FILESYSTEMDIR)/%.h264: $(ASSDIR)/%.mp4
	@mkdir -p $(dir $@)
	@echo "    [VIDEO-H264] $@"
	$(N64_BINDIR)/videoconv64 -c h264 -w 320 -r 24 -q 60 --no-audio -o $(dir $@) "$<"

$(FILESYSTEMDIR)/%.h264: $(ASSDIR)/%.h264
	@mkdir -p $(dir $@)
	@echo "    [H264] $@"
	cp $< $@

$(FILESYSTEMDIR)/%.wav64: $(ASSDIR)/%.wav
	@mkdir -p $(dir $@)
	@echo "    [AUDIO] $@"
	@$(N64_AUDIOCONV) -o $(dir $@) $<

$(FILESYSTEMDIR)/%.xm64: $(ASSDIR)/%.xm
	@mkdir -p $(dir $@)
	@echo "    [AUDIO] $@"
	@$(N64_AUDIOCONV) $(AUDIOCONV_FLAGS) -o $(dir $@) $<

$(FILESYSTEMDIR)/%.bin: $(ASSDIR)/%.bin
	@mkdir -p $(dir $@)
	@echo "    [BIN] $@"
	cp $< $@

$(BUILD_DIR)/$(TARGET).dfs: $(ASSETSCONV)
$(BUILD_DIR)/$(TARGET).elf: $(CODEOBJECTS)

pandemonium.z64: N64_ROM_TITLE="$(ROM_TITLE)"
pandemonium.z64: N64_ROM_SAVETYPE=eeprom4k
pandemonium.z64: $(BUILD_DIR)/$(TARGET).dfs $(BUILD_DIR)/$(TARGET).elf
pandemonium.z64: N64_ROM_METADATA=metadata/metadata.ini

deploy: pandemonium.z64
	./scripts/deploy.sh --no-build

# Copy pandemonium.z64 to SC64 SD at Games/Homebrew/Pandemonium.z64 (USB; overwrites)
upload-sd: pandemonium.z64
	./scripts/upload-sd.sh --no-build

# Reset SC64 to normal bootloader / power-up defaults (sc64deployer reset)
sc64-bootloader:
	./scripts/sc64-bootloader.sh

rebuild:
	rm -rf $(BUILD_DIR) *.z64
	rm -rf $(FILESYSTEMDIR)
	make all

clean:
	rm -rf $(BUILD_DIR) *.z64
	rm -rf $(FILESYSTEMDIR)

versioned: $(VERSIONED_ROM)

build_lib:
	rm -rf $(BUILD_DIR) *.z64
	make -C $(T3D_INST)
	make all

-include $(wildcard $(BUILD_DIR)/*.d)

.PHONY: all clean versioned deploy upload-sd sc64-bootloader
