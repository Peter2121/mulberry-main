#!/bin/sh
#
# Live-test helper, not run by "make check": decodes the reason Google puts
# in the address of its sign-in error page ("Access blocked: ...").  The page
# itself may show only "Request details: flowName=GeneralOAuthFlow"; the
# reason is in the address's authError parameter (base64).  Copy the whole
# address from the browser's address bar before closing the page.
#
# Usage: google-error-decode.sh 'https://accounts.google.com/signin/oauth/error...'
#        (or the address on standard input)
#
# Prints the page's path, the names of the address's parameters and the
# decoded reason, with unprintable bytes shown as spaces.  The reason may
# contain the account's e-mail address.

if [ $# -gt 1 ]; then
	echo "usage: $0 [ERROR-PAGE-ADDRESS]" >&2
	exit 2
fi
if [ $# -eq 1 ]; then url=$1; else read -r url; fi

printf '%s\n' "$url" | python3 -I -c 'import sys, base64, binascii, urllib.parse as u
url = sys.stdin.read().strip()
parts = u.urlsplit(url)
query = u.parse_qs(parts.query)
print("page:", parts.path)
print("parameters:", ", ".join(sorted(query)) or "(none)")
if "authError" not in query:
    print("no authError parameter in this address")
    sys.exit(1)
for value in query["authError"]:
    try:
        # A raw "+" in the address reads as a space
        value = value.replace(" ", "+")
        raw = base64.urlsafe_b64decode(value + "=" * (-len(value) % 4))
    except (binascii.Error, ValueError):
        print("authError: not base64")
        sys.exit(1)
    print("authError:", "".join(c if c.isprintable() else " " for c in raw.decode("utf-8", "replace")))'
