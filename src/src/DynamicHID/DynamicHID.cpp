/*
  Modified by Matthew Heironimus to support HID Report Descriptors to be in 
  standard RAM in addition to program memory (PROGMEM).

   Copyright (c) 2015, Arduino LLC
   Original code (pre-library): Copyright (c) 2011, Peter Barrett

   Permission to use, copy, modify, and/or distribute this software for
   any purpose with or without fee is hereby granted, provided that the
   above copyright notice and this permission notice appear in all copies.

   THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
   WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR
   BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES
   OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
   WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
   ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
   SOFTWARE.
 */

#include "DynamicHID.h"

#if defined(USBCON)

#ifdef _VARIANT_ARDUINO_DUE_X_
#define USB_SendControl USBD_SendControl
#define USB_Send USBD_Send
#define USB_Recv USBD_Recv
#define USB_RecvControl USBD_RecvControl
#define USB_Available USBD_Available
#endif

// Singleton pattern for the DynamicHID_ class.
// Ensures that only one instance of the DynamicHID_ class exists.
DynamicHID_& DynamicHID()
{
    static DynamicHID_ obj; // Static object created once and returned on each call.
    return obj;
}

// Returns the HID interface and sends HID descriptors to the host.
int DynamicHID_::getInterface(uint8_t* interfaceCount)
{
    *interfaceCount += 1; // Increment the number of interfaces by 1.
    
    // Define the HID interface descriptor.
    DYNAMIC_HIDDescriptor hidInterface = {
        D_INTERFACE(pluggedInterface, PID_ENPOINT_COUNT, USB_DEVICE_CLASS_HUMAN_INTERFACE, DYNAMIC_HID_SUBCLASS_NONE, DYNAMIC_HID_PROTOCOL_NONE),
        D_HIDREPORT(descriptorSize),
        D_ENDPOINT(USB_ENDPOINT_IN(PID_ENDPOINT_IN), USB_ENDPOINT_TYPE_INTERRUPT, USB_EP_SIZE, 0x01),
        D_ENDPOINT(USB_ENDPOINT_OUT(PID_ENDPOINT_OUT), USB_ENDPOINT_TYPE_INTERRUPT, USB_EP_SIZE, 0x01)
    };
    
    // Send the descriptor to the host.
    return USB_SendControl(0, &hidInterface, sizeof(hidInterface));
}

// Handles a request for an HID descriptor from the host.
int DynamicHID_::getDescriptor(USBSetup& setup)
{
    // Check if the request is of the correct type.
    if (setup.bmRequestType != REQUEST_DEVICETOHOST_STANDARD_INTERFACE) { return 0; }
    if (setup.wValueH != DYNAMIC_HID_REPORT_DESCRIPTOR_TYPE) { return 0; }

    // Check if the interface number is correct.
    if (setup.wIndex != pluggedInterface) { return 0; }

    // Iterate through all HID sub-descriptors and send them to the host.
    int total = 0;
    DynamicHIDSubDescriptor* node;
    for (node = rootNode; node; node = node->next) {
        // node->data may be RAM or PROGMEM (inProgMem). The joystick descriptor moved to
        // PROGMEM to reclaim its 150 B static buffer; pid_data below is always PROGMEM.
        int res = USB_SendControl(node->inProgMem ? TRANSFER_PGM : 0, node->data, node->length);
        if (res == -1)
            return -1; // Abort on error.
        total += res;

        // Optionally send additional PID data.
        res = USB_SendControl(TRANSFER_PGM, node->pid_data, node->pid_length);
        if (res == -1)
            return -1; // Abort on error.
        total += res;
    }

    // Reset the protocol to the default (report mode).
    protocol = DYNAMIC_HID_REPORT_PROTOCOL;

    return total; // Return the total amount of data sent.
}

// Returns the short name of the device based on descriptor size.
uint8_t DynamicHID_::getShortName(char *name)
{
    name[0] = 'H'; // 'HID' is used as a standard prefix.
    name[1] = 'I';
    name[2] = 'D';
    // Adds additional characters based on descriptor size.
    name[3] = 'A' + (descriptorSize & 0x0F);
    name[4] = 'A' + ((descriptorSize >> 4) & 0x0F);
    return 5; // Returns the length of the generated name (5 characters).
}

// Adds a new descriptor to the linked list of sub-descriptors.
void DynamicHID_::AppendDescriptor(DynamicHIDSubDescriptor *node)
{
    // If no descriptors exist, set the new node as the root.
    if (!rootNode) {
        rootNode = node;
    } else {
        // Traverse the list to find the end.
        DynamicHIDSubDescriptor *current = rootNode;
        while (current->next) {
            current = current->next;
        }
        current->next = node; // Add the new node at the end.
    }
    // Update the total size of the descriptors.
    descriptorSize += node->length;
    descriptorSize += node->pid_length;
}

