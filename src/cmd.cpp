#include "cmd.h"

#include <freertos/FreeRTOS.h>
#include <esp_log.h>
#include <esp_console.h>
#include <esp_wifi.h>
#include <argtable3/argtable3.h>

#include "wifi.h"
#include "globals.h"

static constexpr const char *TAG = "cmd";


/** -------------------------------------------------------------------------------
 * Serial settings
 */

static struct {
    struct arg_int *baud;
    struct arg_end *end;
} set_baud_args;

static int serial_set_baud_cmd(int argc, char **argv) {
    int nerrors = arg_parse(argc, argv, (void **) &set_baud_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, set_baud_args.end, argv[0]);
        return 1;
    }

    int baud = set_baud_args.baud->ival[0];
    if (baud <=0) {
        ESP_LOGE(TAG, "Invalid baud rate %d", baud);
        return 1;
    }

    ESP_LOGI(TAG, "Set serial baud to %d", baud);

    if (!g_serial.set_baud(baud)) {
        ESP_LOGI(TAG, "Set serial baud failed");
        return 1;
    }

    return 0;
}

static void register_serial_set_baud()
{
    set_baud_args.baud = arg_int1(nullptr, nullptr, "<baud>", "Serial baud rate");
    set_baud_args.end = arg_end(2);

    const esp_console_cmd_t cmd = {
        .command = "serial_set_baud",
        .help = "Set serial baud rate",
        .hint = nullptr,
        .func = serial_set_baud_cmd,
        .argtable = &set_baud_args
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}



static int serial_restore_cmd(int argc, char **argv) {
    if (!g_serial.restore()) {
        ESP_LOGW(TAG, "Serial restore command failed");
        return 1;
    }
    ESP_LOGI(TAG, "Serial settings reset to default");
    return 0;
}

static void register_serial_restore()
{
    const esp_console_cmd_t cmd = {
        .command = "serial_restore",
        .help = "Restore serial settings to default",
        .hint = nullptr,
        .func = &serial_restore_cmd,
        .argtable = nullptr,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}



/** -------------------------------------------------------------------------------
 * Wifi commands
 */

static constexpr uint JOIN_TIMEOUT_MS { 10000 };


static struct {
    struct arg_int *timeout;
    struct arg_str *ssid;
    struct arg_str *password;
    struct arg_end *end;
} join_args;

static int wifi_join_cmd(int argc, char **argv) {
    int nerrors = arg_parse(argc, argv, (void **) &join_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, join_args.end, argv[0]);
        return 1;
    }
    ESP_LOGI(TAG, "Connecting to '%s'", join_args.ssid->sval[0]);

    /* set default value*/
    if (join_args.timeout->count == 0) {
        join_args.timeout->ival[0] = JOIN_TIMEOUT_MS;
    }

    bool connected = wifi_join(join_args.ssid->sval[0],
                            join_args.password->sval[0],
                            join_args.timeout->ival[0]);
    if (!connected) {
        ESP_LOGW(TAG, "Connection timed out");
        return 1;
    }
    ESP_LOGI(TAG, "Connected");
    return 0;
}

static void register_wifi_join()
{
    join_args.timeout = arg_int0(nullptr, "timeout", "<t>", "Connection timeout, ms");
    join_args.ssid = arg_str1(nullptr, nullptr, "<ssid>", "SSID of AP");
    join_args.password = arg_str0(nullptr, nullptr, "<pass>", "PSK of AP");
    join_args.end = arg_end(2);

    const esp_console_cmd_t cmd = {
        .command = "wifi_join",
        .help = "Join WiFi AP as a station",
        .hint = nullptr,
        .func = wifi_join_cmd,
        .argtable = &join_args
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}


static struct {
    struct arg_str *code;
    struct arg_end *end;
} cc_args;

static int wifi_set_country_cmd(int argc, char **argv) {
    int nerrors = arg_parse(argc, argv, (void **) &cc_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, cc_args.end, argv[0]);
        return 1;
    }


    ESP_LOGI(TAG, "Set country code to '%s'", cc_args.code->sval[0]);

    auto res = esp_wifi_set_country_code(cc_args.code->sval[0], true);
    if (res!=ESP_OK) {
        ESP_LOGW(TAG, "Set country code failed with code %d", res);
        return 1;
    }


    return 0;
}

static void register_wifi_set_country()
{
    cc_args.code = arg_str1(nullptr, nullptr, "<code>", "Wifi Country code");
    cc_args.end = arg_end(2);

    const esp_console_cmd_t cmd = {
        .command = "wifi_set_country",
        .help = "Set wifi country code",
        .hint = nullptr,
        .func = wifi_set_country_cmd,
        .argtable = &cc_args
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}



static int wifi_restore_cmd(int argc, char **argv) {
    if (!wifi_restore()) {
        ESP_LOGW(TAG, "WiFi restore command failed");
        return 1;
    }
    ESP_LOGI(TAG, "WiFi settings reset to default");
    return 0;
}

static void register_wifi_restore()
{
    const esp_console_cmd_t cmd = {
        .command = "wifi_restore",
        .help = "Restore wifi settings to default",
        .hint = nullptr,
        .func = &wifi_restore_cmd,
        .argtable = nullptr,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}



static int wifi_info_cmd(int argv, char **argc) {
    wifi_ap_record_t record;
    ESP_ERROR_CHECK( esp_wifi_sta_get_ap_info(&record) );

    printf("WiFi info:\n");
    printf("  SSID: %s\n", record.ssid);
    printf("  rssi: %d\n", (int)record.rssi);
    printf("  Country: %s\n", record.country.cc);

    return 0;
}

static void register_wifi_info()
{
    const esp_console_cmd_t cmd = {
        .command = "wifi_info",
        .help = "Get WiFi info",
        .hint = nullptr,
        .func = &wifi_info_cmd,
        .argtable = nullptr,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}



/** -------------------------------------------------------------------------------
 * Wifi commands
 */

void register_cmd()
{
    register_wifi_join();
    register_wifi_set_country();
    register_wifi_restore();
    register_wifi_info();
    register_serial_set_baud();
    register_serial_restore();
}
