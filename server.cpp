//
// Created by explorer on 9/4/19.
//

#include "chain_build.h"
#include <coro/coro.h>
#include <cstring>
#include "proto/protocol.h"
#include "proto/scs.h"
#include "proto/socks5.h"
#include "proto/SC.h"
#include <cstdlib>
#include <unistd.h>
#include <csignal>

int server_chain_builder(aio *io) {
    auto pipes = create_pipe();
    pipes.first->fileno = io->fileno;
    new fd_transport(pipes.second, io);

    auto pipes2 = create_pipe();
    pipes2.first->fileno = io->fileno;
    Future conn2;
    new SC(pipes2.second, pipes.first, &conn2);
    if (conn2.val != 0) {
        return -1;
    }

    auto pipes3 = create_pipe();
    pipes3.first->fileno = io->fileno;
    Future conn;
    auto s = new SCSServer(pipes2.first, pipes3.second, &conn);
    conn.wait();
    if (conn.val != 0) {
        return -1;
    }
    pipes.second->fileno = s->next->fileno;
    return 0;
}

int client_chain_builder(protocol *server, char *url, unsigned short port) {
    auto *client = new aio_client(AF_INET, SOCK_STREAM, 0);
    auto re = client->connect(url, port);
    if (re == -1) {
        logger(ERR, stderr, "connection client failed: %s", strerror(errno));
        return -1;
    }
    server->next->fileno = client->fileno;

    new fd_transport(server->next->peer, client);
    return 0;
}


void run_server() {
    auto *server = new aio_server(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    if (setsockopt(server->fileno, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) != 0) {
        logger(ERR, stderr, "set socket opt error");
        exit(-1);
    }
//    auto re = server->bind(12345);
//    if (re != 0) {
//        logger(ERR, stderr, "bind error: %s", strerror(errno));
//        exit(-1);
//    }
    server->listen(30);

    struct sockaddr_in addr;
    unsigned int addr_len;
    addr_len = sizeof(addr);
    auto result = getsockname(server->fileno, (struct sockaddr *) &addr, (socklen_t *) &addr_len);
    if (result == -1) {
        logger(ERR, stderr, "can not find port, maybe server is error");
    }

    logger(INFO, stderr, "server bind at %d", ntohs(addr.sin_port));

    while (true) {
        auto new_io = server->accept();
        if (new_io == nullptr) {
            logger(ERR, stderr, "accept error");
            exit(-1);
        }
//        auto *coro = new Coroutine(reinterpret_cast<void *(*)(void *)>(server_chain_builder), new_io);
        auto *coro = new Coroutine(server_chain_builder, new_io);
        add_to_poll(coro);
    }
}

void recv_stdin() {
    unsigned char buffer[0x100];
    aio a(STDIN_FILENO);
    while (true) {
        auto re = a.read(buffer, 0x100, read_any);
        if (re == -1) {
            exit(0);
        }
    }
}

int main() {
//    auto *coro = new Coroutine(reinterpret_cast<void *(*)(void *)>(run_server), nullptr);
    auto *coro = new Coroutine(run_server);
    auto *e = new EventLoop;
    e->add_to_poll(coro);

    auto *coro2 = new Coroutine(recv_stdin);
    e->add_to_poll(coro2);
    e->loop();
}