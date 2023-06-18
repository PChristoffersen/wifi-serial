#include <string.h>
#include <stdio.h>
#include <sys/fcntl.h>
#include <sys/errno.h>
#include <sys/unistd.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <esp_vfs_dev.h>

#include "serial.h"
#include "wifi.h"
#include "telnet.h"
#include "console.h"


extern "C" {
    void app_main(void);
}


static constexpr const char *TAG = "main";


static Serial serial(UART_NUM_1, GPIO_NUM_2, GPIO_NUM_3);
static TelnetServer telnet_server(23);

static TelnetConnection telnet_client;


static void on_serial_data()
{
    static uint8_t buf[64];

    auto len = serial.read(buf, sizeof(buf));
    if (len>0) {
        //ESP_LOGI(TAG, "SER %d read", len);
        if (telnet_client) {
            telnet_client.write(buf, len);
        }
    }
}

static void on_telnet_client_data()
{
    static uint8_t buf[64];
    auto len = telnet_client.read(buf, sizeof(buf));
    if (len<0) {
        ESP_LOGW(TAG, "Closing telnet client");
        telnet_client.close();
        return;
    }
    else if (len>0) {
        ESP_LOGI(TAG, "TEL %d read", len);
        serial.write(buf, len);
    }
}


static void on_telnet_window_size(uint16_t width, uint16_t height)
{

}


static void on_telnet_connection()
{
    TelnetConnection client;

    if (!telnet_server.accept(client))
        return;

    if (telnet_client) {
        ESP_LOGW(TAG, "Telnet busy");
        // We already hav a connection - reject connection
        const char msg[] = "Busy\n";
        client.write((const uint8_t*)msg, strlen(msg));
        client.close();
        return;
    }

    ESP_LOGW(TAG, "Client ok");
    telnet_client = client;
    telnet_client.set_window_size_cb(on_telnet_window_size);
}





static void board_init() 
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    esp_vfs_dev_uart_register();

}



void app_main(void)
{
    board_init();
    wifi_init();


    // Start delay...
    constexpr uint START_DELAY { 10 };
    for (uint i=0; i<START_DELAY; i++) {
        ESP_LOGI(TAG, "Starting in %u sec ...", START_DELAY-i);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGI(TAG, "Iterating through app partitions...");
    auto first = esp_partition_find_first(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, nullptr);
    auto it = esp_partition_find(first->type, first->subtype, first->label);

    // Loop through all matching partitions, in this case, all with the type 'data' until partition with desired
    // label is found. Verify if its the same instance as the one found before.
    for (; it != NULL; it = esp_partition_next(it)) {
        const esp_partition_t *part = esp_partition_get(it);
        ESP_LOGI(TAG, "\tfound partition '%s' at offset 0x%lx with size 0x%lx", part->label, part->address, part->size);
    }
    // Release the partition iterator to release memory allocated for it
    esp_partition_iterator_release(it);


    console_init();

    while (!serial.start()) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        ESP_LOGW(TAG, "Retrying serial open");
    }
    while (!telnet_server.start()) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        ESP_LOGW(TAG, "Retrying telnet open");
    }
    int max_fd = MAX(serial.fd(), telnet_server.fd());

    int s;
    fd_set rfds;
    struct timeval tv = {
        .tv_sec = 0,
        .tv_usec = 10000,
    };
    while (true) {
        FD_ZERO(&rfds);
        FD_SET(serial.fd(), &rfds);
        FD_SET(telnet_server.fd(), &rfds);
        if (telnet_client) {
            FD_SET(telnet_client.fd(), &rfds);
        }

        s = select(MAX(max_fd, telnet_client.fd())+1, &rfds, nullptr, nullptr, &tv);

        if (s < 0) {
            ESP_LOGE(TAG, "Select failed: errno %d", errno);
        } 
        else if (s == 0) {
            //ESP_LOGI(TAG, "Timeout has been reached and nothing has been received");
        }
        else {
            if (FD_ISSET(serial.fd(), &rfds)) {
                on_serial_data();
            }
            if (FD_ISSET(telnet_server.fd(), &rfds)) {
                on_telnet_connection();
            }
            if (FD_ISSET(telnet_client.fd(), &rfds)) {
                on_telnet_client_data();
            }
        }
        taskYIELD();
    }

    telnet_server.stop();
    serial.stop();
}

