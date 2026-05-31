.DEFAULT_GOAL := debug

PROG := scran

FONT := assets/font.ttf

BUILD_DIR := build
wayland_protocols_generated_source_dir := $(BUILD_DIR)/wayland-protocols-generated-source
WL_PROTOCOLS_DIR_LOCAL := $(wayland_protocols_generated_source_dir)

PKG_CONFIG ?= pkg-config

SD_BUS_LIB := $(shell if   $(PKG_CONFIG) --exists basu       2>/dev/null; then echo "basu"; \
			          elif $(PKG_CONFIG) --exists libsystemd 2>/dev/null; then echo "libsystemd"; \
					  fi)
ffmpeg_libs   := libavcodec libavutil libavformat
pipewire_libs := libpipewire-0.3 libspa-0.2
PKGCONF_LIBS  := xkbcommon wayland-client $(pipewire_libs) $(ffmpeg_libs) $(SD_BUS_LIB)

_LDLIBS := -lblend2d -lm
_LDLIBS += $(shell $(PKG_CONFIG) --libs $(PKGCONF_LIBS))
ALL_LDLIBS = $(_LDLIBS) $(LDLIBS)

# TODO: Separate Makefile for scranrot
INCDIRS := include/ scranrot/include
INCDIRS += $(WL_PROTOCOLS_DIR_LOCAL)

# TODO: CPPFLAGS?
_CFLAGS := $(addprefix -I, $(INCDIRS)) -Wall -Wextra -Wno-unused-parameter
_CFLAGS += $(shell $(PKG_CONFIG) --cflags $(PKGCONF_LIBS))
ifeq ($(SD_BUS_LIB),libsystemd)
	_CFLAGS += -DSCRAN_LIBSYSTEMD_SD_BUS
endif
ALL_CFLAGS = $(_CFLAGS) $(CFLAGS)

CFLAGS_REL ?= -DNDEBUG -O2
ALL_CFLAGS_REL = $(ALL_CFLAGS) $(CFLAGS_REL)
CFLAGS_DBG ?= -gdwarf-5 -O0 -U_FORTIFY_SOURCE
ALL_CFLAGS_DBG = $(ALL_CFLAGS) $(CFLAGS_DBG)

# $(1): Package name
# $(2): Version
# TODO: Don't fail instantly - let user see all failed first.
define shell_validate_dependency
$(if $(strip $(2)),\
	echo -n "Checking dependency: $(1) >=$(2): "; \
		$(PKG_CONFIG) --atleast-version=$(2) $(1),\
	echo -n "Checking dependency: $(1): "; \
	    $(PKG_CONFIG) --exists $(1)\
) && echo "OK." || { echo "Failed."; exit 1; }
endef

.PHONY: validate_dependencies
# TODO: Add flake as well
# TODO: More comprehensive version validation
validate_dependencies:
	@# wayland-scanner private-code subcommand introduced in 1.14.91
	@$(call shell_validate_dependency,wayland-scanner,1.14.91)
	@# TODO: Verify protocol versions, or just bundle the xmls with scran
	@$(call shell_validate_dependency,wayland-protocols)
	@if [ -z "$(SD_BUS_LIB)" ]; then \
	    echo "No sd-bus library found. Please install 'basu' or 'libsystemd'."; \
		exit 1; \
	fi
	@for pkg in $(PKGCONF_LIBS); do \
		$(call shell_validate_dependency,$$pkg); \
	done

# TODO: Simply-expanded, but lazily initialized shell/$(PKG_CONFIG) output variables
# 			I.e. don't require shell commands to run for targets that don't
# 			need them, but also don't evaluate them more times than necessary.
WAYLAND_SCANNER   := $(shell $(PKG_CONFIG) --variable=wayland_scanner wayland-scanner)
WL_PROTOCOLS_DIR  := $(shell $(PKG_CONFIG) --variable=pkgdatadir wayland-protocols)
WLR_PROTOCOLS_DIR := wayland-protocol-extensions/wlr-protocols
COSMIC_PROTOCOLS_DIR := wayland-protocol-extensions/cosmic-protocols
HYPRLAND_PROTOCOLS_DIR := wayland-protocol-extensions/hyprland-protocols
wl_protocols_required_xml_paths := \
	$(WLR_PROTOCOLS_DIR)/unstable/wlr-layer-shell-unstable-v1.xml \
	$(WLR_PROTOCOLS_DIR)/unstable/wlr-output-management-unstable-v1.xml \
	$(COSMIC_PROTOCOLS_DIR)/unstable/cosmic-output-management-unstable-v1.xml \
	$(HYPRLAND_PROTOCOLS_DIR)/hyprland-surface-v1.xml \
	$(WL_PROTOCOLS_DIR)/stable/xdg-shell/xdg-shell.xml \
	$(WL_PROTOCOLS_DIR)/stable/tablet/tablet-v2.xml \
	$(WL_PROTOCOLS_DIR)/stable/presentation-time/presentation-time.xml \
	$(WL_PROTOCOLS_DIR)/stable/viewporter/viewporter.xml \
	$(WL_PROTOCOLS_DIR)/unstable/xdg-output/xdg-output-unstable-v1.xml \
	$(WL_PROTOCOLS_DIR)/staging/cursor-shape/cursor-shape-v1.xml \
	$(WL_PROTOCOLS_DIR)/staging/ext-image-capture-source/ext-image-capture-source-v1.xml \
	$(WL_PROTOCOLS_DIR)/staging/ext-image-copy-capture/ext-image-copy-capture-v1.xml \
	$(WL_PROTOCOLS_DIR)/staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1.xml \
	$(WL_PROTOCOLS_DIR)/staging/ext-data-control/ext-data-control-v1.xml \
	$(WL_PROTOCOLS_DIR)/staging/fractional-scale/fractional-scale-v1.xml

