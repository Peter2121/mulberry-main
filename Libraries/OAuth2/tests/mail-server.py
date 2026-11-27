#!/usr/bin/env python3
#
# Mock IMAP and SMTP servers for login-tests.sh (OAUTH2-PLAN.md, task 1.8):
# IMAP over TLS from the start (with SASL-IR), and SMTP with STARTTLS.
# Without an initial response, the client gets "+ " or "334 " first.  Both offer XOAUTH2 and
# OAUTHBEARER, accept a token starting with "S3CR3T-access-" and answer
# any other with an error challenge (base64 JSON), then a failure.  Each
# exchange is logged as one JSON line.
#
# Usage: mail-server.py LOGFILE PORTFILE CERT KEY
# PORTFILE receives "IMAPS-PORT SMTP-PORT".

import base64
import json
import os
import re
import socketserver
import ssl
import sys
import threading

LOGFILE, PORTFILE, CERT, KEY = sys.argv[1:5]
CHALLENGE = {'status': 'invalid_token', 'schemes': 'bearer', 'scope': 'https://mail.example/'}
lock = threading.Lock()
context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
context.load_cert_chain(CERT, KEY)


def log(entry):
    with lock:
        with open(LOGFILE, 'a') as f:
            f.write(json.dumps(entry) + '\n')


class Conn:
    def __init__(self, sock):
        self.sock = sock
        self.sock.settimeout(10)
        self.buf = b''

    def readline(self):
        while b'\r\n' not in self.buf:
            data = self.sock.recv(4096)
            if not data:
                return None
            self.buf += data
        line, self.buf = self.buf.split(b'\r\n', 1)
        return line.decode('utf-8', 'replace')

    def send(self, text):
        self.sock.sendall((text + '\r\n').encode())

    def starttls(self):
        self.sock = context.wrap_socket(self.sock, server_side=True)
        self.sock.settimeout(10)


# The client's response: None if malformed, else (user, token, pairs)
def parse_response(mechanism, response):
    try:
        text = base64.b64decode(response, validate=True).decode('utf-8')
    except (ValueError, UnicodeDecodeError):
        return None
    if mechanism == 'XOAUTH2':
        m = re.fullmatch(r'user=([^\x01]+)\x01auth=Bearer ([^\x01]+)\x01\x01', text)
        return (m.group(1), m.group(2), {}) if m else None
    m = re.fullmatch(r'n,a=([^,]*),\x01((?:[a-z]+=[^\x01]*\x01)*)\x01', text)
    if not m:
        return None
    pairs = dict(p.split('=', 1) for p in m.group(2).split('\x01') if p)
    auth = pairs.pop('auth', '')
    if not auth.startswith('Bearer '):
        return None
    return (m.group(1), auth[len('Bearer '):], pairs)


# One SASL exchange; returns True when the token is accepted.  prompt: the
# continuation prefix ("+" or "334")
def sasl(conn, protocol, mechanism, initial, prompt):
    entry = {'protocol': protocol, 'mechanism': mechanism, 'initial_response': initial is not None}
    response = initial
    if response is None:
        conn.send(prompt + ' ')
        response = conn.readline() or ''
    parsed = parse_response(mechanism, response)
    if parsed is None:
        entry['result'] = 'malformed'
        log(entry)
        return False
    entry['user'], token, entry['pairs'] = parsed
    entry['token_ok'] = token.startswith('S3CR3T-access-')
    if not entry['token_ok']:
        conn.send(prompt + ' ' + base64.b64encode(json.dumps(CHALLENGE).encode()).decode())
        entry['reply_to_challenge'] = conn.readline()
    entry['result'] = 'accepted' if entry['token_ok'] else 'refused'
    log(entry)
    return entry['token_ok']


