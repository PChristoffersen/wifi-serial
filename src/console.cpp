#include "console.h"

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <freertos/FreeRTOS.h>
#include <esp_log.h>
#include <esp_console.h>
#include <esp_vfs_dev.h>
#include <esp_vfs_usb_serial_jtag.h>
#include <driver/usb_serial_jtag.h>
#include <soc/usb_serial_jtag_reg.h>
#include <linenoise/linenoise.h>

#include "cmd_system.h"
#include "wifi.h"

#define PROMPT_STR CONFIG_IDF_TARGET

static constexpr uint CONSOLE_MAX_COMMAND_LINE_LENGTH { 256 };
static constexpr uint CONSOLE_DELAY { 10 };
static constexpr const char* TAG = "console";

static esp_console_repl_t *repl = NULL;


void console_init()
{
    /* Register commands */
    esp_console_register_help_command();
    register_system();
    register_wifi();

    static esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    /* Prompt to be printed before each line.
     * This can be customized, made dynamic, etc.
     */
    repl_config.prompt = PROMPT_STR ">";
    repl_config.max_cmdline_length = CONSOLE_MAX_COMMAND_LINE_LENGTH;

    #if defined(CONFIG_ESP_CONSOLE_UART_DEFAULT) || defined(CONFIG_ESP_CONSOLE_UART_CUSTOM)
        esp_console_dev_uart_config_t hw_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_console_new_repl_uart(&hw_config, &repl_config, &repl));
        ESP_LOGI(TAG, "UART %p", repl);

    #elif defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
        esp_console_dev_usb_serial_jtag_config_t hw_config = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&hw_config, &repl_config, &repl));
        ESP_LOGI(TAG, "JTAG %p", repl);

    #else
        #error Unsupported console type
    #endif


    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
