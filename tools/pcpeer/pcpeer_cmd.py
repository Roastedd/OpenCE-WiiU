#!/usr/bin/env python3
"""Sends one command to a running pcpeer and prints the answer: pcpeer_cmd.py <port> <command words...>   e.g.  pcpeer_cmd.py 2323 social list"""
import socket
import sys

if len(sys.argv) < 3:
    sys.exit("usage: pcpeer_cmd.py <port> <command...>")
connection = socket.create_connection(("127.0.0.1", int(sys.argv[1])), timeout=10)
connection.sendall((" ".join(sys.argv[2:]) + "\n").encode())
connection.shutdown(socket.SHUT_WR)
answer = b""
while True:
    chunk = connection.recv(65536)
    if not chunk:
        break
    answer += chunk
sys.stdout.write(answer.decode("utf-8", "replace"))
