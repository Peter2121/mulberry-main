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
- No timeline or criteria are published. Approval for a personal or
  open-source test project is **unknown and unlikely** (**unverified**).

**Thunderbird is not a counter-example.** A Yahoo account can use OAuth2 in
Thunderbird because Mozilla is an approved Yahoo partner. Its client ID is
Yahoo-approved, and its redirect (`net.thunderbird://oauth/yahoo`) was set by
Yahoo. Mozilla's source says "Don't copy these values for your own
application". Using them in another client would break Yahoo's terms, and the
redirect could only be received by Thunderbird.

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
- **SASL:** Yahoo's servers accept XOAUTH2 and OAUTHBEARER (checked on
  2026-11-24).

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
