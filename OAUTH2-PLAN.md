# OAuth2 authentication: implementation plan

*Written 2026-11-25. Not implemented yet: no code exists. It is based on the
research report `reports/OAuth2 for Mulberry mail client.md` (the decisions
and their reasons) and on the notes in `research_notes/OAuth2 for Mulberry
mail client/` (code analysis with file and line references). The
registration guides for the test accounts are in `docs/oauth2/`. Microsoft Graph
(Microsoft 365 calendar and contacts) is **not** part of this plan; it gets
its own plan later, on top of the token engine built here.*

## 1. Goal and scope

Mulberry signs in to mail servers with OAuth2 instead of a password where the
provider requires or offers it.

| In scope | Out of scope (later plans) |
|---|---|
| IMAP, SMTP, POP3, ManageSieve with SASL XOAUTH2 and OAUTHBEARER | Microsoft Graph (Microsoft 365 / Outlook.com calendar and contacts, `sendMail`) |
| CalDAV and CardDAV with `Authorization: Bearer` (Google calendar and contacts) | A project-owned, Google-verified client ID |
| Providers: Google (bring your own client ID), Microsoft (personal and work), Yahoo (only if Yahoo grants mail access), Yandex, Mail.ru, Zoho (table entries, untested here) | LDAP, ACAP, IMSP with OAuth2 |
| Sign-in in the system browser (authorization code + PKCE, loopback redirect), device code, paste-the-URL fallback, external token command | Embedded browser |
| Secrets (passwords, refresh tokens, client secrets) in the system keyring (Secret Service through libdbus-1, task 2.4) or in Mulberry's encrypted keyring, chosen when no keyring is found at start (2.5) | The libsecret, KWallet and Qt keychain libraries; macOS and Windows system stores (notes only, for the FLTK port) |

### Decisions already taken (report)

- **Built into Mulberry** as a fourth authenticator type (`eOAuth2`, class
  `CAuthenticatorOAuth2`), not as a SASL plugin. The plugin interface passes
  credentials in 256-byte buffers, and HTTP clients never use plugins.
- **Toolkit-independent core** in `Libraries/OAuth2`, with a command-line
  test tool, as for the HTML viewer (`Libraries/HTMLView`, `html2png`). The
  FLTK rewrite reuses it unchanged.
- **Sign-in flow:** authorization code + PKCE (S256), the system browser, and
  a one-shot listener on the loopback address (RFC 8252). Fallbacks: device
  code (Microsoft), paste the final URL, an external token command.
