#include "telnet.h"

#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/stream_buffer.h>
#include <esp_log.h>
#include <esp_vfs_eventfd.h>

#include <lwip/err.h>
#include <lwip/sockets.h>
#include <lwip/sys.h>
#include <lwip/netdb.h>

#include "serial.h"


static constexpr const char* TAG = "telnet";

static constexpr int KEEPALIVE_IDLE     { 5 };
static constexpr int KEEPALIVE_INTERVAL { 5 };
static constexpr int KEEPALIVE_COUNT    { 3 };



// RFC 854 : https://tools.ietf.org/html/rfc854
static constexpr uint8_t TELNET_SE = 0xf0;                  // End of subnegotiation parameters.
static constexpr uint8_t TELNET_NOP = 0xf1;                 // No operation.
static constexpr uint8_t TELNET_DATA_MARK = 0xf2;           // The data stream portion of a Synchronization This should always be accompanied by a TCP Urgent notification.
static constexpr uint8_t TELNET_BREAK = 0xf3;               // NVT character BRK.
static constexpr uint8_t TELNET_INTERRUPT_PROCESS = 0xf4;   // The function IP.
static constexpr uint8_t TELNET_ABORT_OUTPUT = 0xf5;        // The function AO.
static constexpr uint8_t TELNET_ARE_YOU_THERE = 0xf6;       // The function AYT.
static constexpr uint8_t TELNET_ERASE_CHARACTER = 0xf7;     // The function EC.
static constexpr uint8_t TELNET_ERASE_LINE = 0xf8;          // The function EL.
static constexpr uint8_t TELNET_GO_AHEAD = 0xf9;            // The GA signal.
static constexpr uint8_t TELNET_SB = 0xfa;                  // Indicates that what follows is subnegotiation of the indicated option.
static constexpr uint8_t TELNET_WILL = 0xfb;                // Indicates the desire to begin performing, or confirmation that you are now performing, the indicated option.
static constexpr uint8_t TELNET_WONT = 0xfc;                // Indicates the refusal to perform, or continue performing, the indicated option.
static constexpr uint8_t TELNET_DO = 0xfd;                  // Indicates the request that the other party perform, or confirmation that you are expecting the other party to perform, the indicated option.
static constexpr uint8_t TELNET_DONT = 0xfe;                // Indicates the demand that the other party stop performing, or confirmation that you are no longer expecting the other party to perform, the indicated option.
static constexpr uint8_t TELNET_IAC = 0xff;                 // Data Byte 255.

/** https://tools.ietf.org/html/rfc857 */
static constexpr uint8_t TELNET_OPT_ECHO                = 0x01;
/** https://tools.ietf.org/html/rfc858 */
static constexpr uint8_t TELNET_OPT_SUPPRESS_GO_AHEAD   = 0x03;
/** https://tools.ietf.org/html/rfc859 */
static constexpr uint8_t TELNET_OPT_STATUS              = 0x05;
/** https://tools.ietf.org/html/rfc1091 */
static constexpr uint8_t TELNET_OPT_TERMINAL_TYPE       = 0x18;
/** https://tools.ietf.org/html/rfc1073 */
static constexpr uint8_t TELNET_OPT_WINDOW_SIZE         = 0x1f;
/** https://tools.ietf.org/html/rfc1079 */
static constexpr uint8_t TELNET_OPT_TERMINAL_SPEED      = 0x20;
/** https://tools.ietf.org/html/rfc1372 */
static constexpr uint8_t TELNET_OPT_REMOTE_FLOW_CONTROL = 0x21;
/** https://tools.ietf.org/html/rfc1184 */
static constexpr uint8_t TELNET_OPT_TERMINAL_LINEMODE   = 0x22;
/** https://tools.ietf.org/html/rfc1096 */
static constexpr uint8_t TELNET_OPT_X_DISPLAY_LOCATION  = 0x23;
/** https://tools.ietf.org/html/rfc927 */
static constexpr uint8_t TELNET_OPT_TUID                = 0x26;
/** https://tools.ietf.org/html/rfc1572 */
static constexpr uint8_t TELNET_OPT_ENVIRONMENT         = 0x27;



