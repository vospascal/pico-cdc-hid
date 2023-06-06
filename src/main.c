#include <ctype.h>
#include <tusb.h>

#define PICO_STDIO_USB_ENABLE_RESET_VIA_VENDOR_INTERFACE 1

#include "stdio_tinyusb_cdc.h"
#include "hardware/watchdog.h"
#include "pico/bootrom.h"
#include "pico/stdio_uart.h"
#include "pico/stdio_usb.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <unistd.h>

#include "bsp/board.h"
#include "tusb.h"
#include <usb_descriptors.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"

// temp
uint32_t board_button_read(void)
{
    return 0;
}

// Prototypes
static void cdc_task(void);
static void uart_task(void);
static void hid_task(void);

// Main

int main()
{
    // stdio_init_all();
    stdio_uart_init();

    printf("ADC Example, measuring GPIO26\n");
    adc_init();
    adc_gpio_init(26);
    adc_gpio_init(27);
    adc_gpio_init(28);

    // init device stack on configured root-hub port
    tud_init(BOARD_TUD_RHPORT);
    // stdio_tinyusb_cdc_init();
    stdio_usb_init();

    printf("Start\n");

    while (1)
    {
        tud_task(); // tinyusb device task
        cdc_task();
        uart_task();
        hid_task();
    }
}

// echo to either Serial0 or Serial1
// with Serial0 as all lower case, Serial1 as all upper case
static void echo_serial_port(uint8_t itf, uint8_t buf[], uint32_t count)
{
    uint8_t const case_diff = 'a' - 'A';

    for (uint32_t i = 0; i < count; i++)
    {
        if (itf == 0)
        {
            // echo back 1st port as lower case
            if (isupper(buf[i]))
                buf[i] += case_diff;
        }
        else
        {
            // echo back 2nd port as upper case
            if (islower(buf[i]))
                buf[i] -= case_diff;
        }

        tud_cdc_n_write_char(itf, buf[i]);
    }
    tud_cdc_n_write_flush(itf);
}

// USB CDC

static void cdc_task(void)
{
    uint8_t itf;

    for (itf = 1; itf < CFG_TUD_CDC; itf++)
    {
        // connected() check for DTR bit
        // Most but not all terminal client set this when making connection
        // if ( tud_cdc_n_connected(itf) )
        {
            if (tud_cdc_n_available(itf))
            {
                uint8_t buf[64];

                uint32_t count = tud_cdc_n_read(itf, buf, sizeof(buf));

                // echo back to both serial ports
                echo_serial_port(0, buf, count);
                // echo_serial_port(1, buf, count);
            }
        }
    }
}

char *readInput()
{
    int i = 0;
    char ch;
    char *str = (char *)malloc(sizeof(char) * 63);

    while ((ch = getchar()) != '\r' && ch != '\n' && ch != EOF)
    {
        if (i < 63)
        {
            str[i++] = ch;
            printf("%c\n", ch); // For debugging. Returns the correct character.
        }
    }
    str[i] = '\0';
    printf("%s\n", str); // For debugging. Returns the entered string.

    return str;
}

static void uart_task(void)
{

    // char *input = readInput();

    // if (strcmp(input, "hello") == 0)
    // {
    //     printf("Input matches 'hello'\n");
    // }

    // if (strcmp(input, "b") == 0)
    // {
    //     printf("UART: Bootsel mode\n");
    //     reset_usb_boot(0, 0);
    // }
    // free(input); // Remember to free the allocated memory

    char input = getchar_timeout_us(0);
    switch (input)
    {
    case 'p':
        printf("UART: Hello, world!\n");
        break;
    case 'r':
        printf("UART: Restart\n");
        watchdog_enable(100, false);
        break;
    case 'b':
        printf("UART: Bootsel mode\n");
        reset_usb_boot(0, 0);
        break;

    case '1':
        // adc_set_round_robin
        adc_select_input(0);
        uint result1 = adc_read();
        printf("Raw bit value 1 : %d \n", result1);
        break;
    case '2':
        adc_select_input(1);
        uint result2 = adc_read();
        printf("Raw bit value 2 : %d \n", result2);
        break;
    case '3':
        adc_select_input(2);
        uint result3 = adc_read();
        printf("Raw bit value 3 : %d \n", result3);
        break;
    default:
        // Ignore
        break;
    }
}