# $(1): Wayland protocol .xml path
# $(2): Output file extension
define create_protocol_output_path
$(WL_PROTOCOLS_DIR_LOCAL)/$(basename $(notdir $(1)))$(2)
endef
# $(1): Wayland protocol .xml path
define wl_protocol_rule
$(call create_protocol_output_path,$(1),.h) \
$(call create_protocol_output_path,$(1),.c) \
: $(1)
	@mkdir -p $(WL_PROTOCOLS_DIR_LOCAL)
	$(WAYLAND_SCANNER) client-header $(1) $(call create_protocol_output_path,$(1),.h)
	$(WAYLAND_SCANNER) private-code  $(1) $(call create_protocol_output_path,$(1),.c)
endef
$(foreach path, $(wl_protocols_required_xml_paths), $(eval $(call wl_protocol_rule,$(path))))

# XXX: These should probably inputs to wl_protocol_rule
wayland_protocols_srcs_c := $(foreach path, $(wl_protocols_required_xml_paths), $(call create_protocol_output_path,$(path),.c))
wayland_protocols_srcs_h := $(foreach path, $(wl_protocols_required_xml_paths), $(call create_protocol_output_path,$(path),.h))
wayland_protocols_srcs := $(wayland_protocols_srcs_c) $(wayland_protocols_srcs_h)

.PHONY: protocols_srcs
protocols_srcs: $(wayland_protocols_srcs)


build_dir_release := $(BUILD_DIR)/release
build_dir_debug :=   $(BUILD_DIR)/debug

$(build_dir_release)/%.o: %.c  protocols_srcs
	@mkdir -p $(dir $@)
	$(CC) $(ALL_CFLAGS_REL) -c $< -o $@
$(build_dir_debug)/%.o: %.c    protocols_srcs
	@mkdir -p $(dir $@)
	$(CC) $(ALL_CFLAGS_DBG) -c $< -o $@

# TODO: Separate Makefile for scranrot
# TODO: Handle changed header files
_srcdirs := src                                         \
			src/init                                    \
			src/util                                    \
			src/event-handlers                          \
			src/event-handlers/image-copy-capture-frame \
			src/event-handlers/surface                  \
			src/event-handlers/layer-surface	        \
			src/event-handlers/fractional-scale		    \
			scranrot/src
srcs := $(foreach dir,$(_srcdirs),$(wildcard $(dir)/*.c))

prog_release := $(build_dir_release)/$(PROG)
prog_debug :=   $(build_dir_debug)/$(PROG)

_objs := $(srcs:.c=.o) $(wayland_protocols_srcs_c:.c=.o)
objs_release := $(addprefix $(build_dir_release)/, $(_objs))
objs_debug :=   $(addprefix $(build_dir_debug)/,   $(_objs))

# TODO: Allow arch override/detection
obj_font := $(FONT:.ttf=.o)
$(obj_font): $(FONT)
	objcopy \
		--input-target binary \
		--output-target elf64-x86-64 \
		--binary-architecture i386:x86-64 \
		--rename-section .data=.rodata,alloc,load,readonly,data,contents \
		--add-section .note.GNU-stack=/dev/null \
		--set-section-flags .note.GNU-stack=contents,readonly \
		$< $@

$(prog_release): $(objs_release) $(obj_font)
	$(CC) $(ALL_CFLAGS_REL) $(LDFLAGS) $^ $(ALL_LDLIBS) -o $(prog_release)
$(prog_debug):	 $(objs_debug)   $(obj_font)
	$(CC) $(ALL_CFLAGS_DBG) $(LDFLAGS) $^ $(ALL_LDLIBS) -o $(prog_debug)


.PHONY: all
all: release debug

.PHONY: release debug
release: validate_dependencies $(prog_release)
debug:   validate_dependencies $(prog_debug)

.PHONY: protocols
_wayland_protocols_objs_debug := $(addprefix $(build_dir_debug)/, $(wayland_protocols_srcs_c:.c=.o))
protocols: $(wayland_protocols_srcs) $(_wayland_protocols_objs_debug)

.PHONY: clean clean-objs clean-generated-src
clean: 
	rm -rf $(BUILD_DIR) $(obj_font)
clean-objs:
	rm -f $(objs_release) $(objs_debug) $(obj_font)
clean-generated-src:
	rm -rf $(wayland_protocols_generated_source_dir)

