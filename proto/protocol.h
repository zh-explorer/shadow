//
// Created by explorer on 9/3/19.
//

#ifndef SHADOW_PROTOCOL_H
#define SHADOW_PROTOCOL_H

#include <coro/coro.h>
#include <netinet/in.h>


#define close_ret prev->close(); next->close(); return;

#define io_check(expr) { \
    auto __ret_value = (expr); \
    if (__ret_value == -1){ \
        close_ret;      \
    } \
}

enum PIPE_READ_MODE {
    READ_FIX,       // sleep until read fixed size.
    READ_ANY,       // ret imm if have data.
};

// the pipe always be a pair
class PIPE : Event {
public:
    void set_peer(PIPE *peer_pipe);

    //this read return the actual read size.
    int read(unsigned char *buffer, unsigned int size, enum PIPE_READ_MODE mode = READ_FIX);

    //write will sleep until all data write. write -1 mean write failed
    int write(unsigned char *buffer, unsigned int size);

    void recv_data(unsigned char *buffer, unsigned int size);

    bool should_release() override;

    void close();

    // this will block the write
    void set_block();

    void set_unblock();

    bool is_closed = false;

    int fileno;
    PIPE *peer;
private:
    array_buf arrayBuf;

    bool request_data = false;
    unsigned request_size = 0;

    bool write_block = false;
};

class protocol {
public:
    protocol(PIPE *prev, PIPE *next, Future *protocol_ready);

    void coro_finish();

    static void coro_exit(Coroutine *coro, protocol *proto);

    virtual void all_coro_fin();

//    Coroutine *new_coro(void *(*func)(void *), void *arg, size_t stack_size = 0x20000);

    template<class R, class ... ARGS>
    Coroutine *new_coro(R (*func)(ARGS ...), ARGS ... args) {
        auto *coro = new Coroutine(func, args...);
        current_event->add_to_poll(coro);
        coro->add_done_callback(protocol::coro_exit, this);
        coro_count_add();
        return coro;
    }

    template<class R, class ... ARGS>
    Coroutine *new_coro(std::function<R(ARGS...)> func, ARGS ... args) {
        auto *coro = new Coroutine(func, args...);
        current_event->add_to_poll(coro);
        coro->add_done_callback(protocol::coro_exit, this);
        coro_count_add();
        return coro;
    }

    void coro_count_add() {
        coro_count++;
    }

    PIPE *prev;
    PIPE *next;

    Future *protocol_ready;
private:
    int coro_count = 0;

};


class client_protocol : public protocol {
public:
    client_protocol(PIPE *prev, PIPE *next, Future *protocol_ready, char *url, in_port_t port);

    char *url;
    in_port_t port;
};

class fd_transport {
public:
    explicit fd_transport(PIPE *prev, aio *io);

    void transport_from();

    void transport_to();

    void coro_exit();

    static void coro_done(Coroutine *coro, fd_transport *transport);

private:
    int exit_count = 2;
    PIPE *prev;
    aio *io;
};

std::pair<PIPE *, PIPE *> create_pipe();

#endif //SHADOW_PROTOCOL_H
