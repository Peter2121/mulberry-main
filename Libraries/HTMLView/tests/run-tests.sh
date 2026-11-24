#!/bin/sh
#
# Renders the HTML viewer's test pages with html2png.
#
# Usage: run-tests.sh HTML2PNG TESTDIR OUTDIR
#
# Renders TESTDIR/*.html and generated stress pages (hostile input: deep
# nesting, huge tables, ...) into OUTDIR/<name>.png, with timings and memory
# use in OUTDIR/<name>.txt.  A page fails when html2png crashes, hangs
# (CPU time limit) or exits with an error; a page rejected by the viewer's
# limits (exit status 3) is a success for the stress pages.  The PNG files
# are for looking at: nothing compares them automatically.

set -u

if [ $# -ne 3 ]; then
	echo "usage: $0 HTML2PNG TESTDIR OUTDIR" >&2
	exit 2
fi
TOOL=$1
TESTDIR=$2
OUTDIR=$3

# CPU seconds per page before it counts as hanging
TIME_LIMIT=60

mkdir -p "$OUTDIR/generated" || exit 1
GEN=$OUTDIR/generated

# ---------------------------------------------------------------------------
# Generated stress pages
# ---------------------------------------------------------------------------

# 100,000 nested elements: must be rejected (stack overflow otherwise)
awk 'BEGIN { for (i = 0; i < 100000; i++) printf "<div>"; print "deep" }' \
	> "$GEN/stress-deep-nesting.html"

# Nesting just below the limit (500 levels)
awk 'BEGIN { for (i = 0; i < 500; i++) printf "<div style=\"margin-left:1px\">"; print "500 levels" }' \
	> "$GEN/stress-nesting-500.html"

# Nested tables, as some newsletters have (30 levels)
awk 'BEGIN { for (i = 0; i < 30; i++) printf "<table border=1><tr><td>%d", i; print " nested tables" }' \
	> "$GEN/stress-nested-tables.html"

# A table of 20,000 rows
awk 'BEGIN {
	print "<table border=1>";
	for (i = 0; i < 20000; i++)
		printf "<tr><td>%d</td><td>cell %d</td><td>some more text in a cell</td></tr>\n", i, i;
	print "</table>" }' > "$GEN/stress-big-table.html"

# A word of 200,000 letters
awk 'BEGIN { printf "<p>"; for (i = 0; i < 200000; i++) printf "x"; print "</p>" }' \
	> "$GEN/stress-long-word.html"

# 50,000 small inline elements: over the element limit, must be rejected
awk 'BEGIN { printf "<p>"; for (i = 0; i < 50000; i++) printf "<span>w%d</span> ", i; print "</p>" }' \
	> "$GEN/stress-many-spans.html"

# 15,000 elements, below the limit: shows the time and memory such a page costs
awk 'BEGIN {
	for (i = 0; i < 3000; i++)
		printf "<p>Paragraph %d with <b>bold</b>, <i>italic</i> and a <a href=\"https://example.org/%d\">link</a>.</p>\n", i, i }' \
	> "$GEN/stress-15000-elements.html"

# A large newsletter: the newsletter sample 25 times
if [ -f "$TESTDIR/10-newsletter.html" ]; then
	i=0
	: > "$GEN/stress-large-newsletter.html"
	while [ $i -lt 25 ]; do
		cat "$TESTDIR/10-newsletter.html" >> "$GEN/stress-large-newsletter.html"
		i=$((i + 1))
	done
fi

# ---------------------------------------------------------------------------
# Rendering
# ---------------------------------------------------------------------------

total=0
failed=0
for page in "$TESTDIR"/*.html "$GEN"/*.html; do
	[ -f "$page" ] || continue
	name=$(basename "$page" .html)
	total=$((total + 1))

	# Remote images "downloaded" from tests/images (no network)
	(ulimit -t $TIME_LIMIT; exec "$TOOL" -t -w 800 -R "$TESTDIR/images" "$page" "$OUTDIR/$name.png") \
		> "$OUTDIR/$name.txt" 2>&1
	rc=$?
	case $rc in
	0)	status=ok ;;
	3)	status="rejected (limits)" ;;
	*)	status="FAILED (exit status $rc)"
		failed=$((failed + 1)) ;;
	esac
	printf '%-30s %-20s %s\n' "$name" "$status" "$(tail -n 1 "$OUTDIR/$name.txt")"
done

echo
if [ $failed -ne 0 ]; then
	echo "$failed of $total pages failed (output in $OUTDIR)"
	exit 1
fi
echo "all $total pages rendered or rejected (images in $OUTDIR)"
