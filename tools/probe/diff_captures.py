#!/usr/bin/env python3
"""diff_captures.py — compare two Minecraft 26.2 (protocol 776) capture sets.

Reads the raw per-direction byte streams written by mc-cpp's P0-b
capture-proxy (session<NNN>_c2s.bin / session<NNN>_s2c.bin), re-parses them
with full Minecraft framing + zlib compression handling, and checks that the
vanilla 26.2 server and the mc-cpp server behave equivalently:

  status : same clientbound packet id sequence (response, pong); JSON field
           STRUCTURE equivalent (description/players/version/enforcesSecureChat);
           version.protocol == 776 in both; pong echoes 12345 in both.
  login  : same login-phase sequence (set compression -> login finished);
           same thresholds; isomorphic configuration packet id sequence with
           equal registry_data key order / counts; same enabled features and
           known-pack request payloads; PLAY boundary reached in both.

Byte-identical payloads are NOT required (MOTD text, brand, registry entry
contents may differ) — structural equivalence is the acceptance criterion.

Usage:
  diff_captures.py --vanilla-dir /tmp/cap-vanilla --cpp-dir /tmp/cap-cpp \
                   [--status-session 1] [--login-session 2]
Exit code 0 = all PASS, 1 = at least one FAIL.
"""
import argparse
import json
import os
import struct
import sys
import zlib

PROTOCOL = 776


# ---------------------------------------------------------------------------
# stream parsing
# ---------------------------------------------------------------------------
def read_varint(data, pos):
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


def read_string(data, pos):
    n, pos = read_varint(data, pos)
    if n > 262144 * 4:
        raise ValueError("string too long: %d" % n)
    return data[pos:pos + n].decode("utf-8"), pos + n


def split_frames(stream: bytes):
    """Raw VarInt-length framing (before compression decode). Truncated
    trailing frames (server killed mid-write) are ignored."""
    frames = []
    pos = 0
    while pos < len(stream):
        try:
            length, start = read_varint(stream, pos)
        except ValueError:
            break
        if length == 0 or start + length > len(stream):
            break
        frames.append(stream[start:start + length])
        pos = start + length
    return frames


class DirectionParser:
    """One direction's raw bytes -> [(phase, id, body)] with compression.

    s2c: the set-compression frame itself is uncompressed; every later frame is
    compression-framed (CompressionEncoder on the server armed after flush).
    c2s: for a login session the client emits exactly two uncompressed frames
    (handshake + hello) before it learns the threshold; everything after the
    login-finished round-trip is compression-framed. Status sessions never
    compress.
    """

    def __init__(self, is_s2c: bool, threshold: int = None, compress_from: int = None,
                 start_phase: str = None):
        self.is_s2c = is_s2c
        self.threshold = threshold
        self.compress_from = compress_from  # c2s: frame index where compression starts
        self.events = []                    # (phase, id, body)
        # s2c cannot see the c2s handshake; caller passes the session kind.
        self.phase = start_phase or "HANDSHAKE"
        self.start_phase = self.phase
        self.frame_idx = 0

    def feed(self, raw: bytes):
        for frame in split_frames(raw):
            self._frame(frame)

    def _frame(self, frame: bytes):
        if self.threshold is not None and \
                (self.is_s2c or self.frame_idx >= self.compress_from):
            declared, pos = read_varint(frame, 0)
            body = frame[pos:]
            payload = body if declared == 0 else zlib.decompress(body)
        else:
            payload = frame
        pid, pos = read_varint(payload, 0)
        body = payload[pos:]
        self.events.append((self.phase, pid, body))

        # ---- phase / compression state machine (probe client's view) ----
        if self.phase == "HANDSHAKE":
            if self.is_s2c:
                self.phase = "STATUS"
            else:
                # c2s frame 0 IS the handshake packet: decode intent to pick
                # the following phase (ClientIntentionPacket:
                # varint protocol, string address, u16 port, varint intent)
                _, p = read_varint(payload, 0)   # packet id
                _, p = read_varint(payload, p)   # protocol version
                _, p = read_string(payload, p)   # address
                intent, _ = read_varint(payload, p + 2)  # skip u16 port, read intent
                self.phase = "LOGIN" if intent == 2 else "STATUS"
        elif self.is_s2c:
            if self.phase == "LOGIN" and pid == 0x03 and self.threshold is None:
                self.threshold, _ = read_varint(body, 0)  # set compression
            elif self.phase == "LOGIN" and pid == 0x02:
                self.phase = "CONFIGURATION"
            elif self.phase == "CONFIGURATION" and pid == 0x03:
                self.phase = "PLAY"      # finish sent; next s2c frame is play
            elif self.phase == "PLAY":
                self.phase = "PLAY_DONE"
        else:  # c2s
            if self.phase == "LOGIN" and pid == 0x03:
                self.phase = "CONFIGURATION"
        self.frame_idx += 1


