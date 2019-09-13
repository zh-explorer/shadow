//
// Created by explorer on 9/3/19.
//

#include <cstring>
#include "protocol.h"


std::pair<PIPE *, PIPE *> create_pipe() {
    PIPE *a, *b;
    a = new PIPE();
    b = new PIPE();
    a->set_peer(b);
    b->set_peer(a);
    std::pair<PIPE *, PIPE *> p(a, b);
    return p;
}

void PIPE::set_peer(PIPE *peer_pipe) {
    this->peer = peer_pipe;
}

int PIPE::read(unsigned char *buffer, unsigned int size, enum PIPE_READ_MODE mode) {
    if (!size) {
        return 0;
    }
    if (mode == READ_FIX) {
        unsigned read_size = 0;
        while (true) {
            while (!arrayBuf.length()) {
                add_event(this, -1);
                wait_event();
                if (is_closed) {
                    return -1;
                }
            }
            read_size += arrayBuf.read(buffer + read_size, size - read_size);
            if (arrayBuf.length() < 1048576) {
//                peer->set_unblock();
            }
            if (read_size == size) {
                return read_size;
            }
        }
    } else {
        while (!arrayBuf.length()) {
            add_event(this, -1);
            wait_event();
            if (is_closed) {
                return -1;
            }
        }
        unsigned int read_size = arrayBuf.read(buffer, size);
        if (arrayBuf.length() < 1048576) {
//            peer->set_unblock();
        }
        return read_size;
    }
}


bool PIPE::should_release() {
    if (is_closed) {
        return true;
    }
    return arrayBuf.length() || write_block;
}

int PIPE::write(unsigned char *buffer, unsigned int size) {
    if (!size) {
        return 0;
    }
    if (is_closed) {
        return -1;
    }
    while (write_block) {
        add_event(this, -1);
        wait_event();
    }
    if (is_closed) { // the pipe maybe close when wait
        return -1;
    }
    peer->recv_data(buffer, size);
    return size;
}

void PIPE::recv_data(unsigned char *buffer, unsigned int size) {
    arrayBuf.write(buffer, size);
    if (arrayBuf.length() >= 1048576) {   // 10m
//        peer->set_block();
    }
}

void PIPE::close() {
    if (is_closed) {
        return;
    }
    is_closed = true;
    peer->is_closed = true;
}

void PIPE::set_block() {
    this->write_block = true;
}

void PIPE::set_unblock() {
    this->write_block = false;
}


protocol::protocol(PIPE *prev, PIPE *next, Future *protocol_ready) {
    this->prev = prev;
    this->next = next;
    this->protocol_ready = protocol_ready;
}

void protocol::coro_exit(Coroutine *coro, protocol *proto) {
    delete coro;
    proto->coro_finish();
}

void protocol::coro_finish() {
    coro_count--;
    if (!coro_count) {
        all_coro_fin();
    }
}

void protocol::all_coro_fin() {
    if (protocol_ready != nullptr) {
        protocol_ready->set(reinterpret_cast<void *>(-1));
    }
    delete prev;
    delete next;
    delete this;
}

client_protocol::client_protocol(PIPE *prev, PIPE *next, Future *protocol_ready, char *url, in_port_t port)
        : protocol(prev, next, protocol_ready) {
    this->url = url;
    this->port = port;
}

fd_transport::fd_transport(PIPE *prev, aio *io) {
    this->prev = prev;
    this->io = io;

    std::function<void()> f = [this]() { this->transport_from(); };
    auto *coro1 = new Coroutine(f);
    coro1->add_done_callback(fd_transport::coro_done, this);
    current_event->add_to_poll(coro1);

    std::function<void()> f2 = [this]() { this->transport_to(); };
    auto *coro2 = new Coroutine(f2);
    coro1->add_done_callback(fd_transport::coro_done, this);
    current_event->add_to_poll(coro2);
}

char *hexset3 = "0123456789ABCDEF";
char tohex_buf3[0x4000];

char *tohex3(unsigned char *data, unsigned int size) {
    unsigned int i;
    char *out = tohex_buf3;
    for (i = 0; i < size; i++) {
        out[2 * i] = hexset3[data[i] >> 4];
        out[2 * i + 1] = hexset3[data[i] & 0xf];
    }
    out[2 * i] = '\x00';
    return out;
}

void fd_transport::transport_from() {
    unsigned char buffer[0x1000];
    while (true) {
        auto read_re = prev->read(buffer, 0x1000, READ_ANY);
        if (read_re == -1) {
            prev->close();
            io->close();
            return;
        }
        auto write_re = io->write(buffer, read_re, write_all);
        if (write_re == -1) {
            prev->close();
            io->close();
            return;
        }
    }
}

void fd_transport::transport_to() {
    unsigned char buffer[0x1000];
    while (true) {
        auto read_re = io->read(buffer, 0x1000, read_any);
        if (read_re == -1) {
            prev->close();
            io->close();
            return;
        }
        auto write_re = prev->write(buffer, read_re);
        if (write_re == -1) {
            prev->close();
            io->close();
            return;
        }
    }
}

void fd_transport::coro_exit() {
    exit_count--;
    if (!exit_count) {
        delete prev;
        delete io;
        delete this;
    }
}

void fd_transport::coro_done(Coroutine *coro, fd_transport *transport) {
    delete coro;
    transport->coro_exit();
}
