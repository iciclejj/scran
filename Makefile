# TODO: THIS IS ALL A MESS

.DEFAULT_GOAL := debug

ENV_CFLAGS := $(CFLAGS)
ENV_CFLAGS_REL := $(CFLAGS_REL)
ENV_CFLAGS_DBG := $(CFLAGS_DBG)

FFMPEG_LIBS = libavcodec libavutil libavformat libswscale
PKGCONF_LIBS = xkbcommon $(FFMPEG_LIBS)

BUILD_DIR = build
BUILD_DIR_REL = $(BUILD_DIR)/release
BUILD_DIR_DBG = $(BUILD_DIR)/debug

PROG = main
PROG_REL = $(BUILD_DIR_REL)/$(PROG)
PROG_DBG = $(BUILD_DIR_DBG)/$(PROG)
LDLIBS = -lwayland-client -lblend2d
LDLIBS += $(foreach pkg, $(PKGCONF_LIBS), $(shell pkg-config --libs $(pkg)))
INCDIRS = include/
INCDIRS += $(WAYLAND_PROTOCOLS_DIR_LOCAL)
CFLAGS = $(addprefix -I, $(INCDIRS))
CFLAGS += $(foreach pkg, $(PKGCONF_LIBS), $(shell pkg-config --cflags $(pkg)))
CFLAGS_REL = $(CFLAGS)
CFLAGS_REL += $(ENV_CFLAGS) $(ENV_CFLAGS_REL)
CFLAGS_DBG = $(CFLAGS) -g -O0 -U_FORTIFY_SOURCE
CFLAGS_DBG += $(ENV_CFLAGS) $(ENV_CFLAGS_DBG)
SRCDIRS = src src/event-handlers src/init
SRCS = $(foreach dir, $(SRCDIRS), $(wildcard $(dir)/*.c))
SRCS += $(addprefix $(WAYLAND_PROTOCOLS_DIR_LOCAL)/, $(WAYLAND_PROTOCOLS_REQUIRED_C_FILENAMES))
OBJS = $(SRCS:.c=.o)
OBJS_REL = $(addprefix $(BUILD_DIR_REL)/, $(OBJS))
OBJS_DBG = $(addprefix $(BUILD_DIR_DBG)/, $(OBJS))

# TODO: Ensure package versions. Flake?
WAYLAND_SCANNER = $(shell pkg-config --variable=wayland_scanner wayland-scanner)
WAYLAND_PROTOCOLS_DIR = $(shell pkg-config --variable=pkgdatadir wayland-protocols)
WAYLAND_PROTOCOLS_DIR_WLR = $(shell pkg-config --variable=pkgdatadir wlr-protocols)
WAYLAND_PROTOCOLS_DIR_LOCAL = wayland-protocols
# TODO: Ensure sway-compatible protocol versions
WAYLAND_PROTOCOLS_REQUIRED_XML_PATHS = \
	$(WAYLAND_PROTOCOLS_DIR_WLR)/unstable/wlr-layer-shell-unstable-v1.xml \
	$(WAYLAND_PROTOCOLS_DIR)/stable/xdg-shell/xdg-shell.xml \
	$(WAYLAND_PROTOCOLS_DIR)/unstable/xdg-output/xdg-output-unstable-v1.xml \
	$(WAYLAND_PROTOCOLS_DIR)/stable/tablet/tablet-v2.xml \
	$(WAYLAND_PROTOCOLS_DIR)/staging/cursor-shape/cursor-shape-v1.xml \
	$(WAYLAND_PROTOCOLS_DIR)/staging/ext-image-capture-source/ext-image-capture-source-v1.xml \
	$(WAYLAND_PROTOCOLS_DIR)/staging/ext-image-copy-capture/ext-image-copy-capture-v1.xml \
	$(WAYLAND_PROTOCOLS_DIR)/staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1.xml
WAYLAND_PROTOCOLS_REQUIRED_BASENAMES = $(foreach path, $(WAYLAND_PROTOCOLS_REQUIRED_XML_PATHS), $(basename $(notdir $(path))))

# $(1): Wayland protocol .xml path
define WAYLAND_PROTOCOL_GEN_RULE
$(WAYLAND_PROTOCOLS_DIR_LOCAL)/$(basename $(notdir $(1))).h \
$(WAYLAND_PROTOCOLS_DIR_LOCAL)/$(basename $(notdir $(1))).c \
: $(1)
	@mkdir -p $(WAYLAND_PROTOCOLS_DIR_LOCAL)
	$(WAYLAND_SCANNER) client-header $(1) $(WAYLAND_PROTOCOLS_DIR_LOCAL)/$(basename $(notdir $(1))).h
	$(WAYLAND_SCANNER) private-code $(1) $(WAYLAND_PROTOCOLS_DIR_LOCAL)/$(basename $(notdir $(1))).c
endef
WAYLAND_PROTOCOLS_REQUIRED_C_FILENAMES = $(patsubst %, %.c, $(WAYLAND_PROTOCOLS_REQUIRED_BASENAMES))
WAYLAND_PROTOCOLS_REQUIRED_H_FILENAMES = $(patsubst %, %.h, $(WAYLAND_PROTOCOLS_REQUIRED_BASENAMES))



.PHONY: all clean protocols debug


$(foreach path, $(WAYLAND_PROTOCOLS_REQUIRED_XML_PATHS), $(eval $(call WAYLAND_PROTOCOL_GEN_RULE, $(path))))

WAYLAND_PROTOCOLS = $(addprefix \
	$(WAYLAND_PROTOCOLS_DIR_LOCAL)/, \
	$(WAYLAND_PROTOCOLS_REQUIRED_C_FILENAMES) \
	$(WAYLAND_PROTOCOLS_REQUIRED_H_FILENAMES) \
)

protocols: $(WAYLAND_PROTOCOLS)

$(BUILD_DIR_REL)/%.o: %.c
	@mkdir -p $(shell dirname $@)
	$(CC) $(CFLAGS_REL) -c $< -o $@
$(BUILD_DIR_DBG)/%.o: %.c
	@mkdir -p $(shell dirname $@)
	$(CC) $(CFLAGS_DBG) -c $< -o $@

$(PROG_REL): $(OBJS_REL)
	$(CC) $(OBJS_REL) $(CFLAGS_REL) -o $(PROG_REL) $(LDLIBS)
$(PROG_DBG): $(OBJS_DBG)
	$(CC) $(OBJS_DBG) $(CFLAGS_DBG) -o $(PROG_DBG) $(LDLIBS)

all: $(PROG_REL) $(PROG_DBG)

release: $(PROG_REL)
debug: $(PROG_DBG)

clean: 
	trash -rf ./build/ || rm -rf ./build/

