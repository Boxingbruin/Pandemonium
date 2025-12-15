BUILD_DIR=build

include $(N64_INST)/include/n64.mk
include $(T3D_INST)/t3d.mk

SRCDIR = src
INCDIR = include
ASSDIR = assets
OBJDIR = build
FILESYSTEMDIR = filesystem

TARGET = pandemonium

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

# Convert asset paths to filesystem output paths, preserving subdirs
ASSETSCONV = $(patsubst $(ASSDIR)/%.png,$(FILESYSTEMDIR)/%.sprite,$(assets_png)) \
	$(patsubst $(ASSDIR)/%.xm,$(FILESYSTEMDIR)/%.xm64,$(assets_xm)) \
	$(patsubst $(ASSDIR)/%.ttf,$(FILESYSTEMDIR)/%.font64,$(assets_ttf)) \
	$(patsubst $(ASSDIR)/%.glb,$(FILESYSTEMDIR)/%.t3dm,$(assets_gltf)) \
	$(patsubst $(ASSDIR)/%.wav,$(FILESYSTEMDIR)/%.wav64,$(assets_wav)) \
	$(patsubst $(ASSDIR)/%.bin,$(FILESYSTEMDIR)/%.bin,$(assets_bin))

CODEFILES   =  $(wildcard $(SRCDIR)/*.c) $(wildcard $(SRCDIR)/*/*.c)
CODEOBJECTS = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(CODEFILES))

AUDIOCONV_FLAGS ?=

all: pandemonium.z64

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
	@echo "    [SPRITE] $@"
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

$(FILESYSTEMDIR)/%.wav64: $(ASSDIR)/%.wav
	@mkdir -p $(dir $@)
	@echo "    [AUDIO] $@"
	@$(N64_AUDIOCONV) --wav-compress 1,bits=2 --wav-resample 22050 -v -o $(dir $@) $<

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

pandemonium.z64: N64_ROM_TITLE="Pandemonium"
pandemonium.z64: $(BUILD_DIR)/$(TARGET).dfs $(BUILD_DIR)/$(TARGET).elf

clean:
	rm -rf $(BUILD_DIR) *.z64
	rm -rf $(FILESYSTEMDIR)

versioned: $(VERSIONED_ROM)

build_lib:
	rm -rf $(BUILD_DIR) *.z64
	make -C $(T3D_INST)
	make all

-include $(wildcard $(BUILD_DIR)/*.d)

.PHONY: all clean versioned