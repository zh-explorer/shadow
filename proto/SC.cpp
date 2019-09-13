//
// Created by explorer on 9/5/19.
//

#include <cstring>
#include <cassert>
#include "SC.h"
#include "../unit/crypto.h"

char *hexset = "0123456789ABCDEF";
char tohex_buf[0x4000];

char *tohex(unsigned char *data, unsigned int size) {
    unsigned int i;
    char *out = tohex_buf;
    for (i = 0; i < size; i++) {
        out[2 * i] = hexset[data[i] >> 4];
        out[2 * i + 1] = hexset[data[i] & 0xf];
    }
    out[2 * i] = '\x00';
    return out;
}

bool check_replay(time_t timestamp, char *noise);

typedef struct {
    unsigned char token[16];
    time_t timestamp;
    unsigned char noise[8];
    unsigned char main_version;
    uint32_t length;
    unsigned char random_len;
    unsigned char padding[10];
    unsigned char hash_sum[32];
    unsigned char data[1];
} __attribute__ ((packed)) sc_package;


SC::SC(PIPE *prev, PIPE *next, Future *protocol_ready) : protocol(prev, next, protocol_ready) {
    std::function<void()> f = [this]() { this->enc_proto(); };
    new_coro(f);
    std::function<void()> f2 = [this]() { this->dec_proto(); };
    new_coro(f2);
    this->protocol_ready->set();
    this->protocol_ready = nullptr;
}

void SC::enc_proto() {
    while (true) {
        unsigned char buffer[2048];
        sc_package *pack;
        auto data_size = prev->read(buffer, 2048, READ_ANY);

        if (data_size == -1) {
            close_ret;
        }
        unsigned char pad_size = AES_BLOCK_SIZE - data_size % AES_BLOCK_SIZE;

        unsigned char random_len;
        random::random_byte(&random_len, 1);
        random_len %= 0x30;

        unsigned int length = 80 + data_size + pad_size + random_len;
        pack = static_cast<sc_package *>(malloc(length));
        memset(pack, 0, length);

        pack->length = length;
        pack->timestamp = time(nullptr);
        random::random_byte(pack->noise, 8);

        sha256 token_calc;
        token_calc.update(password, strlen(reinterpret_cast<const char *>(password)));
        token_calc.update(reinterpret_cast<unsigned char *>(&pack->timestamp), 8);
        token_calc.update(pack->noise, 8);
        unsigned char md[sha256::digest_size()];
        token_calc.digest(md, sha256::digest_size());
        memcpy(pack->token, md, 16);

        pack->main_version = 1;
        pack->random_len = random_len;
        random::random_byte(pack->padding, 10);

        memcpy(pack->data, buffer, data_size);
        for (int i = 0; i < pad_size; i++) {
            pack->data[data_size + i] = pad_size;
        }

        random::random_byte(pack->data + data_size + pad_size, random_len);
        sha256::SHA256(reinterpret_cast<unsigned char *>(pack), length, md, sha256::digest_size());
        memcpy(pack->hash_sum, md, sha256::digest_size());

        aes_enc aes(pack->token, 16);
        aes.update(reinterpret_cast<unsigned char *>(&pack->timestamp), length - 16 - random_len);
        aes.data_fin();

        aes.get_enc_data(reinterpret_cast<unsigned char *>(&pack->timestamp), length - 16 - random_len);

        auto re = next->write(reinterpret_cast<unsigned char *>(pack), length);
        free(pack);
        if (re == -1) {
            close_ret;
        }
    }
}

void SC::dec_proto() {
    while (true) {
        sc_package pack;
        memset(&pack, 0, sizeof(sc_package));
        io_check(next->read(pack.token, 16));

        aes_dec aes(pack.token, 16);


        io_check(next->read((unsigned char *) &pack.timestamp, 16));

        aes.update(reinterpret_cast<unsigned char *>(&pack.timestamp), 16);
        aes.get_enc_data(reinterpret_cast<unsigned char *>(&pack.timestamp), 16);

        sha256 token_calc;
        token_calc.update(password, strlen(reinterpret_cast<const char *>(password)));
        token_calc.update(reinterpret_cast<unsigned char *>(&pack.timestamp), 8);
        token_calc.update(pack.noise, 8);
        unsigned char md[sha256::digest_size()];
        token_calc.digest(md, sha256::digest_size());
        if (memcmp(pack.token, md, 16) != 0) {
            logger(ERR, stderr, "token error");
            close_ret;
        }

        if (!check_replay(pack.timestamp, reinterpret_cast<char *>(pack.noise))) {
            logger(ERR, stderr, "replay packed");
            close_ret;
        }

        io_check(next->read(&pack.main_version, 48));

        aes.update(&pack.main_version, 48);
        aes.get_enc_data(&pack.main_version, 48);

        if (pack.main_version != 1) {
            logger(ERR, stderr, "unknown main version");
            close_ret;
        }

        unsigned int data_size = pack.length - pack.random_len - 80;
        auto enc_data = (unsigned char *) malloc(data_size + 1);
        auto dec_data = (char *) enc_data;

        auto re = next->read(enc_data, data_size);
        if (re == -1) {
            close_ret;
        }
        aes.update(enc_data, data_size);
        aes.get_enc_data(enc_data, data_size);

        unsigned char random_data[0x30];
        assert(pack.random_len <= 0x30);
        re = next->read(random_data, pack.random_len);
        if (re == -1) {
            close_ret;
        }

        sha256 sha;
        unsigned char hash_sum[sha256::digest_size()];
        memcpy(hash_sum, pack.hash_sum, sha256::digest_size());
        memset(pack.hash_sum, 0, sha256::digest_size());
        sha.update(reinterpret_cast<unsigned char *>(&pack), 80);
        memcpy(pack.hash_sum, hash_sum, sha256::digest_size());
        sha.update(enc_data, data_size);
        sha.update(random_data, pack.random_len);

        sha.digest(hash_sum, sha256::digest_size());

        if (memcmp(pack.hash_sum, hash_sum, sha256::digest_size()) != 0) {
            logger(ERR, stderr, "hash sum mismatch");
            close_ret;
        }

        auto pad = dec_data[data_size - 1];
        assert(pad <= 16);
        re = prev->write(enc_data, data_size - pad);
        if (re == -1) {
            close_ret;
        }
    }
}

bool check_replay(time_t timestamp, char *noise) {
    static std::map<time_t, std::vector<uint64_t>> noise_map;
    time_t t = time(nullptr);
    if (timestamp < t - 600) {
        return false;
    }
    auto end = noise_map.upper_bound(t);
    noise_map.erase(noise_map.begin(), end);

    auto noise_n = *(uint64_t *) noise;
    auto vec = noise_map[timestamp];
    for (auto &&n : vec) {
        if (n == noise_n) {
            return false;
        }
    }
    vec.push_back(noise_n);
    return true;

}

