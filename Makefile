CC := clang
CFLAGS := -std=c11 -Wall -Wextra -pedantic -O2
HID_LDFLAGS := -framework IOKit -framework CoreFoundation
BRIDGE_LDFLAGS := -framework IOKit -framework CoreFoundation -framework ApplicationServices
TARGETS := lp998-hid-logger lp998-key-bridge

.PHONY: all clean

all: $(TARGETS)

lp998-hid-logger: lp998-hid-logger.c
	$(CC) $(CFLAGS) lp998-hid-logger.c $(HID_LDFLAGS) -o lp998-hid-logger

lp998-key-bridge: lp998-key-bridge.c
	$(CC) $(CFLAGS) lp998-key-bridge.c $(BRIDGE_LDFLAGS) -o lp998-key-bridge

clean:
	rm -f $(TARGETS)
