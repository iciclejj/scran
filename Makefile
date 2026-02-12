# TODO: THIS IS ALL A MESS

.DEFAULT_GOAL := debug

ENV_CFLAGS := $(CFLAGS)
ENV_CFLAGS_REL := $(CFLAGS_REL)
ENV_CFLAGS_DBG := $(CFLAGS_DBG)

FFMPEG_LIBS := libavcodec libavutil libavformat libswscale
PKGCONF_LIBS := xkbcommon $(FFMPEG_LIBS)

BUILD_DIR := build
wayland_protocols_generated_source_dir := $(BUILD_DIR)/wayland-protocols-generated-source
WAYLAND_PROTOCOLS_DIR_LOCAL := $(wayland_protocols_generated_source_dir)

PROG := scran
LDLIBS := -lwayland-client -lblend2d
LDLIBS += $(foreach pkg, $(PKGCONF_LIBS), $(shell pkg-config --libs $(pkg)))
INCDIRS := include/
INCDIRS += $(WAYLAND_PROTOCOLS_DIR_LOCAL)
CFLAGS := $(addprefix -I, $(INCDIRS))
CFLAGS += $(foreach pkg, $(PKGCONF_LIBS), $(shell pkg-config --cflags $(pkg)))
CFLAGS_REL := $(CFLAGS) -DNDEBUG
CFLAGS_REL += $(ENV_CFLAGS) $(ENV_CFLAGS_REL)
CFLAGS_DBG := $(CFLAGS) -gdwarf-5 -O0 -U_FORTIFY_SOURCE
CFLAGS_DBG += $(ENV_CFLAGS) $(ENV_CFLAGS_DBG)
ifeq ($(CC),clang)
CFLAGS_DBG += -gembed-source
endif


# TODO: Ensure package versions. Flake?
# TODO: Simply-expanded, but lazily initialized shell/pkg-config output variables
# 			I.e. don't require shell commands to run for targets that don't
# 			need them, but also don't evaluate them more times than necessary.
WAYLAND_SCANNER := $(shell pkg-config --variable=wayland_scanner wayland-scanner)
WAYLAND_PROTOCOLS_DIR := $(shell pkg-config --variable=pkgdatadir wayland-protocols)
WAYLAND_PROTOCOLS_DIR_WLR := $(shell pkg-config --variable=pkgdatadir wlr-protocols)
# TODO: Ensure sway-compatible protocol versions
WAYLAND_PROTOCOLS_REQUIRED_XML_PATHS := \
	$(WAYLAND_PROTOCOLS_DIR_WLR)/unstable/wlr-layer-shell-unstable-v1.xml \
	$(WAYLAND_PROTOCOLS_DIR)/stable/xdg-shell/xdg-shell.xml \
	$(WAYLAND_PROTOCOLS_DIR)/unstable/xdg-output/xdg-output-unstable-v1.xml \
	$(WAYLAND_PROTOCOLS_DIR)/stable/tablet/tablet-v2.xml \
	$(WAYLAND_PROTOCOLS_DIR)/staging/cursor-shape/cursor-shape-v1.xml \
	$(WAYLAND_PROTOCOLS_DIR)/staging/ext-image-capture-source/ext-image-capture-source-v1.xml \
	$(WAYLAND_PROTOCOLS_DIR)/staging/ext-image-copy-capture/ext-image-copy-capture-v1.xml \
	$(WAYLAND_PROTOCOLS_DIR)/staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1.xml \
	$(WAYLAND_PROTOCOLS_DIR)/staging/ext-data-control/ext-data-control-v1.xml

# $(1): Wayland protocol .xml path
# $(2): Output file extension
define _CREATE_PROTOCOL_OUTPUT_PATH
$(WAYLAND_PROTOCOLS_DIR_LOCAL)/$(basename $(notdir $(1)))$(2)
endef
# $(1): Wayland protocol .xml path
define WAYLAND_PROTOCOL_GEN_RULE
$(call _CREATE_PROTOCOL_OUTPUT_PATH,$(1),.h) \
$(call _CREATE_PROTOCOL_OUTPUT_PATH,$(1),.c) \
: $(1)
	@mkdir -p $(WAYLAND_PROTOCOLS_DIR_LOCAL)
	$(WAYLAND_SCANNER) client-header $(1) $(call _CREATE_PROTOCOL_OUTPUT_PATH,$(1),.h)
	$(WAYLAND_SCANNER) private-code $(1) $(call _CREATE_PROTOCOL_OUTPUT_PATH,$(1),.c)
endef
$(foreach path, $(WAYLAND_PROTOCOLS_REQUIRED_XML_PATHS), $(eval $(call WAYLAND_PROTOCOL_GEN_RULE,$(path))))

# XXX: These should probably inputs to WAYLAND_PROTOCOL_GEN_RULE
wayland_protocols_srcs_c := $(foreach path, $(WAYLAND_PROTOCOLS_REQUIRED_XML_PATHS), $(call _CREATE_PROTOCOL_OUTPUT_PATH,$(path),.c))
wayland_protocols_srcs_h := $(foreach path, $(WAYLAND_PROTOCOLS_REQUIRED_XML_PATHS), $(call _CREATE_PROTOCOL_OUTPUT_PATH,$(path),.h))
wayland_protocols_srcs := $(wayland_protocols_srcs_c) $(wayland_protocols_srcs_h)

.PHONY: protocols_srcs
protocols_srcs: $(wayland_protocols_srcs)


build_dir_release := $(BUILD_DIR)/release
build_dir_debug :=   $(BUILD_DIR)/debug

$(build_dir_release)/%.o: %.c  protocols_srcs
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_REL) -c $< -o $@
$(build_dir_debug)/%.o: %.c    protocols_srcs
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_DBG) -c $< -o $@

# TODO: Handle changed header files
_srcdirs := src src/event-handlers src/init
srcs := $(foreach dir,$(_srcdirs),$(wildcard $(dir)/*.c))

prog_release := $(build_dir_release)/$(PROG)
prog_debug :=   $(build_dir_debug)/$(PROG)

_objs := $(srcs:.c=.o) $(wayland_protocols_srcs_c:.c=.o)
objs_release := $(addprefix $(build_dir_release)/, $(_objs))
objs_debug :=   $(addprefix $(build_dir_debug)/,   $(_objs))

$(prog_release): $(objs_release)
	$(CC) $(CFLAGS_REL) $^ $(LDLIBS) -o $(prog_release)
$(prog_debug):	 $(objs_debug)
	$(CC) $(CFLAGS_DBG) $^ $(LDLIBS) -o $(prog_debug)


.PHONY: all
all: $(prog_release) $(prog_debug)

.PHONY: release debug
release: $(prog_release)
debug: $(prog_debug)

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

