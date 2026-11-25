# Registering a Microsoft OAuth2 application for testing Mulberry

*Written 2026-11-25 for the OAuth2 work (`OAUTH2-PLAN.md`, tasks 0.2 and
0.3). One registration serves both test accounts: personal (Outlook.com,
Hotmail, Live) and work (Microsoft 365). The Entra admin center changes
often; points not confirmed on an official page are marked **(unverified)**.*


> **Application name.** This guide uses "Mulberry (test)" as the registered
> name. The project will later continue as a fork under another name, with the
> same copyright and licence. The name users see on the provider's consent page
> comes from this registration, not from the code, so a fork registers its own
> application under its own name. The paths `~/.config/mulberry/` and
> `~/.mulberry/` stand for the application's configuration folder.

## What you get

| Service | Server | Scope requested by Mulberry |
|---|---|---|
| IMAP | `outlook.office365.com:993` (TLS), for both account kinds | `https://outlook.office.com/IMAP.AccessAsUser.All` |
| POP3 | `outlook.office365.com:995` (TLS) | `https://outlook.office.com/POP.AccessAsUser.All` |
| SMTP, work | `smtp.office365.com:587` (STARTTLS) | `https://outlook.office.com/SMTP.Send` |
| SMTP, personal | `smtp-mail.outlook.com:587` (STARTTLS) | `https://outlook.office.com/SMTP.Send` |
| Refresh tokens, identity | — | `offline_access`, `openid`, `email`, `profile` |

- **Mechanism:** the servers offer only SASL **XOAUTH2**, not OAUTHBEARER
  (checked on 2026-11-24).
- **Endpoints:**
  - authorization: `https://login.microsoftonline.com/common/oauth2/v2.0/authorize`;
  - token: `https://login.microsoftonline.com/common/oauth2/v2.0/token`;
  - device code: `https://login.microsoftonline.com/common/oauth2/v2.0/devicecode`.
- **Tenants:** `common` accepts both account kinds. `consumers` accepts only
  personal accounts, and `organizations` or a tenant ID only work accounts.
- **Graph:** calendar and contacts (`Calendars.ReadWrite`,
  `Contacts.ReadWrite`, `User.Read`, `Mail.Send`) are a later plan. They can be
  added to the same registration then.

## Step 0: where to register (a directory is required)

A personal Microsoft account **cannot register applications outside a
directory any more**: the portal answers "The ability to create applications
outside of a directory has been deprecated". This has applied since about
June 2024 and was still the case in 2026 (**unverified**: Microsoft Q&A and
GitHub reports, no Learn page). Choose one:

