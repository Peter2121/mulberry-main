# Microsoft mailboxes (Microsoft 365 / Exchange Online and Outlook.com) for a third-party IMAP/POP/SMTP client: OAuth2 vs Microsoft Graph, as of 2026-11-24

Scope: how Mulberry (open-source C++ desktop client on FreeBSD/Linux, speaking IMAP/POP3/SMTP and CalDAV/CardDAV) can reach Microsoft mailboxes in late 2026. Covers retirement timelines, Microsoft identity platform details, SASL mechanics, the Graph mail API, CalDAV, and what other open-source clients do.

Bottom line up front: the claim that Office 365 "needs Graph" is **incorrect for mail**. IMAP, POP and SMTP AUTH with OAuth2 (SASL XOAUTH2) are still supported in Exchange Online and Outlook.com as of 2026-11-24, and Microsoft has announced no end date for them. What *does* require Graph (or ActiveSync/MAPI) is **calendar and contacts**, because Exchange Online has no CalDAV/CardDAV. Graph also replaces **EWS**, which Microsoft started switching off in October 2026. Mail clients that used EWS (Thunderbird, Evolution) are the ones moving to Graph, not IMAP clients.

Scope extension (sections 8–9): the coordinator confirmed that Graph is the required route for Microsoft 365 **calendar and contacts**. Sections 8 and 9 cover the Graph Calendar and Contacts APIs in depth: iCalendar/vCard mapping, recurrence, time zones, scopes and tokens, consumer accounts, throttling, reference implementations, and effort.

---

## 1. Basic-auth retirement timeline and current protocol status (IMAP/POP/SMTP, Outlook.com)

### Takeaway
Basic auth for IMAP/POP (and EWS, EAS, etc.) in Exchange Online was turned off from 2022-10-01, and nobody can turn it back on any more. Basic auth for SMTP AUTH client submission still works. Under the revision of 2026-01-27 it becomes **disabled by default for existing tenants at the end of December 2026**, but admins can re-enable it, and the final removal date will be announced in H2 2027. Outlook.com (consumer) dropped basic auth on 2024-09-16. OAuth2 (XOAUTH2) for IMAP, POP and SMTP is supported for both Microsoft 365 and Outlook.com, and no retirement of IMAP/POP itself has been announced.