// Sends a HID report to the host.
int DynamicHID_::SendReport(uint8_t id, const void* data, int len)
{
    // USB_Send() is BLOCKING (the core's own header says so): while the IN bank still
    // holds the previous report - the host has not polled it yet - it spins on delay(1)
    // for up to 250 ms. sendState() runs in the main loop *ahead of* updateEffects(), so
    // a stalled joystick report also stalls RecvfromUsb() (host effect Start/Stop/params
    // pile up unprocessed) and the motor update behind it.
    //
    // A joystick position report is worthless the moment a newer one exists, so drop it
    // instead of waiting: the next loop pass sends fresher data. This also self-clamps
    // the report rate to the host's 1 kHz polling with zero dead time, no magic constant.
    if (USB_SendSpace(PID_ENDPOINT_IN) < (uint8_t)(len + 1))
        return -1;   // bank busy - skip this frame's position, never block the FFB loop

    // Create an array containing the report ID and the data.
    uint8_t p[len + 1];
    p[0] = id;
    memcpy(&p[1], data, len); // Copy the data into the array.
    
    // Send the data through the IN endpoint.
    return USB_Send(PID_ENDPOINT_IN | TRANSFER_RELEASE, p, len + 1);
}

// Receives data from the USB OUT endpoint and stores it in the data buffer.
int DynamicHID_::RecvData(byte* data)
{
    int count = 0;
    // Read all available data from the USB buffer.
    while (usb_Available() > 0) {
        data[count++] = USB_Recv(PID_ENDPOINT_OUT);
    }
    return count; // Return the number of bytes received.
}

// This function checks if data is available and reads it when present.
void DynamicHID_::RecvfromUsb()
{
    // Drain the OUT endpoint. TelemFFB (and any rich FFB host) streams ONE parameter
    // report per active effect per telemetry frame - 10-20 reports arriving together.
    // Processing a single report per main-loop pass lets that batch back up in the
    // endpoint FIFO, so effect parameters land late and in bursts -> forces feel jerky
    // and effects seem to randomly not take. The guard bounds the worst case (a flood)
    // so the FFB path can't starve the encoder read / motor PWM.
    uint8_t out_ffbdata[64]; // Data buffer.
    // Guard bounds a pathological flood; 64 clears a full per-frame burst (TelemFFB can
    // resend Set Effect + Set Constant Force + Set Periodic for ~15-18 effects at once)
    // in a single pass so nothing carries over to back up behind the next frame.
    for (uint8_t guard = 0; guard < 64 && usb_Available() > 0; ++guard) {
        int len = USB_Recv(PID_ENDPOINT_OUT, &out_ffbdata, 64); // one report per read
        if (len <= 0) break;   // D4: was `uint16_t len; if (len >= 0)` - always true, -1 wrapped to 65535
        // D5: every handler in UppackUsbData casts this buffer to a struct that may be
        // longer than the report actually received, and reads the difference in as effect
        // parameters. The read stays inside the buffer so nothing is corrupted - it simply
        // consumes whatever the previous iteration left there, silently and with no
        // repeatability. Zeroing the tail makes that case defined and harmless.
        // Deliberately NOT a rejecting length check: if the report descriptor declares
        // fewer bytes than one of those structs, rejecting would silently disable that
        // effect type, and this firmware has had enough silent behaviour changes.
        // UppackUsbData counts mismatches (shortReportCount) so they can be measured
        // before anything starts refusing reports.
        // Only up to the longest report struct: past PID_MAX_OUT_REPORT no handler ever
        // reads, so zeroing the rest of the 64 B buffer was work for nothing. Measured
        // 2026-09-08: shortReportCount stayed 0 across 870 s of TelemFFB with effects, so
        // this path does not currently fire at all - it is here for a host that truncates.
        if ((uint8_t)len < PID_MAX_OUT_REPORT)
            memset(out_ffbdata + len, 0, PID_MAX_OUT_REPORT - (uint8_t)len);
        pidReportHandler.UppackUsbData(out_ffbdata, (uint16_t)len);
#ifdef FFB_SERIAL_TRACE
        pidReportHandler.rxReportCount++;
#endif
    }
}

