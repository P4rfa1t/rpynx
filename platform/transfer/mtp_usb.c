#include <switch.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "mtp_usb.h"
#include "mtp_core.h"

#define CHUNK (4 * 1024 * 1024)
#define USB_WAIT_NS 100000000ULL

static UsbDsInterface *interface;
static UsbDsEndpoint *bulk_in, *bulk_out, *interrupt_in;
static _Alignas(4096) unsigned char incoming[2][CHUNK], outgoing[CHUNK];
static int incoming_cur;
static bool incoming_pending;
static uint32_t incoming_urb;
static Thread worker;
static atomic_bool stopping;
static Mutex mutex;
static rsl_ftpd_status state;
static bool started;
static size_t read_pos, read_end, write_pos;
static uint32_t write_left, write_total;
static const char *storage_root;

static Result configure(void)
{
    u8 vendor = 0, product = 0, serial = 0, name = 0;
    const u16 languages[] = { 0x0409 };
    Result rc = usbDsAddUsbLanguageStringDescriptor(NULL, languages, 1);
    if (R_SUCCEEDED(rc)) rc = usbDsAddUsbStringDescriptor(&vendor, "rpynx");
    if (R_SUCCEEDED(rc)) rc = usbDsAddUsbStringDescriptor(&product, "rpynx");
    if (R_SUCCEEDED(rc)) rc = usbDsAddUsbStringDescriptor(&serial, "rpynx-1");
    if (R_SUCCEEDED(rc)) rc = usbDsAddUsbStringDescriptor(&name, "MTP");
    if (R_SUCCEEDED(rc)) rc = usbDsRegisterInterface(&interface);
    if (R_FAILED(rc)) return rc;
    u8 index = interface->interface_index;
    /* 3000 is libnx usbComms' ID and often has an exact-match WinUSB driver. */
    struct usb_device_descriptor device = {
        .bLength = USB_DT_DEVICE_SIZE, .bDescriptorType = USB_DT_DEVICE,
        .bMaxPacketSize0 = 64, .idVendor = 0x057e, .idProduct = 0x3001, .bcdDevice = 0x0100,
        .iManufacturer = vendor, .iProduct = product, .iSerialNumber = serial, .bNumConfigurations = 1,
    };
    struct usb_interface_descriptor desc = {
        .bLength = USB_DT_INTERFACE_SIZE, .bDescriptorType = USB_DT_INTERFACE,
        .bInterfaceNumber = index, .bNumEndpoints = 3,
        .bInterfaceClass = 6, .bInterfaceSubClass = 1, .bInterfaceProtocol = 1, .iInterface = name,
    };
    struct usb_endpoint_descriptor endpoints[] = {
        { .bLength = USB_DT_ENDPOINT_SIZE, .bDescriptorType = USB_DT_ENDPOINT,
          .bEndpointAddress = 0x80 | (index + 1), .bmAttributes = USB_TRANSFER_TYPE_BULK },
        { .bLength = USB_DT_ENDPOINT_SIZE, .bDescriptorType = USB_DT_ENDPOINT,
          .bEndpointAddress = index + 1, .bmAttributes = USB_TRANSFER_TYPE_BULK },
        { .bLength = USB_DT_ENDPOINT_SIZE, .bDescriptorType = USB_DT_ENDPOINT,
          .bEndpointAddress = 0x80 | (index + 2), .bmAttributes = USB_TRANSFER_TYPE_INTERRUPT,
          .wMaxPacketSize = 28, .bInterval = 8 },
    };
    const struct {
        UsbDeviceSpeed speed;
        uint16_t revision, packet;
        uint8_t control;
    } speeds[] = {
        { UsbDeviceSpeed_Full, 0x0110, 64, 64 },
        { UsbDeviceSpeed_High, 0x0200, 512, 64 },
        { UsbDeviceSpeed_Super, 0x0300, 1024, 9 },
    };
    const struct usb_ss_endpoint_companion_descriptor companion = {
        .bLength = 6, .bDescriptorType = USB_DT_SS_ENDPOINT_COMPANION,
        .bMaxBurst = 0x0f,
    };
    const unsigned char bos[] = { 5, USB_DT_BOS, 22, 0, 2,
        7, USB_DT_DEVICE_CAPABILITY, 2, 2, 0, 0, 0,
        10, USB_DT_DEVICE_CAPABILITY, 3, 0, 0x0c, 0, 3, 0, 0, 0 };
    rc = usbDsSetBinaryObjectStore(bos, sizeof bos);
    for (size_t i = 0; R_SUCCEEDED(rc) && i < sizeof speeds / sizeof speeds[0]; ++i) {
        device.bcdUSB = speeds[i].revision;
        device.bMaxPacketSize0 = speeds[i].control;
        rc = usbDsSetUsbDeviceDescriptor(speeds[i].speed, &device);
        if (R_SUCCEEDED(rc))
            rc = usbDsInterface_AppendConfigurationData(interface, speeds[i].speed, &desc, desc.bLength);
        endpoints[0].wMaxPacketSize = endpoints[1].wMaxPacketSize = speeds[i].packet;
        for (size_t j = 0; R_SUCCEEDED(rc) && j < 3; ++j) {
            rc = usbDsInterface_AppendConfigurationData(interface, speeds[i].speed,
                                                        &endpoints[j], endpoints[j].bLength);
            if (R_SUCCEEDED(rc) && speeds[i].speed == UsbDeviceSpeed_Super) {
                struct usb_ss_endpoint_companion_descriptor cap = companion;
                if (j == 2) {
                    cap.bMaxBurst = 0;
                    cap.wBytesPerInterval = endpoints[j].wMaxPacketSize;
                }
                rc = usbDsInterface_AppendConfigurationData(interface, speeds[i].speed, &cap, cap.bLength);
            }
        }
    }
    if (R_SUCCEEDED(rc)) rc = usbDsInterface_RegisterEndpoint(interface, &bulk_in, endpoints[0].bEndpointAddress);
    if (R_SUCCEEDED(rc)) rc = usbDsInterface_RegisterEndpoint(interface, &bulk_out, endpoints[1].bEndpointAddress);
    if (R_SUCCEEDED(rc)) rc = usbDsInterface_RegisterEndpoint(interface, &interrupt_in, endpoints[2].bEndpointAddress);
    if (R_SUCCEEDED(rc)) rc = usbDsInterface_EnableInterface(interface);
    return R_SUCCEEDED(rc) ? usbDsEnable() : rc;
}

