#!/usr/bin/env python3
#
# Mock OAuth2 token endpoint for the token client tests (token-tests.sh):
# one route per case, and a log of each request (path, headers, form).
#
# Usage: token-server.py LOGFILE PORTFILE [CERTFILE KEYFILE]
#
# Listens on 127.0.0.1 on a free port; with a certificate, also on a second
# port with TLS.  PORTFILE gets "PORT" or "PORT TLSPORT" when ready.
#
# Every secret it issues or expects contains "S3CR3T", so that the tests can
# check that none reaches the client's log or output.

import http.server
import json
import os
import ssl
import sys
import threading
import time
import urllib.parse

LOGFILE, PORTFILE = sys.argv[1], sys.argv[2]
CERT = sys.argv[3] if len(sys.argv) > 4 else None
KEY = sys.argv[4] if len(sys.argv) > 4 else None

CLIENT_ID = 'test-client'
CLIENT_SECRET = 'S3CR3T-client-secret'
CODE = 'S3CR3T-code-1'
REDIRECT = 'http://127.0.0.1/callback'

lock = threading.Lock()
codes_used = set()
# Refresh tokens: those that rotate on use, those that do not, those
# already rotated out
rotating = {'S3CR3T-refresh-1'}
stable = set()
rotated_out = set()
counter = [1]


def tokens(refresh=None, scope='openid email https://mail.example/'):
    counter[0] += 1
    answer = {
        'access_token': 'S3CR3T-access-%d' % counter[0],
        'token_type': 'Bearer',
        'expires_in': 3599,
        'scope': scope,
        'id_token': 'S3CR3T.idtoken.%d' % counter[0],
    }
    if refresh:
        answer['refresh_token'] = refresh
    return answer


GOOGLE_RAPT = {
    'error': 'invalid_grant',
    'error_description': 'reauth related error (invalid_rapt)',
    'error_uri': 'https://support.google.com/a/answer/9368756',
    'error_subtype': 'invalid_rapt',
}

MS_INVALID_CLIENT = {
    'error': 'invalid_client',
    'error_description': 'AADSTS7000215: Invalid client secret provided. Ensure the secret being '
                         'sent in the request is the client secret value, not the client secret '
                         'ID.\r\nTrace ID: 0000aaaa-11bb-cccc-dd22-eeeeee333333\r\nCorrelation '
                         'ID: aaaa0000-bb11-2222-33cc-444444dddddd\r\nTimestamp: 2026-11-26 10:00:00Z',
    'error_codes': [7000215],
    'timestamp': '2026-11-26 10:00:00Z',
    'trace_id': '0000aaaa-11bb-cccc-dd22-eeeeee333333',
    'correlation_id': 'aaaa0000-bb11-2222-33cc-444444dddddd',
    'error_uri': 'https://login.microsoftonline.com/error?code=7000215',
}

MS_CONSENT = {
    'error': 'invalid_grant',
    'error_description': 'AADSTS65001: The user or administrator has not consented to use the '
                         'application with ID \'test-client\'.\r\nTrace ID: x\r\nCorrelation ID: y',
    'error_codes': [65001],
    'suberror': 'consent_required',
    'correlation_id': 'aaaa0000-bb11-2222-33cc-555555eeeeee',
}

