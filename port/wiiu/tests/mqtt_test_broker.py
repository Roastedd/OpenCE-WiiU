#!/usr/bin/env python3
"""A minimal MQTT 3.1.1 broker for internet play's signalling (port/linux/src/p2p_signal.c) on one computer, so that a
test of many copies of the game (internet_swarm.sh, beside this) needs neither the public brokers nor the internet.

It refuses MQTT 5 (CONNACK return code 1), which p2p_signal.c answers by connecting again with 3.1.1. It keeps
subscriptions (with the + and # wildcards), passes publishes to them at most once, keeps retained messages (an empty
one clears its topic), acknowledges QoS 1 publishes, answers pings, and publishes a connection's will when it ends
without a DISCONNECT.

    python3 mqtt_test_broker.py [port] [log file]
"""
import asyncio
import sys

retained = {}
subscribers = {}  # writer -> set of filters
log_file = None


def log(text):
    if log_file:
        log_file.write(text + "\n")
        log_file.flush()


def matches(topic_filter, topic):
    filter_parts = topic_filter.split("/")
    topic_parts = topic.split("/")
    for index, part in enumerate(filter_parts):
        if part == "#":
            return True
        if index >= len(topic_parts):
            return False
        if part != "+" and part != topic_parts[index]:
            return False
    return len(filter_parts) == len(topic_parts)


def encode_length(length):
    out = bytearray()
    while True:
        byte = length % 128
        length //= 128
        if length:
            byte |= 128
        out.append(byte)
        if not length:
            return bytes(out)


def packet(kind, body):
    return bytes([kind]) + encode_length(len(body)) + body


def string(data, offset):
    size = data[offset] << 8 | data[offset + 1]
    return data[offset + 2:offset + 2 + size], offset + 2 + size


def publish_packet(topic, payload, retain):
    body = len(topic).to_bytes(2, "big") + topic + payload
    return packet(0x30 | (1 if retain else 0), body)


def deliver(topic, payload):
    text = topic.decode("utf-8", "replace")
    for writer, filters in list(subscribers.items()):
        if any(matches(f, text) for f in filters):
            try:
                writer.write(publish_packet(topic, payload, False))
            except Exception:
                pass


async def read_packet(reader):
    header = await reader.readexactly(1)
    length = 0
    shift = 0
    while True:
        byte = (await reader.readexactly(1))[0]
        length |= (byte & 127) << shift
        shift += 7
        if not byte & 128:
            break
        if shift > 21:
            raise ValueError("bad length")
    body = await reader.readexactly(length) if length else b""
    return header[0], body


async def client(reader, writer):
    peer = writer.get_extra_info("peername")
    will = None
    clean = False
    subscribers[writer] = set()
    try:
        kind, body = await read_packet(reader)
        if kind != 0x10:
            return
        _, offset = string(body, 0)
        level = body[offset]
        flags = body[offset + 1]
        offset += 4
        if level != 4:
            writer.write(packet(0x20, bytes([0, 1])))
            await writer.drain()
            log(f"{peer}: refused MQTT level {level}")
            return
        client_id, offset = string(body, offset)
        if flags & 0x04:
            will_topic, offset = string(body, offset)
            will_message, offset = string(body, offset)
            will = (will_topic, will_message, bool(flags & 0x20))
        writer.write(packet(0x20, bytes([0, 0])))
        log(f"{peer}: connected {client_id!r}")
        while True:
            kind, body = await read_packet(reader)
            kind_high = kind & 0xF0
            if kind_high == 0x30:
                topic, offset = string(body, 0)
                quality = (kind >> 1) & 3
                if quality:
                    identifier = body[offset:offset + 2]
                    offset += 2
                    if quality == 1:
                        writer.write(packet(0x40, identifier))
                payload = body[offset:]
                if kind & 1:
                    if payload:
                        retained[topic] = payload
                    else:
                        retained.pop(topic, None)
                deliver(topic, payload)
            elif kind_high == 0x80:
                identifier = body[0:2]
                offset = 2
                granted = bytearray()
                new_filters = []
                while offset < len(body):
                    topic_filter, offset = string(body, offset)
                    offset += 1
                    text = topic_filter.decode("utf-8", "replace")
                    subscribers[writer].add(text)
                    new_filters.append(text)
                    granted.append(0)
                writer.write(packet(0x90, identifier + bytes(granted)))
                for topic, payload in list(retained.items()):
                    text = topic.decode("utf-8", "replace")
                    if any(matches(f, text) for f in new_filters):
                        writer.write(publish_packet(topic, payload, True))
            elif kind_high == 0xA0:
                identifier = body[0:2]
                offset = 2
                while offset < len(body):
                    topic_filter, offset = string(body, offset)
                    subscribers[writer].discard(topic_filter.decode("utf-8", "replace"))
                writer.write(packet(0xB0, identifier))
            elif kind_high == 0xC0:
                writer.write(packet(0xD0, b""))
            elif kind_high == 0xE0:
                clean = True
                return
            await writer.drain()
    except (asyncio.IncompleteReadError, ConnectionError, ValueError, IndexError):
        pass
    finally:
        subscribers.pop(writer, None)
        if will and not clean:
            topic, message, retain = will
            if retain:
                if message:
                    retained[topic] = message
                else:
                    retained.pop(topic, None)
            deliver(topic, message)
        try:
            writer.close()
        except Exception:
            pass


async def main():
    global log_file
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 1883
    if len(sys.argv) > 2:
        log_file = open(sys.argv[2], "a")
    server = await asyncio.start_server(client, "0.0.0.0", port)
    print(f"mqtt broker on port {port}", flush=True)
    async with server:
        await server.serve_forever()


if __name__ == "__main__":
    asyncio.run(main())
