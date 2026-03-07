# XXX TODO: Clean up in this entire file

.DEFAULT_GOAL := debug

PROG := scran

BUILD_DIR := build
wayland_protocols_generated_source_dir := $(BUILD_DIR)/wayland-protocols-generated-source
WL_PROTOCOLS_DIR_LOCAL := $(wayland_protocols_generated_source_dir)

ffmpeg_libs := libavcodec libavutil libavformat libavfilter libswscale
PKGCONF_LIBS := xkbcommon $(ffmpeg_libs)

_LDLIBS := -lwayland-client -lblend2d
_LDLIBS += $(shell pkg-config --libs $(PKGCONF_LIBS))
ALL_LDLIBS = $(_LDLIBS) $(LDLIBS)

INCDIRS := include/
INCDIRS += $(WL_PROTOCOLS_DIR_LOCAL)

# TODO: CPPFLAGS?
_CFLAGS := $(addprefix -I, $(INCDIRS))
_CFLAGS += $(shell pkg-config --cflags $(PKGCONF_LIBS))
ALL_CFLAGS = $(_CFLAGS) $(CFLAGS)

CFLAGS_REL ?= -DNDEBUG
ALL_CFLAGS_REL = $(ALL_CFLAGS) $(CFLAGS_REL)
CFLAGS_DBG ?= -gdwarf-5 -O0 -U_FORTIFY_SOURCE
ALL_CFLAGS_DBG = $(ALL_CFLAGS) $(CFLAGS_DBG)

.PHONY: validate_dependencies
# TODO: Add flake as well
# TODO: More comprehensive version validation
validate_dependencies:
	# wayland-scanner private-code subcommand introduced in 1.14.91 
	pkg-config --atleast-version=1.14.91 wayland-scanner
	# TODO: Should we verify protocol versions, or is the xml files existing
	# enough, then verify rest at runtime?
	pkg-config --exists wayland-protocols
	pkg-config --exists wlr-protocols
	pkg-config --exists $(PKGCONF_LIBS)

# TODO: Simply-expanded, but lazily initialized shell/pkg-config output variables
# 			I.e. don't require shell commands to run for targets that don't
# 			need them, but also don't evaluate them more times than necessary.
WAYLAND_SCANNER   := $(shell pkg-config --variable=wayland_scanner wayland-scanner)
WL_PROTOCOLS_DIR  := $(shell pkg-config --variable=pkgdatadir wayland-protocols)
WLR_PROTOCOLS_DIR := $(shell pkg-config --variable=pkgdatadir wlr-protocols)
wl_protocols_required_xml_paths := \
	$(WLR_PROTOCOLS_DIR)/unstable/wlr-layer-shell-unstable-v1.xml \
	$(WL_PROTOCOLS_DIR)/stable/xdg-shell/xdg-shell.xml \
	$(WL_PROTOCOLS_DIR)/unstable/xdg-output/xdg-output-unstable-v1.xml \
	$(WL_PROTOCOLS_DIR)/stable/tablet/tablet-v2.xml \
	$(WL_PROTOCOLS_DIR)/staging/cursor-shape/cursor-shape-v1.xml \
	$(WL_PROTOCOLS_DIR)/staging/ext-image-capture-source/ext-image-capture-source-v1.xml \
	$(WL_PROTOCOLS_DIR)/staging/ext-image-copy-capture/ext-image-copy-capture-v1.xml \
	$(WL_PROTOCOLS_DIR)/staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1.xml \
	$(WL_PROTOCOLS_DIR)/staging/ext-data-control/ext-data-control-v1.xml

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

# TODO: Handle changed header files
_srcdirs := src src/event-handlers src/init src/util
srcs := $(foreach dir,$(_srcdirs),$(wildcard $(dir)/*.c))

prog_release := $(build_dir_release)/$(PROG)
prog_debug :=   $(build_dir_debug)/$(PROG)

_objs := $(srcs:.c=.o) $(wayland_protocols_srcs_c:.c=.o)
objs_release := $(addprefix $(build_dir_release)/, $(_objs))
objs_debug :=   $(addprefix $(build_dir_debug)/,   $(_objs))

$(prog_release): $(objs_release)
	$(CC) $(ALL_CFLAGS_REL) $^ $(ALL_LDLIBS) -o $(prog_release)
$(prog_debug):	 $(objs_debug)
	$(CC) $(ALL_CFLAGS_DBG) $^ $(ALL_LDLIBS) -o $(prog_debug)


.PHONY: all
all: $(prog_release) $(prog_debug)

.PHONY: release debug
release: validate_dependencies $(prog_release)
debug:   validate_dependencies $(prog_debug)

.PHONY: protocols
_wayland_protocols_objs_debug := $(addprefix $(build_dir_debug)/, $(wayland_protocols_srcs_c:.c=.o))
protocols: $(wayland_protocols_srcs) _wayland_protocols_objs_debug

CMD_RM := $(shell command -v trash || command -v rm)

clean: 
	$(CMD_RM) -rf $(BUILD_DIR)

clean-objs:
	$(CMD_RM) -f $(objs_release) $(objs_debug)

clean-generated-src:
	$(CMD_RM) -rf $(wayland_protocols_generated_source_dir)

