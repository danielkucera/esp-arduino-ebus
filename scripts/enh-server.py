#!/usr/bin/env python

import socket
import time


BUFFER_SIZE = 1024
MESSAGE = "H"

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(('0.0.0.0', 3335))
s.listen(1)

M1 = 0b11000000
M2 = 0b10000000

SYN = 0xAA

# requests
INIT = 0x0
SEND = 0x1
START = 0x2
INFO = 0x3

# responses

RESETTED = 0x0
RECEIVED = 0x1
STARTED = 0x2
INFO = 0x3
FAILED = 0xa
ERROR_EBUS = 0xb
ERROR_HOST = 0xc


def encode(c, d):
    return bytes([ M1 | c << 2 | d >> 6, M2 | (d & 0b00111111)])

def decode(b1, b2):
    c = (b1 >> 2) & 0b1111
    d = ((b1 & 0b11) << 6) | (b2 & 0b00111111)
    print(b1, b2, "->", c, d)
    return c, d 

def received(b):
    return encode(RECEIVED, b) 

def process_cmd(c, d):
    print("cmd:", c, d)
    if c == INIT:
        print("INIT:", d)
        return RESETTED, 0x0
    if c == START:
        if d == SYN:
            # abort arbitration ... ?
            return
        else:
            return STARTED, d
    if c == SEND:
        return RECEIVED, d

while True:
    print("Waiting for client")
    conn, addr = s.accept()
    conn.settimeout(0.1)

    while True:
        try:
            data = conn.recv(1)
        except TimeoutError as e:
            try:
                conn.sendall(received(0xaa))
                continue
            except:
                print("unable to sendall")
                break
        if len(data) < 1:
            continue

        ch = data[0]
        print(ch)
        if ch < 0b10000000:
            print("echoback")
            conn.send(ch)
        else:
            if ch < 0b11000000:
                print("error: first command signature error")
                continue
            data = conn.recv(1)
            if len(data) < 1:
                print("error: second command byte timeout")
                continue
            ch2 = data[0]

            if ch2 & 0b01000000 > 0:
                print("error: second command signature error")
                continue

            #print("command", ch, ch2)
            c, d = decode(ch, ch2)
            (rc, rd) = process_cmd(c, d)
            if rc:
                conn.send(encode(rc, rd))
                #if rc == STARTED:
                #    conn.send(received(d))




    conn.close()
