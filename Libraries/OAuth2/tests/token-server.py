#!/usr/bin/env python3
#
# Mock OAuth2 token endpoint for the token client tests (token-tests.sh)
# and the login tests of oauth2-tool (login-tests.sh): one route per case,
# and a log of each request (path, headers, form).
#
# Usage: token-server.py LOGFILE PORTFILE [CERTFILE KEYFILE]
#
# Listens on 127.0.0.1 on a free port; with a certificate, also on a second
# port with TLS.  PORTFILE gets "PORT" or "PORT TLSPORT" when ready.
#
# Every secret it issues or expects contains "S3CR3T", so that the tests can
# check that none reaches the client's log or output.

import base64
import http.server
import json
import os
import re
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
# Other codes of this form are accepted once each (login-tests.sh)
CODE_PREFIX = 'S3CR3T-code-'
REDIRECT = 'http://127.0.0.1/callback'
# The redirect with the listener's port (RFC 8252 section 7.3)
REDIRECT_WITH_PORT = re.compile(r'http://127\.0\.0\.1:[0-9]+/callback')
EMAIL = 'user@example.org'

lock = threading.Lock()
codes_used = set()
# Refresh tokens: those that rotate on use, those that do not, those
# already rotated out
rotating = {'S3CR3T-refresh-1'}
stable = set()
rotated_out = set()
counter = [1]

# Device code flows (login-tests.sh, task 1.7): the scenario comes from the
# endpoint path /device/<scenario>; each poll of a device code gets the next
# answer of its script (the last one repeats)
DEVICE_SCRIPTS = {
    'ok': ['pending', 'grant'],
    'complete': ['grant'],
    'slow': ['pending', 'slow_down', 'grant'],
    'denied': ['pending', 'access_denied'],
    'declined': ['authorization_declined'],
    'expired': ['expired_token'],
    'forever': ['pending'],
    'flaky': ['http_500', 'grant'],
    'publicoff': ['public_off'],
}
device_codes = {}


def b64url(data):
    return base64.urlsafe_b64encode(json.dumps(data).encode()).rstrip(b'=').decode()


def id_token(number):
    # A JWT with an unverifiable signature (the client does not check it:
    # the token comes from the token endpoint over TLS)
    now = int(time.time())
    claims = {'iss': 'https://mock.example', 'sub': '1001', 'aud': CLIENT_ID, 'email': EMAIL,
              'email_verified': True, 'name': 'Test User', 'iat': now, 'exp': now + 3600}
    return '%s.%s.S3CR3T-signature-%d' % (b64url({'alg': 'RS256', 'typ': 'JWT'}), b64url(claims), number)


