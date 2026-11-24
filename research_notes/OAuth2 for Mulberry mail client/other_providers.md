# OAuth2 / modern auth for IMAP/POP/SMTP: providers other than Google and Microsoft (state as of 2026-11-24)

Notes on method: besides web sources, I ran a few direct checks on 2026-11-24 from a FreeBSD host. They were anonymous, with no account data:
- (a) IMAP `CAPABILITY` on port 993 and SMTP `EHLO` on port 465 (587 STARTTLS for iCloud) through `openssl s_client`, to read the advertised SASL mechanisms.
- (b) An `AUTHENTICATE OAUTHBEARER` with a deliberately invalid token, to capture the RFC 7628 error JSON.
- (c) Fetches of the providers' `.well-known` OAuth/OIDC metadata.
- (d) A download of Thunderbird's `OAuth2Providers.sys.mjs` (comm-central tip) and its change log.
These are marked "Direct observation 2026-11-24". The URLs are given so the checks can be repeated.

## Q1. Per-provider profile: is OAuth2 mandatory for IMAP/SMTP, or are app passwords still accepted?

### Takeaway
As of late 2026, none of the non-Google/Microsoft consumer providers surveyed has made OAuth2 *mandatory* for IMAP/SMTP. App passwords or separate "mail program passwords" remain the accepted (and usually the only self-service) path for a new third-party client. Providers that support OAuth2 for IMAP/SMTP: Yahoo/AOL (XOAUTH2 + OAUTHBEARER), Yandex (XOAUTH2), Mail.ru (XOAUTH2), Fastmail (XOAUTH2 + OAUTHBEARER), Zoho (XOAUTH2, IMAP/POP), Comcast/Xfinity (XOAUTH2 + OAUTHBEARER on IMAP), Seznam.cz (XOAUTH2 + OAUTHBEARER), and iCloud (advertises XOAUTH2, but there is no public developer program). GMX/WEB.DE/mail.com, T-Online, Orange and Libero advertise only PLAIN/LOGIN. Proton is reachable only through the local Bridge. The biggest 2026 change is not technical: Mail.ru put third-party IMAP/POP/SMTP/OAuth access behind a paid "Mail Space" subscription (Feb 2026).

### Cited Findings

#### Live SASL mechanisms advertised (Direct observation 2026-11-24)
| Server | IMAP (993) AUTH= | SMTP AUTH |
|---|---|---|
| imap/smtp.mail.yahoo.com | OAUTHBEARER PLAIN XOAUTH2 | PLAIN LOGIN XOAUTH2 OAUTHBEARER |
| imap/smtp.aol.com | OAUTHBEARER PLAIN XOAUTH2 | PLAIN LOGIN XOAUTH2 OAUTHBEARER |
| imap/smtp.yandex.com | PLAIN XOAUTH2 | LOGIN PLAIN XOAUTH2 |
| imap/smtp.mail.ru | PLAIN XOAUTH2 | PLAIN LOGIN XOAUTH2 |
| imap/smtp.fastmail.com | LOGIN OAUTHBEARER PLAIN XOAUTH2 | PLAIN LOGIN XOAUTH2 OAUTHBEARER |
| imap.mail.me.com / smtp.mail.me.com:587 (iCloud) | ATOKEN ATOKEN2 PLAIN XOAUTH2 | LOGIN PLAIN ATOKEN ATOKEN2 WSTOKEN WETOKEN XOAUTH2 |
| imap/smtp.zoho.com | PLAIN XOAUTH2 | LOGIN PLAIN (no XOAUTH2 on 465) |
| imap.gmx.net / mail.gmx.net | LOGIN PLAIN | PLAIN LOGIN |
| imap/smtp.web.de | LOGIN PLAIN | PLAIN LOGIN |
| imap/smtp.mail.com | LOGIN PLAIN | PLAIN LOGIN |
| imap/smtp.comcast.net | LOGIN OAUTHBEARER PLAIN XOAUTH2 | LOGIN PLAIN XOAUTH2 |
| securesmtp.t-online.de | (IMAP capability not returned by probe) | LOGIN PLAIN |
| imap/smtp.orange.fr | LOGIN PLAIN | LOGIN PLAIN |
| imapmail.libero.it / smtp.libero.it | LOGIN PLAIN | LOGIN PLAIN |
| imap/smtp.seznam.cz | LOGIN OAUTHBEARER PLAIN XOAUTH2 | PLAIN LOGIN OAUTHBEARER XOAUTH2 |

The rows come from direct observation on 2026-11-24 (`openssl s_client -connect <host>:993` with `a1 CAPABILITY`, and `:465` with `EHLO`).

