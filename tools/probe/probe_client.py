#!/usr/bin/env python3
"""probe_client.py — hand-written Minecraft Java 26.2 (protocol 776) probe client.

Pure stdlib (socket + zlib). VarInt/framing implemented locally; packet ids and
field codecs mirror the 26.2 sources in /home/z/my-project/mcsrc:
  - handshake ClientIntentionPacket: varint protocol, utf host, u16 port, varint intent
  - status:    sb 0x00 request / 0x01 ping(i64); cb 0x00 response(utf json) / 0x01 pong(i64)
  - login:     sb 0x00 hello(utf16 name + uuid), 0x03 login_acknowledged;
               cb 0x02 login_finished(uuid, utf16 name, props, uuid session),
               0x03 login_compression(varint threshold), 0x00 disconnect
  - configuration: cb 0x01 custom_payload, 0x03 finish, 0x04 keepalive, 0x05 ping,
               0x07 registry_data(utf key + varint count + entries), 0x0C features,
               0x0D update_tags, 0x0E select_known_packs;
               sb 0x00 client_information, 0x03 finish, 0x04 keepalive, 0x05 pong,
               0x07 select_known_packs (echo)

Usage:
  probe_client.py --port N status          # handshake(intent=1) -> JSON -> ping -> pong
  probe_client.py --port N login           # full login -> configuration -> play boundary
Options:
  --tag NAME   tag used for registry dump dir (/tmp/probe-registry-<tag>) [default: probe]
  --timeout S  per-read socket timeout [default: 10]
"""
import argparse
import hashlib
import json
import os
import socket
import struct
import sys
import time
import zlib


# ---------------------------------------------------------------------------
# VarInt (net/minecraft/network/VarInt.java: LEB128, 7 bits/byte, max 5 bytes)
# ---------------------------------------------------------------------------
def write_varint(value: int) -> bytes:
    value &= 0xFFFFFFFF
    out = bytearray()
    for _ in range(5):
        b = value & 0x7F
        value >>= 7
        if value:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)
    raise ValueError("varint too big")


def read_varint(data: bytes, pos: int):
    value = 0
    for i in range(5):
        if pos >= len(data):
            raise ValueError("varint truncated")
        b = data[pos]
        pos += 1
        value |= (b & 0x7F) << (7 * i)
        if not (b & 0x80):
            return value, pos
    raise ValueError("varint too big")


def write_string(s: str) -> bytes:
    raw = s.encode("utf-8")
    return write_varint(len(raw)) + raw


def read_string(data: bytes, pos: int):
    n, pos = read_varint(data, pos)
    if n > 32767 * 4:
        raise ValueError("string too long")
    s = data[pos:pos + n].decode("utf-8")
    return s, pos + n


def read_uuid(data: bytes, pos: int):
    msb, lsb = struct.unpack_from(">QQ", data, pos)
    return "%08x-%04x-%04x-%04x-%012x" % (
        msb >> 32, (msb >> 16) & 0xFFFF, msb & 0xFFFF, lsb >> 48, lsb & 0xFFFFFFFFFFFF), pos + 16


def offline_uuid(name: str):
    # UUIDUtil.createOfflinePlayerUUID: md5("OfflinePlayer:"+name), v3+IETF variant
    d = bytearray(hashlib.md5(("OfflinePlayer:" + name).encode()).digest())
    d[6] = (d[6] & 0x0F) | 0x30
    d[8] = (d[8] & 0x3F) | 0x80
    return bytes(d)


# ---------------------------------------------------------------------------
# Framed connection (CompressionDecoder/Encoder semantics after Set Compression)
# ---------------------------------------------------------------------------
class Conn:
    def __init__(self, host: str, port: int, timeout: float):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.settimeout(timeout)
        self.buf = b""
        self.threshold = None  # None = compression off

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass

    def _recv_more(self) -> bool:
        try:
            chunk = self.sock.recv(65536)
        except socket.timeout:
            return False
        if not chunk:
            raise ConnectionError("EOF")
        self.buf += chunk
        return True

    def read_frame(self, deadline: float) -> bytes:
        """One decompressed packet payload (id varint + body), sans framing."""
        while True:
            # try to parse a full frame from buf
            try:
                length, pos = read_varint(self.buf, 0)
            except ValueError:
                length = None
                pos = 0
            if length is not None and len(self.buf) - pos >= length:
                frame = self.buf[pos:pos + length]
                self.buf = self.buf[pos + length:]
                return self._decode_frame(frame)
            if time.time() > deadline:
                raise TimeoutError("read deadline exceeded")
            self._recv_more()

    def _decode_frame(self, frame: bytes) -> bytes:
        if self.threshold is None:
            return frame
        declared, pos = read_varint(frame, 0)
        payload = frame[pos:]
        if declared == 0:
            return payload  # below threshold: 0 prefix + raw
        if declared < self.threshold:
            raise ValueError("Badly compressed packet - size of %d is below threshold %d"
                             % (declared, self.threshold))
        raw = zlib.decompress(payload)
        if len(raw) != declared:
            raise ValueError("declared %d != actual %d" % (declared, len(raw)))
        return raw

    def send_packet(self, payload: bytes):
        if self.threshold is None:
            body = payload
        else:
            if len(payload) < self.threshold:
                body = write_varint(0) + payload       # below threshold -> 0 prefix
            else:
                body = write_varint(len(payload)) + zlib.compress(payload)
        self.sock.sendall(write_varint(len(body)) + body)

    def enable_compression(self, threshold: int):
        self.threshold = threshold


