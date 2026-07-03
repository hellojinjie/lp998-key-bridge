#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDManager.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
  uint32_t vendor_id;
  uint32_t product_id;
  int listen_all;
} Options;

static void print_usage(const char *program) {
  printf("Usage: %s [--vid 0x0e05] [--pid 0x0a00] [--all]\n", program);
  printf("\n");
  printf("Listen for raw HID input reports from UGREEN-LP998.\n");
  printf("\n");
  printf("Options:\n");
  printf("  --vid VALUE   Vendor ID to match. Default: 0x0e05\n");
  printf("  --pid VALUE   Product ID to match. Default: 0x0a00\n");
  printf("  --all         Listen to all HID devices. Useful for diagnosis.\n");
  printf("  --help        Show this help.\n");
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
  options->listen_all = 0;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      print_usage(argv[0]);
      exit(0);
    }

    if (strcmp(argv[i], "--all") == 0) {
      options->listen_all = 1;
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

static void timestamp_now(char *buffer, size_t buffer_size) {
  struct timespec ts;
  struct tm tm_value;

  clock_gettime(CLOCK_REALTIME, &ts);
  localtime_r(&ts.tv_sec, &tm_value);
  strftime(buffer, buffer_size, "%H:%M:%S", &tm_value);

  size_t used = strlen(buffer);
  if (used < buffer_size) {
    snprintf(buffer + used, buffer_size - used, ".%03ld", ts.tv_nsec / 1000000);
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

static const char *consumer_usage_name(uint16_t usage) {
  switch (usage) {
  case 0x00e2:
    return "mute";
  case 0x00e9:
    return "volume_increment";
  case 0x00ea:
    return "volume_decrement";
  case 0x00b0:
    return "play";
  case 0x00b1:
    return "pause";
  case 0x00b5:
    return "scan_next_track";
  case 0x00b6:
    return "scan_previous_track";
  case 0x0000:
    return "release";
  default:
    return "unknown";
  }
}

static void print_decoded_report(uint32_t report_id, const uint8_t *report,
                                 CFIndex report_length) {
  if (report_id == 6 && report_length >= 5) {
    uint8_t flags = report[1];
    int x = (int)report[2] | (((int)report[3] & 0x0f) << 8);
    int y = (((int)report[3] >> 4) & 0x0f) | ((int)report[4] << 4);

    printf(" decoded=touch flags=0x%02x active=%s x=%d y=%d", flags,
           (flags & 0x03) ? "yes" : "no", x, y);
    return;
  }

  if (report_id == 3 && report_length >= 3) {
    uint16_t usage = (uint16_t)report[1] | ((uint16_t)report[2] << 8);
    printf(" decoded=consumer usage=0x%04x name=%s", usage,
           consumer_usage_name(usage));
    return;
  }

  printf(" decoded=unknown");
}

static void input_report(void *context, IOReturn result, void *sender,
                         IOHIDReportType type, uint32_t report_id,
                         uint8_t *report, CFIndex report_length) {
  (void)context;
  (void)type;

  char timestamp[64];
  char description[512];
  timestamp_now(timestamp, sizeof(timestamp));

  if (sender != NULL) {
    describe_device((IOHIDDeviceRef)sender, description, sizeof(description));
  } else {
    snprintf(description, sizeof(description), "Unknown HID sender");
  }

  printf("%s result=0x%08x reportId=%u length=%ld device=\"%s\" data=",
         timestamp, result, report_id, (long)report_length, description);

  for (CFIndex i = 0; i < report_length; i++) {
    printf("%s%02x", i == 0 ? "" : " ", report[i]);
  }

  print_decoded_report(report_id, report, report_length);
  printf("\n");
  fflush(stdout);
}

int main(int argc, char **argv) {
  Options options;
  if (!parse_args(argc, argv, &options)) {
    print_usage(argv[0]);
    return 2;
  }

  IOHIDManagerRef manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
  if (manager == NULL) {
    fprintf(stderr, "Failed to create IOHIDManager.\n");
    return 1;
  }

  if (options.listen_all) {
    IOHIDManagerSetDeviceMatching(manager, NULL);
    printf("Listening to all HID devices.\n");
    fflush(stdout);
  } else {
    CFMutableDictionaryRef matching = make_matching_dictionary(&options);
    if (matching == NULL) {
      fprintf(stderr, "Failed to create matching dictionary.\n");
      CFRelease(manager);
      return 1;
    }

    IOHIDManagerSetDeviceMatching(manager, matching);
    CFRelease(matching);
    printf("Listening for VendorID=0x%04x ProductID=0x%04x.\n",
           options.vendor_id, options.product_id);
    fflush(stdout);
  }

  IOHIDManagerRegisterDeviceMatchingCallback(manager, device_matched, NULL);
  IOHIDManagerRegisterDeviceRemovalCallback(manager, device_removed, NULL);
  IOHIDManagerRegisterInputReportCallback(manager, input_report, NULL);
  IOHIDManagerScheduleWithRunLoop(manager, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);

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

  if (devices != NULL && device_count > 0) {
    IOHIDDeviceRef *device_refs = calloc((size_t)device_count, sizeof(IOHIDDeviceRef));
    if (device_refs != NULL) {
      CFSetGetValues(devices, (const void **)device_refs);
      for (CFIndex i = 0; i < device_count; i++) {
        char description[512];
        describe_device(device_refs[i], description, sizeof(description));
        printf("device[%ld]: %s\n", (long)i, description);
      }
      free(device_refs);
    }
    CFRelease(devices);
  }

  printf("Press buttons on the remote. Press Ctrl-C to stop.\n");
  fflush(stdout);
  CFRunLoopRun();

  IOHIDManagerUnscheduleFromRunLoop(manager, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
  IOHIDManagerClose(manager, kIOHIDOptionsTypeNone);
  CFRelease(manager);
  return 0;
}
