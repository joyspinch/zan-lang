"""Local WS/WSS client parity. Compile the fixture under the default build gate first.

python -I tests/integration/ws_client_session_test.py --exe _scratch/ws-client-session-fixture.exe
Uses ephemeral loopback listeners and temporary explicit CAs; never modifies system trust.
"""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import shutil
import socket
import ssl
import struct
import subprocess
import tempfile
import threading
import time


def exact(peer, size):
    data = bytearray()
    while len(data) < size:
        part = peer.recv(size - len(data))
        if not part:
            raise EOFError(f"frame truncated at {len(data)}/{size}")
        data.extend(part)
    return bytes(data)


def receive(peer):
    first, second = exact(peer, 2)
    assert first & 128 and not first & 112, "invalid client FIN/RSV"
    assert second & 128, "client frame was not masked"
    size = second & 127
    if size == 126:
        size = struct.unpack("!H", exact(peer, 2))[0]
        assert size >= 126
    elif size == 127:
        size = struct.unpack("!Q", exact(peer, 8))[0]
        assert size > 65535
    assert size <= 16777216
    key = exact(peer, 4)
    payload = exact(peer, size)
    return first & 15, bytes(v ^ key[i % 4] for i, v in enumerate(payload))


def frame(opcode, payload=b"", fin=True):
    size = len(payload)
    header = bytes([(128 if fin else 0) | opcode])
    if size < 126:
        header += bytes([size])
    elif size <= 65535:
        header += b"\x7e" + struct.pack("!H", size)
    else:
        header += b"\x7f" + struct.pack("!Q", size)
    return header + payload


def certificates(directory):
    openssl = shutil.which("openssl")
    if not openssl:
        raise RuntimeError("SKIP: openssl unavailable")
    def run(*arguments):
        subprocess.run([openssl, *map(str, arguments)], cwd=directory,
                       capture_output=True, check=True, timeout=30)
    run("req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", "ca.key",
        "-out", "ca.pem", "-days", "1", "-subj", "/CN=Zan WS local CA",
        "-addext", "basicConstraints=critical,CA:TRUE")
    run("req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", "wrong.key",
        "-out", "wrong.pem", "-days", "1", "-subj", "/CN=Zan WS wrong CA",
        "-addext", "basicConstraints=critical,CA:TRUE")
    for name, host in (("leaf", "localhost"), ("badhost", "wrong.invalid")):
        (directory / f"{name}.ext").write_text(
            f"basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature,keyEncipherment\n"
            f"extendedKeyUsage=serverAuth\nsubjectAltName=DNS:{host}\n", encoding="ascii")
        run("req", "-newkey", "rsa:2048", "-nodes", "-keyout", f"{name}.key",
            "-out", f"{name}.csr", "-subj", f"/CN={host}")
        run("x509", "-req", "-in", f"{name}.csr", "-CA", "ca.pem", "-CAkey", "ca.key",
            "-CAcreateserial", "-out", f"{name}.pem", "-days", "1", "-extfile", f"{name}.ext")