static bool configured(void)
{
    UsbState s;
    return R_SUCCEEDED(usbDsGetState(&s)) && s == UsbState_Configured;
}

static uint32_t bulk_packet_size(void)
{
    UsbDeviceSpeed speed = UsbDeviceSpeed_High;
    if (R_SUCCEEDED(usbDsGetSpeed(&speed))) {
        if (speed == UsbDeviceSpeed_Super) return 1024;
        if (speed == UsbDeviceSpeed_High) return 512;
        return 64;
    }
    return 512;
}

static int exchange(UsbDsEndpoint *ep, void *buffer, size_t size)
{
    uint32_t id, transferred;
    UsbDsReportData report;
    if (atomic_load(&stopping) || !configured()) return -1;
    if (R_FAILED(usbDsEndpoint_PostBufferAsync(ep, buffer, size, &id))) return -1;
    while (R_FAILED(eventWait(&ep->CompletionEvent, USB_WAIT_NS))) {
        if (atomic_load(&stopping) || !configured()) {
            usbDsEndpoint_Cancel(ep);
            return -1;
        }
    }
    eventClear(&ep->CompletionEvent);
    if (R_FAILED(usbDsEndpoint_GetReportData(ep, &report)) ||
        R_FAILED(usbDsParseReportData(&report, id, NULL, &transferred)) || transferred > size)
        return -1;
    return (int)transferred;
}

static void cancel_incoming(void)
{
    if (incoming_pending) {
        usbDsEndpoint_Cancel(bulk_out);
        incoming_pending = false;
    }
}

static bool post_incoming(int index)
{
    if (atomic_load(&stopping) || !configured()) return false;
    if (R_FAILED(usbDsEndpoint_PostBufferAsync(bulk_out, incoming[index], CHUNK, &incoming_urb)))
        return false;
    incoming_pending = true;
    return true;
}

static int next_incoming(int *out_index)
{
    uint32_t transferred = 0;
    UsbDsReportData report;
    if (!incoming_pending && !post_incoming(incoming_cur)) return -1;
    while (R_FAILED(eventWait(&bulk_out->CompletionEvent, USB_WAIT_NS))) {
        if (atomic_load(&stopping) || !configured()) {
            cancel_incoming();
            return -1;
        }
    }
    eventClear(&bulk_out->CompletionEvent);
    incoming_pending = false;
    if (R_FAILED(usbDsEndpoint_GetReportData(bulk_out, &report)) ||
        R_FAILED(usbDsParseReportData(&report, incoming_urb, NULL, &transferred)) ||
        transferred > CHUNK)
        return -1;
    int ready = incoming_cur;
    /* Pre-post the sibling buffer while SD I/O runs so USB 3.0 and SD writes overlap. */
    if (transferred == CHUNK) {
        incoming_cur ^= 1;
        post_incoming(incoming_cur);
    }
    *out_index = ready;
    return (int)transferred;
}