// This function handles GET_REPORT requests from the host.
bool DynamicHID_::GetReport(USBSetup& setup) {
    uint8_t report_id = setup.wValueL;
    uint8_t report_type = setup.wValueH;

    if (report_type == DYNAMIC_HID_REPORT_TYPE_INPUT) {
        // Send the PID status report back.
        USB_SendControl(TRANSFER_RELEASE, pidReportHandler.getPIDStatus(), sizeof(USB_FFBReport_PIDStatus_Input_Data_t));
    }
    if (report_type == DYNAMIC_HID_REPORT_TYPE_OUTPUT) {
        // No implementation for OUTPUT types in this example.
    }
    if (report_type == DYNAMIC_HID_REPORT_TYPE_FEATURE) {
        // Example for FEATURE reports:
        if (report_id == 6) {
            delayMicroseconds(500);
            USB_SendControl(TRANSFER_RELEASE, pidReportHandler.getPIDBlockLoad(), sizeof(USB_FFBReport_PIDBlockLoad_Feature_Data_t));
            pidReportHandler.pidBlockLoad.reportId = 0; // Reset the report ID.
            return true;
        }
        if (report_id == 7) {
            // Send information about the PID memory pool. This device is device-managed
            // (memoryManagement bit0), so ramPoolSize is informational - Windows tracks
            // capacity via RAM Pool Available in the Block Load reports (see C1).
            USB_FFBReport_PIDPool_Feature_Data_t ans;
            ans.reportId = report_id;
            ans.ramPoolSize = 0xffff;
            ans.maxSimultaneousEffects = MAX_EFFECTS;
            ans.memoryManagement = 1;   // F3: Device Managed Pool only (not Shared Parameter Blocks)
            USB_SendControl(TRANSFER_RELEASE, &ans, sizeof(USB_FFBReport_PIDPool_Feature_Data_t));
            return true;
        }
    }
    return false; // Report not recognized or supported.
}

// Processes SET_REPORT requests from the host.
bool DynamicHID_::SetReport(USBSetup& setup) {
    uint8_t report_id = setup.wValueL;
    uint8_t report_type = setup.wValueH;
    uint16_t length = setup.wLength;
    uint8_t data[10]; // Buffer for incoming data.

    if (report_type == DYNAMIC_HID_REPORT_TYPE_FEATURE) {
        if (length == 0) {
            USB_RecvControl(&data, length); // Receive control data.
            return true;
        }
        if (report_id == 5) {
            USB_FFBReport_CreateNewEffect_Feature_Data_t ans;
            USB_RecvControl(&ans, sizeof(USB_FFBReport_CreateNewEffect_Feature_Data_t));
            pidReportHandler.CreateNewEffect(&ans); // Create a new effect based on the received data.
        }
        return true;
    }
    return false; // Report not supported.
}

// This function processes general USB setup requests from the host.
bool DynamicHID_::setup(USBSetup& setup)
{
    if (pluggedInterface != setup.wIndex) {
        return false; // Wrong interface, ignore the request.
    }

    uint8_t request = setup.bRequest;
    uint8_t requestType = setup.bmRequestType;

    if (requestType == REQUEST_DEVICETOHOST_CLASS_INTERFACE) {
        if (request == DYNAMIC_HID_GET_REPORT) {
            GetReport(setup); // Process GET_REPORT request.
            return true;
        }
        if (request == DYNAMIC_HID_GET_PROTOCOL) {
            // TODO: Send protocol to the host.
            return true;
        }
        if (request == DYNAMIC_HID_GET_IDLE) {
            // TODO: Send idle status to the host.
        }
    }

    if (requestType == REQUEST_HOSTTODEVICE_CLASS_INTERFACE) {
        if (request == DYNAMIC_HID_SET_PROTOCOL) {
            // The USB host tells us if we are in boot or report mode.
            protocol = setup.wValueL; // Update the protocol based on host request.
            return true;
        }
        if (request == DYNAMIC_HID_SET_IDLE) {
            idle = setup.wValueL; // Update the idle rate based on host request.
            return true;
        }
        if (request == DYNAMIC_HID_SET_REPORT) {
            SetReport(setup); // Process SET_REPORT request.
            return true;
        }
    }
    return false; // Request not handled.
}

// Constructor for the DynamicHID_ class, initializing the interface and endpoints.
DynamicHID_::DynamicHID_(void) : PluggableUSBModule(PID_ENPOINT_COUNT, 1, epType),
                   rootNode(NULL), descriptorSize(0),
                   protocol(DYNAMIC_HID_REPORT_PROTOCOL), idle(1)
{
    // Initialize the endpoints for IN and OUT.
	epType[0] = EP_TYPE_INTERRUPT_IN;
	epType[1] = EP_TYPE_INTERRUPT_OUT;
    PluggableUSB().plug(this); // Register this class as a USB device.
}

int DynamicHID_::begin(void)
{
	return 0;
}

bool DynamicHID_::usb_Available() {
	return USB_Available(PID_ENDPOINT_OUT);
}

#endif /* if defined(USBCON) */
