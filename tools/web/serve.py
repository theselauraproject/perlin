import http.client
import http.server
import ipaddress
import os
import socketserver
import sys

root = os.path.dirname(os.path.abspath(__file__))
port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
os.chdir(root)


# Microsoft sign-in, Xbox Live, Realms and Minecraft services answer browsers without CORS headers: the page sends
# those requests to /__relay-https/<host>/<path> and this forwards them (the same thing tools/web/auth-relay does
# for a site on a static host)
RELAY_HOSTS = ("login.live.com", "account.xbox.com", ".xboxlive.com", ".minecraft.net", ".minecraft-services.net",
               ".playfabapi.com")
HOP_HEADERS = {"host", "origin", "referer", "cookie", "connection", "keep-alive", "transfer-encoding", "te",
               "upgrade", "proxy-authorization", "accept-encoding", "content-length"}


def relay_allowed(host):
    return any(host == h or (h.startswith(".") and (host.endswith(h) or host == h[1:])) for h in RELAY_HOSTS)


class Handler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map, ".wasm": "application/wasm", ".js": "text/javascript"}

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def _relay(self):
        parts = self.path.split("/", 4)
        if len(parts) < 5 or not parts[3].isdigit():
            self.send_error(400)
            return
        host, port, rest = parts[2], int(parts[3]), "/" + parts[4]
        try:
            ip = ipaddress.ip_address(__import__("socket").gethostbyname(host))
            if not (ip.is_loopback or ip.is_private):
                self.send_error(403, "relay only reaches local/private addresses")
                return
            length = int(self.headers.get("Content-Length") or 0)
            body = self.rfile.read(length) if length else None
            headers = {}
            if self.headers.get("Content-Type"):
                headers["Content-Type"] = self.headers["Content-Type"]
            conn = http.client.HTTPConnection(host, port, timeout=30)
            conn.request(self.command, rest, body, headers)
            r = conn.getresponse()
            data = r.read()
            self.send_response(r.status)
            self.send_header("Content-Type", r.getheader("Content-Type", "application/octet-stream"))
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            conn.close()
        except Exception as e:
            self.send_error(502, str(e))

    def _relay_https(self):
        rest = self.path[len("/__relay-https/"):]
        if rest == "ping":
            data = b"perlin-relay"
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return
        host, _, path = rest.partition("/")
        if not relay_allowed(host):
            self.send_error(403, "not a sign-in host")
            return
        try:
            length = int(self.headers.get("Content-Length") or 0)
            body = self.rfile.read(length) if length else None
            headers = {k: v for k, v in self.headers.items() if k.lower() not in HOP_HEADERS}
            headers["Accept-Encoding"] = "identity"
            conn = http.client.HTTPSConnection(host, 443, timeout=30)
            conn.request(self.command, "/" + path, body, headers)
            r = conn.getresponse()
            data = r.read()
            self.send_response(r.status)
            for k, v in r.getheaders():
                if k.lower() not in HOP_HEADERS and k.lower() not in ("content-encoding", "set-cookie"):
                    self.send_header(k, v)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            conn.close()
        except Exception as e:
            self.send_error(502, str(e))

    def do_POST(self):
        if self.path.startswith("/__relay/"):
            self._relay()
        elif self.path.startswith("/__relay-https/"):
            self._relay_https()
        else:
            self.send_error(501)

    def do_GET(self):
        if self.path.startswith("/__relay/"):
            self._relay()
        elif self.path.startswith("/__relay-https/"):
            self._relay_https()
        else:
            super().do_GET()

    def do_PUT(self):
        self._relay_https() if self.path.startswith("/__relay-https/") else self.send_error(501)

    def do_DELETE(self):
        self._relay_https() if self.path.startswith("/__relay-https/") else self.send_error(501)

    def log_message(self, *args):
        pass


socketserver.ThreadingTCPServer.allow_reuse_address = True
with socketserver.ThreadingTCPServer(("127.0.0.1", port), Handler) as server:
    print(f"Perlin web: http://localhost:{port}/")
    server.serve_forever()