### Cited Findings
**Exchange Online: IMAP/POP/EWS/EAS etc.**
- "Basic authentication is now disabled in all tenants. Before December 31, 2022, you could re-enable the affected protocols … Now no one (you or Microsoft support) can re-enable Basic authentication in your tenant." — [Microsoft Learn: Deprecation of Basic authentication in Exchange Online (ms.date 2026-07-10)](https://learn.microsoft.com/en-us/exchange/clients-and-mobile-in-exchange-online/deprecation-of-basic-authentication-exchange-online)
- Protocols removed: EAS, POP, IMAP, Remote PowerShell, EWS, OAB, Autodiscover, Outlook for Windows/Mac. The general date was 2022-10-01 (21Vianet: 2023-03-31). Tenants with extensions were disabled in early 2023. — [same page](https://learn.microsoft.com/en-us/exchange/clients-and-mobile-in-exchange-online/deprecation-of-basic-authentication-exchange-online)
- App passwords: "The deprecation of basic authentication also prevents the use of app passwords with apps that don't support two-step verification." — [same page](https://learn.microsoft.com/en-us/exchange/clients-and-mobile-in-exchange-online/deprecation-of-basic-authentication-exchange-online)
- IMAP/POP with OAuth stays supported. "In 2020, we released OAuth 2.0 support for POP, IMAP, and SMTP AUTH … Application developers who have built apps that send, read, or otherwise process email using these protocols will be able to keep the same protocol; however, they need to implement secure, Modern authentication." The page's protocol table says "IMAP is popular for Linux and education customers. OAuth 2.0 support started rolling out in April 2020." The same table recommends "Move away from these protocols as they don't enable full features" and "Move to OAuth 2.0 for POP/IMAP". This is advice, not a retirement notice. — [same page](https://learn.microsoft.com/en-us/exchange/clients-and-mobile-in-exchange-online/deprecation-of-basic-authentication-exchange-online)
- "There's no plan for Outlook clients to support OAuth for POP and IMAP" (Microsoft's own Outlook uses MAPI/HTTP or EWS). — [same page](https://learn.microsoft.com/en-us/exchange/clients-and-mobile-in-exchange-online/deprecation-of-basic-authentication-exchange-online)

**Exchange Online: SMTP AUTH (client submission)**
- History of the dates: the original target was September 2025. A June 2025 plan then had rejections starting 2026-03-01 and reaching 100% on 2026-04-30. That plan was dropped in January 2026. — [Office365ITPros, 2026-01-29](https://office365itpros.com/2026/01/29/smtp-auth-basic-retirement/)
- Current official plan (MC786329 v4, updated 2026-01-27): "Now through December 2026: Basic Auth behavior is unchanged." At the end of December 2026, "SMTP AUTH Basic Authentication will be disabled by default for existing tenants", and admins can still re-enable it. New tenants created after December 2026 get no Basic Auth by default, and OAuth is the supported method. Microsoft will announce the final removal date in the second half of 2027. — [MC786329 mirror (mc.merill.net)](https://mc.merill.net/message/MC786329); also [Office365ITPros](https://office365itpros.com/2026/01/29/smtp-auth-basic-retirement/)
- The Learn basic-auth page now points to the updated timeline post ("updated-exchange-online-smtp-auth-basic-authentication-deprecation-timeline/4489835"). It also lists Graph `sendMail` as an alternative for authenticated sending. — [Microsoft Learn](https://learn.microsoft.com/en-us/exchange/clients-and-mobile-in-exchange-online/deprecation-of-basic-authentication-exchange-online)
- When basic auth is blocked, the server reportedly returns `550 5.7.30 Basic authentication is not supported for Client Submission`. — secondary sources aggregated in search results (e.g. [captaindns.com](https://www.captaindns.com/en/blog/microsoft-exchange-basic-auth-smtp-end)). I did not verify this against a Microsoft page.
- Observed on 2026-11-24 by direct anonymous EHLO (no credentials sent): `smtp.office365.com:587` and `smtp-mail.outlook.com:587` both advertise `AUTH LOGIN XOAUTH2` after STARTTLS, with `SIZE 157286400` (150 MiB), `8BITMIME`, `BINARYMIME`, `CHUNKING`, `SMTPUTF8`, `DSN`, `PIPELINING` and `ENHANCEDSTATUSCODES`. `AUTH LOGIN` is still present, which fits basic SMTP auth still being available until end-2026. — direct protocol observation (researcher's own `openssl s_client` test). No source URL.

**SMTP AUTH and IMAP/POP enablement in tenants**
- Microsoft "highly recommend[s] that you disable SMTP AUTH in your Exchange Online organization, and enable it only for the accounts (mailboxes) that still require it". There is an org-wide setting (`Set-TransportConfig -SmtpClientAuthenticationDisabled`) and a per-mailbox override (`Set-CASMailbox -SmtpClientAuthenticationDisabled $false|$true|$null`). The mailbox setting takes precedence. — [Microsoft Learn: Enable or disable SMTP AUTH in Exchange Online (ms.date 2026-09-16)](https://learn.microsoft.com/en-us/exchange/clients-and-mobile-in-exchange-online/authenticated-client-smtp-submission)
- "If *security defaults* is enabled in your organization, SMTP AUTH is already disabled in Exchange Online. To use SMTP AUTH, you need to disable *security defaults*." — [same page](https://learn.microsoft.com/en-us/exchange/clients-and-mobile-in-exchange-online/authenticated-client-smtp-submission)
- The basic-auth page also says Microsoft "disabled SMTP AUTH in all tenants where it wasn't being used". — [Microsoft Learn](https://learn.microsoft.com/en-us/exchange/clients-and-mobile-in-exchange-online/deprecation-of-basic-authentication-exchange-online)

**Outlook.com (consumer)**
- Microsoft support timeline: 2024-06-11 end of support announced. End of June 2024, active Basic Auth users emailed. "**September 16, 2024: Basic Authentication no longer available to access any Outlook account**." Third-party apps must support "Modern Authentication against EAS, POP, IMAP, and SMTP". Thunderbird is named as supporting it. — [Microsoft Support: Modern Authentication Methods now needed to continue syncing Outlook Email in non-Microsoft email apps (last updated 2024-06-10)](https://support.microsoft.com/en-us/topic/c5d65390-9676-4763-b41f-d7986499a90d)
- App passwords for Outlook.com IMAP: user reports (Microsoft Q&A, Jan 2025) say they began failing around 2024-10-01. I found no official Microsoft statement giving an app-password cut-off date. — [Microsoft Q&A: IMAP and app passwords not working anymore?](https://learn.microsoft.com/en-nz/answers/questions/4726858/imap-and-app-passwords-not-working-anymore)
- "OAuth2 support for IMAP, POP, and SMTP protocols as described below is available for both Microsoft 365 (which includes Office on the web) and Outlook.com users." — [Microsoft Learn: Authenticate an IMAP, POP or SMTP connection using OAuth (updated 2025-10-17)](https://learn.microsoft.com/en-us/exchange/client-developer/legacy-protocols/how-to-authenticate-an-imap-pop-smtp-application-by-using-oauth)

### Inferences
- As of 2026-11-24, IMAP + SMTP with OAuth2/XOAUTH2 is a fully supported path to both Microsoft 365 and Outlook.com mailboxes, and no end date has been announced. For a classic IMAP client like Mulberry, the work is "add XOAUTH2 + an OAuth2 token flow", not "write a Graph backend".
- For SMTP, OAuth will be practically required for most M365 tenants from January 2027 (basic auth off by default). Outlook.com already requires it. Mulberry should treat XOAUTH2 for SMTP as mandatory.
- Tenant admin settings (security defaults, `SmtpClientAuthenticationDisabled`, per-mailbox IMAP/POP enablement, Conditional Access) can still block a user even with a correct OAuth implementation. The client should show useful error text for that case.

### Gaps
- I did not fetch the 2026-01-27 Exchange Team blog post itself (techcommunity returned HTTP 403). The timeline comes from the MC786329 mirror and Office365ITPros, which agree with each other.
- I found no Microsoft statement that IMAP/POP will *ever* be retired, and no explicit guarantee that they will stay. The absence of an announcement is the evidence.
- Per-mailbox IMAP/POP enablement (`Set-CASMailbox -ImapEnabled/-PopEnabled`) and whether new tenants disable POP/IMAP by default were not verified in a source during this session.

---

## 2. EWS retirement in Exchange Online

### Takeaway
EWS in Exchange Online began global disablement in **October 2026** and is scheduled to be **fully disabled in April 2027**. A tenant can keep it alive only until April 2027, and only for allow-listed apps (`EWSAllowedAppIDs`). EWS on on-premises Exchange Server is unaffected. EWS is not an option for a new Mulberry backend against Exchange Online.

### Cited Findings
- Timeline: July 2018, EWS gets no more feature updates. 2023, disablement set for 10/2026. "**October 2026**: EWS starts to be disabled globally for all organizations." "**April 2027**: EWS is fully disabled." The page was updated 2026-10-23 and carries a banner: "Are you here because your Exchange Online EWS applications have stopped working? Read this". That suggests disablement is under way. — [Microsoft Learn: Deprecation of Exchange Web Services in Exchange Online (updated 2026-10-23)](https://learn.microsoft.com/en-us/exchange/clients-and-mobile-in-exchange-online/deprecation-of-ews-exchange-online)
- Mechanism per MC1447678 and the "Introducing EWSAllowedAppIDs" post: tenants with `EWSEnabled` still Null on 2026-10-01 are flipped to False during rollout. Admins can enable EWS and allow-list specific app IDs until April 2027. Exchange Server (on-prem) is unchanged. — [MC1447678 (mc.merill.net)](https://mc.merill.net/message/MC1447678); [Exchange Team blog: Introducing EWSAllowedAppIDs](https://techcommunity.microsoft.com/blog/exchange/introducing-ewsallowedappids-preparing-for-the-final-phase-of-ews-retirement/4529471); [Exchange Team blog: Exchange Online EWS, Your Time is Almost Up](https://techcommunity.microsoft.com/blog/exchange/exchange-online-ews-your-time-is-almost-up/4492361) (403 on fetch; summarized via search)
- Graph parity gaps still listed for Q3/Q4 CY2026 include **"Non-draft MIME update/creation — Create or update non-draft messages by using MIME content" (Q4 CY2026)**, "Mark All Items As Read" (Q4 CY2026), archive/public-folder import/export, and "Contact Lists" (Q3 CY2026). Generic Public Folder CRUD and generic M365 Group mailbox CRUD "won't be added to Microsoft Graph". — [Microsoft Learn EWS deprecation page](https://learn.microsoft.com/en-us/exchange/clients-and-mobile-in-exchange-online/deprecation-of-ews-exchange-online)

### Inferences
- EWS is ruled out for Exchange Online. It would only matter for on-prem Exchange Server customers, which is outside this scope.
- The "non-draft MIME creation" gap matters for any Graph mail backend. The Graph equivalent of IMAP `APPEND` (putting an arbitrary received/sent RFC 5322 message into a folder, e.g. saving to Sent, copying between accounts, importing mbox) was still on Microsoft's to-do list for Q4 2026. IMAP `APPEND` does this today.

### Gaps
- No Microsoft post-October-2026 status report confirming how far the rollout has progressed. Only the Learn page banner (updated 2026-10-23) indicates it is under way.

---

## 3. Microsoft identity platform details for a public (native) desktop client

### Takeaway
Register one multitenant + personal-accounts app (`signInAudience = AzureADandPersonalMicrosoftAccount`) as a "Mobile and desktop" public client, with no client secret. Use the auth code + PKCE flow with an `http://localhost` (or `http://127.0.0.1`) loopback redirect, where the port is ignored for matching. The device code flow is a fallback. Use `/common` (or `/organizations` / `/consumers`) and request `https://outlook.office.com/IMAP.AccessAsUser.All`, `…/POP.AccessAsUser.All`, `…/SMTP.Send` and `offline_access`. Access tokens last 60–90 min and refresh tokens 90 days (they replace themselves on each use). The main real-world blockers are tenant consent policies ("Need admin approval") and the missing publisher-verified badge.

### Cited Findings
**Scopes and flows**
- Scopes: IMAP `https://outlook.office.com/IMAP.AccessAsUser.All`, POP `https://outlook.office.com/POP.AccessAsUser.All`, SMTP AUTH `https://outlook.office.com/SMTP.Send`, plus `offline_access` for refresh tokens. Supported flows: authorization code and device authorization grant. Client credentials (app-only) also exists, using `IMAP.AccessAsApp`/`POP.AccessAsApp`/`SMTP.SendAsApp`, admin consent, `New-ServicePrincipal` and per-mailbox `Add-MailboxPermission`. That is irrelevant for a desktop client. — [Microsoft Learn: Authenticate an IMAP, POP or SMTP connection using OAuth](https://learn.microsoft.com/en-us/exchange/client-developer/legacy-protocols/how-to-authenticate-an-imap-pop-smtp-application-by-using-oauth)
- Device code: tenant can be `/common`, `/consumers`, `/organizations` or a specific tenant. The user has 15 min (`expires_in` default). `verification_uri_complete` is "not included or supported". Personal-account users on `/common` or `/consumers` must sign in again on the device-code page. — [Microsoft Learn: OAuth 2.0 device authorization grant](https://learn.microsoft.com/en-us/entra/identity-platform/v2-oauth2-device-code)
- Token warning: "Tokens for Microsoft services can use a special format that will not validate as a JWT, and may also be encrypted for consumer (Microsoft account) users." Clients must treat access tokens as opaque. — [same page](https://learn.microsoft.com/en-us/entra/identity-platform/v2-oauth2-device-code); [Microsoft Learn: Access tokens](https://learn.microsoft.com/en-us/entra/identity-platform/access-tokens)
- Tenant aliases: `/organizations` = any Entra tenant. `/common` = any Entra tenant + personal MSA. `/consumers` = personal only (tenant 9188040d-6c67-4c5b-b112-36a304b66dad). — [Microsoft Learn: Access tokens](https://learn.microsoft.com/en-us/entra/identity-platform/access-tokens)

**Redirect URIs (loopback)**
- "Mobile and desktop applications" is the platform for native apps. HTTP is allowed only for localhost. "the port component … is ignored for the purposes of matching a localhost redirect URI", so `http://localhost:1234/MyApp` ≡ `http://localhost:8080/MyApp`. Don't register several localhost URIs that differ only by port. IPv6 `[::1]` is "isn't currently supported". Microsoft recommends `127.0.0.1` over `localhost`, but an `http://127.0.0.1` redirect must be added through the app manifest (`replyUrlsWithType`), not the portal text box. — [Microsoft Learn: Redirect URI best practices and limitations](https://learn.microsoft.com/en-us/entra/identity-platform/reply-url)
- Apps with `signInAudience = AzureADandPersonalMicrosoftAccount`: at most 100 redirect URIs, **no query parameters** in redirect URIs, and no wildcards. — [same page](https://learn.microsoft.com/en-us/entra/identity-platform/reply-url)

**Token lifetimes**
- Access token: random 60–90 min (avg 75). With CAE, long-lived tokens are 20–28 h. — [Microsoft Learn: Access tokens (updated 2026-07-17)](https://learn.microsoft.com/en-us/entra/identity-platform/access-tokens)
- Refresh token: **24 h** for SPA redirect URIs and for email-OTP flows, **90 days** for all other cases. "Refresh tokens replace themselves with a fresh token upon every use", and old ones are not revoked. Securely delete the old one. They can be revoked at any time, e.g. on password change for password-based tokens, or when an admin or user revokes them. Refresh tokens are not tied to a resource, so one RT can get ATs for different resources the client has permission for. — [Microsoft Learn: Refresh tokens (updated 2026-06-15)](https://learn.microsoft.com/en-us/entra/identity-platform/refresh-tokens)

**Consent, verification, tenant policy**
- Publisher verification: since November 2020, if risk-based step-up consent is enabled, users "can't consent to most newly registered multitenant apps that aren't publisher verified" when the apps request more than basic sign-in/profile permissions from other tenants. Verification needs a verified Microsoft AI Cloud Partner Program (formerly MPN) account. The app must be registered in an Entra work/school tenant (not via an MSA). The publisher domain can't be `*.onmicrosoft.com`. "Microsoft doesn't charge developers for publisher verification. No license is required." — [Microsoft Learn: Publisher verification overview](https://learn.microsoft.com/en-us/entra/identity-platform/publisher-verification-overview)
- MC1097272 (rollout mid-July to August 2025): Microsoft-managed app consent policies stop users from consenting to third-party apps that access their **files and sites** by default (tenants on default settings only). Later Entra docs say the Microsoft-managed policy, the default for new tenants, excludes broad Files, Sites, Mail and Calendar permissions. A built-in policy `microsoft-user-default-consent-apps` exists, reportedly covering popular mail clients. — [MC1097272 mirror](https://mc.merill.net/message/MC1097272); [Office365ITPros 2025-06-19](https://office365itpros.com/2025/06/19/app-consent-policy-user-app-consent/) (Mail-specific details come from a search summary and are not fully verified)
- Thunderbird's Microsoft client ID is `9e5f94bc-e8a4-4e73-b8be-63364c29d753`. Mozilla's KB gives admins an `adminconsent` URL for it covering IMAP.AccessAsUser.All, POP.AccessAsUser.All, SMTP.Send and offline_access. "Need admin approval" means a tenant admin has to consent. — [Mozilla KB: Microsoft OAuth Authentication and Thunderbird in 2026](https://support.mozilla.org/en-US/kb/microsoft-oauth-authentication-and-thunderbird-202) (page body could not be fetched; facts come from search snippets); [Bugzilla 1815715](https://bugzilla.mozilla.org/show_bug.cgi?id=1815715)

### Inferences
- Mulberry needs its own app registration. Microsoft has no "public well-known client" for IMAP. Reusing Thunderbird's ID, as many mutt guides do, is fragile and arguably against the terms of use. In many tenants, users of a new unverified multitenant app will hit "Need admin approval" because of user-consent restrictions or risk-based step-up. Publisher verification (free, but needs a CPP/MPN org account and a custom domain) reduces this but does not remove it. Mulberry should therefore also let the user enter their own client ID/tenant ID, as Thunderbird's "Custom OAuth" does.
- Since refresh tokens rotate, Mulberry must persist the new refresh token after every refresh (atomically) and handle `invalid_grant` by starting interactive sign-in again.
- Auth code + PKCE with a loopback listener on an ephemeral port is the right default on FreeBSD/Linux. Device code is a useful fallback for headless or SSH sessions, but some tenants block it with Conditional Access policies.

### Gaps
- No official source was fetched confirming that Entra app registration is free. This is widely understood and implied by the free publisher-verification statement, but it was not verified this session.
- Whether a single app can request IMAP + SMTP scopes (resource outlook.office.com) *and* Graph scopes in one consent: the identity platform issues one access token per resource, so separate token requests are needed. This was not explicitly sourced.

---

## 4. SASL mechanisms and server behaviour (XOAUTH2, OAUTHBEARER, quirks)

### Takeaway
Microsoft servers advertise **only XOAUTH2**, not OAUTHBEARER. The XOAUTH2 string is `base64("user=" + upn + "\x01auth=Bearer " + token + "\x01\x01")`. For shared mailboxes, put the **shared mailbox address** in `user=` with the delegate's token. Failures come back as a bare `NO AUTHENTICATE failed.` / `535 5.7.3 Authentication unsuccessful` with no further detail.

### Cited Findings
- XOAUTH2 format, IMAP `AUTHENTICATE XOAUTH2 <b64>` (SASL-IR), POP `AUTH XOAUTH2` followed by `+` and then the base64 line, SMTP `AUTH XOAUTH2` with `334` continuation. Success responses: `A01 OK AUTHENTICATE completed.`, `+OK User successfully authenticated.`, `235 2.7.0 Authentication successful`. Failure responses: `A01 NO AUTHENTICATE failed.`, `-ERR Authentication failure: unknown user name or bad password.`, `535 5.7.3 Authentication unsuccessful [...]`. — [Microsoft Learn: Authenticate an IMAP, POP or SMTP connection using OAuth](https://learn.microsoft.com/en-us/exchange/client-developer/legacy-protocols/how-to-authenticate-an-imap-pop-smtp-application-by-using-oauth)
- Shared mailbox: "obtain the access token on behalf of a user but replace the userName field in the SASL XOAUTH2 encoded string with the email address of the shared mailbox." — [same page](https://learn.microsoft.com/en-us/exchange/client-developer/legacy-protocols/how-to-authenticate-an-imap-pop-smtp-application-by-using-oauth)
- Observed 2026-11-24 (anonymous connect, no credentials): `outlook.office365.com:993` and `imap-mail.outlook.com:993` both return `CAPABILITY IMAP4 IMAP4rev1 AUTH=XOAUTH2 LOGINDISABLED SASL-IR UIDPLUS MOVE ID UNSELECT CHILDREN IDLE NAMESPACE LITERAL+`. The list has **no AUTH=PLAIN, no OAUTHBEARER, no CONDSTORE/QRESYNC, no IMAP4rev2**. `outlook.office365.com:995` POP3 `CAPA` returns `TOP`, `UIDL`, `SASL XOAUTH2`. SMTP (`smtp.office365.com`, `smtp-mail.outlook.com`) returns `AUTH LOGIN XOAUTH2`. Both the consumer and M365 hostnames land on the same Exchange Online front end (banner "Microsoft Exchange IMAP4 service ready … tcpproxy/15.21.x"). — direct protocol observation (researcher's own `openssl s_client` test). No source URL.
- Community reports: Microsoft requires xoauth2 while Gmail also accepts oauthbearer. Mutt/neomutt Office 365 guides configure `imap_authenticators="oauthbearer:xoauth2"` so that it falls back to XOAUTH2. — [UW guide: Configuring mutt (neomutt) for O365](https://staff.washington.edu/shrike/unix/configuring-mutt-neomutt-for-imap-access-to-o365-exchange); [Glasgow DCS post](https://www.dcs.gla.ac.uk/~jacobd/posts/2022/03/configure-mutt-to-work-with-oauth-20)
- Known quirk: "AUTHENTICATE failed" despite a valid-looking token (e.g. wrong scope/resource, tenant blocks IMAP, mailbox IMAP disabled). For Outlook.com, a reported fix is to disable IMAP in Outlook.com settings, sign out and in, then re-enable. — [Mozilla support thread (via search)](https://support.mozilla.org/ar/questions/1617070); [Microsoft Q&A: XOAUTH2 for IMAP does not work for some @outlook.com accounts](https://learn.microsoft.com/en-us/answers/questions/570312/xoauth2-(aka-modern-authentication)-for-imap-does)

### Inferences
- Mulberry must implement the XOAUTH2 mechanism specifically. An OAUTHBEARER-only (RFC 7628) implementation will not work with Microsoft. Implementing both is best, preferring OAUTHBEARER where the server advertises it (e.g. other providers).
- Because Exchange Online IMAP lacks CONDSTORE/QRESYNC, Mulberry's sync against Microsoft has to rely on UIDVALIDITY/UIDNEXT plus flag re-fetch, and IDLE for push. This is the same as any classic IMAP4rev1 server.
- Error messages carry no reason, so the client should report likely causes itself: token expired or wrong scope, tenant/mailbox has IMAP or SMTP AUTH disabled, admin consent missing.

### Gaps
- I found no official Microsoft statement on OAUTHBEARER. Its absence comes from the live CAPABILITY output only. It is possible (not verified) that capabilities differ after STARTTLS on port 143 or per tenant.

---

## 5. Microsoft Graph mail API: what a Graph backend would involve, and when it is actually required

### Takeaway
Graph is **not required** for mail access. It is required (or the only modern option) for **calendar/contacts in Exchange Online**, for replacing EWS, and for tenants that block IMAP/POP/SMTP AUTH. A Graph mail backend is a whole new REST/JSON sync engine:
- per-folder delta sync, and opaque item IDs that change on move unless you request immutable IDs
- a non-IMAP flag model (isRead, followupFlag, categories; no arbitrary keywords)
- MIME read via `/$value`, and send via base64 MIME to `sendMail`
- no general non-draft MIME import (IMAP APPEND equivalent) as of the Q4 2026 roadmap
- throttling at 10,000 requests/10 min, 4 concurrent requests and 150 MB upload per 5 min per app per mailbox
- push only via public webhooks, so a desktop client must poll

The effort is significantly larger than adding XOAUTH2 to existing IMAP/SMTP code.

### Cited Findings
**Message model and operations**
- Message methods: list, create draft, get, update, delete, copy, move ("creates a new copy of the message in the destination folder"), createReply/createReplyAll/createForward, send draft, reply/replyAll/forward, permanentDelete, reportMessage, attachments, open extensions, extended properties, and delta. — [Graph: message resource type (updated 2026-11-10)](https://learn.microsoft.com/en-us/graph/api/resources/message?view=graph-rest-1.0)
- Properties relevant to a mail client: `isRead`, `flag` (followupFlag), `categories`, `importance`, `internetMessageId`, `conversationId`, `changeKey`, `parentFolderId`, `internetMessageHeaders` (needs `$select`), `inferenceClassification` (focused/other), `isDraft`, `hasAttachments` (excludes inline attachments). The `id` "changes when the item is moved … To change this behavior, use the `Prefer: IdType="ImmutableId"` header." The limit is 500 total recipients per message. — [same page](https://learn.microsoft.com/en-us/graph/api/resources/message?view=graph-rest-1.0)
- MIME: messages can be created or sent in MIME by using `Content-Type: text/plain` with a base64-encoded RFC 822 body. This is supported for send, reply, forward and creating drafts, including S/MIME signed and encrypted messages. "Microsoft Graph does not support editing or updating MIME properties individually." MIME content of a message is read via `$value` (see "getting MIME content of a message"). — [Graph: Send emails with MIME content](https://learn.microsoft.com/en-us/graph/outlook-send-mime-message); [Graph: Get MIME content](https://learn.microsoft.com/en-us/graph/outlook-get-mime-message)
- `sendMail`: `POST /me/sendMail`, JSON or base64 MIME (`text/plain`), saves to Sent Items, returns `202 Accepted` (accepted, not yet delivered). Permission `Mail.Send` is available for **delegated work/school, delegated personal Microsoft account**, and application. — [Graph: user: sendMail (updated 2026-06-19)](https://learn.microsoft.com/en-us/graph/api/user-sendmail?view=graph-rest-1.0)
- Size: community/Q&A reports put a single `sendMail`/`POST` request at roughly 3–4 MB, with 413 errors above that. Attachments of 3–150 MB must go through an upload session on a draft, and there is no upload session for a raw MIME message. — [Graph: Attach large files](https://learn.microsoft.com/en-us/graph/outlook-large-attachments); [Microsoft Q&A: sendMail request size limit](https://learn.microsoft.com/en-in/answers/questions/1824204/graph-api-user-sendmail-request-size-limit); [Microsoft Q&A: PGP Graph API 4MB limit](https://learn.microsoft.com/en-us/answers/questions/892013/pgp-graph-api-4mb-limit). Exact figures are partly community-sourced.
- Missing parity: "Non-draft MIME update/creation" and "Mark All Items As Read" are listed as Graph gaps with ETA Q4 CY2026. — [Microsoft Learn: EWS deprecation, parity roadmap](https://learn.microsoft.com/en-us/exchange/clients-and-mobile-in-exchange-online/deprecation-of-ews-exchange-online)

**Sync**
- "Delta query is a per-folder operation. To track the changes of the messages in a folder hierarchy, you need to track each folder individually." It uses `GET /me/mailFolders/{id}/messages/delta`, `@odata.nextLink`/`@odata.deltaLink` opaque state tokens, deletions as `@removed: {reason: deleted}`, `$select`/`$top`/`$expand`, and an optional `changeType=created|updated|deleted`. `$filter` is limited to `receivedDateTime ge/gt` (max 5,000 messages when filtered), `$orderby` only `receivedDateTime desc`, there is no `$search`, and page size is set with `Prefer: odata.maxpagesize`. — [Graph: Get incremental changes to messages in a folder](https://learn.microsoft.com/en-us/graph/delta-query-messages)
- Change notifications are delivered only via **webhooks** (a public HTTPS endpoint), Azure Event Hubs or Azure Event Grid. Outlook message/event/contact subscriptions last at most 10,080 min (under 7 days), or 1,440 min with resource data, with up to 1,000 active subscriptions per mailbox. Message notification latency averages under 1 min, max 3 min. — [Graph: change notifications overview](https://learn.microsoft.com/en-us/graph/change-notifications-overview)

**Throttling**
- Outlook service limits **per app ID + mailbox**: "10,000 API requests in a 10-minute period", "Four concurrent requests", "150 megabytes (MB) upload (PATCH, POST, PUT) in a 5-minute period". JSON batching sends at most 4 requests to Outlook in parallel. There is also a global 130,000 requests per 10 s per app across all tenants. — [Graph: throttling limits (updated 2026-11-19)](https://learn.microsoft.com/en-us/graph/throttling-limits)

**Scopes**
- `Mail.Send` (see above). For read/write, `Mail.ReadWrite` is the delegated scope, plus `Mail.ReadWrite.Shared`/`Mail.Send.Shared` for shared or delegated mailboxes. These are standard Graph permission names, but the permissions-reference page was not fetched this session.

**Calendar/contacts: CalDAV/CardDAV**
- Exchange Online offers no CalDAV/CardDAV. Microsoft lists ActiveSync, MAPI, Outlook on the web, IMAP and POP3 as client access methods, and CardDAV is not among them. Third-party CalDAV access to M365 calendars isn't possible without a gateway. — secondary: [CiraSync blog](https://cirasync.com/?p=74448); [eM Client forum](https://forum.emclient.com/t/integrate-only-m365-calendar/70327); [Microsoft Q&A](https://learn.microsoft.com/en-us/answers/a/2059193); feature request [Outlook CalDAV/CardDAV support idea (techcommunity)](https://techcommunity.microsoft.com/idea/microsoft365developerplatform/outlook-caldavcarddav-support/3591840/comments/4095392)
- Graph change-notification support for `event` and `contact` (personal contacts), and the Graph calendar/contacts APIs are part of the same Outlook throttling bucket. — [Graph change notifications](https://learn.microsoft.com/en-us/graph/change-notifications-overview); [Graph throttling limits](https://learn.microsoft.com/en-us/graph/throttling-limits)
- Evolution maintainer (Aug 2026): Graph has no Global Address List equivalent, so apps must use multiple separate address books. — [GNOME Discourse: Microsoft 365 (Graph) in Evolution 3.60](https://discourse.gnome.org/t/is-microsoft-365-graph-api-expected-to-be-available-in-evolution-3-60-x/36019)

### Inferences
- **When Graph is actually needed:**
  - (a) Calendar/contacts for Exchange Online. Mulberry's CalDAV/CardDAV code cannot talk to M365, so the alternatives are Graph or EAS.
  - (b) Tenants whose admins disabled IMAP/POP or SMTP AUTH. Thunderbird says EWS/Graph are "meant for organizations that block IMAP".
  - (c) Features IMAP doesn't expose: Focused Inbox, categories with colours, follow-up flags with dates, server-side rules, out-of-office.
- **For plain mail, Graph is optional and costly.** A Graph backend means:
  - a REST/JSON/OData client
  - per-folder delta sync, and mapping Graph IDs to a local store (use ImmutableId)
  - fetching MIME via `/$value` to reuse Mulberry's MIME parser
  - mapping IMAP flags: \Seen↔isRead, \Flagged↔flag.flagStatus, keywords↔categories (lossy), \Deleted→move to Deleted Items/permanentDelete, \Answered/\Draft partly via extended properties
  - no APPEND equivalent for non-draft messages (save-to-folder, cross-account copy)
  - small request-size limits for MIME send
  - client-side throttling (4 concurrent requests per mailbox)
  - polling instead of IDLE, because webhooks need a public HTTPS endpoint that a desktop app can't host
  - Graph works only for Microsoft accounts, so none of this code serves other providers
- Recommended split: mail via IMAP/SMTP + XOAUTH2 now. Treat a Graph calendar/contacts adapter as a separate, later project if M365 calendar access is wanted.

### Gaps
- The Graph permissions reference (exact consent requirements for `Mail.ReadWrite` with personal accounts) and the exact `sendMail` MIME size limit are not confirmed from official pages.
- Whether Graph mail delta works identically for Outlook.com personal accounts was not verified. `sendMail` explicitly supports personal accounts.

---

## 6. How other open-source clients handle Microsoft (late 2026)

### Takeaway
Thunderbird does IMAP/POP/SMTP via OAuth2 (its own client ID). It added EWS for mail in late 2025 (v145 timeframe) and **Microsoft Graph for mail in Thunderbird 154 (2026-08-18)**. Calendar and contacts are not yet supported over EWS or Graph. Evolution's evolution-ews ships an EWS module and a "Microsoft 365" (Graph) module, usable in 3.60.x. mutt/neomutt/alpine use helper scripts (mutt_oauth2.py, ooauth2.py) and generally require users to register their own app.

### Cited Findings
- Thunderbird blog (2026-09-02): Graph support first shipped in **Thunderbird 154** (Release channel). **ESR 153 supports EWS but not Graph**. Graph is **email only**. "Calendar and Address Book" are not supported with EWS or Graph, and Graph calendar is in progress with no date. Users whose M365 account works over IMAP need no action, because EWS/Graph are meant for orgs that block IMAP. There is no protocol migration, so the account must be deleted and re-added. EWS basic sync took 11 months and Graph 4 months, thanks to a new mail architecture they hope can support JMAP later. Custom Client/Tenant IDs are covered on the Mozilla wiki. — [Thunderbird blog: New Protocol Support: Microsoft Graph API](https://blog.thunderbird.net/2026/09/thunderbird-desktop-new-protocol-support-microsoft-graph-api/)
- Release date 2026-08-18 for Thunderbird 154 with Graph for M365. — [heise](https://www.heise.de/en/news/Thunderbird-switches-Exchange-Online-to-Microsoft-Graph-11439704.html); [it-connect](https://www.it-connect.tech/thunderbird-154-adds-microsoft-graph-support-for-microsoft-365-accounts/); [Mozilla KB: Microsoft EWS to Graph Migration October 2026](https://support.mozilla.org/en-US/kb/microsoft-ews-graph-migration)
- Thunderbird's Microsoft OAuth client ID is `9e5f94bc-e8a4-4e73-b8be-63364c29d753`, with a tenant admin-consent URL for IMAP/POP/SMTP/offline_access. A client-ID change caused new "Need admin approval" prompts. — [Mozilla KB (search snippet)](https://support.mozilla.org/en-US/kb/microsoft-oauth-authentication-and-thunderbird-202); [Bugzilla 1815715](https://bugzilla.mozilla.org/show_bug.cgi?id=1815715); [Mozilla KB: Custom OAuth for Thunderbird](https://support.mozilla.org/kb/tb-custom-oauth)
- Evolution: evolution-ews has offered Office 365 OAuth2 for EWS since 3.27.91. The Graph ("Microsoft 365") module was in preview in the 3.52/3.56 series. In July–August 2026, a user on Evolution 3.60.2 reports Graph setup working. The maintainer says many pain points are fixed, but protocol-level gaps remain (no GAL, shared mailboxes tracked as issue #341). — [GNOME Discourse thread (Jul–Aug 2026)](https://discourse.gnome.org/t/is-microsoft-365-graph-api-expected-to-be-available-in-evolution-3-60-x/36019); [GNOME wiki EWS/OAuth2](https://wiki.gnome.org/Apps/Evolution/EWS/OAuth2); [evolution-ews issue #245](https://gitlab.gnome.org/GNOME/evolution-ews/-/issues/245)
- mutt/neomutt: `mutt_oauth2.py` needs a client_id (and secret, if set) from your own Azure registration. Guides add delegated permissions IMAP.AccessAsUser.All, SMTP.Send, offline_access (and User.Read). Some guides reuse Thunderbird's ID. Alpine ships `ooauth2.py`, which takes `--client_id` and uses tenant endpoints. — [mutt_oauth2.py README (openSUSE)](https://src.opensuse.org/rpm/mutt/raw/branch/devel/mutt_oauth2.py.README); [UW neomutt O365 guide](https://staff.washington.edu/shrike/unix/configuring-mutt-neomutt-for-imap-access-to-o365-exchange); [Alpine ooauth2 script](https://alpineapp.email/scripts/ooauth2.html)

### Inferences
- The open-source ecosystem confirms the split: IMAP/SMTP + XOAUTH2 is the baseline everyone supports for both M365 and Outlook.com. Graph is being adopted by Thunderbird and Evolution only because they had EWS backends to replace and wanted Exchange-native features. Even Thunderbird has no Graph calendar/contacts yet.
- Registration strategy options for Mulberry:
  - (a) A project-owned multitenant + personal app, ideally publisher-verified. This needs an organization with a CPP account and a custom domain.
  - (b) Shipping no ID and requiring users or admins to register their own, as mutt does.
  - (c) Both, with an override, as Thunderbird does.

### Gaps
- KMail and aerc current Microsoft handling were not researched. KMail bug 457233 suggests OAuth2 for non-Gmail IMAP was long missing ([KDE bug 457233](https://bugs.kde.org/show_bug.cgi?id=457233)), but its 2026 status was not verified.
- The exact Thunderbird version that first shipped EWS (blog implies November 2025) was not confirmed.

---

## 7. JMAP or other standard alternatives

### Takeaway
Microsoft offers no JMAP endpoint for Exchange Online or Outlook.com. No source announcing one was found. The standards-based options are IMAP/POP/SMTP with XOAUTH2 for mail, and nothing for calendar/contacts. Exchange ActiveSync (proprietary, licensed) and Graph (proprietary REST) are the non-standard options.

### Cited Findings
- Microsoft's list of client access methods for Exchange Online mailboxes is ActiveSync, MAPI, Outlook on the web, IMAP and POP3, with no CalDAV/CardDAV (and no JMAP). — secondary via [CiraSync](https://cirasync.com/?p=74448)
- Thunderbird hopes its new architecture could support JMAP "later", which implies no Microsoft JMAP is in play. — [Thunderbird blog 2026-09](https://blog.thunderbird.net/2026/09/thunderbird-desktop-new-protocol-support-microsoft-graph-api/)
- An Exchange Online ActiveSync minimum version (16.1) is reportedly enforced as of August 2026. — vendor blog via search only, not verified on Microsoft Learn.

### Inferences
- For Mulberry, JMAP is irrelevant for Microsoft. If Microsoft calendar/contacts are needed, the realistic options are a Graph adapter or EAS. EAS has licensing and protocol complexity and is not recommended.

### Gaps
- There is no explicit Microsoft statement "we do not support JMAP/CalDAV". The conclusion rests on the absence of any such endpoint in Microsoft's protocol lists and on secondary sources.

---

# SCOPE EXTENSION: Microsoft Graph Calendar and Contacts (needed because Exchange Online has no CalDAV/CardDAV)

## 8. Graph Calendar API vs Mulberry's iCalendar/CalDAV model

### Takeaway
Graph calendars are JSON "event" objects, not iCalendar. **No Graph endpoint imports or exports raw ICS** for events. Mulberry would need a bidirectional JSON↔CICalendar mapping layer. Sync has two options:
- `calendarView/delta`: v1.0, but bounded by a time window, returns occurrences expanded, and does not support `$select`.
- `events/delta`: unbounded, but beta only.

Recurrence uses `patternedRecurrence`, which has 6 pattern types (daily, weekly, absolute/relative monthly, absolute/relative yearly) and 3 range types. That is a strict subset of RRULE: every Graph pattern maps to RRULE, but many RRULEs cannot map back. Time zones are Windows names (plus a fixed list of IANA aliases). Responses default to UTC unless `Prefer: outlook.timezone` is sent. Scheduling (invites/responses) is done by Exchange server-side through `accept`/`decline`/`tentativelyAccept`/`cancel`/`forward` actions, not by iTIP/iMIP from the client. Mapping is lossy in both directions and needs extensions/extended properties to round-trip iCalendar-only data.

### Cited Findings
**Resources, endpoints, methods**
- `event` methods: List (returns "single instance meetings and series masters"), Create, Get, Update, Delete, Permanently delete, delta (calendarView), Forward, Cancel ("Send a cancellation message from the organizer to all the attendees"), Accept, Tentatively accept, Decline, Dismiss/Snooze reminder, List instances, attachments (file, item, reference), open extensions, and single/multi-value extended properties. Group calendars: attachments are not supported, Outlook auto-accepts meeting requests for groups, and there are no reminders for group events. Max 500 attendees. — [Graph v1.0: event resource (updated 2026-04-20)](https://learn.microsoft.com/en-us/graph/api/resources/event?view=graph-rest-1.0)
- Event properties relevant to mapping:
  - `start`/`end` (dateTimeTimeZone, "By default … UTC") and `isAllDay`: "start, and endtime must be set to midnight and be in the same time zone".
  - `recurrence`, `type` (`singleInstance`, `occurrence`, `exception`, `seriesMaster`), `seriesMasterId`, `originalStart`, `originalStartTimeZone`/`originalEndTimeZone` (`tzone://Microsoft/Custom` = legacy custom zone).
  - `attendees`, `organizer`, `isOrganizer`, `responseStatus`, `responseRequested`, `allowNewTimeProposals`, `hideAttendees`.
  - `reminderMinutesBeforeStart`, `isReminderOn` (a single reminder).
  - `showAs` (free/tentative/busy/oof/workingElsewhere/unknown), `sensitivity` (normal/personal/private/confidential), `importance`, `categories`.
  - `location`/`locations`, `body` (HTML/text), `isOnlineMeeting`/`onlineMeeting`, `isCancelled`, `isDraft`.
  - `changeKey` (version/ETag-like), `transactionId` (client idempotency key for POST), `cancelledOccurrences`, `exceptionOccurrences`.
  - `id` "changes when the item is moved … use the `Prefer: IdType="ImmutableId"` header". — [same page](https://learn.microsoft.com/en-us/graph/api/resources/event?view=graph-rest-1.0)
- **UID**: v1.0 `iCalUId` is "A unique identifier for an event across calendars. This ID is different for each occurrence in a recurring series." Beta adds `uid`: "For recurring events, the value is the same for the series master and all of its occurrences including exceptions." Beta also adds `occurrenceId` = `OID.{seriesMasterId}.{occurrence-start-date}`, which can address any occurrence including modified or cancelled ones. The v1.0 Get-event example response shows `iCalUId` and `uid` values of the form `040000008200E00074…` (an Exchange Global Object ID in hex). — [Graph beta: event resource](https://learn.microsoft.com/en-us/graph/api/resources/event?view=graph-rest-beta); [Graph v1.0: Get event, Example 3](https://learn.microsoft.com/en-us/graph/api/event-get?view=graph-rest-1.0)
- Expanding a series master: `GET /me/events/{masterId}?$select=…,exceptionOccurrences,cancelledOccurrences&$expand=exceptionOccurrences` returns the exceptions (with their own ids) and cancelled occurrence ids. — [Graph v1.0: Get event](https://learn.microsoft.com/en-us/graph/api/event-get?view=graph-rest-1.0)
- Get event returns bodies in HTML only, unless `Prefer: outlook.body-content-type="text"` is sent. Event endpoints exist under `/me/events`, `/me/calendar/events`, `/me/calendars/{id}/events` and `/me/calendarGroups/{id}/calendars/{id}/events`. Shared/delegated calendars of other users are reachable with appropriate delegated permissions. — [same page](https://learn.microsoft.com/en-us/graph/api/event-get?view=graph-rest-1.0)

**Sync (delta)**
- v1.0 delta is only over a **calendarView**: `GET /me/calendarView/delta?startDateTime=…&endDateTime=…` (or per calendar). The window is encoded in the state tokens. "To track the changes in multiple calendars, you need to track each calendar individually." "`$select` is not supported." `Prefer: odata.maxpagesize`. Deletions come as `@removed`. Returned events "might include single instances or occurrences and exceptions of a recurring series". Unbounded per-calendar delta "is currently available only in the beta version". — [Graph: Get incremental changes to events in a calendar view](https://learn.microsoft.com/en-us/graph/delta-query-events)
- Beta APIs: "Use of these APIs in production applications is not supported." — [Graph beta event page banner](https://learn.microsoft.com/en-us/graph/api/resources/event?view=graph-rest-beta)
- Change notifications for `event` exist: webhook/Event Hubs/Event Grid only, max 10,080 min subscription lifetime, up to 1,000 subscriptions per mailbox. — [Graph change notifications overview](https://learn.microsoft.com/en-us/graph/change-notifications-overview)

**Recurrence model**
- `recurrencePattern.type`: `daily`, `weekly` (needs `daysOfWeek`, `firstDayOfWeek`), `absoluteMonthly` (`dayOfMonth`), `relativeMonthly` (`daysOfWeek`, optional `index` = first/second/third/fourth/last), `absoluteYearly` (`dayOfMonth`, `month`), `relativeYearly` (`daysOfWeek`, `month`, `index`), plus a required `interval`. "If **type** is `relativeMonthly` or `relativeYearly`, and **daysOfWeek** specifies more than one day, the event falls on the first day that satisfies the pattern." "Any property that you include that does not have a supported value would result in an error." — [Graph: recurrencePattern](https://learn.microsoft.com/en-us/graph/api/resources/recurrencepattern?view=graph-rest-1.0)
- `recurrenceRange.type`: `endDate` (a date, inclusive), `noEnd`, `numbered` (`numberOfOccurrences`). Also `startDate` ("Must be the same value as the **start** property of the recurring event") and optional `recurrenceTimeZone`. — [Graph: recurrenceRange](https://learn.microsoft.com/en-us/graph/api/resources/recurrencerange?view=graph-rest-1.0)

**Time zones**
- `dateTimeTimeZone.timeZone` "can be set to any of the time zones currently supported by Windows, as well as the other time zones supported by the calendar API". The page lists about 100 accepted IANA names (e.g. `Europe/Paris`, `America/New_York`, `Etc/GMT+12`). "Methods such as create or update might not support all dateTimeTimeZone time zones." — [Graph: dateTimeTimeZone](https://learn.microsoft.com/en-us/graph/api/resources/datetimetimezone?view=graph-rest-1.0)
- "For all GET operations that return events, you can use the `Prefer: outlook.timezone` header … If the `Prefer: outlook.timezone` header is not specified, the start and end times are returned in UTC." Use `originalStartTimeZone`/`originalEndTimeZone` to learn the creation zone. — [Graph v1.0: Get event](https://learn.microsoft.com/en-us/graph/api/event-get?view=graph-rest-1.0)

**ICS import/export**
- No official Graph endpoint for ICS import or export of events was found. A Microsoft Q&A user reports there is "no way to import" ICS events into an M365 group calendar via Graph. The Mailbox Import/Export API exists, but it handles a "full-fidelity" Exchange format, not ICS. — [Microsoft Q&A](https://learn.microsoft.com/en-za/answers/questions/1818512/how-can-i-use-microsoft-graph-(-powershell)-to-cop); [Graph throttling page listing the Mailbox import and export API](https://learn.microsoft.com/en-us/graph/throttling-limits); [EWS deprecation page: import/export "in a full-fidelity format"](https://learn.microsoft.com/en-us/exchange/clients-and-mobile-in-exchange-online/deprecation-of-ews-exchange-online)

**Permissions (calendar)**
- `Calendars.ReadWrite` delegated: AdminConsentRequired **No**. "The Calendars.ReadWrite delegated permission is available for consent in personal Microsoft accounts." `Calendars.ReadWrite.Shared` delegated: AdminConsentRequired No, with no personal-account note. — [Graph permissions reference](https://learn.microsoft.com/en-us/graph/permissions-reference)
- Get event works with `Calendars.ReadBasic`/`Calendars.Read` for both work/school and personal accounts. — [Graph v1.0: Get event](https://learn.microsoft.com/en-us/graph/api/event-get?view=graph-rest-1.0)
- DavMail 7.0.0 (2026-09-22) requests `Calendars.ReadWrite.Shared` for shared folders. Admins in tenants that restrict app permissions must approve the new scope. — [Help Net Security: DavMail 7.0.0](https://helpnetsecurity.com/2026/09/22/davmail-7-0-0-microsoft-graph)

**Existing open-source Graph ↔ iCalendar/vCard implementations**
- **Evolution / evolution-ews**: ships `module-microsoft365-backend.so` and `libevolution-microsoft365.so` (Graph mail, calendar and contacts backends), licensed **LGPL-2.1** (distro metadata: openSUSE "LGPL-2.1-only", others "LGPLv2+"). It is written in C on libical/EContact. Usable in 3.60.x (2026), with known gaps (no GAL, shared mailboxes WIP). — [openSUSE evolution-ews.spec](https://build.opensuse.org/projects/openSUSE/packages/evolution-ews/files/evolution-ews.spec?expand=0); [GNOME Discourse 2026](https://discourse.gnome.org/t/is-microsoft-365-graph-api-expected-to-be-available-in-evolution-3-60-x/36019); source [gitlab.gnome.org/GNOME/evolution-ews](https://github.com/gnome/evolution-ews) (GitHub mirror)
- **DavMail**: a Java gateway exposing IMAP/SMTP/**CalDAV/CardDAV**/LDAP in front of Exchange/O365. It has an "O365Graph" mode. 6.6.0 (April 2026) covered Graph contact sync, CalDAV event handling, recurrence management and people search, but was described then as not production ready. 7.0.0 (2026-09-22) had 70 Graph changelog entries, including:
  - folder delta sync, on by default (`davmail.graph.deltaSync`)
  - contact distribution lists exposed as contact groups
  - meeting responses targeting single occurrences
  - throttling retry caps
  - `davmail.caldavReadonly`

  Licence: **GPL-2.0** (Debian: GPL-2+). — [Help Net Security 7.0.0](https://helpnetsecurity.com/2026/09/22/davmail-7-0-0-microsoft-graph); [Help Net Security 6.6.0](https://www.helpnetsecurity.com/2026/04/14/davmail-6-6-0-released/); [Debian copyright](https://metadata.ftp-master.debian.org/changelogs/main/d/davmail/stable_copyright); [GitHub mguessan/davmail](https://github.com/mguessan/davmail)
- **Thunderbird**: Graph support (TB 154, August 2026) is mail only. "Graph calendar work is in progress, with no release date", so there is no Thunderbird Graph-calendar code to study yet. — [Thunderbird blog 2026-09](https://blog.thunderbird.net/2026/09/thunderbird-desktop-new-protocol-support-microsoft-graph-api/)

### Inferences
**RRULE mapping (derived from the pattern model above)**
- Graph → RRULE is always possible:
  - `daily` → `FREQ=DAILY;INTERVAL=n`
  - `weekly` → `FREQ=WEEKLY;INTERVAL=n;BYDAY=…;WKST=…`
  - `absoluteMonthly` → `FREQ=MONTHLY;BYMONTHDAY=d`
  - `relativeMonthly` → `FREQ=MONTHLY;BYDAY=<days>;BYSETPOS=<1..4|-1>`. "First day that satisfies the pattern" with several days is the BYSETPOS-over-set semantics, e.g. "first weekday" = `BYDAY=MO,TU,WE,TH,FR;BYSETPOS=1`.
  - `absoluteYearly` → `FREQ=YEARLY;BYMONTH=m;BYMONTHDAY=d`
  - `relativeYearly` → `FREQ=YEARLY;BYMONTH=m;BYDAY=…;BYSETPOS=…`
  - Range: `endDate` → `UNTIL` (date-only in Graph, so the client must compute a proper UNTIL in UTC or local form), `numbered` → `COUNT`, `noEnd` → no limit.
- RRULE → Graph is **not** possible for:
  - FREQ=HOURLY/MINUTELY/SECONDLY
  - multiple BYMONTHDAY values (e.g. 1st and 15th), negative BYMONTHDAY (except "last day" via a workaround), BYYEARDAY, BYWEEKNO, multiple BYMONTH
  - BYSETPOS outside {1,2,3,4,-1}, mixed numeric BYDAY (e.g. `1MO,3WE`)
  - BYHOUR/BYMINUTE
  - multiple RRULEs, EXRULE, and RDATE (no way to add extra occurrences)

  Mulberry would have to refuse such edits, flatten them into separate events, or write them read-only.
- EXDATE ↔ deleting an occurrence (it shows up in `cancelledOccurrences`). RECURRENCE-ID overrides ↔ PATCHing the occurrence instance (locate via `/events/{master}/instances?startDateTime&endDateTime` or beta `occurrenceId`). The overridden instance is a separate Graph object with its own `id`, `type=exception`, `seriesMasterId`, `originalStart`.
- Because v1.0 delta works on a time-windowed calendarView that returns expanded occurrences, a Mulberry sync engine must:
  - (a) choose a sliding window, e.g. past N months to future M months
  - (b) group returned items by `seriesMasterId`
  - (c) fetch each series master (`/events/{id}`) to rebuild one VEVENT with RRULE + overridden VEVENTs, using `$expand=exceptionOccurrences` and `cancelledOccurrences` for EXDATEs
  - (d) re-request on window shifts

  This is substantially different from CalDAV's per-resource ETag/sync-token model, where the server stores whole iCalendar objects.
- **UID mapping:** use beta `uid` or the series master's v1.0 `iCalUId` as UID. Never use per-occurrence `iCalUId`, which differs per occurrence in v1.0. Graph UIDs are Exchange GOID hex strings, not the UID originally sent in an iMIP invitation, so cross-matching with iTIP messages from other systems needs care. That GOID detail is inferred from the example value and is not verified.
- **Time zones:** CICalendar works with VTIMEZONE/Olson IDs, while Graph wants Windows names (or its IANA alias list) and returns UTC by default. Mulberry needs a Windows↔IANA mapping table; Unicode CLDR `windowsZones.xml` is the usual source, not fetched here. Safest pattern: send `Prefer: outlook.timezone` with the event's Windows zone, or read in UTC and use `originalStartTimeZone` to reconstruct TZID. All-day events must be midnight-to-midnight in one zone (DTSTART;VALUE=DATE).
- **Scheduling:** creating an event with attendees in the organizer's calendar makes Exchange send invitations itself. Responses use `accept`/`decline`/`tentativelyAccept` (with optional comment and sendResponse). Organizer cancellation uses `cancel`. Mulberry's iTIP/iMIP generation must be **disabled** for Graph-backed calendars, or attendees get duplicate invitations. Incoming invitations appear both as `eventMessage` items in the mailbox and as tentative events in the calendar.
- **Lossy fields:**
  - Only one reminder (`reminderMinutesBeforeStart`) vs multiple VALARMs.
  - `showAs` ↔ TRANSP + X-MICROSOFT-CDO-BUSYSTATUS-like values.
  - `sensitivity` ↔ CLASS (personal/confidential don't map cleanly).
  - `categories` must match the user's outlookCategory names.
  - Body is HTML by default (DESCRIPTION is plain text; request `Prefer: outlook.body-content-type="text"`).
  - Attachments are not inline in the event JSON (separate calls). ATTACH by URI → referenceAttachment.
  - Arbitrary X- properties need open extensions or extended properties to survive a round-trip.
- **Licensing:** DavMail is GPL-2.0+ and evolution-ews is LGPL-2.1. Both are good *references* for mapping logic, but copying code into Mulberry depends on Mulberry's own licence (not checked here; GPL code generally can't go into a permissively licensed codebase).
- **Pragmatic short-term option:** DavMail in O365Graph mode can run as a local CalDAV/CardDAV gateway. Mulberry's *existing* CalDAV/CardDAV clients could then talk to Microsoft 365 with no new Mulberry code, at the cost of an external Java process (available on FreeBSD via ports/pkg, not verified this session) and DavMail's own app registration/consent.

### Gaps
- The exact Exchange constraints on modifying exceptions (e.g. an occurrence can't be moved past an adjacent occurrence) and on which RRULE-shaped series Exchange will *accept* via Graph were not verified.
- Whether `events/delta` (beta, unbounded) is planned for v1.0 was not found.
- Whether the Windows-name requirement also applies to `recurrenceTimeZone`, and the full list of accepted names, was not verified beyond the dateTimeTimeZone page.
- I did not check how meeting invitations from external (non-Exchange) organizers map to Graph `uid`/`iCalUId` relative to the original iCalendar UID.
- The KDE/Akonadi Graph calendar status was not researched.
- The `calendar-post-events` page (invitation-sending behaviour on create) was not fetched. The scheduling statements above are inferences from the method list (accept/decline/cancel/forward).

---

## 9. Graph Contacts, directory/GAL, scopes/tokens, consumer accounts, throttling and effort

### Takeaway
Graph personal contacts are a fixed-schema Outlook contact. Fixed slots include home/business/other address, a business/home phones collection plus one mobile, email addresses, IM addresses, birthday, notes and a photo. It is not vCard, so mapping is lossy and needs extensions to round-trip. Sync is per-folder `contacts/delta`. Graph has **no personal contact-group/distribution-list API** (an EWS parity gap still listed for Q3 CY2026) and **no GAL equivalent**. Directory lookup is via `/users` (work/school) or `/people`.

One access token covers one resource. Graph (`graph.microsoft.com`) and IMAP/SMTP (`outlook.office.com`) tokens must be requested separately. A single refresh token can mint both, because refresh tokens aren't bound to a resource. Calendars.ReadWrite, Contacts.ReadWrite and People.Read are user-consentable and available for personal Microsoft accounts.

### Cited Findings
**Contacts API**
- `contact` methods: get, create (in root Contacts or another contact folder), update, delete, permanentDelete, delta ("added, deleted, or updated in a specified folder"), open extensions, and single/multi-value extended properties. Photo via the `photo` relationship ("You can get or set a photo for a contact"). — [Graph v1.0: contact resource (ms.date 2026-02-27)](https://learn.microsoft.com/en-us/graph/api/resources/contact?view=graph-rest-1.0)
- Properties: `givenName`, `middleName`, `surname`, `title`, `generation` (suffix), `nickName`, `initials`, `displayName` (later updates "may cause an automatically generated value to overwrite" it unless it is resent), `fileAs`, `yomiGivenName`/`yomiSurname`/`yomiCompanyName`, `emailAddresses` (collection), plus `primaryEmailAddress`/`secondaryEmailAddress`/`tertiaryEmailAddress`, `businessPhones` and `homePhones` (collections), `mobilePhone` (single), `imAddresses`, `homeAddress`/`businessAddress`/`otherAddress` (one each), `companyName`, `department`, `jobTitle`, `officeLocation`, `profession`, `manager`, `assistantName`, `businessHomePage`, `birthday`, `spouseName`, `children`, `personalNotes`, `categories`, `changeKey`, `parentFolderId`. `id` changes on move unless ImmutableId is used. — [same page](https://learn.microsoft.com/en-us/graph/api/resources/contact?view=graph-rest-1.0)
- Contact folders: `/me/contactFolders`, `/me/contactFolders/{id}/contacts`. Contacts.Read is the least-privileged delegated permission, including for personal accounts. Creating contacts requires Contacts.ReadWrite. No `contactGroup`/distribution-list resource was found in v1.0 or beta. — search summary of [Graph: contactFolder list contacts](https://learn.microsoft.com/en-us/graph/api/contactfolder-list-contacts) and [Outlook personal contacts API overview](https://learn.microsoft.com/graph/outlook-contacts-concept-overview)
- EWS→Graph parity roadmap: "Contact Lists — Access and manage personal contact lists stored in a mailbox" (ETA Q3 CY2026) and "Additional Contact Properties — … available in EWS but not Microsoft Graph" (Q3 CY2026). As of the page's 2026-10-23 update these were still listed as gaps. — [Microsoft Learn: EWS deprecation, parity roadmap](https://learn.microsoft.com/en-us/exchange/clients-and-mobile-in-exchange-online/deprecation-of-ews-exchange-online)
- DavMail 7.0.0 nevertheless returns "contact distribution lists … as contact groups" in Graph mode. The mechanism is not described; it is probably extended properties. — [Help Net Security](https://helpnetsecurity.com/2026/09/22/davmail-7-0-0-microsoft-graph)
- Change notifications are supported for Outlook personal `contact` (`/me/contacts`), with the same 7-day max and webhook-only delivery. — [Graph change notifications](https://learn.microsoft.com/en-us/graph/change-notifications-overview)

**Directory / GAL / people**
- Evolution maintainer (Aug 2026): Graph has no Global Address List equivalent, so apps must use multiple separate address books. — [GNOME Discourse](https://discourse.gnome.org/t/is-microsoft-365-graph-api-expected-to-be-available-in-evolution-3-60-x/36019)
- `People.Read` delegated: AdminConsentRequired No, "available for consent in personal Microsoft accounts". `People.Read.All` delegated: AdminConsentRequired **Yes**. — [Graph permissions reference](https://learn.microsoft.com/en-us/graph/permissions-reference)
- `User.ReadBasic.All` (delegated) is listed as not needing admin consent by a third-party permissions table. This is not verified on Microsoft's page this session. — [Quest KB](https://support.quest.com/nova/kb/4299875/microsoft-permissions-for-dpc)

**Scopes, consent, tokens across resources**
- `Contacts.ReadWrite` delegated: AdminConsentRequired No, "available for consent in personal Microsoft accounts". `Contacts.ReadWrite.Shared` delegated: AdminConsentRequired No. `Calendars.ReadWrite`: No admin consent, personal accounts OK. — [Graph permissions reference](https://learn.microsoft.com/en-us/graph/permissions-reference)
- `Mail.Send` is available for delegated work/school **and** delegated personal accounts. — [Graph: user sendMail](https://learn.microsoft.com/en-us/graph/api/user-sendmail?view=graph-rest-1.0)
- Tenant policy can still restrict user consent even for permissions that don't require admin consent. — [Graph permissions overview (search summary)](https://learn.microsoft.com/en-au/graph/permissions-overview)
- **One resource per access token:** "Access Token requests in MSAL.js are meant to be per-resource-per-scope(s) … cannot be used for accessing resource B of any scope." "When you have to access multiple resources, initiate a separate token request for each." "In case you erroneously pass multiple resources in your token request, the token you will receive will only be issued for the first resource." `extraScopesToConsent` lets one interactive sign-in collect consent for another resource, so its token can later be acquired silently. "consent lives beyond the lifetime of the application". — [MSAL.js: Resources and Scopes (updated 2026-10-23)](https://learn.microsoft.com/en-us/entra/msal/javascript/browser/resources-and-scopes)
- **Refresh token is multi-resource:** "Refresh tokens are bound to a combination of user and client, but aren't tied to a resource or tenant. A client can use a refresh token to acquire access tokens across any combination of resource and tenant where it has permission to do so." — [Microsoft Learn: Refresh tokens](https://learn.microsoft.com/en-us/entra/identity-platform/refresh-tokens)

**Consumer (Outlook.com) accounts**
- Calendars.ReadWrite, Contacts.ReadWrite, People.Read and Mail.Send are documented as consentable for personal Microsoft accounts (see above). `user` change-notification subscriptions are "Not supported for personal Microsoft accounts like outlook.com". Event, contact and message subscriptions carry no such note. Group calendars/attachments/reminders limitations apply to M365 groups only. Consumer access tokens may be encrypted and must be treated as opaque. — [Graph permissions reference](https://learn.microsoft.com/en-us/graph/permissions-reference); [Graph change notifications](https://learn.microsoft.com/en-us/graph/change-notifications-overview); [Device code page warning](https://learn.microsoft.com/en-us/entra/identity-platform/v2-oauth2-device-code)
- `webLink` formats differ: `outlook.office365.com` for work/school vs `outlook.live.com` for Microsoft accounts. That is the same API with different hosts behind it. — [Graph v1.0: event resource](https://learn.microsoft.com/en-us/graph/api/resources/event?view=graph-rest-1.0)

**Throttling, paging, batching**
- Outlook service limits per app+mailbox: 10,000 requests per 10 min, **4 concurrent requests**, 150 MB upload per 5 min. These cover the Calendar API (event, calendar, calendarGroup, attachment, outlookCategory) and the Personal contacts API (contact, contactFolder). In JSON batches Graph sends Outlook at most 4 requests in parallel, and `dependsOn` forces sequential execution. — [Graph throttling limits (updated 2026-11-19)](https://learn.microsoft.com/en-us/graph/throttling-limits)
- Paging and delta use `@odata.nextLink`/`@odata.deltaLink` with opaque tokens and `Prefer: odata.maxpagesize`. — [Graph: delta for events](https://learn.microsoft.com/en-us/graph/delta-query-events); [Graph: delta for messages](https://learn.microsoft.com/en-us/graph/delta-query-messages)

### Inferences
**vCard mapping (Graph contact ↔ Mulberry vCard library)**
- N ↔ surname/givenName/middleName/title/generation. FN ↔ displayName (always resend it). NICKNAME ↔ nickName. ORG ↔ companyName;department. TITLE ↔ jobTitle. ROLE ↔ profession.
- EMAIL ↔ emailAddresses. Exchange historically has 3 email slots, which matches the primary/secondary/tertiary properties, so a vCard with more than 3 EMAILs is lossy.
- TEL: work → businessPhones, home → homePhones, cell → mobilePhone (single). There are **no fax/pager/other TEL types** in v1.0 properties (extended properties or the "Additional Contact Properties" gap).
- ADR ↔ exactly one each of home/business/other. Extra ADRs are lost.
- BDAY ↔ birthday (a UTC timestamp, so watch for date shifting). NOTE ↔ personalNotes. CATEGORIES ↔ categories. PHOTO ↔ `/contacts/{id}/photo/$value`. URL ↔ businessHomePage only. IMPP ↔ imAddresses.
- No ANNIVERSARY/GENDER/GEO/RELATED/KIND=group/X- properties in v1.0. Store unmapped vCard data in an open extension so it survives a round-trip.
- vCard groups (KIND:group / X-ADDRESSBOOKSERVER-MEMBER) cannot be represented through Graph today.

**Tokens for both IMAP/SMTP and Graph from one sign-in**
1. Interactive authorize request with `scope=https://outlook.office.com/IMAP.AccessAsUser.All https://outlook.office.com/SMTP.Send offline_access`. Collect consent for the Graph scopes in the same interaction (MSAL's extraScopesToConsent; in raw protocol, include them in the authorize request's scope list). The token endpoint then returns an access token only for the first resource.
2. Later, call `/token` with `grant_type=refresh_token&scope=https://graph.microsoft.com/Calendars.ReadWrite https://graph.microsoft.com/Contacts.ReadWrite offline_access` to get a Graph token. Call it with the outlook.office.com scopes to get an IMAP/SMTP token.
3. Cache access tokens per resource. Persist whichever rotated refresh token came back last.

Exact raw-protocol behaviour of multi-resource scopes in the authorize request is inferred from the MSAL docs and was not tested.

**Consumer accounts:** the same Graph endpoints and code path should work for Outlook.com calendars/contacts via `/common` or `/consumers`, with fewer features (no groups, no directory `/users`).

**Effort estimate (qualitative), compared with Mulberry's existing CalDAV/CardDAV clients**
- What can be reused:
  - Mulberry's local calendar/address-book model, UI, and the CICalendar/vCard object model.
  - OAuth2 token management, which IMAP/SMTP XOAUTH2 needs anyway.
- New work:
  - (1) An HTTPS/JSON client with OData paging, `Prefer` headers, ImmutableId, ETag/changeKey handling, 429/503 + `Retry-After` handling and a 4-concurrent-request limiter.
  - (2) A sync engine:
    - per-calendar time-windowed calendarView delta plus series-master reconstruction
    - per-folder contacts delta
    - polling, because webhooks need a public endpoint
  - (3) Bidirectional mappers: event JSON ↔ VEVENT (recurrence, exceptions, TZ Windows↔IANA, alarms, attendees, all-day, body HTML/text) and contact JSON ↔ vCard. Each needs a "can't represent" policy.
  - (4) Scheduling integration: suppress client iMIP and use accept/decline/cancel.
  - (5) Folder discovery (calendars, calendarGroups, contactFolders, shared calendars).
- The mappers and the recurrence/exception reconstruction are the bulk of the risk. CalDAV/CardDAV give Mulberry server-stored iCalendar/vCard verbatim; Graph gives a different object model that Mulberry must translate both ways.
- **Rough order:** comparable to, or larger than, writing the original CalDAV + CardDAV clients, because those protocols carry Mulberry's native formats and Graph does not.
- **Lower-effort interim alternative:** use DavMail (O365Graph mode) as a local CalDAV/CardDAV gateway with Mulberry's existing clients (see section 8).

### Gaps
- `Mail.ReadWrite`, `User.ReadBasic.All` and `offline_access` consent rows were not read from the permissions reference (page truncation). Mail.ReadWrite is generally user-consentable but not verified here.
- The JSON `$batch` maximum request count (commonly 20) was not re-verified this session.
- Exact contact email-address cap and the handling of more than 3 addresses in `emailAddresses` were not verified.
- No documentation was found on how DavMail implements Graph contact groups.
- Whether the "Contact Lists" parity item shipped in Graph (beta) after the roadmap's Q3 2026 ETA is not confirmed; the roadmap still listed it as of 2026-10-23.
- Raw-protocol test of a single authorize request carrying scopes for two resources was not performed.
