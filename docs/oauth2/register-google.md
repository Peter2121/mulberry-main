# Registering a Google OAuth2 client for testing Mulberry

*Written 2026-11-25 for the OAuth2 work (`OAUTH2-PLAN.md`, task 0.1). The
Google Cloud console changes often: the menu names below are those of the
"Google Auth Platform" pages as documented in late 2026. Points not
confirmed on an official page are marked **(unverified)**.*


> **Application name.** This guide uses "Mulberry (test)" as the registered
> name. The project will later continue as a fork under another name, with the
> same copyright and licence. The name users see on the provider's consent page
> comes from this registration, not from the code, so a fork registers its own
> application under its own name. The paths `~/.config/mulberry/` and
> `~/.mulberry/` stand for the application's configuration folder.

## What you get

You create your **own** OAuth client for **your own** Google account ("bring
your own client ID"). Mulberry uses it to sign in to:

| Service | Server | Scope |
|---|---|---|
| IMAP | `imap.gmail.com:993` (TLS) | `https://mail.google.com/` |
| SMTP | `smtp.gmail.com:465` (TLS) or `587` (STARTTLS) | `https://mail.google.com/` |
| POP3 | `pop.gmail.com:995` (TLS) | `https://mail.google.com/` |
| CalDAV | `https://apidata.googleusercontent.com/caldav/v2/` | `https://www.googleapis.com/auth/calendar` |
| CardDAV | `https://www.googleapis.com/.well-known/carddav` | `https://www.googleapis.com/auth/carddav` |

The sign-in uses the **authorization code flow with PKCE** in your normal
browser. The browser then returns to Mulberry on the **loopback address**:
`http://127.0.0.1:<random port>`, `http://[::1]:<port>` or
`http://localhost:<port>`. Desktop clients accept any port without
registration (**unverified**: universal practice, not stated by Google).
Google offers **no device-code sign-in** for Gmail, Calendar or Contacts:
its device flow allows only `openid`, `email`, `profile`, some Drive and
YouTube scopes
([limited-input devices](https://developers.google.com/identity/protocols/oauth2/limited-input-device)).

`https://mail.google.com/` is a **restricted** scope
([Restricted scopes](https://support.google.com/cloud/answer/13464325)). A
client shared by many users would need Google's verification. A client used
only by its creator does not.

## Choose the audience type first: Personal mode (recommended) or External

| | **Personal mode** | **External, Testing** | **External, In production (unverified)** |
|---|---|---|---|
| Who can sign in | only the Google account that created the project | the test users you list (up to 100) | any Google account |
| Warning screen | none | "Google hasn't verified this app" | the same warning |
| 100-user cap | does not apply | test users only | 100 users in total, for the life of the project, cannot be reset |
| Refresh tokens | normal lifetime | **expire 7 days after consent** | normal lifetime |
| Can be changed later | **never**: the choice is permanent for the project | to production with "Publish app" | back to Testing |

Sources: [When verification is not needed](https://support.google.com/cloud/answer/13464323),
[Audience](https://support.google.com/cloud/answer/15549945),
[Personal mode](https://support.google.com/cloud/answer/15544987).

**Recommendation for the Mulberry tests:**

- **Project 1, Personal mode:** used for all normal testing. No warning
  screen, no cap, no 7-day expiry.
- **Project 2 (optional), External in Testing** with your address as test
  user. Its tokens expire after 7 days, which gives a natural test of
  Mulberry's "refresh token rejected → sign in again" path (`OAUTH2-PLAN.md`,
  task 4.3). Create it only when that task is reached. Its client ID and
  secret go into the test account's own fields, not into the provider file
  (see "What to record").

If Personal mode does not appear in your console, use External: keep it in
Testing for the first weeks, then **Publish app** to get rid of the 7-day
expiry. You alone count as one user of the 100 (**unverified**).

## Before you start

- **Account:** a personal Google account (Gmail). 2-Step Verification is not
  needed for OAuth (**unverified**). It is needed only for app passwords,
  which these tests do not use.
- **Gmail:**
  - IMAP needs no setting: since January 2025 "IMAP access is always turned
    on in Gmail" ([Gmail help](https://support.google.com/mail/answer/7126229)).
  - POP (for task 4.5) is enabled in Gmail → Settings → See all settings →
    Forwarding and POP/IMAP → POP download.
- **Calendar and contacts:** have a test calendar and a few test contacts that
  may be modified.

## Steps

### A. Personal mode (recommended)

1. Open <https://console.cloud.google.com/auth/overview/personal> while
   signed in with the test Google account.
2. Select an existing Personal-mode project, or let the console create one
   (default name `personal-mode-app`; you may rename it, e.g.
   `mulberry-test`).
3. **Application type:** choose **Desktop app**. Do not tick the "AI-powered
   agent" box.
4. Accept the Terms of Service and click **Continue**.
5. In the **"OAuth client created"** dialog, copy the **Client ID** and the
   **Client secret**, or click **Download JSON**. Google shows the secret
   **only now**; afterwards only its last 4 characters are visible. If it is
   lost, use *client secret rotation* on the client's page
   ([client secrets](https://support.google.com/cloud/answer/15549257)).
6. If the console says some APIs could not be enabled automatically, finish
   the onboarding, then do step C.
7. Do steps C and D below. Declaring the scopes in Personal mode is not
   documented as required; declare them anyway (**unverified**).

### B. External audience (alternative)

1. <https://console.cloud.google.com/> → project selector → **New project**
   (for example `mulberry-test`) → **Create**, and select it.
2. **Google Auth Platform → Overview → Get started**:
   - **App information:** app name `Mulberry (test)`, user support email =
     your address.
   - **Audience:** **External**.
   - **Contact information:** your address.
   - Accept the policy, **Create**.
3. **Audience → Test users → Add users:** your Gmail address. The status
   stays **Testing**; **Publish app** moves it to *In production*.
4. **Clients → Create client → Application type: Desktop app**, name
   `Mulberry desktop`, **Create**. Save the client ID and secret as in A.5.
5. Do steps C and D.

### C. Enable the APIs

**APIs & Services → Library**, search each and click **Enable**:

| API | Needed for | Note |
|---|---|---|
| **Gmail API** | IMAP/SMTP/POP scope | Not required by the IMAP/SMTP protocol itself (**unverified**), but it makes `https://mail.google.com/` appear in the scope picker |
| **CalDAV API** | CalDAV | Explicitly required by Google's [CalDAV guide](https://developers.google.com/workspace/calendar/caldav/v2/guide) |
| **CardDAV API** (if listed) and **People API** | CardDAV | Google only says the project must be "registered on DevConsole" ([CardDAV](https://developers.google.com/people/carddav)) |

The CalDAV guide notes that you "may be prompted to enable billing". For
these APIs at this usage level nothing is charged (**unverified**). If billing
is demanded, record it in the plan (section 8, "Registrations and accounts").

### D. Declare the scopes

**Google Auth Platform → Data Access → Add or remove scopes**:

- `https://mail.google.com/` (Gmail API; shown as **Restricted**);
- `https://www.googleapis.com/auth/calendar`;
- `https://www.googleapis.com/auth/carddav`. It may not be listed: Google's
  scope page does not document it, although Thunderbird and DAVx⁵ use it
  (**unverified**). Paste it into **Manually add scopes** and click **Update**.
  If Google rejects it during the tests, try
  `https://www.googleapis.com/auth/contacts` and note the result.

Click **Save**.

## What to record

In your user provider file, `~/.config/mulberry/oauth2-providers.conf.json`
(outside the repository; see `OAUTH2-PLAN.md` section 7). The JSON is parsed
strictly: an error stops Mulberry at startup with the file, line and column.

**Create the folder and the file private.** The file holds your client
secret:

```sh
mkdir -p -m 700 ~/.config/mulberry
touch ~/.config/mulberry/oauth2-providers.conf.json
chmod 600 ~/.config/mulberry/oauth2-providers.conf.json
```

Otherwise Mulberry warns at each start and offers **Fix Permissions**
(`OAUTH2-PLAN.md` section 7, "File permissions").

**One file for all providers.** The Google, Microsoft and Yahoo guides fill
the same file: a single `providers` object with one entry per provider. If
the file already exists, add the new entry inside `providers`, separated by
a comma. A second `"providers"` key or a provider named twice is a parse
error, and Mulberry stops at startup. With all three guides done, the file
looks like this:

```json
{
  "version": 1,
  "providers": {
    "google": {
      "client_id": "….apps.googleusercontent.com",
      "client_secret": "…"
    },
    "microsoft": {
      "client_id": "<Application (client) ID>",
      "tenant": "common"
    },
    "yahoo": {
      "client_id": "<from the Yahoo app>",
      "redirect": "<the form Yahoo accepted>"
    }
  }
}
```

For Google alone, keep only the `google` entry.

**The second project (External, Testing) for task 4.3 does not go into this
file.** The file holds one `google` entry. A second entry under another name
would claim the same servers, and the "Automatic" provider choice would be
ambiguous. Enter that project's client ID and secret in the test account's
own OAuth2 fields instead (plan tasks 3.1 and 3.2); the account's fields
override the file.

Note in the plan (section 8, "Registrations and accounts"): the project name,
the audience (Personal mode or External), and the creation date.

Never commit these values. For a Desktop client Google itself says the secret
"is obviously not treated as a secret", but it identifies your project.

## What you will see when signing in

- **Personal mode:** the normal Google account chooser and a consent page
  listing Gmail, Calendar and Contacts access. Some scopes have checkboxes:
  leave them all ticked, except when testing task 5.3.
- **External (Testing or unverified production):** first the screen "Google
  hasn't verified this app". Click **Advanced → Go to Mulberry (test)
  (unsafe)**; the exact wording may differ (**unverified**).
- Afterwards the browser shows Mulberry's "You can close this window" page.

## Keeping the client alive

- Google **deletes OAuth clients unused for 6 months** (no token requests, no
  settings changes), after an e-mail warning 30 days before. Deleted clients
  can be restored from **Deleted credentials** for about 30 days
  ([client secrets](https://support.google.com/cloud/answer/15549257)).
- Mulberry's regular token refreshes count as use.

## Revoking access (for tests)

- **Google Account → Security → Your connections to third-party apps &
  services → Mulberry (test) → Delete all connections.**
- Mulberry's next refresh then gets `invalid_grant` and must ask you to sign
  in again (task 4.3).

## Troubleshooting

| Message | Meaning |
|---|---|
| `client_user_mismatch` | Personal-mode client used with another Google account |
| `access_denied` after the warning screen | You declined, or the account is not a test user (Testing mode) |
| `invalid_client` | Client ID or secret mistyped, or the client was deleted |
| `redirect_uri_mismatch` | The client is not of type Desktop app |
| `admin_policy_enforced` | Workspace accounts only: the admin blocks the app (not expected with a personal Gmail account) |
| `invalid_grant` on refresh | Token revoked, 7-day Testing expiry, password changed, or unused for 6 months |
| IMAP `AUTHENTICATE` answered with `+ eyJzdGF0dXMiOiI0MDAi…` | The token was refused; the base64 JSON gives the reason (`oauth2-tool` decodes it from task 1.8, Mulberry from task 4.1) |