class Imap(socketserver.BaseRequestHandler):
    def handle(self):
        try:
            conn = Conn(context.wrap_socket(self.request, server_side=True))
        except (ssl.SSLError, OSError):
            return
        capability = 'IMAP4rev1 SASL-IR AUTH=XOAUTH2 AUTH=OAUTHBEARER'
        # As Microsoft's: the front end's name in base64 of UTF-16LE
        front_end = base64.b64encode('MOCK1P01CA0001.EXAMPLE'.encode('utf-16-le')).decode()
        conn.send('* OK Mock IMAP4 service ready. (tcpproxy BACKENDAUTHENTICATE) [%s]' % front_end)
        try:
            while True:
                line = conn.readline()
                if line is None:
                    return
                words = line.split(' ')
                tag, command = words[0], (words[1].upper() if len(words) > 1 else '')
                if command == 'CAPABILITY':
                    conn.send('* CAPABILITY ' + capability)
                    conn.send(tag + ' OK CAPABILITY completed')
                elif command == 'AUTHENTICATE' and len(words) >= 3:
                    mechanism = words[2].upper()
                    if mechanism not in ('XOAUTH2', 'OAUTHBEARER'):
                        conn.send(tag + ' NO unsupported mechanism')
                    elif sasl(conn, 'imap', mechanism, words[3] if len(words) > 3 else None, '+'):
                        conn.send(tag + ' OK [CAPABILITY IMAP4rev1] Authenticated')
                    else:
                        conn.send(tag + ' NO [AUTHENTICATIONFAILED] Invalid credentials (Failure)')
                elif command == 'LOGOUT':
                    log({'protocol': 'imap', 'command': 'LOGOUT'})
                    conn.send('* BYE Mock IMAP closing')
                    conn.send(tag + ' OK LOGOUT completed')
                    return
                else:
                    conn.send(tag + ' BAD unknown command')
        except (OSError, ssl.SSLError):
            return


class Smtp(socketserver.BaseRequestHandler):
    def handle(self):
        conn = Conn(self.request)
        tls = False
        conn.send('220 mock.example ESMTP ready')
        try:
            while True:
                line = conn.readline()
                if line is None:
                    return
                words = line.split(' ')
                command = words[0].upper()
                if command in ('EHLO', 'HELO'):
                    conn.send('250-mock.example')
                    conn.send('250-AUTH XOAUTH2 OAUTHBEARER' if tls else '250-STARTTLS')
                    conn.send('250 8BITMIME')
                elif command == 'STARTTLS' and not tls:
                    conn.send('220 2.0.0 Ready to start TLS')
                    conn.starttls()
                    tls = True
                elif command == 'AUTH' and len(words) >= 2:
                    mechanism = words[1].upper()
                    if not tls:
                        conn.send('530 5.7.0 Must issue a STARTTLS command first')
                    elif mechanism not in ('XOAUTH2', 'OAUTHBEARER'):
                        conn.send('504 5.5.4 Unrecognized authentication type')
                    elif sasl(conn, 'smtp', mechanism, words[2] if len(words) > 2 else None, '334'):
                        conn.send('235 2.7.0 Accepted')
                    else:
                        conn.send('535-5.7.8 Username and Password not accepted.')
                        conn.send('535 5.7.8 Learn more at https://mock.example/oauth')
                elif command == 'QUIT':
                    log({'protocol': 'smtp', 'command': 'QUIT'})
                    conn.send('221 2.0.0 closing connection')
                    return
                else:
                    conn.send('502 5.5.2 unknown command')
        except (OSError, ssl.SSLError):
            return


class Server(socketserver.ThreadingTCPServer):
    daemon_threads = True
    allow_reuse_address = True


imap = Server(('127.0.0.1', 0), Imap)
smtp = Server(('127.0.0.1', 0), Smtp)
threading.Thread(target=imap.serve_forever, daemon=True).start()
with open(PORTFILE + '.tmp', 'w') as f:
    f.write('%d %d' % (imap.server_address[1], smtp.server_address[1]))
os.rename(PORTFILE + '.tmp', PORTFILE)
smtp.serve_forever()
