#ifndef DAB_USB_CONSOLE_H
#define DAB_USB_CONSOLE_H

#include "hal_data.h"
#include "r_usb_basic.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Non-blocking USB CDC console used by both production and isolated test
 * firmware.  The application can start before a PC opens the COM port.
 */
fsp_err_t dab_usb_console_init(void);
void      dab_usb_console_service(void);
size_t    dab_usb_console_write(void const * p_data, size_t length);
bool      dab_usb_console_read_byte(uint8_t * p_data);
bool      dab_usb_console_connected(void);
bool      dab_usb_console_take_disconnect(void);
bool      dab_usb_console_take_reconnect(void);
uint32_t  dab_usb_console_dropped_bytes(void);

/* FSP callback selected by configuration.xml. */
void dab_usb_pcdc_callback(usb_event_info_t * p_event, usb_hdl_t task, usb_onoff_t state);

#endif /* DAB_USB_CONSOLE_H */