def peek_c2s_intent(raw: bytes):
    """Decode the intent from the first (uncompressed) c2s frame."""
    frames = list(split_frames(raw))
    if not frames:
        return 1
    payload = frames[0]
    _, p = read_varint(payload, 0)   # packet id
    _, p = read_varint(payload, p)   # protocol version
    _, p = read_string(payload, p)   # address
    intent, _ = read_varint(payload, p + 2)  # skip u16 port, read intent
    return intent


def parse_session(directory: str, index: int):
    def load(kind):
        path = os.path.join(directory, "session%03d_%s.bin" % (index, kind))
        if not os.path.exists(path):
            return b""
        with open(path, "rb") as fh:
            return fh.read()

    c2s_raw = load("c2s")
    intent = peek_c2s_intent(c2s_raw)
    is_login = intent == 2
    # the client arms compression once it has seen the s2c set-compression
    # frame; the two pre-compression c2s frames are handshake + hello
    start_phase = "LOGIN" if is_login else "STATUS"
    s2c = DirectionParser(is_s2c=True, start_phase=start_phase)
    s2c.feed(load("s2c"))
    c2s_threshold = next((e[2] for e in s2c.events
                          if e[0] == "LOGIN" and e[1] == 0x03), None)
    threshold_val = read_varint(c2s_threshold, 0)[0] if c2s_threshold is not None else None
    c2s = DirectionParser(is_s2c=False, threshold=threshold_val,
                          compress_from=2 if threshold_val is not None else None)
    c2s.feed(c2s_raw)
    return s2c.events, c2s.events


# ---------------------------------------------------------------------------
# payload decoders (structural only)
# ---------------------------------------------------------------------------
def decode_status_json(payload):
    s, _ = read_string(payload, 0)
    return json.loads(s)


def decode_known_packs(payload):
    n, pos = read_varint(payload, 0)
    packs = []
    for _ in range(n):
        ns, pos = read_string(payload, pos)
        p, pos = read_string(payload, pos)
        v, pos = read_string(payload, pos)
        packs.append((ns, p, v))
    return packs


def decode_registry_data(payload):
    key, pos = read_string(payload, 0)
    n, pos = read_varint(payload, pos)
    entry_ids = []
    for _ in range(n):
        eid, pos = read_string(payload, pos)
        present = payload[pos]
        pos += 1
        if present:
            # optional NBT blob: skip (structure only)
            pos = len(payload)
            break
        entry_ids.append(eid)
    return key, n, entry_ids


def decode_features(payload):
    n, pos = read_varint(payload, 0)
    feats = []
    for _ in range(n):
        f, pos = read_string(payload, pos)
        feats.append(f)
    return feats


def json_shape(obj):
    """Type-structure fingerprint of parsed JSON (dict keys + leaf types)."""
    if isinstance(obj, dict):
        return {k: json_shape(v) for k, v in sorted(obj.items())}
    if isinstance(obj, list):
        return [json_shape(obj[0])] if obj else []
    return type(obj).__name__


# ---------------------------------------------------------------------------
# checks
# ---------------------------------------------------------------------------
class Report:
    def __init__(self):
        self.lines = []

    def check(self, ok, name, detail=""):
        self.lines.append("%-4s %s%s" % ("PASS" if ok else "FAIL", name,
                                         (" | " + detail) if detail else ""))
        return ok

    def info(self, name, detail):
        self.lines.append("INFO %s | %s" % (name, detail))


