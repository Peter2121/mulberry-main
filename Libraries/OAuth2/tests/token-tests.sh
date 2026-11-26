#!/bin/sh
#
# Tests of the token client (OAUTH2-PLAN.md, task 1.4) against a local mock
# token endpoint (token-server.py, Python 3): success, rotation, the
# providers' errors, HTTP and JSON failures, time and size limits, TLS
# verification, and a log without secrets.  Needs no network beyond
# 127.0.0.1.
#
# Usage: token-tests.sh DRIVER TESTDIR OUTDIR

set -u

if [ $# -ne 3 ]; then
	echo "usage: $0 DRIVER TESTDIR OUTDIR" >&2
	exit 2
fi
DRIVER=$1
TESTDIR=$2
OUT=$3
rm -rf "$OUT"
mkdir -p "$OUT" || exit 1

command -v python3 > /dev/null 2>&1 || {
	echo "token-tests: python3 is needed for the mock server (pkg install python3)" >&2
	exit 1
}

# A self-signed certificate for the TLS case (no CA trusts it)
TLS=yes
if ! openssl req -x509 -newkey rsa:2048 -nodes -days 1 -subj /CN=127.0.0.1 \
		-addext subjectAltName=IP:127.0.0.1 \
		-keyout "$OUT/key.pem" -out "$OUT/cert.pem" > "$OUT/openssl.txt" 2>&1; then
	TLS=no
	echo "token-tests: openssl could not make a certificate: TLS case skipped" >&2
fi

SLOG=$OUT/server-requests.jsonl
LOG=$OUT/client.log
PORTFILE=$OUT/port
: > "$SLOG"
: > "$LOG"
if [ $TLS = yes ]; then
	python3 -I "$TESTDIR/token-server.py" "$SLOG" "$PORTFILE" "$OUT/cert.pem" "$OUT/key.pem" &
else
	python3 -I "$TESTDIR/token-server.py" "$SLOG" "$PORTFILE" &
fi
SERVER=$!
trap 'kill $SERVER 2>/dev/null' EXIT INT TERM

i=0
while [ ! -s "$PORTFILE" ] && [ $i -lt 50 ]; do
	sleep 0.1
	i=$((i + 1))
done
[ -s "$PORTFILE" ] || { echo "token-tests: the mock server did not start" >&2; exit 1; }
read PORT TLSPORT < "$PORTFILE"
BASE=http://127.0.0.1:$PORT

REDIRECT=http://127.0.0.1/callback
VERIFIER=S3CR3T-verifier-0123456789abcdefghijklmnopq
SECRET=S3CR3T-client-secret
TOKENFILE=$OUT/refresh-token

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

# run NAME ARGS...: the driver with the shared log; NAME.out, NAME.err and
# NAME.status in OUT
run() {
	name=$1
	shift
	"$DRIVER" --log "$LOG" "$@" > "$OUT/$name.out" 2> "$OUT/$name.err"
	echo $? > "$OUT/$name.status"
}

# requests "FILTER"...: server log lines containing every fixed string
requests() {
	result=$(cat "$SLOG")
	for filter in "$@"; do
		result=$(printf '%s\n' "$result" | grep -F -- "$filter")
	done
	printf '%s\n' "$result" | grep -c . || true
}

# Authorization-code exchange
run exchange --verifier "$VERIFIER" --save "$TOKENFILE" "$BASE/token" exchange S3CR3T-code-1 "$REDIRECT"
check "exchange: success" sh -c "[ \"\$(cat '$OUT/exchange.status')\" = 0 ]"
check "exchange: token type, expiry, scope" sh -c "grep -q -x 'token_type: Bearer' '$OUT/exchange.out' &&
	grep -q -x 'expires_in: 3599' '$OUT/exchange.out' &&
	grep -q -x 'scope: openid email https://mail.example/' '$OUT/exchange.out'"
check "exchange: tokens redacted in the output" sh -c "grep -q -x 'access_token: <redacted 15 bytes>' '$OUT/exchange.out' &&
	grep -q -x 'refresh_token: <redacted 16 bytes>' '$OUT/exchange.out' &&
	grep -q '^id_token: <redacted' '$OUT/exchange.out'"
check "exchange: refresh token saved, mode 0600" sh -c "[ \"\$(cat '$TOKENFILE')\" = S3CR3T-refresh-1 ] &&
	[ \"\$(ls -l '$TOKENFILE' | cut -c1-10)\" = -rw------- ]"
check "exchange: form has grant_type, code, redirect_uri, code_verifier, client_id" \
	sh -c "[ \"\$(grep -F '\"grant_type\": \"authorization_code\", \"code\": \"S3CR3T-code-1\", \"redirect_uri\": \"$REDIRECT\", \"code_verifier\": \"$VERIFIER\", \"client_id\": \"test-client\"}' '$SLOG' | grep -c .)\" = 1 ]"
check "exchange: no client_secret sent when there is none" \
	sh -c "! grep -F '\"authorization_code\"' '$SLOG' | grep -q client_secret"

run reuse --verifier "$VERIFIER" "$BASE/token" exchange S3CR3T-code-1 "$REDIRECT"
check "code used twice: invalid_grant, sign in again" sh -c "[ \"\$(cat '$OUT/reuse.status')\" = 1 ] &&
	grep -q -x 'kind: oauth' '$OUT/reuse.out' && grep -q -x 'http_status: 400' '$OUT/reuse.out' &&
	grep -q -x 'error: invalid_grant' '$OUT/reuse.out' && grep -q -x 'requires_sign_in: yes' '$OUT/reuse.out'"

# Refresh with rotation, then without
run rotate --client-secret "$SECRET" --scope "openid email" "$BASE/token" refresh "$TOKENFILE"
check "refresh: success, rotated" sh -c "grep -q -x 'result: ok' '$OUT/rotate.out' &&
	grep -q -x 'refresh_token: rotated (file updated)' '$OUT/rotate.out'"
check "refresh: the new refresh token replaced the old one in the file (0600)" \
	sh -c "[ \"\$(cat '$TOKENFILE')\" = S3CR3T-refresh-2 ] && [ \"\$(ls -l '$TOKENFILE' | cut -c1-10)\" = -rw------- ] &&
	[ ! -e '$TOKENFILE.tmp' ]"
check "refresh: form has the old refresh token, client_secret and scope" \
	sh -c "grep -F '\"refresh_token\": \"S3CR3T-refresh-1\"' '$SLOG' | grep -F '\"client_secret\": \"$SECRET\"' |
	grep -q -F '\"scope\": \"openid email\"'"

run stable "$BASE/token" refresh "$TOKENFILE"
check "refresh without a new refresh token: the old one is kept" sh -c "grep -q -x 'result: ok' '$OUT/stable.out' &&
	grep -q -x 'refresh_token: unchanged' '$OUT/stable.out' && [ \"\$(cat '$TOKENFILE')\" = S3CR3T-refresh-2 ]"
check "refresh: the rotated token was the one sent, without scope when none is given" \
	sh -c "grep -F '\"refresh_token\": \"S3CR3T-refresh-2\"' '$SLOG' | head -n 1 | grep -v -q '\"scope\"'"

echo S3CR3T-refresh-1 > "$OUT/old-token"
run old "$BASE/token" refresh "$OUT/old-token"
check "refresh with the rotated-out token: invalid_grant" sh -c "grep -q -x 'error: invalid_grant' '$OUT/old.out' &&
	grep -q -x 'requires_sign_in: yes' '$OUT/old.out' && [ \"\$(cat '$OUT/old-token')\" = S3CR3T-refresh-1 ]"

run wrong-secret --client-secret S3CR3T-wrong "$BASE/token" refresh "$TOKENFILE"
check "wrong client secret: invalid_client (HTTP 401), not a sign-in problem" \
	sh -c "grep -q -x 'error: invalid_client' '$OUT/wrong-secret.out' && grep -q -x 'http_status: 401' '$OUT/wrong-secret.out' &&
	grep -q -x 'client_problem: yes' '$OUT/wrong-secret.out' && grep -q -x 'requires_sign_in: no' '$OUT/wrong-secret.out'"

# The providers' errors
run rapt "$BASE/google-rapt" refresh "$TOKENFILE"
check "Google invalid_rapt: error_subtype read" sh -c "grep -q -x 'error: invalid_grant' '$OUT/rapt.out' &&
	grep -q -x 'subtype: invalid_rapt' '$OUT/rapt.out' &&
	grep -q -x 'message: invalid_grant (invalid_rapt): reauth related error (invalid_rapt)' '$OUT/rapt.out'"

run aad-client "$BASE/ms-invalid-client" refresh "$TOKENFILE"
check "Microsoft AADSTS7000215: code, correlation ID, first line of the description" \
	sh -c "grep -q -x 'aadsts: 7000215' '$OUT/aad-client.out' &&
	grep -q -x 'correlation_id: aaaa0000-bb11-2222-33cc-444444dddddd' '$OUT/aad-client.out' &&
	grep -q '^message: invalid_client: AADSTS7000215: Invalid client secret provided\\. .*client secret ID\\.\$' '$OUT/aad-client.out' &&
	! grep -q 'Trace ID' '$OUT/aad-client.out'"

run aad-consent "$BASE/ms-consent" refresh "$TOKENFILE"
check "Microsoft AADSTS65001: code and suberror" sh -c "grep -q -x 'aadsts: 65001' '$OUT/aad-consent.out' &&
	grep -q -x 'subtype: consent_required' '$OUT/aad-consent.out'"

run object-error "$BASE/object-error" refresh "$TOKENFILE"
check "error as an object (reported for Yahoo): errorId and message" \
	sh -c "grep -q -x 'error: INVALID_INPUT' '$OUT/object-error.out' &&
	grep -q -x 'message: INVALID_INPUT: client request is not acceptable or not supported' '$OUT/object-error.out'"

# HTTP and JSON failures
run http-500 "$BASE/http-500" refresh "$TOKENFILE"
check "HTTP 500 with an HTML page" sh -c "grep -q -x 'kind: http' '$OUT/http-500.out' &&
	grep -q -x 'http_status: 500' '$OUT/http-500.out' &&
	grep -q '^message: HTTP 500 from the token endpoint (text/html, [0-9]* bytes)\$' '$OUT/http-500.out'"

run malformed "$BASE/malformed" refresh "$TOKENFILE"
check "malformed JSON" sh -c "grep -q -x 'kind: protocol' '$OUT/malformed.out' &&
	grep -q 'not a JSON object' '$OUT/malformed.out'"

run not-object "$BASE/not-object" refresh "$TOKENFILE"
check "JSON that is not an object" sh -c "grep -q -x 'kind: protocol' '$OUT/not-object.out'"

run no-access "$BASE/no-access-token" refresh "$TOKENFILE"
check "no access_token" sh -c "grep -q -x 'message: the token endpoint'\\''s answer has no access_token' '$OUT/no-access.out'"

run mac "$BASE/mac-token" refresh "$TOKENFILE"
check "token_type other than Bearer refused" sh -c "grep -q -x 'kind: protocol' '$OUT/mac.out' &&
	grep -q 'unsupported token_type \"mac\"' '$OUT/mac.out'"

run string-expires "$BASE/string-expires" refresh "$TOKENFILE"
check "expires_in as a string; scope not given; refresh_token_expires_in" \
	sh -c "grep -q -x 'expires_in: 3599' '$OUT/string-expires.out' && grep -q -x 'scope: (not given)' '$OUT/string-expires.out' &&
	grep -q -x 'refresh_token_expires_in: 604799' '$OUT/string-expires.out'"

run bad-expires "$BASE/bad-expires" refresh "$TOKENFILE"
check "expires_in not a number: refused" sh -c "grep -q -x 'kind: protocol' '$OUT/bad-expires.out'"

# Limits
start=$(date +%s)
run slow --timeout 2 "$BASE/slow" refresh "$TOKENFILE"
elapsed=$(($(date +%s) - start))
check "slow server: time limit (2 s) applied" sh -c "grep -q -x 'kind: timeout' '$OUT/slow.out' && [ $elapsed -lt 4 ]"

# Redirects: 307 and 308 to the same host or a listed domain, at most 3
for code in 307 308; do
	before=$(requests '"path": "/token"' '"S3CR3T-refresh-2"')
	run redirect-$code "$BASE/redirect-$code" refresh "$TOKENFILE"
	after=$(requests '"path": "/token"' '"S3CR3T-refresh-2"')
	check "redirect $code to the same host: followed with the same form" \
		sh -c "grep -q -x 'result: ok' '$OUT/redirect-$code.out' && [ $after = $((before + 1)) ] &&
		grep -q -F '* following redirect $code to $BASE/token' '$LOG'"
done

before=$(requests '"path": "/token"')
run redirect-302 "$BASE/redirect-302" refresh "$TOKENFILE"
after=$(requests '"path": "/token"')
check "redirect 302: not followed (it would drop the form)" sh -c "grep -q -x 'kind: http' '$OUT/redirect-302.out' &&
	grep -q -x 'http_status: 302' '$OUT/redirect-302.out' &&
	grep -q 'only 307 and 308 keep the request' '$OUT/redirect-302.out' && [ $before = $after ]"

before=$(requests '"path": "/token"')
run redirect-other "$BASE/redirect-localhost" refresh "$TOKENFILE"
after=$(requests '"path": "/token"')
check "redirect to another host not in redirect_domains: refused, nothing sent" \
	sh -c "grep -q -x 'kind: http' '$OUT/redirect-other.out' &&
	grep -q 'host localhost is neither' '$OUT/redirect-other.out' && [ $before = $after ]"

run redirect-listed --redirect-domain localhost "$BASE/redirect-localhost" refresh "$TOKENFILE"
check "redirect to a host in redirect_domains: followed" grep -q -x 'result: ok' "$OUT/redirect-listed.out"

before=$(requests '"path": "/redirect-loop"')
run redirect-loop "$BASE/redirect-loop" refresh "$TOKENFILE"
after=$(requests '"path": "/redirect-loop"')
check "redirect loop: stopped after 3 redirects" sh -c "grep -q 'more than 3 redirects' '$OUT/redirect-loop.out' &&
	[ $after = $((before + 4)) ]"

run redirect-none "$BASE/redirect-none" refresh "$TOKENFILE"
check "307 without a target: an HTTP error" sh -c "grep -q -x 'kind: http' '$OUT/redirect-none.out' &&
	grep -q -x 'http_status: 307' '$OUT/redirect-none.out'"

run big "$BASE/big" refresh "$TOKENFILE"
check "answer over 1 MiB refused" sh -c "grep -q -x 'kind: protocol' '$OUT/big.out' &&
	grep -q 'larger than the limit' '$OUT/big.out'"

# Transport and configuration
if [ $TLS = yes ]; then
	before=$(grep -c . "$SLOG")
	run tls-untrusted "https://127.0.0.1:$TLSPORT/token" refresh "$TOKENFILE"
	after=$(grep -c . "$SLOG")
	check "TLS: an untrusted certificate is refused" sh -c "grep -q -x 'kind: network' '$OUT/tls-untrusted.out' &&
		grep -q -i 'certificate' '$OUT/tls-untrusted.out'"
	check "TLS: no request reached the server" [ "$before" = "$after" ]
fi

run https-plain "https://127.0.0.1:$PORT/token" refresh "$TOKENFILE"
check "https to a plain HTTP server: TLS failure" sh -c "grep -q -x 'kind: network' '$OUT/https-plain.out'"

run not-https "http://192.0.2.1/token" refresh "$TOKENFILE"
check "http to another host than loopback: refused before sending" sh -c "grep -q -x 'kind: config' '$OUT/not-https.out' &&
	grep -q 'not an https URL' '$OUT/not-https.out'"

run user-in-url "https://user:pw@127.0.0.1:$PORT/token" refresh "$TOKENFILE"
check "user name in the URL: refused" sh -c "grep -q -x 'kind: config' '$OUT/user-in-url.out'"

run no-client --client-id "" "$BASE/token" refresh "$TOKENFILE"
check "no client ID: refused before sending" sh -c "grep -q -x 'kind: config' '$OUT/no-client.out' &&
	grep -q 'no client ID' '$OUT/no-client.out'"

run refused "http://127.0.0.1:1/token" refresh "$TOKENFILE"
check "connection refused: network error" sh -c "grep -q -x 'kind: network' '$OUT/refused.out'"

# What the requests carried
check "the secrets were sent (the next checks are not empty)" grep -q S3CR3T "$SLOG"
check "no Cookie header sent (cookies were set)" sh -c "! grep -q -i '\"cookie\"' '$SLOG'"
check "no Authorization header sent" sh -c "! grep -q -i '\"authorization\"' '$SLOG'"
check "neutral User-Agent" sh -c "grep -q '\"User-Agent\": \"Mozilla/5.0\"' '$SLOG' && ! grep -q -i 'curl/' '$SLOG'"
check "Accept: application/json" grep -q '"Accept": "application/json"' "$SLOG"

# The log
check "log: request line and headers" grep -q '^> POST /token HTTP/1.1$' "$LOG"
check "log: answer status and headers" grep -q '^< HTTP/1.1 200' "$LOG"
check "log: the form, redacted" grep -q '^> grant_type=authorization_code&code=<redacted 13 bytes>&redirect_uri=http%3A%2F%2F127.0.0.1%2Fcallback&code_verifier=<redacted 43 bytes>&client_id=test-client$' "$LOG"
check "log: client secret redacted" grep -q 'client_secret=<redacted 20 bytes>' "$LOG"
check "log: the JSON answer, redacted" grep -q '^< {"access_token":"<redacted 15 bytes>","token_type":"Bearer"' "$LOG"
check "log: Set-Cookie redacted" grep -q -i '^< Set-Cookie: <redacted' "$LOG"
check "log: summary line" grep -q '^\* token received: Bearer, expires in 3599 s, refresh token <redacted 16 bytes>' "$LOG"
check "log: no secret at all" sh -c "! grep -q S3CR3T '$LOG'"
check "output: no secret without --show-token" sh -c "! cat '$OUT'/*.out '$OUT'/*.err | grep -q S3CR3T"

# The check above must be able to fail: --show-token prints them
"$DRIVER" --show-token "$BASE/string-expires" refresh "$TOKENFILE" > "$OUT/show.txt" 2>&1
check "--show-token prints the access token" grep -q '^access_token: S3CR3T-access-' "$OUT/show.txt"

echo
if [ $failed -ne 0 ]; then
	echo "token-tests: $failed checks failed (details in $OUT)"
	exit 1
fi
echo "token-tests: all checks passed (details in $OUT)"
