# Copyright 2024 Filipe Coelho <falktx@falktx.com>
# SPDX-License-Identifier: ISC

CFLAGS += -std=c11
CFLAGS += $(shell pkg-config --cflags lv2)

TARGETS = lv2-gtk-ui-bridge.lv2/lv2-gtk-ui-bridge.so

TARGETS += lv2-gtk-ui-bridge.lv2/lv2-gtk2-ui-bridge
TARGETS += lv2-gtk-ui-bridge.lv2/lv2-gtk3-ui-bridge

all: $(TARGETS)

lv2-gtk-ui-bridge.lv2/lv2-gtk-ui-bridge.so: src/ui-server.c src/ipc/*.h
	$(CC) $< $(CFLAGS) $(LDFLAGS) -fPIC -shared -Wl,-no-undefined -o $@

lv2-gtk-ui-bridge.lv2/lv2-gtk2-ui-bridge: src/ui-client.c src/ipc/*.h
	$(CC) $< $(CFLAGS) $(LDFLAGS) -DUI_GTK2 $(shell pkg-config --cflags --libs gtk+-2.0 lilv-0 x11) -ldl -Wno-deprecated-declarations -o $@

lv2-gtk-ui-bridge.lv2/lv2-gtk3-ui-bridge: src/ui-client.c src/ipc/*.h
	$(CC) $< $(CFLAGS) $(LDFLAGS) -DUI_GTK3 $(shell pkg-config --cflags --libs gtk+-3.0 lilv-0 x11) -ldl -Wno-deprecated-declarations -o $@

test: src/test.c src/ipc/*.h
	$(CC) $< $(CFLAGS) $(LDFLAGS) -o $@

clean:
	rm -f $(TARGETS)
