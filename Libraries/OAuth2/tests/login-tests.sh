#!/bin/sh
#
# Tests of "oauth2-tool login", "device", "refresh", "imap-check" and
# "smtp-check" (OAUTH2-PLAN.md, tasks 1.6 to 1.8) without a provider: the mock token endpoint over TLS
# (token-server.py), a fake xdg-open, and a fake browser (fake-browser.py)
# that brings a code back to the tool's listener.  Checks the authorization
# URL, the browser launch without a shell, the client check, the code
# exchange, the token file, the refresh with rotation, the failures, and an
# output without secrets.  Needs no network beyond 127.0.0.1.
#
# Usage: login-tests.sh TOOL TESTDIR OUTDIR

set -u

if [ $# -ne 3 ]; then
	echo "usage: $0 TOOL TESTDIR OUTDIR" >&2
	exit 2
fi
absolute() {
	case $1 in
	/*) echo "$1" ;;
	*) echo "$(pwd)/$1" ;;
	esac
}
TOOL=$(absolute "$1")
TESTDIR=$(absolute "$2")
OUT=$(absolute "$3")
rm -rf "$OUT"
mkdir -p "$OUT" || exit 1

PYTHON=$(command -v python3) || {
	echo "login-tests: python3 is needed for the mock server (pkg install python3)" >&2
	exit 1
}
# The tool accepts only https endpoints: the mock server needs a certificate
if ! openssl req -x509 -newkey rsa:2048 -nodes -days 1 -subj /CN=127.0.0.1 \
		-addext subjectAltName=IP:127.0.0.1 \
		-keyout "$OUT/key.pem" -out "$OUT/cert.pem" > "$OUT/openssl.txt" 2>&1; then
	echo "login-tests: skipped: openssl could not make a certificate for the mock server" >&2
	exit 0
fi

SLOG=$OUT/server-requests.jsonl
PORTFILE=$OUT/port
: > "$SLOG"
"$PYTHON" -I "$TESTDIR/token-server.py" "$SLOG" "$PORTFILE" "$OUT/cert.pem" "$OUT/key.pem" &
SERVER=$!
trap 'kill $SERVER 2>/dev/null' EXIT INT TERM
i=0
while [ ! -s "$PORTFILE" ] && [ $i -lt 50 ]; do
	sleep 0.1
	i=$((i + 1))
done
[ -s "$PORTFILE" ] || { echo "login-tests: the mock server did not start" >&2; exit 1; }
read PORT TLSPORT < "$PORTFILE"

# The provider "mock" for the domain example.org
CONFIG=$OUT/providers.json
cat > "$CONFIG" <<EOF
{
  "version": 1,
  "providers": {
    "mock": {
      "name": "Mock",
      "domains": ["example.org"],
      "authorization_endpoint": "https://127.0.0.1:$TLSPORT/authorize",
      "token_endpoint": "https://127.0.0.1:$TLSPORT/token",
      "scopes": {
        "always": ["openid", "email"],
        "mail": ["https://mail.example/"],
        "calendar": ["https://calendar.example/"]
      },
      "authorization_params": { "access_type": "offline", "prompt": "consent" },
      "redirect": "http://127.0.0.1/callback",
      "client_id": "test-client",
      "client_secret": "S3CR3T-client-secret"
    },
    "mockms": {
      "_comment": "Checks its client as Microsoft: a device code request",
      "name": "MockMS",
      "authorization_endpoint": "https://127.0.0.1:$TLSPORT/authorize",
      "token_endpoint": "https://127.0.0.1:$TLSPORT/token",
      "device_authorization_endpoint": "https://127.0.0.1:$TLSPORT/devicecode",
      "client_check": "device_authorization",
      "scopes": { "always": ["openid", "email"], "mail": ["https://mail.example/"] },
      "redirect": "http://127.0.0.1/callback",
      "client_id": "test-client"
    },
    "mockuuid": {
      "name": "MockUUID",
      "authorization_endpoint": "https://127.0.0.1:$TLSPORT/authorize",
      "token_endpoint": "https://127.0.0.1:$TLSPORT/token",
      "scopes": { "always": ["openid", "email"], "mail": ["https://mail.example/"] },
      "redirect": "http://127.0.0.1/callback",
      "client_id_format": "uuid",
      "client_id": "test-client"
    },
    "mockdev": {
      "_comment": "Device code; the tenant picks the mock server's scenario",
      "name": "MockDev",
      "authorization_endpoint": "https://127.0.0.1:$TLSPORT/authorize",
      "token_endpoint": "https://127.0.0.1:$TLSPORT/token",
      "device_authorization_endpoint": "https://127.0.0.1:$TLSPORT/device/{tenant}",
      "scopes": { "always": ["offline_access", "openid"], "mail": ["https://mail.example/"] },
      "redirect": "http://127.0.0.1/callback",
      "tenant": "ok",
      "client_id": "test-client"
    }
  }
}
EOF
sed -e 's/"test-client"/""/' "$CONFIG" > "$OUT/no-client.json"

# Fake commands.  bin: an xdg-open that starts the fake browser and returns
# at once, as with a desktop.  bin-fail: an xdg-open that fails, and a
# "browser" for $BROWSER.  bin-none: nothing at all.
mkdir -p "$OUT/bin" "$OUT/bin-fail" "$OUT/bin-none"
cat > "$OUT/bin/xdg-open" <<EOF
#!/bin/sh
{ echo "\$#"; for a in "\$@"; do printf '%s\n' "\$a"; done; } > "$OUT/xdg-open.args"
"$PYTHON" -I "$TESTDIR/fake-browser.py" "\$1" "\${FAKE_BROWSER_MODE:-code}" "$OUT/urls" > /dev/null 2>&1 &
exit 0
EOF
printf '#!/bin/sh\nexit 3\n' > "$OUT/bin-fail/xdg-open"
cat > "$OUT/bin-fail/browser" <<EOF
#!/bin/sh
{ echo "\$#"; for a in "\$@"; do printf '%s\n' "\$a"; done; } > "$OUT/browser.args"
for a in "\$@"; do url=\$a; done
"$PYTHON" -I "$TESTDIR/fake-browser.py" "\$url" code "$OUT/urls" > /dev/null 2>&1 &
exit 0
EOF
chmod 755 "$OUT/bin/xdg-open" "$OUT/bin-fail/xdg-open" "$OUT/bin-fail/browser"

failed=0
check() {
	# check DESCRIPTION COMMAND...: the command must succeed
	description=$1
	shift
	if "$@" > /dev/null 2>&1; then
		echo "ok      $description"
	else
		echo "FAILED  $description"
		failed=$((failed + 1))
	fi
}

# run NAME MODE BIN [ARGS...]: the tool with the fake commands of OUT/BIN
# as its only PATH, the fake browser in MODE, and $BROWSER from BROWSER_SET;
# NAME.out, NAME.err and NAME.status in OUT
BROWSER_SET=
run() {
	name=$1
	mode=$2
	bin=$3
	shift 3
	env -u BROWSER ${BROWSER_SET:+"BROWSER=$BROWSER_SET"} PATH="$OUT/$bin" XDG_CONFIG_HOME="$OUT/config" \
		FAKE_BROWSER_MODE="$mode" "$TOOL" --system-config "$CONFIG" --no-user-config --ca-file "$OUT/cert.pem" \
		"$@" > "$OUT/$name.out" 2> "$OUT/$name.err"
	echo $? > "$OUT/$name.status"
}
status() {
	[ "$(cat "$OUT/$1.status")" = "$2" ]
}

# Checks the authorization URL of the last login, the client check and the
# code exchange in the server's log: check-url.py URLS SLOG REDIRECT_URI HINT
cat > "$OUT/check-url.py" <<'EOF'
import base64, hashlib, json, sys, urllib.parse
urls, slog, redirect, hint, tlsport = sys.argv[1:6]
url = open(urls).read().split('\n')[-2]
parts = urllib.parse.urlsplit(url)
problems = []
if '%s://%s%s' % (parts.scheme, parts.netloc, parts.path) != 'https://127.0.0.1:%s/authorize' % tlsport:
    problems.append('endpoint')
params = urllib.parse.parse_qsl(parts.query, keep_blank_values=True)
names = [name for name, value in params]
if names != ['response_type', 'client_id', 'redirect_uri', 'scope', 'state', 'code_challenge',
             'code_challenge_method', 'login_hint', 'access_type', 'prompt']:
    problems.append('names %s' % names)
q = dict(params)
expected = {'response_type': 'code', 'client_id': 'test-client', 'redirect_uri': redirect,
            'scope': 'openid email https://mail.example/ https://calendar.example/',
            'code_challenge_method': 'S256', 'login_hint': hint, 'access_type': 'offline', 'prompt': 'consent'}
for name, value in expected.items():
    if q.get(name) != value:
        problems.append(name)
if len(q.get('state', '')) != 22:
    problems.append('state length')
if ' ' in url or '+' in parts.query:
    problems.append('spaces not as %20')
requests = [json.loads(line) for line in open(slog)]
forms = [r['form'] for r in requests if r['form'].get('grant_type') == 'authorization_code']
probe = [f for f in forms if f.get('code') == 'invalid-code-for-the-client-check']
exchange = [f for f in forms if f.get('code', '').startswith('S3CR3T-code-')]
if not probe or probe[-1].get('redirect_uri') != redirect:
    problems.append('client check')
if not exchange or exchange[-1].get('redirect_uri') != redirect:
    problems.append('exchange redirect_uri')
else:
    digest = hashlib.sha256(exchange[-1]['code_verifier'].encode()).digest()
    if base64.urlsafe_b64encode(digest).rstrip(b'=').decode() != q.get('code_challenge'):
        problems.append('PKCE: the verifier does not match the challenge')
if problems:
    sys.exit('check-url: ' + ', '.join(problems))
EOF

# Checks the token file: check-file.py FILE REFRESH_PREFIX
cat > "$OUT/check-file.py" <<'EOF'
import json, sys
f = json.load(open(sys.argv[1]))
ok = (f['version'] == 1 and f['provider'] == 'mock' and f['account'] == 'user@example.org' and
      f['client_id'] == 'test-client' and f['email'] == 'user@example.org' and
      f['requested_scopes'] == ['openid', 'email', 'https://mail.example/', 'https://calendar.example/'] and
      f['scope_given'] is True and f['granted_scopes'] == ['openid', 'email', 'https://mail.example/'] and
      f['refresh_token'].startswith(sys.argv[2]))
sys.exit(0 if ok else 'check-file: unexpected content')
EOF

# 1. Sign-in: client check, browser, code, exchange, token file
run login code bin login user@example.org
REDIRECT=$(sed -n 's/^redirect URI: //p' "$OUT/login.out")
URL=$(sed -n 2p "$OUT/xdg-open.args" 2> /dev/null)
TOKENFILE=$(ls "$OUT"/config/*/oauth2-tool/mock-user@example.org.json 2> /dev/null | head -n 1)
check "login: exit status 0" status login 0
check "login: redirect URI http://127.0.0.1:PORT/callback" \
	sh -c "echo '$REDIRECT' | grep -q -x 'http://127.0.0.1:[0-9]*/callback'"
check "login: authorization URL, client check, PKCE and exchange" \
	"$PYTHON" -I "$OUT/check-url.py" "$OUT/urls" "$SLOG" "$REDIRECT" user@example.org "$TLSPORT"
check "login: xdg-open got the URL as its only argument (no shell)" \
	sh -c "[ \"\$(sed -n 1p '$OUT/xdg-open.args')\" = 1 ] && [ -n '$URL' ] && [ \"\$(tail -n 1 '$OUT/urls')\" = '$URL' ]"
check "login: the URL is printed for the user" grep -q -x -F "$URL" "$OUT/login.err"
check "login: provider, account, ID token address" sh -c "grep -q -x 'provider: mock (Mock)' '$OUT/login.out' &&
	grep -q -x 'account: user@example.org' '$OUT/login.out' &&
	grep -q -x 'signed in as: user@example.org (verified)' '$OUT/login.out'"
check "login: token type, expiry, scopes" sh -c "grep -q -x 'token type: Bearer' '$OUT/login.out' &&
	grep -q '^expires in: 3599 s (' '$OUT/login.out' &&
	grep -q -x 'granted scopes: openid email https://mail.example/' '$OUT/login.out'"
check "login: the refused calendar scope is reported" \
	grep -q -x 'not granted: https://calendar.example/' "$OUT/login.out"
check "login: tokens redacted" sh -c "grep -q -x 'access token: <redacted 15 bytes>' '$OUT/login.out' &&
	grep -q '^refresh token: <redacted [0-9]* bytes>, saved in .*/oauth2-tool/mock-user@example.org.json$' '$OUT/login.out'"
check "login: token file, mode 0600, folder 0700" sh -c "[ -n '$TOKENFILE' ] &&
	[ \"\$(ls -l '$TOKENFILE' | cut -c1-10)\" = -rw------- ] &&
	[ \"\$(ls -ld \"\$(dirname '$TOKENFILE')\" | cut -c1-10)\" = drwx------ ]"
check "login: token file content" "$PYTHON" -I "$OUT/check-file.py" "$TOKENFILE" S3CR3T-refresh-for-
check "login: no secret in the output" sh -c "! cat '$OUT/login.out' '$OUT/login.err' | grep -q S3CR3T"
cp -p "$TOKENFILE" "$OUT/token-before-rotation.json"

# 2. Refresh: rotation saved, refused scope not asked for again
run refresh none bin refresh user@example.org
check "refresh: exit status 0" status refresh 0
check "refresh: rotated token saved" sh -c "
	grep -q '^refresh token: rotated, <redacted [0-9]* bytes>, saved in ' '$OUT/refresh.out' &&
	grep -q '\"refresh_token\": \"S3CR3T-refresh-for-.*-rotated\"' '$TOKENFILE' &&
	[ \"\$(ls -l '$TOKENFILE' | cut -c1-10)\" = -rw------- ]"
check "refresh: scopes asked for: the requested minus the refused" \
	sh -c "tail -n 1 '$SLOG' | grep -q -F '\"scope\": \"openid email https://mail.example/\"'"
check "refresh: ID token address" grep -q -x 'signed in as: user@example.org (verified)' "$OUT/refresh.out"
run refresh2 none bin refresh user@example.org
check "refresh again: token kept (none in the answer)" sh -c "[ \"\$(cat '$OUT/refresh2.status')\" = 0 ] &&
	grep -q -x 'refresh token: unchanged' '$OUT/refresh2.out'"
run old none bin refresh --token-file "$OUT/token-before-rotation.json" user@example.org
check "refresh with the rotated-out token: invalid_grant, sign in again" sh -c "[ \"\$(cat '$OUT/old.status')\" = 1 ] &&
	grep -q 'invalid_grant' '$OUT/old.err' && grep -q 'sign in again with: oauth2-tool login user@example.org' '$OUT/old.err'"
run otherclient none bin refresh --client-id other-client user@example.org
check "refresh with another client ID: refused before sending" sh -c "[ \"\$(cat '$OUT/otherclient.status')\" = 1 ] &&
	grep -q 'issued to client ID test-client, but the configuration has other-client' '$OUT/otherclient.err'"
run nofile none bin refresh nobody@example.org
check "refresh without a token file: sign in first" sh -c "[ \"\$(cat '$OUT/nofile.status')\" = 1 ] &&
	grep -q 'sign in first: oauth2-tool login nobody@example.org' '$OUT/nofile.err'"
chmod 644 "$TOKENFILE"
run perms none bin refresh user@example.org
check "refresh: a token file readable by others is reported" grep -q 'readable or writable by group or others (mode 0644)' "$OUT/perms.err"
run fix none bin --fix-permissions refresh user@example.org
check "--fix-permissions: the token file is 0600 again" sh -c "[ \"\$(cat '$OUT/fix.status')\" = 0 ] &&
	[ \"\$(ls -l '$TOKENFILE' | cut -c1-10)\" = -rw------- ]"
check "refresh: no secret in the output" sh -c "! cat '$OUT'/refresh*.out '$OUT'/refresh*.err | grep -q S3CR3T"

# 3. The provider's refusal, the time limit, Ctrl-C
run deny deny bin login deny@example.org
check "user refuses: exit 1, the provider's error, no token file" sh -c "[ \"\$(cat '$OUT/deny.status')\" = 1 ] &&
	grep -q 'the provider did not sign you in: access_denied: The user said no' '$OUT/deny.err' &&
	! ls '$OUT'/config/*/oauth2-tool/mock-deny@example.org.json"
started=$(date +%s)
run timeout none bin login --timeout 2 user@example.org
elapsed=$(($(date +%s) - started))
check "no answer from the browser: time limit" sh -c "[ \"\$(cat '$OUT/timeout.status')\" = 1 ] &&
	grep -q 'no answer from the browser within 2 s' '$OUT/timeout.err' && [ $elapsed -le 5 ]"
env -u BROWSER PATH="$OUT/bin" XDG_CONFIG_HOME="$OUT/config" FAKE_BROWSER_MODE=none "$TOOL" \
	--system-config "$CONFIG" --no-user-config --ca-file "$OUT/cert.pem" login user@example.org \
	> "$OUT/interrupt.out" 2> "$OUT/interrupt.err" &
TOOLPID=$!
sleep 1.5
kill -INT $TOOLPID
wait $TOOLPID
echo $? > "$OUT/interrupt.status"
check "Ctrl-C: cancelled, exit 1" sh -c "[ \"\$(cat '$OUT/interrupt.status')\" = 1 ] && grep -q 'cancelled' '$OUT/interrupt.err'"

# 4. Launching the browser
rm -f "$OUT/xdg-open.args"
BROWSER_SET="missing-browser:browser --new-window %s"
run browservar code bin-fail login user@example.org
BROWSER_SET=
check "xdg-open fails: \$BROWSER entries tried, %s replaced" sh -c "[ \"\$(cat '$OUT/browservar.status')\" = 0 ] &&
	[ \"\$(sed -n 1,2p '$OUT/browser.args' | tr '\n' ' ')\" = '2 --new-window ' ] &&
	[ \"\$(sed -n 3p '$OUT/browser.args')\" = \"\$(tail -n 1 '$OUT/urls')\" ] &&
	grep -q -x 'Browser started (browser).' '$OUT/browservar.err'"
run nobrowser none bin-none login --timeout 1 user@example.org
check "no browser at all: said, the URL printed" sh -c "
	grep -q 'no browser could be started: xdg-open (.*); \$BROWSER is not set' '$OUT/nobrowser.err' &&
	grep -q '^https://127.0.0.1:$TLSPORT/authorize?response_type=code&' '$OUT/nobrowser.err'"
run nobrowseropt code bin login --no-browser --timeout 1 user@example.org
check "--no-browser: xdg-open not started" sh -c "[ ! -e '$OUT/xdg-open.args' ] &&
	grep -q '^https://127.0.0.1:$TLSPORT/authorize?' '$OUT/nobrowseropt.err'"

# 5. Registration and configuration errors
run badclient code bin login --client-id wrong-client user@example.org
check "wrong client ID: refused by the client check, no browser" sh -c "[ \"\$(cat '$OUT/badclient.status')\" = 1 ] &&
	grep -q 'the check of the client registration failed: .*invalid_client' '$OUT/badclient.err' &&
	grep -q 'check the client ID, the client secret' '$OUT/badclient.err' && [ ! -e '$OUT/xdg-open.args' ]"
run nocheck code bin login --no-client-check --client-id wrong-client user@example.org
check "--no-client-check: the exchange reports the wrong client ID" sh -c "[ \"\$(cat '$OUT/nocheck.status')\" = 1 ] &&
	grep -q 'the code exchange failed: .*invalid_client' '$OUT/nocheck.err'"
run noprovider code bin login someone@unknown.test
check "unknown domain: exit 2, --provider suggested" sh -c "[ \"\$(cat '$OUT/noprovider.status')\" = 2 ] &&
	grep -q 'no provider for \"someone@unknown.test\": name it with --provider' '$OUT/noprovider.err'"
run services code bin login --services mail,diary user@example.org
check "unknown scope group: exit 2" sh -c "[ \"\$(cat '$OUT/services.status')\" = 2 ] &&
	grep -q 'mock has no scope group \"diary\"' '$OUT/services.err'"
env -u BROWSER PATH="$OUT/bin" XDG_CONFIG_HOME="$OUT/config" "$TOOL" --system-config "$OUT/no-client.json" \
	--no-user-config login user@example.org > "$OUT/noclient.out" 2> "$OUT/noclient.err"
echo $? > "$OUT/noclient.status"
check "no client ID configured: exit 2" sh -c "[ \"\$(cat '$OUT/noclient.status')\" = 2 ] &&
	grep -q 'no client ID for mock' '$OUT/noclient.err'"

# 6. Options: services, prompt, the log, --show-token
rm -f "$OUT/urls"
run options code bin login --services mail --prompt select_account --verbose --token-file "$OUT/other.json" \
	user@example.org
check "--services mail and --prompt: in the URL" sh -c "[ \"\$(cat '$OUT/options.status')\" = 0 ] &&
	grep -q 'scope=openid%20email%20https%3A%2F%2Fmail.example%2F&state=' '$OUT/urls' &&
	grep -q '&prompt=select_account\$' '$OUT/urls' && ! grep -q -F 'not granted' '$OUT/options.out'"
check "--token-file: used" sh -c "[ \"\$(ls -l '$OUT/other.json' | cut -c1-10)\" = -rw------- ]"
check "--verbose: client check, listener and exchange logged" sh -c "
	grep -q '^\* checking the client registration' '$OUT/options.err' &&
	grep -q '^< GET /callback?code=<redacted [0-9]* bytes>&state=<redacted 22 bytes>' '$OUT/options.err' &&
	grep -q '^> grant_type=authorization_code&code=<redacted' '$OUT/options.err' &&
	grep -q '^\* browser: xdg-open: exit status 0' '$OUT/options.err'"
check "--verbose: no secret in the log" sh -c "! grep -q S3CR3T '$OUT/options.err'"
run prompt-none code bin login --prompt '' --token-file "$OUT/other.json" user@example.org
check "--prompt '': no prompt sent" sh -c "[ \"\$(cat '$OUT/prompt-none.status')\" = 0 ] && ! tail -n 1 '$OUT/urls' | grep -q prompt"
run show none bin refresh --show-token --token-file "$OUT/other.json" user@example.org
check "--show-token prints the tokens" sh -c "grep -q '^access token: S3CR3T-access-' '$OUT/show.out' &&
	grep -q '^refresh token: rotated, S3CR3T-refresh-' '$OUT/show.out'"

# 7. Client checks of other providers: a device code request (Microsoft),
# the form of the client ID.  requests-since N: the server's requests after
# its first N, one "path client_id grant_type code has-secret scope" line each
requests_since() {
	"$PYTHON" -I -c 'import json, sys
for line in open(sys.argv[1]).readlines()[int(sys.argv[2]):]:
    r = json.loads(line); f = r["form"]
    print(r["path"], f.get("client_id"), f.get("grant_type"), f.get("code"), "client_secret" in f, f.get("scope"))' "$SLOG" "$1"
}
before=$(wc -l < "$SLOG")
rm -f "$OUT/xdg-open.args"
run devok code bin login --verbose --provider mockms --token-file "$OUT/ms.json" user@example.org
requests_since $before > "$OUT/devok.requests"
check "device code check: one request with client ID and scopes, no secret, then the sign-in" sh -c "
	[ \"\$(cat '$OUT/devok.status')\" = 0 ] &&
	[ \"\$(sed -n 1p '$OUT/devok.requests')\" = '/devicecode test-client None None False openid email https://mail.example/' ] &&
	! grep -q invalid-code-for-the-client-check '$OUT/devok.requests' && [ -e '$OUT/xdg-open.args' ] &&
	grep -q 'checking the client registration with a device code request' '$OUT/devok.err' &&
	! grep -q S3CR3T '$OUT/devok.err' '$OUT/devok.out'"
rm -f "$OUT/xdg-open.args"
run devbad code bin login --provider mockms --client-id wrong-client user@example.org
check "device code check, AADSTS50059: refused, no browser" sh -c "[ \"\$(cat '$OUT/devbad.status')\" = 1 ] &&
	grep -q 'the check of the client registration failed: the provider does not know this client ID (invalid_request: AADSTS50059: No tenant-identifying' '$OUT/devbad.err' &&
	[ \"\$(grep -c 'check the client ID, the client secret' '$OUT/devbad.err')\" = 1 ] && [ ! -e '$OUT/xdg-open.args' ]"
run devnotfound code bin login --provider mockms --client-id not-found-client user@example.org
check "device code check, AADSTS700016: refused, the advice once" sh -c "[ \"\$(cat '$OUT/devnotfound.status')\" = 1 ] &&
	grep -q 'does not know this client ID (unauthorized_client: AADSTS700016: Application' '$OUT/devnotfound.err' &&
	[ \"\$(grep -c 'check the client ID, the client secret' '$OUT/devnotfound.err')\" = 1 ] && [ ! -e '$OUT/xdg-open.args' ]"
before=$(wc -l < "$SLOG")
run badform code bin login --provider mockuuid --client-id wrong-id user@example.org
check "client ID of the wrong form: exit 2 before any request" sh -c "[ \"\$(cat '$OUT/badform.status')\" = 2 ] &&
	grep -q 'the client ID (8 characters) is not in MockUUID.s form xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx' '$OUT/badform.err' &&
	[ \$(wc -l < '$SLOG') -eq $before ] && [ ! -e '$OUT/xdg-open.args' ]"
run formskip code bin login --no-client-check --provider mockuuid --client-id wrong-id user@example.org
check "--no-client-check: the form is not checked either" sh -c "[ \"\$(cat '$OUT/formskip.status')\" = 1 ] &&
	grep -q 'the code exchange failed: .*invalid_client' '$OUT/formskip.err'"

# 8. Device code (task 1.7): mockdev, the scenario as tenant.  polls N
# SCENARIO: "gap gap..." in seconds between the device code request and the
# polls of the server's requests after its first N
polls() {
	"$PYTHON" -I -c 'import json, sys
times = []
for line in open(sys.argv[1]).readlines()[int(sys.argv[2]):]:
    r = json.loads(line)
    if r["path"].startswith("/device/" + sys.argv[3]) or r["form"].get("device_code", "").startswith("S3CR3T-device-" + sys.argv[3] + "-"):
        times.append(r["time"])
print(" ".join("%.1f" % (b - a) for a, b in zip(times, times[1:])))' "$SLOG" "$1" "$2"
}
device() {
	# device NAME SCENARIO [ARGS...]
	name=$1
	scenario=$2
	shift 2
	run "$name" none bin-none device --provider mockdev --tenant "$scenario" --token-file "$OUT/$name.json" "$@" \
		user@example.org
}
before=$(wc -l < "$SLOG")
device devok ok --verbose
requests_since $before > "$OUT/devok.requests"
polls $before ok > "$OUT/devok.polls"
check "device: code and URL shown, polled at the interval, tokens saved" sh -c "
	[ \"\$(cat '$OUT/devok.status')\" = 0 ] &&
	grep -q -x '    https://127.0.0.1/device' '$OUT/devok.err' && grep -q -x '    WDJB-MJHT' '$OUT/devok.err' &&
	grep -q 'The code expires in 30 s' '$OUT/devok.err' &&
	[ \"\$(sed -n 1p '$OUT/devok.requests')\" = '/device/ok test-client None None False offline_access openid https://mail.example/' ] &&
	[ \"\$(grep -c '^/token test-client urn:ietf:params:oauth:grant-type:device_code' '$OUT/devok.requests')\" = 2 ] &&
	\"$PYTHON\" -I -c 'import sys; g = [float(x) for x in open(sys.argv[1]).read().split()]; sys.exit(not (len(g) == 2 and all(0.9 <= x < 2.5 for x in g)))' '$OUT/devok.polls' &&
	grep -q '^refresh token: <redacted .*saved in' '$OUT/devok.out' && grep -q 'S3CR3T-refresh-device-' '$OUT/devok.json' &&
	[ \"\$(stat -f %Lp '$OUT/devok.json')\" = 600 ]"
check "device: no secret in the output or the log (device code redacted)" sh -c "
	! grep -q S3CR3T '$OUT/devok.out' '$OUT/devok.err' && grep -q 'device_code.*<redacted' '$OUT/devok.err'"
run devrefresh none bin-none refresh --provider mockdev --tenant ok --token-file "$OUT/devok.json" user@example.org
check "device: the saved refresh token works" sh -c "[ \"\$(cat '$OUT/devrefresh.status')\" = 0 ] &&
	grep -q '^refresh token: rotated' '$OUT/devrefresh.out'"
device devcomplete complete
check "device: the URL with the code shown when given" sh -c "[ \"\$(cat '$OUT/devcomplete.status')\" = 0 ] &&
	grep -q -x '(or open https://127.0.0.1/device?code=WDJB-MJHT, which holds the code)' '$OUT/devcomplete.err'"
before=$(wc -l < "$SLOG")
device devslow slow --verbose
polls $before slow > "$OUT/devslow.polls"
check "device: slow_down adds 5 s to the interval" sh -c "[ \"\$(cat '$OUT/devslow.status')\" = 0 ] &&
	grep -q 'slow_down, polling every 6 s' '$OUT/devslow.err' &&
	\"$PYTHON\" -I -c 'import sys; g = [float(x) for x in open(sys.argv[1]).read().split()]; sys.exit(not (len(g) == 3 and g[2] >= 5.8))' '$OUT/devslow.polls'"
device devdenied denied
check "device: access_denied: refused, exit 1, no token file" sh -c "[ \"\$(cat '$OUT/devdenied.status')\" = 1 ] &&
	grep -q 'the sign-in was refused: access_denied: The user said no' '$OUT/devdenied.err' && [ ! -e '$OUT/devdenied.json' ]"
device devdeclined declined
check "device: Microsoft's authorization_declined: refused" sh -c "[ \"\$(cat '$OUT/devdeclined.status')\" = 1 ] &&
	grep -q 'the sign-in was refused: authorization_declined' '$OUT/devdeclined.err'"
device devexpired expired
check "device: expired_token: run again" sh -c "[ \"\$(cat '$OUT/devexpired.status')\" = 1 ] &&
	grep -q 'the provider says the code expired (expired_token): run the command again' '$OUT/devexpired.err'"
device devforever forever
check "device: the code's lifetime ends the wait" sh -c "[ \"\$(cat '$OUT/devforever.status')\" = 1 ] &&
	grep -q 'the code expired after 3 s without a sign-in' '$OUT/devforever.err'"
device devtimeout forever --timeout 1
check "device: --timeout ends the wait sooner" sh -c "[ \"\$(cat '$OUT/devtimeout.status')\" = 1 ] &&
	grep -q 'Waiting for the sign-in (at most 1 s' '$OUT/devtimeout.err' &&
	grep -q 'no sign-in within 1 s' '$OUT/devtimeout.err'"
device devflaky flaky --verbose
check "device: an HTTP 500 is tried again later" sh -c "[ \"\$(cat '$OUT/devflaky.status')\" = 0 ] &&
	grep -q 'poll failed (HTTP 500 from the token endpoint .*), trying again in 2 s' '$OUT/devflaky.err'"
device devpublic publicoff
check "device: AADSTS7000218: public client flows advice" sh -c "[ \"\$(cat '$OUT/devpublic.status')\" = 1 ] &&
	grep -q 'waiting for the sign-in failed: invalid_client: AADSTS7000218' '$OUT/devpublic.err' &&
	grep -q 'Allow public client flows' '$OUT/devpublic.err'"
device devbadanswer badanswer
check "device: an answer without user code: refused" sh -c "[ \"\$(cat '$OUT/devbadanswer.status')\" = 1 ] &&
	grep -q 'the device code request failed: .*has no valid user_code' '$OUT/devbadanswer.err'"
device devbadclient ok --client-id wrong-client
check "device: unknown client (AADSTS50059): the advice once, exit 1" sh -c "[ \"\$(cat '$OUT/devbadclient.status')\" = 1 ] &&
	grep -q 'the device code request failed: invalid_request: AADSTS50059' '$OUT/devbadclient.err' &&
	[ \"\$(grep -c 'check the client ID, the client secret' '$OUT/devbadclient.err')\" = 1 ]"
env -u BROWSER PATH="$OUT/bin-none" XDG_CONFIG_HOME="$OUT/config" "$TOOL" --system-config "$CONFIG" --no-user-config \
	--ca-file "$OUT/cert.pem" device --provider mockdev --tenant forever --token-file "$OUT/devint.json" user@example.org \
	> "$OUT/devint.out" 2> "$OUT/devint.err" &
TOOLPID=$!
sleep 1.5
kill -INT $TOOLPID
wait $TOOLPID
echo $? > "$OUT/devint.status"
check "device: Ctrl-C cancels the wait" sh -c "[ \"\$(cat '$OUT/devint.status')\" = 1 ] && grep -q 'cancelled' '$OUT/devint.err'"
run devnone none bin-none device user@example.org
check "device: a provider without device code: exit 2, login suggested" sh -c "[ \"\$(cat '$OUT/devnone.status')\" = 2 ] &&
	grep -q 'Mock offers no device code sign-in .*use oauth2-tool login' '$OUT/devnone.err'"

# 9. imap-check and smtp-check (task 1.8): mail-server.py, IMAP over TLS and
# SMTP with STARTTLS; the token of other.json (mock provider)
MLOG=$OUT/mail-requests.jsonl
: > "$MLOG"
"$PYTHON" -I "$TESTDIR/mail-server.py" "$MLOG" "$OUT/mail-ports" "$OUT/cert.pem" "$OUT/key.pem" &
MAILSERVER=$!
trap 'kill $SERVER $MAILSERVER 2>/dev/null' EXIT INT TERM
i=0
while [ ! -s "$OUT/mail-ports" ] && [ $i -lt 50 ]; do
	sleep 0.1
	i=$((i + 1))
done
read IMAPPORT SMTPPORT < "$OUT/mail-ports"
# mail_last FIELD...: those fields of the mail server's last exchange
mail_last() {
	"$PYTHON" -I -c 'import json, sys
entries = [json.loads(l) for l in open(sys.argv[1]) if "result" in l]
print(" ".join(str(entries[-1].get(f)) for f in sys.argv[2:]))' "$MLOG" "$@"
}
mailcheck() {
	# mailcheck NAME imap|smtp [ARGS...]
	name=$1
	proto=$2
	shift 2
	if [ "$proto" = imap ]; then
		set -- --server "127.0.0.1:$IMAPPORT" --tls "$@"
	else
		set -- --server "127.0.0.1:$SMTPPORT" "$@"
	fi
	run "$name" none bin-none "$proto-check" --token-file "$OUT/other.json" "$@" user@example.org
}
mailcheck imapx imap
mail_last protocol mechanism user result initial_response > "$OUT/imapx.mail"
check "imap-check: refresh, XOAUTH2 (the provider's default), logged out" sh -c "[ \"\$(cat '$OUT/imapx.status')\" = 0 ] &&
	grep -q '^access token: <redacted' '$OUT/imapx.out' && grep -q -x 'mechanism: XOAUTH2' '$OUT/imapx.out' &&
	grep -q -x 'server: 127.0.0.1:$IMAPPORT (IMAP, TLS)' '$OUT/imapx.out' &&
	grep -q '^authenticated: yes (A002 OK \[CAPABILITY IMAP4rev1\] Authenticated)' '$OUT/imapx.out' &&
	grep -q -x 'logged out: A999 OK LOGOUT completed' '$OUT/imapx.out' &&
	grep -q -x 'front end: MOCK1P01CA0001.EXAMPLE' '$OUT/imapx.out' &&
	[ \"\$(cat '$OUT/imapx.mail')\" = 'imap XOAUTH2 user@example.org accepted True' ] &&
	grep -q '\"command\": \"LOGOUT\"' '$MLOG'"
mailcheck imapb imap --mechanism oauthbearer --verbose
mail_last mechanism result pairs > "$OUT/imapb.mail"
check "imap-check OAUTHBEARER: host and port sent, token redacted in the log" sh -c "[ \"\$(cat '$OUT/imapb.status')\" = 0 ] &&
	[ \"\$(cat '$OUT/imapb.mail')\" = \"OAUTHBEARER accepted {'host': '127.0.0.1', 'port': '$IMAPPORT'}\" ] &&
	grep -q '^> A002 AUTHENTICATE OAUTHBEARER <redacted [0-9]* bytes>\$' '$OUT/imapb.err' &&
	! grep -q S3CR3T '$OUT/imapb.out' '$OUT/imapb.err'"
mailcheck smtpx smtp --verbose
mail_last protocol mechanism user result > "$OUT/smtpx.mail"
check "smtp-check: STARTTLS (port not 465), EHLO [127.0.0.1], XOAUTH2, QUIT" sh -c "[ \"\$(cat '$OUT/smtpx.status')\" = 0 ] &&
	grep -q -x 'server: 127.0.0.1:$SMTPPORT (SMTP, STARTTLS)' '$OUT/smtpx.out' &&
	grep -q -x 'authenticated: yes (235 2.7.0 Accepted)' '$OUT/smtpx.out' &&
	grep -q -x 'logged out: 221 2.0.0 closing connection' '$OUT/smtpx.out' &&
	[ \"\$(grep -c '^> EHLO \[127.0.0.1\]\$' '$OUT/smtpx.err')\" = 2 ] &&
	grep -q '^> AUTH XOAUTH2 <redacted' '$OUT/smtpx.err' && ! grep -q S3CR3T '$OUT/smtpx.out' '$OUT/smtpx.err' &&
	[ \"\$(cat '$OUT/smtpx.mail')\" = 'smtp XOAUTH2 user@example.org accepted' ] && grep -q '\"command\": \"QUIT\"' '$MLOG'"
mailcheck smtpb smtp --mechanism OAUTHBEARER
mail_last mechanism result pairs > "$OUT/smtpb.mail"
check "smtp-check OAUTHBEARER: host and port sent" sh -c "[ \"\$(cat '$OUT/smtpb.status')\" = 0 ] &&
	[ \"\$(cat '$OUT/smtpb.mail')\" = \"OAUTHBEARER accepted {'host': '127.0.0.1', 'port': '$SMTPPORT'}\" ]"
mailcheck imapbad imap --bad-token --mechanism OAUTHBEARER
mail_last result reply_to_challenge > "$OUT/imapbad.mail"
check "imap-check --bad-token OAUTHBEARER: challenge decoded, AQ== sent, NO shown" sh -c "[ \"\$(cat '$OUT/imapbad.status')\" = 1 ] &&
	grep -q -x 'access token: invalid-token-for-the-check (--bad-token)' '$OUT/imapbad.out' &&
	grep -q -x '    error challenge: status invalid_token (the access token is invalid or expired), scope https://mail.example/, schemes bearer' '$OUT/imapbad.err' &&
	grep -q -x '    server: A002 NO \[AUTHENTICATIONFAILED\] Invalid credentials (Failure)' '$OUT/imapbad.err' &&
	[ \"\$(cat '$OUT/imapbad.mail')\" = 'refused AQ==' ]"
mailcheck imapbadx imap --bad-token
check "imap-check --bad-token XOAUTH2: challenge decoded, libcurl's early close said" sh -c "[ \"\$(cat '$OUT/imapbadx.status')\" = 1 ] &&
	grep -q '^    error challenge: status invalid_token' '$OUT/imapbadx.err' &&
	grep -q -x '    server: no final reply (libcurl closed the connection after the challenge)' '$OUT/imapbadx.err'"
mailcheck smtpbad smtp --bad-token --mechanism OAUTHBEARER
check "smtp-check --bad-token OAUTHBEARER: 535 shown" sh -c "[ \"\$(cat '$OUT/smtpbad.status')\" = 1 ] &&
	grep -q '^    error challenge: status invalid_token' '$OUT/smtpbad.err' &&
	grep -q -x '    server: 535 5.7.8 Learn more at https://mock.example/oauth' '$OUT/smtpbad.err'"
run mailnoserver none bin-none imap-check --token-file "$OUT/other.json" user@example.org
check "imap-check: no known server for the provider: exit 2" sh -c "[ \"\$(cat '$OUT/mailnoserver.status')\" = 2 ] &&
	grep -q 'no known IMAP server for mock: name it with --server' '$OUT/mailnoserver.err'"
run mailbadserver none bin-none smtp-check --token-file "$OUT/other.json" --server 127.0.0.1:0 user@example.org
run mailbadmech none bin-none smtp-check --mechanism PLAIN user@example.org
check "imap-check: a wrong --server or --mechanism: exit 2" sh -c "[ \"\$(cat '$OUT/mailbadserver.status')\" = 2 ] &&
	grep -q -- '--server needs HOST or HOST:PORT' '$OUT/mailbadserver.err' && [ \"\$(cat '$OUT/mailbadmech.status')\" = 2 ] &&
	grep -q -- '--mechanism needs XOAUTH2 or OAUTHBEARER' '$OUT/mailbadmech.err'"

echo
if [ $failed -ne 0 ]; then
	echo "login-tests: $failed checks failed (details in $OUT)"
	exit 1
fi
echo "login-tests: all checks passed (details in $OUT)"
