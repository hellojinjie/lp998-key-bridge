#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDManager.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
  MODE_ARROWS,
  MODE_PAGES,
} BridgeMode;

typedef struct {
  uint32_t vendor_id;
  uint32_t product_id;
  BridgeMode mode;
  int verbose;
  int test_key;
  int swipe_threshold;
} Options;

typedef struct {
  int active;
  int start_x;
  int start_y;
  int last_x;
  int last_y;
  int active_reports;
  int emitted;
} TouchState;

typedef struct {
  Options options;
  TouchState touch;
} BridgeContext;

enum {
  KEY_RETURN = 36,
  KEY_LEFT_ARROW = 123,
  KEY_RIGHT_ARROW = 124,
  KEY_DOWN_ARROW = 125,
  KEY_UP_ARROW = 126,
  KEY_PAGE_UP = 116,
  KEY_PAGE_DOWN = 121,
};

static void print_usage(const char *program) {
  printf("Usage: %s [--vid 0x0e05] [--pid 0x0a00] [--mode arrows|pages] [--swipe-threshold N] [--verbose] [--test-key]\n",
         program);
  printf("\n");
  printf("Bridge raw UGREEN-LP998 HID touch reports to macOS keyboard events.\n");
  printf("\n");
  printf("Default mapping in --mode arrows:\n");
  printf("  remote up/down/left/right -> arrow keys\n");
  printf("  remote center tap -> Return\n");
  printf("  remote touch photo tap -> Return\n");
  printf("  consumer volume button -> ignored\n");
  printf("\n");
  printf("Options:\n");
  printf("  --vid VALUE        Vendor ID to match. Default: 0x0e05\n");
  printf("  --pid VALUE        Product ID to match. Default: 0x0a00\n");
  printf("  --mode arrows      Map swipes to arrow keys. Default.\n");
  printf("  --mode pages       Map up/down to PageUp/PageDown, left/right to arrows.\n");
  printf("  --swipe-threshold  Minimum 2-frame movement to treat as swipe. Default: 20.\n");
  printf("  --verbose          Print decoded gestures and emitted keys.\n");
  printf("  --test-key         Emit Return once and exit. Useful for permission checks.\n");
  printf("  --help             Show this help.\n");
}

static int parse_u32(const char *value, uint32_t *out) {
  char *end = NULL;
  unsigned long parsed = strtoul(value, &end, 0);

  if (value[0] == '\0' || end == NULL || *end != '\0' || parsed > UINT32_MAX) {
    return 0;
  }

  *out = (uint32_t)parsed;
  return 1;
}

static int parse_args(int argc, char **argv, Options *options) {
  options->vendor_id = 0x0e05;
  options->product_id = 0x0a00;
  options->mode = MODE_ARROWS;
  options->verbose = 0;
  options->test_key = 0;
  options->swipe_threshold = 20;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      print_usage(argv[0]);
      exit(0);
    }

    if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
      options->verbose = 1;
      continue;
    }

    if (strcmp(argv[i], "--test-key") == 0) {
      options->test_key = 1;
      continue;
    }

    if (strcmp(argv[i], "--mode") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "Missing value for --mode\n");
        return 0;
      }

      if (strcmp(argv[i + 1], "arrows") == 0) {
        options->mode = MODE_ARROWS;
      } else if (strcmp(argv[i + 1], "pages") == 0) {
        options->mode = MODE_PAGES;
      } else {
        fprintf(stderr, "Invalid mode: %s\n", argv[i + 1]);
        return 0;
      }

      i++;
      continue;
    }

    if (strcmp(argv[i], "--swipe-threshold") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "Missing value for --swipe-threshold\n");
        return 0;
      }

      uint32_t parsed = 0;
      if (!parse_u32(argv[i + 1], &parsed) || parsed < 10 || parsed > 1000) {
        fprintf(stderr, "Invalid --swipe-threshold value: %s\n", argv[i + 1]);
        return 0;
      }

      options->swipe_threshold = (int)parsed;
      i++;
      continue;
    }

    if (strcmp(argv[i], "--vid") == 0 || strcmp(argv[i], "--pid") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "Missing value for %s\n", argv[i]);
        return 0;
      }

      uint32_t parsed = 0;
      if (!parse_u32(argv[i + 1], &parsed)) {
        fprintf(stderr, "Invalid numeric value for %s: %s\n", argv[i], argv[i + 1]);
        return 0;
      }

      if (strcmp(argv[i], "--vid") == 0) {
        options->vendor_id = parsed;
      } else {
        options->product_id = parsed;
      }

      i++;
      continue;
    }

    fprintf(stderr, "Unknown argument: %s\n", argv[i]);
    return 0;
  }

  return 1;
}

