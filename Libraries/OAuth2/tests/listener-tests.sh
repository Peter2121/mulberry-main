#!/bin/sh
#
# Tests of the loopback listener (OAUTH2-PLAN.md, task 1.5), with curl in
# the role of the browser: the code, a wrong state, the provider's error,
# requests to ignore, the Host check, IPv4 and IPv6 for "localhost", the
# time limit, the cancellation, no access from a non-loopback address, and
# a log without the code.
#
# Usage: listener-tests.sh DRIVER OUTDIR

set -u

if [ $# -ne 2 ]; then
	echo "usage: $0 DRIVER OUTDIR" >&2
	exit 2
fi
DRIVER=$1
OUT=$2
rm -rf "$OUT"
mkdir -p "$OUT" || exit 1

for tool in curl python3; do
	command -v $tool > /dev/null 2>&1 || {
		echo "listener-tests: $tool is needed (pkg install $tool)" >&2
		exit 1
	}
done

LOG=$OUT/listener.log
: > "$LOG"
STATE=st-0123456789abcdefghijk
CODE=S3CR3T-code-1

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

# start NAME REDIRECT [OPTIONS...]: starts the driver in the background;
# sets PID, PORT and URI
start() {
	name=$1
	redirect=$2
	shift 2
	rm -f "$OUT/$name.ready"
	"$DRIVER" --log "$LOG" --app TestApp --ready "$OUT/$name.ready" "$@" "$redirect" "$STATE" \
		> "$OUT/$name.out" 2> "$OUT/$name.err" &
	PID=$!
	i=0
	while [ ! -s "$OUT/$name.ready" ] && [ $i -lt 50 ]; do
		sleep 0.1
		i=$((i + 1))
	done
	read PORT URI < "$OUT/$name.ready" || { PORT=0; URI=; }
}

# finish NAME: waits for the driver; NAME.status
finish() {
	wait $PID
	echo $? > "$OUT/$1.status"
}

# get FILE URL [CURL OPTIONS...]: prints the HTTP status (000: no answer);
# body in FILE.body, headers in FILE.head
get() {
	file=$1
	url=$2
	shift 2
	curl -g -s --noproxy '*' --max-time 5 -o "$OUT/$file.body" -D "$OUT/$file.head" \
		-w '%{http_code}' "$@" "$url" 2> /dev/null
}

# The code, after requests to ignore
start main http://127.0.0.1
check "redirect URI is http://127.0.0.1:PORT, no path" sh -c "echo '$URI' | grep -q -x 'http://127.0.0.1:[0-9]*'"
BASE=http://127.0.0.1:$PORT
check "favicon: 404, ignored" [ "$(get favicon "$BASE/favicon.ico")" = 404 ]
check "other path: 404" [ "$(get other "$BASE/other?code=$CODE&state=$STATE")" = 404 ]
check "POST: 405" [ "$(get post "$BASE/?code=$CODE&state=$STATE" -X POST)" = 405 ]
check "HEAD: 405" [ "$(get head "$BASE/?code=$CODE&state=$STATE" -I)" = 405 ]
check "wrong state: 400" [ "$(get wrong "$BASE/?code=$CODE&state=other")" = 400 ]
check "no state: 400" [ "$(get nostate "$BASE/?code=$CODE")" = 400 ]
check "neither code nor error: 400" [ "$(get empty "$BASE/?state=$STATE")" = 400 ]
check "code given twice: 400" [ "$(get twice "$BASE/?code=$CODE&state=$STATE&code=S3CR3T-other")" = 400 ]
check "foreign Host header (DNS rebinding): 400" \
	[ "$(get host "$BASE/?code=$CODE&state=$STATE" -H 'Host: attacker.example')" = 400 ]
check "Host localhost for a 127.0.0.1 redirect: 400" \
	[ "$(get host2 "$BASE/?code=$CODE&state=$STATE" -H "Host: localhost:$PORT")" = 400 ]
big=$(python3 -I -c 'print("x" * 9000)')
check "request over 8 KiB: 431" [ "$(get big "$BASE/" -H "X-Big: $big")" = 431 ]
# A connection that sends nothing (a browser's preconnect) must not block
python3 -I -c "import socket, time; s = socket.create_connection(('127.0.0.1', $PORT)); time.sleep(4)" &
IDLE=$!
sleep 0.3
started=$(date +%s)
check "the code: 200 and the page" sh -c "[ \"\$(curl -g -s --noproxy '*' --max-time 5 -o '$OUT/code.body' -D '$OUT/code.head' \
	-w '%{http_code}' '$BASE/?code=$CODE&state=$STATE&scope=openid+email')\" = 200 ] &&
	grep -q 'You can close this window and return to TestApp.' '$OUT/code.body'"
elapsed=$(($(date +%s) - started))
check "an idle connection did not delay the answer" [ $elapsed -lt 2 ]
finish main
kill $IDLE 2> /dev/null
check "driver: code received" sh -c "[ \"\$(cat '$OUT/main.status')\" = 0 ] && grep -q -x 'result: code' '$OUT/main.out' &&
	grep -q -x 'code: <redacted 13 bytes>' '$OUT/main.out' && grep -q -x 'params: code state scope' '$OUT/main.out'"
check "page headers: no-store, no-referrer, nosniff, CSP, close" sh -c "
	grep -q -i '^Cache-Control: no-store' '$OUT/code.head' && grep -q -i '^Referrer-Policy: no-referrer' '$OUT/code.head' &&
	grep -q -i '^X-Content-Type-Options: nosniff' '$OUT/code.head' &&
	grep -q -i \"^Content-Security-Policy: default-src 'none'\" '$OUT/code.head' && grep -q -i '^Connection: close' '$OUT/code.head' &&
	grep -q -i '^Content-Type: text/html; charset=utf-8' '$OUT/code.head'"
check "the page does not contain the code" sh -c "! grep -q S3CR3T '$OUT/code.body'"
check "the listener is closed after the code" [ "$(get after "$BASE/?code=$CODE&state=$STATE")" = 000 ]

# The provider's error (with HTML in the description, escaped on the page)
start error http://127.0.0.1
check "error redirect: 200 and the page" sh -c "[ \"\$(curl -g -s --noproxy '*' --max-time 5 -o '$OUT/error.body' -w '%{http_code}' \
	'http://127.0.0.1:$PORT/?error=access_denied&error_description=The+user+said+%3Cb%3Eno%3C%2Fb%3E&state=$STATE')\" = 200 ] &&
	grep -q 'sign-in not completed' '$OUT/error.body' && grep -q 'access_denied: The user said &lt;b&gt;no&lt;/b&gt;' '$OUT/error.body'"
finish error
check "driver: the provider's error" sh -c "[ \"\$(cat '$OUT/error.status')\" = 1 ] && grep -q -x 'result: error' '$OUT/error.out' &&
	grep -q -x 'error: access_denied' '$OUT/error.out' && grep -q -x 'error_description: The user said <b>no</b>' '$OUT/error.out'"

# A redirect with a path
start path http://127.0.0.1/oauth2/callback
check "redirect URI keeps the path" [ "$URI" = "http://127.0.0.1:$PORT/oauth2/callback" ]
check "path /: 404" [ "$(get root "http://127.0.0.1:$PORT/?code=$CODE&state=$STATE")" = 404 ]
check "path /oauth2/callback: 200" [ "$(get path "http://127.0.0.1:$PORT/oauth2/callback?code=$CODE&state=$STATE")" = 200 ]
finish path
check "driver: code received with a path" grep -q -x 'result: code' "$OUT/path.out"

# localhost: the same port on 127.0.0.1 and ::1
for family in 4 6; do
	start localhost$family http://localhost/cb
	if [ $family = 4 ]; then host=127.0.0.1; else host='[::1]'; fi
	check "localhost: redirect URI http://localhost:PORT/cb" [ "$URI" = "http://localhost:$PORT/cb" ]
	check "localhost, reached on $host: 200" \
		[ "$(get localhost$family "http://$host:$PORT/cb?code=$CODE&state=$STATE" -H "Host: localhost:$PORT")" = 200 ]
	finish localhost$family
	check "driver: code received through $host" grep -q -x 'result: code' "$OUT/localhost$family.out"
done
check "log: localhost listens on both families" grep -q "^\* listening on 127.0.0.1:[0-9]* and \[::1\]:[0-9]* for http://localhost:" "$LOG"

# [::1]: IPv6 only
start ipv6 'http://[::1]'
check "[::1]: 127.0.0.1 is not listened on" [ "$(get ipv6-v4 "http://127.0.0.1:$PORT/?code=$CODE&state=$STATE")" = 000 ]
check "[::1]: reached on [::1]" [ "$(get ipv6 "http://[::1]:$PORT/?code=$CODE&state=$STATE")" = 200 ]
finish ipv6

# Not reachable from a non-loopback address of this machine
ADDRESS=$(ifconfig 2> /dev/null | awk '/inet / { print $2 }' | grep -v '^127\.' | head -n 1)
if [ -n "$ADDRESS" ]; then
	start remote http://127.0.0.1
	check "not reachable on $ADDRESS" [ "$(get remote "http://$ADDRESS:$PORT/?code=$CODE&state=$STATE" --connect-timeout 2)" = 000 ]
	get remote-end "http://127.0.0.1:$PORT/?code=$CODE&state=$STATE" > /dev/null
	finish remote
else
	echo "skipped no non-loopback IPv4 address: reachability not tested"
fi

# Time limit and cancellation
started=$(date +%s)
start timeout http://127.0.0.1 --timeout 2
finish timeout
elapsed=$(($(date +%s) - started))
check "time limit (2 s)" sh -c "grep -q -x 'result: timeout' '$OUT/timeout.out' && [ \"\$(cat '$OUT/timeout.status')\" = 1 ] &&
	[ $elapsed -ge 2 ] && [ $elapsed -le 4 ]"
check "closed after the time limit" [ "$(get timeout-after "http://127.0.0.1:$PORT/?code=$CODE&state=$STATE")" = 000 ]

started=$(date +%s)
start cancel http://127.0.0.1 --timeout 60 --cancel-after 1
finish cancel
elapsed=$(($(date +%s) - started))
check "cancellation from another thread" sh -c "grep -q -x 'result: cancelled' '$OUT/cancel.out' && [ $elapsed -le 3 ]"

# Refused redirects
"$DRIVER" http://example.org/cb "$STATE" > "$OUT/bad.out" 2>&1
status=$?
check "a non-loopback redirect is refused (exit status 2)" sh -c "[ $status = 2 ] && grep -q 'not a loopback redirect' '$OUT/bad.out'"
"$DRIVER" http://127.0.0.1:8080 "$STATE" > "$OUT/port.out" 2>&1
status=$?
check "a redirect with a port is refused" sh -c "[ $status = 2 ] && grep -q 'not a loopback redirect' '$OUT/port.out'"

# The log
check "log: requests with code and state redacted" \
	grep -q -F "< GET /?code=<redacted 13 bytes>&state=<redacted 24 bytes>&scope=openid+email HTTP/1.1" "$LOG"
check "log: why a request was ignored" grep -q -F "> 400 Bad Request (wrong state (not the sign-in this application started))" "$LOG"
check "log: no code at all" sh -c "! grep -q S3CR3T '$LOG'"
check "output: no code without --show-code" sh -c "! cat '$OUT'/*.out '$OUT'/*.err | grep -q S3CR3T"
start show http://127.0.0.1 --show-code
get show "http://127.0.0.1:$PORT/?code=$CODE&state=$STATE" > /dev/null
finish show
check "--show-code prints the code" grep -q -x "code: $CODE" "$OUT/show.out"

echo
if [ $failed -ne 0 ]; then
	echo "listener-tests: $failed checks failed (details in $OUT)"
	exit 1
fi
echo "listener-tests: all checks passed (details in $OUT)"
