PROG=main
LIBS=-lwayland-client -lblend2d

WAYLAND_SCANNER=$(shell pkg-config --variable=wayland_scanner wayland-scanner)
WAYLAND_PROTOCOLS=$(shell pkg-config --variable=pkgdatadir wayland-protocols)
WLR_PROTOCOLS=$(shell pkg-config --variable=pkgdatadir wlr-protocols)

.PHONY: all protocols

wlr-layer-shell-unstable-v1.h:
	$(WAYLAND_SCANNER) client-header $(WLR_PROTOCOLS)/unstable/wlr-layer-shell-unstable-v1.xml $@
wlr-layer-shell-unstable-v1.c:
	$(WAYLAND_SCANNER) private-code $(WLR_PROTOCOLS)/unstable/wlr-layer-shell-unstable-v1.xml $@
wlr-layer-shell-unstable-v1.o: wlr-layer-shell-unstable-v1.h wlr-layer-shell-unstable-v1.c

xdg-shell-protocol.h:
	$(WAYLAND_SCANNER) client-header $(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@
xdg-shell-protocol.c:
	$(WAYLAND_SCANNER) private-code $(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@
xdg-shell-protocol.o: xdg-shell-protocol.c xdg-shell-protocol.h

cursor-shape-v1.h:
	$(WAYLAND_SCANNER) client-header $(WAYLAND_PROTOCOLS)/staging/cursor-shape/cursor-shape-v1.xml $@
cursor-shape-v1.c:
	$(WAYLAND_SCANNER) private-code $(WAYLAND_PROTOCOLS)/staging/cursor-shape/cursor-shape-v1.xml $@
cursor-shape-v1.o: cursor-shape-v1.h cursor-shape-v1.c

# TODO: Get version supported by sway
tablet-v1.h:
	$(WAYLAND_SCANNER) client-header $(WAYLAND_PROTOCOLS)/stable/tablet/tablet-v2.xml $@
tablet-v1.c:
	$(WAYLAND_SCANNER) private-code $(WAYLAND_PROTOCOLS)/stable/tablet/tablet-v2.xml $@
tablet-v1.o: tablet-v1.h tablet-v1.c

protocols: wlr-layer-shell-unstable-v1.o xdg-shell-protocol.o cursor-shape-v1.o tablet-v1.o

$(PROG): main.c state.h protocols
	# TODO: Clean up (SRC)
	gcc *.c -o main $(LIBS)

all: $(PROG)