static CFNumberRef make_cf_number(uint32_t value) {
  int int_value = (int)value;
  return CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &int_value);
}

static CFMutableDictionaryRef make_matching_dictionary(const Options *options) {
  CFMutableDictionaryRef dict = CFDictionaryCreateMutable(
      kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
      &kCFTypeDictionaryValueCallBacks);

  if (dict == NULL) {
    return NULL;
  }

  CFNumberRef vendor = make_cf_number(options->vendor_id);
  CFNumberRef product = make_cf_number(options->product_id);

  if (vendor == NULL || product == NULL) {
    if (vendor != NULL) {
      CFRelease(vendor);
    }
    if (product != NULL) {
      CFRelease(product);
    }
    CFRelease(dict);
    return NULL;
  }

  CFDictionarySetValue(dict, CFSTR(kIOHIDVendorIDKey), vendor);
  CFDictionarySetValue(dict, CFSTR(kIOHIDProductIDKey), product);

  CFRelease(vendor);
  CFRelease(product);
  return dict;
}

static int get_int_property(IOHIDDeviceRef device, CFStringRef key, int fallback) {
  CFTypeRef value = IOHIDDeviceGetProperty(device, key);

  if (value == NULL || CFGetTypeID(value) != CFNumberGetTypeID()) {
    return fallback;
  }

  int result = fallback;
  if (!CFNumberGetValue((CFNumberRef)value, kCFNumberIntType, &result)) {
    return fallback;
  }

  return result;
}

static void copy_string_property(IOHIDDeviceRef device, CFStringRef key, char *buffer,
                                 size_t buffer_size, const char *fallback) {
  if (buffer_size == 0) {
    return;
  }

  CFTypeRef value = IOHIDDeviceGetProperty(device, key);

  if (value != NULL && CFGetTypeID(value) == CFStringGetTypeID() &&
      CFStringGetCString((CFStringRef)value, buffer, buffer_size, kCFStringEncodingUTF8)) {
    return;
  }

  snprintf(buffer, buffer_size, "%s", fallback);
}

static void describe_device(IOHIDDeviceRef device, char *buffer, size_t buffer_size) {
  char product[256];
  copy_string_property(device, CFSTR(kIOHIDProductKey), product, sizeof(product),
                       "Unknown HID device");

  int vendor_id = get_int_property(device, CFSTR(kIOHIDVendorIDKey), -1);
  int product_id = get_int_property(device, CFSTR(kIOHIDProductIDKey), -1);
  int usage_page = get_int_property(device, CFSTR(kIOHIDPrimaryUsagePageKey), -1);
  int usage = get_int_property(device, CFSTR(kIOHIDPrimaryUsageKey), -1);

  snprintf(buffer, buffer_size, "%s vid=0x%04x pid=0x%04x usagePage=%d usage=%d",
           product, vendor_id, product_id, usage_page, usage);
}

static void emit_key(CGKeyCode key_code, const char *name, int verbose) {
  CGEventRef down = CGEventCreateKeyboardEvent(NULL, key_code, true);
  CGEventRef up = CGEventCreateKeyboardEvent(NULL, key_code, false);

  if (down == NULL || up == NULL) {
    fprintf(stderr, "Failed to create keyboard event for %s.\n", name);
    if (down != NULL) {
      CFRelease(down);
    }
    if (up != NULL) {
      CFRelease(up);
    }
    return;
  }

  CGEventPost(kCGHIDEventTap, down);
  CGEventPost(kCGHIDEventTap, up);
  CFRelease(down);
  CFRelease(up);

  if (verbose) {
    printf("emit key=%s code=%u\n", name, key_code);
    fflush(stdout);
  }
}

