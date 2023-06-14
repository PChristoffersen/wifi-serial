#pragma once

#include <cstdint>
#include <functional>
#include <unistd.h>


class TelnetConnection {
    public:
        using window_size_cb = std::function<void(uint16_t, uint16_t)>;
        using terminal_cb = std::function<void(const char *)>;

        TelnetConnection() : 
            m_fd { -1 }, 
            m_iac { false }, 
            m_state { STATE_NONE },
            m_subnegotiation_sz { 0 }, 
            m_window_size_cb { nullptr },
            m_terminal_cb { nullptr }
        {}

        ssize_t read(uint8_t *buf, size_t count);
        bool write(const uint8_t *buf, size_t count);

        void close();

        int fd() const { return m_fd; }

        void set_window_size_cb(window_size_cb cb) { m_window_size_cb = cb; }
        void set_terminal_cb(terminal_cb cb) { m_terminal_cb = cb; }

        operator bool() const { return m_fd>=0; }
        TelnetConnection &operator=(TelnetConnection &other);

    protected:
        void set(int fd);
        void reset();

    private:
        static constexpr uint8_t STATE_NONE  { 0x00 };
        static constexpr size_t SUBNEG_MAX { 128 };

        friend class TelnetServer;
        int m_fd;

        bool m_iac;
        uint8_t m_state;
        uint8_t m_subnegotiation_buf[SUBNEG_MAX];
        size_t m_subnegotiation_sz;

        window_size_cb m_window_size_cb;
        terminal_cb m_terminal_cb;
        
        void do_iac(uint8_t state);
        void on_subnegotiation(uint8_t data);
        void on_command(uint8_t command, uint8_t value);
        void process_subnegotiation(const uint8_t *data, size_t len);
        void process_do_command(uint8_t value);
        void process_will_command(uint8_t value);
        void process_window_size(const uint8_t *data, size_t len);
        void process_terminal_type(const uint8_t *data, size_t len);

        bool write_raw(const uint8_t *buf, size_t count);
        bool write_command(uint8_t command, uint8_t value);
        bool write_subnegotiation(const uint8_t *data, size_t len);
};

class TelnetServer {
    public:
        constexpr TelnetServer(uint16_t port) :
            m_port { port },
            m_server_fd { -1 }
        {}

        bool start();
        void stop();

        bool accept(TelnetConnection &connection);

        int fd() const { return m_server_fd; }

    private:
        const uint16_t m_port;

        int m_server_fd;
};