# ---------------------------------------------------------------------------
# packet builders
# ---------------------------------------------------------------------------
def handshake(protocol: int, host: str, port: int, intent: int) -> bytes:
    return write_varint(0x00) + write_varint(protocol) + write_string(host) + \
        struct.pack(">H", port) + write_varint(intent)


def pkt_status_request() -> bytes:
    return write_varint(0x00)


def pkt_ping_request(time_ms: int) -> bytes:
    return write_varint(0x01) + struct.pack(">q", time_ms)


def pkt_login_start(name: str, uid: bytes) -> bytes:
    return write_varint(0x00) + write_string(name) + uid


def pkt_login_acknowledged() -> bytes:
    return write_varint(0x03)


def pkt_client_information() -> bytes:
    # ClientInformation.write: utf lang, byte viewDistance, varint chatVisibility,
    # bool colors, ubyte modelCustomisation, varint mainHand, bool filtering,
    # bool allowsListing, varint particleStatus
    return (write_varint(0x00) + write_string("en_us") +
            bytes([12]) + write_varint(0) + b"\x01" + bytes([7]) +
            write_varint(1) + b"\x00\x00" + write_varint(0))


def pkt_select_known_packs(packs):
    out = write_varint(0x07) + write_varint(len(packs))
    for ns, pid, ver in packs:
        out += write_string(ns) + write_string(pid) + write_string(ver)
    return out


def pkt_keep_alive(v: int) -> bytes:
    return write_varint(0x04) + struct.pack(">q", v)


def pkt_pong(v: int) -> bytes:
    return write_varint(0x05) + struct.pack(">q", v)


# ---------------------------------------------------------------------------
# subcommands
# ---------------------------------------------------------------------------
def cmd_status(port: int, args) -> int:
    conn = Conn("127.0.0.1", port, args.timeout)
    try:
        conn.send_packet(handshake(776, "127.0.0.1", port, 1))
        conn.send_packet(pkt_status_request())
        deadline = time.time() + args.timeout
        resp = conn.read_frame(deadline)
        pid, pos = read_varint(resp, 0)
        if pid != 0x00:
            print("FAIL: expected status response (0x00), got 0x%02x" % pid, file=sys.stderr)
            return 1
        status_json, pos = read_string(resp, pos)
        print(status_json)  # full JSON to stdout
        json.loads(status_json)  # must be valid JSON
        conn.send_packet(pkt_ping_request(12345))
        pong = conn.read_frame(time.time() + args.timeout)
        pid, pos = read_varint(pong, 0)
        (t,) = struct.unpack_from(">q", pong, pos)
        if pid != 0x01 or t != 12345:
            print("FAIL: bad pong (id=0x%02x t=%d)" % (pid, t), file=sys.stderr)
            return 1
        print("STATUS OK: response+pong", file=sys.stderr)
        return 0
    finally:
        conn.close()


