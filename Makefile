
CFLAGS += -std=c11

# CFLAGS  += $(shell pkg-config --cflags jack lilv-0 lv2) -fPIC
# LDFLAGS += $(shell pkg-config --libs jack lilv-0 lv2)
# LDFLAGS += -Wl,-no-undefined -ldl

all: build

build: calf-x11-guis.lv2/calf-x11-guis.so calf-x11-guis.lv2/calf-x11-run

calf-x11-guis.lv2/calf-x11-guis.so: calf-x11-guis.c
	$(CC) $^ $(CFLAGS) $(LDFLAGS) -shared -o $@

calf-x11-guis.lv2/calf-x11-run: calf-x11-run.c
	$(CC) $^ $(CFLAGS) $(LDFLAGS) $(shell pkg-config --cflags --libs gtk+-x11-2.0 x11) -Wno-deprecated-declarations -o $@

test: src/test.c src/ipc.h
	$(CC) $< $(CFLAGS) $(LDFLAGS) -o $@
