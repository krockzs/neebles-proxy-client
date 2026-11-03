#!/usr/bin/env python3
"""Integration probe for Proxy Client Config's native Module IPC session.

Run only against a freshly compiled Qt executable in a controlled Esbirro
runtime. This test supplies a mock framed transport, not Boss authorization.
"""
import argparse
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import tempfile


def receive_exact(sock, length):
    data = bytearray()
    while len(data) < length:
        part = sock.recv(length - len(data))
        if not part:
            raise RuntimeError("module IPC connection closed")
        data.extend(part)
    return bytes(data)


def read_frame(sock):
    length = struct.unpack(">I", receive_exact(sock, 4))[0]
    assert 0 < length <= 1024 * 1024
    return json.loads(receive_exact(sock, length))


def write_frame(sock, message):
    payload = json.dumps(message, separators=(",", ":")).encode()
    sock.sendall(struct.pack(">I", len(payload)) + payload)


def run(binary, reject=False):
    with tempfile.TemporaryDirectory(prefix="neebles-client-ipc-") as root:
        path = str(Path(root) / "modules.sock")
        listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        listener.settimeout(15)
        listener.bind(path)
        listener.listen(1)
        env = dict(os.environ)
        env["NEEBLES_MODULES_SOCKET"] = path
        env["QT_QPA_PLATFORM"] = "offscreen"
        # Execute this probe only inside an isolated Esbirro runtime without
        # the actual user Secrets socket mounted.
        proc = subprocess.Popen([str(binary), "--config"], env=env,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            with listener.accept()[0] as conn:
                conn.settimeout(15)
                registered = read_frame(conn)
                assert registered["type"] == "register", registered
                assert registered["protocol"] == 1
                assert registered["module"] == "neebles-proxy-client"
                assert registered["endpoints"] == {}
                session = registered["session_id"]
                assert session
                if reject:
                    write_frame(conn, {"type": "error", "module": registered["module"],
                                       "error": {"kind": "register_rejected", "message": "fixture rejection"}})
                    proc.wait(timeout=15)
                    assert proc.returncode == 7, proc.returncode
                    return
                identity = {"module": registered["module"], "session_id": session}
                write_frame(conn, {"type": "registered", "protocol": 1, **identity})
                write_frame(conn, {"type": "ping", **identity})
                pong = read_frame(conn)
                assert pong == {"type": "pong", **identity}, pong
                write_frame(conn, {"type": "shutdown", "reason": "fixture", **identity})
                ack = read_frame(conn)
                assert ack == {"type": "shutdown_ack", **identity}, ack
            proc.wait(timeout=15)
            assert proc.returncode == 0, proc.returncode
        finally:
            if proc.poll() is None:
                proc.kill()
            proc.communicate(timeout=5)
            listener.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve()
    if not binary.is_file():
        raise SystemExit("compiled client ELF missing")
    run(binary, reject=True)
    run(binary, reject=False)
    print("PASS: Client Config Module IPC register/reject/ping/shutdown")