def serve(listener, context, case, errors):
    try:
        peer, _ = listener.accept()
        with peer:
            peer.settimeout(15)
            if context:
                try:
                    peer = context.wrap_socket(peer, server_side=True)
                except (ssl.SSLError, ConnectionError, EOFError):
                    if case in ("wrong_ca", "wrong_host"):
                        return
                    raise
            with peer:
                if case in ("wrong_ca", "wrong_host"):
                    try:
                        assert not peer.recv(1), "untrusted connection sent application data"
                    except (ssl.SSLError, ConnectionError):
                        pass
                    return
                request = bytearray()
                while not request.endswith(b"\r\n\r\n"):
                    request.extend(exact(peer, 1))
                    assert len(request) <= 65536
                headers = dict(line.split(b":", 1) for line in bytes(request).split(b"\r\n")[1:] if b":" in line)
                nonce = headers[b"Sec-WebSocket-Key"].strip()
                assert len(base64.b64decode(nonce)) == 16
                accept = base64.b64encode(hashlib.sha1(nonce + b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11").digest())
                response = b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + accept + b"\r\n\r\n"
                if case == "protocol":
                    # Masked server frame must be rejected in both transports.
                    peer.sendall(response + b"\x81\x81\x01\x02\x03\x04\x79")
                    assert not peer.recv(1)
                    return
                if case == "close":
                    peer.sendall(response + frame(8, b"\x03\xe8"))
                    assert receive(peer) == (8, b"\x03\xe8")
                    return
                if case == "wake":
                    peer.sendall(response)
                    time.sleep(2.5)
                    return
                # Upgrade and partial binary frame share a write. timeout must
                # preserve both this header and subsequent fragmented state.
                binary = frame(2, b"a", False)
                ping = frame(9, b"\x00p")
                if case == "timeout":
                    peer.sendall(response + binary + ping[:1])
                    time.sleep(0.2)
                    peer.sendall(ping[1:])
                else:
                    peer.sendall(response + binary[:1])
                    peer.sendall(binary[1:] + ping)
                peer.sendall(frame(0, b"\x00b") + frame(1, b"next"))
                assert receive(peer) == (10, b"\x00p"), "Pong payload/mask mismatch"
                seen = set()
                for i in range(8):
                    opcode, payload = receive(peer)
                    assert opcode == 2 and 65536 <= len(payload) < 65544 and len(payload) not in seen, "concurrent frame length/uniqueness"
                    seen.add(len(payload))
                    assert payload == bytes(j % 251 for j in range(len(payload))), "concurrent frame interleaved"
                    peer.sendall(frame(2, payload))
                assert receive(peer) == (8, b""), "client close missing/misframed"
    except BaseException as error:
        errors.append(repr(error))
    finally:
        listener.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--output", type=Path, default=Path("build/test-out/ws-client-session"))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    results = []
    with tempfile.TemporaryDirectory(prefix="local-ca-", dir=args.output.resolve()) as temporary:
        directory = Path(temporary)
        certificates(directory)
        for mode in ("ws", "wss12", "wss13"):
            cases = ["parity", "protocol", "close", "wake"]
            cases += ["timeout"] if mode == "ws" else ["wrong_ca", "wrong_host"]
            for case in cases:
                listener = socket.socket()
                listener.bind(("127.0.0.1", 0))
                listener.listen(1)
                listener.settimeout(20)
                port = listener.getsockname()[1]
                context = None
                if mode != "ws":
                    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
                    version = ssl.TLSVersion.TLSv1_2 if mode == "wss12" else ssl.TLSVersion.TLSv1_3
                    context.minimum_version = context.maximum_version = version
                    name = "badhost" if case == "wrong_host" else "leaf"
                    context.load_cert_chain(directory / f"{name}.pem", directory / f"{name}.key")
                errors = []
                thread = threading.Thread(target=serve, args=(listener, context, case, errors), daemon=True)
                thread.start()
                environment = dict(os.environ, ZAN_WS_MODE=mode, ZAN_WS_CASE=case,
                                   ZAN_WS_PORT=str(port), ZAN_WS_CA=str(directory / ("wrong.pem" if case == "wrong_ca" else "ca.pem")),
                                   ZAN_RT_HARD="1")
                name = f"{mode}_{case}"
                try:
                    process = subprocess.run([str(args.exe.resolve())], env=environment,
                                             capture_output=True, text=True, timeout=25)
                    text = process.stdout + process.stderr
                    (args.output / f"{name}.log").write_text(text, encoding="utf-8")
                    thread.join(3)
                    passed = (process.returncode == 0 and process.stdout.strip() == "ws_client_fixture=ok"
                              and not process.stderr.strip() and not thread.is_alive() and not errors)
                    result = dict(case=name, passed=passed, exit=process.returncode, server_errors=errors,
                                  cleanup=not thread.is_alive())
                except subprocess.TimeoutExpired:
                    result = dict(case=name, passed=False, error="client timeout")
                finally:
                    listener.close()
                    thread.join(1)
                results.append(result)
                print(json.dumps(result), flush=True)
    (args.output / "results.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    return 0 if all(result["passed"] for result in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
