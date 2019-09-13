//
// Created by explorer on 9/4/19.
//

#ifndef SHADOW_SOCKS5_H
#define SHADOW_SOCKS5_H

#include "protocol.h"

class socks5Server : public protocol {
public:
    socks5Server(PIPE *prev, PIPE *next, Future *protocol_ready);

    void server_proto();

    void method_select_err();

    void resp_err(unsigned char err_code, in_addr_t ip, in_port_t port);

    void transport();
};


#endif //SHADOW_SOCKS5_H