# Non-standard error object (reported for Yahoo, unverified)
OBJECT_ERROR = {
    'error': {
        'localizedMessage': 'client request is not acceptable or not supported',
        'errorId': 'INVALID_INPUT',
        'message': 'client request is not acceptable or not supported',
    }
}


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, *args):
        pass

    def send(self, code, body, ctype='application/json', extra=None):
        if isinstance(body, (dict, list)):
            body = json.dumps(body).encode()
        elif isinstance(body, str):
            body = body.encode()
        self.send_response(code)
        self.send_header('Content-Type', ctype)
        self.send_header('Cache-Control', 'no-store')
        for name, value in (extra or {}).items():
            self.send_header(name, value)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def error(self, code, error, description=''):
        answer = {'error': error}
        if description:
            answer['error_description'] = description
        self.send(code, answer)

    def do_GET(self):
        self.send(405, {'error': 'invalid_request', 'error_description': 'POST only'})

    def do_POST(self):
        length = int(self.headers.get('Content-Length', '0'))
        raw = self.rfile.read(length).decode('utf-8', 'replace')
        form = dict(urllib.parse.parse_qsl(raw, keep_blank_values=True))
        with lock:
            with open(LOGFILE, 'a') as log:
                log.write(json.dumps({'path': self.path, 'headers': dict(self.headers),
                                      'form': form}) + '\n')
        route = self.path.split('?')[0]
        try:
            if route == '/token':
                self.token(form)
            elif route == '/google-rapt':
                self.send(400, GOOGLE_RAPT)
            elif route == '/ms-invalid-client':
                self.send(401, MS_INVALID_CLIENT)
            elif route == '/ms-consent':
                self.send(400, MS_CONSENT)
            elif route == '/object-error':
                self.send(400, OBJECT_ERROR)
            elif route == '/http-500':
                self.send(500, '<html><body>Internal error S3CR3T-in-html</body></html>', 'text/html')
            elif route == '/malformed':
                self.send(200, '{"access_token": "S3CR3T-access-malformed", "token_type": "Bearer",')
            elif route == '/not-object':
                self.send(200, '["S3CR3T-access-array"]')
            elif route == '/no-access-token':
                self.send(200, {'token_type': 'Bearer', 'expires_in': 3600})
            elif route == '/mac-token':
                self.send(200, {'access_token': 'S3CR3T-access-mac', 'token_type': 'mac'})
            elif route == '/string-expires':
                answer = tokens()
                answer['expires_in'] = '3599'
                answer['refresh_token_expires_in'] = 604799
                del answer['scope']
                self.send(200, answer)
            elif route == '/bad-expires':
                answer = tokens()
                answer['expires_in'] = 'soon'
                self.send(200, answer)
            elif route == '/slow':
                time.sleep(4)
                self.send(200, tokens())
            elif route in ('/redirect-307', '/redirect-308', '/redirect-302'):
                self.send(int(route[-3:]), '', 'text/plain', {'Location': '/token'})
            elif route == '/redirect-localhost':
                target = 'http://localhost:%d/token' % self.server.server_address[1]
                self.send(307, '', 'text/plain', {'Location': target})
            elif route == '/redirect-loop':
                self.send(307, '', 'text/plain', {'Location': '/redirect-loop'})
            elif route == '/redirect-none':
                self.send(307, '', 'text/plain')
            elif route == '/big':
                filler = 'x' * (2 * 1024 * 1024)
                self.send(200, {'access_token': 'S3CR3T-access-big', 'token_type': 'Bearer',
                                'filler': filler})
            else:
                self.send(404, 'not found', 'text/plain')
        except (BrokenPipeError, ConnectionResetError):
            pass

    def token(self, form):
        if self.headers.get('Content-Type', '') != 'application/x-www-form-urlencoded':
            return self.error(400, 'invalid_request', 'form expected')
        if form.get('client_id') != CLIENT_ID:
            return self.error(401, 'invalid_client', 'unknown client')
        if 'client_secret' in form and form['client_secret'] != CLIENT_SECRET:
            return self.error(401, 'invalid_client', 'wrong client secret')
        cookie = {'Set-Cookie': 'session=S3CR3T-cookie; Path=/; HttpOnly'}
        grant = form.get('grant_type')
        with lock:
            if grant == 'authorization_code':
                verifier = form.get('code_verifier', '')
                if form.get('code') != CODE or CODE in codes_used:
                    return self.error(400, 'invalid_grant', 'code unknown or already used')
                if form.get('redirect_uri') != REDIRECT:
                    return self.error(400, 'invalid_grant', 'redirect_uri mismatch')
                if not 43 <= len(verifier) <= 128:
                    return self.error(400, 'invalid_grant', 'code_verifier missing or invalid')
                codes_used.add(CODE)
                return self.send(200, tokens('S3CR3T-refresh-1'), extra=cookie)
            if grant == 'refresh_token':
                token = form.get('refresh_token', '')
                if token in rotating:
                    # Rotation: a new refresh token, the old one is dead
                    rotating.discard(token)
                    rotated_out.add(token)
                    new = 'S3CR3T-refresh-2'
                    stable.add(new)
                    scope = form.get('scope', 'openid email https://mail.example/')
                    return self.send(200, tokens(new, scope), extra=cookie)
                if token in stable:
                    # No refresh_token in the answer: the client keeps its own
                    return self.send(200, tokens(), extra=cookie)
                if token in rotated_out:
                    return self.error(400, 'invalid_grant', 'refresh token already rotated')
                return self.error(400, 'invalid_grant', 'unknown refresh token')
        return self.error(400, 'unsupported_grant_type')


class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True

    def handle_error(self, request, client_address):
        # Clients that give up (time limit, size limit): expected
        pass


plain = Server(('127.0.0.1', 0), Handler)
ports = [str(plain.server_address[1])]
if CERT:
    secure = Server(('127.0.0.1', 0), Handler)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(CERT, KEY)
    secure.socket = context.wrap_socket(secure.socket, server_side=True)
    ports.append(str(secure.server_address[1]))
    threading.Thread(target=secure.serve_forever, daemon=True).start()

with open(PORTFILE + '.tmp', 'w') as f:
    f.write(' '.join(ports))
os.rename(PORTFILE + '.tmp', PORTFILE)
plain.serve_forever()