def tokens(refresh=None, scope='openid email https://mail.example/'):
    counter[0] += 1
    answer = {
        'access_token': 'S3CR3T-access-%d' % counter[0],
        'token_type': 'Bearer',
        'expires_in': 3599,
        'scope': scope,
        'id_token': id_token(counter[0]),
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
                                      'form': form, 'time': time.time()}) + '\n')
        route = self.path.split('?')[0]
        try:
            if route == '/token':
                self.token(form)
            elif route == '/devicecode':
                self.devicecode(form)
            elif route.startswith('/device/'):
                self.device_flow(form, route[len('/device/'):])
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

    # Device code request, as Microsoft answers it (client check of
    # login-tests.sh): a code for CLIENT_ID, AADSTS700016 for
    # "not-found-client", AADSTS50059 for any other client (/common)
    def devicecode(self, form):
        if 'client_secret' in form:
            return self.error(400, 'invalid_request', 'no client secret expected')
        client = form.get('client_id')
        if client == CLIENT_ID:
            return self.send(200, {'device_code': 'S3CR3T-device-code', 'user_code': 'ABCD-EFGH',
                                   'verification_uri': 'https://127.0.0.1/device', 'expires_in': 900,
                                   'interval': 5, 'message': 'Not used'})
        if client == 'not-found-client':
            return self.send(400, {'error': 'unauthorized_client', 'error_codes': [700016],
                                   'error_description': "AADSTS700016: Application with identifier "
                                   "'not-found-client' was not found in the directory 'Mock'.\r\n"
                                   "Trace ID: t\r\nCorrelation ID: c"})
        self.send(400, {'error': 'invalid_request', 'error_codes': [50059],
                        'error_description': 'AADSTS50059: No tenant-identifying information found in '
                        'either the request or implied by any provided credentials.\r\nTrace ID: t'})

    # Device authorization request of a device code flow (RFC 8628 section
    # 3.2); "badanswer": no user_code
    def device_flow(self, form, scenario):
        if form.get('client_id') != CLIENT_ID:
            return self.send(400, {'error': 'invalid_request', 'error_codes': [50059],
                                   'error_description': 'AADSTS50059: No tenant-identifying information found.'})
        if scenario != 'badanswer' and scenario not in DEVICE_SCRIPTS:
            return self.error(400, 'invalid_request', 'unknown scenario')
        with lock:
            counter[0] += 1
            code = 'S3CR3T-device-%s-%d' % (scenario, counter[0])
            device_codes[code] = {'scenario': scenario, 'polls': 0}
        answer = {'device_code': code, 'user_code': 'WDJB-MJHT',
                  'verification_uri': 'https://127.0.0.1/device',
                  'expires_in': 3 if scenario == 'forever' else 30, 'interval': 1,
                  'message': 'Enter the code WDJB-MJHT at https://127.0.0.1/device'}
        if scenario == 'complete':
            answer['verification_uri_complete'] = 'https://127.0.0.1/device?code=WDJB-MJHT'
        if scenario == 'badanswer':
            del answer['user_code']
        self.send(200, answer)

    # One poll of a device code (RFC 8628 section 3.5, Microsoft's codes);
    # called with the lock held
    def device_poll(self, form, cookie):
        state = device_codes.get(form.get('device_code', ''))
        if not state:
            return self.error(400, 'bad_verification_code', 'unknown device code')
        script = DEVICE_SCRIPTS[state['scenario']]
        step = script[min(state['polls'], len(script) - 1)]
        state['polls'] += 1
        if step == 'pending':
            return self.error(400, 'authorization_pending', 'AADSTS70016: OAuth 2.0 device flow error. '
                              'Authorization is pending.')
        if step in ('slow_down', 'access_denied', 'authorization_declined', 'expired_token'):
            return self.error(400, step, 'The user said no' if step == 'access_denied' else '')
        if step == 'http_500':
            return self.send(500, '<html>busy</html>', 'text/html')
        if step == 'public_off':
            return self.send(401, {'error': 'invalid_client', 'error_codes': [7000218],
                                   'error_description': 'AADSTS7000218: The request body must contain the '
                                   'following parameter: client_assertion or client_secret.'})
        del device_codes[form['device_code']]
        refresh = 'S3CR3T-refresh-device-%d' % counter[0]
        rotating.add(refresh)
        return self.send(200, tokens(refresh), extra=cookie)

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
                code = form.get('code', '')
                if not code.startswith(CODE_PREFIX) or code in codes_used:
                    return self.error(400, 'invalid_grant', 'code unknown or already used')
                redirect = form.get('redirect_uri', '')
                if redirect != REDIRECT and not REDIRECT_WITH_PORT.fullmatch(redirect):
                    return self.error(400, 'invalid_grant', 'redirect_uri mismatch')
                if not 43 <= len(verifier) <= 128:
                    return self.error(400, 'invalid_grant', 'code_verifier missing or invalid')
                codes_used.add(code)
                # The first code keeps the token the token tests expect
                refresh = 'S3CR3T-refresh-1' if code == CODE else 'S3CR3T-refresh-for-' + code[len(CODE_PREFIX):]
                rotating.add(refresh)
                return self.send(200, tokens(refresh), extra=cookie)
            if grant == 'urn:ietf:params:oauth:grant-type:device_code':
                return self.device_poll(form, cookie)
            if grant == 'refresh_token':
                token = form.get('refresh_token', '')
                if token in rotating:
                    # Rotation: a new refresh token, the old one is dead
                    rotating.discard(token)
                    rotated_out.add(token)
                    new = 'S3CR3T-refresh-2' if token == 'S3CR3T-refresh-1' else token + '-rotated'
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