static const char *key_name_for_code(CGKeyCode key_code) {
  switch (key_code) {
  case KEY_RETURN:
    return "Return";
  case KEY_LEFT_ARROW:
    return "LeftArrow";
  case KEY_RIGHT_ARROW:
    return "RightArrow";
  case KEY_DOWN_ARROW:
    return "DownArrow";
  case KEY_UP_ARROW:
    return "UpArrow";
  case KEY_PAGE_UP:
    return "PageUp";
  case KEY_PAGE_DOWN:
    return "PageDown";
  default:
    return "Unknown";
  }
}

static void emit_mapped_direction(BridgeContext *bridge, int dx, int dy) {
  CGKeyCode key_code = KEY_RETURN;

  if (abs(dx) > abs(dy)) {
    key_code = dx > 0 ? KEY_LEFT_ARROW : KEY_RIGHT_ARROW;
  } else if (bridge->options.mode == MODE_PAGES) {
    key_code = dy > 0 ? KEY_PAGE_UP : KEY_PAGE_DOWN;
  } else {
    key_code = dy > 0 ? KEY_UP_ARROW : KEY_DOWN_ARROW;
  }

  emit_key(key_code, key_name_for_code(key_code), bridge->options.verbose);
}

static void handle_tap(BridgeContext *bridge, int x, int y) {
  (void)x;
  (void)y;
  emit_key(KEY_RETURN, "Return", bridge->options.verbose);
}

static void handle_touch_report(BridgeContext *bridge, const uint8_t *report,
                                CFIndex report_length) {
  if (report_length < 5) {
    return;
  }

  uint8_t flags = report[1];
  int active = (flags & 0x01) != 0;
  int x = (int)report[2] | (((int)report[3] & 0x0f) << 8);
  int y = (((int)report[3] >> 4) & 0x0f) | ((int)report[4] << 4);

  if (bridge->options.verbose) {
    printf("touch flags=0x%02x active=%s x=%d y=%d\n", flags,
           active ? "yes" : "no", x, y);
    fflush(stdout);
  }

  if (active && !bridge->touch.active) {
    bridge->touch.active = 1;
    bridge->touch.start_x = x;
    bridge->touch.start_y = y;
    bridge->touch.last_x = x;
    bridge->touch.last_y = y;
    bridge->touch.active_reports = 1;
    bridge->touch.emitted = 0;
    return;
  }

  if (active && bridge->touch.active) {
    int dx = x - bridge->touch.start_x;
    int dy = y - bridge->touch.start_y;
    bridge->touch.last_x = x;
    bridge->touch.last_y = y;
    bridge->touch.active_reports += 1;

    if (!bridge->touch.emitted && bridge->touch.active_reports >= 2 &&
        (abs(dx) >= bridge->options.swipe_threshold ||
         abs(dy) >= bridge->options.swipe_threshold)) {
      bridge->touch.emitted = 1;
      if (bridge->options.verbose) {
        printf("gesture two-frame-swipe dx=%d dy=%d threshold=%d reports=%d\n",
               dx, dy, bridge->options.swipe_threshold,
               bridge->touch.active_reports);
        fflush(stdout);
      }
      emit_mapped_direction(bridge, dx, dy);
    }

    return;
  }

  if (!active && bridge->touch.active) {
    int dx = bridge->touch.last_x - bridge->touch.start_x;
    int dy = bridge->touch.last_y - bridge->touch.start_y;
    int last_x = bridge->touch.last_x;
    int last_y = bridge->touch.last_y;
    int already_emitted = bridge->touch.emitted;
    bridge->touch.active = 0;
    bridge->touch.emitted = 0;

    if (already_emitted) {
      if (bridge->options.verbose) {
        printf("gesture release ignored after two-frame emit dx=%d dy=%d\n", dx, dy);
        fflush(stdout);
      }
    } else if (abs(dx) >= bridge->options.swipe_threshold ||
               abs(dy) >= bridge->options.swipe_threshold) {
      if (bridge->options.verbose) {
        printf("gesture swipe dx=%d dy=%d\n", dx, dy);
        fflush(stdout);
      }
      emit_mapped_direction(bridge, dx, dy);
    } else {
      if (bridge->options.verbose) {
        printf("gesture tap x=%d y=%d dx=%d dy=%d\n", last_x, last_y, dx, dy);
        fflush(stdout);
      }
      handle_tap(bridge, last_x, last_y);
    }
  }
}

