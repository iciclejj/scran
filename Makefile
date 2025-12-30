PROG = main
LDLIBS = -lwayland-client -lblend2d
INCDIRS = include/
INCDIRS += $(WAYLAND_PROTOCOLS_DIR_LOCAL)
CFLAGS = $(addprefix -I, $(INCDIRS))
SRCS = main.c
SRCS += $(addprefix $(WAYLAND_PROTOCOLS_DIR_LOCAL)/, $(WAYLAND_PROTOCOLS_REQUIRED_C_FILENAMES))
OBJS = $(SRCS:.c=.o)

WAYLAND_SCANNER = $(shell pkg-config --variable=wayland_scanner wayland-scanner)
WAYLAND_PROTOCOLS_DIR = $(shell pkg-config --variable=pkgdatadir wayland-protocols)
WAYLAND_PROTOCOLS_DIR_WLR = $(shell pkg-config --variable=pkgdatadir wlr-protocols)
WAYLAND_PROTOCOLS_DIR_LOCAL = wayland-protocols
# TODO: Ensure sway-compatible protocol versions
WAYLAND_PROTOCOLS_REQUIRED_XML_PATHS = \
	$(WAYLAND_PROTOCOLS_DIR_WLR)/unstable/wlr-layer-shell-unstable-v1.xml \
	$(WAYLAND_PROTOCOLS_DIR)/stable/xdg-shell/xdg-shell.xml \
	$(WAYLAND_PROTOCOLS_DIR)/stable/tablet/tablet-v2.xml \
	$(WAYLAND_PROTOCOLS_DIR)/staging/cursor-shape/cursor-shape-v1.xml
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

# $(WAYLAND_PROTOCOLS_REQUIRED_XML_PATHS): $($(WAYLAND_PROTOCOLS_REQUIRED_H_FILENAMES): %.xml $(WAYLAND_PROTOCOLS_DIR_LOCAL)
# 	$(WAYLAND_SCANNER) client-header $< $(WAYLAND_PROTOCOLS_DIR_LOCAL)/$@
#
# $(WAYLAND_PROTOCOLS_REQUIRED_XML_PATHS): %.c: %.xml $(WAYLAND_PROTOCOLS_DIR_LOCAL)
# 	$(WAYLAND_SCANNER) private-code $< $(WAYLAND_PROTOCOLS_DIR_LOCAL)/$@
#
# wlr-layer-shell-unstable-v1.h:
# 	$(WAYLAND_SCANNER) client-header $(WAYLAND_PROTOCOLS_DIR_WLR)/unstable/wlr-layer-shell-unstable-v1.xml $@
# wlr-layer-shell-unstable-v1.c:
# 	$(WAYLAND_SCANNER) private-code $(WAYLAND_PROTOCOLS_DIR_WLR)/unstable/wlr-layer-shell-unstable-v1.xml $@
# wlr-layer-shell-unstable-v1.o: wlr-layer-shell-unstable-v1.h wlr-layer-shell-unstable-v1.c
#
# xdg-shell-protocol.h:
# 	$(WAYLAND_SCANNER) client-header $(WAYLAND_PROTOCOLS_DIR)/stable/xdg-shell/xdg-shell.xml $@
# xdg-shell-protocol.c:
# 	$(WAYLAND_SCANNER) private-code $(WAYLAND_PROTOCOLS_DIR)/stable/xdg-shell/xdg-shell.xml $@
# xdg-shell-protocol.o: xdg-shell-protocol.c xdg-shell-protocol.h
#
# cursor-shape-v1.h:
# 	$(WAYLAND_SCANNER) client-header $(WAYLAND_PROTOCOLS_DIR)/staging/cursor-shape/cursor-shape-v1.xml $@
# cursor-shape-v1.c:
# 	$(WAYLAND_SCANNER) private-code $(WAYLAND_PROTOCOLS_DIR)/staging/cursor-shape/cursor-shape-v1.xml $@
# cursor-shape-v1.o: cursor-shape-v1.h cursor-shape-v1.c
#
# # TODO: Get version supported by sway
# tablet-v1.h:
# 	$(WAYLAND_SCANNER) client-header $(WAYLAND_PROTOCOLS_DIR)/stable/tablet/tablet-v2.xml $@
# tablet-v1.c:
# 	$(WAYLAND_SCANNER) private-code $(WAYLAND_PROTOCOLS_DIR)/stable/tablet/tablet-v2.xml $@
# tablet-v1.o: tablet-v1.h tablet-v1.c
#
# protocols: wlr-layer-shell-unstable-v1.o xdg-shell-protocol.o cursor-shape-v1.o tablet-v1.o

$(PROG): $(OBJS) protocols
	# gcc $(SRC) -o main $(LDLIBS)
	$(CC) $(OBJS) $(CFLAGS) -o $(PROG) $(LDLIBS)


all: $(PROG)

clean: 
	rm $(OBJS)