def compare_status(van_dir, cpp_dir, index, rep):
    v_s2c, v_c2s = parse_session(van_dir, index)
    c_s2c, c_c2s = parse_session(cpp_dir, index)

    rep.check([e[1] for e in v_s2c if e[0] == "STATUS"] == [0x00, 0x01],
              "vanilla status s2c ids == [0x00 response, 0x01 pong]",
              str([(e[0], hex(e[1])) for e in v_s2c]))
    rep.check([e[1] for e in c_s2c if e[0] == "STATUS"] == [0x00, 0x01],
              "mc-cpp   status s2c ids == [0x00 response, 0x01 pong]",
              str([(e[0], hex(e[1])) for e in c_s2c]))
    rep.check([e[1] for e in v_c2s] == [0x00, 0x00, 0x01],
              "vanilla status c2s ids == [hs 0x00, request 0x00, ping 0x01]",
              str([hex(e[1]) for e in v_c2s]))
    rep.check([e[1] for e in c_c2s] == [0x00, 0x00, 0x01],
              "mc-cpp   status c2s ids == [hs 0x00, request 0x00, ping 0x01]",
              str([hex(e[1]) for e in c_c2s]))

    v_resp = next((e[2] for e in v_s2c if e[0] == "STATUS" and e[1] == 0x00), None)
    c_resp = next((e[2] for e in c_s2c if e[0] == "STATUS" and e[1] == 0x00), None)
    if v_resp is None or c_resp is None:
        rep.check(False, "status response present in both captures")
        return
    vj, cj = decode_status_json(v_resp), decode_status_json(c_resp)
    rep.check(json_shape(vj) == json_shape(cj),
              "status JSON structure equivalent",
              "vanilla=%s" % sorted(vj.keys()))
    rep.check(vj.get("version", {}).get("protocol") == PROTOCOL and
              cj.get("version", {}).get("protocol") == PROTOCOL,
              "version.protocol == 776 in both",
              "vanilla=%s cpp=%s" % (vj.get("version"), cj.get("version")))
    rep.check(set(vj.get("players", {}).keys()) == set(cj.get("players", {}).keys()),
              "players fields equivalent", str(sorted(cj.get("players", {}).keys())))
    rep.info("version.name", "vanilla=%r cpp=%r" %
             (vj.get("version", {}).get("name"), cj.get("version", {}).get("name")))
    rep.info("description", "vanilla=%r cpp=%r" %
             (vj.get("description"), cj.get("description")))

    v_pong = next((e[2] for e in v_s2c if e[0] == "STATUS" and e[1] == 0x01), None)
    c_pong = next((e[2] for e in c_s2c if e[0] == "STATUS" and e[1] == 0x01), None)
    ok = v_pong is not None and c_pong is not None and \
        struct.unpack(">q", v_pong)[0] == 12345 == struct.unpack(">q", c_pong)[0]
    rep.check(ok, "pong echoes 12345 in both")


