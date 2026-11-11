#!/usr/bin/env python3
"""Read-only download of a file from a Makera Z1 / Z1 Pro over WiFi (ticket #38).

Used to read the machine configuration (e.g. /sd/config.txt, /sd/config.default) and compare it with the
values the handwheel firmware relies on (SPEC 8.7, 8.8). The tool only reads: the machine is never sent a
console line, G-code, realtime character or upload.

Protocol (TCP port 2222, frame format SPEC 8.1: 0x86 0x68 | len | type | payload | crc16 | 0x55 0xAA), as in
MakeraInc/CarveraController, carveracontroller/XMODEM.py (XMODEM.recv) and Controller.download():

    host -> machine  0xB0  "download <path>\\n", blanks in the path replaced by 0x01
    machine -> host  0xB1  MD5 of the file (hex text)
    host -> machine  0xB2  empty: request the overview
    machine -> host  0xB2  packet count (4 bytes, big-endian), packet size (2 bytes)
    host -> machine  0xB3  sequence number (4 bytes, big-endian, starting at 1): request this packet
    machine -> host  0xB3  sequence number (4 bytes) + data
    host -> machine  0xB4  empty: transfer finished
    machine -> host  0xB5  transfer cancelled by the machine

Frames of other types (status 0x81, text 0x90, ...) are skipped; they arrive because the WiFi module mirrors
all output to every client. The only frames this tool sends are 0xB0 (once) and 0xB2, 0xB3, 0xB4.

Usage: python3 -I tools/z1_config_download.py <machine-ip> /sd/config.txt config.txt [--port 2222]
Then e.g.: grep -E '^(alpha|beta|gamma|delta)_max_rate' config.txt
The downloaded file is machine data and does not belong in the repository.
"""
import argparse
import hashlib
import socket
import sys
import time

TYPE_DOWNLOAD = 0xB0
TYPE_MD5 = 0xB1
TYPE_OVERVIEW = 0xB2
TYPE_DATA = 0xB3
TYPE_END = 0xB4
TYPE_CANCEL = 0xB5


def crc16(data):
    """CRC-16/CCITT-XModem (poly 0x1021, init 0), as SPEC 8.1."""
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def build_frame(ftype, payload):
    if ftype not in (TYPE_DOWNLOAD, TYPE_OVERVIEW, TYPE_DATA, TYPE_END):
        raise ValueError("read-only tool: frame type 0x%02X is never sent" % ftype)
    length = len(payload) + 3
    body = bytes([length >> 8, length & 0xFF, ftype]) + payload
    crc = crc16(body)
    return b"\x86\x68" + body + bytes([crc >> 8, crc & 0xFF]) + b"\x55\xaa"


class Reader:
    """Splits the TCP stream into frames (type, payload); drops frames with a bad CRC or footer."""

    def __init__(self, sock, timeout_s):
        self.sock = sock
        self.timeout_s = timeout_s
        self.buf = bytearray()

    def next(self):
        deadline = time.monotonic() + self.timeout_s
        while time.monotonic() < deadline:
            j = self.buf.find(b"\x86\x68")
            if j > 0:
                del self.buf[:j]
                j = 0
            if j == 0 and len(self.buf) >= 4:
                length = (self.buf[2] << 8) | self.buf[3]
                total = 4 + length + 2  # header, len bytes cover type + payload + crc, footer
                if length < 3:
                    del self.buf[:2]
                    continue
                if len(self.buf) >= total:
                    body = bytes(self.buf[2 : 4 + length - 2])
                    crc = (self.buf[4 + length - 2] << 8) | self.buf[4 + length - 1]
                    footer = bytes(self.buf[4 + length : total])
                    if crc16(body) != crc or footer != b"\x55\xaa":
                        del self.buf[:2]
                        continue
                    del self.buf[:total]
                    return body[2], body[3:]
            try:
                self.sock.settimeout(max(0.05, deadline - time.monotonic()))
                data = self.sock.recv(65536)
            except socket.timeout:
                break
            if not data:
                raise SystemExit("machine closed the connection")
            self.buf += data
        raise SystemExit("timeout waiting for the machine")


def download(host, port, remote, timeout_s):
    sock = socket.create_connection((host, port), timeout=timeout_s)
    try:
        reader = Reader(sock, timeout_s)
        sock.sendall(build_frame(TYPE_DOWNLOAD, ("download " + remote.replace(" ", "\x01") + "\n").encode()))
        md5 = None
        total = None
        seq = 0
        data = bytearray()
        while True:
            ftype, payload = reader.next()
            if ftype < TYPE_MD5 or ftype > TYPE_CANCEL:
                continue  # status, text and other output on the shared connection
            if ftype == TYPE_CANCEL:
                raise SystemExit("machine cancelled the transfer (file missing?)")
            if ftype == TYPE_MD5:
                md5 = payload.decode(errors="replace").strip()
                sock.sendall(build_frame(TYPE_OVERVIEW, b""))
            elif ftype == TYPE_OVERVIEW:
                total = int.from_bytes(payload[0:4], "big")
                size = int.from_bytes(payload[4:6], "big")
                print("packets %d, size %d" % (total, size), file=sys.stderr)
                if total == 0:
                    sock.sendall(build_frame(TYPE_END, b""))
                    return bytes(data), md5
                seq = 1
                sock.sendall(build_frame(TYPE_DATA, seq.to_bytes(4, "big")))
            elif ftype == TYPE_DATA and total is not None:
                if int.from_bytes(payload[0:4], "big") != seq:
                    sock.sendall(build_frame(TYPE_DATA, seq.to_bytes(4, "big")))  # ask again
                    continue
                data += payload[4:]
                if seq == total:
                    sock.sendall(build_frame(TYPE_END, b""))
                    return bytes(data), md5
                seq += 1
                sock.sendall(build_frame(TYPE_DATA, seq.to_bytes(4, "big")))
    finally:
        sock.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("host", help="machine IP")
    ap.add_argument("remote", help="file on the machine, e.g. /sd/config.txt")
    ap.add_argument("out", help="local output file")
    ap.add_argument("--port", type=int, default=2222)
    ap.add_argument("--timeout", type=float, default=5.0, help="seconds per frame")
    args = ap.parse_args()
    data, md5 = download(args.host, args.port, args.remote, args.timeout)
    if md5:
        got = hashlib.md5(data).hexdigest()
        state = "ok" if got == md5.lower() else "differs, local " + got + " (check the file)"
        print("md5 %s: %s" % (md5, state), file=sys.stderr)
    with open(args.out, "wb") as f:
        f.write(data)
    print("%d bytes -> %s" % (len(data), args.out), file=sys.stderr)


if __name__ == "__main__":
    main()
