# Build one OAuth2 engine, reserve Graph for calendars

*Updated 2026-11-24: added the section "How Evolution and KMail reach Gmail", corrected the statements about Google's CASA security assessment, and added the Graph SDK findings ("No Graph SDK for C++", "Calendar sync").*

Mulberry should implement OAuth2 itself, in-process, as a new **built-in authenticator type** rather than as a SASL plugin. The design uses the RFC 8252 native-app pattern: authorization code + PKCE (S256), the login page opened in the user's system browser, and the code returned to a one-shot listener on `127.0.0.1` at an ephemeral port. Where the provider allows it, the device-code grant serves as a fallback for headless use. Mulberry then speaks SASL **XOAUTH2 and OAUTHBEARER** natively for IMAP/POP3/SMTP/ManageSieve and sends an `Authorization: Bearer` header for CalDAV/CardDAV. Refresh tokens live in Mulberry's existing libsodium keyring. A data table of providers drives all of this, the client IDs in it are user-overridable, and RFC 8414/7591 discovery and dynamic registration cover the IETF "open public client" profile. One engine covers Google, Microsoft 365/Outlook.com, Yandex, Mail.ru, Fastmail, Zoho, Seznam and any standards-following server, and it adds **zero new link-time packages**. It needs libcurl (already linked, but only into the HTML viewer), OpenSSL/libsodium (already required), a vendored MIT JSON parser and about 200 lines of socket code. My estimate is about 1,800–2,500 new or changed lines, roughly **6–8 person-weeks**. The GUI change is one popup item, an optional small panel and one "waiting for browser sign-in" dialog. **Microsoft Graph is not needed for mail**: IMAP/POP/SMTP with OAuth2 remain supported for both Microsoft 365 and Outlook.com, and no retirement date has been announced. Graph is needed for Microsoft 365 **calendar and contacts**, because Exchange Online has no CalDAV/CardDAV. That is a separate and much larger project, a JSON sync engine plus lossy iCalendar/vCard mappers, which I estimate at 3–5 person-months; it reuses the same token engine. The real critical path is client registration, not code. Google's IMAP scope is "restricted" and requires verification, renewed every year. Google's pages disagree on whether a local-only client also needs the paid annual third-party security assessment (CASA). Yahoo/AOL mail scopes need a commercial application. Microsoft tenants often show "Need admin approval" to apps whose publisher is not verified.

## Only Google and Microsoft force OAuth2; everyone else still takes app passwords