//--------------------------------------------------------------------+
// USB HID
//--------------------------------------------------------------------+

static void send_hid_report(uint8_t report_id, uint32_t btn)
{
    // skip if hid is not ready yet
    if (!tud_hid_ready())
        return;

    switch (report_id)
    {
        // https://github.com/hathach/tinyusb/blob/c7686f8d5e98660137ef176d35bc8a82f63675f3/examples/device/hid_composite/src/main.c#L114
    case REPORT_ID_GAMEPAD:
    {
        // use to avoid send multiple consecutive zero report for keyboard
        static bool has_gamepad_key = false;

        hid_gamepad_report_t report =
            {
                .x = 0, .y = 0, .z = 0, .rz = 0, .rx = 0, .ry = 0, .hat = 0, .buttons = 0};

        if (btn)
        {
            report.hat = GAMEPAD_HAT_UP;
            report.buttons = GAMEPAD_BUTTON_A;
            tud_hid_report(REPORT_ID_GAMEPAD, &report, sizeof(report));
            // https://github.com/hathach/tinyusb/blob/c7686f8d5e98660137ef176d35bc8a82f63675f3/src/class/hid/hid_device.c#L149
            has_gamepad_key = true;
        }
        else
        {
            report.hat = GAMEPAD_HAT_CENTERED;
            report.buttons = 0;
            if (has_gamepad_key)
                tud_hid_report(REPORT_ID_GAMEPAD, &report, sizeof(report));
            // https://github.com/hathach/tinyusb/blob/c7686f8d5e98660137ef176d35bc8a82f63675f3/src/class/hid/hid_device.c#L149
            has_gamepad_key = false;
        }
    }
    break;

    default:
        break;
    }
}

// Every 10ms, we will sent 1 report for each HID profile (keyboard, mouse etc ..)
// tud_hid_report_complete_cb() is used to send the next report after previous one is complete
void hid_task(void)
{
    // Poll every 10ms
    const uint32_t interval_ms = 10;
    static uint32_t start_ms = 0;

    if (board_millis() - start_ms < interval_ms)
        return; // not enough time
    start_ms += interval_ms;

    uint32_t const btn = board_button_read();

    // Remote wakeup
    // Remote wakeup
    if (tud_suspended() && btn)
    {
        // Wake up host if we are in suspend mode
        // and REMOTE_WAKEUP feature is enabled by host
        tud_remote_wakeup();
    }
    else
    {
        // Send the 1st of report chain, the rest will be sent by tud_hid_report_complete_cb()
        send_hid_report(REPORT_ID_GAMEPAD, btn);
    }
    // https://github.com/hathach/tinyusb/blob/c7686f8d5e98660137ef176d35bc8a82f63675f3/examples/device/hid_composite/src/main.c#L219
}

// Invoked when sent REPORT successfully to host
// Application can use this to send the next report
// Note: For composite reports, report[0] is report ID
void tud_hid_report_complete_cb(uint8_t instance, uint8_t const *report, uint16_t len)
{
    (void)instance;
    (void)len;

    uint8_t next_report_id = report[0] + 1u;

    if (next_report_id < REPORT_ID_COUNT)
    {
        send_hid_report(next_report_id, board_button_read());
    }
}

// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen)
{
    // TODO not Implemented
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;

    return 0;
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize)
{
    (void)instance;

    // https://github.com/hathach/tinyusb/blob/c7686f8d5e98660137ef176d35bc8a82f63675f3/examples/device/hid_composite/src/main.c#L260
}
