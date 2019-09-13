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
#include <sys/types.h>
#include <sys/socket.h>
#include <csignal>
#include <zconf.h>

char *target_host;
unsigned short target_port;

int server_chain_builder(aio *io) {
    auto pipes = create_pipe();
    pipes.first->fileno = io->fileno;
    new fd_transport(pipes.second, io);

    auto pipes2 = create_pipe();
    pipes2.first->fileno = io->fileno;
    Future conn;
    auto s = new socks5Server(pipes.first, pipes2.second, &conn);
    conn.wait();
    if (conn.val != 0) {
        return -1;
    }
    pipes.second->fileno = s->next->fileno;
    return 0;
}

// TODO: this wtf code need rebuild
int client_chain_builder(protocol *server, char *url, unsigned short port) {
    auto *client = new aio_client(AF_INET, SOCK_STREAM, 0);

    auto re = client->connect(target_host, target_port);
    if (re == -1) {
        logger(ERR, stderr, "connection client failed: %s", strerror(errno));
        return -1;
    }

    server->next->fileno = client->fileno;

    auto pipes = create_pipe();
    pipes.second->fileno = server->prev->fileno;
    pipes.first->fileno = client->fileno;
    new fd_transport(pipes.second, client);

    auto pipes2 = create_pipe();            // TODO: the fileno in sc is not right. never mind
    pipes2.second->fileno = server->prev->fileno;
    pipes2.first->fileno = client->fileno;
    Future conn;
    new SC(pipes2.second, pipes.first, &conn);  // the sc read from prev enc and send to next
    conn.wait();
    if (conn.val != 0) {
        return -1;
    }

    Future conn2;
    new SCSClient(pipes2.first, server->next->peer, &conn2, url, port);
    conn2.wait();
    if (conn2.val != 0) {
        return -1;
    }
    return 0;
}


int run_server() {
    auto *server = new aio_server(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    if (setsockopt(server->fileno, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) != 0) {
        logger(ERR, stderr, "set socket opt error");
        exit(-1);
    }
    auto re = server->bind(1080);
    if (re != 0) {
        logger(ERR, stderr, "bind error: %s", strerror(errno));
        exit(-1);
    }
    server->listen(30);
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

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("%s: ip port\n", argv[0]);
        exit(0);
    }
    target_host =  argv[1];
    target_port = atoi(argv[2]);
//    auto *coro = new Coroutine(reinterpret_cast<void *(*)(void *)>(run_server), nullptr);
    auto *coro = new Coroutine(run_server);
    auto *e = new EventLoop;
    e->add_to_poll(coro);
    e->loop();
}