static void input_report(void *context, IOReturn result, void *sender,
                         IOHIDReportType type, uint32_t report_id,
                         uint8_t *report, CFIndex report_length) {
  (void)result;
  (void)sender;
  (void)type;

  BridgeContext *bridge = (BridgeContext *)context;

  if (report_id == 6) {
    handle_touch_report(bridge, report, report_length);
    return;
  }

  if (bridge->options.verbose && report_id == 3 && report_length >= 3) {
    uint16_t usage = (uint16_t)report[1] | ((uint16_t)report[2] << 8);
    printf("consumer usage=0x%04x ignored\n", usage);
    fflush(stdout);
  }
}

static void device_matched(void *context, IOReturn result, void *sender,
                           IOHIDDeviceRef device) {
  (void)context;
  (void)result;
  (void)sender;

  char description[512];
  describe_device(device, description, sizeof(description));
  printf("matched: %s\n", description);
  fflush(stdout);
}

static void device_removed(void *context, IOReturn result, void *sender,
                           IOHIDDeviceRef device) {
  (void)context;
  (void)result;
  (void)sender;

  char description[512];
  describe_device(device, description, sizeof(description));
  printf("removed: %s\n", description);
  fflush(stdout);
}

int main(int argc, char **argv) {
  BridgeContext bridge;
  memset(&bridge, 0, sizeof(bridge));

  if (!parse_args(argc, argv, &bridge.options)) {
    print_usage(argv[0]);
    return 2;
  }

  if (bridge.options.test_key) {
    emit_key(KEY_RETURN, "Return", 1);
    return 0;
  }

  IOHIDManagerRef manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
  if (manager == NULL) {
    fprintf(stderr, "Failed to create IOHIDManager.\n");
    return 1;
  }

  CFMutableDictionaryRef matching = make_matching_dictionary(&bridge.options);
  if (matching == NULL) {
    fprintf(stderr, "Failed to create matching dictionary.\n");
    CFRelease(manager);
    return 1;
  }

  IOHIDManagerSetDeviceMatching(manager, matching);
  CFRelease(matching);

  IOHIDManagerRegisterDeviceMatchingCallback(manager, device_matched, &bridge);
  IOHIDManagerRegisterDeviceRemovalCallback(manager, device_removed, &bridge);
  IOHIDManagerRegisterInputReportCallback(manager, input_report, &bridge);
  IOHIDManagerScheduleWithRunLoop(manager, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);

  printf("LP998 key bridge listening for VendorID=0x%04x ProductID=0x%04x mode=%s.\n",
         bridge.options.vendor_id, bridge.options.product_id,
         bridge.options.mode == MODE_PAGES ? "pages" : "arrows");
  fflush(stdout);

  IOReturn open_result = IOHIDManagerOpen(manager, kIOHIDOptionsTypeNone);
  if (open_result != kIOReturnSuccess) {
    fprintf(stderr, "Failed to open IOHIDManager: 0x%08x\n", open_result);
    if (open_result == kIOReturnNotPermitted) {
      fprintf(stderr, "This means macOS denied HID access.\n");
    }
    fprintf(stderr, "Check System Settings > Privacy & Security > Input Monitoring.\n");
    CFRelease(manager);
    return 1;
  }

  CFSetRef devices = IOHIDManagerCopyDevices(manager);
  CFIndex device_count = devices == NULL ? 0 : CFSetGetCount(devices);
  printf("Initial matched devices: %ld\n", (long)device_count);
  if (devices != NULL) {
    CFRelease(devices);
  }

  printf("Press LP998 buttons. Press Ctrl-C to stop.\n");
  printf("If key events are not delivered, allow this terminal in Accessibility.\n");
  fflush(stdout);

  CFRunLoopRun();

  IOHIDManagerUnscheduleFromRunLoop(manager, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
  IOHIDManagerClose(manager, kIOHIDOptionsTypeNone);
  CFRelease(manager);
  return 0;
}