The urgency comes from two providers. Google stopped accepting plain passwords from third-party apps on personal accounts on 2022-05-30. Google Workspace followed on **2025-05-01**, which also ended password access for CalDAV, CardDAV, IMAP, SMTP and POP ([Workspace Admin Help](https://knowledge.workspace.google.com/admin/apps/control-access-to-less-secure-apps?hl=en), [transition article](https://knowledge.workspace.google.com/admin/sync/transition-from-less-secure-apps-to-oauth?hl=en)). App passwords survive only for personal accounts with 2-Step Verification that are not on Advanced Protection or security-key-only 2SV, and Google calls them "not recommended" ([Google Account Help](https://support.google.com/accounts/answer/185833?hl=en)). Microsoft disabled basic auth for IMAP/POP/EWS in Exchange Online from 2022-10-01, and no one can re-enable it ([Microsoft Learn](https://learn.microsoft.com/en-us/exchange/clients-and-mobile-in-exchange-online/deprecation-of-basic-authentication-exchange-online)). Outlook.com followed on **2024-09-16** ([Microsoft Support](https://support.microsoft.com/en-us/topic/c5d65390-9676-4763-b41f-d7986499a90d)). SMTP AUTH basic auth in Microsoft 365 is unchanged through December 2026. At the **end of December 2026** it becomes **disabled by default** for existing tenants, though admins can still re-enable it. The final removal date will be announced in H2 2027 ([MC786329](https://mc.merill.net/message/MC786329), [Office365ITPros](https://office365itpros.com/2026/01/29/smtp-auth-basic-retirement/)). For Gmail, Workspace, Microsoft 365 and Outlook.com, OAuth2 is therefore the only path a general-purpose client can rely on from 2027.

Every other large provider surveyed still accepts app passwords or a separate "mail program password" over PLAIN/LOGIN. For them OAuth2 is a security and convenience upgrade, not a requirement. Yandex, for example, recommends OAuth but still offers "Mail" app passwords ([Yandex 360 help](https://www.yandex.com/support/yandex-360/customers/mail/en/mail-clients/others.html)), and ISPDB still lists password authentication next to OAuth2 for Yahoo and AOL ([ISPDB yahoo.com](https://autoconfig.thunderbird.net/v1.1/yahoo.com)). The biggest 2026 change outside Google and Microsoft is commercial, not technical. Since February 2026 Mail.ru puts third-party IMAP/POP/SMTP access, "or with OAuth authorization", behind a paid "Mail Space" subscription for newer accounts, at 399 RUB/month or 1,296 RUB/year ([kod.ru](https://kod.ru/malru-vvela-platu-za-storonnie-clienti)). An anonymous CAPABILITY/EHLO probe on 2026-11-24 showed that GMX, WEB.DE, mail.com, T-Online, Orange and Libero advertise only PLAIN/LOGIN. Thunderbird's ISPDB likewise lists only password authentication for them ([ISPDB gmx.net](https://autoconfig.thunderbird.net/v1.1/gmx.net), [ISPDB t-online.de](https://autoconfig.thunderbird.net/v1.1/t-online.de)). Proton is reachable only through its local Bridge ([Proton](https://proton.me/support/de/clients-supported-bridge)).

The table below gives the realisation details Mulberry needs per hoster as of late 2026. The SASL columns combine provider documentation with the 2026-11-24 anonymous probes recorded in the research notes.

| Provider | Password status (late 2026) | Authorize / token endpoints | Scopes Mulberry needs | SASL offered | Desktop flows | Getting a client ID |
|---|---|---|---|---|---|---|
| **Gmail / Workspace** | OAuth only (app passwords: personal + 2SV only) | `accounts.google.com/o/oauth2/v2/auth`, `oauth2.googleapis.com/token`, revoke `oauth2.googleapis.com/revoke` ([Google](https://developers.google.com/identity/protocols/oauth2/native-app)) | `https://mail.google.com/` (restricted), `…/auth/calendar`, `…/auth/carddav` ([Thunderbird](https://raw.githubusercontent.com/mozilla/releases-comm-central/master/mailnews/base/src/OAuth2Providers.sys.mjs)) | XOAUTH2 (documented), OAUTHBEARER (advertised) ([Google XOAUTH2](https://developers.google.com/workspace/gmail/imap/xoauth2-protocol)) | Loopback 127.0.0.1/[::1], any port; **no device flow for mail** ([Google device flow](https://developers.google.com/identity/protocols/oauth2/limited-input-device)) | Restricted-scope verification renewed yearly (CASA possibly required, see below), or bring-your-own ID |
| **Microsoft 365 / Outlook.com** | OAuth only for IMAP/POP; SMTP basic off by default end-2026 | `login.microsoftonline.com/{common\|organizations\|consumers}/oauth2/v2.0/authorize`, `/token` | `https://outlook.office.com/IMAP.AccessAsUser.All`, `…/POP.AccessAsUser.All`, `…/SMTP.Send`, `offline_access`; Graph `Calendars.ReadWrite`, `Contacts.ReadWrite` ([Microsoft](https://learn.microsoft.com/en-us/exchange/client-developer/legacy-protocols/how-to-authenticate-an-imap-pop-smtp-application-by-using-oauth)) | **XOAUTH2 only** | Loopback (port ignored), device code | Free Entra registration; publisher verification needs a Cloud Partner Program account |
| **Yahoo / AOL / att.net** | App passwords | `api.login.yahoo.com/oauth2/request_auth`, `/get_token` (AOL: `api.login.aol.com`) | `mail-w ycal-w sdct-w` | XOAUTH2, OAUTHBEARER | Auth code + PKCE only; no device grant ([Yahoo metadata](https://api.login.yahoo.com/.well-known/openid-configuration)) | Mail scopes "not available for self-served setup"; commercial application form ([Yahoo](https://senders.yahooinc.com/developer/developer-access)) |
| **Yandex / Yandex 360** | App passwords or OAuth; user must enable "App passwords and OAuth tokens" | `oauth.yandex.com/authorize`, `/token` | `mail:imap_full` (or `mail:imap_ro`), `mail:smtp` ([Yandex](https://yandex.ru/support/mail-business/web/security/oauth.html)) | XOAUTH2 | Loopback; device code at `oauth.yandex.com/device/code` ([Yandex ID](https://yandex.ru/dev/id/doc/en/codes/screen-code-oauth)) | Self-service at `oauth.yandex.ru/client/new/`; apps can be moderated |
| **Mail.ru (VK)** | App passwords; third-party access paywalled for newer accounts | `o2.mail.ru/login`, `o2.mail.ru/token` | `mail.imap` | XOAUTH2 | Loopback (`http://localhost` in Thunderbird) | Self-service at `oauth.mail.ru/app/` ([EmailEngine](https://learn.emailengine.app/docs/accounts/mail-ru)) |
| **Fastmail** | App passwords "for testing"; OAuth for distributed apps | `api.fastmail.com/oauth/authorize`, `/oauth/refresh` | `urn:ietf:params:oauth:scope:mail`, `…:contacts`, `…:calendars` ([metadata](https://api.fastmail.com/.well-known/oauth-authorization-server)) | XOAUTH2, OAUTHBEARER | Loopback or private-use scheme; no device grant | **None needed**: dynamic registration or Client ID Metadata Document ([Fastmail](https://www.fastmail.com/dev/)) |
| **Zoho Mail** | App-specific passwords with 2FA | `accounts.zoho.com/oauth/v2/auth`, `/token` (per data centre) | `ZOHOMAIL.PROTOCOL.IMAP`, `…POP` ([Zoho](https://www.zoho.com/mail/help/sasl-xoauth2-smtp-imap-pop-protocol-exchange.html)) | XOAUTH2 on IMAP/POP; SMTP showed only PLAIN/LOGIN | Loopback; device code `/oauth/v3/device/code` ([metadata](https://accounts.zoho.com/.well-known/openid-configuration)) | Self-service API console |
| **Comcast / Xfinity** | Password + OAuth | `oauth.xfinity.com/oauth/authorize`, `/token` | `https://email.comcast.net/ profile openid` | XOAUTH2, OAUTHBEARER (IMAP) | Loopback | No public registration found |
| **Seznam.cz** | Password + OAuth | `login.seznam.cz/api/v1/oauth/auth`, `/token` | `identity calendar email` | XOAUTH2, OAUTHBEARER | Loopback, PKCE | Partner (Thunderbird added it in Sept 2026) |
| **iCloud** | App-specific passwords ([Apple](https://support.apple.com/102654)) | none public | — | XOAUTH2 advertised, partner-only ([Apple](https://support.apple.com/en-us/121539)) | — | No developer program |
| **GMX, WEB.DE, mail.com, T-Online, Orange, Libero** | Password only | — | — | PLAIN/LOGIN | — | — |

The provider sources not linked in the table are [Thunderbird's OAuth2Providers.sys.mjs](https://hg-edge.mozilla.org/comm-central/file/tip/mailnews/base/src/OAuth2Providers.sys.mjs) for the Yahoo, AOL, Mail.ru, Comcast and Seznam endpoints and scopes, [ISPDB yahoo.com](https://autoconfig.thunderbird.net/v1.1/yahoo.com) for Yahoo's OAuth block, and Microsoft's [redirect URI](https://learn.microsoft.com/en-us/entra/identity-platform/reply-url) and [publisher verification](https://learn.microsoft.com/en-us/entra/identity-platform/publisher-verification-overview) pages.

The per-provider details below change what the code must do, not just its configuration tables.

**Google.** Refresh tokens die after six months unused, on a password change (for tokens with Gmail scopes), after 7 days if the Cloud project is External and still in "Testing", and when an account exceeds **100 refresh tokens per client ID**, which silently evicts the oldest ([Google: Using OAuth 2.0](https://developers.google.com/identity/protocols/oauth2)). Mulberry must therefore keep one refresh token per account and share it across IMAP, SMTP, CalDAV and CardDAV, rather than authorising each protocol separately. Incremental authorization is not supported for installed apps. With granular consent the user may untick calendar or contacts, so the app "must verify which scopes were actually granted" ([Google native-app guide](https://developers.google.com/identity/protocols/oauth2/native-app)). Google's CalDAV (`apidata.googleusercontent.com/caldav/v2/…`) and CardDAV (`www.googleapis.com/.well-known/carddav`) reject Basic auth with 401 ([CalDAV guide](https://developers.google.com/workspace/calendar/caldav/v2/guide), [CardDAV](https://developers.google.com/people/carddav)). Mulberry's existing DAV clients therefore need nothing beyond the Bearer header. Workspace admins can mark a client ID Trusted, Limited or Blocked, and setting Gmail to "Restricted" revokes untrusted apps' tokens ([Workspace Admin Help](https://knowledge.workspace.google.com/admin/apps/control-which-apps-access-google-workspace-data?hl=en)). Mulberry should show its client ID in the UI so users can hand it to their admin. Errors such as `admin_policy_enforced` and `invalid_grant` with `error_subtype=invalid_rapt` need specific messages.

**Microsoft.** The port in a localhost redirect is ignored for matching. An `http://127.0.0.1` redirect can only be added through the app manifest, and `[::1]` is not supported ([Microsoft Learn](https://learn.microsoft.com/en-us/entra/identity-platform/reply-url)). Access tokens last 60–90 minutes. Refresh tokens last 90 days, **replace themselves on every use**, and are not bound to a resource ([Microsoft: refresh tokens](https://learn.microsoft.com/en-us/entra/identity-platform/refresh-tokens)). Each refresh must therefore persist the new token atomically. The servers advertise only XOAUTH2. To open a shared mailbox, the client puts that mailbox's address into the XOAUTH2 `user=` field while presenting the delegate's token. Failures come back as a bare `NO AUTHENTICATE failed.` or `535 5.7.3` with no reason ([Microsoft Learn](https://learn.microsoft.com/en-us/exchange/client-developer/legacy-protocols/how-to-authenticate-an-imap-pop-smtp-application-by-using-oauth)), so Mulberry must suggest likely causes itself. If a tenant has security defaults on, SMTP AUTH is already disabled, whatever the client does ([Microsoft Learn](https://learn.microsoft.com/en-us/exchange/clients-and-mobile-in-exchange-online/authenticated-client-smtp-submission)).

**Yahoo/AOL.** Token requests use HTTP Basic client authentication. Thunderbird's registration uses an `https://127.0.0.1` redirect plus a custom scheme that "Yahoo set … and we can't change", which shows that Yahoo controls redirect URIs per partner ([Thunderbird source](https://hg-edge.mozilla.org/comm-central/file/tip/mailnews/base/src/OAuth2Providers.sys.mjs)).

**Error discovery.** Fastmail's failed OAUTHBEARER returns an RFC 7628 JSON challenge that contains an `openid-configuration` URL. The IETF open-public-client draft makes that field mandatory for compliant servers, which gives Mulberry a standards-based discovery path for unknown hosts ([draft-ietf-mailmaint-oauth-public-06](https://www.ietf.org/archive/id/draft-ietf-mailmaint-oauth-public-06.html)).

## Graph is for calendars and contacts, not for mail

Microsoft Graph is Microsoft's proprietary REST/JSON (OData) API at `graph.microsoft.com`. It exposes the Exchange Online and Outlook.com mailbox (messages, events, contacts) as JSON resources. It is not a transport for IMAP, CalDAV or anything standard. The claim that Microsoft 365 "needs Graph" is **wrong for mail** and **right for calendars and contacts**. Microsoft states that OAuth2 for IMAP, POP and SMTP "is available for both Microsoft 365 … and Outlook.com users" ([Microsoft Learn](https://learn.microsoft.com/en-us/exchange/client-developer/legacy-protocols/how-to-authenticate-an-imap-pop-smtp-application-by-using-oauth)). Developers on those protocols "will be able to keep the same protocol" provided they implement modern authentication ([Microsoft Learn](https://learn.microsoft.com/en-us/exchange/clients-and-mobile-in-exchange-online/deprecation-of-basic-authentication-exchange-online)). The protocol being switched off is **EWS**: global disablement started in October 2026 and EWS is fully disabled in April 2027 ([Microsoft Learn: EWS deprecation](https://learn.microsoft.com/en-us/exchange/clients-and-mobile-in-exchange-online/deprecation-of-ews-exchange-online)). That is why Thunderbird and Evolution, which had EWS backends, are moving to Graph. Thunderbird 154 (August 2026) shipped Graph **for email only**, and Mozilla says that users whose Microsoft 365 account works over IMAP "need no action", because EWS/Graph are "meant for organizations that block IMAP" ([Thunderbird blog](https://blog.thunderbird.net/2026/09/thunderbird-desktop-new-protocol-support-microsoft-graph-api/)).

| Function | Microsoft 365 (work/school) | Outlook.com (personal) | Protocol Mulberry should use | Token audience and scopes |
|---|---|---|---|---|
| Read/sync mail | IMAP (or POP) + XOAUTH2 | IMAP (or POP) + XOAUTH2 | existing `CIMAPClient` / `CPOP3Client` | `outlook.office.com`: `IMAP.AccessAsUser.All` / `POP.AccessAsUser.All` |
| Send mail | SMTP + XOAUTH2 (needs tenant SMTP AUTH on) | SMTP + XOAUTH2 | existing `CSMTPSender`; optional Graph `sendMail` fallback | `outlook.office.com/SMTP.Send` (fallback: Graph `Mail.Send`) |
| Server-side filters | No ManageSieve on Exchange | No | none | — |
| Calendar | **Graph only** (no CalDAV) | **Graph only** | new Graph calendar client | `graph.microsoft.com`: `Calendars.ReadWrite` (+ `.Shared`) |
| Contacts | **Graph only** (no CardDAV) | **Graph only** | new Graph address-book client | `graph.microsoft.com`: `Contacts.ReadWrite` (+ `People.Read`) |

A Graph *mail* backend would cost far more than it returns, because it is a second, Microsoft-only mail engine. Delta sync is per folder. Item IDs change when an item moves unless `Prefer: IdType="ImmutableId"` is sent. Flags are reduced to `isRead`, follow-up flags and categories ([Graph message resource](https://learn.microsoft.com/en-us/graph/api/resources/message?view=graph-rest-1.0), [delta query](https://learn.microsoft.com/en-us/graph/delta-query-messages)). There is no IMAP `APPEND` equivalent: "non-draft MIME creation" was still on Microsoft's parity roadmap for Q4 2026 ([EWS deprecation page](https://learn.microsoft.com/en-us/exchange/clients-and-mobile-in-exchange-online/deprecation-of-ews-exchange-online)). Push works only through public webhooks, so a desktop client must poll ([change notifications](https://learn.microsoft.com/en-us/graph/change-notifications-overview)). Each app and mailbox is limited to **4 concurrent requests** and 10,000 requests per 10 minutes ([throttling limits](https://learn.microsoft.com/en-us/graph/throttling-limits)). The one mail case worth keeping in reserve is sending for tenants that have SMTP AUTH disabled. `POST /me/sendMail` accepts a base64 MIME body and works for both work and personal accounts ([Graph sendMail](https://learn.microsoft.com/en-us/graph/api/user-sendmail?view=graph-rest-1.0)). Community reports put its request ceiling around 3–4 MB, and an upload session is needed above that ([Microsoft Q&A](https://learn.microsoft.com/en-in/answers/questions/1824204/graph-api-user-sendmail-request-size-limit)). That limit makes it a fallback, not a replacement.

Calendars and contacts are where Graph is unavoidable. Microsoft's client-access list for Exchange Online is ActiveSync, MAPI, Outlook on the web, IMAP and POP, with **no CalDAV or CardDAV** ([CiraSync summary](https://cirasync.com/?p=74448)). ActiveSync is proprietary and licensed, so Graph is the only realistic modern route. The work lies in translation, not transport. Graph events are JSON, not iCalendar, and no Graph endpoint imports or exports ICS ([Microsoft Q&A](https://learn.microsoft.com/en-za/answers/questions/1818512/how-can-i-use-microsoft-graph-(-powershell)-to-cop)). Officially, delta sync in v1.0 exists only over a time-windowed `calendarView` that returns expanded occurrences and does not support `$select`; the documentation calls the unbounded per-calendar `events/delta` beta ([delta for events](https://learn.microsoft.com/en-us/graph/delta-query-events)). In practice `events/delta` also works on v1.0 (see "Calendar sync" below). With the documented window, Mulberry would sync a sliding window and rebuild each series from its master event, using `exceptionOccurrences` and `cancelledOccurrences` ([Graph get event](https://learn.microsoft.com/en-us/graph/api/event-get?view=graph-rest-1.0)). Recurrence uses six pattern types and three range types ([recurrencePattern](https://learn.microsoft.com/en-us/graph/api/resources/recurrencepattern?view=graph-rest-1.0)). Every Graph pattern converts to an RRULE, but many RRULEs cannot convert back: HOURLY, multiple BYMONTHDAY values, BYWEEKNO, RDATE and others. Mulberry needs a "cannot represent" policy for those.

Time zones are Windows names plus a fixed list of IANA aliases, and GET responses come back in UTC unless `Prefer: outlook.timezone` is sent ([dateTimeTimeZone](https://learn.microsoft.com/en-us/graph/api/resources/datetimetimezone?view=graph-rest-1.0)). In v1.0, `iCalUId` differs for each occurrence; only the beta `uid` is stable across a series ([event beta](https://learn.microsoft.com/en-us/graph/api/resources/event?view=graph-rest-beta)). Exchange performs scheduling itself through `accept`/`decline`/`cancel`. Mulberry must therefore switch off its own iTIP/iMIP output for Graph-backed calendars, or attendees will receive duplicate invitations. Events allow a single reminder ([event resource](https://learn.microsoft.com/en-us/graph/api/resources/event?view=graph-rest-1.0)).

Contacts use a fixed Outlook schema. It has three email slots, one mobile number, one home, one business and one other address, and no fax or pager types ([contact resource](https://learn.microsoft.com/en-us/graph/api/resources/contact?view=graph-rest-1.0)). Personal contact lists and a Global Address List equivalent are still missing ([EWS parity roadmap](https://learn.microsoft.com/en-us/exchange/clients-and-mobile-in-exchange-online/deprecation-of-ews-exchange-online), [Evolution maintainer](https://discourse.gnome.org/t/is-microsoft-365-graph-api-expected-to-be-available-in-evolution-3-60-x/36019)). Unmapped vCard data must be kept in open extensions so that it survives a round trip.

The good news is on the authorization side. `Calendars.ReadWrite`, `Contacts.ReadWrite` and `People.Read` need no admin consent and can be granted by personal accounts ([Graph permissions reference](https://learn.microsoft.com/en-us/graph/permissions-reference)), although tenant policy can still restrict user consent. An access token is issued for **one resource only**, but "refresh tokens … aren't tied to a resource" ([Microsoft: refresh tokens](https://learn.microsoft.com/en-us/entra/identity-platform/refresh-tokens), [MSAL resources and scopes](https://learn.microsoft.com/en-us/entra/msal/javascript/browser/resources-and-scopes)). One sign-in can therefore collect consent for both `outlook.office.com` and `graph.microsoft.com`. The token engine then mints a token for each resource with separate `refresh_token` grants. This raw-protocol behaviour is inferred from the MSAL documentation and has not been tested. Two open-source mappings exist to learn from. evolution-ews is **LGPL-2.1** ([openSUSE spec](https://build.opensuse.org/projects/openSUSE/packages/evolution-ews/files/evolution-ews.spec?expand=0)). DavMail 7.0.0, released September 2026 with 70 Graph changes, is **GPL-2.0** ([Help Net Security](https://helpnetsecurity.com/2026/09/22/davmail-7-0-0-microsoft-graph), [Debian copyright](https://metadata.ftp-master.debian.org/changelogs/main/d/davmail/stable_copyright)). Mulberry is Apache-2.0 ([LICENSE](../LICENSE)), so both are references to study, not code to copy. DavMail in O365Graph mode also works today as a local CalDAV/CardDAV gateway. Mulberry's *existing* DAV clients can talk to Microsoft 365 through it without new Mulberry code, at the price of a Java daemon.

### No Graph SDK for C++: a thin REST layer over libcurl

**No Graph SDK exists in C or C++, official or community, and none is planned.** Microsoft's [SDK overview](https://learn.microsoft.com/en-us/graph/sdks/sdks-overview) lists "C#, CLI, PowerShell, TypeScript | JavaScript, Java, Go, PHP, Python". As of 2026-11-24 the C#, Java, Go, Python, PHP and PowerShell SDKs are maintained. The TypeScript SDK is still pre-release. The CLI, Ruby, Objective-C, iOS and Android SDKs are archived.

None of the 233 repositories in the `microsoftgraph` GitHub organisation is written in C or C++. Microsoft's own C++ REST library is archived too; its [README](https://github.com/microsoft/cpprestsdk) says "This repository is no longer maintained … alternatives … include libcurl or Boost beast". The overview page also notes that "Microsoft CSS doesn't officially support SDKs but Microsoft supports the HTTP request". Using the raw REST API is therefore a fully supported way to use Graph.

The other ways to get generated or ready-made C++ code all fail:

- **Kiota** generates the official SDKs. Its targets in [v1.39](https://github.com/microsoft/kiota) (2026-11) are C#, Java, Go, PHP and Python (stable), TypeScript and Ruby (preview) and Dart (experimental). It has no C++ target, and no plan for one was found.
- **openapi-generator over Microsoft's OpenAPI description** is possible in principle. The description is [msgraph-metadata](https://github.com/microsoftgraph/msgraph-metadata) (MIT): `openapi.yaml` is 44.9 MB for v1.0, and the pre-split module files are `Calendar.yml` (700 KB) and `PersonalContacts.yml` (136 KB). The C++ client generators need cpprestsdk (archived), Qt, Boost or Oat++. Its plain `c` generator (libcurl + cJSON) would fit Mulberry's dependencies, but it produces only the trivial part: data structures and simple calls. Paging, delta, `@removed`, `@odata.type`, the `Prefer` headers, `$batch` and retry on 429 must still be written by hand, and so must the iCalendar/vCard mapping, which is the real work.
- **Community libraries** don't fit. GNOME's [msgraph](https://gitlab.gnome.org/GNOME/msgraph) (LGPL-3.0+, FreeBSD `net/msgraph`) needs GLib, libsoup, json-glib and GNOME Online Accounts, and has no calendar support, no delta and no contact writes. The Microsoft 365 code in evolution-ews (LGPL-2.1+, GLib) and KDE's Graph resource (LGPL, Qt) are internal code, not libraries, and copyleft. They can be studied but not copied into Apache-2.0 sources. The rest is Arduino/ESP32 experiments and security tools.
- **MSAL**, Microsoft's sign-in library, has no public C++ version ([MSAL overview](https://learn.microsoft.com/en-us/entra/identity-platform/msal-overview)). Mulberry doesn't need it: the plain v2.0 authorization-code + PKCE flow is fully documented for public desktop clients, which "must not use secrets" ([auth code flow](https://learn.microsoft.com/en-us/entra/identity-platform/v2-oauth2-auth-code-flow)).

**The official Java SDK can serve as a reference, not as a template to mirror.** It is MIT-licensed (from general knowledge, not re-checked in this research), and MIT code may be translated into Apache-2.0 sources if the notice is kept. It has three layers, of unequal value to Mulberry:

| Layer | Contents | Use for Mulberry |
|---|---|---|
| `msgraph-sdk-java` (generated by Kiota) | thousands of model classes (`Event`, `Contact`, `PatternedRecurrence`…) and fluent request builders | Consult occasionally to check a field. The models come from the same OpenAPI files Mulberry can read directly. Do not reproduce the request builders: Mulberry needs about twenty endpoints, not the whole API |
| `msgraph-sdk-java-core` (hand-written) | retry handler (429/503, `Retry-After`), redirects, `PageIterator`, `BatchRequestContent`, upload sessions | **Port the logic**: this is the Graph know-how, a few hundred lines that fit into a libcurl wrapper |
| `kiota-java` (hand-written) | request adapter, `ParseNode` / `SerializationWriter`, `AdditionalData` | **Copy the design** for a small C++ interface over cJSON. `AdditionalData` keeps unknown JSON fields, so data Graph does not model survives a round trip |

The Go equivalents (`msgraph-sdk-go-core`, `kiota-http-go`, MIT) contain the same logic in more compact code. Microsoft's [msgraph-sdk-design](https://github.com/microsoftgraph/msgraph-sdk-design) repository (MIT) describes in prose the expected behaviour of the retry, redirect and delta handlers and of the page iterator. Reading that spec first and then one implementation is the fastest route.

**Recommended stack:**

- **HTTP:** libcurl, with `CURLOPT_ACCEPT_ENCODING` and system CAs.
- **JSON:** vendored cJSON (MIT).
- **Data structures:** a small build-time Python script that turns the trimmed OpenAPI modules into C++ structs and cJSON (de)serializers. The closure is 42 schemas for `event` and `calendar` and 10 for `contact` and `contactFolder`. This is the approach Thunderbird takes: its Rust crate `ms_graph_tb` (MPL-2.0) generates only the types and paths listed in `supported_types.txt` and `supported_paths.txt`, from a pinned, checksummed `openapi.yaml`.

The REST surface on `https://graph.microsoft.com/v1.0` is small:

- **Bootstrap:** `GET /me` and `GET /me/mailboxSettings`, which gives the user's time zone.
- **Calendars:**
  - `GET /me/calendars`;
  - event sync (next subsection);
  - `GET /me/events/{id}`, the series master with `recurrence`;
  - `…/instances`;
  - create, `PATCH` and `DELETE` of events;
  - `accept`, `tentativelyAccept`, `decline` and `cancel`;
  - optionally `getSchedule` for free/busy.
- **Contacts:**
  - `GET /me/contactFolders`;
  - `…/contactFolders/{id}/contacts/delta` and `/me/contacts/delta`;
  - create, `PATCH` and `DELETE` of contacts;
  - the photo via `…/photo/$value`.
- **Mail, optional:** `POST /me/sendMail` with a base64 MIME body.
- **Unmapped iCalendar/vCard data:** open extensions or `singleValueExtendedProperties`.

The request rules:

- **Headers:**
  - `Authorization: Bearer`.
  - `Prefer: IdType="ImmutableId"` on every request, so IDs survive moves ([immutable IDs](https://learn.microsoft.com/en-us/graph/outlook-immutable-id)).
  - `Prefer: outlook.timezone="…"`, otherwise responses come in UTC.
  - `Prefer: odata.maxpagesize=50`.
  - `Prefer: outlook.body-content-type`.
  - `client-request-id`, for diagnostics.
- **Paging and delta:** follow `@odata.nextLink` verbatim until `@odata.deltaLink`, persist the delta link, and handle `@removed`. When a delta token is rejected, restart with a full resync, as Evolution does.
- **Throttling:**
  - at most 20 requests per `$batch`, each throttled individually ([JSON batching](https://learn.microsoft.com/en-us/graph/json-batching));
  - at most **4 concurrent requests** and 10,000 requests per 10 minutes per app and mailbox ([throttling limits](https://learn.microsoft.com/en-us/graph/throttling-limits));
  - honour `Retry-After` on 429, 503 and 504, falling back to exponential back-off.
- **Authentication:** on 401, refresh the token once and retry.

**Size estimate (judgement):**

- REST core (HTTP wrapper, retry and back-off, paging and delta driver, batch, error parsing): about 1,000–1,500 lines.
- Models and (de)serializers: about 1,000–2,000 lines, or generated.
- The iCalendar/vCard mapping remains the bulk of the work.

This fits the 3–5 person-month estimate for the whole adapter.

**Licences of the reference material:**

- **Reusable:**
  - msgraph-metadata, msgraph-sdk-design, the Java and Go SDK cores, and Kiota's libraries (MIT);
  - openapi-generator templates (Apache-2.0);
  - CLDR `windowsZones.xml` (Unicode licence), needed for the Windows↔IANA time-zone table.
- **Usable with attribution:** the Graph documentation (CC-BY-4.0).
- **Study only:** Thunderbird's `ms_graph_tb` (MPL-2.0), evolution-ews (LGPL-2.1+), KDE's Graph resource (LGPL-2.0+), GNOME msgraph (LGPL-3.0+) and DavMail (GPL-2.0).

### Calendar sync: the windowed view and the undocumented `events/delta`

[Microsoft's delta documentation](https://learn.microsoft.com/en-us/graph/delta-query-events) offers, in v1.0, only `calendarView/delta`. It is limited to a date window, returns expanded occurrences and supports no `$select`. The documentation says an unbounded per-calendar delta "is currently available only in the beta version".

However, `GET /me/calendars/{id}/events/delta` **is present in the v1.0 OpenAPI description**. evolution-ews has used it in production on v1.0 since June 2026 (Evolution 3.62), and Thunderbird's allow-list contains `/users/{user-id}/calendars/{calendar-id}/events/delta()`. It is therefore implemented but undocumented.

Evolution found that Graph clamps its page size to 50 and that larger initial syncs fail with 504. The source comment reads: "Graph clamps 'Prefer: odata.maxpagesize' to 50 for the events delta endpoint … can exceed the gateway timeout … failing every initial sync with 504".

If `events/delta` holds, Mulberry can sync whole calendars, series masters included, instead of a sliding window. It must be tested against a real Microsoft 365 tenant and an Outlook.com account before being relied on, keeping `calendarView/delta` as the documented fallback.

## Eight ways to bolt on OAuth2, and why the in-process loopback flow wins

Eight architectures are available, and the providers have already ruled most of them out. Google recommends the loopback redirect for "macOS, Linux, and Windows desktop apps". It no longer supports custom URI schemes or copy/paste (OOB), and it blocks embedded web views with `disallowed_useragent` ([Google native-app guide](https://developers.google.com/identity/protocols/oauth2/native-app)). Microsoft ignores the port on localhost redirects precisely so that native apps can use ephemeral ports ([Microsoft Learn](https://learn.microsoft.com/en-us/entra/identity-platform/reply-url)). The IETF mail-client profile requires an external user agent and allows `http://127.0.0.1/` redirects ([draft-ietf-mailmaint-oauth-public-06](https://www.ietf.org/archive/id/draft-ietf-mailmaint-oauth-public-06.html)).

| Approach | Security | UX | Effort for Mulberry | Added deps | Provider coverage | Verdict |
|---|---|---|---|---|---|---|
| **1. Auth code + PKCE, system browser, 127.0.0.1 loopback** | RFC 8252 best practice; state + PKCE defeat local port races | Good on a local desktop | Low–medium | none (xdg-open at runtime) | All OAuth providers | **Primary** |
| 2. Private-use URI scheme (`.desktop` + `x-scheme-handler`) | Weaker (any app can claim the scheme) | Depends on the desktop environment honouring xdg-mime | Medium (needs single-instance IPC) | DE integration | Google: unsupported | Only if a provider insists |
| **3. Device grant (RFC 8628)** | Good; risk of device-code phishing | Best for SSH/headless | Low | none | Microsoft, Yandex, Zoho; **not Google mail** | **Fallback** |
| 4. Embedded web view (WebKitGTK/CEF/Qt) | Poor (app sees credentials) | Seamless when it works | High | Very large | Google blocks it | Reject |
| **5. External token command** (mutt/aerc style) | Depends on the helper | Poor for non-experts | Very low | helper tool | Whatever the helper supports | **Escape hatch** |
| 6. Local proxy (email-oauth2-proxy, DavMail) | Plaintext local hop | Transparent | Zero | Python/Java daemon | Broad | Document only |
| 7. GNOME Online Accounts / KAccounts | Good | GNOME/KDE only | Medium (D-Bus) | GLib or Qt stack | Few providers | Reject |
| 8. Cyrus/GNU SASL library | Frames the token only | — | Low but pointless | cyrus-sasl / gnutls | — | Reject |

The other options fail for concrete reasons. Evolution started with an embedded WebKitGTK view and added an external-browser option in 3.46 after Microsoft 365 problems ([evolution-list](https://lists.gnome.org/archives/evolution-list/2022-May/msg00091.html)). Geary depends entirely on GOA and cannot do OAuth without it ([Geary FAQ](https://wiki.gnome.org/Apps/Geary/FAQ)). GOA's generic IMAP provider contains no OAuth code ([GOA backends](https://gitlab.gnome.org/GNOME/gnome-online-accounts/-/tree/master/src/goabackend)). The GNU SASL manual lists neither XOAUTH2 nor OAUTHBEARER ([GNU SASL manual](https://www.gnu.org/software/gsasl/manual/gsasl.html)). Yahoo is the one large provider where a private-use scheme appears, and Yahoo also controls which redirects are allowed. Approach 2 is therefore relevant only if Yahoo ever grants Mulberry access. The token-command mode copies mutt's design and costs a few dozen lines: run a configured helper and read one line containing an access token ([mutt_oauth2.py README](https://gitlab.com/muttmua/mutt/-/raw/master/contrib/mutt_oauth2.py.README)). It is worth keeping. It lets users of pizauth or oama, and users of providers Mulberry has no client ID for, work without any project registration ([pizauth](https://github.com/ltratt/pizauth), [oama](https://github.com/pdobsan/oama)).

The recommended combination has four parts:

- **Loopback flow (1)** as the default for all providers.
- **Device flow (3)** offered automatically where the provider supports it for mail scopes. It covers remote X sessions and SSH, where a local browser cannot reach the remote 127.0.0.1.
- **"Paste the final redirect URL" fallback** for Google in that remote case. email-oauth2-proxy's `--external-auth` mode does this ([email-oauth2-proxy](https://github.com/simonrob/email-oauth2-proxy)).
- **Token command (5)** as an advanced option.

The provider engine underneath should be table-driven in Thunderbird's style: hostname → issuer → endpoints, scopes, PKCE and redirect style, and an optional default client ID. Thunderbird's own source warns not to copy its values ([Thunderbird source](https://raw.githubusercontent.com/mozilla/releases-comm-central/master/mailnews/base/src/OAuth2Providers.sys.mjs)). Each account can override the client ID and secret, as Claws, Alpine and Evolution allow ([EDS Yahoo service](https://gitlab.gnome.org/GNOME/evolution-data-server/-/blob/master/src/libedataserver/e-oauth2-service-yahoo.c)).

The engine also needs a generic path: RFC 8414 discovery followed by RFC 7591 dynamic registration. That path is what makes it work "for any hoster". It covers Fastmail today, and it covers any server that adopts the IETF profile or answers a bogus OAUTHBEARER with an `openid-configuration` URL. Evolution-data-server added exactly such an `EOAuth2ServiceDynamic` in October 2026, targeted at 3.64 ([EDS source](https://gitlab.gnome.org/GNOME/evolution-data-server/-/blob/master/src/libedataserver/e-oauth2-service-dynamic.c)). Thunderbird's ISPDB `<oAuth2>` blocks add endpoints and scopes for Yahoo, AOL, Mail.ru, Yandex and Comcast ([ISPDB yandex.ru](https://autoconfig.thunderbird.net/v1.1/yandex.ru)). They still require a compiled-in client ID.

Two SASL rules hold for every provider:

- **Implement both mechanisms.** Prefer OAUTHBEARER when the server advertises it and fall back to XOAUTH2. Microsoft, Yandex, Mail.ru, Zoho and iCloud accept only XOAUTH2, while the IETF drafts push servers towards OAUTHBEARER ([draft-ietf-mailmaint-pacc-01](https://www.ietf.org/archive/id/draft-ietf-mailmaint-pacc-01.html)).
- **Handle the failure challenge.** On failure the server sends a base64 JSON challenge. The client must answer it with an empty line for XOAUTH2, or `%x01` for OAUTHBEARER, before the tagged `NO` arrives ([RFC 7628](https://www.rfc-editor.org/rfc/rfc7628.txt), [Google XOAUTH2](https://developers.google.com/workspace/gmail/imap/xoauth2-protocol)).

## The dependency bill can be zero new packages

No maintained, lightweight C or C++ OAuth2 client library fits a desktop mail client. **liboauth2** is aimed at servers and web-server plugins, and it pulls in OpenSSL, libcurl, jansson, cjose and pcre2 across 27k lines ([OpenIDC/liboauth2](https://github.com/OpenIDC/liboauth2)). **liboauth** implements OAuth 1.0. **QtNetworkAuth** drags in Qt (qt6-base alone is 65.9 MiB installed), and GOA drags in GLib and D-Bus ([Qt docs](https://doc.qt.io/qt-6/qtnetworkauth-index.html), [FreeBSD ports](https://cgit.freebsd.org/ports/tree/net/qt6-networkauth/Makefile)). The comparable mail clients write their own. Claws Mail's `oauth2.c` is 907 lines. Alpine 2.26 has about 2,600 core lines, much of them its own HTTP client and JSON parser ([Claws oauth2.c](https://git.claws-mail.org/?p=claws.git;a=blob;f=src/oauth2.c;hb=HEAD), [Alpine source](https://alpineapp.email/alpine/release/src/alpine-2.26.tar.xz)). Claws also shows the main risk of hand-rolled HTTP: its token request sets `ssl_cert_auto_accept = TRUE`, so certificate problems on the token endpoint are accepted silently ([Claws oauth2.c](https://git.claws-mail.org/?p=claws.git;a=blob;f=src/oauth2.c;hb=HEAD)).

Mulberry already has almost every building block. The table gives the minimal choice for each need, alongside the alternatives that were rejected.

| Need | Recommended | Size / cost | Rejected alternatives (why) |
|---|---|---|---|
| HTTPS for token, device and Graph endpoints | **system libcurl** (8.22 with base OpenSSL 3.5.6 on the host), easy interface, default peer verification | libcurl.so 1.0 MB, already linked ([curl docs](https://curl.se/libcurl/c/CURLOPT_XOAUTH2_BEARER.html)) | cpp-httplib (25.6k-line header, about 474 KB `.text`, redundant); Boost.Beast (needs Boost); own HTTP over `CTLSSocket` (more code, easy to get TLS wrong) |
| Loopback listener | **own BSD-socket code**, about 150–300 lines | libc only | civetweb (25k lines); mongoose (GPLv2/commercial, incompatible with Apache-2.0); libmicrohttpd (LGPL, pulls GnuTLS on FreeBSD) ([ports](https://cgit.freebsd.org/ports/tree/www/libmicrohttpd/Makefile), [mongoose licence](https://github.com/cesanta/mongoose/blob/master/LICENSE)) |
| JSON, OAuth only | **jsmn** vendored (MIT, 471 lines, 1.6 KB) | none | regexes as in Claws (fragile) |
| JSON, once Graph arrives | **cJSON** vendored (MIT, 3.5k lines, 15.9 KB) ([cJSON](https://github.com/DaveGamble/cJSON)) | none | nlohmann/json (49k-line header, 119 KB for trivial use); yyjson (107–239 KB, only if speed matters); RapidJSON (no release since 2016) |
| PKCE, state, randomness | **libsodium** `randombytes_buf` + `crypto_hash_sha256`, or OpenSSL `RAND_bytes`/`EVP_Digest`; base64url as a 15-line transform of the existing `base64_encode` | about 40 lines | — |
| Browser launch | existing `CURLHelpersMap::LaunchURL` → `xdg-open`, or `posix_spawnp` with an argv array; `$BROWSER` fallback; show URL + copy | xdg-utils 315 KiB, runtime-only, optional ([ports](https://cgit.freebsd.org/ports/tree/devel/xdg-utils/Makefile)) | — |
| Refresh-token storage | existing **libsodium keyring** (`CPasswordManagerKeyring`: Argon2id + XSalsa20-Poly1305) | 50–100 lines of API extension | libsecret (GLib 106 MiB, libgcrypt, a D-Bus session and a Secret Service daemon); KWallet/QtKeychain (Qt) ([ports](https://cgit.freebsd.org/ports/tree/security/libsecret/Makefile)) |
| ID token (optional, for account verification) | base64url-decode the middle segment + the same JSON parser; TLS is enough to validate the issuer per OIDC §3.1.3.7 | about 30 lines ([OIDC Core](https://openid.net/specs/openid-connect-core-1_0.html#IDTokenValidation)) | jwt-cpp, libjwt (pull picojson/jansson) |

The resulting footprint is **one vendored C file plus header and a few thousand lines of Mulberry's own code**, with no new FreeBSD or Linux package at link time. libcurl needs one change. Today `configure.in` checks for it only inside the HTML-viewer block, and only `libHTMLView` links `-lcurl` ([configure.in:528-537](../configure.in), [Libraries/HTMLView/src/Fetcher.cpp](../Libraries/HTMLView/src/Fetcher.cpp)). It must become a general requirement, otherwise `--disable-html-viewer` builds would lose OAuth. One consequence is acceptable: libcurl validates the identity providers' certificates against the system CA store rather than through Mulberry's own accept-certificate UI. For Google and Microsoft endpoints that is arguably the safer behaviour. libcurl even implements SASL XOAUTH2/OAUTHBEARER ([curl docs](https://curl.se/libcurl/c/CURLOPT_XOAUTH2_BEARER.html)), but that feature is irrelevant here because Mulberry has its own IMAP/SMTP stacks.

## A built-in authenticator type touches about ten call sites

OAuth2 belongs in the core as a fourth authenticator kind, not in the dlopen plugin system. The plugin ABI passes only a user ID and a password into fixed `char[256]` buffers, and `SetPassword` truncates at 255 bytes ([CAuthPluginDLL.h](../Plug-ins/Common/CAuthPluginDLL.h), [CAuthPluginDLL.cpp:170-173](../Plug-ins/Common/CAuthPluginDLL.cpp)). Google alone allows access tokens of up to **2,048 bytes** ([Google: Using OAuth 2.0](https://developers.google.com/identity/protocols/oauth2)). Plugins are also never consulted for HTTP, so CalDAV and CardDAV could not use them. The authenticator wrapper already distinguishes `eNone`, `ePlainText`, `eSSL` and `ePlugin`, and handles PLAIN and EXTERNAL in-process ([CAuthenticator.h](../Sources_Common/Application/Preferences/CAuthenticator.h)). Adding `eOAuth2` with a letter class `CAuthenticatorOAuth2` follows the existing pattern. That class holds the email address/UID, provider ID, optional client ID/secret override and the granted scopes, but never the refresh token.

The class should **derive from `CAuthenticatorUserPswd`**. `CINETAccount::GetAuthenticatorUserPswd()` casts the letter blindly in many places, for example in SMTP BURL/URLAUTH ([CINETAccount.h:138-141](../Sources_Common/Application/Preferences/CINETAccount.h), [CSMTPSender.cpp:228-233](../Sources_Common/Mail/INET_Clients/SMTP/CSMTPSender.cpp)), and deriving keeps those casts safe.

| Area | Where | Change | Approx. lines |
|---|---|---|---|
| Authenticator model | `CAuthenticator.h/.cpp` (enum, descriptor table, `SetAuthenticatorType`, copy/assign/compare `dynamic_cast` chains, `GetInfo/SetInfo`) | add `eOAuth2`, `CAuthenticatorOAuth2`; serialise only non-secret fields via `CFutureItems`-compatible s-expressions | ~200 |
| OAuth2 engine (new) | `Sources_Common/OAuth2/` | provider table, PKCE/state, loopback listener, device flow, libcurl token client, JSON, per-account token cache with mutex and expiry, discovery/DCR | ~900–1,300 |
| SASL (IMAP, POP3, Sieve) | `CINETClient::DoAuthentication` dispatch; new `DoOAuth2Authentication` modelled on `DoExternalAuthentication` ([CINETClient.cpp:967-1052, 1249-1286](../Sources_Common/Mail/INET_Clients/CINETClient.cpp)); capability flags in `CIMAPClient`, `CPOP3Client` (parse the CAPA `SASL` line), `CSIEVEClient` | XOAUTH2/OAUTHBEARER payloads, error-challenge handling | ~190 |
| SMTP | `CSMTPSender::SMTPAuthenticate` and the `GetSASLID()`-vs-`mAUTHTypes` check ([CSMTPSender.cpp:674-715, 1914-2070](../Sources_Common/Mail/INET_Clients/SMTP/CSMTPSender.cpp)) | new case; choose the mechanism dynamically; write the payload directly to the stream | ~80 |
| HTTP (CalDAV, CardDAV, WebDAV prefs) | new `CHTTPAuthorizationBearer` beside Basic/Digest ([CHTTPAuthorization.h](../Sources_Common/HTTP/HTTPClient/CHTTPAuthorization.h)); `eOAuth2` case in the three duplicated `GetAuthorization()` ([CWebDAVCalendarClient.cpp:1935-2016](../Sources_Common/Calendar_Store/Clients/CWebDAVCalendarClient.cpp), [CWebDAVVCardClient.cpp](../Sources_Common/VCard_Store/Clients/CWebDAVVCardClient.cpp), [CWebDAVPrefsClient.cpp](../Sources_Common/Preferences_Store/Clients/CWebDAVPrefsClient.cpp)) | Bearer header; on 401, refresh once | ~105 |
| Prompt/retry | `CMailControl::PromptUser` ([CMailControl.cpp:944-970](../Sources_Common/Mail/Control/CMailControl.cpp)); a `CTaskSync` like `CUserPswdTask` for the browser wait | OAuth2 branch: get a token before SASL; on failure refresh → retry → interactive flow | ~160 + dialog |
| Secret storage | `CPasswordManager`/`CPasswordManagerKeyring` ([CPasswordManagerKeyring.cpp](../Sources_Common/Mail/Control/CPasswordManagerKeyring.cpp)) | store refresh tokens under a distinct key (e.g. `oauth2:`+issuer+user); add a "create keyring" path | ~80–150 |
| Logging | WebDAV `DoRequest` header logging ([CWebDAVCalendarClient.cpp:2018-2030](../Sources_Common/Calendar_Store/Clients/CWebDAVCalendarClient.cpp)); SASL send paths | redact `Authorization:`; never log tokens even with "Show Authentication Details" | ~20 |
| GUI (JX now, FLTK later) | `CPrefsAccountAuth::BuildAuthPopup/SetAuthPanel/UpdateData` ([CPrefsAccountAuth.cpp:200-321](../Linux/Sources/Application/Preferences_Dialog/Sub-panels/Account_Panels/Authentication_Panels/CPrefsAccountAuth.cpp)) | one "OAuth2" popup item; reuse the User ID card or add a small card (provider popup + optional client ID); one sign-in status dialog (URL, device code, Copy, Cancel) | ~50–150 + ~100 |
| Build | `configure.in`, `Linux/Makefile.in`, `Linux/Make.files` | make libcurl a general dependency; add sources | small |

The code reading found several traps that would break a naive implementation:

- **The SASL error path.** `INETProcess()` treats a `+` continuation as success ([CINETClient.cpp:1748-1830](../Sources_Common/Mail/INET_Clients/CINETClient.cpp)). A failed XOAUTH2 attempt returns `+ <base64 JSON>`, so it would look like progress unless the new code checks for the continuation, sends the empty or `\x01` response and processes again.
- **SMTP buffer size.** SMTP's line buffer is **1,024 bytes** against 8,192 for the other protocols ([CSMTPSender.cpp:67](../Sources_Common/Mail/INET_Clients/SMTP/CSMTPSender.cpp), [CINETCommon.h:62](../Sources_Common/Mail/INET_Clients/CINETCommon.h)). Base64 of a 2 KB token exceeds it, so the payload must bypass `mLineData`.
- **POP3 initial response.** RFC 5034 caps a POP3 initial response at 255 octets, so POP3 must use the continuation form.
- **Only one auth attempt today.** `PromptUser` currently gives non-password authenticators exactly one attempt. OAuth2 needs a second round after a forced refresh.
- **Refresh tokens must stay out of the prefs file.** Saved passwords there are only obfuscated with the fixed key "Modified-UTF7", and prefs can be uploaded to remote WebDAV/IMSP/ACAP storage ([CAuthenticator.cpp:299-349](../Sources_Common/Application/Preferences/CAuthenticator.cpp), [cdstring.cpp:1599-1635](../Sources_Common/Utilities/cdstring.cpp)).
- **The keyring may not exist.** It is activated only if the file already exists, and every Get/Add re-runs Argon2 and rewrites the whole file ([CMulberryApp.cpp:684-697](../Linux/Sources/Application/General/CMulberryApp.cpp)). Access tokens should therefore stay in memory, the keyring should be written only when a refresh token actually changes, and Mulberry needs a first-use "create keyring" prompt.
- **Token leak in HTTP logs.** The WebDAV request logger writes full headers, `Authorization:` included, whenever HTTP logging is on. It already leaks Basic credentials today and would leak Bearer tokens too.
- **Parallel refreshes.** Cloned IMAP connections, SMTP and Sieve authenticate in parallel. The token cache needs a per-account mutex so that Microsoft's rotating refresh token is not raced and Google's 100-token budget is not burned.

None of this touches the mailbox, message or address-book UIs. The visible change is confined to the account authentication tab and one modal dialog. Provider knowledge lives in `Sources_Common`, so the FLTK rewrite only needs to reproduce a popup, an optional text field and that dialog.

## How Evolution and KMail reach Gmail

Two mature open-source Linux clients show how the Google problem is handled in practice. The Evolution findings come from evolution-data-server (EDS) master at `1b604c1d` (2026-11-24; latest release 3.62.1, 2026-10-09) and GNOME Online Accounts (GOA) master at `fa76dfad` (latest release 3.58.1). Statements marked *(inference)* are not confirmed by a source.

### Evolution (GNOME)

**Two separate paths.** Evolution reaches Google in two ways, and they never mix: EDS's own OAuth2 service refuses sources marked as external ([e-oauth2-service.c#L175](https://gitlab.gnome.org/GNOME/evolution-data-server/-/blob/1b604c1d/src/libedataserver/e-oauth2-service.c#L175)).

- **Built-in.** The account uses the authentication type "OAuth2 (Google)", and EDS itself runs the whole flow ([e-oauth2-service-google.c#L119-131](https://gitlab.gnome.org/GNOME/evolution-data-server/-/blob/1b604c1d/src/libedataserver/e-oauth2-service-google.c#L119-131)):
  - the sign-in dialog, in `libedataserverui`;
  - the token exchange and the refreshes, inside the EDS processes.

  The Google collection backend points IMAP at `imap.googlemail.com:993` and SMTP at `smtp.googlemail.com:465`, both on implicit TLS ([module-google-backend.c#L22-37](https://gitlab.gnome.org/GNOME/evolution-data-server/-/blob/1b604c1d/src/modules/google-backend/module-google-backend.c#L22-37)).
- **Through GNOME Online Accounts.** The `goa-daemon` owns the flow and the tokens. EDS only asks it over D-Bus for the current access token ([module-gnome-online-accounts.c#L1723-1853](https://gitlab.gnome.org/GNOME/evolution-data-server/-/blob/1b604c1d/src/modules/gnome-online-accounts/module-gnome-online-accounts.c#L1723-1853)).

**Two separate Google clients.**

- **EDS** compiles in its own client ID and secret, set by the CMake options `WITH_GOOGLE_CLIENT_ID` and `WITH_GOOGLE_CLIENT_SECRET` ([CMakeLists.txt#L462-471](https://gitlab.gnome.org/GNOME/evolution-data-server/-/blob/1b604c1d/CMakeLists.txt#L462-471)).
  - Google's consent and error pages name this client "GNOME Evolution" ([Debian #1025729](https://bugs.debian.org/1025729)).
  - Since 3.45.1 (2022) the values are stored lightly obfuscated: base64 plus XOR, with the commit message "At least do not store them in the clear form". The obfuscation does not hide them.
  - No major distribution replaces the default: Fedora, Debian, Ubuntu, Arch, Gentoo and FreeBSD `databases/evolution-data-server` 3.56.2 all keep it.
  - Users can override the pair with the GSettings keys `oauth2-google-client-id` and `oauth2-google-client-secret`, available since 3.32, 2019 ([gschema](https://gitlab.gnome.org/GNOME/evolution-data-server/-/blob/1b604c1d/data/org.gnome.evolution-data-server.gschema.xml.in#L55-64)). The override is global, not per account, and needs a restart.
  - *(inference)* The browser hand-back handler is registered at build time for the built-in client ID only. With a user-supplied ID, the user therefore has to paste the final URL back.
- **GOA** uses a separate client owned by the GNOME Foundation. Its secret is in plain text in [meson_options.txt#L7-10](https://gitlab.gnome.org/GNOME/gnome-online-accounts/-/blob/fa76dfad/meson_options.txt#L7-10).
  - GOA 3.54.0 (March 2025) dropped the secret. Google then answered "client_secret is missing", and the secret was restored two days later in 3.54.1 ([GOA#411](https://gitlab.gnome.org/GNOME/gnome-online-accounts/-/issues/411)). The client therefore still needs its secret, even though it uses PKCE.
  - GNOME's stated position is that the key "has only ever been intended for use by GNOME applications" ([desktop-devel-list, 2019](https://lists.gnome.org/archives/desktop-devel-list/2019-February/msg00019.html)) and "is necessarily public, since it's open source" ([LWN](https://lwn.net/Articles/779620)).

**Verification history.** GOA's verification is publicly documented ([GOA#42](https://gitlab.gnome.org/GNOME/gnome-online-accounts/-/work_items/42)):

- **February 2019.** The GNOME Foundation applied. Google pushed for `gmail.modify` instead of `https://mail.google.com/`; GNOME argued that IMAP needs the full scope. Google also asked for a demonstration video with the client ID visible in the URL bar.
- **November 2019.** The project reached 100% of its user cap, and new users saw "Sign in with Google temporarily disabled for this app".
- **December 2019.** The thread reports "GNOME's Google API key should be verified now".

Since then, GNOME has dropped Google Documents, Photos and, in GOA 3.58.0 (March 2026), Drive, but **never mail**: GOA master still requests `https://mail.google.com/`. Two points remain open:

- No public record was found of verification for Evolution's own client. *(inference)* No reports of "unverified app" warnings or 100-user blocks were found for it in 2019–2026 either, which suggests it is verified or otherwise allowed.
- No evidence was found that either project pays for, or goes through, an annual CASA assessment.

**Flow mechanics.**

- **Scopes.** One consent covers `https://mail.google.com/`, `…/auth/calendar`, `…/auth/carddav`, `…/auth/tasks` and the legacy `m8/feeds` ([e-oauth2-service-google.c#L184-199](https://gitlab.gnome.org/GNOME/evolution-data-server/-/blob/1b604c1d/src/libedataserver/e-oauth2-service-google.c#L184-199)). The request also carries `login_hint`.
- **Redirect.** Google withdrew the copy-paste (OOB) redirect, and since 2022 (3.44.2/3.45.1) EDS uses a private scheme built from the reversed client ID, `com.googleusercontent.apps.<id>:/oauth2redirect` ([e-oauth2-service-google.c#L133-177](https://gitlab.gnome.org/GNOME/evolution-data-server/-/blob/1b604c1d/src/libedataserver/e-oauth2-service-google.c#L133-177)). GOA uses the same form. It is not a loopback redirect, which is what Google now recommends for desktop apps. It is unknown whether Google would accept this scheme for a newly created Desktop client.
- **Browser.** The default is an embedded WebKitGTK window. It still works for Google despite Google's ban on embedded browsers, but hangs with security keys and passkeys ([evo#2870](https://gitlab.gnome.org/GNOME/evolution/-/issues/2870), [evo#3083](https://gitlab.gnome.org/GNOME/evolution/-/issues/3083)).
  - Since 3.45.1 the dialog offers "Open in Browser" and "Copy URL", followed by a manual paste of the code or the final URL.
  - Since 3.51.1 a desktop file registered for the scheme returns the redirect over D-Bus.
  - GOA removed its WebView entirely in GNOME 46 (2024).
  - *(inference)* FreeBSD's default port options build EDS's GTK3 UI library without WebKitGTK, so FreeBSD users always get the external-browser or paste flow.
- **PKCE and state.** EDS added PKCE for Google only on 2026-11-04 (master, not yet released; expected in 3.64, March 2027), and `state` validation in October 2026. GOA has used PKCE since 2024.

**Token storage and refresh.**

- **Storage.** Tokens go into libsecret, using the schema `org.gnome.Evolution.Data.Source`, one item per account labelled `Evolution Data Source - Google[<user>]` ([e-secret-store.c#L242-256](https://gitlab.gnome.org/GNOME/evolution-data-server/-/blob/1b604c1d/src/libedataserver/e-secret-store.c#L242-256)). The item holds JSON with `refresh_token`, `access_token` and `expires_after`.
- **Refresh.** The access token is refreshed 10 seconds before it expires. Refreshes are serialised inside a process and across processes, the latter with a lock file `$TMPDIR/.evo-oauth2-<uid>.lock`. A new refresh token replaces the stored one, and a failed refresh reopens the consent window.

**Protocols.**

- **IMAP and SMTP:** XOAUTH2 only. Camel has no OAUTHBEARER mechanism.
- **Calendar:** CalDAV at `apidata.googleusercontent.com/caldav/v2/` with a Bearer header.
- **Contacts:** CardDAV at `www.googleapis.com/.well-known/carddav`, since 3.41.1 (2021).
- **Tasks:** the Google Tasks REST API.

**Problems users reported in 2024–2026.**

- **Keyring missing or locked.** Sign-in then hangs, or reports "OAuth2 secret not found" ([evo#3137](https://gitlab.gnome.org/GNOME/evolution/-/issues/3137), [evo#3196](https://gitlab.gnome.org/GNOME/evolution/-/issues/3196)).
- **"Token has been expired or revoked".** The workaround is to delete the keyring item and sign in again ([GNOME Discourse t/30588](https://discourse.gnome.org/t/30588)).
- **Security keys in the embedded window.** The workaround is to use the external browser; in Flatpak builds the user must paste the URL.
- **Broken or missing scheme handlers.** Reported in Flatpak, with `NoDisplay`, and in GOA ([GOA#316](https://gitlab.gnome.org/GNOME/gnome-online-accounts/-/issues/316), [GOA#430](https://gitlab.gnome.org/GNOME/gnome-online-accounts/-/issues/430)).

**What Mulberry takes from Evolution.**

- **Adopt:**
  - a user override of the client ID, but per account rather than global;
  - a refresh lock that works across processes;
  - replacing the stored refresh token whenever a new one arrives;
  - a copy-URL and paste fallback.
- **Avoid:**
  - a private-scheme redirect, which depends on desktop-file, D-Bus and Flatpak plumbing that keeps breaking; use loopback with PKCE, as Google recommends;
  - an embedded browser;
  - depending on a desktop keyring that may be locked or absent;
  - obfuscating the secret, which protects nothing.

### KMail (KDE PIM)

These findings come from invent.kde.org `master` as of 2026-11-24 (PIM 6.9.40, the KDE Gear 26.12 cycle). The released version is KDE Gear 26.08.x; FreeBSD ships 26.08.3. Statements marked *(inference)* are not confirmed by a source.

**Paths into KMail.** OAuth2 is chosen by hostname suffix, not by a provider table.

- **IMAP.** `GmailPasswordRequester` handles servers under `.gmail.com` and `.googlemail.com`, and `OutlookPasswordRequester` handles `.outlook.com`, `.office365.com` and `.hotmail.com` ([passwordrequester.cpp](https://invent.kde.org/pim/kdepim-runtime/-/blob/master/resources/imap/passwordrequester.cpp)). Any other host silently falls back to the stored password, even when XOAUTH2 is selected.
- **SMTP.** KMailTransport uses the same suffix switch ([xoauthpasswordrequester.cpp](https://invent.kde.org/pim/kmailtransport/-/blob/master/src/plugins/smtp/xoauthpasswordrequester.cpp)).
- **History.** Gmail IMAP via OAuth2 arrived in KDE Applications 17.04 (2017) and Gmail SMTP in 18.08. Microsoft 365 and Outlook.com IMAP/SMTP followed in KDE Gear 24.05, partly written by g10 Code ([changelog 24.05.0](https://kde.org/announcements/changelogs/gear/24.05.0/)).
- **Not covered.** There is no OAuth2 for POP3 ([bug 455153](https://bugs.kde.org/show_bug.cgi?id=455153)) and none for Yahoo, Yandex, Fastmail or any generic provider ([bug 457233](https://bugs.kde.org/show_bug.cgi?id=457233), "Please provide oauth2 login for IMAP – not just Gmail").
- **No Online Accounts.** KMail does not use KDE's KAccounts: "KMail has its own account system not connected to this" ([bug 480779](https://bugs.kde.org/show_bug.cgi?id=480779)).
- **Account wizard.** It reads Thunderbird's ISPDB and maps its `OAuth2` entries to XOAUTH2. It creates the Google calendar and contacts resource only for addresses ending in `gmail.com`, so Workspace domains get no groupware ([bug 527957](https://bugs.kde.org/show_bug.cgi?id=527957)).

**Mechanism.**

- **Token traffic.** libkgapi carries all Google token traffic, running inside the Akonadi IMAP resource process and the SMTP transport plugin.
- **SASL.** kimap and ksmtp authenticate through **Cyrus SASL**. XOAUTH2 itself is a Cyrus SASL plugin that libkgapi installs as `libkdexoauth2.so` in the `sasl2` directory ([libkgapi saslplugin](https://invent.kde.org/pim/libkgapi/-/tree/master/src/saslplugin)); it receives the access token in place of the password. The commit that introduced it says: "The XOAUTH2 support is implemented in a custom SASL plugin. The token request/refresh is done via KGAPI."
- **Google's error challenge.** ksmtp answers it with the required empty response and marks the token as expired ([ksmtp loginjob.cpp](https://invent.kde.org/pim/ksmtp/-/blob/master/src/loginjob.cpp)). On that mark, or on Microsoft's `5.7.3`, the SMTP job forces a refresh and retries.
- **Calendar and contacts.** For Google these use **REST, not DAV**: libkgapi's Calendar v3, People and Tasks APIs. KDE's DAV resource has no OAuth and no Google preset.

**Client IDs and verification history.**

- **The Gmail client.** The client is named "Akonadi Resources for Google Services" in Google's consent screen. Its ID and secret are hard-coded as `#define` literals in three places:
  - [kdepim-runtime IMAP](https://invent.kde.org/pim/kdepim-runtime/-/blob/master/resources/imap/gmailpasswordrequester.cpp)
  - [kmailtransport SMTP](https://invent.kde.org/pim/kmailtransport/-/blob/master/src/plugins/smtp/gmailpasswordrequester.cpp)
  - [Google groupware settings](https://invent.kde.org/pim/kdepim-runtime/-/blob/master/resources/google-groupware/googlesettings.cpp)
- **No override.** There is no build option and no runtime setting; users can supply their own ID only by patching the source. Making it configurable was proposed in 2019 and never done ([bug 404990](https://bugs.kde.org/show_bug.cgi?id=404990)). Debian and FreeBSD (`net/libkgapi`, `deskutils/kdepim-runtime`) ship the upstream values unchanged.
- **KAccounts' client.** KAccounts (Plasma and Dolphin, not KMail) uses a second KDE Google client with an embedded browser ([google.provider.in](https://invent.kde.org/network/kaccounts-providers/-/blob/master/providers/google.provider.in)).
- **Verification timeline:**
  - **2019:** Google blocked new sign-ins: "Sign in with Google temporarily disabled for this app … has not yet been verified". The maintainer explained: "they found out we do not comply with their requirement, sent us a warning, but I did not have time and energy to act on it, so they blocked new logins" ([bug 404990](https://bugs.kde.org/show_bug.cgi?id=404990)). The KDE e.V. board holds the Google Cloud console keys.
  - **January 2020:** calendar and contacts were re-approved; "GMail is not yet fixed" ([euroquis.nl](https://euroquis.nl/kde/2020/01/13/google.html)).
  - **2020-05-12:** "Google has approved KMail access to Gmail via Google Sign-in today" ([bug 404990](https://bugs.kde.org/show_bug.cgi?id=404990)).
  - **February 2024:** Google wrote to the KDE e.V. board about kio-gdrive's restricted Drive scope. Re-verification required a video and "An annual security assessment"; without it, the app faced the "ongoing 100 user limit" ([kde-devel](https://mail.kde.org/pipermail/kde-devel/2024-February/002420.html), [kio-gdrive#1](https://invent.kde.org/network/kio-gdrive/-/issues/1)).
  - **June 2024:** Drive access was blocked: "Google blocked us from using this back in June because we weren't able justify our API usage to their satisfaction". The KAccounts commit removing it is dated 2025-01 ([bug 500335](https://bugs.kde.org/show_bug.cgi?id=500335)). A KDE developer commented: "No one is in charge of managing this; KDE is not a company…" ([bug 480779](https://bugs.kde.org/show_bug.cgi?id=480779)).
  - **2025–2026:** no evidence was found of Gmail blocks or unverified-app screens for the Akonadi client. *(inference)* It still appears to be approved for `https://mail.google.com/`. No KDE statement on Gmail re-verification, CASA, or who pays was found.

**Flow mechanics.**

- **Google (libkgapi).** Since KDE Apps 20.12 (2020) libkgapi uses the **system browser with a loopback redirect** `http://127.0.0.1:<ephemeral port>` ([fullauthenticationjob.cpp](https://invent.kde.org/pim/libkgapi/-/blob/master/src/core/private/fullauthenticationjob.cpp)). Google's 2022 OOB shutdown therefore did not affect it. The commit message gives the reason: "Google claiming that the app of the browser is not secure … we can get rid of the awful webengine dependency".
  - The request sends `login_hint` but **no PKCE and no `state`**, and the secret goes in the token POST.
  - Scopes: `https://mail.google.com/` plus `userinfo.email` for mail; calendar, contacts and tasks scopes for the groupware resource.
- **Microsoft IMAP/SMTP (24.05).** The flow uses the system browser and a loopback `http://localhost:<ephemeral>` redirect, with **PKCE S256** ([outlookoauthtokenrequester.cpp](https://invent.kde.org/pim/kmailtransport/-/blob/master/src/outlookoauthtokenrequester.cpp)).
  - It uses the v2.0 endpoints with tenant `common`.
  - It requests the `IMAP.AccessAsUser.All` or `SMTP.Send` scope together with `offline_access`.
  - The client is public, with a KDE-owned app ID; no client or tenant override exists.
  - On `invalid_grant` it starts a new interactive sign-in.
- **EWS resource.** The EWS resource (OAuth2 since 2018) still uses an **embedded QtWebEngine browser**, the v1 endpoint and the OOB redirect, with its own "Akonadi EWS" app ID ([ewsresource.kcfg](https://invent.kde.org/pim/kdepim-runtime/-/blob/master/resources/ews/ewsresource.kcfg), [ewsoauth.cpp](https://invent.kde.org/pim/kdepim-runtime/-/blob/master/resources/ews/ewsclient/auth/ewsoauth.cpp)). It also supports the Microsoft Identity Broker for Intune-managed devices. Organisations blocking the app is a reported issue ([bug 464534](https://bugs.kde.org/show_bug.cgi?id=464534)). Microsoft's EWS retirement for Exchange Online (October 2026 to April 2027) ends it for Microsoft 365.
- **Graph resource.** A new **Microsoft Graph resource** was merged on 2026-08-23 and first ships in Gear 26.12 ([resources/graph](https://invent.kde.org/pim/kdepim-runtime/-/blob/master/resources/graph/README.md)). It covers mail, calendar, contacts and To Do.
  - Auth uses Qt NetworkAuth with automatic PKCE and the loopback `http://localhost:53682/callback`, falling back to an ephemeral port.
  - It uses the v2.0 endpoints with tenant `common`, and asks for `Mail.ReadWrite`, `Mail.Send`, `Calendars.ReadWrite`, `Contacts.ReadWrite` and `Tasks.ReadWrite`.
  - The token is refreshed 5 minutes before expiry.
  - **KDE ships no client ID for it**: "Each user (or organisation) registers their own — free — Azure application".

**Token storage.**

- **Gmail mail.** Tokens (access, refresh, expiry, scopes) are stored by libkgapi in **KWallet** only, in folder `LibKGAPI`, keyed by client ID and then account. IMAP and SMTP share the entry ([accountstorage](https://invent.kde.org/pim/libkgapi/-/blob/master/src/core/accountstorage.cpp)).
  - A refresh failing with `invalid_grant` does not trigger a new browser sign-in. The user must delete the KWallet entry by hand ([bug 520588](https://bugs.kde.org/show_bug.cgi?id=520588)).
- **Google groupware.** A separate QtKeychain entry, "Akonadi Google", holds these tokens. Mail and calendar therefore hold separate refresh tokens for the same client.
- **Outlook.** IMAP and SMTP use QtKeychain under the services `imap` and `mailtransports`.
  - The stored access token is used until the server rejects it, with no expiry tracking.
  - The result was SMTP re-authentication every day ([bug 489610](https://bugs.kde.org/show_bug.cgi?id=489610)) and IMAP stuck "Ready" without syncing after a failed refresh ([bug 525082](https://bugs.kde.org/show_bug.cgi?id=525082)).
- **KWallet removal.** kdepim-runtime removed direct KWallet use in December 2025 (Gear 26.04), but libkgapi still depends on it.

**Problems users reported in 2024–2026.**

- **Plugin not found:** "No worthy mechs found" when the SASL plugin is missing or `SASL_PATH` is wrong ([bug 505428](https://bugs.kde.org/show_bug.cgi?id=505428)).
- **Conflicting plugin:** a different `sasl-xoauth2` Cyrus plugin installed by the distribution took over XOAUTH2 and treated the token as a file path: "File name too long" ([bug 526511](https://bugs.kde.org/show_bug.cgi?id=526511)). *(inference)* FreeBSD's `security/cyrus-sasl2-xoauth2` could conflict the same way.
- **Re-authentication loops:** "Failed to authenticate additional scopes" and `invalid_grant` loops; the workaround is to delete the KWallet entry ([bugs 502433](https://bugs.kde.org/show_bug.cgi?id=502433), [520588](https://bugs.kde.org/show_bug.cgi?id=520588)).
- **Token expiry during sync:** the token expired in the middle of a long folder sync ([bug 526758](https://bugs.kde.org/show_bug.cgi?id=526758)).
- **Fallback:** Google app passwords with PLAIN still work as a fallback.

**What Mulberry takes from KMail.**

- **Adopt:**
  - libkgapi's system-browser and loopback design, which escaped both the OOB shutdown and Google's ban on embedded browsers;
  - PKCE on every flow, as KMail does for Microsoft but not for Google;
  - expiry tracking with proactive refresh, which avoids KMail's Outlook problems.
- **Avoid:**
  - **Cyrus SASL for XOAUTH2.** The plugin path, packaging and conflicting third-party plugins cause real failures. Mulberry's plan to build the SASL payload in its own IMAP/SMTP code is safer.
  - **Hostname-suffix dispatch with a silent password fallback.** A provider table with an explicit error is better.
  - **Separate token stores** for mail and calendar on one account. These double the refresh tokens, which counts against Google's limit of 100 per client.
  - **Hard-coded IDs without override.** KDE's 2019 lockout left users with no workaround.
  - **Any failed refresh that strands the account** instead of falling back to an interactive sign-in.

### Side by side

| | Evolution (EDS) | KMail (KDE PIM) | Plan for Mulberry |
|---|---|---|---|
| Google client | Own "GNOME Evolution" ID, compiled in; GOA has a second (GNOME Foundation) ID | "Akonadi Resources for Google Services", hard-coded | Per-account BYO ID; a project ID only if verified |
| User override of client ID | Global GSettings keys | None (patch the source) | Per account, in the provider table |
| Google verification | GOA documented 2019; EDS client works, no public record | Blocked 2019, approved 2020-05; Drive lost in 2024 | Plan for yearly re-verification if a project ID is shipped |
| Google redirect | Private scheme `com.googleusercontent.apps.<id>:/oauth2redirect` | Loopback `127.0.0.1:<ephemeral>` | Loopback `127.0.0.1:<ephemeral>` |
| Browser | Embedded WebKitGTK by default, external option | System browser | System browser |
| PKCE (Google) | Only in master since 2026-11-04 | No | Yes |
| XOAUTH2 implementation | Inside Camel (Evolution's mail library) | Cyrus SASL plugin `libkdexoauth2.so` | Mulberry's own IMAP/POP/SMTP code |
| OAUTHBEARER | No | No | Yes |
| Google calendar and contacts | CalDAV + CardDAV with Bearer; Tasks via REST | REST: Calendar v3, People, Tasks | CalDAV + CardDAV with Bearer |
| Microsoft IMAP/SMTP | Yes (Outlook.com/M365 service) | Yes since 24.05: PKCE, loopback, KDE app ID | Yes, plus device code |
| Microsoft 365 calendar and contacts | EWS module, Graph-based Microsoft365 module | EWS (ends 2027-04 for Exchange Online); Graph resource from 26.12, users bring their own Azure app | Graph adapter later; DavMail in the meantime |
| Token storage | libsecret (Secret Service) | KWallet (libkgapi), QtKeychain | Mulberry's libsodium keyring |
| Generic or other providers | Dynamic registration in master (3.64) | None | Provider table plus discovery and dynamic registration |

## Client registration, not code, is the critical path

Every provider except Fastmail requires a registered client ID, and the terms differ sharply. **Google** classifies `https://mail.google.com/` as restricted because it "includes any usage of IMAP, SMTP, and POP3" ([Google Cloud Help](https://support.google.com/cloud/answer/13464325?hl=en)). Restricted-scope verification takes about six weeks and must be renewed at least every 12 months. Whether a desktop client that keeps mail only on the user's machine also needs the CASA security assessment by an authorized lab is ambiguous, because Google's own pages disagree:

- **Developer documentation ties CASA to servers.** It says: "If you store or transmit restricted scope data on servers, then you need to complete a security assessment". It also says that apps which can access data "from or through a third-party server must go through a security assessment" ([restricted-scope verification](https://developers.google.com/identity/protocols/oauth2/production-readiness/restricted-scope-verification), updated 2026-11-10; same wording in the [Gmail scopes page](https://developers.google.com/workspace/gmail/api/auth/scopes)). The same page still says apps "must be reverified for compliance and complete a security assessment at least every 12 months".
- **The Cloud Help pages have no server qualifier.** They say: "applications requesting access to restricted scopes must undergo an annual security assessment" and "All applications must be revalidated every year" ([Security Assessment](https://support.google.com/cloud/answer/13465431?hl=en)). The FAQ calls the assessment "additional" to restricted-scope verification ([verification FAQ](https://support.google.com/cloud/answer/13463817?hl=en)).
- **Cost.** Google itself charges nothing, but the developer pays the assessor.
- **In practice Google decides case by case.** In 2024 Google asked KDE for an "annual security assessment" for a desktop Drive client, and KDE lost access when it could not meet the requirements (see the KMail section). GNOME and KDE both hold Gmail-capable clients, and no public evidence was found that either has undergone CASA.

A local-only Mulberry client might therefore need only the yearly verification review, but that cannot be relied on. Published 2026 list prices of about $675–$855 for AL1 and about $5,400 for AL2 come from secondary sources and are unverified ([deepstrike.io](https://deepstrike.io/blog/google-casa-security-assessment-2025)). Without verification, an app has a **lifetime cap of 100 users** that "cannot be reset" ([FAQ](https://support.google.com/cloud/answer/13463817?hl=en)). That is why Alpine, Claws, NeoMutt and aerc make each user create their own Google Cloud "Desktop app" client ([Alpine](https://alpineapp.email/alpine/alpine-info/misc/RegisteringAlpineinGmail.html), [NeoMutt README](https://raw.githubusercontent.com/neomutt/neomutt/main/contrib/oauth2/README.md)). The calendar and CardDAV scopes are not restricted, so a calendar-only Google client would avoid CASA. Google desktop client secrets are "obviously not treated as a secret", so shipping one in open source is normal ([Google](https://developers.google.com/identity/protocols/oauth2)).

**Microsoft** registration is free. Users in other tenants, however, often "can't consent to most newly registered multitenant apps that aren't publisher verified". Publisher verification itself is free but needs a verified Cloud Partner Program account, a work tenant and a custom domain ([Microsoft Learn](https://learn.microsoft.com/en-us/entra/identity-platform/publisher-verification-overview)). Even Thunderbird publishes an admin-consent URL for its client ([Mozilla KB](https://support.mozilla.org/en-US/kb/microsoft-oauth-authentication-and-thunderbird-202)). For the remaining providers:

- **Yahoo/AOL** require a commercial-access application with privacy and security evidence and publish no timeline ([Yahoo](https://senders.yahooinc.com/developer/developer-access)), so app passwords remain the pragmatic Yahoo path.
- **Yandex** and **Mail.ru** are self-service. Yandex can still answer `unauthorized_client` for apps "awaiting moderation" ([Yandex](https://yandex.com/dev/id/doc/en/codes/screen-code-oauth)).
- **Fastmail** needs nothing at all.
- **iCloud** has no public program.

The policy that follows treats ownership and override as separate concerns. Ship project-owned public client IDs for Microsoft (ideally publisher-verified), Yandex, Mail.ru and Zoho. Use dynamic registration for Fastmail and other providers that follow the profile. Ship Google as "bring your own client ID" unless and until someone takes on yearly verification, and pays for CASA if Google requires it. Keep the provider table in a data file, so that the FreeBSD port, a Linux distribution or a Workspace admin can inject their own registered IDs without patching code. Never reuse Thunderbird's IDs: Mozilla's source explicitly says "register one for yourself!" ([Thunderbird source](https://hg-edge.mozilla.org/comm-central/file/tip/mailnews/base/src/OAuth2Providers.sys.mjs)), and the providers can revoke a client that is being misused.

## Effort: six to eight weeks for OAuth2, months for Graph PIM

The line counts in this section come from the codebase analysis. Converting them to person-days is my own judgement, assuming a developer already familiar with Mulberry. Testing against live Gmail (personal and Workspace), Microsoft 365, Outlook.com, Yandex, Mail.ru and Fastmail accounts is budgeted explicitly, because error handling (admin policy, consent, rotation, revoked grants) is where OAuth2 implementations usually fail.

| Work package | New/changed lines | Person-days (judgement) |
|---|---|---|
| OAuth2 engine: provider table, PKCE, loopback listener, device flow, libcurl token client, jsmn/cJSON, token cache and refresh | 900–1,300 | 10–15 |
| `CAuthenticatorOAuth2` + prefs serialisation | ~200 | 2–3 |
| SASL XOAUTH2/OAUTHBEARER for IMAP, POP3, Sieve, SMTP, incl. error continuation | ~270 | 3–5 |
| Bearer for CalDAV/CardDAV/WebDAV prefs + header redaction | ~125 | 1–2 |
| PromptUser/retry policy + sign-in `CTaskSync` dialog | ~260 | 3–4 |
| Keyring storage + create-keyring path | 80–150 | 1–2 |
| Preferences GUI (popup item, optional card) | 50–150 | 1–2 |
| Build changes (libcurl general) | small | 0.5 |
| Live-provider testing and error-message mapping | — | 5–8 |
| **Core total** | **~1,800–2,500** | **~27–42 (≈6–8 weeks)** |
| Optional: RFC 8414/7628 discovery + RFC 7591 DCR (Fastmail, IETF profile) | 300–400 | 4–6 |
| Optional: external token-command mode | ~100 | 1–2 |
| Optional: Graph `sendMail` fallback for SMTP-AUTH-disabled tenants | ~200 | 2–3 |

The core figure is consistent with the reference implementations. Claws needed about 900 lines with no PKCE and with hand-rolled HTTP ([Claws oauth2.c](https://git.claws-mail.org/?p=claws.git;a=blob;f=src/oauth2.c;hb=HEAD)). Alpine needed about 2,600 core lines because it carries its own HTTP and JSON code ([Alpine source](https://alpineapp.email/alpine/release/src/alpine-2.26.tar.xz)). Mulberry avoids both of those by reusing libcurl and a vendored parser.

The Graph calendar and contacts adapter is a different order of magnitude. It would add new `CCalendarClient` and address-book client subclasses beside `CWebDAVCalendarClient`/`CWebDAVVCardClient`, and new server types in `CINETAccount` ([CINETAccount.h:38-59](../Sources_Common/Application/Preferences/CINETAccount.h)). Inside them sit four pieces:

- an OData client with paging, `Prefer` headers, ImmutableId, 429/`Retry-After` handling and a four-request concurrency limiter;
- a calendar delta sync: per-calendar `events/delta` if it proves reliable, otherwise a windowed calendarView delta that reconstructs series from their masters;
- per-folder contacts delta;
- bidirectional event↔VEVENT and contact↔vCard mappers, including a Windows↔IANA time-zone table and iMIP suppression.

Thunderbird reports that Graph mail sync alone took **four months**, and only because it built on a new architecture created for EWS, which had taken eleven ([Thunderbird blog](https://blog.thunderbird.net/2026/09/thunderbird-desktop-new-protocol-support-microsoft-graph-api/)). Graph calendars are still unfinished in Thunderbird. On that basis I estimate **3–5 person-months**, with the recurrence and exception mapping as the main risk.

The sensible phasing has three steps:

1. Ship OAuth2 for mail first. That alone keeps Gmail, Workspace, Microsoft 365 and Outlook.com usable after SMTP basic auth goes dark by default at the turn of 2027.
2. Document DavMail as the interim route to Microsoft 365 calendars and contacts.
3. Build the native Graph PIM adapter as a separate project on the same token engine.

## Conclusion

The research reframes the problem. "Modern authentication" for Mulberry is not one integration per hoster. It is a single, standards-shaped token engine of about two thousand lines, and the provider differences reduce to rows in a table: endpoints, scopes, PKCE, redirect style, device-flow support and mechanism preference. The hard limits are commercial and policy decisions outside the code. Google's yearly restricted-scope verification (possibly with a paid CASA assessment), Yahoo's gatekeeping and Microsoft's consent walls decide which providers can ship with a project-owned client ID. That makes per-account client-ID overrides, the token-command escape hatch and dynamic registration core features, not extras. The IETF open-public-client profile, already live at Fastmail and being adopted by Evolution, is the one development that could eventually remove the registration problem. Building the generic discovery path now positions Mulberry for it.

Microsoft splits cleanly. Mail is a small XOAUTH2 addition to protocols Mulberry already speaks, with no retirement date in sight. Calendars and contacts are a genuine new backend, because Exchange Online offers no CalDAV/CardDAV and Graph's object model is not iCalendar or vCard. Treating these as one "Graph migration" would inflate the mail work by an order of magnitude for no gain. Treating them separately lets the OAuth2 release ship in weeks, while the Graph PIM adapter is scoped, funded and scheduled on its own merits.