bool TelnetServer::start() 
{
    int addr_family = AF_INET;
    int ip_protocol = 0;
    struct sockaddr_storage dest_addr;

    if (addr_family == AF_INET) {
        struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
        dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr_ip4->sin_family = AF_INET;
        dest_addr_ip4->sin_port = htons(m_port);
        ip_protocol = IPPROTO_IP;
    }

    m_server_fd = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (m_server_fd < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        return -1;
    }
    int opt = 1;
    setsockopt(m_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    ESP_LOGI(TAG, "Socket created");

    int err = bind(m_server_fd, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        ESP_LOGE(TAG, "IPPROTO: %d", addr_family);
        close(m_server_fd);
        m_server_fd = -1;
        return false;
    }
    ESP_LOGI(TAG, "Socket bound, port %d", m_port);

    err = listen(m_server_fd, 1);
    if (err != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        close(m_server_fd);
        m_server_fd = -1;
        return false;
    }

    ESP_LOGI(TAG, "Socket listening");

    return true;
}

void TelnetServer::stop()
{
    if (m_server_fd>=0) {
        close(m_server_fd);
        m_server_fd = -1;
    }
}



bool TelnetServer::accept(TelnetConnection &connection)
{
    char addr_str[128];
    int keepAlive = 1;
    int keepIdle = KEEPALIVE_IDLE;
    int keepInterval = KEEPALIVE_INTERVAL;
    int keepCount = KEEPALIVE_COUNT;

    struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
    socklen_t addr_len = sizeof(source_addr);
    int sock = ::accept(m_server_fd, (struct sockaddr *)&source_addr, &addr_len);
    if (sock < 0) {
        ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
        return false;
    }

    // Set tcp keepalive option
    setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int));
    // Convert ip address to string
    if (source_addr.ss_family == PF_INET) {
        inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
    }

    ESP_LOGI(TAG, "Client connected ip address: %s", addr_str);

    // Negotiation
    connection.set(sock);

    if (!connection.write_command(TELNET_WILL, TELNET_OPT_ECHO)) { connection.close(); return false; }

    return true;    
}



TelnetConnection &TelnetConnection::operator=(TelnetConnection &other)
{
    close();
    m_fd = other.m_fd;
    m_iac = other.m_iac;
    m_state = other.m_state;
    if (other.m_subnegotiation_sz) {
        memcpy(m_subnegotiation_buf, other.m_subnegotiation_buf, other.m_subnegotiation_sz);
    }
    m_subnegotiation_sz = other.m_subnegotiation_sz;
    other.reset();
    return *this;
}


void TelnetConnection::set(int fd) 
{
    close();
    m_fd = fd;
}

void TelnetConnection::reset()
{
    m_fd = -1;
    m_iac = false;
    m_state = STATE_NONE;
    m_subnegotiation_sz = 0;
    m_window_size_cb = nullptr;
    m_terminal_cb = nullptr;
}




void TelnetConnection::close()
{
    if (m_fd>=0) {
        shutdown(m_fd, SHUT_RDWR);
        ::close(m_fd);
    }
    reset();
}


bool TelnetConnection::write_raw(const uint8_t *buf, size_t count)
{
    while (count > 0) {
        auto res = send(m_fd, buf, count, 0);
        if (res <= 0) {
            ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
            return false;
        }
        buf+=res;
        count-=res;
    }

    return true;
}



bool TelnetConnection::write(const uint8_t *buf, size_t count)
{
    uint8_t buffer[count*2];
    uint8_t *obuf = buffer;
    size_t olen = 0;

    while (count) {
        *(obuf++) = *buf;
        if (*buf==TELNET_IAC) {
            ESP_LOGI(TAG, "Padding!!");
            *(obuf++) = *buf;
            olen++;
        }
        buf++;
        olen++;
        count--;
    }
    #if 0
    printf("> ");
    for (int i=0; i<olen; i++) {
        printf("%02x ", buffer[i]);
    }
    printf("\n");
    #endif
    return write_raw(buffer, olen);
}


bool TelnetConnection::write_command(uint8_t command, uint8_t value)
{
    uint8_t CMD[] { TELNET_IAC, command, value };
    return write_raw(CMD, sizeof(CMD));
}


