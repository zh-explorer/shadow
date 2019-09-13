//
// Created by explorer on 9/4/19.
//

#ifndef SHADOW_CHAIN_BUILD_H
#define SHADOW_CHAIN_BUILD_H

class protocol;

class aio;

int client_chain_builder(protocol *server, char *url, unsigned short port);

int server_chain_builder(aio *io);

#endif //SHADOW_CHAIN_BUILD_H