def cmd_login(port: int, args) -> int:
    reg_dir = "/tmp/probe-registry-%s" % args.tag
    os.makedirs(reg_dir, exist_ok=True)
    conn = Conn("127.0.0.1", port, args.timeout)
    seq = []  # (phase, direction, id, len) trace lines for the diff tool
    try:
        conn.send_packet(handshake(776, "127.0.0.1", port, 2))
        conn.send_packet(pkt_login_start("Probe", offline_uuid("Probe")))

        # --- login phase ---
        login_finished = None
        deadline = time.time() + args.timeout
        while login_finished is None:
            payload = conn.read_frame(deadline)
            pid, pos = read_varint(payload, 0)
            if pid == 0x03:  # login compression
                threshold, pos = read_varint(payload, pos)
                conn.enable_compression(threshold)
                print("login: set compression threshold=%d" % threshold, file=sys.stderr)
            elif pid == 0x00:  # login disconnect
                reason, _ = read_string(payload, pos)
                print("login: disconnected: %s" % reason, file=sys.stderr)
                return 1
            elif pid == 0x02:  # login finished (in compressed framing)
                uuid, pos = read_uuid(payload, pos)
                name, pos = read_string(payload, pos)
                nprops, pos = read_varint(payload, pos)
                session, pos = read_uuid(payload, pos)
                login_finished = (uuid, name, nprops, session)
                print("login: success uuid=%s name=%s props=%d session=%s"
                      % (uuid, name, nprops, session), file=sys.stderr)
            else:
                print("login: unexpected packet 0x%02x len=%d" % (pid, len(payload) - pos),
                      file=sys.stderr)
                return 1
        conn.send_packet(pkt_login_acknowledged())
        # the vanilla client pushes its options when entering configuration
        conn.send_packet(pkt_client_information())

        # --- configuration phase ---
        boundary = None
        deadline = time.time() + args.timeout
        requested_packs = None
        reg_idx = 0
        while boundary is None:
            try:
                payload = conn.read_frame(deadline)
            except TimeoutError:
                boundary = "timeout-10s"
                break
            except ConnectionError:
                boundary = "eof-after-finish"
                break
            pid, pos = read_varint(payload, 0)
            print("configuration: packet id=0x%02x len=%d" % (pid, len(payload) - pos))
            seq.append("config 0x%02x %d" % (pid, len(payload) - pos))
            if pid == 0x0E:  # select known packs request -> echo back
                n, pos2 = read_varint(payload, pos)
                packs = []
                for _ in range(n):
                    ns, pos2 = read_string(payload, pos2)
                    p, pos2 = read_string(payload, pos2)
                    v, pos2 = read_string(payload, pos2)
                    packs.append((ns, p, v))
                requested_packs = packs
                conn.send_packet(pkt_select_known_packs(packs))
                print("configuration: echoed %d known pack(s): %s" % (len(packs), packs),
                      file=sys.stderr)
            elif pid == 0x07:  # registry data -> dump
                key, pos2 = read_string(payload, pos)
                fname = "%03d_%s.bin" % (reg_idx, key.replace(':', '_').replace('/', '-'))
                with open(os.path.join(reg_dir, fname), "wb") as fh:
                    fh.write(payload)
                reg_idx += 1
            elif pid == 0x04:  # keepalive -> echo
                (t,) = struct.unpack_from(">q", payload, pos)
                conn.send_packet(pkt_keep_alive(t))
            elif pid == 0x05:  # ping -> pong
                (t,) = struct.unpack_from(">q", payload, pos)
                conn.send_packet(pkt_pong(t))
            elif pid == 0x03:  # finish configuration
                conn.send_packet(write_varint(0x03))
                print("configuration: finish acknowledged", file=sys.stderr)
                boundary = "finish-acknowledged"
            elif pid == 0x02:  # disconnect
                reason, _ = read_string(payload, pos)
                print("configuration: disconnected: %s" % reason, file=sys.stderr)
                return 1

        # --- play boundary: first packet after finish ack (or close/timeout) ---
        if boundary == "finish-acknowledged":
            try:
                payload = conn.read_frame(time.time() + 10)
                pid, pos = read_varint(payload, 0)
                print("play boundary: first packet id=0x%02x len=%d" % (pid, len(payload) - pos))
                boundary = "play-packet"
            except TimeoutError:
                print("play boundary: no packet within 10s", file=sys.stderr)
                boundary = "timeout-10s"
            except ConnectionError:
                print("play boundary: server closed after finish ack", file=sys.stderr)
                boundary = "eof-after-finish"
        print("LOGIN OK: boundary=%s registry_dumped=%d" % (boundary, reg_idx), file=sys.stderr)
        with open(os.path.join(reg_dir, "trace.txt"), "w") as fh:
            fh.write("\n".join(seq) + "\nboundary=%s\n" % boundary)
        return 0
    finally:
        conn.close()


def main() -> int:
    ap = argparse.ArgumentParser(description="MC 26.2 protocol probe client")
    ap.add_argument("--port", type=int, required=True)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--tag", default="probe")
    ap.add_argument("--timeout", type=float, default=10.0)
    ap.add_argument("cmd", choices=["status", "login"])
    args = ap.parse_args()
    if args.cmd == "status":
        return cmd_status(args.port, args)
    return cmd_login(args.port, args)


if __name__ == "__main__":
    sys.exit(main())
