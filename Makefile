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

# Collision export (single-file workflow):
# - Put an Object named "COLLISION" inside assets/bossroom.glb
# - This rule exports only that node into filesystem/bossroom.collision (uncompressed)
# ASSETSCONV += $(FILESYSTEMDIR)/bossroom/bossroom.collision

CODEFILES   =  $(shell find $(SRCDIR) -name "*.c" ! -path "$(SRCDIR)/objects/boss.c")
CODEOBJECTS = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(CODEFILES))

AUDIOCONV_FLAGS ?=

# Collision exporter Python venv (avoids macOS PEP 668 "externally managed" pip errors)
COLLISION_VENV := tools/.venv
COLLISION_PY := $(COLLISION_VENV)/bin/python3
COLLISION_DEPS := tools/requirements-collision.txt
COLLISION_STAMP := $(COLLISION_VENV)/.collision_deps_installed

all:
	@START_TIME=$$(date +%s); \
	$(MAKE) pandemonium.z64; \
	END_TIME=$$(date +%s); \
	ELAPSED=$$((END_TIME - START_TIME)); \
	if [ $$ELAPSED -gt 0 ]; then \
	  echo "Build duration: $${ELAPSED} seconds"; \
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

$(COLLISION_STAMP): $(COLLISION_DEPS)
	@echo "    [PY-VENV] $(COLLISION_VENV)"
	@python3 -m venv $(COLLISION_VENV)
	@$(COLLISION_PY) -m pip install --upgrade pip >/dev/null
	@$(COLLISION_PY) -m pip install -r $(COLLISION_DEPS)
	@touch $(COLLISION_STAMP)

# $(FILESYSTEMDIR)/bossroom/bossroom.collision: $(ASSDIR)/bossroom/bossroom.glb tools/export_collision.py
# 	@mkdir -p $(dir $@)
# 	@echo "    [COLLISION] $@"
# 	@$(MAKE) $(COLLISION_STAMP)
# 	@$(COLLISION_PY) tools/export_collision.py "$<" "$@" || ( \
# 		echo "    [COLLISION] WARNING: No COLLISION node found in $< (or exporter failed)."; \
# 		echo "    [COLLISION] Writing placeholder $@ so the build can continue."; \
# 		echo "# exported collision mesh" > "$@"; \
# 		echo "# EMPTY - add an Object named COLLISION to assets/bossroom/bossroom.glb" >> "$@"; \
# 		true \
# 	)

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