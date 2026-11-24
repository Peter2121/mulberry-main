# Google (Gmail / Google Workspace): OAuth2 for a third-party desktop IMAP/SMTP/POP + CalDAV/CardDAV client (state as of 2026-11-24)

Notes compiled 2026-11-24. "Live probe" = anonymous TLS connection made on 2026-11-24 to Google's servers to read the pre-authentication CAPABILITY / EHLO / CAPA lists (no credentials used).

## 1. Status of password ("less secure apps") access and app passwords for IMAP/SMTP/POP

### Takeaway
Plain account-password access is gone. Personal Gmail lost it on 2022-05-30. Workspace lost it on 2025-05-01; that date was announced as 2025-03-14 and later postponed. App passwords still exist, but only for personal accounts with 2-Step Verification that aren't on Advanced Protection or security-key-only 2SV. Google says they are "not recommended". For Workspace they are framed as a fallback for scanners and copiers. OAuth2 is the only path a general-purpose client like Mulberry can rely on.

### Cited Findings
- Personal accounts: starting 2022-05-30, Google stopped allowing third-party apps or devices that sign in with only a username and password. This deadline did not apply to Workspace or Cloud Identity customers. Source: Google notice reproduced on a vendor FAQ, so secondary — [D-Link FAQ](https://www.dlink.com/mx/es/support/faq/knowledge/why-can-t-i-send-an-email-from-my-device-using-my-google-account); user reports in [Microsoft Tech Community thread](https://techcommunity.microsoft.com/t5/outlook/gmail-less-secure-apps-being-locked-down/td-p/3259342).
- Workspace timeline from Google's transition article:
  - Summer 2024: new first-time less-secure-app connections were blocked, the admin toggle was removed, and users could no longer toggle IMAP in Gmail settings.
  - Autumn 2024: password-based IMAP/CalDAV/CardDAV/POP profiles pushed via MDM stopped working.
  - 2025-03-14 (as originally announced): "CalDAV, CardDAV, IMAP, SMTP, and POP will no longer work with legacy passwords (basic authentication)."
  - Source: [Google Workspace Admin Help – Transition from less secure apps to OAuth](https://knowledge.workspace.google.com/admin/sync/transition-from-less-secure-apps-to-oauth?hl=en)
- The final Workspace cutoff date: "Starting May 1, 2025, Google Workspace accounts no longer support less secure apps"; apps that require username-and-password sign-in are no longer supported — [Workspace Admin Help – Control access to less secure apps](https://knowledge.workspace.google.com/admin/apps/control-access-to-less-secure-apps?hl=en). The search summary for the Workspace Updates blog indicates the 2025-03-14 date was moved to 2025-05-01 — [Workspace Updates blog, 2023-09 "winding down Google Sync and less secure apps support"](https://workspaceupdates.googleblog.com/2023/09/winding-down-google-sync-and-less-secure-apps-support.html?hl=en).
- Google's recommended action for "Mozilla Thunderbird or other clients": remove the Google account, re-add it, and configure IMAP to use OAuth. Recommended actions for scanners and SMTP devices are OAuth, an alternative method, or an app password — [Transition article](https://knowledge.workspace.google.com/admin/sync/transition-from-less-secure-apps-to-oauth?hl=en).
- On Workspace, office devices (scanners, copiers) can keep using SMTP/IMAP/POP3 "if configured for OAuth" or by creating "an app password for use with the device" — [Control access to less secure apps](https://knowledge.workspace.google.com/admin/apps/control-access-to-less-secure-apps?hl=en).
- App passwords:
  - "App passwords can only be used with accounts that have 2-Step Verification turned on."
  - The option may not appear when 2SV is set up only with security keys, for work, school, or other organization accounts, or for accounts with Advanced Protection.
  - "App passwords aren't recommended and are unnecessary in most cases."
  - No end date is announced (page checked 2026-11).
  - Source: [Google Account Help – Sign in with app passwords](https://support.google.com/accounts/answer/185833?hl=en)
- Advanced Protection: enrolment revokes existing app passwords and blocks apps that use them, and only verified third-party apps can access sensitive data. Source: Google help, via search summary — [Common questions with Advanced Protection Program](https://support.google.com/accounts/answer/7539956?hl=en).
- Live probe on 2026-11-24 of what the servers still advertise:
  - imap.gmail.com:993: `CAPABILITY IMAP4rev1 UNSELECT IDLE NAMESPACE QUOTA ID XLIST CHILDREN X-GM-EXT-1 XYZZY SASL-IR AUTH=XOAUTH2 AUTH=PLAIN AUTH=PLAIN-CLIENTTOKEN AUTH=OAUTHBEARER`
  - smtp.gmail.com, both 587 (after STARTTLS) and 465: `AUTH LOGIN PLAIN XOAUTH2 PLAIN-CLIENTTOKEN OAUTHBEARER XOAUTH`
  - pop.gmail.com:995: `SASL PLAIN XOAUTH2 OAUTHBEARER` (also `USER`)
  - PLAIN, LOGIN, and USER are still advertised. App passwords use them.

### Inferences
- Mulberry can keep its existing PLAIN/LOGIN code paths for app passwords as a manual fallback for personal accounts. It must not treat them as the main Gmail path, because Workspace users, Advanced Protection users, and security-key-only users cannot use them.
- An "incorrect username/password" error from Gmail on PLAIN/LOGIN usually means OAuth is required. It rarely means the password is actually wrong, so the UI should suggest switching to OAuth.

### Gaps
- I found no Google statement giving an end date for app passwords on personal accounts. As of 2026-11 none appears to be announced.
- I found no Google page saying whether Workspace admins can still let ordinary users create app passwords, other than the device-oriented wording.
- PLAIN-CLIENTTOKEN and XOAUTH (the OAuth1-era mechanism) are advertised but have no public Google documentation for third parties.

## 2. OAuth2 flows Google allows for installed/desktop apps; endpoints, parameters, token lifetimes and refresh-token rules

### Takeaway
For a desktop app on FreeBSD or Linux, the only supported interactive flow is authorization code + PKCE (S256), with a loopback redirect to http://127.0.0.1:<random port> or http://[::1]:<port>, opened in the system browser:
- Custom URI schemes are not supported.
- OOB copy-paste (urn:ietf:wg:oauth:2.0:oob) is removed.
- The device flow does not allow any Gmail scope.

Desktop clients are public clients: the client_secret is optional and is not a secret. Refresh tokens are always returned for installed apps.

Refresh tokens die when:
- the user revokes access;
- the token goes 6 months unused;
- the user changes their password (for tokens with Gmail scopes);
- the 100-tokens-per-account-per-client limit pushes it out (oldest first);
- the user granted time-based access and it expired;
- an admin restricts access;
- the GCP session-control length passes;
- after 7 days, if the project is External and in "Testing" status.

### Cited Findings
- Redirect methods for desktop apps:
  - Loopback IP (127.0.0.1 or [::1]) is recommended for macOS, Linux and Windows desktop apps: "start an HTTP listener on a random available port".
  - "localhost" can be used but may cause firewall issues.
  - "Custom URI schemes are no longer supported due to the risk of app impersonation."
  - Manual copy-paste (OOB) is no longer supported. A migration guide is linked; the related Developers Blog post is dated February 2022.
  - Source: [Google Identity – OAuth 2.0 for iOS & Desktop Apps](https://developers.google.com/identity/protocols/oauth2/native-app)
- PKCE: "Always use PKCE (Proof Key for Code Exchange) when requesting authorization codes."
  - code_challenge_method is S256 (recommended) or plain; it defaults to plain if omitted.
  - code_verifier is 43–128 unreserved characters.
  - Source: [native-app](https://developers.google.com/identity/protocols/oauth2/native-app)
- Endpoints:
  - Authorization: `https://accounts.google.com/o/oauth2/v2/auth`
  - Token: `https://oauth2.googleapis.com/token`
  - Revocation: `https://oauth2.googleapis.com/revoke`. Revoking an access token also revokes its refresh token. It returns 200 on success and 400 on error.
  - Source: [native-app](https://developers.google.com/identity/protocols/oauth2/native-app)
- Authorization request parameters: client_id, redirect_uri (must exactly match a registered URI), response_type=code, scope (space-delimited), code_challenge, code_challenge_method, state (CSRF protection; "The OAuth client must prevent CSRF"), and login_hint (an email address or `sub`) — [native-app](https://developers.google.com/identity/protocols/oauth2/native-app).
- Token request (code exchange): client_id, client_secret (optional), code, code_verifier, grant_type=authorization_code, redirect_uri.
- Refresh request: client_id, client_secret (optional), grant_type=refresh_token, refresh_token.
- "Desktop apps are public clients and cannot keep secrets confidential."
- "Refresh tokens are always returned for installed applications."
- Source for the three items above: [native-app](https://developers.google.com/identity/protocols/oauth2/native-app)
- access_type / prompt: the native-app page does not list `access_type` or `prompt` as authorization parameters. `prompt=consent` appears only in a DPoP note about obtaining a new refresh token — [native-app](https://developers.google.com/identity/protocols/oauth2/native-app).
- Token response fields:
  - expires_in: "remaining lifetime of the access token in seconds".
  - refresh_token_expires_in: "only set when the user grants time-based access".
  - "Refresh tokens are valid until the user revokes access or the refresh token expires."
  - Source: [native-app](https://developers.google.com/identity/protocols/oauth2/native-app)
- The overview page gives no fixed access-token lifetime; it says only that access tokens "have limited lifetimes" — [Google Identity – Using OAuth 2.0](https://developers.google.com/identity/protocols/oauth2).
- Token size maximums: authorization code 256 bytes, access token 2048 bytes, refresh token 512 bytes. Sizes may change within these limits — [Using OAuth 2.0](https://developers.google.com/identity/protocols/oauth2).
- Refresh token expiry reasons, all from [Using OAuth 2.0 – Refresh token expiration](https://developers.google.com/identity/protocols/oauth2):
  - user revoked access;
  - unused for six months;
  - user changed password and the token includes Gmail scopes;
  - account exceeded the maximum number of live refresh tokens;
  - time-based access expired;
  - admin set a service in the app's scopes to Restricted (`admin_policy_enforced`);
  - GCP session control (1–24 h): `invalid_grant` with `error_subtype` such as `invalid_rapt`;
  - External user type in "Testing" status: tokens expire after 7 days, unless only userinfo.email, userinfo.profile, and openid are requested.
- Refresh-token limits — [Using OAuth 2.0](https://developers.google.com/identity/protocols/oauth2):
  - "There is currently a limit of 100 refresh tokens per Google Account per OAuth 2.0 client ID."
  - Above the limit, creating a new token silently invalidates the oldest.
  - A separate, larger limit applies per user across all clients.
- Device authorization grant — [Google Identity – OAuth 2.0 for TV and Limited-Input Device Applications](https://developers.google.com/identity/protocols/oauth2/limited-input-device):
  - Endpoints: `https://oauth2.googleapis.com/device/code`, then polling `https://oauth2.googleapis.com/token`.
  - grant_type: `urn:ietf:params:oauth:grant-type:device_code`.
  - Requires the "TVs and Limited Input devices" client type.
  - Errors: authorization_pending (428), slow_down (403), access_denied (403), admin_policy_enforced, invalid_client, invalid_grant, org_internal, rate_limit_exceeded.
  - Allowed scopes are only: email, openid, profile, drive.appdata, drive.file, youtube, youtube.readonly. **No Gmail scope, and https://mail.google.com/ is not allowed.**
- Alpine confirms the device flow is not usable for Gmail ("At this time the Device flow is only supported by Outlook"). Alpine used OOB in 2.22–2.25 and requires 2.26+ for Gmail after Google disabled OOB — [Alpine XOAUTH2 help](https://alpineapp.email/alpine/alpine-info/misc/xoauth2.html).
- Claws Mail's Google template uses the loopback redirect `http://127.0.0.1:8888`. It has empty access_type and prompt, and scope `https://mail.google.com`. The user pastes the redirected URL and the code is extracted from it — [Claws Mail src/oauth2.c](https://git.claws-mail.org/?p=claws.git;a=blob_plain;f=src/oauth2.c;hb=HEAD). In 2022 Claws users hit errors when Google retired OOB — [Claws users list 2022-04](https://lists.claws-mail.org/pipermail/users/2022-April/029870.html).
- Thunderbird (comm-central OAuth2Providers.sys.mjs):
  - Uses the legacy endpoints `https://accounts.google.com/o/oauth2/auth` and `https://www.googleapis.com/oauth2/v3/token`.
  - Has `usePKCE: true` and `useExternalBrowser: true`.
  - Sets no explicit redirectionEndpoint.
  - Embeds the client ID and secret in source.
  - Source: [Thunderbird OAuth2Providers.sys.mjs](https://raw.githubusercontent.com/mozilla/releases-comm-central/master/mailnews/base/src/OAuth2Providers.sys.mjs)

### Inferences
- Recommended implementation for Mulberry:
  1. Bind a TCP listener on 127.0.0.1 with port 0 so the OS picks one, and build `redirect_uri=http://127.0.0.1:<port>`. For Google "Desktop app" clients, any loopback port is accepted without registering each port; I infer this from the "random available port" guidance.
  2. Generate a 32-byte random verifier and use S256, plus a random `state`.
  3. Launch the system browser with xdg-open on FreeBSD/Linux. Never use an embedded webview: Google blocks sign-in from embedded webviews. That policy is general knowledge and was not re-verified in this session.
  4. Receive `GET /?code=...&state=...&scope=...` and return a small HTML "you may close this window" page.
  5. POST to the token endpoint and store the refresh token in the OS keyring or Mulberry's encrypted password store.
- Do not rely on `access_type=offline`. Desktop clients get refresh tokens anyway, but adding it is harmless; I infer this from web-server flow docs that were not fetched here. Use `prompt=consent` only to force a new refresh token, for example when a previous one was lost.
- Treat the access-token lifetime as opaque: use `expires_in` and refresh proactively about 60–300 s before expiry. Google's token responses conventionally give expires_in ≈ 3599 s (1 h), but the docs fetched here don't state a fixed value.
- On `invalid_grant` from the token endpoint, drop the refresh token and run the interactive flow again. If `error_subtype=invalid_rapt` comes back, tell the user that their organization requires re-authentication.
- Every new interactive login creates a new refresh token for the same account and client. If Mulberry re-authorizes repeatedly, for example once per protocol, it consumes the 100-token budget and can silently invalidate other Mulberry installs of the same user. So use one token per account, shared by IMAP, SMTP, CalDAV and CardDAV.

### Gaps
- A fixed access-token lifetime is not stated on the pages fetched. The 1 h figure comes from common practice and example responses, not a fetched citation.
- The deprecation date for custom URI schemes on desktop clients was not found on the fetched page.
- I could not confirm from a primary source whether `http://localhost` is matched as loosely as 127.0.0.1 for Desktop clients.

## 3. Scopes, restricted-scope verification, CASA, unverified-app limits, and how open-source clients cope

### Takeaway
IMAP, SMTP and POP require the single scope `https://mail.google.com/`, which Google classifies as **restricted**. To be used by more than 100 users without an "unverified app" warning, a client ID with this scope needs:
- restricted-scope OAuth verification (about 6 weeks);
- a CASA security assessment by an authorized third-party lab, revalidated every year.

Google charges nothing, but the developer pays the assessor. Published prices are in the hundreds to low thousands of USD per year for Tier 2 / AL1; vendor blogs claim far more. Thunderbird ships an embedded, presumably verified, Google client ID. Most small FOSS clients make every user register their own Google Cloud project instead: Claws Mail, Alpine, NeoMutt's mutt_oauth2.py, and aerc. GNOME Online Accounts ships a shared key and has struggled with it.

### Cited Findings
- `https://mail.google.com/` is restricted and "includes any usage of IMAP, SMTP, and POP3 protocols". Other restricted groups are Drive, Fit, Chat, Data Portability, Photos Ambient, and Health — [Google Cloud Help – Restricted Scopes](https://support.google.com/cloud/answer/13464325?hl=en).
- Google's FAQ on IMAP/SMTP — [Google Cloud Help – OAuth verification FAQ](https://support.google.com/cloud/answer/13463817?hl=en):
  - Q "What if my app is using IMAP or SMTP? Do I need to submit for verification?" A: "Yes, because IMAP and SMTP usage requires using https://mail.google.com/".
  - The scope "should only be requested if your application also needs to immediately and permanently delete threads and messages"; "all other actions can be performed with less permissive scopes."
  - "using the broad access https://mail.google.com/ scope just for sending emails with the SMTP protocol violates the minimum scope policy". Such apps should "use the sensitive https://www.googleapis.com/auth/gmail.send scope instead", which means the Gmail REST API, not SMTP.
- Narrower Gmail scopes exist only for the Gmail REST API: gmail.readonly, gmail.modify, gmail.send, gmail.compose, gmail.metadata, gmail.labels, gmail.insert, gmail.settings.* — [Google OAuth 2.0 Scopes](https://developers.google.com/identity/protocols/oauth2/scopes). The XOAUTH2 doc says "The scope for IMAP, POP, and SMTP access is https://mail.google.com/" — [Gmail XOAUTH2 protocol](https://developers.google.com/workspace/gmail/imap/xoauth2-protocol).
- Verification:
  - Sensitive and restricted scopes require OAuth app verification.
  - Restricted-scope apps must "complete 're-verification' annually".
  - Source: [Google Cloud Help – OAuth App Verification](https://support.google.com/cloud/answer/13463073?hl=en)
- Expected review times are 2–3 business days for brand verification, 10 business days for sensitive scopes, and 6 weeks for restricted scopes ("not guaranteed") — [FAQ](https://support.google.com/cloud/answer/13463817?hl=en).
- Security assessment — [Google Cloud Help – Security Assessment](https://support.google.com/cloud/answer/13465431?hl=en):
  - It uses the App Defense Alliance CASA framework, ending in a "Letter of Validation" from the assessor.
  - Tiers are AL1 or AL2, assigned by risk (user count, scopes, other signals). An app validated at AL2 stays at AL2.
  - "All applications must be revalidated every year."
  - The page gives no explicit exemption for apps that keep data only on the user's device.
- Cost — [FAQ](https://support.google.com/cloud/answer/13463817?hl=en):
  - "Google does not charge the developer any fees for security assessment"; the price "is agreed on between the developer and the assessor without any involvement from Google".
  - Reassessment must be "a complete test of your application" even without changes.
  - On Tier 2 self-scanning: "You can choose to reachout to the CASA authorized assessor and pay for the assessment."
- Cost estimates (secondary sources, treat with caution):
  - Reported Google figure of about $500–$4,500.
  - TAC Security's 2026 list prices of about $675–$855 for AL1 and about $5,400 for AL2.
  - Vendor blogs claiming $15,000–$75,000, attributed to unnamed developers.
  - Sources: search synthesis of [bright-softwares.com "The $50,000 Gmail Add-on Myth"](https://bright-softwares.com/blog/en/google-workspace/the-50000-gmail-add-on-myth-what-google-s-casa-certification-really-costs), [deepstrike.io CASA 2026](https://deepstrike.io/blog/google-casa-security-assessment-2025), and [Medium "The $50K Email API Nightmare"](https://medium.com/reversebits/the-50k-email-api-nightmare-why-your-simple-gmail-integration-just-became-a-compliance-hell-6071300b09b4). I did not verify these figures against assessor price lists.
- 100-user cap: "unverified apps that are accessing restricted or sensitive scopes have a 100 new-user cap restriction"; "the user cap applies over the entire lifetime of the project, and it cannot be reset or changed." Once the cap is exhausted, sign-in is disabled for new users — [FAQ](https://support.google.com/cloud/answer/13463817?hl=en).
- When verification is not needed — [Google Cloud Help – When is verification not needed](https://support.google.com/cloud/answer/13464323?hl=en):
  - Personal mode: used only by the creator's own account, no unverified warning, and the project cannot be converted or shared later.
  - Limited audience: fewer than 100 users who click through the unverified-app warning.
  - Development, testing or staging: unverified screen plus the 100-user cap.
  - Service-owned data only.
  - Internal use: only within the owner's Workspace or Cloud Identity org; the project must be org-owned.
  - Apps an admin has trusted or Marketplace-installed.
- Thunderbird — [OAuth2Providers.sys.mjs](https://raw.githubusercontent.com/mozilla/releases-comm-central/master/mailnews/base/src/OAuth2Providers.sys.mjs):
  - Ships client ID `406964657835-aq8lmia8j95dhl1a2bvharmfk3t1hgqj.apps.googleusercontent.com` with its secret embedded in source.
  - Requests `https://mail.google.com/`, `https://www.googleapis.com/auth/carddav`, and `https://www.googleapis.com/auth/calendar` together when a Google mail account is added.
  - The file warns: "Don't copy these values for your own application - register one for yourself!"
- Alpine: "Google does not allow me to get a client-id and client-secret for Alpine that will work for all users, but will ask every user to get their own client-id and client-secret." — [Alpine XOAUTH2 help](https://alpineapp.email/alpine/alpine-info/misc/xoauth2.html).
- Claws Mail ships no Google client ID: `oa2_client_id=` is empty and `oa2_client_secret=.`. Users enter their own — [Claws oauth2.c](https://git.claws-mail.org/?p=claws.git;a=blob_plain;f=src/oauth2.c;hb=HEAD). Users report that keeping the consent screen in "Testing" worked while switching to "In Production" broke access; one complained about a FAQ step requiring Search Console domain verification. Sources: [Claws users list 2025-01](https://lists.claws-mail.org/pipermail/users/2025-January/033662.html) and the [Arch forum](https://bbs.archlinux.org/viewtopic.php?id=276948), via search summary.
- NeoMutt mutt_oauth2.py ships no client ID. The user creates a project, enables the Gmail API, creates an "External" or "Internal" consent screen with scope `https://mail.google.com/` and themselves as a test user, then creates a "Desktop app" credential. It supports authcode, localhostauthcode and devicecode flows, warning that "some flows might not work" — [NeoMutt contrib/oauth2 README](https://raw.githubusercontent.com/neomutt/neomutt/main/contrib/oauth2/README.md).
- aerc takes `imaps+oauthbearer` / `smtps+oauthbearer` URLs with `token_endpoint`, `client_id`, `client_secret` and `scope` parameters. When token_endpoint is set, the configured password is treated as a refresh token. Users get that token through external tools such as Google's oauth2.py or oauth2.dance — [aerc Gmail guide (tilde.club)](https://tilde.club/~djhsu/aerc-gmail-oauth2.html); [aerc lib/xoauth2.go (Debian sources)](https://sources.debian.org/src/aerc/0.21.0-2/lib/xoauth2.go).
- GNOME Online Accounts (used by Evolution): a 2019 GNOME maintainer noted the GNOME key is necessarily public because the project is open source, and that third-party apps cannot be allowed to use it because Google wants to know who uses the key — [desktop-devel-list 2019-02](https://mail.gnome.org/archives/desktop-devel-list/2019-February/msg00042.html); [LWN coverage](https://lwn.net/Articles/779620). Google sign-in in GOA 3.52.4 broke with "Bad Request" when getting the access token (upstream issue 411) — [Arch GOA packaging issue](https://gitlab.archlinux.org/archlinux/packaging/packages/gnome-online-accounts/-/issues/2).

### Inferences
- For Mulberry, three realistic options:
  - (a) A project-owned, verified client ID. This means restricted-scope verification plus an annual CASA assessment paid by whoever owns the project; a small open-source project would likely pay low thousands of USD per year at published lab prices, plus the effort of meeting the requirements. Someone has to own the Google Cloud project, the privacy policy, and a verified home-page domain.
  - (b) Bring-your-own client ID, as Alpine, Claws, NeoMutt and aerc do. Per user it is free.
  - (c) An unverified shared client ID. Users see the warning screen, and the project burns through its lifetime cap of 100 users — not viable for distribution.
- Under option (b), the user's own project has to be "In production" or the user is stuck with 7-day refresh tokens. In production but unverified, it works for up to 100 users, each clicking through the unverified warning; for a single user, that is fine.
- Option (b) suits Workspace users particularly well: they can create an "Internal" project with no verification and no 7-day expiry, or their admin can mark an existing client ID as Trusted.
- Requesting calendar and CardDAV scopes adds sensitive-scope verification on top of the restricted Gmail scope. I infer the sensitivity class from Google's scope lists; it was not confirmed on a fetched page.
- Thunderbird's embedded ID must not be reused. Mozilla's own source says so, and it would breach Google's terms.

### Gaps
- Nowhere did I find a Google statement exempting purely client-side desktop apps (no developer server receives user data) from CASA. Both the Security Assessment page and the FAQ are silent on it.
- Mozilla's own verification and CASA status, and who pays for it, were not found in a primary source.
- Whether Evolution/GOA currently holds a verified Google client with the restricted Gmail scope (2025–2026) was not found.
- Exact 2026 CASA assessor prices weren't verified from primary price lists.

## 4. Is the desktop client secret secret? Bring-your-own client ID

### Takeaway
No. Google states that desktop/installed-app secrets are "obviously not treated as a secret", that desktop apps are public clients, and that the secret is optional in token requests. Embedding it in open-source code is normal; Thunderbird does so. Bring-your-own client ID costs the user about 10–15 minutes in the Google Cloud console, but most end users find it confusing. The consent screen must be published, or tokens expire every 7 days.

### Cited Findings
- Installed apps get a client ID and, in some cases, a secret embedded in the source; "In this context, the client secret is obviously not treated as a secret." — [Using OAuth 2.0](https://developers.google.com/identity/protocols/oauth2).
- "Desktop apps are public clients and cannot keep secrets confidential"; `client_secret` is optional in both the code exchange and the refresh request — [native-app](https://developers.google.com/identity/protocols/oauth2/native-app).
- Thunderbird embeds its Google client secret in public source — [OAuth2Providers.sys.mjs](https://raw.githubusercontent.com/mozilla/releases-comm-central/master/mailnews/base/src/OAuth2Providers.sys.mjs).
- BYO registration steps, as documented by clients:
  - Alpine: create a project; consent screen "Internal" for a Workspace account or "External" for personal; app name and support email; add yourself as a test user; create an OAuth client ID of type "Desktop app"; paste the client ID and secret into Alpine — [Alpine: Registering Alpine in Gmail](https://alpineapp.email/alpine/alpine-info/misc/RegisteringAlpineinGmail.html).
  - NeoMutt: same steps plus "enable Gmail API" — [NeoMutt README](https://raw.githubusercontent.com/neomutt/neomutt/main/contrib/oauth2/README.md).
  - CalDAV additionally needs "enable the CalDAV API" in the API Library, and "You may be prompted to enable billing" — [Google CalDAV guide](https://developers.google.com/workspace/calendar/caldav/v2/guide).
- With External user type and "Testing" status, refresh tokens expire after 7 days — [Using OAuth 2.0](https://developers.google.com/identity/protocols/oauth2).
- Apps used only by the creator can use "Personal mode" with no unverified warning; this choice is one-way — [When is verification not needed](https://support.google.com/cloud/answer/13464323?hl=en).

### Inferences
- Mulberry's account preferences should expose editable Client ID and Client Secret fields per provider, defaulting to a project-shipped ID if one exists. Claws exposes `oauth2.google.clientid` and `oauth2.google.clientsecret`.
- Mulberry should also document the BYO steps: enable the Gmail API, plus the CalDAV and People/CardDAV APIs if needed; create a Desktop app client; publish the app, or use Internal for Workspace.
- If the secret is left blank, the token endpoint should still work for Desktop clients, because the docs list it as optional. Test this before relying on it.

### Gaps
- I found no Google policy text that forbids or explicitly permits shipping a desktop client secret in open-source code. The "not treated as a secret" wording implies it is tolerated.
- Whether the "Personal mode" option is available for External/consumer Gmail projects (versus Workspace) was not clear from the page summary.

## 5. SASL on imap.gmail.com / smtp.gmail.com / pop.gmail.com: XOAUTH2, OAUTHBEARER, errors, expiry

### Takeaway
XOAUTH2 is supported and documented by Google on all three servers. OAUTHBEARER (RFC 7628) is advertised by all three in a live probe on 2026-11-24, but Google's XOAUTH2 doc does not mention it. On failure, the server sends a base64 JSON challenge (`{"status":"401","schemes":"bearer","scope":"https://mail.google.com/"}`). The client must answer with an empty response, then gets a tagged NO or a 5xx code. Refresh the token proactively by expires_in and retry once after refreshing on auth failure.

### Cited Findings
- Format: `base64("user=" {User} "^Aauth=Bearer " {Access Token} "^A^A")`, where ^A is \001 and base64 follows RFC 4648 — [Gmail XOAUTH2 protocol](https://developers.google.com/workspace/gmail/imap/xoauth2-protocol).
- Commands — [XOAUTH2 doc](https://developers.google.com/workspace/gmail/imap/xoauth2-protocol):
  - IMAP: `A01 AUTHENTICATE XOAUTH2 <base64>`, sending the initial response in the same line, which relies on SASL-IR.
  - SMTP: `AUTH XOAUTH2 <base64>`.
  - POP: `AUTH XOAUTH2 <base64>`.
  - Each command must be a single line.
- Errors: the server returns a challenge `base64({JSON})` with `status`, `schemes` and `scope`, for example status "401", scheme "bearer", scope "https://mail.google.com/". "The SASL protocol requires clients to send an empty response to this challenge." — [XOAUTH2 doc](https://developers.google.com/workspace/gmail/imap/xoauth2-protocol).
- The XOAUTH2 page does not mention OAUTHBEARER, nor token expiry or refresh — [XOAUTH2 doc](https://developers.google.com/workspace/gmail/imap/xoauth2-protocol).
- Live probe on 2026-11-24 (see section 1): IMAP advertises `AUTH=XOAUTH2 … AUTH=OAUTHBEARER` and `SASL-IR`; SMTP on 465/587 advertises `AUTH … XOAUTH2 … OAUTHBEARER XOAUTH`; POP advertises `SASL PLAIN XOAUTH2 OAUTHBEARER`.
- aerc uses OAUTHBEARER against Gmail (`imaps+oauthbearer`, `smtps+oauthbearer`) — [aerc Gmail guide](https://tilde.club/~djhsu/aerc-gmail-oauth2.html).
- Hostnames:
  - Thunderbird maps imap.gmail.com, pop.gmail.com, smtp.gmail.com and smtp-relay.gmail.com, plus the googlemail.com equivalents, to the `https://mail.google.com/` scope — [OAuth2Providers.sys.mjs](https://raw.githubusercontent.com/mozilla/releases-comm-central/master/mailnews/base/src/OAuth2Providers.sys.mjs).
  - The live probe used imap.gmail.com:993, smtp.gmail.com:465/587 and pop.gmail.com:995.
- The SMTP server's pre-auth error text points to `https://support.google.com/accounts/troubleshooter/2402620` (live probe).

### Inferences
- Implementation details for Mulberry:
  - IMAP: use SASL-IR when `SASL-IR` is in CAPABILITY. Otherwise send `AUTHENTICATE XOAUTH2`, wait for `+ `, then send the base64. On a `+ <base64-json>` challenge, send an empty line (`\r\n`), then read the tagged `NO`.
  - SMTP: on a `334 <base64-json>` reply, send an empty line, then read the 535 error.
  - POP: same pattern as SMTP, with `+ <base64-json>` and then `-ERR`.
- Prefer XOAUTH2, the documented one, for Gmail; OAUTHBEARER can be a generic fallback for other providers. OAUTHBEARER's GS2 format is `n,a=user@example.com,^Ahost=…^Aport=…^Aauth=Bearer <token>^A^A`, per RFC 7628, not fetched here.
- Detecting expiry:
  - Track the expiry time from `expires_in` and refresh before each new connection or AUTH when within a margin.
  - If AUTH fails and the decoded JSON says `"status":"401"`, refresh once and retry.
  - If the refresh itself returns `invalid_grant`, start an interactive re-login.
  - Already-authenticated long-lived IMAP sessions (IDLE) are not dropped when the access token expires. I infer this from common client behaviour; not verified.
- The "user=" value must be the account email address. For Workspace aliases, use the primary address. This is an inference, not verified.

### Gaps
- No Google primary doc for OAUTHBEARER on Gmail; support is shown only by the live CAPABILITY probe.
- The exact IMAP NO / SMTP 535 / POP -ERR texts after an expired token weren't captured. That would need a real token.
- Whether Gmail closes authenticated IMAP sessions when the token or grant is revoked: no source found.

## 6. CalDAV / CardDAV with the same OAuth tokens

### Takeaway
Both CalDAV and CardDAV are OAuth2-only; Basic auth returns 401. A single authorization can request `https://mail.google.com/`, `https://www.googleapis.com/auth/calendar` and `https://www.googleapis.com/auth/carddav` together, as Thunderbird does, and the same token then works for all protocols. The calendar and CardDAV scopes are not restricted, so a client that needs only calendar and contacts avoids CASA, though sensitive-scope verification is still needed (inferred). Both services must be enabled in the Google Cloud project that owns the client ID.

### Cited Findings
- CalDAV — [Google Calendar CalDAV guide](https://developers.google.com/workspace/calendar/caldav/v2/guide):
  - Endpoints: principal `https://apidata.googleusercontent.com/caldav/v2/CALENDAR_ID/user` and calendar collection `https://apidata.googleusercontent.com/caldav/v2/CALENDAR_ID/events`. CALENDAR_ID is the email address for the primary calendar.
  - The old `https://www.google.com/calendar/dav` "is deprecated and no longer supported".
  - OAuth 2.0 over HTTPS is required; Basic auth returns 401.
  - The CalDAV API must be enabled in the Cloud project, and you "may be prompted to enable billing".
  - Not supported: VTODO, VJOURNAL, free-busy-query, LOCK/UNLOCK/COPY/MOVE/MKCOL/MKCALENDAR, If* headers except If-Match, arbitrary WebDAV properties, RFC 3744 ACL, and AUDIO alarms. The scheduling inbox is always empty; invitations go into the events collection.
  - Quota is the same as the Calendar API. Updated limits apply to new Cloud projects from 2026-05-01; projects active between 2025-11 and 2026-04 keep their previous quotas.
- CardDAV — [Google People API – CardDAV](https://developers.google.com/people/carddav):
  - Discovery: `https://www.googleapis.com/.well-known/carddav`.
  - Address book: `https://www.googleapis.com/carddav/v1/principals/userEmail/lists/default`. Don't hard-code it.
  - OAuth 2.0 only; "Google does not support any other authentication method". Basic auth returns 401.
  - Not supported: LOCK/UNLOCK/COPY/MOVE/MKCOL, arbitrary properties, RFC 3744. Not all REPORT types are implemented.
- Scopes used by Thunderbird: CardDAV `https://www.googleapis.com/auth/carddav` (host www.googleapis.com) and CalDAV `https://www.googleapis.com/auth/calendar` (host apidata.googleusercontent.com) — [OAuth2Providers.sys.mjs](https://raw.githubusercontent.com/mozilla/releases-comm-central/master/mailnews/base/src/OAuth2Providers.sys.mjs).
- Calendar scopes listed by Google: `calendar` (full, including permanent deletion), `calendar.events`, `calendar.readonly` — [OAuth 2.0 Scopes](https://developers.google.com/identity/protocols/oauth2/scopes).
- Neither restricted-scope group (Gmail, Drive, Fit, Chat, Data Portability, Photos Ambient, Health) includes Calendar or Contacts — [Restricted Scopes](https://support.google.com/cloud/answer/13464325?hl=en).

### Inferences
- Request all needed scopes in one consent and keep one refresh token per account. Incremental authorization is not supported for installed apps (section 7), so adding calendar later means a new full consent and a new refresh token.
- Check the `scope` field of the token response. With granular consent, a user may untick calendar or contacts; disable those features rather than failing.
- Requesting the CardDAV scope probably needs the "CardDAV API" or People API enabled in the project. The CardDAV page says only "registered on DevConsole".

### Gaps
- `https://www.googleapis.com/auth/carddav` was not shown in the visible part of the scopes page, and its sensitive/restricted classification wasn't confirmed. The same goes for the calendar scope's "sensitive" status.
- Whether CalDAV supports `/.well-known/caldav` discovery: not stated on the page.

## 7. Google-specific gotchas: Workspace admin controls, Advanced Protection, account chooser, login_hint, incremental and granular consent

### Takeaway
Workspace admins can mark a client ID Trusted, Limited, Specific Google data, or Blocked. They can also block all unconfigured third-party apps, or mark Gmail as a Restricted service, which revokes tokens of untrusted apps. A Mulberry user in such an org needs the admin to trust Mulberry's client ID or the user's BYO ID. Advanced Protection allows only verified apps. Incremental authorization is not supported for installed apps. Granular consent means the app must check the granted scopes. login_hint pre-selects the account.

### Cited Findings
- Workspace app access control — [Workspace Admin Help – Control which third-party & internal apps access Google Workspace data](https://knowledge.workspace.google.com/admin/apps/control-which-apps-access-google-workspace-data?hl=en):
  - Access levels: "Trusted" (all services, including restricted), "Limited" (only unrestricted services), "Specific Google data" (only the scopes the admin specifies), and "Blocked".
  - Admins configure an app by name or client ID.
  - "Unconfigured third-party apps" options: "Allow users to access any third-party apps" (default); "Allow users to access third-party apps that only request basic info needed for Sign in with Google"; "Don't allow users to access any third-party apps".
  - When a service (e.g. Gmail) is set to Restricted: "any previously installed apps that you haven't trusted stop working, and tokens are revoked".
  - Workspace for Education can set separate rules for users under 18.
  - Changes take "up to 24 hours".
  - Page dated "Last updated 2026-11-24 UTC".
- A token for a service the admin has restricted fails with `admin_policy_enforced` — [Using OAuth 2.0](https://developers.google.com/identity/protocols/oauth2). The device-flow error list also includes `admin_policy_enforced` (400) and `org_internal` (403) — [limited-input-device](https://developers.google.com/identity/protocols/oauth2/limited-input-device).
- Session control: admins can force re-authentication every 1–24 h, which yields `invalid_grant` with `error_subtype` `invalid_rapt` — [Using OAuth 2.0](https://developers.google.com/identity/protocols/oauth2).
- Advanced Protection: only verified third-party apps can access sensitive data, and app passwords are revoked or blocked (Google help, via search summary) — [Advanced Protection FAQ](https://support.google.com/accounts/answer/7539956?hl=en). A developer forum report says Advanced Protection blocks Gmail OAuth for third-party apps, but the same poster then said the customer wasn't enrolled (anecdotal) — [chromium-extensions group thread](https://groups.google.com/a/chromium.org/g/chromium-extensions/c/pCRfJwM-9B8).
- Since 2019, unverified apps accessing Gmail data face extra restrictions, and admins can allowlist (trust) apps to give them access to restricted scopes — [Workspace Updates 2019-06 third-party apps Gmail data](https://workspaceupdates.googleblog.com/2019/06/third-party-apps-gmail-data.html); [Authorize unverified third-party apps](https://knowledge.workspace.google.com/admin/apps/authorize-unverified-third-party-apps?authuser=8).
- "Incremental authorization is not supported for installed apps or devices." — [native-app](https://developers.google.com/identity/protocols/oauth2/native-app).
- Granular consent: users may not grant every requested scope, and "Your app must verify which scopes were actually granted" via the `scope` field of the token response. Exceptions are domain-wide delegation and Trusted apps — [native-app](https://developers.google.com/identity/protocols/oauth2/native-app).
- `login_hint`: an email address or `sub` identifier — [native-app](https://developers.google.com/identity/protocols/oauth2/native-app).
- The `gmail.imap_admin` scope exists only for Workspace domain-wide delegation, not for user clients — [XOAUTH2 doc](https://developers.google.com/workspace/gmail/imap/xoauth2-protocol).
- A user's password change revokes refresh tokens that carry Gmail scopes — [Using OAuth 2.0](https://developers.google.com/identity/protocols/oauth2).

### Inferences
- Always pass `login_hint=<configured email>` so the browser's account chooser pre-selects the right account; users often have several Google accounts signed in.
- After the token exchange, verify which account actually signed in, because the user may pick another account in the chooser. Either request `openid email` and decode the id_token, or compare against what IMAP accepts. Adding `openid email` costs nothing in verification terms, as these are non-sensitive scopes.
- Map errors to clear UI messages:
  - `admin_policy_enforced` / `org_internal`: "your Workspace admin must allow this app (client ID …)".
  - `invalid_rapt`: "your organization requires you to sign in again".
  - `invalid_grant`: "access was revoked or expired; please sign in again".
  - `access_denied`: "permission not granted".
- Show the client ID in Mulberry's UI so Workspace users can give it to their admin for allowlisting.

### Gaps
- The exact user-facing browser error text ("Access blocked: … has not completed the Google verification process", "Error 400: admin_policy_enforced") was not captured from a primary page.
- Whether Advanced Protection accounts can authorize a BYO, unverified, personal-mode client with the Gmail scope is unclear; the sources cover only "verified apps".
