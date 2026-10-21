from __future__ import annotations
import json
import socket
import struct

def exact(client: socket.socket, size: int) -> bytes:
    data = b""
    while len(data) < size:
        part = client.recv(size - len(data))
        if not part:
            return b""
        data += part
    return data

def receive(client: socket.socket) -> str:
    header = exact(client, 2)
    if not header or header[0] & 15 == 8:
        return ""
    length = header[1] & 127
    if length == 126:
        length = struct.unpack(">H", exact(client, 2))[0]
    if length == 127:
        length = struct.unpack(">Q", exact(client, 8))[0]
    mask, payload = exact(client, 4), exact(client, length)
    return bytes(byte ^ mask[index % 4] for index, byte in enumerate(payload)).decode("ascii")

def send(client: socket.socket, payload: str) -> None:
    data = payload.encode("ascii")
    client.sendall((bytes((129, len(data))) if len(data) < 126 else bytes((129, 126)) + struct.pack(">H", len(data))) + data)

