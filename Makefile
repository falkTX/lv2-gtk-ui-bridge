# Copyright 2024 Filipe Coelho <falktx@falktx.com>
# SPDX-License-Identifier: ISC

CFLAGS += -std=c11

TARGETS = lv2-gtk-ui-bridge.lv2/lv2-gtk-ui-bridge.so

TARGETS += lv2-gtk-ui-bridge.lv2/lv2-gtk2-ui-bridge
TARGETS += lv2-gtk-ui-bridge.lv2/lv2-gtk3-ui-bridge

all: $(TARGETS)

lv2-gtk-ui-bridge.lv2/lv2-gtk-ui-bridge.so: src/ui-server.c src/ipc/*.h
	$(CC) $< $(CFLAGS) $(LDFLAGS) $(shell pkg-config --cflags --libs lv2) -fPIC -shared -lrt -Wl,-no-undefined -o $@

lv2-gtk-ui-bridge.lv2/lv2-gtk2-ui-bridge: src/ui-client.c src/ipc/*.h
	$(CC) $< $(CFLAGS) $(LDFLAGS) $(shell pkg-config --cflags --libs gtk+-2.0 lilv-0 lv2 x11) -DUI_GTK2 -ldl -lrt -Wno-deprecated-declarations -o $@

lv2-gtk-ui-bridge.lv2/lv2-gtk3-ui-bridge: src/ui-client.c src/ipc/*.h
	$(CC) $< $(CFLAGS) $(LDFLAGS) $(shell pkg-config --cflags --libs gtk+-3.0 lilv-0 lv2 x11) -DUI_GTK3 -ldl -lrt -Wno-deprecated-declarations -o $@

test: src/test.c src/ipc/*.h
	$(CC) $< $(CFLAGS) $(LDFLAGS) -o $@

testxx: src/test.c src/ipc/*.h
	$(CXX) $< $(CXXFLAGS) $(LDFLAGS) -o $@

clean:
	rm -f $(TARGETS)
