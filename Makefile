# TODO: THIS IS ALL A MESS

PKGCONF_LIBS = libavcodec libavutil

PROG = main
LDLIBS = -lwayland-client -lblend2d
LDLIBS += $(foreach pkg, $(PKGCONF_LIBS), $(shell pkg-config --libs $(pkg)))
INCDIRS = include/
INCDIRS += $(WAYLAND_PROTOCOLS_DIR_LOCAL)
CFLAGS += $(addprefix -I, $(INCDIRS)) -O2
CFLAGS += $(foreach pkg, $(PKGCONF_LIBS), $(shell pkg-config --cflags $(pkg)))
SRCDIRS = src src/event-handlers
SRCS = $(foreach dir, $(SRCDIRS), $(wildcard $(dir)/*.c))
SRCS += $(addprefix $(WAYLAND_PROTOCOLS_DIR_LOCAL)/, $(WAYLAND_PROTOCOLS_REQUIRED_C_FILENAMES))
OBJS = $(SRCS:.c=.o)

# TODO: Ensure package versions. Flake?
WAYLAND_SCANNER = $(shell pkg-config --variable=wayland_scanner wayland-scanner)
WAYLAND_PROTOCOLS_DIR = $(shell pkg-config --variable=pkgdatadir wayland-protocols)
WAYLAND_PROTOCOLS_DIR_WLR = $(shell pkg-config --variable=pkgdatadir wlr-protocols)
WAYLAND_PROTOCOLS_DIR_LOCAL = wayland-protocols
# TODO: Ensure sway-compatible protocol versions
WAYLAND_PROTOCOLS_REQUIRED_XML_PATHS = \
	$(WAYLAND_PROTOCOLS_DIR_WLR)/unstable/wlr-layer-shell-unstable-v1.xml \
	$(WAYLAND_PROTOCOLS_DIR)/stable/xdg-shell/xdg-shell.xml \
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



.PHONY: all clean protocols


$(foreach path, $(WAYLAND_PROTOCOLS_REQUIRED_XML_PATHS), $(eval $(call WAYLAND_PROTOCOL_GEN_RULE, $(path))))

protocols: $(addprefix \
	$(WAYLAND_PROTOCOLS_DIR_LOCAL)/, \
	$(WAYLAND_PROTOCOLS_REQUIRED_C_FILENAMES) \
	$(WAYLAND_PROTOCOLS_REQUIRED_H_FILENAMES) \
)

$(OBJS): %.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(PROG): $(OBJS) protocols
	$(CC) $(OBJS) $(CFLAGS) -o $(PROG) $(LDLIBS)


all: $(PROG)

clean: 
	rm $(OBJS)
	rm $(PROG)

