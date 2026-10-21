"""paramiko SSH server used as the peer for the Zan SSH client interop test.

This is deliberately a **third-party** peer: the point of the interop test
is to catch bugs where both ends of the connection make the *same* mistake
(a wrong exchange-hash layout, a missing mpint length prefix, ...).
Self-consistency between two halves of the same package cannot catch those.

Usage:
    python paramiko_server.py <port> [hostkey.pem]

The server prints the OpenSSH-style SHA256 fingerprint of its host key on
stdout (first line), then serves exactly one connection.
"""
import base64
import hashlib
import os
import socket
import sys
import threading

import paramiko

PASSWORD = "s3cret"
STDOUT_TEXT = b"hello from the server\n"

if os.environ.get("PK_DEBUG"):
    paramiko.util.log_to_file(
        os.path.join(os.path.dirname(os.path.abspath(__file__)), "pk_debug.log"),
        level="DEBUG",
    )


def fingerprint(key):
    blob = key.asbytes()
    digest = hashlib.sha256(blob).digest()
    return "SHA256:" + base64.b64encode(digest).decode().rstrip("=")


class Server(paramiko.ServerInterface):
    def __init__(self):
        self.command = None

    def check_auth_password(self, username, password):
        if password == PASSWORD:
            return paramiko.AUTH_SUCCESSFUL
        return paramiko.AUTH_FAILED

    def check_auth_publickey(self, username, key):
        return paramiko.AUTH_SUCCESSFUL

    def get_allowed_auths(self, username):
        return "password,publickey"

    def check_channel_request(self, kind, chanid):
        if kind == "session":
            return paramiko.OPEN_SUCCEEDED
        return paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED

    def check_channel_exec_request(self, channel, command):
        cmd = command.decode() if isinstance(command, bytes) else command
        self.command = cmd
        channel.sendall(STDOUT_TEXT)
        channel.send_exit_status(7)
        # Do **not** close the channel here.  paramiko answers the request
        # (CHANNEL_SUCCESS) only *after* this callback returns, so closing
        # inside the callback means the success reply is never sent and the
        # client waits for the exec confirmation forever.
        return True


def serve_one(host, port, key):
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind((host, port))
    listener.listen(1)
    print("LISTENING %d" % port, flush=True)

    conn, addr = listener.accept()
    transport = paramiko.Transport(conn)
    transport.add_server_key(key)
    if os.environ.get("PK_DEBUG"):
        transport.packetizer._Packetizer__dump_packets = True
    server = Server()
    try:
        transport.start_server(server=server)
    except Exception as exc:  # handshake failure
        print("HANDSHAKE_FAILED %s" % exc, flush=True)
        listener.close()
        return
    print("NEGOTIATED cipher=%s" % transport.local_cipher, flush=True)

    chan = transport.accept(20)
    if chan is None:
        print("NO_CHANNEL", flush=True)
    else:
        chan.settimeout(10)
        try:
            while True:
                data = chan.recv(4096)
                if not data:
                    break
        except Exception:
            pass
        print("COMMAND %s" % server.command, flush=True)
    try:
        transport.close()
    except Exception:
        pass
    listener.close()


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 22222
    if len(sys.argv) > 2:
        key = paramiko.ECDSAKey(filename=sys.argv[2])
    else:
        key = paramiko.ECDSAKey.generate()
    print("FINGERPRINT %s" % fingerprint(key), flush=True)
    serve_one("127.0.0.1", port, key)


if __name__ == "__main__":
    main()
