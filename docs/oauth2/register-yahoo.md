# Yahoo Mail: OAuth2 registration and app passwords for testing Mulberry

*Written 2026-11-25 for the OAuth2 work (`OAUTH2-PLAN.md`, tasks 0.4, 0.5
and stage 7). Points not confirmed on an official page are marked
**(unverified)**.*


> **Application name.** This guide uses "Mulberry (test)" as the registered
> name. The project will later continue as a fork under another name, with the
> same copyright and licence. The name users see on the provider's consent page
> comes from this registration, not from the code, so a fork registers its own
> application under its own name. The paths `~/.config/mulberry/` and
> `~/.mulberry/` stand for the application's configuration folder.

## Summary

Yahoo's OAuth2 for **mail** is **not self-service**:

- The mail, contacts and calendar scopes (`mail-r`, `mail-w`) "are not
  available for self-served setup in the developer console". Yahoo grants them
  only after a **commercial access** application, with a signed agreement
  ([Yahoo developer access](https://senders.yahooinc.com/developer/developer-access/)).
- No timeline or criteria are published, and the page names no exception
  for personal, open-source or non-commercial projects (checked
  2026-11-27).
- Approval **is possible for an open-source desktop client**: Evolution got
  it in 2021 (see "How other clients access Yahoo Mail"). Whether Yahoo
  still approves new applications is unknown: a 2021 Evolution mailing-list
  post already reported that the developer console no longer offered the
  mail scope (**unverified**).

### How other clients access Yahoo Mail

Checked 2026-11-27. Each client with OAuth2 for Yahoo uses **its own
registration**; none can be reused by Mulberry, whose approval would belong
to its own registered application (under the fork's final name).

| Client | Yahoo access | Source |
|---|---|---|
| **Thunderbird** | OAuth2. Mozilla is an approved Yahoo partner; its client ID is Yahoo-approved and its redirect (`net.thunderbird://oauth/yahoo`) was set by Yahoo. Mozilla's source says "Don't copy these values for your own application": using them elsewhere would break Yahoo's terms, and only Thunderbird can receive the redirect | [Mozilla: OAuth2 for Yahoo, AOL and ATT](https://support.mozilla.org/kb/thunderbird-and-yahoo) |
| **Evolution** | OAuth2, scopes `mail-w ycal-w sdct-w` (mail, calendar, contacts). Its maintainer wrote in March 2021: "I received an approval for the Yahoo! OAuth2 application from the Verizon Media". The client ID and secret are built into evolution-data-server (`WITH_YAHOO_CLIENT_ID`, `WITH_YAHOO_CLIENT_SECRET`, with upstream defaults); users can override them with the GSettings keys `oauth2-yahoo-client-id` and `oauth2-yahoo-client-secret`. Which scopes the approval covered is not stated | [issue #317](https://gitlab.gnome.org/GNOME/evolution-data-server/-/issues/317), [commit](https://mail.gnome.org/archives/commits-list/2021-March/msg09459.html) |
| **Alpine** | XOAUTH2 or OAUTHBEARER since version 2.24, signed in through the browser. Its help says "Alpine is only registered with all the providers listed in this page", so the client ID is the Alpine maintainer's, shipped with Alpine (**unverified**: the page names neither the client ID nor an approval). For Gmail, by contrast, each Alpine user registers their own client | [Alpine help: XOAUTH2](https://alpineapp.email/alpine/alpine-info/misc/xoauth2.html) |
| **KMail** | No OAuth2 for Yahoo: Akonadi's OAuth2 covers Gmail only (adding a Yahoo account even opened a Google sign-in page). Users sign in with plain-text authentication and a Yahoo **app password**, as section A here | KDE bugs [444301](https://bugs.kde.org/show_bug.cgi?id=444301), [455113](https://bugs.kde.org/show_bug.cgi?id=455113), [462042](https://bugs.kde.org/show_bug.cgi?id=462042) (duplicate of 457233, status not checked) |

Until Yahoo approves Mulberry's own application, Mulberry does as KMail does
(plan branch 7.2).

So, for the free personal Yahoo account (Y):

1. **App password** (section A): the account works in Mulberry today with
   IMAP, SMTP and POP over PLAIN/LOGIN. It serves as the **regression test**
   that OAuth2 work does not break password accounts.
2. **Yahoo app and access request** (sections B and C): done once, in case
   Yahoo approves. Stage 7 of the plan depends on the answer.

## A. App password for the test account

[Yahoo help: app passwords](https://help.yahoo.com/kb/SLN15241.html):

1. Sign in at yahoo.com in a browser you have used with Yahoo for **several
   days in a row**, not in a private window. Otherwise Yahoo may refuse to
   create the password, and its support cannot override that.
2. **Account info → Account Security → External connections → Create app
   password** (or "Generate and manage app passwords").
3. Enter a name, for example `Mulberry`, and click **Generate password**.
4. Copy the 16-character password and click **Done**. Yahoo does not show it
   again.

Mulberry settings ([Yahoo server settings](https://help.yahoo.com/kb/SLN4075.html)):

| Service | Server | Security | User | Password |
|---|---|---|---|---|
| IMAP | `imap.mail.yahoo.com:993` | SSL/TLS | full Yahoo address | the app password |
| SMTP | `smtp.mail.yahoo.com:465` (SSL/TLS) or `587` (STARTTLS) | | full address | the app password |
| POP3 | `pop.mail.yahoo.com:995` | SSL/TLS | full address | the app password |

The authentication method in Mulberry stays **Plain Text**. This is the
baseline of plan task 0.5: check IMAP, sending and POP with today's Mulberry
before any OAuth2 code exists.

**Leave "Save password" unticked** with today's Mulberry: it writes the
password, only scrambled with a fixed key, into `preferences.mbp`. Type the
app password at each connection until task 2.6 is done; from then on
"Save password" stores it in the keyring chosen at start (the system keyring
or Mulberry's own). The migration test of task 2.7 uses a throwaway
preferences file, not your real one.

## B. Create a Yahoo app (for the access request)

1. Open <https://developer.yahoo.com/apps/create/> while signed in with the
   Yahoo account (the app list is at <https://developer.yahoo.com/apps/>).
2. Fill in the form. The fields below come partly from third-party guides
   (**unverified**):
   - **Application name:** `Mulberry`
   - **Description:** open-source desktop mail client (IMAP/SMTP)
   - **Homepage URL:** the project's public page, if any
   - **Redirect URI(s):** see the redirect question below
   - **OAuth client type:** **Public client**. Mulberry cannot keep a secret,
     and Yahoo *requires* PKCE for public clients.
   - **API permissions:** **OpenID Connect: Email, Profile**. The **Mail**
     checkbox is probably not selectable before approval (**unverified**);
     tick it if it is.
3. Click **Create App** and copy the **Client ID** and, if shown, the
   **Client secret**. Registering a redirect URI "might take up to 24 hours"
   (**unverified**).

**Redirect question (record the answer for stage 7):**

- Yahoo wants the "complete URL including the HTTP/HTTPS protocol", and the
  token request must repeat it exactly.
- Loopback redirects are not documented.
  - Thunderbird's Yahoo app uses `http://localhost`.
  - Some guides say the form refuses `http://` addresses (**unverified**).
  - Whether a port may change is unknown: assume exact matching.
- Try, in this order:
  1. `http://localhost`;
  2. `http://127.0.0.1`;
  3. `https://localhost`.

  Keep the first one Yahoo accepts.
- If only `https://` is accepted, the loopback listener would need TLS with a
  self-signed certificate, which browsers warn about. The paste-the-final-URL
  fallback (plan task 6.2) then becomes the practical way: the browser shows
  an error page, and the user pastes its address into Mulberry.

Protocol facts, for the provider table (plan task 1.3) ([Sign in with
Yahoo](https://developer.yahoo.com/sign-in-with-yahoo/),
[authorization code flow](https://developer.yahoo.com/oauth2/guide/flows_authcode/)):

- **Endpoints:**
  - authorization: `https://api.login.yahoo.com/oauth2/request_auth`;
  - token: `https://api.login.yahoo.com/oauth2/get_token`;
  - discovery: `https://api.login.yahoo.com/.well-known/openid-configuration`.
- **Client authentication:** for confidential clients, the secret goes in an
  `Authorization: Basic base64(client_id:client_secret)` header.
- **Grants and tokens:** authorization code only, with no device code;
  PKCE S256; access tokens last 1 hour; refresh tokens last long and may
  rotate.
- **Scopes:** `openid email` now; `mail-r` or `mail-w` only after approval.
- **SASL:** Yahoo's IMAP and SMTP servers accept XOAUTH2 and OAUTHBEARER;
  its POP server accepts XOAUTH2 only (checked on 2026-11-24, POP on
  2026-11-25; see the plan's section 8, "Baseline mechanisms").

## C. Request mail access (commercial access form)

1. Read <https://senders.yahooinc.com/developer/developer-access/>.
2. Fill in the form linked there. A third-party guide gives
   `https://senders.yahooinc.com/developer/developer-access-mail-form/`
   (**unverified**). Questions go to `mail-api@yahooinc.com`.
3. Yahoo asks for:
   - the identity of the application and of the requester;
   - a description of the application and how it uses mail data. For
     Mulberry: a desktop client that keeps mail on the user's computer, sends
     nothing to any server of the developer, and neither sells data nor uses
     it for advertising;
   - proof of minimum security standards;
   - a privacy policy, and user consent for each use.
4. If Yahoo continues, it sends a "Yahoo Mail Products Commercial Access
   Agreement" to sign (**unverified**). Approval or questions arrive by e-mail.
5. **Scope to request:** `mail-w`, because Mulberry deletes, moves and sends
   mail. Request `mail-r` only if Yahoo refuses write access.

Record in the plan (section 8, "Registrations and accounts") the date sent
and any answer. Without approval the
plan takes branch 7.2: Yahoo stays on app passwords, and Mulberry explains
this when OAuth2 is chosen for a Yahoo account.

## What to record

In your user provider file, `~/.config/mulberry/oauth2-providers.conf.json`
(outside the repository; see `OAUTH2-PLAN.md` section 7). Fill it in only if
the Yahoo app exists. The JSON is parsed strictly: an error stops Mulberry at
startup with the file, line and column.

**Create the folder and the file private**, if not done yet for Google or
Microsoft:

```sh
mkdir -p -m 700 ~/.config/mulberry
touch ~/.config/mulberry/oauth2-providers.conf.json
chmod 600 ~/.config/mulberry/oauth2-providers.conf.json
```

Otherwise Mulberry warns at each start and offers **Fix Permissions**
(`OAUTH2-PLAN.md` section 7, "File permissions").

**One file for all providers.** If the file already exists, add the `yahoo`
entry inside the existing `providers` object, separated by a comma. A second
`"providers"` key or a provider named twice is a parse error, and Mulberry
stops at startup. The combined example is in `register-google.md`, "What to
record". For Yahoo alone (`client_secret` only if Yahoo showed one):

```json
{
  "version": 1,
  "providers": {
    "yahoo": {
      "client_id": "<from section B>",
      "client_secret": "<if shown>",
      "redirect": "<the form Yahoo accepted>"
    }
  }
}
```

The app password does not go into this file. Mulberry asks for it at
connection; from task 2.6 on, "Save password" keeps it in the keyring chosen
at start, never in the preferences (see section A). Note in the plan
(section 8, "Registrations and accounts") the date the mail access request
was sent, Yahoo's answer and the redirect form Yahoo accepted.

## Troubleshooting

| Situation | Meaning |
|---|---|
| "Create app password" missing or refused | The browser is not "trusted" yet: use the same browser for some days, not in private mode |
| IMAP `[AUTHENTICATIONFAILED]` with the app password | Password mistyped, or the normal Yahoo password was used instead |
| OAuth2 `invalid_scope` for `mail-r`/`mail-w` | Mail access not granted to the app (section C) |
| OAuth2 `invalid_client` | Wrong client ID or secret, or a public/confidential client type mismatch |
| `redirect_uri` error | The URI differs from the registered one, character for character |
