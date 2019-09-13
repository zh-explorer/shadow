//
// Created by explorer on 9/4/19.
//

#ifndef SHADOW_SCS_H
#define SHADOW_SCS_H


#include "protocol.h"

class SCSServer : public protocol {
public:
    SCSServer(PIPE *prev, PIPE *next, Future *protocol_ready);

    void server_proto();

    void resp_err(unsigned char err_code, in_addr_t ip, in_port_t port);

    void transport();
};

class SCSClient : public client_protocol {
public:
    SCSClient(PIPE *prev, PIPE *next, Future *protocol_ready, char *url, unsigned short port);

    void client_proto();

    static char *errmsg(int err);

    void transport();
};

#endif //SHADOW_SCS_H