- **SASL:** XOAUTH2 and OAUTHBEARER, implemented in Mulberry's own protocol
  code; not Cyrus SASL (KMail's plugin problems: report, KMail section).
- **No new link-time packages:**
  - libcurl, already linked for the HTML viewer, becomes a general
    dependency;
  - OpenSSL and libsodium are already required;
  - one MIT JSON library is vendored: nlohmann/json, also for Graph later
    (decision D1).
- **Secrets:** refresh tokens in the keyring chosen by the user, the system
  keyring or Mulberry's libsodium keyring (`.keyring`), never in the
  preferences file. Access tokens stay in memory only.
- **No password in the preferences file, ever (decided 2026-11-25, required).**
  Today "Save password" writes account passwords into `preferences.mbp`,
  only scrambled with a fixed key. A client that does this would never be
  accepted by the large providers. Tasks 2.6, 2.7 and 2.9 stop it for every account
  type, not only for OAuth2, and move the passwords already saved into the
  keyring.
- **Google: bring your own client ID** (the developer's own Google Cloud
  project), so that tests do not use up a shared project's 100-user cap.
- **Minimal GUI change:**
  - one "OAuth2" item in the authentication popup, with a small panel;
  - one sign-in dialog;
  - nothing else in the windows, since the GUI will be rewritten with FLTK.

## 2. Test accounts and what they can test

| Code | Account | Registration | Account-side preparation |
|---|---|---|---|
| **G** | Google, personal (Gmail) | Own Google Cloud project in **Personal mode**, Desktop client (`docs/oauth2/register-google.md`) | Nothing for IMAP (always on since 2025); POP enabled for the POP test; a test calendar and test contacts |
| **MP** | Microsoft, personal (Outlook.com) | The Mulberry test app, "any Entra ID tenant + personal Microsoft accounts" (`docs/oauth2/register-microsoft.md`). A personal account alone can no longer register apps: the app lives in the work tenant or in the free directory of an Azure free account | **IMAP and POP are off by default** on Outlook.com: turn them on in Settings → Mail → Forwarding and IMAP |
| **MW** | Microsoft 365, work | The same app registration | IMAP, POP and authenticated SMTP enabled for the mailbox (admin). **Admin consent is probably needed**: the default managed consent policy forbids users to consent to `IMAP.AccessAsUser.All` and `POP.AccessAsUser.All` |
| **Y** | Yahoo, personal, free | Yahoo app; mail scopes only after Yahoo approves (`docs/oauth2/register-yahoo.md`). The account works with OAuth2 in Thunderbird only because Mozilla is an approved Yahoo partner, with its own client ID and a redirect Yahoo set for it; Mulberry may not reuse them | App password for the regression tests |

Expected coverage (✓ testable, — not offered by the provider, ? depends on
approval or settings):

| | IMAP | SMTP | POP3 | Sieve | CalDAV | CardDAV | Device code |
|---|---|---|---|---|---|---|---|
| G | ✓ | ✓ | ✓ | — | ✓ | ✓ | — (not for mail scopes) |
| MP | ✓ | ✓ | ✓ | — | — (Graph) | — (Graph) | ✓ |
| MW | ✓ | ? (tenant SMTP AUTH; off by default from end-2026) | ? (per mailbox) | — | — (Graph) | — (Graph) | ✓ |
| Y | ? | ? | ? | — | — | — | — |
| Y with app password | ✓ regression of PLAIN/LOGIN | ✓ | ✓ | — | — | — | — |

ManageSieve with OAuth2 has no test server among these accounts (task 4.6).

## 3. Rules for every task

- **Branch `oauth2`**, created from `master`. One task, one commit; each task
  is reviewed and committed by the developer before the next starts.
- **After every task the build and the program stay correct:**
  - a full build from a clean tree with `scripts/build-freebsd.sh`, and also
    with `-n` (`--disable-html-viewer`): OAuth2 must not depend on the HTML
    viewer;
  - Mulberry starts. Every existing account, all of them using passwords,
    certificates or plugins, works exactly as before;
  - existing preference files load unchanged, and a preference file saved
    without OAuth2 accounts is unchanged.
- **Unfinished paths fail cleanly.** Until a protocol supports OAuth2,
  choosing OAuth2 for it gives a clear error ("OAuth2 is not supported for
  POP3 yet"), never a crash or a silent fallback to a password.
- **The core knows nothing about JX or Mulberry.** `Libraries/OAuth2` is
  compiled without Mulberry's prefix header. It knows HTTP (libcurl), JSON,
  sockets and the browser launch. It talks to its host through callbacks:
  - opening a URL;
  - showing a message or a device code;
  - storing and reading a secret;
  - logging.
- **No secret is ever written to the preferences file.** This covers
  passwords, refresh tokens and per-account OAuth2 client secrets. They go to
  the secret store (2.3–2.5), or stay in memory when the user declines a
  keyring. Every task that adds a stored setting checks this.
- **Tokens never appear in logs**, not even with "Show Authentication Details".
  - Tokens, authorization codes, PKCE verifiers and client secrets are
    replaced by `<redacted N bytes>`.
  - Every task that adds logging checks this.
- **Two kinds of checks:**
  - *Offline*, in `gmake -C Libraries/OAuth2 check`, against a local mock
    server (Python, like `Libraries/HTMLView/tests/fetch-server.py`). It
    needs no network and no account.
  - *Live*, by hand, with the accounts of section 2. Each task names the
    accounts.
- **Test credentials stay out of git.** The client IDs and secrets used for
  testing go into the user's provider file,
  `~/.config/mulberry/oauth2-providers.conf.json` (section 7). Mulberry and the
  test tool both read it, so no separate test file is needed. The state of each
  test account (consent, mailbox settings) is noted in this plan's section 8.
  Real `.eml` samples, logs with headers and token files are git-ignored.
- **No hard-coded product name.** Mulberry will at some point become a
  separate fork with another name, keeping the same copyright and licence.
  - **One application identity:** a single structure, filled from one build
    setting (`configure`), gives:
    - the application name, used in dialogs, on the browser's "you can close
      this window" page, and in log lines;
    - the version;
    - the HTTP `User-Agent` of token requests: neutral by default, like the
      image fetcher's `Mozilla/5.0`;
    - the configuration folders: `${sysconfdir}/<app>/` for the system file and
      `~/.config/<app>/` for the user's files (section 7);
    - the prefix of keyring entries.
  - **The core library contains no product name.** `Libraries/OAuth2`,
    namespace `oauth2`, takes the identity from its host.
  - **The name on the consent page** comes from the provider registration,
    not from the code: a fork registers its own applications under its own
    name.
  - Paths such as `~/.config/mulberry/` and `~/.mulberry/` in this plan mean
    "the identity's folder", whose value today is `mulberry`.
- **Sizes:** S = up to half a day, M = 1–2 days, L = 3–5 days.

## 4. Stages

### Stage 0: preparation (no code)

| # | Task | Check | Size |
|---|---|---|---|
| 0.1 | **Google:**<br>• a **Personal-mode** project (only the creator's account can sign in; no warning screen, no 100-user cap, no 7-day expiry; the choice is permanent);<br>• client of type **Desktop app**; its secret is shown only once;<br>• Gmail, CalDAV and CardDAV/People APIs enabled;<br>• scopes `https://mail.google.com/`, `…/auth/calendar`, `…/auth/carddav` declared.<br>An External project in Testing is created only for task 4.3 (`docs/oauth2/register-google.md`) | client ID and secret saved in the user provider file (section 7) | S |
| 0.2 | **Microsoft:**<br>• directory: **the work tenant** (decided 2026-11-25; the admin approves as was done for Thunderbird); an Azure free account only if the work tenant refuses;<br>• one registration for "any Entra ID tenant + personal Microsoft accounts", a public client without a secret;<br>• redirect `http://localhost`; `http://127.0.0.1` tried through the manifest;<br>• public client flows on;<br>• delegated permissions `IMAP.AccessAsUser.All`, `POP.AccessAsUser.All`, `SMTP.Send`, `offline_access`, `openid`, `email`, `profile` (`docs/oauth2/register-microsoft.md`) | application (client) ID saved in the user provider file; whether `127.0.0.1` was accepted recorded (decision D5) | S |
| 0.3 | **Accounts:**<br>• MP: IMAP and POP turned on in Outlook.com's settings;<br>• MW: IMAP, POP and authenticated SMTP enabled for the mailbox, and admin consent granted (portal button or admin-consent URL from the guide) unless the tenant allows user consent | answers recorded in section 8 | S (depends on the admin) |
| 0.4 | Yahoo: app created; mail access requested through Yahoo's form; app password generated for Y (`docs/oauth2/register-yahoo.md`) | request sent (answer may take long or never come); app password works with today's Mulberry (baseline) | S |
| 0.5 | Baseline: each server's advertised mechanisms recorded (`openssl s_client`, `CAPABILITY`, `EHLO`, `CAPA`) for Gmail, Outlook.com, Office 365, Yahoo; today's Mulberry tested with Y's app password (IMAP, SMTP, POP), "Save password" left unticked | the table "Baseline mechanisms" in section 8, used to check later stages | S |
| 0.6 | Decisions D1–D6 (section 6) taken | marked "Decided" | — |

### Stage 1: the core and its test tool (Mulberry unchanged)

All checks offline unless marked "live".

| # | Task | Check | Size |
|---|---|---|---|
| 1.1 | **Build skeleton:**<br>• `configure.in`: libcurl checked always, outside the HTML viewer block; `OAUTH2_CPPFLAGS`/`OAUTH2_LIBS` substituted; the application identity (name, configuration folder) as one `configure` setting, default `mulberry`;<br>• `Libraries/OAuth2`: own `Makefile.in`, static `libOAuth2.a` (namespace `oauth2`), built from `Libraries/Makefile`;<br>• `tools/oauth2-tool` with `--version`;<br>• `gmake check` runs an empty test;<br>• `scripts/build-freebsd.sh` checks for `curl/curl.h` also with `-n`;<br>• `.gitignore` entries | both configure variants build; Mulberry's binary and behaviour unchanged (it does not link the library yet) | M |
| 1.2 | **Vendored nlohmann/json** (`Libraries/OAuth2/thirdparty/nlohmann/json.hpp`, the single-header form, MIT, a pinned release, licence file kept). It is included only in the `.cpp` files that parse JSON, never in public headers, to limit compile time; its exceptions are caught inside the library. **Utilities:**<br>• base64url without padding;<br>• URL query and form encoding and decoding;<br>• random bytes (libsodium `randombytes_buf`);<br>• PKCE verifier and S256 challenge;<br>• `state`;<br>• redaction helper for logs | unit test `oauth2-unit` in `gmake check`: RFC 7636 appendix B vector; base64url round trips; form encoding of reserved characters | S–M |
| 1.3 | **Provider configuration (section 7):**<br>• a `Provider` struct: authorization, token, device and revocation endpoints; scopes per service (mail, calendar, contacts); redirect; PKCE; device-code support; SASL preference; client ID, secret and tenant;<br>• the system file `oauth2-providers.conf.json` in the repository (`Libraries/OAuth2/data/`), with entries `google`, `microsoft`, `yahoo`, `yandex`, `mailru`, `zoho` and no client IDs, installed by `gmake install`;<br>• loading: system file, then user file over it, then the account's own fields; ownership and permission checks of both files and folders (section 7); strict parsing and schema check; on error, a message with file, line and column;<br>• lookup by server hostname and by e-mail domain | unit tests:<br>• hostname lookup: `imap.gmail.com`, `outlook.office365.com`, `smtp-mail.outlook.com`, `imap.mail.yahoo.com`;<br>• merge order: a user `client_id` replaces nothing else;<br>• each error case of section 7 gives the expected message and position (syntax error, trailing comma, comment, duplicate key, unknown key, wrong type, missing field, non-HTTPS endpoint);<br>• permissions: a user file of mode 0644 or 0666, a foreign owner, a group-writable folder, a root-owned but group-writable system file each give the expected warning and never stop loading; the fix function brings 0644/0666 and the folder back to 0600/0700 and refuses the foreign owner;<br>• `gmake check` validates the shipped system file | M |
| 1.4 | **Token client over libcurl:**<br>• authorization-code exchange and refresh (form POST, optional client secret);<br>• parsing of `access_token`, `expires_in`, `refresh_token`, `scope`, `id_token`;<br>• errors: `error`, `error_description`, Google's `error_subtype`, Microsoft's `AADSTS` codes;<br>• refresh-token rotation: the new token replaces the old one;<br>• timeouts, TLS verification on, no cookies;<br>• log callback with redaction | mock server (`tests/token-server.py`, `tests/token-tests.sh`): success; rotation; `invalid_grant`; `invalid_client`; HTTP 500; malformed JSON; slow server (timeout); the log contains no token | M |
| 1.5 | **Loopback listener:**<br>• one-shot HTTP/1.1 server on 127.0.0.1, ephemeral port; also on ::1 when the redirect uses `localhost`, because a browser may resolve `localhost` to ::1;<br>• accepts only `GET <path>?code=…&state=…` or `?error=…`;<br>• checks `state`; ignores other requests such as `/favicon.ico`;<br>• answers with a short HTML page, closes, then stops;<br>• overall timeout (5 min) and cancellation from another thread | test script with curl: correct code; wrong `state` refused; error redirect; favicon ignored; timeout; listener not reachable from a non-loopback address | M |
| 1.6 | **Browser flow in `oauth2-tool login`:**<br>• builds the authorization URL (scopes, PKCE, `state`, `login_hint`, Google `access_type=offline` and `prompt=consent` on first sign-in);<br>• opens the browser: `xdg-open` started without a shell, then `$BROWSER`, else the URL is printed;<br>• waits for the listener, exchanges the code;<br>• prints the token type, expiry, granted scopes and the e-mail address from the ID token (the token itself only with `--show-token`);<br>• keeps the refresh token in a file (mode 0600) for the tool only;<br>• `oauth2-tool refresh` | **live:** G (BYO client), MP, MW sign in; `refresh` works; with G, unticking calendar on the consent screen shows up in the reported scopes; a mistyped client ID gives a readable error | M |
| 1.7 | **Device code in `oauth2-tool device`:** request, display of the code and URL, polling (`authorization_pending`, `slow_down`, `expired_token`, `access_denied`) | mock server: all poll answers; **live:** MP, MW | S–M |
| 1.8 | **SASL payloads and probes:**<br>• XOAUTH2 (`user=…^Aauth=Bearer …^A^A`) and OAUTHBEARER (RFC 7628 GS2 header with `a=`, `host=`, `port=`, `auth=`);<br>• decoder for the base64 JSON error challenge (`status`, `schemes`, `scope`, `openid-configuration`);<br>• `oauth2-tool imap-check` and `smtp-check`: a TLS session through libcurl (`CURLOPT_XOAUTH2_BEARER`, mechanism forced with `CURLOPT_LOGIN_OPTIONS`) that only authenticates and logs out. This checks the registrations and scopes independently of Mulberry's protocol code | unit tests: payloads match the RFC 7628 and Google examples byte for byte; error JSON parsed; **live:** IMAP and SMTP with G (XOAUTH2 and OAUTHBEARER), MP and MW (XOAUTH2); an expired or garbage token fails with a decoded error | M |

**End of stage 1:**
- Every account that can use OAuth2 has been proven, from the command line, to
  accept the registration, scopes and SASL formats.
- Mulberry itself is unchanged.

### Stage 2: secrets and preparation inside Mulberry (no visible OAuth2 yet)

| # | Task | Check | Size |
|---|---|---|---|
| 2.1 | **Redact `Authorization:` in the HTTP log.** The WebDAV request logger (`CWebDAVCalendarClient.cpp` near line 2018, and the CardDAV and WebDAV-prefs equivalents) writes full headers. This already leaks Basic credentials today | HTTP log on, a CalDAV account with a password: the header shows `<redacted>`; the rest of the log is unchanged | S |
| 2.2 | **Link `libOAuth2.a`** into Mulberry (`Linux/Makefile.in`, libcurl in the system libraries also without the HTML viewer). Nothing calls it yet | both configure variants build; binary starts; `ldd` shows libcurl in the `-n` build | S |
| 2.3 | **Secret store API:**<br>• `CPasswordManager` gets `GetSecret`/`SetSecret`/`RemoveSecret(key)` for passwords, refresh tokens and client secrets;<br>• **one selected store**, the system keyring or the application keyring (2.5), plus a session-only memory store when the user declines both;<br>• the application keyring implements the API with keys that cannot clash with account URLs (`oauth2:<provider>:<user>`), and rewrites its file only when a value changes | existing saved passwords still load; a secret written, Mulberry restarted, secret read back; the file stays mode 0600 | M |
| 2.4 | **System keyring through the Secret Service (required; moved here from stage 6 on 2026-11-25, because the keyring choice at start needs it):**<br>• a client of the freedesktop Secret Service D-Bus API over **libdbus-1** (927 KiB installed; no GLib), served by gnome-keyring, KWallet or KeePassXC;<br>• calls: open a session (`plain` first; the encrypted `dh-ietf1024-sha256-aes128-cbc-pkcs7` session with OpenSSL as a follow-up), search, create (replace), read, delete; unlock with the provider's prompt and its `Completed` signal; on a worker thread with timeouts;<br>• **availability check:** a session bus, and a provider answering on it;<br>• item attributes: `application` = the identity's name, `type` (`password`, `oauth2-refresh-token`, `oauth2-client-secret`), `account` or `provider` and `user`; label "<app>: <type> – <account or user>";<br>• `configure`: libdbus-1 through pkg-config, required on Linux and BSD (`--disable-secret-service` builds without it: the system keyring is then never offered) | **live on FreeBSD** with gnome-keyring, KeePassXC (Secret Service integration on) and KWallet:<br>• secrets stored and read back, visible with the right attributes in Seahorse or KeePassXC;<br>• locked collection: the provider's prompt appears, and Cancel is reported cleanly;<br>• no session bus or no provider: reported as "not available";<br>• no secret in any log | M–L |
| 2.5 | **Choosing the keyring at start, and the passphrase policy.** The application cannot know whether this is a first run, a reinstall, or a new system with an old home folder, so **the store is found from what exists, checked at every start**, before the preferences are loaded (they may be on a server):<br>• **the <app> keyring file exists** (`.keyring`, today `~/.mulberry/.keyring`): it is used; only its passphrase is asked, as today (`CMulberryApp.cpp:689-698`). No question. A wrong passphrase is asked again. The dialog's buttons become OK and **Quit** (decided 2026-11-25): today Cancel continues without the keyring, which leaves every account without its password; the dialog mentions `--safe-mode` (2.8);<br>• **otherwise, the system keyring record exists:** a small file `~/.config/<app>/secret-store.conf.json` with `{"store": "system"}` and no secret, mode 0600, written when the user chooses the system keyring. It is kept outside the system keyring so that an unavailable keyring can be told apart from "nothing chosen yet":<br>&nbsp;&nbsp;– system keyring available: it is used, no question; the provider may show its own unlock prompt;<br>&nbsp;&nbsp;– **system keyring not available** (no session bus, KeePassXC not running, provider not answering): an alert "<app>'s passwords are stored in the system keyring, which is not available. Start it and choose Retry. To read local copies of mailboxes without any keyring, start <app> with `--safe-mode`." Buttons **Retry** and **Quit** only (decided 2026-11-25): the application never continues without its secrets and never switches to the other store;<br>• **otherwise, the system keyring is available and holds this application's marker item** (`application` = <app>, `type` = `store-marker`, also written when the user chooses the system keyring): the record file was lost (new home folder, `~/.config` cleaned); the system keyring is used and the record file is written again. No question;<br>• **otherwise** (nothing found): the question. If a system keyring is available: "Store passwords and tokens in: the system keyring (gnome-keyring, KWallet, KeePassXC…) / an <app> keyring protected by a passphrase". If not, the <app> keyring is offered directly. Cancel: nothing is stored yet, so the application continues with no keyring in use (decided 2026-11-25);<br>• **no keyring in use** (the question was cancelled): each time a secret is to be saved (an account's "Save password" ticked, or an OAuth2 sign-in that returns a refresh token, task 3.4), the question is asked again. Cancel again: an alert "No keyring chosen: this password (or token) is kept only until <app> quits, and will be asked again next time", then the application continues with the secret in memory. The question also comes again at the next start;<br>• the choice is never stored in the preferences: the `.keyring` file, or the record file and the marker item, are the record;<br>• **both exist** (a move interrupted, or an old file left behind): the `.keyring` file wins, and a warning names the other store;<br>• **changing later:** a command in the preferences moves all secrets to the other store, then deletes the old one (the `.keyring` file, or the items, the marker and the record file);<br>• **passphrase policy** for a new <app> keyring or a passphrase change: at least 8 characters, with at least one lowercase letter, one uppercase letter and one digit; other characters allowed. The message names the rule that is missing; the passphrase is typed twice;<br>• **existing keyring with a weak passphrase** (decided 2026-11-25): after a successful unlock, an alert "The keyring passphrase does not meet the current policy" with two buttons: **Continue** (the keyring is used as it is) and **Change Passphrase** (new passphrase satisfying the policy, typed twice; the file is re-encrypted, new file then rename). Never forced; the alert comes again at each start while the passphrase is weak | **live:**<br>• fresh home folder with gnome-keyring running: the question appears; system chosen: secrets stored, the marker visible in Seahorse; restart: no question;<br>• same with the <app> keyring: passphrase asked twice; restart: only the passphrase is asked;<br>• an existing `.keyring` (today's Mulberry): only the passphrase is asked, as before;<br>• new preferences file with an old `.keyring` (reinstall): no question, passphrase only;<br>• passphrase dialog, Quit: the application exits, nothing written;<br>• fresh home folder, Cancel at the question: the application starts; tick "Save password" on an account: the question again; Cancel: the alert, the password works until quitting and is not on disk; restart: the question at start;<br>• KeePassXC closed after choosing it: the alert with Retry and Quit; start KeePassXC, Retry: secrets found; Quit: the application exits, nothing written;<br>• record file deleted, gnome-keyring running: no question, the record file is back;<br>• no session bus: the <app> keyring offered directly;<br>• policy: `password`, `abcdefgh`, `ABCD1234`, `Abc123` refused with the right message, `sDf5az3r` accepted;<br>• a `.keyring` made by today's Mulberry with the passphrase `abc`: after unlock the alert appears; Continue: secrets read; Change Passphrase: restart asks the new one, secrets intact;<br>• move from one store to the other and back: all secrets kept, the old store removed | M–L |

**Passwords out of the preferences file (required).** Today's state, from the
code:

- **The only place that writes passwords** to `preferences.mbp` is
  `CAuthenticatorUserPswd::GetInfo()` (`Sources_Common/Application/Preferences/CAuthenticator.cpp:310-313`).
  It does so when the account's "Save password" box is ticked, scrambled
  with the fixed key "Modified-UTF7".
- Every account type uses it: IMAP, POP3, SMTP, ManageSieve, LDAP, CalDAV,
  CardDAV, the remote-preferences account (WebDAV, IMSP, ACAP) and the
  password-based authentication plugins.
- When preferences are stored on a server, the scrambled passwords travel
  there too.
- **Places that only read:**
  - the current authenticator format (`CAuthenticator.cpp:338`);
  - three old formats: address accounts (`CAddressAccount.cpp:304`), the
    global `Password` key of 1.x files (`CPreferencesRW.cpp:818`) and the
    deprecated remote-preferences `Password` key (`CPreferencesRW.cpp:976`).
- **Not affected:** the S/MIME/PGP passphrase cache
  (`CSecurityPlugin.cpp:2713`) and the multi-user login password
  (`CAdminLock::mInitialPswd`) are kept in memory only.
- **The keyring path:** after each successful login `CINETProtocol::Logon()`
  adds the password to the keyring when one is active
  (`CINETProtocol.cpp:451`), whether or not the box is ticked.

| # | Task | Check | Size |
|---|---|---|---|
| 2.6 | **Never write passwords to the preferences:**<br>• `CAuthenticatorUserPswd::GetInfo()` always writes an empty password field, keeping the field so that older versions still read the file (they will ask for the password);<br>• the "Save password" box now means "remember the password in the chosen keyring": its label changes (one text change in each authentication panel); the administrator lock `mLockSavePswd` still forbids it;<br>• the keyring receives a password only when the box is ticked, and loses it when the box is unticked (today it receives every password used);<br>• no keyring in use (the question of 2.5 was cancelled): a ticked box asks the question of 2.5 again; Cancel gives the alert of 2.5 and keeps the password in memory for the session; Cancel keeps the password for the session | accounts of every type with "Save password" ticked, preferences saved: `grep` finds none of the passwords in `preferences.mbp`, nor in the copy stored on a remote preferences server; after a restart the passwords come from the keyring; with the box unticked, the password is asked again and is not in the keyring | M |
| 2.7 | **One-time migration of saved passwords.** On loading, passwords found in the preferences (the current format and the three old ones) are counted. If there are any, one dialog explains: "<app> no longer stores passwords in the preferences file. Move N saved passwords to a keyring?"<br>• **Yes:** the same choice as at start (2.5): the system keyring if available, or the <app> keyring with a passphrase satisfying the policy (or unlocked, if one exists); the passwords are moved, and the preferences are rewritten at once without them (new file then rename);<br>• **No:** the passwords are kept in memory for this session only. The preferences are still rewritten without them, and they are asked for again later;<br>• remote preferences are rewritten on their next save;<br>• the documentation advises changing passwords whose old preference files may have been copied (backups, exported files) | a preferences file from today's Mulberry with saved passwords for IMAP, SMTP, CalDAV and the remote account: after Yes, the file has no password and the accounts connect without asking; after No, the accounts ask, and the file has no password; an old 1.x file with the global `Password` key is migrated the same way | M |
| 2.8 | **Safe mode: `--safe-mode`** (decided 2026-11-25), for starting without any keyring, for example when the system keyring is broken:<br>• the keyrings are completely ignored: neither the `.keyring` file nor the system keyring is opened, read or written; the checks and questions of 2.5 and the migration of 2.7 are skipped;<br>• the application starts disconnected, as with the existing `-d`/`--disconnect` (`CMulberryApp.cpp:296`), and connecting is disabled for the session; no password is ever asked;<br>• accounts with local copies (disconnected mode) can be **read only**: browse, open, search, print, save a message to a file. Every change is refused: flags, delete, expunge, move or copy into a mailbox, saving drafts, sending or queueing, editing address books and calendars. The existing read-only state of mailboxes and address books (`CMbox` and `CAddressBook` flag `eReadOnly`) is forced on, so the existing UI checks apply;<br>• the preferences are not saved at quit (decided 2026-11-25), so a safe-mode session changes nothing on disk;<br>• "Safe mode" shown in the window titles and the status window;<br>• given to an instance that is already running (the MDI server forwards options), it is ignored with a message;<br>• the option name follows the existing long option `--disconnect` (GNU style with a hyphen) | **live:** with a disconnected copy of an IMAP account (MW) and KeePassXC closed:<br>• normal start: the 2.5 alert (Retry, Quit);<br>• `--safe-mode`: no keyring prompt, no password prompt; the local copy of INBOX opens and messages can be read and searched; delete, flag, move and send are refused or disabled;<br>• a debug log shows that no keyring call was made;<br>• `preferences.mbp` and `.keyring` unchanged after quitting (compare checksums) | M |
| 2.9 | **Audit test:** a debug-build check, run when the preferences are saved, that no value read from the secret store or typed in a password dialog during the session appears in the text written. A failure is logged as an error | the check fires when the `GetInfo()` change is reverted on purpose, and stays silent otherwise | S |

### Stage 3: the OAuth2 authenticator and sign-in (no protocol uses it yet)

| # | Task | Check | Size |
|---|---|---|---|
| 3.1 | **Model:**<br>• `eOAuth2` in `CAuthenticator`, and `CAuthenticatorOAuth2` derived from `CAuthenticatorUserPswd` (existing casts stay safe);<br>• fields: user/e-mail, provider ID (or "automatic"), client ID, client secret, tenant;<br>• copy, assign, compare;<br>• `GetInfo`/`SetInfo` write only the non-secret fields (user, provider, client ID, tenant), with `CFutureItems` kept; the client secret, when the account has its own, goes to the secret store (2.3) like a password;<br>• `RequiresUserPswd()` false | preferences round trip (save, restart, same values); old preference files unchanged; a file saved by the new version opened by the previous Mulberry (test binary from `master`): the account falls back without a crash (documented behaviour) | M |
| 3.2 | **Preferences GUI:**<br>• "OAuth2" in the Method popup (`CPrefsAccountAuth::BuildAuthPopup`);<br>• a small panel: user (e-mail), provider popup (Automatic, Google, Microsoft, Yahoo, Yandex, Mail.ru, Zoho), client ID, client secret, tenant (Microsoft only), a read-only line showing which provider "Automatic" chose;<br>• IMAP, SMTP, POP3, Sieve, CalDAV and CardDAV accounts all show it | the item appears for those account types; values saved; connecting with OAuth2 gives the clean "not supported yet" error (rule of section 3) | M |
| 3.3 | **Token manager** (`Sources_Common/OAuth2/COAuth2Manager`), the glue between accounts and the core:<br>• one entry per (provider, user), shared by every account and connection of that user. The same Google user's IMAP, SMTP, CalDAV and CardDAV use one refresh token, which matters for Google's 100-tokens-per-user limit;<br>• a mutex, so one refresh runs at a time;<br>• access token in memory with its expiry, refreshed 2 min before it;<br>• refresh token read from and written to the secret store (2.3), replaced on rotation;<br>• `GetAccessToken(account, scopes, interactive)` | unit-like test through a debug menu or command-line switch: two threads asking at once cause one refresh | M |
| 3.4 | **Sign-in dialog** (a `CTaskSync` like `CUserPswdTask`, run on the UI thread):<br>• "Signing in to <provider> as <user> in your browser…";<br>• buttons Copy URL, Open Browser Again, Cancel;<br>• the browser opened through the existing URL launcher (`CURLHelpersMap::LaunchURL`), checked for shell quoting of `&` in URLs, or `posix_spawnp` directly;<br>• **"Sign In Now" button in the panel** runs the flow without connecting to any server;<br>• no keyring in use: before the refresh token is stored, the question of 2.5; Cancel again gives its alert and keeps the token in memory for the session | **live:** G, MP, MW sign in from the preferences; restart Mulberry, "Sign In Now" again: no browser (refresh from the keyring); Cancel and timeout leave the account unchanged; a wrong client ID shows the provider's error | M–L |
| 3.5 | **Account check after sign-in:** the e-mail address in the ID token (or Microsoft's `preferred_username`) is compared with the account's user. A different account chosen in the browser is reported ("You signed in as …; this account is …"), not silently used | **live:** G signs in while another Google account is selected in the browser: warning shown | S |

### Stage 4: mail protocols

| # | Task | Check | Size |
|---|---|---|---|
| 4.1 | **IMAP, XOAUTH2:**<br>• capability flags `AUTH=XOAUTH2`, `AUTH=OAUTHBEARER` in `CIMAPClient` (today only PLAIN, LOGIN, ANONYMOUS and SASL-IR are recorded);<br>• `DoOAuth2Authentication` in `CINETClient`, modelled on `DoExternalAuthentication`;<br>• initial response when `SASL-IR` is advertised, else continuation;<br>• **error challenge:** a `+ <base64 JSON>` after the payload is answered with an empty line and treated as a failure. Today `INETProcess` treats any `+` as progress;<br>• `CMailControl::PromptUser`: for OAuth2, get the token before SASL; on failure refresh once, retry, then sign in interactively, then give up | **live:** G, MP, MW: log in, list folders, open INBOX, read a message; with a deliberately broken token (debug switch) the failure path refreshes and succeeds; IMAP log with "Show Authentication Details" contains no token | M–L |
| 4.2 | **IMAP, OAUTHBEARER:**<br>• used when the server advertises it (Gmail does; Microsoft does not);<br>• its failure answer is `%x01`;<br>• an advanced/debug setting forces XOAUTH2 for comparison | **live:** G with both mechanisms; MP and MW pick XOAUTH2 | S |
| 4.3 | **IMAP over time:**<br>• token expiry during a long session, with IDLE and reconnects (`INETRecoverReconnect` → `MboxServerReconnect` must ask the manager, not reuse the old token);<br>• several cloned connections at once (one refresh);<br>• a revoked grant (Google account → Security → third-party access; Microsoft → My Apps / account privacy) leads to the interactive sign-in;<br>• debug switch `MULBERRY_OAUTH2_LIFETIME=60` shortens the believed token lifetime | **live:** G and MW kept open for 2 h with IDLE; 10 mailboxes opened in parallel; revoke and recover on G and MP; a second Google project, External in Testing mode (`docs/oauth2/register-google.md`), its client ID and secret in the test account's own fields (3.1, 3.2) rather than in the provider file, gives a natural `invalid_grant` test after 7 days | M |
| 4.4 | **SMTP:**<br>• the mechanism is chosen from the advertised list (`mAUTHTypes`) instead of the fixed check;<br>• the payload is written straight to the stream: the 1,024-byte `mLineData` buffer is too small for a Google token;<br>• Google's `334 <base64 JSON>` challenge answered with an empty line;<br>• on Microsoft's `535 5.7.3` and `5.7.139` (SMTP AUTH disabled): one refresh and retry, then a specific message | **live:** send a message through G, MP, MW; MW with SMTP AUTH disabled (if the admin can show it): the message says that authenticated SMTP is disabled for the mailbox | M |
| 4.5 | **POP3:** parse the `SASL` line of `CAPA` (today unparsed); `AUTH XOAUTH2` with the payload as a continuation (RFC 5034 limits an initial response to 255 bytes) | **live:** G (POP enabled in Gmail), MP and MW (POP enabled); download one message | S–M |
| 4.6 | **ManageSieve:** `AUTHENTICATE "XOAUTH2"`/`"OAUTHBEARER"` with the string literal form in `CSIEVEClient` | no test account offers Sieve with OAuth2. The task is done last in the stage, checked by a mock exchange in the unit tests, and marked "untested on a real server" | S |

**End of stage 4:** Gmail, Outlook.com and Microsoft 365 mail work with OAuth2
in Mulberry.

### Stage 5: Google calendar and contacts

| # | Task | Check | Size |
|---|---|---|---|
| 5.1 | **Bearer for HTTP:**<br>• `CHTTPAuthorizationBearer` beside Basic and Digest;<br>• an `eOAuth2` case in the three `GetAuthorization()` functions (CalDAV, CardDAV, WebDAV prefs);<br>• on 401 refresh once and retry | **live:** G CalDAV at `https://apidata.googleusercontent.com/caldav/v2/`: list calendars, read events, create, change and delete a test event | M |
| 5.2 | **CardDAV for Google** (discovery at `https://www.googleapis.com/.well-known/carddav`) | **live:** G address book: read, create, change, delete a test contact | S–M |
| 5.3 | **Scopes:**<br>• the Google provider asks for mail, calendar and contacts in one consent, so that one refresh token serves every account of the user;<br>• the granted `scope` list is checked: if the user unticked calendar or contacts, the DAV account reports "access to Google Calendar was not granted" and offers a new sign-in;<br>• Microsoft accounts ask only for mail scopes (Graph is a later plan) | **live:** G sign-in with calendar unticked: mail works, calendar shows the message; sign in again with everything ticked: both work | S–M |

### Stage 6: fallbacks and error messages

| # | Task | Check | Size |
|---|---|---|---|
| 6.1 | **Device code in Mulberry:** in the sign-in dialog, "Sign in on another device" for providers that support it (Microsoft; Yandex and Zoho in the table): code and URL shown, Copy, polling in the background | **live:** MP and MW signed in from a phone | M |
| 6.2 | **Paste the final URL:** for remote X sessions, where the browser cannot reach Mulberry's loopback. The dialog accepts the URL the browser ends on (the "unable to connect" page) and extracts `code` and `state` | **live:** G signed in with the listener disabled (debug switch), pasting the URL | S |
| 6.3 | **External token command** (as mutt and aerc do): per account, a command whose first output line is the access token. Mulberry never refreshes it itself. Run without a shell, with a timeout | a script that prints a token from `oauth2-tool`; **live:** G through the command | S |
| 6.4 | **Error messages:**<br>• Google: `admin_policy_enforced`, `org_internal`, `invalid_rapt`, `access_denied`, the unverified-app cap;<br>• Microsoft: AADSTS65001 (consent), AADSTS90094 and AADSTS90095 (admin approval, with the admin-consent URL to copy), AADSTS50020, AADSTS700016 (wrong client ID or tenant), AADSTS50173 (password changed);<br>• Yahoo: `invalid_client` and unapproved scope;<br>• all point to the matching section of the registration guides | **live where reproducible:** a wrong client ID (all providers); MW without consent (if the tenant requires it); others by mock-server tests | M |
| 6.5 | **Sign out:** "Sign Out" in the panel removes the refresh token from the keyring and, for Google, calls the revocation endpoint | **live:** G and MP: after sign-out the next connection asks to sign in; Google no longer lists the grant | S |

### Stage 7: Yahoo (depends on Yahoo's answer)

| # | Task | Check | Size |
|---|---|---|---|
| 7.1 | **If Yahoo grants mail access:**<br>• the Yahoo provider entry is completed: endpoints; the app is registered as a public client with PKCE (guide), so no secret is sent; only if Yahoo issued a client secret anyway, it goes by HTTP Basic; redirect form as registered (Yahoo may require `https://`, see the guide);<br>• IMAP and SMTP with XOAUTH2 and OAUTHBEARER | **live:** Y IMAP, SMTP, POP | S–M |
| 7.2 | **If not:**<br>• Yahoo stays on app passwords;<br>• the Yahoo entry is marked "requires approved client";<br>• choosing OAuth2 for a Yahoo account without a client ID says so and points to the app-password section of the guide | **live:** Y with an app password still works (regression); OAuth2 selected without a client ID shows the message | S |

### Stage 8: other providers and discovery (optional, no test accounts here)

| # | Task | Check | Size |
|---|---|---|---|
| 8.1 | Yandex, Mail.ru and Zoho entries reviewed against current documentation; data-file override documented, so a user can test with their own client | unit tests only; marked "untested" | S |
| 8.2 | **Discovery and dynamic registration:**<br>• RFC 8414 metadata, and RFC 7628's `openid-configuration` in a failed OAUTHBEARER answer;<br>• RFC 7591 registration without a secret (the IETF `oauth-public` profile; Fastmail today);<br>• the registered client stored per server | mock server; live only if a Fastmail (trial) account becomes available | M–L |

### Stage 9: documentation and packaging

| # | Task | Check | Size |
|---|---|---|---|
| 9.1 | User documentation: choosing OAuth2, bring-your-own Google client (from `docs/oauth2/register-google.md`, rewritten for users), Microsoft work accounts and admin consent, Yahoo app passwords, the keyring passphrase, sign out | read through by the developer | S–M |
| 9.2 | **Packaging:**<br>• libcurl as a run-time dependency in every build (FreeBSD port, `build-freebsd.sh`, Debian files);<br>• xdg-utils as a recommended package;<br>• nlohmann/json's licence installed with the others;<br>• the system provider file installed into `${sysconfdir}/<app>/`. FreeBSD ports usually install configuration files as `.sample` (`@sample`), so local edits survive upgrades. The consequence: endpoint updates then do not reach an edited copy, so the documentation tells administrators to leave the system file unedited and to override in user files (accepted 2026-11-25; checked again when the port is written) | fresh-clone builds on FreeBSD and Linux; the installed file loads | S |
| 9.3 | Merge into `master` after a full pass of the live tests of stages 1–7 on all four accounts | test table filled in | S |

## 5. Effort

| Stage | Tasks | Estimate |
|---|---|---|
| 0 Preparation | registrations, account settings, baseline | 1–2 days, plus waiting for the Microsoft admin and Yahoo |
| 1 Core and test tool | 8 | 9–14 days |
| 2 Preparation in Mulberry: secret store, system keyring, keyring choice, passwords out of the preferences, safe mode | 9 | 12–19 days |
| 3 Authenticator and sign-in | 5 | 6–9 days |
| 4 Mail protocols | 6 | 7–11 days |
| 5 Google calendar and contacts | 3 | 3–5 days |
| 6 Fallbacks and messages | 5 | 4–6 days |
| 7 Yahoo | 1 of 2 | 0.5–2 days |
| 8 Other providers, discovery | 2 (optional) | 3–6 days |
| 9 Documentation, packaging | 3 | 2–3 days |
| **Total without stage 8** | | **about 45–70 days (9–14 weeks)** |

This is slightly above the report's 6–8 weeks for three reasons: it now
includes the test tool, the mock-server tests, the keyring creation path and
the live-test time per task.

## 6. Decisions to take before the stage concerned

| # | Decision | Options | Proposal |
|---|---|---|---|
| D1 | JSON library (1.2) | jsmn (471 lines, tokens only), cJSON (3.5k lines), nlohmann/json (single header, about 25k lines, C++ API, exceptions) | **Decided 2026-11-25: nlohmann/json**, vendored, for OAuth2 and later for Graph, so one JSON library everywhere |
| D2 | Random and hash functions (1.2) | libsodium (already required by the keyring) or OpenSSL | **Decided 2026-11-25: libsodium** for randomness; SHA-256 from either |
| D3 | Provider settings (1.3) | built-in table; user file; system file; formats INI or JSON | **Decided 2026-11-25:** JSON files.<br>• System file `/usr/local/etc/<app>/oauth2-providers.conf.json`, installed with the application.<br>• User file `~/.config/<app>/oauth2-providers.conf.json` overrides it.<br>• Strict parsing by nlohmann/json; on a parse error the application reports it and exits.<br>• New configuration files go to `~/.config/mulberry/`; `~/.mulberry/` is moved there one day, outside this plan.<br>Details and proposals in section 7 |
| D4 | Keeping secrets (2.3–2.5) | (a) refuse without a keyring; (b) memory only; (c) the application keyring, created when needed; (d) a plain 0600 file; (e) the preferences file: rejected; (f) desktop keyring libraries (libsecret: GLib 106 MiB; KWallet, QtKeychain: Qt): rejected; (g) the system keyring through the Secret Service protocol spoken directly over libdbus-1 | **Decided 2026-11-25:**<br>• the store is found from what exists (the `.keyring` file, or the record file `secret-store.conf.json` and the marker item in the system keyring), never from a "first run" guess; when nothing is found and a system keyring is available, the user chooses **(g) the system keyring or (c) the application keyring**; otherwise (c) is offered directly;<br>• the application keyring's passphrase must satisfy the policy of task 2.5 (8+ characters, lower and upper case, a digit); an existing keyring with a weaker passphrase gives an alert with Continue or Change Passphrase, never a forced change;<br>• (b) only when the user cancels the question, while nothing is stored yet: the question comes again each time a password or token is to be saved, and a second Cancel shows an alert, then keeps that secret in memory for the session;<br>• a chosen keyring that cannot be opened (system keyring not available, passphrase dialog closed) gives Retry or Quit: no continuing without secrets, no silent switch to the other store;<br>• `--safe-mode` (2.8) ignores every keyring and allows reading local copies only;<br>• the same choice is offered by the migration of passwords saved in the preferences (2.7);<br>• macOS (Keychain, `SecItem` API in Security.framework; Mulberry already has a Keychain password manager on the older `SecKeychain` API) and Windows (Credential Manager, whose secrets hold at most 2,560 bytes, or a DPAPI-encrypted file) need no extra library either; they are notes for the FLTK cross-platform port, not tasks of this plan |
| D5 | Microsoft redirect (1.5, 0.2) | `http://localhost` (registered in the portal; the port is ignored), `http://127.0.0.1` (manifest only; whether the port is ignored is not documented) | **Decided 2026-11-25: `http://localhost`**, the listener on 127.0.0.1 and ::1 |
| D6 | Automatic choice of OAuth2 for known servers (3.2) | never; propose it when creating an account for gmail.com, outlook.com, office365.com and similar; switch existing accounts | **Decided 2026-11-25: propose for new accounts only**; existing accounts are never switched |
| D7 | `oauth2-tool` installed or test-only | install as a helper (also usable as a token command, 6.3); keep it in the build tree | **Decided 2026-11-25: build tree only for now**, to be reviewed at stage 9 |
| D8 | Microsoft client ID for releases (after this plan) | the developer's test registration; a project registration (publisher verification); bring your own as for Google | **Decided 2026-11-25:** chosen before 9.3; the code supports all three. Work tenants with the default consent policy need admin consent for any client except six listed mail apps (Thunderbird among them), whichever option is chosen |
| D9 | Google client for releases (after this plan) | bring your own (Personal mode makes it simple for one user); a project client with yearly verification (and CASA if Google asks) | **Decided 2026-11-25: bring your own for now**; the code supports both |

## 7. Provider configuration files (decision D3)

*Decided 2026-11-25 by the developer: the JSON format, the two locations and
their priority, strict parsing, and exit on error. The details below were
proposed by the plan and accepted the same day.*

### Locations and priority

| Order | File | Written by |
|---|---|---|
| 1 (lowest) | `${sysconfdir}/<app>/oauth2-providers.conf.json`: `/usr/local/etc/mulberry/oauth2-providers.conf.json` with the default prefix on FreeBSD; `/etc/mulberry/…` for Linux packages built with `--sysconfdir=/etc` | Installed with the application, from `Libraries/OAuth2/data/` |
| 2 | `~/.config/<app>/oauth2-providers.conf.json`, that is `~/.config/mulberry/…` today; `$XDG_CONFIG_HOME/<app>/` when that variable is set, as the XDG specification says | The user, by hand |
| 3 (highest) | The account's own fields in the preferences (client ID, client secret, tenant) | The OAuth2 panel (task 3.2) |

`<app>` is the application identity's name (rule of section 3), so a fork
reads its own folders. The existing `~/.mulberry/` folder is left as it is.
Moving the rest of the configuration to `~/.config/<app>/` is a later task,
outside this plan.

### Format

```json
{
  "version": 1,
  "providers": {
    "google": {
      "name": "Google",
      "hosts": ["imap.gmail.com", "smtp.gmail.com", "pop.gmail.com",
                "apidata.googleusercontent.com", "www.googleapis.com"],
      "domains": ["gmail.com", "googlemail.com"],
      "authorization_endpoint": "https://accounts.google.com/o/oauth2/v2/auth",
      "token_endpoint": "https://oauth2.googleapis.com/token",
      "revocation_endpoint": "https://oauth2.googleapis.com/revoke",
      "scopes": {
        "mail": ["https://mail.google.com/"],
        "calendar": ["https://www.googleapis.com/auth/calendar"],
        "contacts": ["https://www.googleapis.com/auth/carddav"]
      },
      "redirect": "http://127.0.0.1",
      "pkce": true,
      "sasl": ["OAUTHBEARER", "XOAUTH2"],
      "client_id": "",
      "client_secret": ""
    }
  }
}
```

A user file for the tests then holds only what differs:

```json
{
  "version": 1,
  "providers": {
    "google":    { "client_id": "….apps.googleusercontent.com", "client_secret": "…" },
    "microsoft": { "client_id": "00000000-0000-0000-0000-000000000000", "tenant": "common" }
  }
}
```

The exact key list is fixed in task 1.3 and documented in
`Libraries/OAuth2/README.md`.

### Merging

- Providers are matched by their identifier (`google`, `microsoft`…).
- Within a provider, each key of the user file replaces the system file's
  value. Objects such as `scopes` merge key by key; arrays such as `hosts`
  are replaced whole.
- A provider found only in the user file is a new provider. After merging it
  must contain every required key.
- `"enabled": false` hides a provider of the system file.

### Strict parsing and checks

- **Syntax:** RFC 8259 JSON as parsed by nlohmann/json, with comments not
  allowed (`ignore_comments` false). Trailing commas, single quotes and
  unquoted keys are errors, as the library already treats them.
- **Duplicate keys:** nlohmann/json silently keeps the last of two identical
  keys in an object. A parser callback detects them and reports an error,
  because a duplicate in a hand-edited file is almost always a mistake.
- **Schema, checked by our code after parsing:**
  - `version` must be 1;
  - unknown keys are errors, so a typo such as `clientid` is not silently
    ignored;
  - every value must have the right type;
  - provider identifiers use `[a-z0-9-]`;
  - endpoints must be `https://` URLs;
  - the redirect must be an `http://` loopback address;
  - after merging, every enabled provider has its required keys.
- **Comments:** JSON has none, so a key `"_comment"` holding a
  string is accepted and ignored at any level.

### File permissions (decided 2026-11-25: warnings only)

The user file may hold client secrets. Whoever can change either file can
redirect sign-ins and tokens to another server, because the endpoints are in
it. Before parsing, both files and their folders are checked, as `ssh` checks
its files. **Every finding is a warning; the application never exits because
of permissions.** For the user's own files the warning offers to fix them:

| File | Warned when | Fix offered |
|---|---|---|
| User file `~/.config/<app>/oauth2-providers.conf.json` | readable or writable by group or others (mode not `0600`/`0400`); not owned by the user; the folder `~/.config/<app>/` writable by group or others; a symbolic link pointing outside the user's home | file mode set to `0600`, folder `go-w` removed (`chmod 700` when it holds nothing else). Not possible for a file owned by another user or a symbolic link: the warning then says what to do by hand |
| System file `${sysconfdir}/<app>/oauth2-providers.conf.json` | not owned by root; writable by group or others; its folder writable by group or others (world-readable is normal: no user secrets) | none (needs root); the warning gives the commands for the administrator |

- **In Mulberry:** one dialog at startup listing every finding, with the
  buttons **Fix Permissions** (only when something is fixable) and
  **Continue**. After a fix the files are checked again; anything left is
  shown once more, then the application continues.
- **In `oauth2-tool`:** the warnings on standard error, with the `chmod`
  commands; `oauth2-tool --fix-permissions` applies the fixes.
- **Same check and fix** for the other files holding secrets or the keyring
  choice, required to be `0600`: Mulberry's `.keyring`,
  `secret-store.conf.json` (task 2.5) and the token files of `oauth2-tool`.
- Files created by the application are always created with mode `0600`
  (folders `0700`), so the warning only appears for files edited or copied
  by hand.

### Errors

- **When:** both files are read **once, at startup**, before any window or
  account is opened.
- **The message** names the file, the line and column (computed from the
  library's byte offset) and the problem, for example:
  `~/.config/mulberry/oauth2-providers.conf.json:14:7: unknown key "clientid" in provider "google"`.
- **What happens:**
  - Mulberry shows the message in an error dialog, or on standard error when
    no display is available, then **exits** with a non-zero status;
  - `oauth2-tool` prints it on standard error and exits with status 2.
- **Consequence, intended:**
  - a mistake in the user file prevents the start until the file is fixed or
    removed;
  - a broken system file blocks every user, which is why `gmake check`
    validates the shipped file.
- **Missing files:**
  - A missing user file is normal.
  - A missing system file is not a parse error: the application starts, and an
    OAuth2 account reports "OAuth2 provider configuration not found (expected
    at …)" when used.
- **Running from the build tree:** the program is not installed
  there, so the system file can be given explicitly:
  - `oauth2-tool --system-config <file>`;
  - for Mulberry, an environment variable whose name is derived from the
    application identity, `MULBERRY_OAUTH2_SYSTEM_CONFIG` today.

## 8. Live test record

Filled in during the work. No client ID, secret, password or token is
written here: those stay in the user provider file and the keyring.

### Registrations and accounts

Filled in at stage 0 (tasks 0.1–0.4), following the guides in `docs/oauth2/`.

| Item | Value | Date |
|---|---|---|
| G: Google project name | | |
| G: audience (Personal mode or External) | | |
| G: APIs enabled; billing asked for (yes/no) | | |
| G: `…/auth/carddav` scope accepted, or `…/auth/contacts` used | | |
| G: second project for task 4.3 (External, Testing), if created | | |
| Microsoft: directory where the app is registered | | |
| Microsoft: manifest accepted `http://127.0.0.1` (yes/no) | | |
| MP: IMAP and POP turned on in Outlook.com | | |
| MW: consent state (user consent, admin consent granted, request pending) | | |
| MW: mailbox settings (IMAP, POP, Authenticated SMTP) | | |
| MW: SMTP AUTH blocked tenant-wide or by security defaults (yes/no) | | |
| Y: app password created | | |
| Y: Yahoo app created; redirect form accepted | | |
| Y: mail access request sent; Yahoo's answer | | |

### Baseline mechanisms

Filled in at task 0.5: what each server advertises before any OAuth2 code
(`openssl s_client`, then `CAPABILITY`, `EHLO`, `CAPA`).

| Server | IMAP `AUTH=` | SMTP `AUTH` | POP3 `SASL` | Date |
|---|---|---|---|---|
| Gmail | | | | |
| Outlook.com | | | | |
| Office 365 | | | | |
| Yahoo | | | | |

### Test results

✓ passed, ✗ failed with a note, — not applicable.

| Task | G | MP | MW | Y | Date | Notes |
|---|---|---|---|---|---|---|
| 0.5 baseline | | | | | | |
| 1.6 login | | | | | | |
| 1.7 device | — | | | — | | |
| 1.8 imap/smtp-check | | | | | | |
| 3.4 sign-in in Mulberry | | | | | | |
| 4.1 IMAP | | | | | | |
| 4.3 IMAP over time | | | | | | |
| 4.4 SMTP | | | | | | |
| 4.5 POP3 | | | | | | |
| 5.1–5.3 CalDAV/CardDAV | | — | — | — | | |
| 6.1 device in Mulberry | — | | | — | | |
| 6.2 paste URL | | | | | | |
| 6.5 sign out | | | | | | |
| 2.4–2.5 keyring choice | | | | | | gnome-keyring / KeePassXC / KWallet / none |
| 2.8 safe mode | | | | | | disconnected copy, keyring off |
| 7.x Yahoo | — | — | — | | | |
