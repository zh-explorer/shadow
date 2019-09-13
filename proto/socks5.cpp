//
// Created by explorer on 9/4/19.
//

#include <arpa/inet.h>
#include "socks5.h"
#include "../chain_build.h"
#include <cstring>

// TODO define a struct

socks5Server::socks5Server(PIPE *prev, PIPE *next, Future *protocol_ready)
        : protocol(prev, next, protocol_ready) {
    std::function<void()> f = [this]() { this->server_proto(); };
    new_coro(f);
}

void socks5Server::server_proto() {
    unsigned char in_buffer[0x100];
    unsigned char out_buffer[0x100];

    // method select
    io_check(prev->read(in_buffer, 2));

    if (in_buffer[0] != 5) {
        int fd = prev->fileno;
        struct sockaddr_in addr;
        unsigned int addr_len;
        getpeername(fd, (struct sockaddr *) &addr, (socklen_t *) &addr_len);
        logger(ERR, stderr, "get a no socks5 conn from %s:%d", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
        close_ret;
    }

    if (in_buffer[1] < 1) {
        // no method
        method_select_err();
        close_ret;
    }

    int method_count = in_buffer[1];

    io_check(prev->read(in_buffer, method_count));
    int i;
    for (i = 0; i < method_count; i++) {
        if (in_buffer[i] == 0) {
            break;
        }
    }
    if (i == method_count) {
        method_select_err();
        close_ret;
    }
    out_buffer[0] = 5;
    out_buffer[1] = 0;
    io_check(prev->write(out_buffer, 2));

    // target req/resp
    io_check(prev->read(in_buffer, 4));

    if (in_buffer[0] != 5 || in_buffer[2] != 0) {
        resp_err(1, 0, 0);
        close_ret;
    }

    // not support other cmd
    if (in_buffer[1] != 1) {
        resp_err(7, 0, 0);
        close_ret;
    }

    int addr_type = in_buffer[3];
    in_addr_t ip;
    in_port_t port;
    unsigned char *url = 0;
    if (addr_type == 1) {
        io_check(prev->read(reinterpret_cast<unsigned char *>(&ip), 4));
        io_check(prev->read(reinterpret_cast<unsigned char *>(&port), 2));
    } else if (addr_type == 3) {
        unsigned char url_size;
        io_check(prev->read(&url_size, 1));
        url = static_cast<unsigned char *>(malloc(url_size + 1));
        memset(url, 0, url_size + 1);
        io_check(prev->read(url, url_size));
        url[url_size] = 0;
        io_check(prev->read(reinterpret_cast<unsigned char *>(&port), 2));
    } else {
        // not support ipv6
        resp_err(8, 0, 0);
        close_ret;
    }

    if (addr_type == 1) {
        struct in_addr tmp;
        tmp.s_addr = ip;
//        logger(INFO, stderr, "get a socks5 req to %s:%d", inet_ntoa(tmp), ntohs(port));
    } else {
//        logger(INFO, stderr, "get a socks5 req to %s:%d", url, ntohs(port));
    }

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
    out_buffer[0] = 5;
    out_buffer[1] = 0;
    out_buffer[2] = 0;
    out_buffer[3] = 1;
    memcpy(out_buffer + 4, &s_ip, 4);
    memcpy(out_buffer + 8, &s_port, 2);
    io_check(prev->write(out_buffer, 10));

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

void socks5Server::method_select_err() {
    int fd = prev->fileno;
    struct sockaddr_in addr;
    unsigned int addr_len;
    getpeername(fd, (struct sockaddr *) &addr, (socklen_t *) &addr_len);
    logger(ERR, stderr, "socks5 conn error from %s:%d", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));

    unsigned char out_buf[2];
    out_buf[0] = 5;
    out_buf[1] = 0xff;

    prev->write(out_buf, 2);
}

void socks5Server::resp_err(unsigned char err_code, in_addr_t ip, in_port_t port) {
    unsigned char buf[10];
    struct sockaddr_in addr;
    unsigned int addr_len;
    int fd = prev->fileno;

    addr_len = sizeof(addr);
    getpeername(fd, (struct sockaddr *) &addr, (socklen_t *) &addr_len);
    logger(ERR, stderr, "socks5 conn error from %s:%d", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));


    buf[0] = 5;
    buf[1] = err_code;
    buf[2] = 0;
    buf[3] = 1;
    memcpy(buf + 4, &ip, 4);
    memcpy(buf + 8, &port, 2);
    prev->write(buf, 10);
}

void socks5Server::transport() {
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
