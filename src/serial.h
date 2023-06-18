#pragma once

#include <cstdint>
#include <unistd.h>
#include <driver/uart.h>
#include <driver/gpio.h>


class Serial {
    public:
        constexpr Serial(uart_port_t port, gpio_num_t tx_pin, gpio_num_t rx_pin) :
            m_port { port },
            m_tx_pin { tx_pin },
            m_rx_pin { rx_pin },
            m_fd { -1 }
        {}

        bool start();
        void stop();

        ssize_t read(uint8_t *buf, size_t count);
        bool write(const uint8_t *buf, size_t count);

        int fd() const { return m_fd; }

        bool set_baud(uint32_t baud);

        bool restore();

    private:
        const uart_port_t m_port;
        const gpio_num_t m_tx_pin;
        const gpio_num_t m_rx_pin;

        int m_fd;

};

void serial_init();

int serial_open();
void serial_close(int fd);