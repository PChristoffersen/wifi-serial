#include "serial.h"

#include <string.h>
#include <stdio.h>
#include <sys/fcntl.h>
#include <sys/errno.h>
#include <sys/unistd.h>
#include <freertos/FreeRTOS.h>
#include <esp_log.h>
#include <esp_vfs.h>
#include <esp_vfs_dev.h>
#include <driver/uart.h>
#include <driver/gpio.h>
#include <nvs.h>


static constexpr const char* TAG = "serial";

static constexpr int         SERIAL_RX_BUF_SIZE { 1024 };
static constexpr int         SERIAL_DEFAULT_BAUD_RATE { 1500000 };

static constexpr const char *SERIAL_NVS_NAMESPACE { "storage" };
static constexpr const char *SERIAL_NVS_BAUD      { "serial_baud" };

bool Serial::start()
{
    if (!uart_is_driver_installed(m_port)) {
        ESP_LOGI(TAG, "Installing UART Driver");
        ESP_ERROR_CHECK( uart_driver_install(m_port, SERIAL_RX_BUF_SIZE*2, 0, 0, nullptr, 0) );
    }

    uart_config_t uart_config = {
        .baud_rate = SERIAL_DEFAULT_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
    };

    nvs_handle_t handle;
    auto err = nvs_open(SERIAL_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
    } else {
        printf("NVS Open\n");

        uint32_t value = SERIAL_DEFAULT_BAUD_RATE;
        if (nvs_get_u32(handle, SERIAL_NVS_BAUD, &value)==ESP_OK) {
            uart_config.baud_rate = value;
        }
        else {
            ESP_LOGI(TAG, "Using default baud %u", SERIAL_DEFAULT_BAUD_RATE);
        }

        nvs_close(handle);
    }



    ESP_ERROR_CHECK(uart_param_config(m_port, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(m_port, m_tx_pin, m_rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));


    char dev[32];
    sprintf(dev, "/dev/uart/%d", m_port);

    m_fd = open(dev,  O_RDWR|O_NONBLOCK);
    if (m_fd<0) {
        ESP_LOGE(TAG, "Cannot open serial '%s': errno %d", dev, errno);
        return false;
    }
    else {
        ESP_LOGI(TAG, "Opened serial device %s", dev);
    }


    esp_vfs_dev_uart_use_driver(m_port);
    esp_vfs_dev_uart_port_set_rx_line_endings(m_port, ESP_LINE_ENDINGS_LF);
    esp_vfs_dev_uart_port_set_tx_line_endings(m_port, ESP_LINE_ENDINGS_LF);

    return true;
}

void Serial::stop()
{
    if (m_fd>=0) {
        close(m_fd);
        m_fd = -1;
    }
}



ssize_t Serial::read(uint8_t *buf, size_t count)
{
    return ::read(m_fd, buf, count);
}

bool Serial::write(const uint8_t *buf, size_t count)
{
    while (count) {
        auto res = ::write(m_fd, buf, count);
        if (res<=0) {
            ESP_LOGW(TAG, "Error writing %u bytes to serial: errno=%d", count, errno);
            return false;
        }
        buf+=res;
        count-=res;
    }
    return true;
}


bool Serial::set_baud(uint32_t baud)
{
    auto res = uart_set_baudrate(m_port, baud);
    if (res==ESP_OK) {
        nvs_handle_t handle;
        res = nvs_open(SERIAL_NVS_NAMESPACE, NVS_READWRITE, &handle);
        if (res != ESP_OK) {
            ESP_LOGE(TAG, "Error opening NVS store: err=%d", res);
            return false;
        }

        res = nvs_set_u32(handle, SERIAL_NVS_BAUD, baud);
        if (res!=ESP_OK) {
            ESP_LOGE(TAG, "Error storing NVS baud: err=%d", res);
            nvs_close(handle);
            return false;
        }

        ESP_LOGI(TAG, "Baud rate set to %lu", baud);
        nvs_close(handle);

        return true;
    }
    else {
        ESP_LOGE(TAG, "Error setting baud rate to %lu: err=%d", baud, res);
        return false;
    }
}
