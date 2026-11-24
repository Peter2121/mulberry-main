# Candidate libraries for OAuth2 (and a Microsoft Graph mail backend) in Mulberry, minimising dependencies

How the numbers were collected (2026-11-24). Unless a bullet says otherwise:
- FreeBSD port versions, licences and dependencies come from the local FreeBSD ports tree, last updated about 2026-11-15, using `make -V PKGVERSION/LICENSE/LIB_DEPENDS/RUN_DEPENDS`. The citation is the port's page on cgit.freebsd.org.
- "pkg" sizes are installed sizes (`pkg query %sh`) on a FreeBSD 15.1 amd64 host.
- Source line counts are `wc -l` over the upstream GitHub HEAD tarball, downloaded 2026-11-24.
- "Latest release" and "last push" dates come from the anonymous GitHub REST API on 2026-11-24.
- Object sizes are the `.text` size of a minimal translation unit built with the base-system `cc`/`c++` (FreeBSD clang 19.1.7, `-Os`, amd64). This is not gcc 14, so treat them as a guide to relative size only.

Host baseline: libcurl 8.22.0 is installed and built against base OpenSSL 3.5.6, zlib, brotli, zstd, libidn2, libpsl, libssh2, nghttp2 and MIT krb5. `/usr/local/lib/libcurl.so.4.8.0` is 1,014,184 bytes and the curl package is 5.64 MiB. Base-system `libcrypto.so.35` is 6.3 MB and `libssl.so.35` is 1.05 MB.

---

## Q1. HTTPS client for the token endpoint, the device endpoint and Graph REST

### Takeaway
libcurl is already linked and covers everything needed: POSTing forms to token and device endpoints, Bearer-authenticated Graph GET/POST/PATCH, and even SASL XOAUTH2/OAUTHBEARER for IMAP, POP3 and SMTP. Every other option adds code or dependencies without adding a needed capability. cpp-httplib is the only lightweight alternative, but it is a 25.6k-line header that compiles to roughly 470 KB of object code. Boost.Beast pulls in Boost.

### Cited Findings

| Option | Licence | Size | Transitive deps (FreeBSD) | FreeBSD port / version | Maintenance 2025-26 | Suitability |
|---|---|---|---|---|---|---|
| **libcurl** (already linked) | curl (MIT-style; port says MIT) | libcurl.so 1.0 MB; pkg 5.64 MiB | Already present: OpenSSL (base), zlib, brotli, zstd, libidn2, libpsl, libssh2, nghttp2, krb5 | `ftp/curl` 8.23.0; 8.22.0 installed, released 2026-09-02 | Very active | **Best**: zero new dependencies |
| **cpp-httplib** | MIT | `httplib.h` 25,591 lines; client+server TU ≈ 474 KB `.text` (clang -Os) | OpenSSL ≥ 3.0; port RUN_DEPENDS adds brotli (option) | `www/cpp-httplib` 0.66.0 (upstream v0.67.0, 2026-11-18) | Very active (pushed 2026-11-24) | OK but redundant with libcurl; blocking I/O, HTTP/1.1 only |
| **Boost.Beast** | BSL-1.0 | header-only, but needs Boost.Asio and other Boost headers | `devel/boost-libs` 1.92.0 (large) | part of boost | Active | Too heavy; low-level, "not a client or server" |
| **Raw OpenSSL BIO / own HTTP** | own | Alpine `http.c` 1,160 lines; Claws builds requests by hand inside its 907-line `oauth2.c` | OpenSSL only | n/a | n/a | Possible but re-implements what libcurl already does; easy to get TLS verification wrong (see Claws below) |
| cpprestsdk (for completeness) | MIT | large | Boost, OpenSSL | `devel/cpprestsdk` 2.10.19 | not verified | Not recommended |

