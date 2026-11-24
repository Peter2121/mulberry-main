# Architectural approaches for OAuth2 in a native desktop mail client (Mulberry: C++, X11 -> FLTK, FreeBSD/Linux, own IMAP/POP/SMTP/CalDAV code), as of late 2026

Research notes. Sources fetched 2026-11-24 unless noted. Items marked "(RFC text, not re-fetched)" cite the primary RFC from general knowledge; the report writer should treat them as standard and low-risk but they were not re-read in this session.

## Q1. Built-in authorization code flow + PKCE, system browser, loopback redirect (RFC 8252 / RFC 7636)

### Takeaway
This is the provider-recommended and de facto standard path for desktop clients: Google explicitly recommends loopback for Linux/macOS/Windows desktop apps and has removed custom schemes and OOB; Microsoft ignores the port on localhost redirects precisely to support ephemeral ports. It needs only an HTTP(S) client, a tiny one-shot HTTP listener, a JSON parser, base64url, SHA-256 and a CSPRNG, all of which a mail client with its own TLS stack already has or can trivially add.

### Cited Findings
- Google: the loopback IP redirect is the "recommended mechanism" for "macOS, Linux, and Windows desktop apps"; desktop apps "must open the system browser and supply a local redirect URI"; the app should "start an HTTP listener on a random available port"; `localhost` may be used instead of the IP literal but "may cause issues with client firewalls" — [Google: OAuth 2.0 for iOS & Desktop Apps](https://developers.google.com/identity/protocols/oauth2/native-app)
- Google: "Always use PKCE ... when requesting authorization codes"; code verifier 43–128 chars; S256 recommended — [Google native-app guide](https://developers.google.com/identity/protocols/oauth2/native-app)
- Google: "Desktop apps are public clients and cannot keep secrets confidential"; `client_secret` is listed as optional in token exchange (in practice Google desktop clients are still issued a secret that is not considered secret) — [Google native-app guide](https://developers.google.com/identity/protocols/oauth2/native-app)
- Google: custom URI schemes "are no longer supported due to the risk of app impersonation"; manual copy/paste (OOB) "is no longer supported"; loopback is deprecated only for mobile (Android/iOS/Chrome apps), not desktop — [Google native-app guide](https://developers.google.com/identity/protocols/oauth2/native-app)
- Microsoft Entra: per RFC 8252 §7.3/§8.3, `http` is acceptable for localhost redirects and "the port component ... is ignored for the purposes of matching a localhost redirect URI" (`http://localhost/MyApp` == `http://localhost:5000/MyApp`). Microsoft recommends `127.0.0.1` over `localhost`; IPv6 `[::1]` "isn't currently supported"; http+127.0.0.1 redirects can only be added via the app manifest (`replyUrlsWithType`), not the portal text box. Redirect URIs without a path are returned with a trailing slash — [Microsoft: Redirect URI best practices (updated 2026-06-15)](https://learn.microsoft.com/en-us/entra/identity-platform/reply-url)
- Thunderbird's provider table uses loopback-style redirect endpoints for several providers: Mail.ru, Yandex, Comcast `http://localhost`; Seznam `http://127.0.0.1/seznam`; Fastmail `http://127.0.0.1/fastmail`; Yahoo/AOL `https://127.0.0.1` (and a scheme redirect for Yahoo, see Q2); Google and Microsoft are flagged `useExternalBrowser: true` — [Thunderbird OAuth2Providers.sys.mjs (comm-central)](https://raw.githubusercontent.com/mozilla/releases-comm-central/master/mailnews/base/src/OAuth2Providers.sys.mjs)
- Claws Mail uses a fixed-port loopback `oa2_redirect_uri=http://127.0.0.1:8888` for Google, Outlook, Exchange and Microsoft GCC High, and extracts the code by string markers — [Claws Mail src/oauth2.c](https://git.claws-mail.org/?p=claws.git;a=blob_plain;f=src/oauth2.c;hb=HEAD)
- Alpine 2.26 switched the Gmail `redirect_uri` from `oob` to `http://localhost` because Google deprecated OOB, and added PKCE (S256 and plain) for Gmail and Outlook — [Alpine release notes](https://alpineapp.email/alpine/release/)
- Headless / remote sessions: mutt_oauth2.py's `localhostauthcode` flow requires the browser to run on the same machine, so it fails when mutt runs over SSH on a remote host — [mutt_oauth2.py README](https://gitlab.com/muttmua/mutt/-/raw/master/contrib/mutt_oauth2.py.README). pizauth offers a fixed `http_listen` port so users can `ssh -L` tunnel it — [pizauth README](https://github.com/ltratt/pizauth). oama: for remote hosts set `redirect_uri: http://localhost:<port>` and tunnel the port over SSH — [oama README](https://github.com/pdobsan/oama). email-oauth2-proxy's `--external-auth` mode has the user paste the final redirect URL back in — [email-oauth2-proxy README](https://github.com/simonrob/email-oauth2-proxy)
- RFC 8252 §7.3: loopback redirect, the AS MUST allow any port at request time; §8.3 recommends the IP literal over `localhost`; §8.1 requires PKCE for native apps; §8.12 recommends against embedded user agents; the listener should only bind to the loopback interface — [RFC 8252](https://www.rfc-editor.org/rfc/rfc8252) (RFC text, not re-fetched)
- RFC 7636 defines `code_verifier`/`code_challenge` with S256 and plain — [RFC 7636](https://www.rfc-editor.org/rfc/rfc7636) (RFC text, not re-fetched)

### Inferences
- Implementation sketch for Mulberry: bind a TCP socket to 127.0.0.1:0 (kernel picks port), build `redirect_uri=http://127.0.0.1:<port>/` (or `http://localhost:<port>/` where a provider registration requires literal "localhost", e.g. Google desktop clients accept both; Thunderbird uses `http://localhost` for Yandex/Mail.ru/Comcast), generate 32+ random bytes for `state` and the PKCE verifier, launch the browser with `xdg-open URL` via `posix_spawn`/`fork+execvp` (never through a shell), accept exactly one request matching the expected path, verify `state`, return a small "You can close this window" HTML page, close the socket, exchange the code at the token endpoint. Add a user-visible timeout/cancel (e.g. 5–10 min) and a "copy URL" fallback button for when xdg-open fails.
- On FreeBSD, `xdg-open` comes from `devel/xdg-utils`; it is not in base. A fallback chain (`$BROWSER`, `xdg-open`, `firefox`, then show URL) is sensible. (Port name from general knowledge; verify.)
- Remote X sessions (ssh -X): xdg-open will typically open the browser on the remote host (displayed over X forwarding) so loopback still works if that browser exists; if the user instead opens the URL in a local browser, the redirect hits the user's local 127.0.0.1 and fails. Providing a "paste the redirect URL" fallback (as email-oauth2-proxy `--external-auth` does) or device flow (Microsoft) covers this.
- Security: loopback listener is exposed to other local users on a multi-user box (any local process can race to connect); `state` + PKCE make a stolen code useless without the verifier, so risk is low. Bind to 127.0.0.1 only, single-shot, short timeout.
- Code size for a C++ implementation using existing TLS/HTTP code: low thousands of lines at most (Claws Mail's whole oauth2.c is roughly 1,000 lines including provider table and its own raw HTTP POST code — see Q10).

### Gaps
- Whether Yahoo and Yandex currently accept arbitrary-port `http://127.0.0.1` redirects for newly registered apps was not verified (Thunderbird uses `https://127.0.0.1` for Yahoo, which suggests Yahoo requires https even for loopback).
- No dedicated academic security analysis of loopback redirects was fetched (e.g. port-squatting or DNS-rebinding analyses); only RFC 8252 security considerations.

## Q2. Private-use URI scheme redirect (x-scheme-handler in a .desktop file)

### Takeaway
Poor fit for Mulberry: Google no longer supports custom schemes, Microsoft's guidance for desktop apps centres on localhost, and on FreeBSD/Linux the scheme must be registered via a .desktop file + `xdg-mime`, which needs single-instance IPC to hand the URL to the running client. Only useful for a provider that mandates it (Thunderbird uses one for Yahoo because "Yahoo set it").

### Cited Findings
- Google: "Custom URI schemes are no longer supported due to the risk of app impersonation" — [Google native-app guide](https://developers.google.com/identity/protocols/oauth2/native-app)
- Thunderbird's Yahoo entry uses `schemeRedirect: net.thunderbird://oauth/yahoo`; the source comments that it "isn't the normal net.thunderbird URL because Yahoo set it and we can't change it" — [Thunderbird OAuth2Providers.sys.mjs](https://raw.githubusercontent.com/mozilla/releases-comm-central/master/mailnews/base/src/OAuth2Providers.sys.mjs)
- RFC 8252 §7.1 allows private-use schemes using reverse-domain names, but notes any app can claim the same scheme (impersonation risk), hence PKCE is mandatory — [RFC 8252](https://www.rfc-editor.org/rfc/rfc8252) (RFC text, not re-fetched)

### Inferences
- On X11 desktops, registration means installing a `.desktop` file with `MimeType=x-scheme-handler/<scheme>;` and `Exec=mulberry --oauth-callback %u`, then the launched instance must forward the URL to the already-running Mulberry (socket/D-Bus). That is extra machinery and breaks without a desktop environment that honours xdg-mime (common on minimal FreeBSD window-manager setups).
- Not recommended except as a per-provider fallback.

### Gaps
- No current provider-by-provider table of which IdPs accept private-use schemes for desktop clients (Microsoft does accept custom schemes for "Mobile and desktop" platform registrations in practice, but this was not verified in a fetched source).

## Q3. Device authorization grant (RFC 8628)

### Takeaway
Excellent UX for headless/remote use and needs no listener, but provider support for mail scopes is uneven: Microsoft supports it for IMAP/POP/SMTP; Google does NOT allow `https://mail.google.com/` in the device flow; Yandex has a device-code flow. It is a complement to, not a replacement for, loopback.

### Cited Findings
- Google device flow allowed scopes are only `email`, `openid`, `profile`, `drive.appdata`, `drive.file`, `youtube`, `youtube.readonly`; `https://mail.google.com/` is not allowed — [Google: OAuth 2.0 for TV and Limited-Input Device Applications](https://developers.google.com/identity/protocols/oauth2/limited-input-device)
- Microsoft lists both "OAuth2 authorization code flow" and "OAuth2 device authorization grant flow" for obtaining IMAP/POP/SMTP tokens — [Microsoft: Authenticate an IMAP, POP or SMTP connection using OAuth](https://learn.microsoft.com/en-us/exchange/client-developer/legacy-protocols/how-to-authenticate-an-imap-pop-smtp-application-by-using-oauth); device code flow needs no redirect URI — [Microsoft redirect URI page](https://learn.microsoft.com/en-us/entra/identity-platform/reply-url)
- email-oauth2-proxy README: only Office 365 is known to support the device authorization grant among its providers — [email-oauth2-proxy](https://github.com/simonrob/email-oauth2-proxy)
- Yandex OAuth documents a device-code flow ("Prompts the user to enter user_code on the Device authorization page"), with error `unauthorized_client` when "The app was rejected during moderation or is awaiting moderation" — [Yandex OAuth: entering the code on the authorization page](https://yandex.com/dev/id/doc/en/codes/screen-code-oauth)
- Alpine: device flow is supported only for Outlook, where it is the default and needs no user-supplied credentials; authorize flow is default for Gmail, Yahoo, Yandex — [Alpine XOAUTH2 help](https://alpineapp.email/alpine/alpine-info/misc/xoauth2.html)
- oama supports device code with `--device` and says Microsoft "now primarily expects" this flow — [oama README](https://github.com/pdobsan/oama); sasl-xoauth2-tool calls device flow the preferred method for Outlook — [sasl-xoauth2 README](https://github.com/tarickb/sasl-xoauth2); mutt_oauth2.py supports `devicecode` — [mutt_oauth2.py README](https://gitlab.com/muttmua/mutt/-/raw/master/contrib/mutt_oauth2.py.README)

### Inferences
- UX: show the `user_code` and `verification_uri` in a dialog (also offer to open the URI with xdg-open), poll the token endpoint at `interval`, handle `authorization_pending`/`slow_down`/`expired_token`. Implementation is even simpler than loopback (no listener).
- Device-code phishing is a known attack class against Microsoft accounts (an attacker sends a user a device code); this is a reason some tenants block device flow via Conditional Access. (General knowledge; not verified in a fetched source this session.)

### Gaps
- Whether Yahoo offers device flow for mail scopes: no source found.
- Whether Microsoft tenants commonly disable device code flow by policy in 2026: not verified.

## Q4. Embedded browser (WebKitGTK, CEF, QtWebEngine)

### Takeaway
Discouraged by RFC 8252 and actively blocked by Google (`disallowed_useragent`); heavy dependency (WebKitGTK/QtWebEngine/CEF are tens to hundreds of MB) that conflicts with an X11/FLTK toolkit; Evolution, which started with WebKitGTK, added an external-browser option in 3.46. Not recommended.

### Cited Findings
- Google returns `disallowed_useragent` when the authorization endpoint is displayed in "an embedded user-agent disallowed by Google's OAuth 2.0 Policies" — [Google native-app guide](https://developers.google.com/identity/protocols/oauth2/native-app)
- RFC 8252 §8.12: embedded user-agents are unsafe for third parties (the app can capture credentials, no shared SSO session) and native apps MUST NOT use them for authorization requests — [RFC 8252](https://www.rfc-editor.org/rfc/rfc8252) (RFC text, not re-fetched)
- Evolution-data-server's built-in OAuth2 (2018) added e-oauth2-service classes and a WebKitGTK + json-glib build check — [GNOME commits list, Jan 2018](https://mail.gnome.org/archives/commits-list/2018-January/msg05171.html); users requested choosing another browser because of WebKitGTK problems with Microsoft 365 — [evolution-data-server issue 379](https://gitlab.gnome.org/GNOME/evolution-data-server/-/issues/379); 3.46.0 added a choice to open the OAuth2 wizard in any browser — [evolution-list, May 2022](https://lists.gnome.org/archives/evolution-list/2022-May/msg00091.html)
- email-oauth2-proxy's GUI mode uses pywebview popup windows (an embedded webview) by default, with `--external-auth` and `--local-server-auth` alternatives — [email-oauth2-proxy](https://github.com/simonrob/email-oauth2-proxy)

### Inferences
- For Mulberry (X11 toolkit now, FLTK later), embedding a web engine means pulling in GTK/WebKitGTK or Qt or CEF, contradicting the lightweight toolkit choice, and still being blocked by Google. Size numbers were not measured in this session (see Gaps).

### Gaps
- Concrete installed sizes of www/webkit2-gtk, QtWebEngine, CEF on FreeBSD were not looked up.
- Whether Microsoft also blocks embedded webviews: Microsoft does not block them as of the sources read (MSAL itself has embedded-webview modes), but this was not verified in a fetched source.

## Q5. External token helper commands (mutt/neomutt, aerc, isync/mbsync, msmtp, offlineimap; pizauth, oama, mailctl, sasl-xoauth2, getmail)

### Takeaway
The Unix CLI ecosystem's answer: the mail client only knows how to put a bearer token into SASL XOAUTH2/OAUTHBEARER and calls a user-configured command that prints a fresh access token; the helper owns browser launch, client IDs, refresh, and storage. Lowest effort and most flexible for Mulberry, but poor UX for non-technical users and still leaves the client-ID problem to the user. Ideal as an "advanced/escape hatch" option alongside a built-in flow.

### Cited Findings
- mutt: "set imap_authenticators=\"oauthbearer:xoauth2\"" and "set imap_oauth_refresh_command=\"/path/to/script/mutt_oauth2.py ${imap_user}.tokens\""; mutt "can send an OAuth2 access token ... but cannot obtain one itself"; the script supports `authcode`, `localhostauthcode`, `devicecode`; tokens are stored in a file encrypted via configurable `ENCRYPTION_PIPE`/`DECRYPTION_PIPE` (gpg in the example); users must supply client_id (and client_secret "if there is one"); borrowing another client's client_id is "a temporary punt" and "not an approach for configuring systems to be used by other people"; XOAUTH2 is described as older/experimental and mutt may drop it once providers support OAUTHBEARER — [mutt_oauth2.py README](https://gitlab.com/muttmua/mutt/-/raw/master/contrib/mutt_oauth2.py.README)
- aerc: source URLs `imaps+oauthbearer://` and `imaps+xoauth2://` with optional `token_endpoint`, `client_id`, `client_secret`, `scope`; when `token_endpoint` is given "the configured password is used as a refresh token to obtain an access token", otherwise "the password acts like an access token" — i.e. aerc itself does the refresh-token grant but not the initial authorization — [aerc-imap(5)](https://git.sr.ht/~rjarry/aerc/blob/master/doc/aerc-imap.5.scd)
- pizauth: daemon (`pizauth server`) + CLI (`pizauth show/refresh/reload/dump/restore/shutdown`); tokens kept only in memory ("never to disk"); `pizauth dump`/`restore` + `token_event_cmd` for user-managed encrypted persistence; random loopback port by default, `http_listen` to fix it for ssh tunnels; `auth_notify_cmd` for desktop notifications; per-account `auth_uri`, `token_uri`, `client_id`, `client_secret`, `scopes`, `auth_uri_fields={login_hint}`; integration examples `passwordeval pizauth show <acct>` (msmtp) and `PassCmd "pizauth show <acct>"` + `AuthMechs XOAUTH2` (mbsync); users "may need to create your own client ID and secret" for Microsoft/Gmail; no device code mentioned — [pizauth README](https://github.com/ltratt/pizauth)
- oama: PKCE + random `state` + dynamically generated random `redirect_uri`; device flow via `--device`; stores refresh tokens in GnuPG-encrypted files under `$XDG_STATE_HOME/oama` or in a FreeDesktop Secret Service keyring (GNOME Keyring, KWallet, KeePassXC); built-in Google and Microsoft; `client_id_cmd`/`client_secret_cmd` to fetch from a password manager; "runs on Linux, macOS and BSD Unix type systems"; lists msmtp, fdm, isync, aerc, neomutt, mutt as consumers — [oama README](https://github.com/pdobsan/oama)
- sasl-xoauth2: a Cyrus SASL client plugin adding XOAUTH2; JSON token file holds access token, expiry and refresh token and the plugin refreshes on expiry (Gmail endpoint default, Outlook needs `token_endpoint`); initial token via `sasl-xoauth2-tool get-token` (Gmail: local 127.0.0.1 redirect, browser must be on same host; Outlook: device flow preferred); client creds in `/etc/sasl-xoauth2.conf`; aimed at Postfix relay — [sasl-xoauth2 README](https://github.com/tarickb/sasl-xoauth2)

### Inferences
- For Mulberry, the minimal "helper" mode is: per-account option "OAuth2 via external command", run the command (via `posix_spawn`, no shell or `sh -c` like aerc), read one line = access token, use it in XOAUTH2/OAUTHBEARER; on auth failure re-run the command once. Optionally aerc-style: store a refresh token as the "password" and let Mulberry do only the refresh grant.
- Pros: zero client-ID liability for the project, works with any provider the helper supports, works headless. Cons: users must install/configure a separate tool and register their own OAuth app (Google registration for a restricted scope as an individual "testing" app has its own friction); no GUI flow.

### Gaps
- mailctl, offlineimap's built-in OAuth (`oauth2_request_url`, `oauth2_refresh_token`), and getmail's `getmail-gmail-xoauth-tokens` were not fetched; their designs are similar (refresh token in config + token endpoint) but unverified here.
- isync/mbsync XOAUTH2 relies on a Cyrus SASL XOAUTH2 plugin (e.g. sasl-xoauth2 or cyrus-sasl-xoauth2) plus `PassCmd`; sasl-xoauth2's README did not mention mbsync explicitly.

## Q6. Local IMAP/SMTP proxy (email-oauth2-proxy, DavMail)

### Takeaway
Fully transparent to the client (it keeps using PLAIN/LOGIN against 127.0.0.1), so it requires no Mulberry changes, but it adds a Python (or Java, for DavMail) daemon, plaintext or self-signed TLS on the local hop, and still requires a client ID. Good thing to document as a workaround; not an architecture to build on.

### Cited Findings
- email-oauth2-proxy intercepts IMAP/POP/SMTP authentication commands and replaces them with SASL (X)OAuth 2.0; local listeners e.g. 127.0.0.1:1993/1995/1587; client should be set to "unencrypted" because the proxy needs to read the traffic; upstream is always TLS/STARTTLS; local STARTTLS for IMAP/POP is a "potential future improvement" — [email-oauth2-proxy](https://github.com/simonrob/email-oauth2-proxy)
- Its "password" is "used only to encrypt and decrypt the OAuth 2.0 authentication token"; tokens are cached in the config file by default or via `--cache-store` — [email-oauth2-proxy](https://github.com/simonrob/email-oauth2-proxy)
- Providers: Office 365/Outlook/Hotmail, Gmail/Workspace, Fastmail, Yahoo, AOL, Comcast, 21Vianet; Python 3.7+, pystray, pywebview — [email-oauth2-proxy](https://github.com/simonrob/email-oauth2-proxy)
- Status notes in that README: Microsoft made OAuth mandatory for IMAP/POP in Office 365 in January 2023 and for personal Outlook/Hotmail in September 2024; Office 365 SMTP AUTH basic auth retirement "scheduled for late 2027"; "AOL and Yahoo are not currently allowing new client registrations with the OAuth email scope" — [email-oauth2-proxy](https://github.com/simonrob/email-oauth2-proxy)

### Inferences
- DavMail (Java) offers the same transparent-proxy pattern for Exchange/O365 (EWS/Graph -> IMAP/SMTP/CalDAV/LDAP); it was not fetched in this session.

### Gaps
- DavMail's current (2026) OAuth modes and Graph backend status not verified.

## Q7. Desktop-environment account services (GNOME Online Accounts, KDE KAccounts/Signon)

### Takeaway
GOA ties the client to GNOME's D-Bus services and GTK; Geary depends on it and suffers when GOA is absent or a provider/tenant type isn't supported. GOA exists in FreeBSD ports, but making an X11/FLTK client depend on it would exclude non-GNOME users and gives no control over client IDs or providers. Not recommended as primary; at most an optional integration.

### Cited Findings
- Geary FAQ: Geary uses gnome-online-accounts to implement OAuth2 and SSO; without GOA you must use plain login/password — [Geary FAQ](https://wiki.gnome.org/Apps/Geary/FAQ); known limitation: OAuth2 IMAP via GOA cannot be combined with a non-Google SMTP server — [Geary issue 1355](https://gitlab.gnome.org/GNOME/geary/-/issues/1355); 2025 report of a personal Microsoft account not being picked up — [GNOME Discourse](https://discourse.gnome.org/t/geary-not-picking-personal-microsoft-account-on-ubuntu-25-10/32312)
- GOA's "Microsoft" provider was renamed "Microsoft Personal" because work/school accounts did not work; evolution-data-server recognises "Microsoft 365" GOA accounts (needs evolution-ews) — [Debian salsa GOA diff](https://salsa.debian.org/gnome-team/gnome-online-accounts/compare/be61eae...8806be2), [Debian salsa EDS diff](https://salsa.debian.org/gnome-team/evolution-data-server/compare/d8a8fab...d1f5c52)
- Users ask for third-party (arbitrary provider) OAuth2 support in Evolution/GOA, indicating GOA supports only a handful of providers — [GNOME Discourse](https://discourse.gnome.org/t/add-third-party-oauth2-support-to-evolution-and-or-gnome-online-accounts/24877)
- FreeBSD ports: `net/gnome-online-accounts` exists — [FreshPorts](https://www.freshports.org/net/gnome-online-accounts); `net/msgraph` (GNOME MS Graph library) added Jan 2025, depends on GOA — [FreeBSD dev-commits-ports-all](https://lists.freebsd.org/archives/dev-commits-ports-all/2025-January/144642.html); a FreeBSD forum user could not confirm Office 365 OAuth2 with evolution-ews — [FreeBSD forums](https://forums.freebsd.org/goto/post?id=526940)

### Inferences
- KDE KAccounts / signond: not researched in detail; KMail itself does not rely on it for Gmail (it uses KGAPI, see Q10). On FreeBSD, KDE is available in ports but this path would add Qt/KDE frameworks dependency.

### Gaps
- KAccounts/Signon provider coverage and FreeBSD availability (`sysutils/signond`?) not verified.

## Q8. Cyrus SASL / GNU SASL libraries with XOAUTH2/OAUTHBEARER

### Takeaway
SASL libraries only frame the token on the wire; they do not acquire or refresh tokens (except plugins like sasl-xoauth2 that read a token file). Since Mulberry has its own IMAP/POP/SMTP code and both mechanisms are a single client-first message, implementing them natively is trivial and avoids a dependency.

### Cited Findings
- sasl-xoauth2 is a Cyrus SASL plugin for client-side XOAUTH2 that refreshes from a token file but needs an external tool for initial acquisition — [sasl-xoauth2](https://github.com/tarickb/sasl-xoauth2)
- KMail/Akonadi implemented Gmail XOAUTH2 as "a custom SASL plugin" with token request/refresh via KGAPI — [KDE bug 354508](https://bugs.kde.org/show_bug.cgi?id=354508)
- XOAUTH2 wire format: `base64("user=" + userName + "^Aauth=Bearer " + accessToken + "^A^A")`; IMAP `AUTHENTICATE XOAUTH2 <b64>`, POP `AUTH XOAUTH2` then continuation, SMTP `AUTH XOAUTH2 <b64>`; failure examples `NO AUTHENTICATE failed`, `-ERR ...`, `535 5.7.3` — [Microsoft IMAP/POP/SMTP OAuth doc](https://learn.microsoft.com/en-us/exchange/client-developer/legacy-protocols/how-to-authenticate-an-imap-pop-smtp-application-by-using-oauth)

### Inferences
- GNU SASL (libgsasl) implements OPENID20/SAML20 and (since 1.x/2.x) XOAUTH2 and OAUTHBEARER, but again needs a token callback (general knowledge, not verified this session). Not worth a dependency.

### Gaps
- GNU SASL's exact mechanism list/version for XOAUTH2/OAUTHBEARER not verified.

## Q9. Protocol details: XOAUTH2 vs OAUTHBEARER, re-auth on expiry/IDLE, refresh, token storage, multiple accounts, revocation

### Takeaway
Implement both mechanisms (OAUTHBEARER per RFC 7628 preferred when advertised; XOAUTH2 for Microsoft and others). Handle the server's JSON error challenge correctly (send `^A`), refresh proactively before `expires_in`, and on reconnect always fetch a valid token. Microsoft documents only XOAUTH2; Alpine uses OAUTHBEARER for Yahoo.

### Cited Findings
- OAUTHBEARER initial response: `n,a=user@example.com,^Ahost=server.example.com^Aport=143^Aauth=Bearer <token>^A^A`; on failure server sends JSON `{"status":"invalid_token","scope":...,"openid-configuration":"https://..."}` as a challenge; client MUST respond with a single `%x01` (base64 `AQ==`) or abort (`*` in IMAP); client "SHOULD NOT cache a single returned value" of openid-configuration; client "MAY request a new access token for each connection" but "SHOULD cache and reuse valid credentials"; resource servers may disconnect clients when tokens expire — [RFC 7628](https://www.rfc-editor.org/rfc/rfc7628.txt)
- Microsoft documents SASL XOAUTH2 for IMAP/POP/SMTP and shared mailbox access by putting the shared mailbox address in `user=`; scopes `https://outlook.office.com/IMAP.AccessAsUser.All`, `.../POP.AccessAsUser.All`, `.../SMTP.Send`, plus `offline_access` for refresh tokens — [Microsoft IMAP/POP/SMTP OAuth doc](https://learn.microsoft.com/en-us/exchange/client-developer/legacy-protocols/how-to-authenticate-an-imap-pop-smtp-application-by-using-oauth). (pizauth/Claws use `https://outlook.office365.com/...` resource URLs, which also work — [pizauth](https://github.com/ltratt/pizauth).)
- Alpine: Yahoo uses `/auth=oauthbearer` (or xoauth2); since 2.24 Alpine "requests a new access token shortly before the current one expires and replaces the session without user action"; tokens saved in Alpine's password file — [Alpine XOAUTH2 help](https://alpineapp.email/alpine/alpine-info/misc/xoauth2.html)
- Claws Mail shortens stored token lifetime: "Reduce available token life to avoid attempting connections with (near) expired tokens"; notes "Some providers issue replacement refresh tokens with each access token"; stores access token, expiry, refresh token in its password store — [Claws Mail oauth2.c](https://git.claws-mail.org/?p=claws.git;a=blob_plain;f=src/oauth2.c;hb=HEAD)
- pizauth warns that tokens can become invalid before expiry and there may be a delay before it notices — [pizauth README](https://github.com/ltratt/pizauth)
- Token storage choices in the ecosystem: memory-only + user-encrypted dump (pizauth); gpg-encrypted file or Secret Service keyring (oama); gpg-encrypted token file (mutt_oauth2.py); client's own password store (Claws, Alpine); encrypted with a local password in the config file (email-oauth2-proxy) — sources as cited in Q5/Q6/Q10.
- Multiple accounts: `login_hint` passed as an extra authorization parameter (pizauth `auth_uri_fields = {"login_hint": ...}`) — [pizauth README](https://github.com/ltratt/pizauth)
- Revocation: Alpine documents manual revocation for Gmail, Outlook (account.microsoft.com vs myaccount.microsoft.com), Yahoo, Yandex — [Alpine XOAUTH2 help](https://alpineapp.email/alpine/alpine-info/misc/xoauth2.html). RFC 7009 defines a token revocation endpoint (Google supports `https://oauth2.googleapis.com/revoke`) — (RFC/general knowledge, not re-fetched)

### Inferences
- IDLE: an established IMAP session is not invalidated merely because the access token expires at most providers (Gmail/Exchange keep the session; RFC 7628 only says servers "may" disconnect). Mulberry should: (a) refresh tokens on a timer at ~expires_in minus 5 min (or lazily before each new connection/AUTHENTICATE), (b) on `NO`/`535`/JSON `invalid_token`, refresh once and retry, then fall back to interactive re-authorization, (c) on `invalid_grant` from the token endpoint (revoked/expired refresh token, password change, Google 7-day expiry for "Testing" apps) prompt the user. Persist rotated refresh tokens atomically.
- Storage recommendation for Mulberry: treat the refresh token as the account "password" in Mulberry's existing password-storage mechanism (with its master-password encryption if any), with optional Secret Service (libsecret/D-Bus) backend; never store access tokens long-term.
- Key per-account identity: (issuer, client_id, username/login_hint). Use `login_hint` so the browser pre-selects the right account when several accounts at the same provider exist; verify the account email from the id_token or userinfo if `openid email` scopes are requested.

### Gaps
- Exact behaviour of Gmail/Exchange Online/Yahoo on long-lived IDLE sessions after token expiry was not verified from primary sources.
- Google's 7-day refresh-token expiry for apps in "Testing" publishing status (relevant to users who register their own client) was not re-verified.

## Q10. How existing clients implement OAuth2

### Takeaway
Two families: (a) full GUI clients that embed per-provider tables with project-registered client IDs and run the flow themselves (Thunderbird, Evolution, KMail, Claws, Alpine) or delegate to the desktop (Geary/GOA); (b) CLI clients that only consume tokens from helpers (mutt/neomutt, aerc, mbsync, msmtp). The small C clients (Claws, Alpine) show the built-in approach is feasible in roughly one source file of ~1k lines on top of an existing TLS socket layer.

### Cited Findings
- Thunderbird: hard-coded provider table keyed by issuer and hostname, with client IDs (and secrets for Google, Mail.ru, Yandex, Comcast) for Google, Microsoft, Yahoo, AOL, Yandex, Mail.ru, Seznam, Fastmail, Comcast, Thundermail; PKCE for most; RFC 9207 issuer identifiers for some; "For the moment these details are hard-coded, since dynamic client registration is not yet supported"; "Don't copy these values for your own application - register one for yourself!"; `registerProvider` exists for extensions; no openid-configuration discovery in this file — [Thunderbird OAuth2Providers.sys.mjs](https://raw.githubusercontent.com/mozilla/releases-comm-central/master/mailnews/base/src/OAuth2Providers.sys.mjs)
- Evolution: built-in OAuth2 services for Google and Outlook.com in evolution-data-server (WebKitGTK-based, external browser option since 3.46); can also use GOA accounts — [Gentoo oauth-gtk4 USE flag](https://packages.gentoo.org/useflags/oauth-gtk4), [GNOME commits list](https://mail.gnome.org/archives/commits-list/2018-January/msg05171.html), [evolution-list 2022](https://lists.gnome.org/archives/evolution-list/2022-May/msg00091.html)
- KMail/Akonadi: Gmail XOAUTH2 since 2017 via a custom SASL plugin and KGAPI — [KDE bug 354508](https://bugs.kde.org/show_bug.cgi?id=354508); requests for OAuth2 for non-Gmail IMAP still open/confirmed in 2024 — [KDE bug 457233](https://bugs.kde.org/show_bug.cgi?id=457233); institutional Google accounts reported failing in 2025 — [KDE bug 505428](https://mail.kde.org/pipermail/kdepim-bugs/2025-June/150146.html)
- Claws Mail (C, GTK): single `oauth2.c` (~1,000 lines by estimate), compiled under `USE_OAUTH2`; built-in `oauth2rc` template with Google, Outlook, Exchange, Microsoft_gcchigh, Yahoo; loopback `http://127.0.0.1:8888` for Google/Microsoft, `oob` copy/paste for Yahoo; users can enter custom client ID/secret per account; uses its own socket + GnuTLS layer and builds HTTP POSTs with snprintf; parses JSON with regexes; Yahoo needs `Authorization: Basic` on token requests. The fetch summary also reported `ssl_cert_auto_accept = TRUE` in the token-request code (i.e. possibly not validating the token endpoint certificate) — this should be verified directly before being repeated — [Claws Mail oauth2.c](https://git.claws-mail.org/?p=claws.git;a=blob_plain;f=src/oauth2.c;hb=HEAD)
- Alpine (C): XOAUTH2 for Gmail since 2.22, Outlook since 2.23 (device) / 2.24 (authorize), Yahoo and Yandex since 2.24; AOL "not possible at this time"; Gmail users must register their own client ID/secret and enter them in the XOAUTH2 config screen; Outlook device flow works with Alpine's built-in ID; a selection screen chooses among multiple client-ids/methods (device or authorize) per server; Gmail 2.22–2.25 used OOB, 2.26+ required; authorize flow = user copies code back into Alpine; tokens in password file; proactive refresh since 2.24 — [Alpine XOAUTH2 help](https://alpineapp.email/alpine/alpine-info/misc/xoauth2.html), [Alpine release notes](https://alpineapp.email/alpine/release/)
- Geary: delegates entirely to GOA — [Geary FAQ](https://wiki.gnome.org/Apps/Geary/FAQ)
- mutt/neomutt, aerc, mbsync, msmtp: token consumers via helper commands — see Q5 sources.

### Inferences
- Thunderbird's per-issuer/per-hostname table is the model most worth copying for Mulberry's provider configuration, with the addition of a user-override for client_id/secret (as Claws and Alpine allow).

### Gaps
- Sylpheed: no OAuth2 research done; believed to lack built-in OAuth2 (unverified).
- Neomutt specifics (beyond mutt's shared mechanism) not fetched.
- Exact line counts for Alpine's XOAUTH2 code (in its c-client / pith tree) not measured.

## Q11. The client-ID problem and provider configuration/discovery

### Takeaway
Every provider requires a registered client. Shipping project client IDs (Thunderbird model) gives the best UX but for Gmail's `https://mail.google.com/` restricted scope the project must pass Google verification with an annual CASA security assessment; Yahoo/AOL reportedly are not accepting new registrations for mail scope; Yandex moderates apps. Letting users supply their own IDs (mutt, pizauth, Alpine-for-Gmail) avoids project liability but is a hard UX barrier. A hybrid — built-in table + per-account override + helper-command escape hatch — is what the more pragmatic clients converge on. Discovery (RFC 8414 / RFC 7628 openid-configuration) cannot remove the need for a registered client_id, and Thunderbird still hard-codes endpoints.

### Cited Findings
- Google: IMAP/SMTP requires `https://mail.google.com/`, so apps "must be submitted for restricted scope verification"; using that scope only for SMTP sending violates minimum-scope policy; restricted-scope apps are subject to CASA assessment, revalidated annually — [Google Cloud OAuth verification FAQ](https://support.google.com/cloud/answer/13463817)
- Google desktop clients are public clients; the secret "cannot be kept confidential" — [Google native-app guide](https://developers.google.com/identity/protocols/oauth2/native-app)
- email-oauth2-proxy: you can register your own client or reuse one from another open-source client, but "access through reused tokens will be associated with the token owner rather than your own client"; "AOL and Yahoo are not currently allowing new client registrations with the OAuth email scope"; personal Hotmail users without an Azure/M365 developer account may need to reuse an existing client ID — [email-oauth2-proxy](https://github.com/simonrob/email-oauth2-proxy)
- Yandex returns `unauthorized_client` when "The app was rejected during moderation or is awaiting moderation" — [Yandex OAuth docs](https://yandex.com/dev/id/doc/en/codes/screen-code-oauth)
- Thunderbird hard-codes IDs and endpoints "since dynamic client registration is not yet supported"; issuer identifiers (RFC 9207) are recorded for some providers — [Thunderbird OAuth2Providers.sys.mjs](https://raw.githubusercontent.com/mozilla/releases-comm-central/master/mailnews/base/src/OAuth2Providers.sys.mjs)
- Alpine: Gmail users register Alpine as an "internal" (Workspace) or "external" (personal) app themselves; Outlook device flow uses Alpine's own ID — [Alpine XOAUTH2 help](https://alpineapp.email/alpine/alpine-info/misc/xoauth2.html)
- mutt calls borrowing an ID "a temporary punt" — [mutt_oauth2.py README](https://gitlab.com/muttmua/mutt/-/raw/master/contrib/mutt_oauth2.py.README); Thunderbird source: "Don't copy these values for your own application" — [Thunderbird source](https://raw.githubusercontent.com/mozilla/releases-comm-central/master/mailnews/base/src/OAuth2Providers.sys.mjs)
- RFC 7628 server error may carry an `openid-configuration` URL for discovery; client must discover endpoints before the OAuth exchange (OIDC Discovery or WebFinger) — [RFC 7628](https://www.rfc-editor.org/rfc/rfc7628.txt)
- Microsoft: multi-tenant + personal accounts app registrations do not allow query parameters in redirect URIs and are limited to 100 redirect URIs; http loopback with 127.0.0.1 only via manifest — [Microsoft redirect URI page](https://learn.microsoft.com/en-us/entra/identity-platform/reply-url)

### Inferences
- Recommended model for Mulberry: a data-driven provider table (issuer, hostnames/domain match, auth/token/revocation endpoints, scopes per protocol incl. CalDAV, PKCE on/off, redirect style, extra params like `prompt`/`login_hint`, preferred SASL mechanism, optional default client_id/secret) loaded from a shipped config file that users/distributors can override (the FreeBSD port or a Linux distro could inject their own registered IDs at build time). Plus per-account override fields, plus RFC 8414 / OIDC discovery for unknown issuers when the user supplies a client_id (e.g. self-hosted Keycloak/Dovecot with OAUTHBEARER). Thunderbird's ISPDB autoconfig XML also carries an OAuth2 authentication marker that could drive provider detection (not verified in this session).
- Obtaining Google verification as a small open-source project is a significant, recurring cost (annual CASA). Microsoft registration of a multi-tenant public client is free and feasible; individual tenants may still require admin consent (mutt README notes admin approval may be needed).

### Gaps
- Exact Thunderbird ISPDB `<authentication>OAuth2</authentication>` / `<oAuth2>` element schema not fetched.
- Whether Google exempts open-source desktop clients that do not store data server-side from CASA: no authoritative source found.
- Current (late 2026) Yahoo/AOL registration policy beyond the email-oauth2-proxy README statement not verified with Yahoo primary docs.
- OAuth 2.1 draft status (draft-ietf-oauth-v2-1) as of late 2026 not checked; its main relevant points (PKCE mandatory, no implicit/ROPC, exact redirect matching except loopback port) are consistent with RFC 8252/9700 BCP.

## Q12. Comparison summary for Mulberry

### Takeaway
Best-fit architecture: built-in auth code + PKCE + system browser + 127.0.0.1 ephemeral-port loopback as the default, device flow where the provider supports mail scopes (Microsoft, Yandex), a "paste redirect URL" fallback for remote X sessions, a data-driven provider table with optional built-in client IDs and per-account overrides, refresh token stored in Mulberry's password store, native XOAUTH2+OAUTHBEARER SASL, and an external "token command" mode as escape hatch. Avoid embedded browsers, GOA dependency, and private-use URI schemes.

### Cited Findings
- Loopback recommended by Google for Linux desktops; custom schemes and OOB removed — [Google native-app guide](https://developers.google.com/identity/protocols/oauth2/native-app)
- Microsoft ignores localhost port; device flow supported for IMAP/POP/SMTP — [Microsoft redirect URI page](https://learn.microsoft.com/en-us/entra/identity-platform/reply-url), [Microsoft IMAP/POP/SMTP OAuth doc](https://learn.microsoft.com/en-us/exchange/client-developer/legacy-protocols/how-to-authenticate-an-imap-pop-smtp-application-by-using-oauth)
- Google device flow excludes mail scope — [Google limited-input device guide](https://developers.google.com/identity/protocols/oauth2/limited-input-device)
- Small C clients do it in-process with modest code (Claws oauth2.c; Alpine) — [Claws oauth2.c](https://git.claws-mail.org/?p=claws.git;a=blob_plain;f=src/oauth2.c;hb=HEAD), [Alpine XOAUTH2 help](https://alpineapp.email/alpine/alpine-info/misc/xoauth2.html)

### Inferences
Comparison matrix (synthesised from the findings above):

| Approach | Security | UX | Effort for Mulberry | Ext. deps | Provider coverage | Used by |
|---|---|---|---|---|---|---|
| 1. Loopback + PKCE + system browser | Good (RFC 8252 BCP; local-port race mitigated by state+PKCE) | Good on local desktop; needs fallback for remote/headless | Low–medium (listener, HTTP POST, JSON, SHA-256/base64url, xdg-open) | xdg-utils (runtime, optional) | All major (Google, MS, Yandex, Mail.ru, Fastmail, Comcast; Yahoo with https loopback/scheme) | Thunderbird, Claws, Alpine (Gmail), pizauth, oama, sasl-xoauth2-tool, mutt_oauth2.py |
| 2. Private-use URI scheme | Weaker (impersonation) | OK if DE honours xdg-mime | Medium (desktop file + single-instance IPC) | DE integration | Google: not supported; Yahoo: used by TB | Thunderbird (Yahoo only) |
| 3. Device grant (RFC 8628) | Good, but device-code phishing risk | Very good headless; extra step locally | Low | none | Microsoft, Yandex; NOT Google mail | Alpine (Outlook), oama, mutt_oauth2.py, sasl-xoauth2-tool, email-oauth2-proxy |
| 4. Embedded webview | Poor (app sees credentials) | Seamless when it works | High | WebKitGTK/Qt/CEF (large) | Google blocks | Evolution (default, ext. browser option since 3.46), email-oauth2-proxy GUI |
| 5. External token command | Depends on helper | Poor for non-experts | Very low | helper tool | Whatever helper supports | mutt/neomutt, aerc, mbsync, msmtp |
| 6. Local proxy | Local plaintext hop | Transparent after setup | Zero | Python/Java daemon | Broad | email-oauth2-proxy, DavMail users |
| 7. GOA/KAccounts | Good | Seamless on GNOME only | Medium (D-Bus) | GNOME/KDE stack | Few providers; MS work accounts issues | Geary, Evolution (optional) |
| 8. SASL library | n/a (framing only) | n/a | Low but unnecessary | Cyrus/GNU SASL | n/a | KMail (custom plugin), Postfix via sasl-xoauth2 |

### Gaps
- No measured binary-size or LOC figures for a C++ implementation; estimates are inferred from Claws Mail.
- CalDAV/CardDAV OAuth scopes per provider (Google `https://www.googleapis.com/auth/calendar`, Fastmail, etc.) not researched beyond Thunderbird's per-type scope note.
