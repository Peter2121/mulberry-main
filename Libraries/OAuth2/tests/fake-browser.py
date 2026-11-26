#!/usr/bin/env python3
#
# Plays the user's browser for login-tests.sh: records the authorization URL
# it was given, asks for the favicon as a browser does, then brings the
# provider's answer back to the redirect URI of the URL, with its state.
#
# Usage: fake-browser.py URL MODE RECORDFILE
#
# MODE: "code" (a new code S3CR3T-code-<random>), "deny" (error
# access_denied), "none" (the user never finishes).

import secrets
import sys
import urllib.error
import urllib.parse
import urllib.request

url, mode, record = sys.argv[1], sys.argv[2], sys.argv[3]
with open(record, 'a') as f:
    f.write(url + '\n')

query = dict(urllib.parse.parse_qsl(urllib.parse.urlsplit(url).query, keep_blank_values=True))
redirect = query['redirect_uri']
state = query['state']
if mode == 'none':
    sys.exit(0)

# The listener is on 127.0.0.1: never through a proxy
opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def get(target):
    try:
        with opener.open(target, timeout=10) as answer:
            return answer.status
    except urllib.error.HTTPError as error:
        return error.code


parts = urllib.parse.urlsplit(redirect)
get('%s://%s/favicon.ico' % (parts.scheme, parts.netloc))
if mode == 'code':
    params = {'code': 'S3CR3T-code-' + secrets.token_hex(6), 'state': state, 'scope': query.get('scope', '')}
elif mode == 'deny':
    params = {'error': 'access_denied', 'error_description': 'The user said no', 'state': state}
else:
    sys.exit('fake-browser: unknown mode ' + mode)
status = get(redirect + '?' + urllib.parse.urlencode(params))
sys.exit(0 if status == 200 else 1)