| Option | Conditions | Remarks |
|---|---|---|
| A. The work tenant | Allowed if "Users can register applications" is on (the default), or if an admin gives you the **Application Developer** role or creates the app for you ([delegate app roles](https://learn.microsoft.com/en-us/entra/identity/role-based-access-control/delegate-app-roles)) | Simplest if allowed. The app belongs to your employer's tenant, so ask first. Error if forbidden: "You don't have permission to register applications in the … directory" |
| **B. Azure free account** with the personal Microsoft account (chosen for the tests, 2026-11-25: done in a day, while an approval in the work tenant can take several days) | Phone, a credit or debit card (a small temporary hold; prepaid cards refused), a Microsoft account ([Azure free account](https://azure.microsoft.com/en-us/pricing/purchase-options/azure-account)) | Creates a **Default Directory** where apps can be registered. The $200 credit ends after 30 days and the paid services stop, but the Entra directory itself is free and the registration keeps working (**unverified**) |
| C. Microsoft 365 Developer Program sandbox | Visual Studio Professional/Enterprise subscribers, some partner programmes and support customers only ([FAQ](https://learn.microsoft.com/en-us/office/developer-program/microsoft-365-developer-program-faq)) | Not available to a plain developer |

Whichever directory you use, the registration below accepts **both** account
kinds. Signing in with the personal account works even if the app lives in
the work tenant, and the reverse is also true.

## Step 1: register the application

[Quickstart](https://learn.microsoft.com/en-us/entra/identity-platform/quickstart-register-app):

1. Sign in to <https://entra.microsoft.com> with an account of the chosen
   directory. If you have several directories, use the **Settings** (gear)
   icon to switch.
2. **Entra ID → App registrations → New registration**.
3. **Name:** `Mulberry (test)`.
4. **Supported account types:** **"Any Entra ID Tenant + Personal Microsoft
   accounts"**. Older wording: "Accounts in any organizational directory and
   personal Microsoft accounts". The manifest value is
   `AzureADandPersonalMicrosoftAccount`.
5. Leave the redirect URI empty here; newer versions of the page no longer
   show it. Click **Register**.
6. On the **Overview** page, copy the **Application (client) ID**.
   - The **Directory (tenant) ID** is needed only for the admin-consent URL
     (step 5).
   - **Do not create a client secret:** Mulberry is a public client, and
     "Public clients … must not use secrets".

## Step 2: redirect URIs

[Add a redirect URI](https://learn.microsoft.com/en-us/entra/identity-platform/how-to-add-redirect-uri),
[redirect URI rules](https://learn.microsoft.com/en-us/entra/identity-platform/reply-url):

1. App → **Manage → Authentication**. On the **Redirect URI configuration**
   tab, click **Add Redirect URI** and choose the tile **Mobile and desktop
   applications**.
2. Add a **custom redirect URI**: `http://localhost` (exactly that, no port, no
   path), then **Configure**.
3. **Optional, `http://127.0.0.1`:** Microsoft recommends it over `localhost`,
   but the portal text box refuses it. It can only be added through the
   manifest:
   - App → **Manifest**. The current manifest is in Microsoft Graph format,
     where desktop redirects are in `publicClient.redirectUris`:
     ```json
     "publicClient": {
         "redirectUris": [ "http://localhost", "http://127.0.0.1" ]
     }
     ```
   - Save.
   - Whether this editor accepts `127.0.0.1`, and whether the port is then
     ignored as for `localhost`, is **unverified**. Microsoft says the port is
     ignored "only for localhost". Decision D5 of the plan already chose
     `http://localhost`; record the result anyway (plan section 8,
     "Registrations and accounts"), in case `127.0.0.1` is wanted later.

Rules that matter for Mulberry:
- The **port is ignored** when matching `localhost` redirects. Mulberry adds
  its random port at run time; do not register URIs that differ only by port.
- `[::1]` is not supported. A browser may still resolve `localhost` to ::1, so
  Mulberry listens on both 127.0.0.1 and ::1 (plan task 1.5).

## Step 3: allow public client flows (for the device code)

App → **Authentication** → **Allow public client flows: Yes** → **Save**.
The setting is under *Advanced settings* in older layouts and on a *Settings*
tab in newer ones (**unverified**). The manifest equivalent is
`"isFallbackPublicClient": true`.

The browser sign-in with PKCE does not need this; the device-code sign-in
does (plan tasks 1.7 and 6.1).

## Step 4: API permissions

App → **API permissions → Add a permission → Microsoft Graph → Delegated
permissions**, and tick:

- `IMAP.AccessAsUser.All`
- `POP.AccessAsUser.All`
- `SMTP.Send`
- `offline_access`, `openid`, `email`, `profile`

Then click **Add permissions**. These permissions are listed under Microsoft
Graph even though the scopes Mulberry requests are on `outlook.office.com`.
All of them are "available for consent in personal Microsoft accounts"
([permissions reference](https://learn.microsoft.com/en-us/graph/permissions-reference)).

Do **not** add the *application* permissions `IMAP.AccessAsApp`,
`POP.AccessAsApp` or `SMTP.SendAsApp` (under "Office 365 Exchange Online").
They are for unattended services, not for a mail client.

## Step 5: consent

### Personal account

The first sign-in shows a consent page that lists the permissions; accept it.
Nothing else is needed.

### Work account

Since 2025, new and many existing tenants use Microsoft's managed consent
policy ("Let Microsoft manage your consent settings"). Under it, **users
cannot consent themselves to `IMAP.AccessAsUser.All` and
`POP.AccessAsUser.All`** ([consent policies](https://learn.microsoft.com/en-us/entra/identity/enterprise-apps/manage-app-consent-policies)).
`SMTP.Send` and `offline_access` are not restricted. Only a fixed list of mail
clients is exempt: Apple Mail, Spark, eM Client, Samsung and Android mail, and
Thunderbird. Expect **"Need admin approval"** at the first sign-in.

An administrator of the work tenant grants consent once, in one of two ways:

1. **Through the portal.** This works only if the app is registered in the
   work tenant (option A): App → **API permissions → Grant admin consent for
   <tenant> → Yes**. Not possible for the tests, whose app is in the Azure
   free directory (option B).
2. **Through the admin-consent URL.** This works for an app registered
   anywhere, and is the way for the tests. Send the URL to the administrator
   with your work tenant ID (or domain) and the application (client) ID filled
   in:
   ```
   https://login.microsoftonline.com/<work tenant ID or domain>/v2.0/adminconsent
       ?client_id=<Application (client) ID>
       &redirect_uri=http://localhost
       &scope=https://outlook.office.com/IMAP.AccessAsUser.All https://outlook.office.com/POP.AccessAsUser.All https://outlook.office.com/SMTP.Send offline_access
   ```
   The URL goes on one line, with spaces in `scope` encoded as `%20`. The
   admin opens it, signs in and accepts. The browser then ends on
   `http://localhost/?admin_consent=True…`; an "unable to connect" page there
   is normal.

If the tenant has the **admin consent workflow** enabled, the "Need admin
approval" page offers to send a request to the admins instead.

Record which case applies to your work account in the plan (section 8,
"Registrations and accounts"; task 0.3): user consent allowed, admin consent
granted, or request pending.

## Step 6: enable the protocols on the mailboxes

### Personal account (Outlook.com)

"POP & IMAP access is disabled by default"
([Outlook.com settings](https://support.microsoft.com/en-us/office/pop-imap-and-smtp-settings-for-outlook-com-d088b986-291d-42b8-9564-9c414e2aa040)).

1. Open outlook.com → **Settings → Mail → Forwarding and IMAP**.
2. Under **POP and IMAP**, turn on **Let devices and apps use IMAP**. Turn on
   POP as well for the POP3 test.
3. Click **Save**.

### Work account (Microsoft 365), done by an administrator

[Authenticated SMTP submission](https://learn.microsoft.com/en-us/exchange/clients-and-mobile-in-exchange-online/authenticated-client-smtp-submission):

- **Microsoft 365 admin center → Users → Active users →** the user → **Mail →
  Manage email apps:** tick **IMAP**, **POP** and **Authenticated SMTP**, then
  **Save changes**.
- Or with Exchange Online PowerShell:
  ```
  Set-CASMailbox -Identity <address> -ImapEnabled $true -PopEnabled $true -SmtpClientAuthenticationDisabled $false
  ```
- **Tenant-wide:**
  - The mailbox setting overrides the organisation's "Turn off SMTP AUTH
    protocol for your organization" (Exchange admin center → Settings → Mail
    flow).
  - **Security defaults** disable SMTP AUTH entirely. The tenant must use
    Conditional Access instead, or the SMTP test is skipped.
- **Timeline:** Basic authentication for SMTP AUTH becomes **disabled by
  default at the end of December 2026** (Message Center MC786329). OAuth is
  not affected, but SMTP AUTH itself must still be enabled for the mailbox.

## What to record

In your user provider file, `~/.config/mulberry/oauth2-providers.conf.json`
(outside the repository; see `OAUTH2-PLAN.md` section 7). The JSON is parsed
strictly: an error stops Mulberry at startup with the file, line and column.

**Create the folder and the file private**, if not done yet for Google:

```sh
mkdir -p -m 700 ~/.config/mulberry
touch ~/.config/mulberry/oauth2-providers.conf.json
chmod 600 ~/.config/mulberry/oauth2-providers.conf.json
```

Otherwise Mulberry warns at each start and offers **Fix Permissions**
(`OAUTH2-PLAN.md` section 7, "File permissions").

**One file for all providers.** If the file already exists (Google guide),
add the `microsoft` entry inside the existing `providers` object, separated
by a comma. A second `"providers"` key or a provider named twice is a parse
error, and Mulberry stops at startup. The combined example is in
`register-google.md`, "What to record". For Microsoft alone:

```json
{
  "version": 1,
  "providers": {
    "microsoft": {
      "client_id": "<Application (client) ID>",
      "tenant": "common"
    }
  }
}
```

Note in the plan (section 8, "Registrations and accounts"):
- the directory where the app is registered;
- whether the manifest accepted `http://127.0.0.1`;
- for the work account: the consent state (user, admin granted, pending) and
  the mailbox settings (IMAP, POP, Authenticated SMTP).

## Revoking access (for tests)

- **Personal account:** <https://account.live.com/consent/Manage> (Microsoft
  account → Privacy → apps and services with access) → Mulberry (test) →
  **Remove these permissions**.
- **Work account:** <https://myapps.microsoft.com> → the app → **Manage →
  Revoke permissions**, if the tenant allows it, or ask the admin to remove the
  user's grant.
- **Rotation:** Microsoft refresh tokens **rotate**: every refresh returns a
  new one, which replaces the old one. Mulberry must store it each time (plan
  task 3.3).

## Troubleshooting

| Message | Meaning |
|---|---|
| AADSTS700016 | Wrong client ID, or the app is not registered for this kind of account (check step 1.4) |
| AADSTS50011 | Redirect URI not registered (step 2) |
| AADSTS65001 | Consent missing |
| AADSTS90094 / AADSTS90095, "Need admin approval" | Work tenant: admin consent required (step 5) |
| AADSTS7000218 | Public client flows not allowed (step 3) for the device code |
| AADSTS50173, `invalid_grant` | Password changed or grant revoked: sign in again |
| IMAP `NO AUTHENTICATE failed.` | IMAP disabled for the mailbox, consent missing, or wrong `user=`; Microsoft gives no detail |
| SMTP `535 5.7.3 Authentication unsuccessful` | Token refused, often SMTP AUTH disabled for the mailbox |
| SMTP `535 5.7.139` | SMTP AUTH disabled (tenant, mailbox or security defaults) |
