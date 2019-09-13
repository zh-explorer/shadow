//
// Created by explorer on 9/5/19.
//

#ifndef SHADOW_SC_H
#define SHADOW_SC_H


#include "protocol.h"

class SC : protocol {
public:
    // read the from prev than enc and send to next
    SC(PIPE *prev, PIPE *next, Future *protocol_ready);

    void enc_proto();

    void dec_proto();
};


#endif //SHADOW_SC_H
