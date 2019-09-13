//
// Created by explorer on 9/4/19.
//

#include <arpa/inet.h>
#include <cstring>
#include "scs.h"
#include "protocol.h"
#include "../unit/ip_lib.h"
#include "../chain_build.h"

// TODO define a struct

SCSServer::SCSServer(PIPE *prev, PIPE *next, Future *protocol_ready)
        : protocol(prev, next, protocol_ready) {
    std::function<void()> f = [this]() { this->server_proto(); };
    new_coro(f);
}

void SCSServer::resp_err(unsigned char err_code, in_addr_t ip, in_port_t port) {
    unsigned char buf[10];
    struct sockaddr_in addr;
    unsigned int addr_len;
    int fd = prev->fileno;

    addr_len = sizeof(addr);
    getpeername(fd, (struct sockaddr *) &addr, (socklen_t *) &addr_len);
    logger(ERR, stderr, "SCS conn error from %s:%d", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));


    buf[0] = 2;
    buf[1] = err_code;
    buf[2] = 1;
    memcpy(buf + 3, &ip, 4);
    memcpy(buf + 7, &port, 2);
    prev->write(buf, 9);
}

void SCSServer::server_proto() {
    unsigned char in_buffer[0x100], out_buf[0x100];

    io_check(prev->read(in_buffer, 3));

    if (in_buffer[0] != 1) {
        logger(ERR, stderr, "unknown proto type");
        close_ret;
    }

    if (in_buffer[1] != 1) {
        logger(ERR, stderr, "unknown cmd type");
        resp_err(7, 0, 0);
        close_ret;
    }

    if (in_buffer[2] != 1 && in_buffer[2] != 3) {
        logger(ERR, stderr, "unknown addr type");
        resp_err(8, 0, 0);
        close_ret;
    }

    int addr_type = in_buffer[2];
    in_addr_t ip;
    in_port_t port;
    unsigned char *url = nullptr;
    if (addr_type == 1) {
        io_check(prev->read(reinterpret_cast<unsigned char *>(&ip), 4));
        io_check(prev->read(reinterpret_cast<unsigned char *>(&port), 2));
    } else {
        unsigned char url_size;
        io_check(prev->read(&url_size, 1));
        url = static_cast<unsigned char *>(malloc(url_size + 1));

        io_check(prev->read(url, url_size));
        io_check(prev->read(reinterpret_cast<unsigned char *>(&port), 2));
        url[url_size] = 0;
    }

    if (addr_type == 1) {
        struct in_addr tmp;
        tmp.s_addr = ip;
        logger(INFO, stderr, "get a scs req to %s:%d", inet_ntoa(tmp), ntohs(port));
    } else {
        logger(INFO, stderr, "get a scs req to %s:%d", url, ntohs(port));
    }

    // connect to target

    int re;
    if (addr_type == 1) {
        struct in_addr tmp;
        tmp.s_addr = ip;
        re = client_chain_builder(this, inet_ntoa(tmp), ntohs(port));
    } else {
        re = client_chain_builder(this, reinterpret_cast<char *>(url), ntohs(port));
    }
    if (re == -1) {
        // is's hard to say what err happen
        resp_err(1, 0, 0);
        close_ret;
    }
    free(url);
    struct sockaddr_in addr;
    unsigned int addr_len = sizeof(addr);

    auto result = getsockname(next->fileno, (struct sockaddr *) &addr, &addr_len);
    if (result == -1) {
        logger(ERR, stderr, "get peer name error");
        close_ret;
    }
    in_addr_t s_ip = addr.sin_addr.s_addr;
    in_port_t s_port = addr.sin_port;

    out_buf[0] = 2;
    out_buf[1] = 0;
    out_buf[2] = 1;
    memcpy(out_buf + 3, &s_ip, 4);
    memcpy(out_buf + 7, &s_port, 2);
    io_check(prev->write(out_buf, 9));

    std::function<void()> f = [this]() { this->transport(); };
    new_coro(f);
    protocol_ready->set();
    protocol_ready = nullptr;

    while (true) {
        unsigned char buffer[0x1000];
        auto re = prev->read(buffer, 0x1000, READ_ANY);
        if (re == -1) {
            close_ret;
        }
        re = next->write(buffer, re);
        if (re == -1) {
            close_ret;
        }
    }
}

void SCSServer::transport() {
    while (true) {
        unsigned char buffer[0x1000];
        auto re = next->read(buffer, 0x1000, READ_ANY);
        if (re == -1) {
            close_ret;
        }
        re = prev->write(buffer, re);
        if (re == -1) {
            close_ret;
        }
    }
}

SCSClient::SCSClient(PIPE *prev, PIPE *next, Future *protocol_ready, char *url, unsigned short port)
        : client_protocol(prev, next, protocol_ready, url, port) {
    std::function<void()> f = [this]() { this->client_proto(); };
    new_coro(f);
}

void SCSClient::client_proto() {
    int req_len;
    unsigned char *req;
    if (is_ip(url)) {
        req_len = 9;
        req = static_cast<unsigned char *>(malloc(req_len));
        req[0] = 1;
        req[1] = 1;
        req[2] = 1;
        in_addr_t ip = inet_addr(url);
        memcpy(req + 3, &ip, 4);
        in_port_t nport = htons(port);
        memcpy(req + 7, &nport, 2);
    } else {
        unsigned char url_len = strlen(url);
        req_len = 5 + 1 + url_len;
        req = static_cast<unsigned char *>(malloc(req_len));
        req[0] = 1;
        req[1] = 1;
        req[2] = 3;
        req[3] = url_len;
        memcpy(req + 4, url, url_len);
        in_port_t nport = htons(port);
        memcpy(req + 4 + url_len, &nport, 2);
    }

    io_check(prev->write(req, req_len));
    free(req);

    unsigned char resp[9];
    io_check(prev->read(resp, 9));

    if (resp[0] != 2) {
        logger(ERR, stderr, "unknown proto type");
        close_ret;
    } else if (resp[1] != 0) {
        logger(ERR, stderr, "conn error %s", errmsg(resp[1]));
        close_ret;
    }

    std::function<void()> f = [this]() { this->transport(); };
    new_coro(f);
    protocol_ready->set();
    protocol_ready = nullptr;

    while (true) {
        unsigned char buffer[0x1000];
        auto re = prev->read(buffer, 0x1000, READ_ANY);
        if (re == -1) {
            close_ret;
        }
        re = next->write(buffer, re);
        if (re == -1) {
            close_ret;
        }
    }
}

char *SCSClient::errmsg(int err) {
    switch (err) {
        case 1:
            return "network unreachable";
        case 2:
            return "cmd unknown";
        case 3:
            return "ip addr error";
        case 4:
            return "unknown error";
        default:
            return "unknown error code";
    }
}

void SCSClient::transport() {
    while (true) {
        unsigned char buffer[0x1000];
        auto re = next->read(buffer, 0x1000, READ_ANY);
        if (re == -1) {
            close_ret;
        }
        re = prev->write(buffer, re);
        if (re == -1) {
            close_ret;
        }
    }
}