#### Yahoo Mail and AOL (same Yahoo Inc. platform; AT&T att.net mail also runs on it)
- **OAuth for mail is restricted.** Yahoo's developer policy says mail, CalDAV and CardDAV scopes "are not available for self-served setup in the developer console". Developers "can request commercial access to IMAP, CalDav, and CardDav by filling out this form" and must "provide evidence that they are honoring our policies and guidelines" and "meet our security and privacy requirements". The page publishes no review timeline or fees. Contact: mail-api@yahooinc.com — [Yahoo Developer Access](https://senders.yahooinc.com/developer/developer-access)
- **Data-use limits.** Data may only support user-facing email, calendar or contact features. It may not be used for ads, profiling or monetisation, and may not be transferred or sold. Yahoo "reserves the right to revoke access to the APIs at any time" — [Yahoo Developer Access](https://senders.yahooinc.com/developer/developer-access)
- **Scopes.** `mail-r` (read) and `mail-w` (read/write) — [Nylas Yahoo guide](https://developer.nylas.com/docs/provider-guides/yahoo-authentication/). Thunderbird requests `mail-w ycal-w sdct-w` (mail, calendar, contacts) for yahoo.com, att.net and aol.com — [Thunderbird OAuth2Providers.sys.mjs](https://hg-edge.mozilla.org/comm-central/file/tip/mailnews/base/src/OAuth2Providers.sys.mjs). Evolution-data-server uses the same three scopes — [EDS e-oauth2-service-yahoo.c](https://gitlab.gnome.org/GNOME/evolution-data-server/-/blob/master/src/libedataserver/e-oauth2-service-yahoo.c)
- **Endpoints.**
  - Yahoo: authorize `https://api.login.yahoo.com/oauth2/request_auth`, token `https://api.login.yahoo.com/oauth2/get_token`.
  - AOL: `https://api.login.aol.com/oauth2/request_auth` and `/oauth2/get_token`.
  - Source: [Thunderbird OAuth2Providers.sys.mjs](https://hg-edge.mozilla.org/comm-central/file/tip/mailnews/base/src/OAuth2Providers.sys.mjs) and ISPDB [yahoo.com](https://autoconfig.thunderbird.net/v1.1/yahoo.com), [aol.com](https://autoconfig.thunderbird.net/v1.1/aol.com)
- **OIDC metadata** (Direct observation 2026-11-24):
  - Issuer `https://api.login.yahoo.com`, `scopes_supported` = openid, openid2, profile, email (mail scopes are not listed).
  - `grant_types_supported` = authorization_code, refresh_token. There is no device_code grant and no `registration_endpoint`.
  - `code_challenge_methods_supported` = S256. `token_endpoint_auth_methods_supported` = client_secret_basic, client_secret_post.
  - AOL is the same shape under `https://api.login.aol.com`.
  - Source: [Yahoo OIDC metadata](https://api.login.yahoo.com/.well-known/openid-configuration), [AOL OIDC metadata](https://api.login.aol.com/.well-known/openid-configuration)
- **Thunderbird's Yahoo/AOL client** (as of 2026-11):
  - It is a public client using PKCE, with no client secret in the source (Bug 1999291, "Update Yahoo and AOL Oauth to use PKCE", 2025-11-11).
  - Redirect is `https://127.0.0.1`. For the external-browser flow it uses the custom scheme `net.thunderbird://oauth/yahoo`. The source comment says: "This isn't the normal net.thunderbird URL because Yahoo set it and we can't change it" (Bug 2043258, 2026-06-11).
  - Source: [OAuth2Providers.sys.mjs](https://hg-edge.mozilla.org/comm-central/file/tip/mailnews/base/src/OAuth2Providers.sys.mjs), [change log](https://hg-edge.mozilla.org/comm-central/log/tip/mailnews/base/src/OAuth2Providers.sys.mjs)
- **Evolution's Yahoo client** uses a hosted HTTPS redirect page, `https://gnome.pages.gitlab.gnome.org/evolution/YahooOAuth2/`, rather than a loopback. It lets the user override the built-in client ID through the setting `oauth2-yahoo-client-id` — [EDS e-oauth2-service-yahoo.c](https://gitlab.gnome.org/GNOME/evolution-data-server/-/blob/master/src/libedataserver/e-oauth2-service-yahoo.c)
- **Older IMAP OAuth doc.** Yahoo's (Verizon Media era) IMAP/SMTP OAuth2 document asks IMAP clients to send an `ID` command whose NAME is the partner name or the ID assigned at approval — [Verizon Media IMAP/SMTP OAuth2 doc](https://developer.verizonmedia.com/imap-smtp/) (old document, pre-2021 branding; current validity unverified)
- **App passwords.** ISPDB still lists `password-cleartext` next to OAuth2 for Yahoo/AOL IMAP/POP/SMTP — [ISPDB yahoo.com](https://autoconfig.thunderbird.net/v1.1/yahoo.com). Third-party 2026 guides say Yahoo rejects the normal account password from third-party clients and requires an app password, created under Account Security → "Create app password" (or "Generate app password"). These are secondary sources — [aiemaily Yahoo app password](https://aiemaily.com/blog/yahoo-mail-app-password), [themailx 2026](https://themailx.com/blog/yahoo-imap-settings)
- GNOME Online Accounts issue #241 notes Yahoo "has implemented app passwords, so it no longer will accept a user's account password for logins" — [GOA #241](https://gitlab.gnome.org/GNOME/gnome-online-accounts/-/issues/241)

#### Yandex Mail / Yandex 360
- **SASL.** IMAP and SMTP accept OAuth through **XOAUTH2**. The help page says it works "even though it does not appear in the CAPABILITY output", but my 2026-11-24 probe shows `AUTH=XOAUTH2` is now advertised. String: `user=<login>@yandex.ru\001auth=Bearer <token>\001\001`, base64-encoded, sent on one line as `AUTHENTICATE XOAUTH2 <b64>` / `AUTH XOAUTH2 <b64>`. Servers are imap.yandex.com:993 and smtp.yandex.com:465 — [Yandex OAuth for mail](https://yandex.ru/support/mail-business/web/security/oauth.html)
- **Scopes.** `mail:imap_full` (read and delete), `mail:imap_ro` (read only), `mail:smtp` (send) — [Yandex OAuth for mail](https://yandex.ru/support/mail-business/web/security/oauth.html)
- **Registration is self-service, with no review mentioned.** Sign in to id.yandex.ru, then go to `https://oauth.yandex.ru/client/new/`, choose platforms and scopes, enter a contact email, and press "Создать приложение" to get the ClientID — [Yandex OAuth for mail](https://yandex.ru/support/mail-business/web/security/oauth.html)
- **Endpoints.** Thunderbird uses authorize `https://oauth.yandex.com/authorize` and token `https://oauth.yandex.com/token`, with a client secret and redirect `http://localhost` (TB's default changed to 127.0.0.1 in Bug 2045029, 2026-08-29). The external-browser flow for Yandex was enabled 2026-08-19 (Bug 2044684) — [OAuth2Providers.sys.mjs](https://hg-edge.mozilla.org/comm-central/file/tip/mailnews/base/src/OAuth2Providers.sys.mjs), [log](https://hg-edge.mozilla.org/comm-central/log/tip/mailnews/base/src/OAuth2Providers.sys.mjs)
- **Thunderbird hostname mapping.** imap.yandex.com uses scope `mail:imap_full` and smtp.yandex.com uses `mail:smtp` — [OAuth2Providers.sys.mjs](https://hg-edge.mozilla.org/comm-central/file/tip/mailnews/base/src/OAuth2Providers.sys.mjs). ISPDB yandex.ru lists only `password-cleartext` per server but includes an `<oAuth2>` block (issuer oauth.yandex.com, scope `mail:imap_full mail:smtp`) — [ISPDB yandex.ru](https://autoconfig.thunderbird.net/v1.1/yandex.ru)
- **Device code flow is supported.**
  - `POST https://oauth.yandex.com/device/code` with client_id and optional device_id/device_name/scope/optional_scope.
  - The user enters the code at `https://oauth.yandex.com/device`.
  - The client polls `POST https://oauth.yandex.com/token` with `grant_type=device_code` and receives an access_token plus a refresh_token ("same lifetime as the access token").
  - Limit: up to 30 device-linked tokens per app, after which the oldest is invalidated. Code lifetime is about 5–10 minutes.
  - Source: [Yandex ID: device code](https://yandex.ru/dev/id/doc/en/codes/screen-code-oauth)
- **User-side settings.** Settings → Email clients must have IMAP enabled ("From the imap.yandex.com server via IMAP") and "App passwords and OAuth tokens" enabled. Yandex recommends OAuth if the client supports it, and otherwise an app password of type "Mail" created in Yandex ID. IMAP host is imap.yandex.com, or imap.ya.ru outside Russia — [Yandex 360 help: other mail clients](https://www.yandex.com/support/yandex-360/customers/mail/en/mail-clients/others.html)
- **Alpine.** Alpine supports Yandex XOAUTH2 with a manual code copy and paste: the user opens a URL, approves, and pastes the code into Alpine — [Alpine: Authorize Alpine with Yandex](https://alpineapp.email/alpine/alpine-info/misc/AuthorizeAlpineYandex.html)
- **No discovery metadata.** `https://oauth.yandex.ru/.well-known/openid-configuration` did not return JSON (Direct observation 2026-11-24).

#### Mail.ru (VK)
- **SASL.** IMAP and SMTP advertise XOAUTH2 (Direct observation 2026-11-24).
- **Thunderbird.**
  - Issuer `o2.mail.ru`, authorize `https://o2.mail.ru/login`, token `https://o2.mail.ru/token`.
  - Client id `thunderbird` with a client secret and redirect `http://localhost`. No PKCE flag.
  - Scope `mail.imap` for both imap.mail.ru and smtp.mail.ru.
  - Source: [OAuth2Providers.sys.mjs](https://hg-edge.mozilla.org/comm-central/file/tip/mailnews/base/src/OAuth2Providers.sys.mjs)
- **ISPDB** lists OAuth2 for imap.mail.ru and pop.mail.ru but only password-cleartext for smtp.mail.ru — [ISPDB mail.ru](https://autoconfig.thunderbird.net/v1.1/mail.ru)
- **Registration.** Done at the Mail.ru OAuth developer portal `https://oauth.mail.ru/app/`, with scopes `userinfo` and `mail.imap`. The redirect URL must match exactly. EmailEngine sends `prompt_force=1` to force the consent screen. This is a secondary source (EmailEngine docs); the endpoints and any approval requirements were not stated — [EmailEngine Mail.ru guide](https://learn.emailengine.app/docs/accounts/mail-ru)
- **Paid access since 2026.** From February 2026, connecting through third-party services over IMAP, POP, SMTP "or with OAuth authorization" is possible only with a "Mail Space для работы" subscription (from 399 RUB/month or 1,296 RUB/year).
  - Affected: recently created accounts. Accounts 20+ years old keep free access, and already-configured clients keep working, but a new app password cannot be created without paying.
  - Mail.ru called it an "experiment" affecting under 1% of users.
  - Source: [kod.ru, 2026-02-20](https://kod.ru/malru-vvela-platu-za-storonnie-clienti)
- **App passwords.** "Пароль для внешнего приложения" is the standard way to connect mail programs — [hi-tech.mail.ru](https://hi-tech.mail.ru/news/113038-pochta-mailru-sgeneriruet-bezopasnyj-parol-dlya-vneshnih-prilozhenij/)
- **No discovery metadata.** `https://o2.mail.ru/.well-known/openid-configuration` did not return JSON (Direct observation 2026-11-24).

#### Fastmail
- **Policy.** "If you are building an app to distribute to others, you should implement OAuth." App passwords (IMAP/SMTP/DAV) and API tokens (JMAP) are offered "For testing purposes, or building an app for just yourself" — [Fastmail developer docs](https://www.fastmail.com/dev/)
- **No manual registration for native clients.** "For native clients (which can use a localhost or private-use scheme for their redirect URI), there is no need to register manually with us. We support the OAuth Profile for Open Public Clients." Alternatively a client can use an OAuth Client ID Metadata Document instead of dynamic client registration. Manual registration (email partnerships@fastmailteam.com) is only needed for public HTTPS redirect URIs. Loopback `http://localhost/` with arbitrary port and reverse-DNS private-use schemes are accepted — [Fastmail developer docs](https://www.fastmail.com/dev/)
- **Authorization server metadata** (Direct observation 2026-11-24):
  - Issuer `https://api.fastmail.com`, authorize `https://api.fastmail.com/oauth/authorize`, token `https://api.fastmail.com/oauth/refresh`.
  - **`registration_endpoint` `https://api.fastmail.com/oauth/register`**, `client_id_metadata_document_supported: true`.
  - `token_endpoint_auth_methods_supported: ["none"]`, PKCE S256 only, grants authorization_code and refresh_token (no device code).
  - `scopes_supported` includes `urn:ietf:params:oauth:scope:mail`, `urn:ietf:params:oauth:scope:contacts`, `urn:ietf:params:oauth:scope:calendars`, openid, profile, email, offline_access.
  - Source: [Fastmail AS metadata](https://api.fastmail.com/.well-known/oauth-authorization-server)
- **RFC 7628 discovery in practice.** A failed `AUTHENTICATE OAUTHBEARER` on imap.fastmail.com returns
  `{"openid-configuration":"https://api.fastmail.com/.well-known/openid-configuration","scope":"https://www.fastmail.com/dev/protocol-imap","status":"400","schemes":"Bearer"}`
  (Direct observation 2026-11-24).
- **Thunderbird client.**
  - Client id `35f141ae` (public, no secret), PKCE, external browser, redirect `http://127.0.0.1/fastmail`.
  - Scopes `https://www.fastmail.com/dev/protocol-imap`, `.../protocol-pop`, `.../protocol-smtp`, `.../protocol-carddav`, `.../protocol-caldav`.
  - Issuer identifier `https://api.fastmail.com`. "Switch to new Fastmail redirection endpoint" landed 2026-11-02 (Bug 2088070).
  - Source: [OAuth2Providers.sys.mjs](https://hg-edge.mozilla.org/comm-central/file/tip/mailnews/base/src/OAuth2Providers.sys.mjs), [log](https://hg-edge.mozilla.org/comm-central/log/tip/mailnews/base/src/OAuth2Providers.sys.mjs)

#### Apple iCloud Mail
- **App-specific passwords** are the documented path for third-party clients. Two-factor authentication is required to create them, at most 25 can be active, and all are revoked when the Apple Account password changes — [Apple: app-specific passwords](https://support.apple.com/102654)
- **Servers.** imap.mail.me.com:993 (SSL) and smtp.mail.me.com:587 (STARTTLS); no POP — [Apple: iCloud Mail server settings](https://support.apple.com/102525)
- **New since 2025-10-07: OAuth-style authorization for "supported" apps.** "You can access your iCloud Mail, Calendar, and Contacts in supported third-party apps ... after you authorize the app using your Apple Account". The user clicks "Allow", and access is revoked at account.apple.com → Sign-In and Security → Account Data Sharing. "If an app doesn't support this, you can sign in using an app-specific password instead." The page names no apps and mentions no developer program — [Apple: Access iCloud Mail in third-party apps (published 2025-10-07)](https://support.apple.com/en-us/121539)
- **Live servers.** IMAP advertises `AUTH=XOAUTH2` (plus Apple-internal ATOKEN/ATOKEN2). SMTP:587 advertises `XOAUTH2` (plus ATOKEN, ATOKEN2, WSTOKEN, WETOKEN) (Direct observation 2026-11-24).
- **ISPDB** icloud.com lists only password-cleartext — [ISPDB icloud.com](https://autoconfig.thunderbird.net/v1.1/icloud.com)

#### Zoho Mail
- **SASL.** XOAUTH2 for IMAP and POP, with scopes `ZOHOMAIL.PROTOCOL.IMAP` and `ZOHOMAIL.PROTOCOL.POP`. The doc says to register as a "Self-Client" in the Zoho API console, and its examples misspell the mechanism as "XAOUTH2" — [Zoho: SASL XOAUTH2 for IMAP/POP](https://www.zoho.com/mail/help/sasl-xoauth2-smtp-imap-pop-protocol-exchange.html)
- **SMTP.** The SMTP:465 probe advertised only LOGIN PLAIN (Direct observation 2026-11-24), so OAuth for Zoho SMTP was not observed.
- **OIDC metadata** (Direct observation 2026-11-24):
  - Issuer `https://accounts.zoho.com`, authorize `https://accounts.zoho.com/oauth/v2/auth`, token `https://accounts.zoho.com/oauth/v2/token`.
  - **`device_authorization_endpoint` `https://accounts.zoho.com/oauth/v3/device/code`**, device_code grant supported, PKCE plain/S256.
  - Token auth is client_secret_basic, client_secret_post or private_key_jwt.
  - Source: [Zoho OIDC metadata](https://accounts.zoho.com/.well-known/openid-configuration)
- **Device flow** goes through `{accounts-server-url}/oauth/v3/device/code` and `/oauth/v3/device/token`. The accounts server is per data centre (.com, .eu, .in, etc.), and using the wrong DC gives `invalid_client` — [Zoho device flow initiation](https://www.zoho.com/accounts/protocol/oauth/devices/initiation-request.html), [polling](https://www.zoho.com/accounts/protocol/oauth/devices/polling-request.html)
- **IMAP access and passwords.** IMAP access must be enabled manually (Settings → Mail Accounts → IMAP Access) and is not available to newly registered free-plan personal users. With 2FA on, an app-specific password must be used. The help page mentions no OAuth for general IMAP clients — [Zoho: IMAP access](https://www.zoho.com/mail/help/imap-access.html)

#### GMX / WEB.DE / mail.com (United Internet / 1&1 Mail & Media)
- **Password only.** IMAP and SMTP for gmx.net, web.de and mail.com advertise only LOGIN and PLAIN, with no XOAUTH2 or OAUTHBEARER (Direct observation 2026-11-24).
- **ISPDB** lists only password-cleartext for gmx.net and web.de — [ISPDB gmx.net](https://autoconfig.thunderbird.net/v1.1/gmx.net), [ISPDB web.de](https://autoconfig.thunderbird.net/v1.1/web.de)
- **Not in Thunderbird's OAuth list.** GMX/WEB.DE are absent from Thunderbird's built-in provider list — [OAuth2Providers.sys.mjs](https://hg-edge.mozilla.org/comm-central/file/tip/mailnews/base/src/OAuth2Providers.sys.mjs)
- **Forum reports conflict** about "OAuth2 for GMX". One poster says GMX.net supports it and GMX.com never did; others see no OAuth2 option. Users with 2FA must use app-specific passwords, and there were 2026 reports of time-outs and repeated password prompts in Thunderbird 148/149 — [SUMO thread 1573333](https://support.mozilla.org/questions/1573333). The live probe contradicts any claim of server-side OAuth for IMAP/SMTP.

#### Comcast / Xfinity
- **Live servers.** IMAP advertises OAUTHBEARER and XOAUTH2. SMTP advertises XOAUTH2 (Direct observation 2026-11-24; a later probe was answered "Temporarily blacklisted IP Address" after several connections).
- **Thunderbird.**
  - Issuer `comcast.net`, client id `thunderbird-oauth` with secret.
  - Authorize `https://oauth.xfinity.com/oauth/authorize`, token `https://oauth.xfinity.com/oauth/token`, PKCE, redirect `http://localhost`.
  - Scope `https://email.comcast.net/ profile openid` for imap/pop/smtp.comcast.net.
  - Source: [OAuth2Providers.sys.mjs](https://hg-edge.mozilla.org/comm-central/file/tip/mailnews/base/src/OAuth2Providers.sys.mjs)
- **ISPDB** lists OAuth2 plus password-cleartext — [ISPDB comcast.net](https://autoconfig.thunderbird.net/v1.1/comcast.net)
- **OIDC metadata** lists `device_code` among the grant types (alongside many Comcast-internal grants). Token auth is client_secret_basic or client_secret_post, and the email scope is not listed in `scopes_supported` — [Xfinity OIDC metadata](https://oauth.xfinity.com/.well-known/openid-configuration)

#### T-Online (Deutsche Telekom)
- **Separate mail password.** Mail programs need a separate "E-Mail-Passwort", set up in the customer centre, which should differ from the Telekom login password. Servers are secureimap.t-online.de:993 and securesmtp.t-online.de:465/587 with normal password authentication — [Telekom Hilfe: E-Mail-Adresse in Programm einrichten](https://www.telekom.de/hilfe/apps-dienste/e-mail/programme/einrichten), [heise](https://www.heise.de/tipps-tricks/IMAP-Server-fuer-t-online-einrichten-4924243.html)
- **Password only on SMTP.** SMTP advertises only LOGIN PLAIN (Direct observation 2026-11-24). ISPDB lists password-cleartext — [ISPDB t-online.de](https://autoconfig.thunderbird.net/v1.1/t-online.de)

#### Orange (FR) and Libero (IT)
- **Password only.** imap.orange.fr and smtp.orange.fr, imapmail.libero.it and smtp.libero.it advertise only LOGIN PLAIN (Direct observation 2026-11-24).

#### Proton Mail
- **Bridge only.** Proton supports standard IMAP/SMTP clients only through the local Proton Mail Bridge app, which auto-generates a local "Bridge password" for client configuration. There is no direct IMAP/OAuth access to Proton servers — [Proton: clients supported by Bridge](https://proton.me/support/de/clients-supported-bridge), [proton-bridge GitHub](https://github.com/ProtonMail/proton-bridge)

#### Seznam.cz (bonus, newly added to Thunderbird)
- **Thunderbird.** Built-in OAuth2 support was added 2026-09-10 (Bug 2068548).
  - Authorize `https://login.seznam.cz/api/v1/oauth/auth`, token `.../oauth/token`, PKCE, external browser, redirect `http://127.0.0.1/seznam`.
  - Scopes `identity calendar email`, issuer identifier `https://login.seznam.cz`.
  - Source: [OAuth2Providers.sys.mjs](https://hg-edge.mozilla.org/comm-central/file/tip/mailnews/base/src/OAuth2Providers.sys.mjs), [log](https://hg-edge.mozilla.org/comm-central/log/tip/mailnews/base/src/OAuth2Providers.sys.mjs)
- **Live servers.** IMAP and SMTP advertise OAUTHBEARER and XOAUTH2. A bad OAUTHBEARER returns `{"scope":"email","status":"invalid_request"}`, with no openid-configuration field (Direct observation 2026-11-24).

### Inferences
- **App passwords are the realistic baseline.** For Mulberry, app or mail passwords over SASL PLAIN/LOGIN remain viable for every provider surveyed except Proton (Bridge) and possibly new Mail.ru accounts (paywalled regardless of auth method). OAuth2 is a convenience and security upgrade, not a hard requirement, outside Google and Microsoft.
- **Implement both mechanisms.** XOAUTH2 is universally accepted wherever OAuth works. OAUTHBEARER is accepted by Yahoo/AOL, Fastmail, Comcast (IMAP) and Seznam but **not** by Yandex, Mail.ru, Zoho or iCloud. Mulberry should implement both and prefer OAUTHBEARER when advertised (the IETF profiles require it), falling back to XOAUTH2.
- **iCloud's XOAUTH2 has no public route in.** Apple's 2025 "authorize the app using your Apple Account" feature plus the XOAUTH2 advertisement suggests Apple runs a private, partner-only OAuth program for iCloud mail. There is no public registration route, so app-specific passwords are the only option for Mulberry.
- **Yahoo/AOL OAuth is practically out of reach.** It would require a commercial-style application with privacy policy and security evidence and no published SLA. For a small open-source desktop client this is likely slow or unsuccessful, so app passwords are the pragmatic Yahoo/AOL path.
- **A missing CAPABILITY entry proves nothing.** The Yandex help states XOAUTH2 works without being advertised, so an IMAP server's capability list alone is not proof of absence. For GMX/WEB.DE, the lack of any OAuth endpoints in Thunderbird and ISPDB corroborates the probe.

### Gaps
- No official Yahoo help-centre page was retrieved confirming the 2026 app-password policy; the evidence is third-party guides and ISPDB.
- Yahoo token lifetimes (access token commonly said to be 1 h) were not confirmed from a primary source.
- Yandex access/refresh token lifetimes were not confirmed: the token-lifetime doc URL returned 404. The device-code doc only says the refresh token has the same lifetime as the access token.
- Mail.ru official developer docs (o2.mail.ru / oauth.mail.ru) on endpoints, approval requirements and token lifetimes were not found. The Mail.ru paywall details come from a news site (kod.ru), not Mail.ru's own help.
- Zoho SMTP OAuth scope (e.g. `ZohoMail.messages` or a protocol SMTP scope) was not confirmed. Zoho token lifetimes were not retrieved (commonly 1 h access token).
- Comcast and Orange/Libero official developer docs were not found. No reliable info was found on whether Orange or Libero plan OAuth.
- The T-Online IMAP capability was not captured: the probe returned nothing, possibly a slow greeting.
- The Fastmail token lifetimes were not stated on the dev page.

## Q2. What does a new client need to do to obtain a client ID for each provider, and how hard/costly is it?

### Takeaway
- **Free and self-service:** Yandex and Zoho (developer consoles), and Mail.ru (oauth.mail.ru app portal, approval status unknown).
- **Fastmail** needs no registration at all for native clients: dynamic client registration or a Client ID Metadata Document.
- **Formal application with policy review:** Yahoo/AOL.
- **No public path:** iCloud, Comcast (Thunderbird's client appears to be a partner arrangement), and GMX/WEB.DE/T-Online/Orange/Libero, which have no OAuth for IMAP.

### Cited Findings
- **Yandex.** Self-service at `https://oauth.yandex.ru/client/new/`: name, platforms, redirect URIs, scopes (`mail:imap_full`, `mail:imap_ro`, `mail:smtp`), contact email → ClientID. No review step is described — [Yandex OAuth for mail](https://yandex.ru/support/mail-business/web/security/oauth.html). Allowed scopes for a client are viewable at `https://oauth.yandex.com/client/<client_id>/info` — [Yandex device code doc](https://yandex.ru/dev/id/doc/en/codes/screen-code-oauth)
- **Mail.ru.** Create an app at `https://oauth.mail.ru/app/` to get a client ID and secret, with scopes `userinfo` and `mail.imap` — [EmailEngine Mail.ru guide](https://learn.emailengine.app/docs/accounts/mail-ru)
- **Yahoo/AOL.** Mail scopes are "not available for self-served setup in the developer console". Developers apply through the commercial access form and must show compliance with privacy and security requirements — [Yahoo Developer Access](https://senders.yahooinc.com/developer/developer-access). A Yahoo uservoice request asks for "Some way to apply for read-r and read-w scopes for email client developers", showing historical difficulty — [Yahoo uservoice](https://yahoo.uservoice.com/forums/182455-yahoo-developer-network/suggestions/42203566-some-way-to-apply-for-read-r-and-read-w-scopes-for)
- **Fastmail.** "For native clients ... there is no need to register manually with us." Manual registration through partnerships@fastmailteam.com is only for public HTTPS redirect URIs — [Fastmail developer docs](https://www.fastmail.com/dev/)
- **Zoho.** Register in the Zoho API console. The SASL doc tells you to use a "Self-Client", and "Non-browser applications" use the device flow with a client ID and secret from the API console — [Zoho SASL doc](https://www.zoho.com/mail/help/sasl-xoauth2-smtp-imap-pop-protocol-exchange.html), [Zoho non-browser apps](https://www.zoho.com/accounts/protocol/oauth/limited-input-devices.html)
- **Apple iCloud.** The third-party authorization page mentions no developer program and lists no supported apps — [Apple 121539](https://support.apple.com/en-us/121539)
- **Thunderbird's own warning.** Thunderbird's source says of its embedded credentials: "Don't copy these values for your own application - register one for yourself! This code (and possibly even the registration itself) will disappear when this is switched to dynamic client registration." — [OAuth2Providers.sys.mjs](https://hg-edge.mozilla.org/comm-central/file/tip/mailnews/base/src/OAuth2Providers.sys.mjs)

### Inferences
- **Order of work for Mulberry:**
  1. Fastmail first: zero registration, via DCR or CIMD; it is also the reference implementation of the IETF profile.
  2. Yandex: self-service registration, a confidential "secret" embedded in an open-source client as Thunderbird does, plus device flow.
  3. Mail.ru: self-service, but the user base is shrinking because of the paywall.
  4. Zoho: self-service, but per-data-centre endpoints.
- **Yahoo/AOL** would need an application to Yahoo, and success is uncertain for a small project.
- **Client secrets in open source.** Embedding "client secrets" in an open-source binary is standard practice (Thunderbird, Evolution) for providers that require one. The secret is not confidential and providers know it.

### Gaps
- None of the providers publishes Yahoo's review duration, whether Yandex or Mail.ru reviews new mail-scope apps, or any fees. I found no evidence of fees for any of them.

## Q3. Device code flow availability per provider

### Takeaway
- **Confirmed** for Yandex (documented, `oauth.yandex.com/device`) and Zoho (documented, `/oauth/v3/device/code`, in the OIDC metadata).
- **Advertised** in Comcast/Xfinity's metadata (but client registration is not public) and Thunderbird Pro's Keycloak.
- **Not offered** by Yahoo/AOL (metadata lists only authorization_code and refresh_token) or Fastmail (metadata lists only authorization_code and refresh_token; the IETF profile also mandates a browser plus redirect).
- **Unknown** for Mail.ru.

### Cited Findings
- **Yandex.** `POST https://oauth.yandex.com/device/code` → user_code and verification_url (`https://oauth.yandex.com/device`) → poll `/token` with `grant_type=device_code`. Max 30 device tokens per app — [Yandex device code](https://yandex.ru/dev/id/doc/en/codes/screen-code-oauth)
- **Zoho.** `device_authorization_endpoint` is `https://accounts.zoho.com/oauth/v3/device/code` — [Zoho OIDC metadata](https://accounts.zoho.com/.well-known/openid-configuration), [Zoho device flow](https://www.zoho.com/accounts/protocol/oauth/devices/initiation-request.html)
- **Comcast.** Metadata grant types include `device_code` — [Xfinity OIDC metadata](https://oauth.xfinity.com/.well-known/openid-configuration)
- **Yahoo/AOL.** grant_types_supported is authorization_code and refresh_token only — [Yahoo OIDC metadata](https://api.login.yahoo.com/.well-known/openid-configuration)
- **Fastmail.** grant_types_supported is authorization_code and refresh_token only — [Fastmail AS metadata](https://api.fastmail.com/.well-known/oauth-authorization-server)
- **Thunderbird Pro (Thundermail).** The Keycloak realm `https://auth.tb.pro/realms/tbpro` exposes a device_authorization_endpoint and a registration_endpoint — [tb.pro OIDC metadata](https://auth.tb.pro/realms/tbpro/.well-known/openid-configuration)
- **IETF profile.** The open-public-clients profile requires the authorization code grant with PKCE in an external user agent; device flow is not part of the profile (the -06 text reviewed does not mention it) — [draft-ietf-mailmaint-oauth-public-06](https://www.ietf.org/archive/id/draft-ietf-mailmaint-oauth-public-06.html)

### Inferences
- A loopback HTTP listener (auth code + PKCE) is the only universally usable flow. Device code is a useful fallback only for Yandex and Zoho (and Google/Microsoft, out of scope here).
- Whether a Yandex device token may carry `mail:imap_full` depends on the app's registered scopes, which is very likely but not explicitly documented.

### Gaps
- No documentation was found on device flow for Mail.ru. No test was done to confirm that the Yandex device flow yields tokens accepted by IMAP.

## Q4. Dynamic client registration or other ways to avoid per-provider registration

### Takeaway
Fastmail is the only large provider in this set that supports registration-free clients today: RFC 7591 dynamic registration at `https://api.fastmail.com/oauth/register` and OAuth Client ID Metadata Documents. This follows the IETF MAILMAINT "OAuth Profile for Open Public Clients", whose -06 draft (Sept 2026) mandates open DCR with no client secret. Thunderbird still hard-codes per-provider client IDs. Evolution-data-server added a generic "OAuth2Dynamic" service (RFC 8414 discovery + RFC 7591 DCR + PKCE) in October 2026, expected in 3.64.

### Cited Findings
- **Fastmail metadata.** `registration_endpoint` and `client_id_metadata_document_supported: true`, `token_endpoint_auth_methods_supported: ["none"]` — [Fastmail AS metadata](https://api.fastmail.com/.well-known/oauth-authorization-server). "We support the OAuth Profile for Open Public Clients" — [Fastmail dev](https://www.fastmail.com/dev/)
- **draft-ietf-mailmaint-oauth-public-06** (Jenkins & Bucksch, 16 Sept 2026, Informational, expires 20 March 2027):
  - DCR with open registration and no initial access token.
  - Redirect URIs must be `http://127.0.0.1/`, `http://[::1]/` or a reverse-domain private-use scheme; claimed-HTTPS redirects are not allowed.
  - `token_endpoint_auth_method` = "none", and the "server MUST NOT issue a client secret".
  - PKCE S256, RFC 9207 `iss` check, RFC 8707 resource indicators.
  - External user agent required; "MUST NOT use an embedded user-agent".
  - Access tokens must last at least 10 minutes and SHOULD last at least 1 hour. Refresh tokens SHOULD NOT expire except on revocation or after 30+ days unused.
  - SASL OAUTHBEARER (RFC 7628) for IMAP/POP/SMTP.
  - Source: [draft-ietf-mailmaint-oauth-public-06](https://www.ietf.org/archive/id/draft-ietf-mailmaint-oauth-public-06.html); predecessor [draft-jenkins-oauth-public](https://datatracker.ietf.org/doc/draft-jenkins-oauth-public/)
- **Thunderbird.** "For the moment these details are hard-coded, since dynamic client registration is not yet supported." Thunderbird added an API for OAuth client registration at run time by extensions (Bug 1967370, 2025-05-22) and let extensions override built-in hostname registrations (Bug 2055286, 2026-08-07) — [OAuth2Providers.sys.mjs](https://hg-edge.mozilla.org/comm-central/file/tip/mailnews/base/src/OAuth2Providers.sys.mjs), [log](https://hg-edge.mozilla.org/comm-central/log/tip/mailnews/base/src/OAuth2Providers.sys.mjs)
- **Evolution-data-server.** `EOAuth2ServiceDynamic` is an "OAuth2 service whose client data is not compiled in but ... filled at setup time ... typically through RFC 8414 discovery and RFC 7591 dynamic client registration ... uses PKCE (RFC 7636). Since: 3.64". Added in MR !255 (2026-10-06) — [EDS e-oauth2-service-dynamic.c](https://gitlab.gnome.org/GNOME/evolution-data-server/-/blob/master/src/libedataserver/e-oauth2-service-dynamic.c)
- **Thunderbird Pro (Thundermail).** The Keycloak realm exposes a registration_endpoint, but Thunderbird uses a fixed client id `desktop` — [tb.pro metadata](https://auth.tb.pro/realms/tbpro/.well-known/openid-configuration), [OAuth2Providers.sys.mjs](https://hg-edge.mozilla.org/comm-central/file/tip/mailnews/base/src/OAuth2Providers.sys.mjs)
- **Other providers.** None of Yahoo, AOL, Zoho or Comcast exposes a registration_endpoint, and Yandex, Mail.ru and Seznam expose no metadata at the standard OIDC path (Direct observation 2026-11-24).

### Inferences
- **Plan a generic engine.** Implementing the IETF open-public-client profile (RFC 8414 discovery → DCR or CIMD → auth code + PKCE with a 127.0.0.1 loopback → OAUTHBEARER) gives Mulberry Fastmail today. It would also cover any future provider adopting the profile, and Thunderbird and Evolution are both moving that way. Per-provider hard-coded entries would still be needed for Yandex, Mail.ru, Zoho (and Google/Microsoft).

### Gaps
- Whether any provider other than Fastmail has publicly committed to the open public client profile was not found.
- The Client ID Metadata Document draft (OAuth WG) details were not reviewed here.

## Q5. Discovery: Thunderbird ISPDB oAuth2 settings, RFC 7628 SASL error JSON, RFC 8414 metadata, IETF drafts

### Takeaway
There are three practical discovery layers:
1. **Thunderbird ISPDB `<oAuth2>` block** (issuer, scope, authURL, tokenURL; non-standard, and the client ID still has to be compiled in).
2. **RFC 7628 OAUTHBEARER failure JSON** with an `openid-configuration` URL: implemented by Fastmail, which the IETF profile now makes mandatory for compliant servers.
3. **RFC 8414 `/.well-known/oauth-authorization-server`** (Fastmail) or OIDC `/.well-known/openid-configuration` (Yahoo, AOL, Zoho, Xfinity).

The IETF PACC draft adds an `oauth-public.issuer` field to a JSON autoconfig at `https://ua-auto-config.{domain}/.well-known/user-agent-configuration.json`.

### Cited Findings
- **ISPDB example** (yahoo.com): `<oAuth2><issuer>login.yahoo.com</issuer><scope>mail-w</scope><authURL>https://api.login.yahoo.com/oauth2/request_auth</authURL><tokenURL>https://api.login.yahoo.com/oauth2/get_token</tokenURL></oAuth2>`, with `<authentication>OAuth2</authentication>` alongside `password-cleartext` per server — [ISPDB yahoo.com](https://autoconfig.thunderbird.net/v1.1/yahoo.com)
- **ISPDB coverage.** Similar blocks exist for aol.com, mail.ru (`o2.mail.ru`, `mail.imap`), yandex.ru (`oauth.yandex.com`, `mail:imap_full mail:smtp`) and comcast.net. None exist for gmx.net, web.de, t-online.de, icloud.com or zoho.com. The fastmail.com ISPDB fetch returned no usable XML — [ISPDB aol.com](https://autoconfig.thunderbird.net/v1.1/aol.com), [mail.ru](https://autoconfig.thunderbird.net/v1.1/mail.ru), [yandex.ru](https://autoconfig.thunderbird.net/v1.1/yandex.ru), [comcast.net](https://autoconfig.thunderbird.net/v1.1/comcast.net), [gmx.net](https://autoconfig.thunderbird.net/v1.1/gmx.net)
- **Thunderbird lookup key.** Thunderbird maps hostnames (e.g. `imap.yandex.com`, `yahoo.com`, `aol.com`, `att.net`) to an issuer, then the issuer to client details. The issuer name in ISPDB is the lookup key, not the RFC 8414 issuer URL. Thunderbird added RFC 9207 issuer identification in Bug 2044463 (2026-06-08) — [OAuth2Providers.sys.mjs](https://hg-edge.mozilla.org/comm-central/file/tip/mailnews/base/src/OAuth2Providers.sys.mjs), [log](https://hg-edge.mozilla.org/comm-central/log/tip/mailnews/base/src/OAuth2Providers.sys.mjs)
- **RFC 7628 error JSON in the wild.** Fastmail returns `"openid-configuration":"https://api.fastmail.com/.well-known/openid-configuration"` plus `scope`. Seznam returns `{"scope":"email","status":"invalid_request"}` without openid-configuration. Yahoo returned a plain `NO [AUTHENTICATIONFAILED]` with no JSON challenge (Direct observation 2026-11-24).
- **draft-ietf-mailmaint-oauth-public-06.**
  - Servers offering IMAP/POP/SMTP under the profile "MUST include this field [openid-configuration] in every failed OAUTHBEARER response". A client can probe by sending an invalid token.
  - AS metadata comes from RFC 8414 `/.well-known/oauth-authorization-server`, with fallback to the OIDC path, and the issuer must match exactly.
  - HTTP resources use RFC 9728 protected resource metadata.
  - Source: [draft-ietf-mailmaint-oauth-public-06](https://www.ietf.org/archive/id/draft-ietf-mailmaint-oauth-public-06.html)
- **draft-ietf-mailmaint-pacc-01** (Eggert/Apple, Bucksch/Beonex, Diephouse/Apple; 2025-10-20; expired 2026-04-23):
  - Config at `https://ua-auto-config.{domain}/.well-known/user-agent-configuration.json`, with an `authentication` object holding `password` (bool) and `oauth-public` {`issuer`}.
  - Metadata comes from `{issuer}/.well-known/oauth-authorization-server`. The client MUST send the username as `login_hint`. One token must work for all servers.
  - Servers must announce OAUTHBEARER and "should not use XOAUTH2". Clients SHOULD prefer OAuth when both are available.
  - Source: [draft-ietf-mailmaint-pacc-01](https://www.ietf.org/archive/id/draft-ietf-mailmaint-pacc-01.html). Apple co-authorship is notable given Apple's 2025 iCloud third-party authorization feature.
- **Other drafts.** An individual draft, draft-francis-oauth2-mail-client-manual-config (May 2025, expired), proposed manual configuration with a discovery URL and mandatory PKCE S256 — [datatracker](https://datatracker.ietf.org/doc/draft-francis-oauth2-mail-client-manual-config/00/). An earlier individual draft, draft-bucksch-mauth (July 2024), also exists — [draft-bucksch-mauth-00](https://www.ietf.org/archive/id/draft-bucksch-mauth-00.html)

### Inferences
- **Recommended discovery order** for Mulberry:
  1. Built-in table by hostname, as Thunderbird does.
  2. ISPDB `<oAuth2>` for endpoints and scopes.
  3. PACC JSON, if published.
  4. OAUTHBEARER invalid-token probe for `openid-configuration`.
  5. RFC 8414/OIDC metadata → DCR/CIMD when a `registration_endpoint` or CIMD support exists.
- Only step 5 removes the need for a compiled-in client ID.

### Gaps
- I did not check whether a newer PACC revision (-02 or later) exists in 2026; the -01 reviewed had expired in April 2026.
- I did not check whether any provider publishes PACC JSON today.

## Q6. Thunderbird's hard-coded OAuth2 providers (current comm-central), reuse of their client IDs, and other open-source clients (Evolution/GOA, KMail, alpine)

### Takeaway
Thunderbird (comm-central tip, 2026-11-24) hard-codes its own registered client IDs for:
- Google
- Mail.ru
- Yandex
- Seznam.cz (new Sept 2026)
- Yahoo (also att.net)
- AOL
- Microsoft (consumer/M365 and, behind a pref, Graph)
- Fastmail
- Comcast
- Thundermail (tb.pro)

The source explicitly forbids copying them. GMX/WEB.DE, iCloud and Zoho are absent. Evolution has built-in Google, Outlook and Yahoo plus a new generic dynamic-registration service. GNOME Online Accounts offers OAuth only for Google and Microsoft (and the generic IMAP/SMTP provider has no OAuth code). KMail has robust OAuth only for Gmail (Outlook partial). Alpine supports XOAUTH2 for Yandex, among others.

### Cited Findings

#### Thunderbird `kHostnames` → issuer → scopes ([OAuth2Providers.sys.mjs](https://hg-edge.mozilla.org/comm-central/file/tip/mailnews/base/src/OAuth2Providers.sys.mjs), fetched 2026-11-24)
| Hostname key(s) | Issuer | Scopes |
|---|---|---|
| googlemail.com, gmail.com (+ googleapis CardDAV/CalDAV) | accounts.google.com | `https://mail.google.com/` (+ carddav/calendar) |
| imap.mail.ru, smtp.mail.ru | o2.mail.ru | `mail.imap` |
| imap.yandex.com / smtp.yandex.com | oauth.yandex.com | `mail:imap_full` / `mail:smtp` |
| imap/smtp/pop3/caldav/carddav(.personal).seznam.cz | Seznam.cz | `identity calendar email` |
| yahoo.com, att.net | login.yahoo.com | `mail-w ycal-w sdct-w` |
| aol.com | login.aol.com | `mail-w ycal-w sdct-w` |
| office365.com, outlook.com, hotmail.com, graph.microsoft.com | login.microsoftonline.com | IMAP.AccessAsUser.All / POP.AccessAsUser.All / SMTP.Send + offline_access (EWS/Graph scopes separately) |
| imap/pop/smtp/carddav/caldav.fastmail.com | www.fastmail.com | `https://www.fastmail.com/dev/protocol-{imap,pop,smtp,carddav,caldav}` |
| imap/pop/smtp.comcast.net | comcast.net | `https://email.comcast.net/ profile openid` |
| thundermail.com | auth.tb.pro | `openid profile email offline_access` |

#### Thunderbird `kIssuers` details (same source)
| Issuer | client_id | Secret | Endpoints | PKCE / browser / redirect |
|---|---|---|---|---|
| accounts.google.com | 406964657835-aq8lmia8j95dhl1a2bvharmfk3t1hgqj.apps.googleusercontent.com | yes | accounts.google.com/o/oauth2/auth, www.googleapis.com/oauth2/v3/token | PKCE, external browser |
| o2.mail.ru | `thunderbird` | yes | o2.mail.ru/login, /token | no PKCE flag; `http://localhost` |
| oauth.yandex.com | 2a00bba7374047a6ab79666485ffce31 | yes | oauth.yandex.com/authorize, /token | external browser; `http://localhost` |
| Seznam.cz | 7c0b7108b6…cddfa7e0c187 | no | login.seznam.cz/api/v1/oauth/auth, /token | PKCE, external browser; `http://127.0.0.1/seznam` |
| login.yahoo.com | dj0yJmk9WVZUaWRN… | no | api.login.yahoo.com/oauth2/request_auth, /get_token | PKCE; `https://127.0.0.1`, scheme `net.thunderbird://oauth/yahoo` |
| login.aol.com | dj0yJmk9MGNoTTQ0… | no | api.login.aol.com/oauth2/request_auth, /get_token | PKCE; `https://127.0.0.1` |
| login.microsoftonline.com | 9e5f94bc-e8a4-4e73-b8be-63364c29d753 | no | /common/oauth2/v2.0/authorize, /token | external browser |
| www.fastmail.com | 35f141ae | no | api.fastmail.com/oauth/authorize, /oauth/refresh | PKCE, external browser; `http://127.0.0.1/fastmail` |
| comcast.net | thunderbird-oauth | yes | oauth.xfinity.com/oauth/authorize, /oauth/token | PKCE; `http://localhost` |
| auth.tb.pro | desktop | no | Keycloak realm tbpro | PKCE, external browser |

#### Recent Thunderbird OAuth changes ([hg log](https://hg-edge.mozilla.org/comm-central/log/tip/mailnews/base/src/OAuth2Providers.sys.mjs))
| Date | Bug | Change |
|---|---|---|
| 2025-11-11 | 1999291 | Yahoo/AOL switched to PKCE |
| 2026-03-19 | 2019793 | Listen on http://localhost redirect targets (loopback listener) |
| 2026-04-30 | 2033048 | Gmail switched to PKCE |
| 2026-05-25 | 2033653 | `net.thunderbird:` scheme callbacks |
| 2026-06-08 | 2044463 | RFC 9207 issuer identification |
| 2026-08-19 | 2044684 | Yandex switched to the external browser flow |
| 2026-08-29 | 2045029 | Default loopback target changed to 127.0.0.1 |
| 2026-09-10 | 2068548 | Seznam added |
| 2026-11-02 | 2088070 | New Fastmail redirect |

#### Reuse of Thunderbird's client IDs
- Thunderbird's source says: "Don't copy these values for your own application - register one for yourself!" — [OAuth2Providers.sys.mjs](https://hg-edge.mozilla.org/comm-central/file/tip/mailnews/base/src/OAuth2Providers.sys.mjs)
- Yahoo's policy forbids building services that recreate the API functionality for non-approved third parties and allows revocation at any time — [Yahoo Developer Access](https://senders.yahooinc.com/developer/developer-access)

#### Evolution / GNOME Online Accounts
- **Evolution-data-server** ships OAuth2 services `google`, `outlook`, `yahoo` and (new) `dynamic`. PKCE for Google and Outlook was added in MR !268 on 2026-11-04 — [EDS libedataserver tree](https://gitlab.gnome.org/GNOME/evolution-data-server/-/tree/master/src/libedataserver), [e-oauth2-service-dynamic.c](https://gitlab.gnome.org/GNOME/evolution-data-server/-/blob/master/src/libedataserver/e-oauth2-service-dynamic.c)
- **GNOME Online Accounts** backends are exchange, fedora (Kerberos), google, imapsmtp, kerberos, msgraph, owncloud and webdav. The `goaimapsmtpprovider.c` file contains no "oauth" references (Direct observation 2026-11-24 of [GOA goabackend](https://gitlab.gnome.org/GNOME/gnome-online-accounts/-/tree/master/src/goabackend)).
- A November 2024 GNOME Discourse thread states that Evolution and GOA "only support a handful of OAuth2 providers, such as Microsoft and Google" — [GNOME Discourse](https://discourse.gnome.org/t/add-third-party-oauth2-support-to-evolution-and-or-gnome-online-accounts/24877)

#### KMail / Akonadi
- Gmail XOAUTH2 support has existed since 2017. KDE bug 457233, "Please provide oauth2 login for IMAP - not just Gmail", was still CONFIRMED with 2025 activity. Outlook OAuth support is disputed and partial — [KDE bug 457233](https://bugs.kde.org/show_bug.cgi?id=457233), [KDE Discuss](https://discuss.kde.org/t/modern-authentication-for-outlook-account-on-kmail/18192)

#### Alpine
- Alpine documents XOAUTH2 for Yandex, configured with `/auth=xoauth2` in the server string and a manual code copy and paste — [Alpine Yandex](https://alpineapp.email/alpine/alpine-info/misc/AuthorizeAlpineYandex.html)

### Inferences
- **Mulberry must register its own clients.** Reusing Thunderbird's IDs would violate Mozilla's explicit request and the providers' terms, and could get the Thunderbird registration revoked. This matters especially for Yahoo, where Yahoo, not Mozilla, controls the redirect URI.
- **Copy the design, not the IDs.** The table-driven design (hostname → issuer → client details, plus an extension or user override of the client ID like Evolution's `oauth2-yahoo-client-id`) is a sensible model. User-supplied client IDs let advanced users use their own Yahoo or Yandex registrations.
- **Yahoo/AOL may share a client.** AOL and Yahoo use different client IDs in Thunderbird, but a Yahoo-approved partner presumably receives both. The att.net domain uses the Yahoo issuer.

### Gaps
- I could not fetch Alpine's source (repo.or.cz is behind an Anubis bot wall), so Alpine's full provider list (it historically includes Gmail, Outlook and Yahoo) and its use of device flow for Outlook/Gmail were not verified here.
- KMail's 2026 status was not checked against current kdepim-runtime source.
- Whether Evolution 3.64 has been released and exposes the dynamic service in its account UI was not verified.
