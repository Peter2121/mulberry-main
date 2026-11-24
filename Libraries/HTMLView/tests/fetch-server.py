#!/usr/bin/env python3
#
# Local HTTP server for the remote image tests (fetch-tests.sh): one route
# per case, and a log of the headers each request brought.
#
# Usage: fetch-server.py IMAGEDIR LOGFILE PORTFILE
#
# Listens on 127.0.0.1 on a free port, written to PORTFILE when ready.

import gzip
import http.server
import json
import os
import sys
import time

IMAGES, LOGFILE, PORTFILE = sys.argv[1], sys.argv[2], sys.argv[3]
TYPES = {'.png': 'image/png', '.jpg': 'image/jpeg', '.gif': 'image/gif', '.webp': 'image/webp'}
BIG = 17 * 1024 * 1024		# over the 16 MB limit per image


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, *args):
        pass

    def record(self):
        with open(LOGFILE, 'a') as log:
            log.write(json.dumps({'path': self.path, 'headers': dict(self.headers)}) + '\n')

    def send(self, code, body=b'', ctype='application/octet-stream', extra=None, length=True):
        self.send_response(code)
        self.send_header('Content-Type', ctype)
        for name, value in (extra or {}).items():
            self.send_header(name, value)
        if length:
            self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def image(self, name):
        path = os.path.join(IMAGES, os.path.basename(name))
        with open(path, 'rb') as f:
            return f.read(), TYPES.get(os.path.splitext(path)[1], 'application/octet-stream')

    def do_GET(self):
        self.record()
        parts = self.path.split('?')[0].split('/')
        try:
            if parts[1] == 'img':
                # A tracking cookie: must never come back
                body, ctype = self.image(parts[2])
                self.send(200, body, ctype, {'Set-Cookie': 'tracker=123; Path=/'})
            elif parts[1] == 'redirect':
                left = int(parts[2])
                target = ('/redirect/%d/%s' % (left - 1, parts[3])) if left > 1 else ('/img/' + parts[3])
                self.send(302, extra={'Location': target})
            elif parts[1] == 'to-file':
                self.send(302, extra={'Location': 'file:///etc/passwd'})
            elif parts[1] == 'to-ftp':
                self.send(302, extra={'Location': 'ftp://127.0.0.1/image.png'})
            elif parts[1] == 'big':
                # Announced length over the limit
                self.send_response(200)
                self.send_header('Content-Type', 'image/png')
                self.send_header('Content-Length', str(BIG))
                self.end_headers()
                try:
                    self.wfile.write(b'\0' * BIG)
                except OSError:
                    pass
            elif parts[1] == 'big-unannounced':
                # No length: the limit must stop it while downloading
                self.send_response(200)
                self.send_header('Content-Type', 'image/png')
                self.send_header('Connection', 'close')
                self.end_headers()
                try:
                    for _ in range(BIG // 65536 + 1):
                        self.wfile.write(b'\0' * 65536)
                except OSError:
                    pass
                self.close_connection = True
            elif parts[1] == 'slow':
                time.sleep(6)
                body, ctype = self.image('logo.png')
                self.send(200, body, ctype)
            elif parts[1] == 'auth':
                self.send(401, b'login', 'text/plain', {'WWW-Authenticate': 'Basic realm="test"'})
            elif parts[1] == 'gzip':
                body, ctype = self.image(parts[2])
                self.send(200, gzip.compress(body), ctype, {'Content-Encoding': 'gzip'})
            elif parts[1] == 'html':
                self.send(200, b'<html><body>not an image</body></html>', 'text/html')
            else:
                self.send(404, b'not found', 'text/plain')
        except (BrokenPipeError, ConnectionResetError):
            pass
        except Exception as error:
            # A bug of this server: visible as a 500, not as an empty reply
            self.send(500, str(error).encode(), 'text/plain')


class Server(http.server.ThreadingHTTPServer):
    def handle_error(self, request, client_address):
        # Downloads stopped by the client (size and time limits): expected
        pass


server = Server(('127.0.0.1', 0), Handler)
with open(PORTFILE + '.tmp', 'w') as f:
    f.write(str(server.server_address[1]))
os.rename(PORTFILE + '.tmp', PORTFILE)
server.serve_forever()
