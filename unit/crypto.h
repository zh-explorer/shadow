//
// Created by explorer on 9/5/19.
//

#ifndef SHADOW_CRYPTO_H
#define SHADOW_CRYPTO_H

#include <openssl/sha.h>
#include <openssl/aes.h>
#include <coro/unit/array_buf.h>

extern unsigned char *password;
#define AES_DATA_BLOCK_SIZE 1024

class aes {
public:
    aes(unsigned char *token, unsigned int token_size, int enc_mode);

    void update(unsigned char *data, unsigned int size);

    virtual void data_fin() = 0;

    unsigned int enc_data_length();

    unsigned int get_enc_data(unsigned char *data, unsigned int size);

protected:
    unsigned char key[AES_BLOCK_SIZE];
    unsigned char iv[AES_BLOCK_SIZE];
    AES_KEY aes_key;
    array_buf out_buffer;
    array_buf in_buffer;
    int enc_mode;
};

class aes_enc : public aes {
public:
    aes_enc(unsigned char *token, unsigned int token_size);

    void data_fin() override;
};

class aes_dec : public aes {
public:
    aes_dec(unsigned char *token, unsigned int token_size);

    void data_fin() override;
};

class random {
public:
    static void random_byte(unsigned char *buffer, unsigned int size);

private:
    static unsigned char seed[32];
    static bool is_init;

    static void random_init();
};

class sha256 {
public:
    sha256();

    static void SHA256(unsigned char *buffer, unsigned int size, unsigned char *md, unsigned int md_size);

    void update(unsigned char *buffer, unsigned int size);

    // buffer size must >= SHA256_DIGEST_LENGTH
    void digest(unsigned char *buffer, unsigned int buffer_size);

    static int digest_size() {
        return SHA256_DIGEST_LENGTH;
    }

    // buffer size must >= SHA256_DIGEST_LENGTH*2 + 1
    void hexdigest(char *buffer, unsigned int buffer_size);

private:
    SHA256_CTX ctx{};
};

void b2hex(const unsigned char *in_buf, unsigned int size, char *out_buf, unsigned int out_buf_size);

#endif //SHADOW_CRYPTO_H
