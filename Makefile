# Copyright 2024 Filipe Coelho <falktx@falktx.com>
# SPDX-License-Identifier: ISC

# ---------------------------------------------------------------------------------------------------------------------
# Set C / C++ language version and base flags

CFLAGS += -std=c11 -Wall -Wextra -Wno-unused-value
CXXFLAGS += -std=c++11 -Wall -Wextra -Wno-unused-value

# ---------------------------------------------------------------------------------------------------------------------
# Use vendored LV2 if available

ifneq (,$(wildcard lv2/include))
LV2_FLAGS = -Ilv2/include
else
LV2_FLAGS = $(shell pkg-config --cflags --libs lv2)
endif

# ---------------------------------------------------------------------------------------------------------------------
# Set per-system flags

TARGET_MACHINE := $(shell $(CC) -dumpmachine)

ifneq (,$(findstring apple,$(TARGET_MACHINE)))
CFLAGS += -Wno-deprecated-declarations
CXXFLAGS += -Wno-deprecated -Wno-deprecated-declarations
SERVER_FLAGS = -fPIC -dynamiclib
else ifneq (,$(findstring mingw,$(TARGET_MACHINE)))
APP_EXT = .exe
CFLAGS += -mstackrealign
CXXFLAGS += -mstackrealign
LDFLAGS += -static
SERVER_FLAGS = -shared -Wl,-no-undefined
else
CLIENT_FLAGS = -ldl
SERVER_FLAGS = -fPIC -shared -Wl,-no-undefined
SHM_LIBS = -lrt
endif

# ---------------------------------------------------------------------------------------------------------------------
# Build targets

TARGETS  = lv2-gtk-ui-bridge.lv2/lv2-gtk-ui-bridge.so
TARGETS += lv2-gtk-ui-bridge.lv2/lv2-gtk2-ui-bridge$(APP_EXT)
TARGETS += lv2-gtk-ui-bridge.lv2/lv2-gtk3-ui-bridge$(APP_EXT)

# ---------------------------------------------------------------------------------------------------------------------

all: $(TARGETS)

lv2-gtk-ui-bridge.lv2/lv2-gtk-ui-bridge.so: src/ui-server.c src/ipc/*.h
	$(CC) $< $(CFLAGS) $(LDFLAGS) $(LV2_FLAGS) $(SERVER_FLAGS) $(SHM_LIBS) -o $@

lv2-gtk-ui-bridge.lv2/lv2-gtk2-ui-bridge$(APP_EXT): src/ui-client.c src/ipc/*.h
	$(CC) $< $(CFLAGS) $(LDFLAGS) $(LV2_FLAGS) $(shell pkg-config --cflags --libs gtk+-2.0 lilv-0 x11) -DUI_GTK2 $(CLIENT_FLAGS) $(SHM_LIBS) -Wno-deprecated-declarations -o $@

lv2-gtk-ui-bridge.lv2/lv2-gtk3-ui-bridge$(APP_EXT): src/ui-client.c src/ipc/*.h
	$(CC) $< $(CFLAGS) $(LDFLAGS) $(LV2_FLAGS) $(shell pkg-config --cflags --libs gtk+-3.0 lilv-0 x11) -DUI_GTK3 $(CLIENT_FLAGS) $(SHM_LIBS) -Wno-deprecated-declarations -o $@

# ---------------------------------------------------------------------------------------------------------------------

test: src/test.c src/ipc/*.h
	$(CC) $< $(CFLAGS) $(LDFLAGS) $(SHM_LIBS) -o $@$(APP_EXT)

testxx: src/test.c src/ipc/*.h
	$(CXX) $< $(CXXFLAGS) $(LDFLAGS) $(SHM_LIBS) -o $@$(APP_EXT)

clean:
	rm -f $(TARGETS) test testxx *.exe