static bool read_bytes(void *user, void *buffer, size_t size)
{
    (void)user;
    unsigned char *out = buffer;
    static int active_buf;
    while (size) {
        if (read_pos == read_end) {
            int n = next_incoming(&active_buf);
            if (n < 0) return false;
            if (!n) continue;
            read_pos = 0;
            read_end = (size_t)n;
        }
        size_t copy = read_end - read_pos;
        if (copy > size) copy = size;
        memcpy(out, incoming[active_buf] + read_pos, copy);
        out += copy;
        read_pos += copy;
        size -= copy;
    }
    return true;
}

static bool write_bytes(void *user, const void *buffer, size_t size)
{
    (void)user;
    const unsigned char *src = buffer;
    while (size) {
        if (write_left == 0) {
            if (size < 12) return false;
            write_left = (uint32_t)src[0] | (uint32_t)src[1] << 8 | (uint32_t)src[2] << 16 | (uint32_t)src[3] << 24;
            if (write_left < 12) return false;
            write_total = write_left;
        }
        size_t copy = sizeof outgoing - write_pos;
        if (copy > size) copy = size;
        if (copy > write_left) copy = write_left;
        memcpy(outgoing + write_pos, src, copy);
        write_pos += copy;
        write_left -= copy;
        src += copy;
        size -= copy;
        if (!write_left || write_pos == sizeof outgoing) {
            bool sent = exchange(bulk_in, outgoing, write_pos) == (int)write_pos;
            write_pos = 0;
            if (!sent) return false;
            /* Terminate a container whose total length is an exact multiple of wMaxPacketSize
               with a Zero-Length Packet (ZLP) so hosts (Windows WPD / Explorer) do not stall. */
            if (!write_left && (write_total % bulk_packet_size()) == 0) {
                if (exchange(bulk_in, outgoing, 0) != 0) return false;
            }
        }
    }
    return true;
}

static void set_connected(bool connected)
{
    const char *label = "";
    if (connected) {
        UsbDeviceSpeed speed = UsbDeviceSpeed_High;
        if (R_SUCCEEDED(usbDsGetSpeed(&speed))) {
            label = speed == UsbDeviceSpeed_Super ? "USB 3.0" :
                    speed == UsbDeviceSpeed_High ? "USB 2.0" : "USB 1.1";
        } else {
            label = "USB";
        }
    }
    mutexLock(&mutex);
    state.connected = connected;
    if (!connected) state.file[0] = '\0';
    snprintf(state.client, sizeof state.client, "%s", label);
    mutexUnlock(&mutex);
}

static void progress(void *user, const char *file, uint64_t bytes, uint64_t total, bool finished)
{
    (void)user;
    mutexLock(&mutex);
    if (*file) snprintf(state.file, sizeof state.file, "%s", file);
    state.expect = finished ? 0 : total;
    state.bytes = state.expect_from + bytes;
    if (finished) {
        state.expect_from = state.bytes;
        if (bytes == total) ++state.files;
    }
    mutexUnlock(&mutex);
}

static void serve(void *arg)
{
    (void)arg;
    while (!atomic_load(&stopping)) {
        if (!configured()) {
            svcSleepThread(USB_WAIT_NS);
            continue;
        }
        MtpCore *core = mtp_core_new(storage_root, read_bytes, write_bytes, progress, NULL);
        if (!core) break;
        read_pos = read_end = write_pos = write_left = write_total = 0;
        incoming_cur = 0;
        incoming_pending = false;
        set_connected(true);
        while (!atomic_load(&stopping) && mtp_core_step(core)) {}
        cancel_incoming();
        mtp_core_free(core);
        set_connected(false);
        svcSleepThread(USB_WAIT_NS);
    }
    mutexLock(&mutex);
    state.running = false;
    mutexUnlock(&mutex);
}

bool rsl_mtp_start(const char *root, char *error, size_t error_size)
{
    if (started) return true;
    Result rc = usbDsInitialize();
    if (R_FAILED(rc)) goto failed;
    rc = configure();
    if (R_FAILED(rc)) goto usb_failed;
    mutexInit(&mutex);
    memset(&state, 0, sizeof state);
    storage_root = root;
    atomic_store(&stopping, false);
    rc = threadCreate(&worker, serve, NULL, NULL, 128 * 1024, 0x2c, 1);
    if (R_FAILED(rc)) goto usb_failed;
    state.running = true;
    rc = threadStart(&worker);
    if (R_FAILED(rc)) {
        threadClose(&worker);
        goto usb_failed;
    }
    started = true;
    return true;
usb_failed:
    usbDsExit();
failed:
    snprintf(error, error_size, "0x%x", (unsigned)rc);
    return false;
}

void rsl_mtp_stop(void)
{
    if (!started) return;
    atomic_store(&stopping, true);
    threadWaitForExit(&worker);
    threadClose(&worker);
    usbDsDisable();
    usbDsExit();
    started = false;
}

void rsl_mtp_get(rsl_ftpd_status *status)
{
    mutexLock(&mutex);
    *status = state;
    mutexUnlock(&mutex);
}
