#!/usr/bin/env python3
"""A CONNECT-only proxy on 127.0.0.1, for running the phone gates while the Q10 has no network
of its own. The phone reaches it through `ssh -R <port>:127.0.0.1:<port> bb10rootx`; TLS stays
end to end between the phone and the server, the proxy only resolves the name and copies bytes.

    python3 tools/gates/connect_proxy.py 8899
"""
import select, socket, sys, threading


def tunnel(client):
    try:
        head = b""
        while b"\r\n\r\n" not in head:
            chunk = client.recv(4096)
            if not chunk:
                return
            head += chunk
        method, target = head.split(b"\r\n", 1)[0].split()[:2]
        if method != b"CONNECT":
            client.sendall(b"HTTP/1.1 405 CONNECT only\r\n\r\n")
            return
        host, port = target.decode().rsplit(":", 1)
        upstream = socket.create_connection((host, int(port)), timeout=20)
        client.sendall(b"HTTP/1.1 200 Connection established\r\n\r\n")
        print("CONNECT %s:%s" % (host, port), flush=True)
        pair = {client: upstream, upstream: client}
        while True:
            ready, _, _ = select.select(list(pair), [], [], 120)
            if not ready:
                return
            for s in ready:
                data = s.recv(65536)
                if not data:
                    return
                pair[s].sendall(data)
    except OSError as e:
        print("tunnel: %s" % e, flush=True)
    finally:
        client.close()


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8899
    server = socket.socket()
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("127.0.0.1", port))
    server.listen(16)
    print("CONNECT proxy on 127.0.0.1:%d" % port, flush=True)
    while True:
        client, _ = server.accept()
        threading.Thread(target=tunnel, args=(client,), daemon=True).start()


if __name__ == "__main__":
    main()
