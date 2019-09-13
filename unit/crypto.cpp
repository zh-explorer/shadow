//
// Created by explorer on 9/5/19.
//

#include <cassert>
#include <cstdio>
#include <cstring>
#include "crypto.h"
#include "coro/coro.h"

char *hexset2 = "0123456789ABCDEF";

char *tohex2(unsigned char *data, unsigned int size) {
    unsigned int i;
    char *out = static_cast<char *>(malloc(size * 2 + 1));
    for (i = 0; i < size; i++) {
        out[2 * i] = hexset2[data[i] >> 4];
        out[2 * i + 1] = hexset2[data[i] & 0xf];
    }
    out[2 * i] = '\x00';
    return out;
}

unsigned char *password = (unsigned char *) "meiyoumima";

sha256::sha256() {
    SHA256_Init(&ctx);
}

void sha256::update(unsigned char *buffer, unsigned int size) {
    SHA256_Update(&ctx, buffer, size);
}

void sha256::digest(unsigned char *buffer, unsigned int buffer_size) {
    assert(buffer_size >= SHA256_DIGEST_LENGTH);
    SHA256_Final(buffer, &ctx);
}

void sha256::hexdigest(char *buffer, unsigned int buffer_size) {
    unsigned char md[SHA256_DIGEST_LENGTH];
    SHA256_Final(md, &ctx);
    b2hex(md, SHA256_DIGEST_LENGTH, buffer, buffer_size);
}

void sha256::SHA256(unsigned char *buffer, unsigned int size, unsigned char *md, unsigned int md_size) {
    ::SHA256(buffer, size, md);
}

char *hex_set = "0123456789ABCDEF";

void b2hex(const unsigned char *in_buf, unsigned int size, char *out_buf, unsigned int out_buf_size) {
    unsigned int i;
    assert(out_buf_size >= size * 2 + 1);
    for (i = 0; i < size; i++) {
        out_buf[2 * i] = hex_set[in_buf[i] >> 4];
        out_buf[2 * i + 1] = hex_set[in_buf[i] & 0xf];
    }
    out_buf[2 * i] = '\x00';
}

bool random::is_init = false;
unsigned char random::seed[32] = {0};

void random::random_init() {
    FILE *fd = fopen("/dev/urandom", "r");
    if (fd == nullptr) {
        logger(ERR, stderr, "urandom open failed");
        exit(-1);
    }
    fread(seed, 32, 1, fd);
    fclose(fd);
}

void random::random_byte(unsigned char *buffer, unsigned int size) {
    int i;
    unsigned char seed2[32];
    if (!is_init) {
        random_init();
        is_init = true;
    }
    for (i = 0; i < (int) (size - 32); i += 32) {
        memcpy(buffer + i, seed, 32);
        sha256::SHA256(buffer + i, 32, seed, sha256::digest_size());
    }
    memcpy(buffer + i, seed, size - i);
    memcpy(seed2, seed, 32);
    sha256::SHA256(seed2, 32, seed, sha256::digest_size());
}

aes::aes(unsigned char *token, unsigned int token_size, int enc_mod) {
    sha256 a;
    this->enc_mode = enc_mod;
    a.update(password, strlen(reinterpret_cast<const char *>(password)));
    a.update(token, token_size);
    unsigned char md[sha256::digest_size()];
    a.digest(md, sha256::digest_size());

    memcpy(key, md, AES_BLOCK_SIZE);
    memcpy(iv, md + AES_BLOCK_SIZE, AES_BLOCK_SIZE);
    if (enc_mod == AES_ENCRYPT) {
        AES_set_encrypt_key(key, AES_BLOCK_SIZE * 8, &aes_key);
    } else if (enc_mod == AES_DECRYPT) {
        AES_set_decrypt_key(key, AES_BLOCK_SIZE * 8, &aes_key);
    } else {
        logger(ERR, stderr, "unknown enc mode");
        exit(-1);
    }
}

void aes::update(unsigned char *data, unsigned int size) {
    unsigned char buffer[AES_DATA_BLOCK_SIZE];
    unsigned char buffer2[AES_DATA_BLOCK_SIZE];
    in_buffer.write(data, size);
    while (in_buffer.length() >= AES_DATA_BLOCK_SIZE) {
        in_buffer.read(buffer, AES_DATA_BLOCK_SIZE);
        AES_cbc_encrypt(buffer, buffer2, AES_DATA_BLOCK_SIZE, &aes_key, iv, enc_mode);
        out_buffer.write(buffer2, AES_DATA_BLOCK_SIZE);
    }

    if (in_buffer.length() == 0) {
        return;
    }

    unsigned enc_size = in_buffer.length() - in_buffer.length() % 16;
    in_buffer.read(buffer, enc_size);
    AES_cbc_encrypt(buffer, buffer2, enc_size, &aes_key, iv, enc_mode);
    out_buffer.write(buffer2, enc_size);
}


unsigned int aes::enc_data_length() {
    return out_buffer.length();
}

unsigned int aes::get_enc_data(unsigned char *data, unsigned int size) {
    return out_buffer.read(data, size);
}

aes_enc::aes_enc(unsigned char *token, unsigned int token_size) : aes(token, token_size, AES_ENCRYPT) {

}

void aes_enc::data_fin() {
    assert(in_buffer.length() == 0);
}

aes_dec::aes_dec(unsigned char *token, unsigned int token_size) : aes(token, token_size, AES_DECRYPT) {

}

void aes_dec::data_fin() {
    assert(in_buffer.length() == 0);
}