- libcurl's `CURLOPT_XOAUTH2_BEARER` sets an OAuth 2.0 access token for HTTP, IMAP, LDAP, POP3 and SMTP. It was added in 7.33.0, and the username for mail protocols goes in `CURLOPT_USERNAME`. — [curl docs: CURLOPT_XOAUTH2_BEARER](https://curl.se/libcurl/c/CURLOPT_XOAUTH2_BEARER.html)
- libcurl 8.23.0 source `lib/curl_sasl.c` has both a `"XOAUTH2"` and an `"OAUTHBEARER"` SASL mechanism (`SASL_MECH_XOAUTH2`, `SASL_MECH_OAUTHBEARER`), which are selected when a bearer token is set. — [curl 8.23.0 source tarball](https://curl.se/download/curl-8.23.0.tar.xz) (local grep)
- HTTP Bearer auth is `CURLAUTH_BEARER`: "HTTP Bearer token authentication, used primarily in OAuth 2.0 protocol", added in 7.61.0. — [curl docs: CURLOPT_HTTPAUTH](https://curl.se/libcurl/c/CURLOPT_HTTPAUTH.html)
- The `CURLOPT_LOGIN_OPTIONS` page does not list XOAUTH2 or OAUTHBEARER as `AUTH=` values. Its examples are NTLM, GSSAPI, `*` and `+LOGIN`. — [curl docs: CURLOPT_LOGIN_OPTIONS](https://curl.se/libcurl/c/CURLOPT_LOGIN_OPTIONS.html)
- cpp-httplib describes itself as "A C++11 single-file header-only cross platform HTTP/HTTPS library". It "uses 'blocking' socket I/O… Only HTTP/1.1 is supported". It needs OpenSSL "3.0 or later" via `CPPHTTPLIB_OPENSSL_SUPPORT` and also supports MbedTLS and wolfSSL. — [cpp-httplib README](https://github.com/yhirose/cpp-httplib)
- The FreeBSD port `www/cpp-httplib` 0.66.0 is MIT, with RUN_DEPENDS on brotli and openssl and the BROTLI and SSL options on by default. — [FreeBSD ports www/cpp-httplib](https://cgit.freebsd.org/ports/tree/www/cpp-httplib/Makefile)
- Boost.Beast: "Sources are header-only", but Beast "only works with Boost, not stand-alone Asio". It provides "low-level HTTP/1, WebSocket, and networking protocol" building blocks: "This library is not a client or server, but it can be used to build those things." OpenSSL is required for TLS. — [Boost.Beast introduction](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/introduction.html)
- Claws Mail's `oauth2.c` makes its token request with its own `sock_connect(host, 443)` plus `ssl_init_socket()` (GnuTLS priority string at line 50) and sets `sock->ssl_cert_auto_accept = TRUE`. That means certificate problems are auto-accepted for the token request. — [Claws Mail src/oauth2.c (git HEAD)](https://git.claws-mail.org/?p=claws.git;a=blob;f=src/oauth2.c;hb=HEAD) (local inspection, lines 491-498)

### Inferences
- If Mulberry has its own IMAP and SMTP code, libcurl's XOAUTH2/OAUTHBEARER support does not matter for mail. Mulberry only has to build the SASL string itself (`user=<u>^Aauth=Bearer <tok>^A^A`, base64), which is a few lines. For Graph REST, `CURLAUTH_BEARER` + `CURLOPT_XOAUTH2_BEARER`, or a plain `Authorization:` header, is enough.
- The Claws example shows that hand-rolled TLS in OAuth code is a real risk. Using libcurl with its default `CURLOPT_SSL_VERIFYPEER=1` avoids that whole class of bug.

### Gaps
- I did not check cpprestsdk's maintenance status, which is reportedly maintenance-only.
- I did not measure the installed size of Boost.

---

## Q2. A tiny embedded HTTP listener for the loopback redirect (RFC 8252)

### Takeaway
Hand-written listener code using plain BSD sockets is the norm and the smallest option. Mulberry needs to accept a single GET on 127.0.0.1 with an ephemeral port, parse `?code=&state=` (or `error=`), return a short HTML page and close. Of the libraries, only civetweb (MIT) and cpp-httplib (MIT) are licence-compatible and small enough. mongoose is GPLv2/commercial, and libmicrohttpd is LGPL and pulls GnuTLS by default on FreeBSD. Microsoft's device-code flow needs no listener at all.

### Cited Findings

| Option | Licence | Size | Deps (FreeBSD) | Port / version | Maintenance | Suitability |
|---|---|---|---|---|---|---|
| **Own code (BSD sockets)** | own | Alpine's whole own HTTP client is 1,160 lines; a one-shot listener is far smaller (see Inferences) | libc only | n/a | n/a | **Best** |
| **cpp-httplib server** | MIT | same 25.6k-line header as Q1 | OpenSSL not needed for loopback | `www/cpp-httplib` 0.66.0 | very active | OK if cpp-httplib is adopted anyway |
| **civetweb** | MIT | `civetweb.c`+`.h` 25,109 lines (all `src/` 37,274) | none (port has no LIB_DEPENDS) | `www/civetweb` 1.15 (upstream latest release v1.16, 2023-04-10; last push 2026-08-01) | slow releases | Overkill |
| **mongoose** | **GPLv2 or commercial** | `mongoose.c`+`.h` 44,883 lines | none | `www/mongoose` 5.6 (very old; upstream 7.24, 2026-11-18) | very active | Licence problem for a non-GPL app |
| **libmicrohttpd** | LGPL-2.1+ | pkg 1,017 KiB | `security/gnutls` by default (gnutls 7.75 MiB + nettle 8.3 MiB + p11-kit 4.4 MiB + libtasn1 + libidn2) | `www/libmicrohttpd` 1.0.10 | active | Too much for one request |

- Microsoft: for loopback redirect URIs, "the port component … is ignored for the purposes of matching a localhost redirect URI". `http` is acceptable for loopback per RFC 8252 §§7.3/8.3. Microsoft recommends `127.0.0.1` over `localhost`. "The IPv6 loopback address ([::1]) isn't currently supported". An `http://127.0.0.1` redirect currently has to be added through the app manifest (`replyUrlsWithType`), not the portal text box. — [Microsoft Learn: Redirect URI best practices](https://learn.microsoft.com/en-us/entra/identity-platform/reply-url)
- Microsoft: an app does not need to register a redirect URI if it uses the "OAuth 2.0 device code flow". — [Microsoft Learn: Redirect URI best practices](https://learn.microsoft.com/en-us/entra/identity-platform/reply-url)
- Google, desktop apps: the loopback IP address "is the recommended mechanism for obtaining the authorization code". Custom URI schemes "are no longer supported due to the risk of app impersonation", and the manual copy/paste (OOB) method is no longer supported. — [Google: OAuth 2.0 for iOS & Desktop Apps](https://developers.google.com/identity/protocols/oauth2/native-app)
- The mongoose LICENSE reads: "dual-licensed: you can redistribute it and/or modify it under the terms of the GNU General Public License version 2 … Alternatively, you can license this software under a commercial" licence. — [cesanta/mongoose LICENSE](https://github.com/cesanta/mongoose/blob/master/LICENSE)
- `www/libmicrohttpd` 1.0.10 is licensed LGPL21+, with `LIB_DEPENDS` on `security/gnutls` and the GNUTLS option on by default. — [FreeBSD ports www/libmicrohttpd](https://cgit.freebsd.org/ports/tree/www/libmicrohttpd/Makefile)
- `www/civetweb` 1.15 is MIT and has no library dependencies. Its description: "C/C++ embeddable web server with optional CGI, SSL and Lua support". — [FreeBSD ports www/civetweb](https://cgit.freebsd.org/ports/tree/www/civetweb/Makefile)

### Inferences
- A one-shot loopback listener takes about 150-300 lines of C++: `socket`/`bind(127.0.0.1:0)`/`getsockname`/`listen`, one `accept` with a timeout via `poll` (which fits an X11/FLTK event loop), read up to `\r\n\r\n`, parse the request line, percent-decode, verify `state`, send `HTTP/1.1 200` and close. This is an estimate, not a measurement.
- The Mulberry licence (believed to be Apache-2.0; not checked here) rules out mongoose, because GPLv2 is not compatible with Apache-2.0.

### Gaps
- I found no small, standalone, licence-friendly "loopback OAuth listener" library in C or C++. Rust has `loopauth` and `clio-auth`, but nothing comparable turned up for C or C++.

---

## Q3. JSON parsing (small token responses; larger Graph responses)

### Takeaway
For token and device-code responses, a tokenizer such as jsmn (471 lines, ~1.6 KB of code) or a ~500-line parser is enough, which is what Alpine does. Graph responses (message lists, `$batch`, delta queries) favour a DOM parser.
- **cJSON** (C89, 3.5k lines, ~16 KB, MIT, `devel/libcjson`) is the best size/maintenance trade-off.
- **picojson** (1.2k-line C++ header, BSD-2) is the smallest C++ DOM option but is stale.
- **yyjson** is the fastest small C option (~107-239 KB of code).
- **nlohmann/json** gives the nicest C++ API at a large size (49k-line header, ~119 KB of code even for a trivial use).

### Cited Findings

| Library | Lang / API | Licence | Source lines | `.text` (clang -Os) | FreeBSD port (version; pkg size) | Latest release / last push | Notes |
|---|---|---|---|---|---|---|---|
| **jsmn** | C, tokenizer (no DOM, no unescaping) | MIT | 471 (`jsmn.h`) | 1.6 KB | none | v1.1.0 (2019-06-23) / 2024-06-09 | No dynamic allocation; zero-copy |
| **pdjson** | C, streaming pull parser | Unlicense | 1,139 | 9.2 KB | `textproc/pdjson` g20250825 | none / **archived** on GitHub | |
| **picojson** | C++ header, DOM (`std::map`) | BSD-2 | 1,200 | ~10 KB (parse only) | `devel/picojson` 1.3.1.20210117 | v1.3.0 (2015-02-25) / 2024-07-13 | Default JSON backend of jwt-cpp |
| **sajson** | C++ header, single-allocation DOM | MIT | 2,606 | not measured | none | no releases / 2025-04-06 | Low activity |
| **cJSON** | C89, DOM tree | MIT | 3,512 (`.c`+`.h`) | 15.9 KB | `devel/libcjson` 1.7.19 (156 KiB) | v1.7.19 (2025-09-09) / 2026-09-16 | "single file of C, and a single header file" |
| **json-c** | C, ref-counted DOM | MIT | 10,575 (top-level `.c`/`.h`) | not measured | `devel/json-c` 0.19 (344 KiB) | 0.19 (2026-06-27) / 2026-11-21 | |
| **jansson** | C, ref-counted DOM | MIT | 12,419 (`src/`) | not measured | `devel/jansson` 2.15.1 (139 KiB) | v2.15.1 (2026-07-01) / 2026-11-23 | Required by libjwt, cjose, liboauth2 |
| **yyjson** | ANSI C89, immutable/mutable DOM | MIT | 20,206 | 239 KB full; 107 KB with writer/utils/fast-fp disabled | `devel/yyjson` 0.13.0 | 0.13.0 (2026-09-08) / 2026-11-19 | Very fast |
| **RapidJSON** | C++ headers, SAX + DOM | MIT (+ JSON licence only in `bin/jsonchecker`) | 19,062 (`include/`) | not measured | `devel/rapidjson` 1.1.0.763 (git snapshot) | **v1.1.0 (2016-08-25)** / 2026-11-03 | No release in 10 years |
| **nlohmann/json** | C++11 header, DOM (`std::map`, `std::vector`) | MIT | 49,106 (single header) | 119 KB for one `parse`+`contains` | `devel/nlohmann-json` 3.12.0 | v3.12.0 (2025-04-11) / 2026-11-24 | Best ergonomics; heavy |
| **simdjson** | C++17, On-Demand | Apache-2.0 / MIT | large | not measured | `devel/simdjson` 5.1.1 (pkg 10.1 MiB) | v5.3.0 (2026-11-22) | Overkill |
| **Boost.JSON** | C++ | BSL-1.0 | part of Boost | — | in `devel/boost-libs` 1.92.0 | no separate releases / 2026-11-11 | Needs Boost |
| **glaze** | modern C++ (reflection) | MIT | — | — | `devel/glaze` 9.0.0 | v9.1.0 (2026-10-28) | Heavy templates |
| **jsoncpp** | C++ DOM | MIT | — | — | `devel/jsoncpp` 1.9.8 | — | Used by sasl-xoauth2 |
| **Alpine's own `json.c`** | C | Apache-2.0 (Alpine) | 479 (+42 `.h`) | — | — | — | Example of a self-written parser |

Sources for the table:
- jsmn — [zserge/jsmn](https://github.com/zserge/jsmn). The README lists "no dynamic memory allocation" and makes "it possible to use zero-copy techniques".
- cJSON — [DaveGamble/cJSON](https://github.com/DaveGamble/cJSON). The README says "Ultralightweight JSON parser in ANSI C" and "It's a single file of C, and a single header file".
- yyjson — [ibireme/yyjson](https://github.com/ibireme/yyjson). The README says "can read or write gigabytes of JSON data per second", "complies with ANSI C (C89), no explicit SIMD".
- RapidJSON licence — [Tencent/rapidjson license.txt](https://github.com/Tencent/rapidjson/blob/master/license.txt): "To avoid the problematic JSON license … it's sufficient to exclude the bin/jsonchecker/ directory".
- nlohmann — [nlohmann/json README](https://github.com/nlohmann/json): "There are certainly faster JSON libraries out there"; it uses `std::map` for objects and `std::vector` for arrays by default.
- pdjson — [skeeto/pdjson](https://github.com/skeeto/pdjson), archived according to the GitHub API.
- picojson — [kazuho/picojson](https://github.com/kazuho/picojson).
- sajson — [chadaustin/sajson](https://github.com/chadaustin/sajson).
- json-c — [json-c/json-c](https://github.com/json-c/json-c).
- jansson — [akheron/jansson](https://github.com/akheron/jansson).
- simdjson — [simdjson/simdjson](https://github.com/simdjson/simdjson).
- glaze — [stephenberry/glaze](https://github.com/stephenberry/glaze).
- Boost.JSON — [boostorg/json](https://github.com/boostorg/json).
- FreeBSD port data comes from the ports tree, for example [devel/libcjson](https://cgit.freebsd.org/ports/tree/devel/libcjson/Makefile) and [devel/yyjson](https://cgit.freebsd.org/ports/tree/devel/yyjson/Makefile). The JSON ports listed (libcjson, json-c, jansson, yyjson) have no LIB_DEPENDS.
- Alpine `json.c` — [Alpine 2.26 source](https://alpineapp.email/alpine/release/src/alpine-2.26.tar.xz), `imap/src/c-client/json.c`.
- Claws Mail uses no JSON parser at all. It pulls `access_token`, `refresh_token` and `expires_in` out of the response with GLib regexes, for example `g_regex_new ("\"refresh_token\": ?\"(.*?)\",?", …)`. — [Claws Mail src/oauth2.c](https://git.claws-mail.org/?p=claws.git;a=blob;f=src/oauth2.c;hb=HEAD) (lines 326-365)

### Inferences
- **Token responses only:** use jsmn (or a ~400-500-line self-written parser like Alpine's), plus a small helper that unescapes `\uXXXX`. jsmn does not decode strings, but token values are ASCII.
- **With a Graph backend:** a DOM parser saves a lot of code. cJSON is the smallest maintained choice and is easy to vendor as two files. yyjson only pays off if large `delta`/`$batch` responses become a bottleneck. If Mulberry wants C++ idioms, picojson is tiny but stale; nlohmann is well maintained but roughly 10× the code of cJSON.
- Vendoring cJSON or jsmn (MIT) avoids a new FreeBSD package dependency entirely.
- The regex approach used by Claws is fragile (escaped quotes, key order, whitespace) and should not be copied.

### Gaps
- I did not benchmark parse speed on Graph-sized payloads.
- I did not measure sajson, json-c or jansson object sizes.
- Whether Mulberry already contains a JSON parser is outside this scope and was not checked.

---

## Q4. OAuth2-specific libraries: worth using, or is a small own implementation the norm?

### Takeaway
No maintained, lightweight, desktop-oriented C or C++ OAuth2 client library exists that fits Mulberry.
- **liboauth2 (OpenIDC)** targets server-side and web-server plugins, and pulls in OpenSSL, curl, jansson, cjose and pcre2.
- **liboauth** is OAuth 1.0.
- **QtNetworkAuth** and **GNOME Online Accounts** bring Qt or GLib/D-Bus with them.
- The SASL plugins (cyrus-sasl-xoauth2, sasl-xoauth2) only cover the SASL step or target Postfix.

Comparable mail clients write their own: Claws Mail in about 1,000 lines, and Alpine in about 2,600 lines of core code (protocol, HTTP and JSON) plus about 1,400 lines of UI and provider tables. So the norm is an own implementation of about 1,000-1,500 lines on top of libcurl, a small JSON parser and OpenSSL.

### Cited Findings

| Library | Licence | Size | Deps | FreeBSD port | Maintenance | Verdict |
|---|---|---|---|---|---|---|
| **liboauth2** (OpenIDC) | Apache-2.0 | 27,465 lines (`src/`+`include/`) | OpenSSL, libcurl, jansson, cjose, pcre2; optional memcache, hiredis, libjq, APR | none | v2.4.0 (2026-09-10) | Server/plugin oriented; too heavy |
| **liboauth** | MIT | — | libcurl, OpenSSL | `net/liboauth` 1.0.3_5 | OAuth **1.0** only | Not applicable |
| **cyrus-sasl-xoauth2** (moriyoshi) | MIT | 1,158 lines | cyrus-sasl2 (pkg 8.18 MiB) | `security/cyrus-sasl2-xoauth2` 0.2 | v0.2 (2020-04-27), last push 2021-08-10 | SASL step only; dormant |
| **sasl-xoauth2** (tarickb) | GitHub reports NOASSERTION | 2,357 lines C++ | libcurl, jsoncpp, Cyrus SASL | none | release-0.25 (2024-10-30), push 2026-10-15 | Postfix-oriented token-refresh plugin |
| **GNU SASL** (gsasl/libgsasl) | LGPL-2.1 | — | gnutls, libgcrypt, libidn, libntlm | `security/gsasl` 2.2.4 | active | The manual's mechanism list has no XOAUTH2/OAUTHBEARER |
| **QtNetworkAuth** | GPLv3 / commercial per Qt docs (the port says LGPL21) | — | qt6-base (pkg 65.9 MiB) | `net/qt6-networkauth` 6.11.3 | active | Qt only |
| **GNOME Online Accounts** | LGPL-2.0+ | — | GLib/GTK/D-Bus/libsecret stack | `net/gnome-online-accounts` 3.54.11 | active | Desktop service, not a library to embed |
| **signon-plugin-oauth2** | — | — | Ubuntu Online Accounts / signond (Qt) | `sysutils/signon-plugin-oauth2` 0.25 | — | Not applicable |
| keycloak-pkce-cpp | — | tiny | — | none | 3 stars | Keycloak-specific |
| Chilkat CkOAuth2 | commercial | — | — | none | — | Commercial |

- liboauth2 README: "Generic library to build C-based OAuth 2.x and OpenID Connect servers and clients e.g. web-server plugins". It "extends cjose", with "generic code with plugins for Apache, NGINX". Its `configure.ac` checks for openssl, libcurl, jansson, cjose, libpcre2-8, and optionally libmemcached, hiredis, libjq and apr. — [OpenIDC/liboauth2](https://github.com/OpenIDC/liboauth2)
- `net/liboauth` 1.0.3_5 has LIB_DEPENDS on curl. — [FreeBSD ports net/liboauth](https://cgit.freebsd.org/ports/tree/net/liboauth/Makefile)
- The `security/cyrus-sasl2-xoauth2` description: "a plugin implementation of XOAUTH2 for cyrus-sasl2 … allows clients to send OAuth 2.0 access tokens". — [FreeBSD ports security/cyrus-sasl2-xoauth2](https://cgit.freebsd.org/ports/tree/security/cyrus-sasl2-xoauth2/pkg-descr)
- sasl-xoauth2's CMake requires CURL, jsoncpp and libsasl2. — [tarickb/sasl-xoauth2](https://github.com/tarickb/sasl-xoauth2)
- The GNU SASL manual lists ANONYMOUS, CRAM-MD5, DIGEST-MD5, EXTERNAL, GS2-KRB5, GSSAPI, LOGIN, NTLM, OPENID20, PLAIN, SCRAM-*, SAML20 and SECURID, with no XOAUTH2 or OAUTHBEARER. — [GNU SASL manual](https://www.gnu.org/software/gsasl/manual/gsasl.html)
- `security/libgsasl` 2.2.4 LIB_DEPENDS: libgcrypt, gnutls, libidn, libntlm, gettext. — [FreeBSD ports security/libgsasl](https://cgit.freebsd.org/ports/tree/security/libgsasl/Makefile)
- Qt Network Authorization is available under commercial licences "or … the GNU General Public License, version 3". It focuses on OAuth 2.0 with limited OpenID support. — [Qt docs: Qt Network Authorization](https://doc.qt.io/qt-6/qtnetworkauth-index.html). The FreeBSD port lists `LICENSE=LGPL21`, which contradicts this. — [FreeBSD ports net/qt6-networkauth](https://cgit.freebsd.org/ports/tree/net/qt6-networkauth/Makefile)
- GNOME 46 added a "Microsoft 365" account type, mainly for OneDrive. Mail historically went through the "Microsoft Personal" provider, and users report that mail activation is unreliable on GNOME 46. — [GNOME Discourse: Microsoft 365 online account](https://discourse.gnome.org/t/questions-about-microsoft-365-online-account/21232); [Zorin forum report](https://forum.zorin.com/t/feature-request-update-to-gnome-49-for-microsoft-365-online-accounts-compatibility/59471) (user reports, not official)
- A search found no established C++ OAuth2 client library with PKCE and loopback support. The common pattern is "libcurl for HTTP, OpenSSL for randomness and hashing, and jwt-cpp or a similar JWT library". — [MojoAuth guide: OAuth 2.0 and OIDC in C++](https://mojoauth.com/security-guides/implement-oauth2-oidc-with-cpp) (vendor blog; it describes the pattern, it is not an authority); [Chilkat CkOAuth2](https://chilkatsoft.com/refdoc/vcCkOAuth2Ref.html)

**Reference implementations in mail clients (measured):**

- **Claws Mail** `src/oauth2.c` is 907 lines (830 non-blank) and `oauth2.h` is 88 lines. It uses GLib (`g_regex`, `g_base64_encode`), Claws's own socket and GnuTLS layer, and `passcrypt` for storage. The provider table (Google, Microsoft variants, Yahoo) is built in as strings. A grep finds no `code_verifier`, so there is no PKCE in this file. It hand-builds `POST … HTTP/1.1` requests with `snprintf` and parses JSON with regexes. — [Claws Mail src/oauth2.c](https://git.claws-mail.org/?p=claws.git;a=blob;f=src/oauth2.c;hb=HEAD)
- **Alpine 2.26** (C, Apache-2.0), all in-tree with no extra libraries:

  | File | Lines | Role |
  |---|---|---|
  | `imap/src/c-client/auth_oa2.c` | 176 | XOAUTH2 SASL |
  | `imap/src/c-client/auth_bea.c` | 165 | OAUTHBEARER SASL |
  | `imap/src/c-client/oauth2_aux.c` | 610 (+24 `.h`) | Token logic; uses `code_verifier`, i.e. PKCE |
  | `imap/src/c-client/http.c` | 1,160 (+137 `.h`) | Own HTTP client on c-client's TCP/SSL |
  | `imap/src/c-client/json.c` | 479 (+42 `.h`) | Own JSON parser |
  | `alpine/xoauth2info.c` | 249 | Provider table |
  | `alpine/xoauth2conf.c` | 1,126 | Configuration UI |

  The total, including headers, is about 4,400 lines, of which about 2,600 are core. — [Alpine 2.26 source tarball](https://alpineapp.email/alpine/release/src/alpine-2.26.tar.xz) (local `wc -l`)
- Alpine also ships `scripts/ooauth2.py` (204 lines), a helper script. — same tarball; [Alpine OOauth2 script page](https://alpineapp.email/scripts/ooauth2.html)

### Inferences
- With libcurl doing HTTP and TLS and a vendored jsmn or cJSON doing JSON, Mulberry's own OAuth2 core needs roughly:
  - provider table: ~100-200 lines
  - authorization URL, PKCE and state: ~100 lines
  - loopback listener: ~200 lines
  - token, refresh and device-code POSTs with JSON extraction: ~300 lines
  - SASL XOAUTH2/OAUTHBEARER string: ~50 lines
  - expiry/refresh scheduling and error handling: ~200 lines

  That totals about 1,000 lines, plus UI. This matches the order of magnitude of Claws (~1k) and Alpine's core. Alpine's is larger mainly because it carries its own HTTP client and JSON parser.
- None of the OAuth2 libraries reduces the work enough to justify its dependency cost.

### Gaps
- I could not check how Evolution and evolution-data-server implement OAuth2 internally; the source was not inspected.
- I did not confirm the release date of liboauth 1.0.3.
- I did not inspect the licence text of sasl-xoauth2.

---

## Q5. PKCE: SHA-256, base64url and randomness

### Takeaway
Base-system OpenSSL 3.5.6, already linked through libcurl and directly, provides everything:
- `RAND_bytes` for 32 random bytes (the verifier) and for `state`;
- `EVP_Digest` with `EVP_sha256()` (or the one-shot `SHA256()`) for the challenge;
- `EVP_EncodeBlock` for base64, then a 5-line post-pass turns it into base64url (`+`→`-`, `/`→`_`, strip `=`).

No new dependency is needed.

### Cited Findings
- Google: "Always use PKCE (Proof Key for Code Exchange) when requesting authorization codes"; S256 is recommended over plain. Desktop apps "are public clients and cannot keep secrets confidential", and `client_secret` is "Optional" in the token and refresh requests. — [Google: OAuth 2.0 for iOS & Desktop Apps](https://developers.google.com/identity/protocols/oauth2/native-app)
- PKCE (RFC 7636) defines `code_challenge = BASE64URL(SHA256(ASCII(code_verifier)))` for S256, with a 43-128 character verifier. — [RFC 7636](https://datatracker.ietf.org/doc/html/rfc7636)
- The host has OpenSSL 3.5.6 (7 Apr 2026) in base (`libcrypto.so.35`, `libssl.so.35`). — local `openssl version`; [OpenSSL RAND_bytes man page](https://docs.openssl.org/3.5/man3/RAND_bytes/); [OpenSSL EVP_EncodeInit/EVP_EncodeBlock man page](https://docs.openssl.org/3.5/man3/EVP_EncodeInit/)
- Alpine implements PKCE in plain C on top of OpenSSL; `code_verifier` appears in `oauth2_aux.c` and `xoauth2info.c`. — [Alpine 2.26 source](https://alpineapp.email/alpine/release/src/alpine-2.26.tar.xz)

### Inferences
- Total PKCE code is about 40 lines. `EVP_EncodeBlock` is preferable to the BIO base64 filter because it is one call with no newlines.

### Gaps
- None material.

---

## Q6. Opening the system browser

### Takeaway
Use `xdg-open <url>` via `posix_spawnp`. Fall back to `$BROWSER`, then to a list of known browsers. As a last resort, show the URL with a copy button, which is also the UI for the device-code flow. RFC 8252 requires an external user agent rather than an embedded web view. Thunderbird gained an external-browser option only in version 151.

### Cited Findings
- `devel/xdg-utils` 1.2.1 is MIT and about 315 KiB installed. RUN_DEPENDS are `misc/hicolor-icon-theme`, `x11/xprop` and `x11/xset`, all tiny (72 KiB, 46 KiB, 35 KiB), with no library dependencies. Description: "Tools to allow all applications to easily integrate with the free desktop." — [FreeBSD ports devel/xdg-utils](https://cgit.freebsd.org/ports/tree/devel/xdg-utils/Makefile)
- Thunderbird can show the provider login "in a pop-up window or, as of version 151, in the user's default browser". The `mailnews.oauth.useExternalBrowser` preference selects which. — [Thunderbird source docs: OAuth](https://source-docs.thunderbird.net/en/latest/backend/oauth.html)
- Microsoft cites RFC 8252 for loopback redirects. RFC 8252 is the native-apps BCP and requires external user agents (system browser) rather than embedded user agents. — [Microsoft Learn: Redirect URI best practices](https://learn.microsoft.com/en-us/entra/identity-platform/reply-url); [RFC 8252](https://datatracker.ietf.org/doc/html/rfc8252)

### Inferences
- xdg-open is a runtime dependency only (a shell script), not a link dependency. Mulberry can mark it as an optional RUN_DEPENDS in a port.
- The `$BROWSER` convention (a colon-separated list, `%s` substitution) is informal, and I found no formal spec here.
- Never pass the URL through a shell. Spawn with an argv array, because authorization URLs contain `&`.

### Gaps
- I did not verify any formal specification for `$BROWSER`.

---

## Q7. Secure storage of refresh tokens

### Takeaway
Comparable lightweight clients do not use an OS keyring. They encrypt the refresh token into their own password store under an optional master passphrase: Claws Mail uses `passwordstorerc` (AES-256-CBC, PBKDF2), Alpine uses its password file, and Thunderbird uses its logins store with an optional primary password.
- **libsecret** on FreeBSD pulls in GLib (106 MiB installed), libgcrypt (8 MiB) and libxml2. It also needs a running D-Bus session bus plus a Secret Service provider (gnome-keyring or KeePassXC) at runtime.
- **KWallet** and **QtKeychain** pull in Qt 6 (65.9 MiB), and KWallet also KDE Frameworks.

The minimal option is OpenSSL-based encryption in Mulberry's own preferences, with libsecret support as optional extra (for example, dlopen'ed).

### Cited Findings

| Option | Licence | Port / version | Lib deps (FreeBSD) | Runtime requirements | Installed size impact |
|---|---|---|---|---|---|
| **libsecret** | LGPL-2.1+ (port lists APACHE20 GPLv2+ LGPL21+) | `security/libsecret` 0.21.7_2 (pkg 5.24 MiB) | glib20, libgcrypt, libgpg-error, gettext-runtime, libxml2 (glib itself needs libffi, pcre2, gettext, libiconv) | D-Bus session + Secret Service provider: `security/gnome-keyring` 48.0 (4.15 MiB) or `security/keepassxc` 2.7.12 (34.7 MiB) | glib 106 MiB, libgcrypt 7.99 MiB, pcre2 8.08 MiB, libxml2 3.71 MiB, dbus 0.9 MiB |
| **KWallet** | LGPL-2.0 | `sysutils/kf6-kwallet` 6.31.0 | qt6-base, about 9 KF6 frameworks, gpgme/gpgmepp, libassuan, libgcrypt, qca, libsecret, libglvnd | kwalletd | qt6-base alone 65.9 MiB |
| **QtKeychain** | BSD-2 | `security/qtkeychain` 0.17.0_1 (upstream 0.18.0, 2026-11-10) | qt6-base, libsecret | as libsecret | Qt + GLib stacks |
| **pass** (password-store) | GPLv2+ | `sysutils/password-store` 1.7.4_7 | shell script around GnuPG | gpg-agent | external process, not a library |
| **Own encryption** (OpenSSL) | own | n/a | base OpenSSL (already linked) | optional master passphrase prompt | 0 |

- Port dependency data comes from the ports tree: [security/libsecret](https://cgit.freebsd.org/ports/tree/security/libsecret/Makefile), [sysutils/kf6-kwallet](https://cgit.freebsd.org/ports/tree/sysutils/kf6-kwallet/Makefile), [security/qtkeychain](https://cgit.freebsd.org/ports/tree/security/qtkeychain/Makefile), [sysutils/password-store](https://cgit.freebsd.org/ports/tree/sysutils/password-store/Makefile), [devel/glib20](https://cgit.freebsd.org/ports/tree/devel/glib20/Makefile).
- Secret Service needs a provider on the session bus. Without one you get "org.freedesktop.secrets was not provided by any .service files". KeePassXC "will not be automatically started when an application requests secrets" unless a user D-Bus service file is added. A minimal WM setup may need a manually started D-Bus session. — [Setup org.freedesktop.secrets (gist)](https://gist.github.com/GrabbenD/6658c36a1c7fc7ee30ee2498647ca4c6); [ArchWiki: KeePass](https://wiki.archlinux.org/title/KeePass); [secretsd](https://www.Github.com/grawity/secretsd) (community sources)
- Thunderbird looks for "an existing refresh token in the logins store" and saves "the refresh token to the logins store". — [Thunderbird source docs: OAuth](https://source-docs.thunderbird.net/en/latest/backend/oauth.html). The provider's token is stored by Thunderbird in its list of saved passwords. — [Mozilla support: OAuth2 for Thunderbird](https://support.mozilla.org/en-US/kb/tb-oauth)
- Claws Mail 3.14 moved account passwords to `~/.claws-mail/passwordstorerc` with "a stronger encryption", using a key derived "from either PASSCRYPT_KEY, or user-selected master passphrase, using PBKDF2" and AES-256-CBC. Its `passwordstore.h` defines the keys `oauth2_refresh` and `oauth2_access_expiry`. — [Linux Today: Claws Mail 3.14 master passphrase](https://www.linuxtoday.com/news/claws-mail-3-14-email-client-lets-you-secure-passwords-with-a-master-passphrase); [Claws passwordstore.h (DragonFly grok mirror)](https://grok.dragonflybsd.org/xref/dports/mail/claws-mail-pdf_viewer/claws-mail-3.18.0/src/passwordstore.h?a=true). The PBKDF2/AES detail comes from a search snippet of a Claws users-list post that later returned 404, so it is secondary.
- Alpine saves XOAUTH2 refresh and access tokens in its password file, which requires a build "compiled with password file support". Without saving, the user must re-authorize. — [Alpine help: XOAUTH2](https://alpineapp.email/alpine/alpine-info/misc/xoauth2.html). Since Alpine 2.10 the password file is encrypted with a personal S/MIME certificate (older SISSA page, possibly outdated). — [SISSA Alpine page](https://www.itcs.sissa.it/_export/xhtml/services/email/clients/alpine). Alpine source `pith/smime.c` has `load_key_and_cert()` under `#ifdef PASSFILE`. — [Alpine 2.26 source](https://alpineapp.email/alpine/release/src/alpine-2.26.tar.xz)

### Inferences
- For "minimum dependencies", the Claws/Alpine/Thunderbird pattern fits Mulberry: encrypt tokens with AES-256-GCM using a key from `PKCS5_PBKDF2_HMAC` (or scrypt/argon2 in OpenSSL 3.2+) over an optional master passphrase, in a 0600 file. That is roughly 150 lines of OpenSSL EVP code. Without a passphrase it is only obfuscation, which is what Claws does with its built-in PASSCRYPT_KEY.
- libsecret could be offered as optional runtime support (dlopen `libsecret-1.so`) so that the FreeBSD package does not hard-depend on GLib.
- Storing refresh tokens is equivalent to storing passwords, so the threat model matches Mulberry's existing saved-password handling.

### Gaps
- I did not verify the exact current encryption scheme of the Alpine password file or Claws `passwordstorerc` from primary documentation; the Claws list post returned 404.
- I did not inspect how Evolution stores tokens.
- I did not research how Mulberry stores passwords today; that is out of scope.

---

## Q8. Optional: JWT / ID-token parsing

### Takeaway
Not needed for mail. If the email/UPN from the ID token is wanted, base64url-decode the middle segment and read it with the JSON parser already chosen. OIDC permits relying on TLS instead of verifying the signature when the ID token comes directly from the token endpoint. Full JWT libraries pull in jansson or picojson.

### Cited Findings

| Library | Licence | Size | Deps | FreeBSD port | Upstream |
|---|---|---|---|---|---|
| **jwt-cpp** | MIT | `jwt.h` 4,283 lines (header-only) | OpenSSL + picojson (default) or other JSON traits | `devel/jwt-cpp` 0.7.1_2 (RUN_DEPENDS picojson) | v0.7.3 (2026-11-24) |
| **libjwt** | MPL-2.0 | about 57.9k lines in `libjwt/` (includes tests/vendored code) | jansson, OpenSSL | `www/libjwt` 2.1.3 | v3.8.0 (2026-11-20); the port lags a major version |
| **cjose** | MIT | — | jansson, OpenSSL | `devel/cjose` 0.8.0.20260912 | — |

- Sources: [Thalhammer/jwt-cpp](https://github.com/Thalhammer/jwt-cpp), [benmcollins/libjwt](https://github.com/benmcollins/libjwt), and FreeBSD ports [devel/jwt-cpp](https://cgit.freebsd.org/ports/tree/devel/jwt-cpp/Makefile), [www/libjwt](https://cgit.freebsd.org/ports/tree/www/libjwt/Makefile) and [devel/cjose](https://cgit.freebsd.org/ports/tree/devel/cjose/Makefile).
- OpenID Connect Core §3.1.3.7: if the ID token is received directly from the token endpoint, TLS server validation may be used to validate the issuer instead of checking the token signature. — [OpenID Connect Core 1.0](https://openid.net/specs/openid-connect-core-1_0.html#IDTokenValidation)

### Inferences
- About 30 lines: split on `.`, base64url-decode (OpenSSL `EVP_DecodeBlock` after reversing the URL alphabet and re-padding), parse with jsmn or cJSON, and read `preferred_username` / `email`.

### Gaps
- None material.

---

## Q9. Recommended minimal dependency set and resulting footprint

### Takeaway
The minimal set adds **zero new FreeBSD packages at link time**:
- libcurl (already linked) for all HTTPS (token, device, Graph);
- base OpenSSL for PKCE, randomness, base64 and token-file encryption;
- one vendored MIT JSON parser: jsmn (≈0.5k lines) if only OAuth is needed, cJSON (≈3.5k lines) once a Graph backend is added;
- own code for the loopback listener (≈200 lines) and the OAuth2 flow (≈1,000 lines);
- `xdg-open` as an optional runtime dependency (xdg-utils, 315 KiB, no libraries).

Anything keyring-based adds GLib (≈100+ MiB installed) or Qt (≈66+ MiB) and should be optional at most.

### Cited Findings
- The libcurl features installed on the host include HTTPS, HTTP/2, IPv6, AsynchDNS and threadsafe. Protocols include http(s), imap(s), pop3(s) and smtp(s), and the library is linked to OpenSSL 3.5.6. — local `curl -V`; [curl docs: CURLOPT_XOAUTH2_BEARER](https://curl.se/libcurl/c/CURLOPT_XOAUTH2_BEARER.html)
- The JSON, listener, storage and browser footprint figures are as cited in Q2, Q3, Q6 and Q7 above.

### Inferences

| Need | Minimal choice | New pkg deps | Approx. new code |
|---|---|---|---|
| HTTPS (token, device, Graph) | libcurl (existing) | none | wrapper ≈100-200 lines |
| Loopback redirect | own BSD-socket listener | none | ≈150-300 lines |
| JSON, OAuth only | vendored jsmn (MIT) or own parser (Alpine-style ≈500 lines) | none | 0.5k lines |
| JSON, with Graph backend | vendored cJSON (MIT) or `devel/libcjson`; yyjson if speed matters; nlohmann if C++ ergonomics outweigh size | none if vendored | 3.5k lines (cJSON) |
| PKCE / random / base64url | OpenSSL (base) | none | ≈40 lines |
| Browser | `xdg-open` → `$BROWSER` → known browsers → show URL | xdg-utils (runtime, optional) | ≈50 lines |
| Token storage | AES-GCM + PBKDF2 via OpenSSL into Mulberry prefs; optional dlopen libsecret | none (libsecret optional) | ≈150 lines |
| ID token (optional) | base64url + existing JSON parser | none | ≈30 lines |
| SASL XOAUTH2/OAUTHBEARER | own (if own IMAP/SMTP) or libcurl | none | ≈50 lines |

- The Microsoft device-code flow removes the listener requirement for Microsoft accounts, and the same "show URL + code" dialog doubles as the no-browser fallback. For Google, the loopback flow remains necessary.

### Gaps
- I did not verify which Google scopes, if any, are allowed in Google's device flow (it is believed to exclude Gmail scopes).
- I did not measure gcc 14 object sizes; the clang 19 `-Os` figures are given instead.