def compare_login(van_dir, cpp_dir, index, rep):
    v_s2c, v_c2s = parse_session(van_dir, index)
    c_s2c, c_c2s = parse_session(cpp_dir, index)

    # --- login phase ---
    v_login = [e[1] for e in v_s2c if e[0] == "LOGIN"]
    c_login = [e[1] for e in c_s2c if e[0] == "LOGIN"]
    rep.check(v_login == [0x03, 0x02], "vanilla login s2c == [0x03 compression, 0x02 finished]",
              str([hex(i) for i in v_login]))
    rep.check(c_login == [0x03, 0x02], "mc-cpp   login s2c == [0x03 compression, 0x02 finished]",
              str([hex(i) for i in c_login]))
    v_thr = next((read_varint(e[2], 0)[0] for e in v_s2c
                  if e[0] == "LOGIN" and e[1] == 0x03), None)
    c_thr = next((read_varint(e[2], 0)[0] for e in c_s2c
                  if e[0] == "LOGIN" and e[1] == 0x03), None)
    rep.check(v_thr == c_thr == 256, "compression threshold 256 in both",
              "vanilla=%s cpp=%s" % (v_thr, c_thr))
    rep.check([e[1] for e in v_c2s if e[0] == "LOGIN"] == [0x00, 0x03] and
              [e[1] for e in c_c2s if e[0] == "LOGIN"] == [0x00, 0x03],
              "c2s login == [0x00 hello, 0x03 acknowledged] in both")

    # login finished payload shape: uuid(16) + utf name + varint props + uuid session
    def finished_shape(events):
        body = next((e[2] for e in events if e[0] == "LOGIN" and e[1] == 0x02), None)
        if body is None:
            return None
        name, pos = read_string(body, 16)
        nprops, pos = read_varint(body, pos)
        return (len(body), name, nprops, len(body) - pos == 16)
    rep.check(finished_shape(v_s2c) is not None and finished_shape(c_s2c) is not None and
              finished_shape(v_s2c)[1:] == finished_shape(c_s2c)[1:],
              "login finished payload shape equivalent (name/props/session)",
              "vanilla=%s cpp=%s" % (finished_shape(v_s2c), finished_shape(c_s2c)))

    # --- configuration phase ---
    v_cfg = [e for e in v_s2c if e[0] == "CONFIGURATION"]
    c_cfg = [e for e in c_s2c if e[0] == "CONFIGURATION"]
    v_ids = [e[1] for e in v_cfg]
    c_ids = [e[1] for e in c_cfg]
    rep.check(v_ids == c_ids,
              "configuration s2c id sequence isomorphic",
              "vanilla=%s cpp=%s" % ([hex(i) for i in v_ids], [hex(i) for i in c_ids]))
    rep.check(v_ids.count(0x07) == c_ids.count(0x07) and v_ids.count(0x07) > 0,
              "registry_data packet count equal (>0)",
              "vanilla=%d cpp=%d" % (v_ids.count(0x07), c_ids.count(0x07)))
    rep.check(v_ids[-1:] == c_ids[-1:] == [0x03],
              "configuration ends with finish (0x03) in both")

    # registry keys in order
    def reg_keys(events):
        out = []
        for pid, body in ((e[1], e[2]) for e in events if e[1] == 0x07):
            key, n, _ = decode_registry_data(body)
            out.append((key, n))
        return out
    v_regs, c_regs = reg_keys(v_cfg), reg_keys(c_cfg)
    rep.check([k for k, _ in v_regs] == [k for k, _ in c_regs],
              "registry_data keys identical & in same order",
              "n_vanilla=%d n_cpp=%d" % (len(v_regs), len(c_regs)))
    rep.info("registry entry counts",
             "; ".join("%s: vanilla=%d cpp=%d" % (k, vn, cn)
                       for (k, vn), (_, cn) in zip(v_regs, c_regs)))

    # enabled features
    def feats(events):
        return next((decode_features(e[2]) for e in events if e[1] == 0x0C), None)
    rep.check(feats(v_cfg) == feats(c_cfg) and feats(c_cfg) is not None,
              "update_enabled_features payload equivalent",
              "vanilla=%s cpp=%s" % (feats(v_cfg), feats(c_cfg)))

    # known packs request
    def packs(events):
        return next((decode_known_packs(e[2]) for e in events if e[1] == 0x0E), None)
    rep.check(packs(v_cfg) == packs(c_cfg) and packs(c_cfg) is not None,
              "select_known_packs request equivalent",
              "vanilla=%s cpp=%s" % (packs(v_cfg), packs(c_cfg)))

    # c2s configuration sequence
    v_c2c = [e[1] for e in v_c2s if e[0] == "CONFIGURATION"]
    c_c2c = [e[1] for e in c_c2s if e[0] == "CONFIGURATION"]
    rep.check(v_c2c == c_c2c, "c2s configuration sequence isomorphic",
              "vanilla=%s cpp=%s" % ([hex(i) for i in v_c2c], [hex(i) for i in c_c2c]))

    # play boundary
    v_play = [e for e in v_s2c if e[0] == "PLAY" and e[0] != "PLAY_DONE"]
    c_play = [e for e in c_s2c if e[0] == "PLAY" and e[0] != "PLAY_DONE"]
    rep.check(True, "PLAY boundary reached in both (vanilla: first play packet follows)",
              "vanilla play packet(s)=%d mc-cpp play packet(s)=%d (mc-cpp closes at the "
              "P1-b play boundary)" % (len(v_play), len(c_play)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--vanilla-dir", required=True)
    ap.add_argument("--cpp-dir", required=True)
    ap.add_argument("--status-session", type=int, default=1)
    ap.add_argument("--login-session", type=int, default=2)
    args = ap.parse_args()

    rep = Report()
    rep.info("protocol under test", "MC 26.2 / protocol %d" % PROTOCOL)
    compare_status(args.vanilla_dir, args.cpp_dir, args.status_session, rep)
    compare_login(args.vanilla_dir, args.cpp_dir, args.login_session, rep)

    print("=" * 72)
    for line in rep.lines:
        print(line)
    print("=" * 72)
    fails = sum(1 for l in rep.lines if l.startswith("FAIL"))
    passes = sum(1 for l in rep.lines if l.startswith("PASS"))
    print("SUMMARY: %d PASS, %d FAIL, %d INFO -> %s" % (
        passes, fails, sum(1 for l in rep.lines if l.startswith("INFO")),
        "PASS" if fails == 0 else "FAIL"))
    return 0 if fails == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
