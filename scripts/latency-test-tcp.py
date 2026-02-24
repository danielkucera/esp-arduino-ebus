#!/usr/bin/env python

import socket
import time


TCP_IP = 'esp-ebus.local'
TCP_PORT = 3334
BUFFER_SIZE = 1024
MESSAGE = "H"

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.connect((TCP_IP, TCP_PORT))

while True:
    start = time.time()
#    s.send(MESSAGE)
    #s.sendall(MESSAGE)
    data = s.recv(BUFFER_SIZE)
    print("received data:", data, time.time() - start)

s.close()