bool TelnetConnection::write_subnegotiation(const uint8_t *data, size_t len)
{
    uint8_t buffer[len*2+4];
    uint8_t *obuf = buffer;
    size_t olen = 0;
    *(obuf++) = TELNET_IAC;
    *(obuf++) = TELNET_SB;
    olen+=2;
    while (len) {
        if ((*data)==TELNET_IAC) {
            *(obuf++) = *data;
            olen+=2;
        }
        else {
            *(obuf++) = *data;
            olen++;
        }
        data++;
        len--;
    }
    *(obuf++) = TELNET_IAC;
    *(obuf++) = TELNET_SE;
    olen+=2;

    #if 0
    printf("SB> ");
    for (int i=0; i<olen; i++) {
        printf("%02x ", buffer[i]);
    }
    printf("\n");
    #endif

    return write_raw(buffer, olen);
}


void TelnetConnection::process_window_size(const uint8_t *data, size_t len)
{
    if (len!=5) {
        ESP_LOGW(TAG, "Invalid window size subneg");
        return;
    }
    uint16_t width = (static_cast<uint16_t>(data[1]) << 8) | data[2];
    uint16_t height = (static_cast<uint16_t>(data[3]) << 8) | data[4];

    ESP_LOGI(TAG, "Client window size: %u x %u", width, height);
    if (m_window_size_cb) {
        m_window_size_cb(width, height);
    }
}


void TelnetConnection::process_terminal_type(const uint8_t *data, size_t len)
{
    if (len<3 || data[1]!=0x00) {
        ESP_LOGW(TAG, "Invalid terminal type");
        return;
    }
    char term[len];
    memcpy(term, data+2, len-2);
    term[len] = '\0';
    ESP_LOGI(TAG, "Client terminal: %s", term);
    if (m_terminal_cb) {
        m_terminal_cb(term);
    }
}


void TelnetConnection::process_subnegotiation(const uint8_t *data, size_t len)
{
    if (len<1) 
        return;

    switch (*data) {
        case TELNET_OPT_WINDOW_SIZE:
            process_window_size(data, len);
            break;
        case TELNET_OPT_TERMINAL_TYPE:
            process_terminal_type(data, len);
            break;
        default: 
            ESP_LOGW(TAG, "Unsupported subnegotiation %02x", *data);
            for (int i=0; i<len; i++) {
                printf("%02x ", data[i]);
            }
            printf("\n");
    }
}




void TelnetConnection::process_do_command(uint8_t value)
{
    switch (value) {
        case TELNET_OPT_ECHO: 
            ESP_LOGI(TAG, "Server DO ECHO");
            write_command(TELNET_WILL, TELNET_OPT_ECHO);
            break;
        case TELNET_OPT_SUPPRESS_GO_AHEAD:
            ESP_LOGI(TAG, "Server DO suppress GA");
            write_command(TELNET_WILL, TELNET_OPT_SUPPRESS_GO_AHEAD);
            break;
        case TELNET_OPT_STATUS:
            ESP_LOGI(TAG, "Server WON'T DO Status");
            write_command(TELNET_WONT, TELNET_OPT_STATUS);
            break;
        case TELNET_OPT_TUID:
            ESP_LOGI(TAG, "Server WON'T DO TUID");
            write_command(TELNET_WONT, TELNET_OPT_TUID);
            break;
        default: 
            ESP_LOGI(TAG, "Server WON'T DO unknown %02x", value);
            write_command(TELNET_WONT, value);
            break;
    }
}


