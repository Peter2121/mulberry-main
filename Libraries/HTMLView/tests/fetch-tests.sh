#!/bin/sh
#
# Tests of the remote image downloads (htmlview::Fetcher) against a local
# HTTP server (fetch-server.py, Python 3): what is loaded, what is refused
# and why, and what the requests carry.  Needs no network beyond 127.0.0.1.
#
# Usage: fetch-tests.sh HTML2PNG TESTDIR OUTDIR

set -u

if [ $# -ne 3 ]; then
	echo "usage: $0 HTML2PNG TESTDIR OUTDIR" >&2
	exit 2
fi
TOOL=$1
TESTDIR=$2
OUT=$3/fetch
rm -rf "$OUT"
mkdir -p "$OUT" || exit 1

LOG=$OUT/server-requests.jsonl
PORTFILE=$OUT/port
: > "$LOG"
python3 -I "$TESTDIR/fetch-server.py" "$TESTDIR/images" "$LOG" "$PORTFILE" &
SERVER=$!
trap 'kill $SERVER 2>/dev/null' EXIT INT TERM

i=0
while [ ! -s "$PORTFILE" ] && [ $i -lt 50 ]; do
	sleep 0.1
	i=$((i + 1))
done
[ -s "$PORTFILE" ] || { echo "fetch-tests: the test server did not start" >&2; exit 1; }
PORT=$(cat "$PORTFILE")
BASE=http://127.0.0.1:$PORT

# One image per case, in this order
cat > "$OUT/page.html" <<EOF
<html><body>
<img src="$BASE/img/logo.png">
<img src="$BASE/redirect/3/photo.jpg">
<img src="$BASE/redirect/7/badge.gif">
<img src="$BASE/to-file">
<img src="$BASE/to-ftp">
<img src="$BASE/big">
<img src="$BASE/big-unannounced">
<img src="$BASE/slow">
<img src="$BASE/auth">
<img src="$BASE/missing">
<img src="$BASE/gzip/alpha.webp">
<img src="$BASE/html">
<img src="http://user:secret@127.0.0.1:$PORT/img/triangle.png">
<img src="ftp://127.0.0.1/image.png">
<img src="$BASE/img/cmyk.jpg">
</body></html>
EOF

"$TOOL" -t -L -V -T 2 -w 700 "$OUT/page.html" "$OUT/page.png" > "$OUT/stdout.txt" 2> "$OUT/stderr.txt"
echo "html2png: exit status $?, $(tail -n 1 "$OUT/stdout.txt")"

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

check "4 of 15 remote images loaded (plain, 3 redirects, gzip, after a cookie)" \
	grep -q "remote images: 4 of 15 loaded" "$OUT/stdout.txt"
check "more than 5 redirects refused" \
	sh -c "grep -F 'remote image failed: $BASE/redirect/7/badge.gif:' '$OUT/stderr.txt' | grep -q -i redirect"
check "redirect to file: refused" \
	sh -c "grep -F 'remote image failed: $BASE/to-file:' '$OUT/stderr.txt' | grep -q -i -E 'protocol|scheme'"
check "redirect to ftp: refused" \
	sh -c "grep -F 'remote image failed: $BASE/to-ftp:' '$OUT/stderr.txt' | grep -q -i -E 'protocol|scheme'"
check "announced size over 16 MB refused" \
	sh -c "grep -F 'remote image failed: $BASE/big:' '$OUT/stderr.txt' | grep -q 'size limit'"
check "unannounced size over 16 MB stopped while downloading" \
	sh -c "grep -F 'remote image failed: $BASE/big-unannounced:' '$OUT/stderr.txt' | grep -q 'size limit'"
check "time limit (2 s) applied" \
	sh -c "grep -F 'remote image failed: $BASE/slow:' '$OUT/stderr.txt' | grep -q -i 'timeout'"
check "401: no login, fails" sh -c "grep -F 'remote image failed: $BASE/auth:' '$OUT/stderr.txt' | grep -q 'HTTP 401'"
check "404 fails" sh -c "grep -F 'remote image failed: $BASE/missing:' '$OUT/stderr.txt' | grep -q 'HTTP 404'"
check "HTML instead of an image: downloaded, not shown" \
	grep -q -F "not shown (format or size): $BASE/html" "$OUT/stderr.txt"
check "user name in the URL refused" \
	grep -q -F "remote image failed: http://user:secret@127.0.0.1:$PORT/img/triangle.png:" "$OUT/stderr.txt"
check "ftp: URL refused" \
	grep -q -F "remote image failed: ftp://127.0.0.1/image.png: not an http or https URL" "$OUT/stderr.txt"

check "the URL with a user name never reached the server" \
	sh -c "! grep -q 'triangle.png' '$LOG'"
check "no Cookie header sent (a cookie was set by the first image)" \
	sh -c "! grep -q -i '\"cookie\"' '$LOG'"
check "no Referer header sent" \
	sh -c "! grep -q -i '\"referer\"' '$LOG'"
check "no Authorization header sent" \
	sh -c "! grep -q -i '\"authorization\"' '$LOG'"
check "neutral User-Agent" \
	sh -c "grep -q '\"User-Agent\": \"Mozilla/5.0\"' '$LOG' && ! grep -q -i 'curl' '$LOG'"
check "Accept asks for images" \
	grep -q '"Accept": "image/' "$LOG"

# At most 200 images per load: 201 distinct URLs
{
	echo "<html><body>"
	i=0
	while [ $i -lt 201 ]; do
		echo "<img src=\"$BASE/img/logo.png?n=$i\" width=\"10\" height=\"10\">"
		i=$((i + 1))
	done
	echo "</body></html>"
} > "$OUT/many.html"
"$TOOL" -t -L "$OUT/many.html" "$OUT/many.png" > "$OUT/many-stdout.txt" 2> "$OUT/many-stderr.txt"
check "at most 200 images per load" \
	sh -c "grep -q 'remote images: 200 of 201 loaded' '$OUT/many-stdout.txt' && grep -q 'too many images' '$OUT/many-stderr.txt'"

check "network log: request headers" grep -q '^> GET /img/logo.png' "$OUT/stderr.txt"
check "network log: response headers" grep -q '^< HTTP/1.1 200' "$OUT/stderr.txt"
check "network log: no image data" sh -c "! grep -q -a 'PNG' '$OUT/stderr.txt'"

echo
if [ $failed -ne 0 ]; then
	echo "$failed checks failed (details in $OUT)"
	exit 1
fi
echo "all checks passed (details in $OUT)"
