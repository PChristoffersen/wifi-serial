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

static constexpr const char* TAG = "serial";

static constexpr int         SERIAL_RX_BUF_SIZE { 1024 };
static constexpr int         SERIAL_DEFAULT_BAUD_RATE { 1500000 };


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

    // TODO set baud rate from NVS

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