void TelnetConnection::process_will_command(uint8_t value)
{
    switch (value) {
        case TELNET_OPT_SUPPRESS_GO_AHEAD:
            ESP_LOGI(TAG, "Client WILL suppress GA");
            write_command(TELNET_DO, TELNET_OPT_SUPPRESS_GO_AHEAD);
            break;
        case TELNET_OPT_TERMINAL_TYPE: 
            ESP_LOGI(TAG, "Client WILL Terminal type");
            if (m_terminal_cb) {
                uint8_t cmd[] { TELNET_OPT_TERMINAL_TYPE, 1 };
                write_subnegotiation(cmd, sizeof(cmd));
            }
            break;
        case TELNET_OPT_TERMINAL_SPEED:
            ESP_LOGI(TAG, "Client WILL Terminal speed");
            break;
        case TELNET_OPT_REMOTE_FLOW_CONTROL:
            ESP_LOGI(TAG, "Client WILL Remote flow control");
            break;
        case TELNET_OPT_TERMINAL_LINEMODE:
            ESP_LOGI(TAG, "Client WILL Terminal line mode");
            break;
        case TELNET_OPT_X_DISPLAY_LOCATION:
            ESP_LOGI(TAG, "Client WILL X Display location");
            break;
        case TELNET_OPT_ENVIRONMENT:
            ESP_LOGI(TAG, "Client WILL Environment");
            break;
        case TELNET_OPT_WINDOW_SIZE:
            ESP_LOGI(TAG, "Client WILL Window size");
            if (m_window_size_cb) {
                write_command(TELNET_DO, TELNET_OPT_WINDOW_SIZE);
            }
            break;
        
        default:
            ESP_LOGI(TAG, "Client WILL unknown %02x", value);
            break;
    }
}



void TelnetConnection::on_command(uint8_t command, uint8_t value)
{
    switch (command) {
        case TELNET_WILL:
            process_will_command(value);
            break;
        case TELNET_WONT:
            ESP_LOGI(TAG, "Command: WONT  %02x", value);
            break;
        case TELNET_DO:
            process_do_command(value);
            break;
        case TELNET_DONT: 
            ESP_LOGI(TAG, "Command: DON'T  %02x", value);
            break;
        default:
            ESP_LOGI(TAG, "Command: %02x  %02x", command, value);
            break;
    }
}


void TelnetConnection::do_iac(uint8_t state)
{
    switch (state) {
        // Option codes
        case TELNET_WILL:
        case TELNET_WONT:
        case TELNET_DO:
        case TELNET_DONT: {
            m_state = state;
            break;
        }
        case TELNET_SB: {
            m_state = state;
            m_subnegotiation_sz = 0;
            break;
        }
        case TELNET_SE: {
            process_subnegotiation(m_subnegotiation_buf, m_subnegotiation_sz);
            m_subnegotiation_sz = 0;
            m_state = STATE_NONE;
            break;
        }
        default: {
            ESP_LOGW(TAG, "Unexpected command: %02x", state);
            break;
        }
    }
}




ssize_t TelnetConnection::read(uint8_t *buf, size_t count)
{
    ssize_t rsz = 0;
    uint8_t rbuf[count];
    auto rc = recv(m_fd, rbuf, count, 0);
    if (rc<0) {
        ESP_LOGW(TAG, "Client read failed: errno=%d", errno);
        return -1;
    }
    uint8_t *inb = buf;
    for (ssize_t i=0; i<rc; i++) {
        uint8_t ch = rbuf[i];
        if (m_iac) {
            m_iac = false;
            if (ch!=TELNET_IAC) {
                do_iac(ch);
                continue;
            }
            ESP_LOGW(TAG, "Double 0xFF");
        }
        switch (m_state) {
            case TELNET_WILL:
            case TELNET_WONT:
            case TELNET_DO:
            case TELNET_DONT: 
                on_command(m_state, ch);
                m_state = STATE_NONE;
                continue;
            default:
                break;
        }
        if (ch==TELNET_IAC) {
            m_iac = true;
            continue;
        }
        if (m_state==TELNET_SB) {
            if (m_subnegotiation_sz<SUBNEG_MAX) {
                m_subnegotiation_buf[m_subnegotiation_sz++] = ch;
            }
            else {
                ESP_LOGW(TAG, "Subnegotiation buffer overflow");
                m_subnegotiation_sz = 0;
                m_state = STATE_NONE;
            }
        }
        else {
            if (ch!=0x00) {
                *(buf++) = ch;
                rsz++;
            }
        }
    }

    if (rsz>0) {
        printf("< ");
        for (uint i=0; i<rsz; i++) {
            printf("%02x ", inb[i]);
        }
        printf("\n");
    }

    return rsz;
}


