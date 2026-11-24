# Mulberry codebase: existing authentication architecture and OAuth2 integration points

Scope: local code reading of /usr/home/peter/Programming/mulberry-main (git HEAD bef5b02b, 2026-11-24). All "sources" are repository paths (relative to the repo root) with line numbers as they were at that commit. Nothing was compiled or run. ~/.mulberry was not read.

## Q1. How is authentication modelled (classes, plugins, plugin loading)?

### Takeaway
Each account (`CINETAccount`) holds one `CAuthenticator`. That is a letter/envelope wrapper with a 4-value type enum (`eNone`, `ePlainText`, `eSSL`, `ePlugin`). Its inner "letter" is one of three credential classes: `CAuthenticatorNone`, `CAuthenticatorUserPswd` or `CAuthenticatorKerberos`. Which letter is used depends on the *UI type* (`eAuthUserPswd`/`eAuthKerberos`/`eAuthAnonymous`), and a plugin declares that UI type. SASL mechanisms other than PLAIN/LOGIN/ANONYMOUS/EXTERNAL are dlopen()ed `.so` plugins (CRAM-MD5, DIGEST-MD5, SCRAM-SHA-1/256, GSSAPI, Kerberos). They are driven line by line by `CAuthPlugin::DoAuthentication`.

### Cited Findings
- `CAuthenticator::EAuthenticators { eNone=0, ePlainText, eSSL, ePlugin }`. The wrapper holds `mType`, `mDescriptor` (the visible name, e.g. "Plain Text" or a plugin's prefs descriptor) and `CAuthenticator* mAuthenticator` (the letter). — [Sources_Common/Application/Preferences/CAuthenticator.h:30-81](Sources_Common/Application/Preferences/CAuthenticator.h)
- `CAuthenticatorUserPswd` stores `mUID`, `mPswd`, `mSaveUID`, `mSavePswd` and `CFutureItems mFuture`. `SetUID()` clears the password when the UID changes. `GetUniqueness()` returns the UID. — [CAuthenticator.h:110-165](Sources_Common/Application/Preferences/CAuthenticator.h)
- `CAuthenticatorKerberos` stores default-principal/server-principal/default-UID/UID and `mRecoveredUID`, which the plugin fills in. — [CAuthenticator.h:167-228](Sources_Common/Application/Preferences/CAuthenticator.h)
- The descriptor tables are hard-coded: `{"None","Plain Text","SSL Client Certificate"}` with SASL IDs `{"ANONYMOUS","PLAIN","EXTERNAL"}`. Any other descriptor makes `SetDescriptor()` set `mType=ePlugin`. — [Sources_Common/Application/Preferences/CAuthenticator.cpp:37-45, 211-229](Sources_Common/Application/Preferences/CAuthenticator.cpp)
- `SetAuthenticatorType()` picks the letter class. For `ePlugin` it uses `plugin->GetAuthUIType()`. `eAuthUserPswd` gives `CAuthenticatorUserPswd`, `eAuthKerberos` gives `CAuthenticatorKerberos`, anything else gives `CAuthenticatorNone`. — [CAuthenticator.cpp:125-175](Sources_Common/Application/Preferences/CAuthenticator.cpp)
- The copy constructor, `operator=` and `operator==` use `dynamic_cast` chains over the three letter classes. A new letter class (e.g. `CAuthenticatorOAuth2`) has to be added to all three. — [CAuthenticator.cpp:59-123](Sources_Common/Application/Preferences/CAuthenticator.cpp)
- `GetSASLID()` returns the fixed IDs or `plugin->GetAuthTypeID()`. `GetPlugin()` looks the plugin up by prefs descriptor via `CPluginManager::sPluginManager.GetAuthPlugin(mDescriptor)`. — [CAuthenticator.cpp:191-245](Sources_Common/Application/Preferences/CAuthenticator.cpp)
- `CAuthPlugin` (app side) wraps a plugin. `ProcessInfo` reads `SAuthPluginInfo{mAuthTypeID[256], mPrefsDescriptor[256], mAuthUIType}`. The call codes are SetServerTypeString, Use/Set/GetUserID, Use/SetPassword, SetServer, SetRealServer, ProcessData and SetChannelBinding. The return codes are eAuthError, eAuthServerError, eAuthDone, eAuthSendDataToServer, eAuthSendLiteralDataToServer, eAuthMoreData and eAuthMoreLiteralData. — [Sources_Common/Plugins/Authentication/CAuthPlugin.h:34-155](Sources_Common/Plugins/Authentication/CAuthPlugin.h)
- Plugin side: `CAuthPluginDLL` has fixed buffers `char mUserID[256]`, `mPassword[256]`, `mServer[256]`, `mRealServer[256]` (`cMaxAuthStringLength = 256`). `SetPassword` uses `strncpy(..., 255)`, so a value passed as the "password" is truncated at 255 bytes. — [Plug-ins/Common/CAuthPluginDLL.h:150-157](Plug-ins/Common/CAuthPluginDLL.h), [Plug-ins/Common/CAuthPluginDLL.cpp:170-173](Plug-ins/Common/CAuthPluginDLL.cpp)
- Plugin loading on Linux/FreeBSD: `CPlugin::LoadPlugin` calls `dlopen(mFname, RTLD_NOW)` and `dlsym(mConn, cPluginEntry)`. `ClonePlugin()` gives each authentication its own data section. — [Sources_Common/Plugins/General/CPlugin.cpp:376-523](Sources_Common/Plugins/General/CPlugin.cpp)
- The plugin search dirs on Unix are `mulberry/Plug-ins` (install libdir) and `~/.mulberry/Plug-ins`, matching `*.so`. — [Sources_Common/Plugins/General/CPluginManager.cpp:69-71](Sources_Common/Plugins/General/CPluginManager.cpp)
- `GetAuthPlugins()` lists the prefs descriptors of all `ePluginNetworkAuthentication` plugins. `FindAuthPlugin(auth_id)` matches by SASL ID and `GetAuthPlugin(prefs_id)` by descriptor. — [CPluginManager.cpp:599-632](Sources_Common/Plugins/General/CPluginManager.cpp)
- Plugins built on Linux: `BUILD_PLUGINS := PluginLibrary CRAM-MD5 DIGEST-MD5 SCRAM-SHA-1 SCRAM-SHA-256 GSSAPI @KERBEROS@ ... GPG @SMIME@ @SSL@`. Built `.so` files exist, e.g. `Plug-ins/SCRAM-SHA-256/SCRAMSHA256Plugin.so`. — [Plug-ins/Makefile.in:6-10](Plug-ins/Makefile.in)
- Example plugin build: SCRAM-SHA-256 links `../Common/CAuthPluginDLL.o`, `../SCRAM-SHA-1/sources/CSCRAMPluginDLL.o`, `kbase64.o`, `os_dep.o` with `SO_LDFLAGS = -lcrypto`. It is a separate Makefile.in per plugin. — [Plug-ins/SCRAM-SHA-256/Makefile.in](Plug-ins/SCRAM-SHA-256/Makefile.in)
- The SCRAM plugins already use OpenSSL `RAND_bytes` for the nonce. — [Plug-ins/SCRAM-SHA-1/sources/CSCRAMPluginDLL.cpp:90](Plug-ins/SCRAM-SHA-1/sources/CSCRAMPluginDLL.cpp)
- The plugin-side `EINETServerType` enum stops at ManageSIEVE. It has no HTTP/CalDAV/CardDAV values. — [Plug-ins/Common/CAuthPluginDLL.h (enum EINETServerType)](Plug-ins/Common/CAuthPluginDLL.h)

### Inferences
- The plugin ABI only passes a user ID and a ≤255-char password. It has no "get token" callback and no UI type beyond the three values. An OAuth2 *SASL plugin* (XOAUTH2/OAUTHBEARER) would therefore need either (a) a new UI type plus a new ABI call to pass a long bearer token, or (b) to run inside the app instead. Access tokens are often longer than 255 bytes: Microsoft Entra tokens are typically more than 1 KB (general knowledge, not from this repo). The current password channel would silently truncate them.
- The cleanest fit is probably a new built-in authenticator: `CAuthenticator::eOAuth2` (descriptor e.g. "OAuth2") with a new letter class `CAuthenticatorOAuth2`. Like PLAIN/EXTERNAL it would be handled directly in `CINETClient`/`CSMTPSender`, not as a plugin. It would hold the UID (e-mail), a provider/issuer id, client id, scopes, and maybe a refresh-token reference. This avoids changing the plugin ABI. Estimated change: CAuthenticator.h/.cpp +150–250 lines.
- `CAuthPlugin::DoAuthentication` ([CAuthPlugin.cpp:136-428](Sources_Common/Plugins/Authentication/CAuthPlugin.cpp)) already contains a generic, protocol-aware SASL exchange loop (command framing per protocol, `*` cancel, unsolicited CAPABILITY capture). It is a good reference, or a candidate for refactoring into a shared "SASL exchange" helper that a built-in XOAUTH2/OAUTHBEARER mechanism could reuse.

### Gaps
- I did not read `CPlugin.cpp` fully (only the dlopen/dlsym lines) or the plugin registration/licensing code (`CRegistrationCommon`). I did not check whether a plugin needs any registration step beyond being in the directory.

## Q2. How do protocol clients choose and run SASL (IMAP, POP3, SMTP, ManageSieve, ACAP/IMSP), and how are AUTH capabilities parsed?

### Takeaway
IMAP, POP3, IMSP, ACAP, ManageSieve, LDAP and the WebDAV/CalDAV clients all derive from `CINETClient`. `CINETClient::Logon()` → `DoAuthentication()` loops on `CMailControl::PromptUser()` and dispatches on the authenticator type to `DoAnonymousAuthentication`, `DoPlainAuthentication`, `DoExternalAuthentication` or `DoPluginAuthentication`. SMTP (`CSMTPSender`) is a separate class with its own copy of that loop. Capability parsing only sets booleans for PLAIN/LOGIN/ANONYMOUS (IMAP also SASL-IR). The general mechanism list is kept only as a raw capability string. SMTP is the only protocol that keeps a vector of AUTH types and checks the configured mechanism against it.

### Cited Findings
- `CINETClient::Logon()` does: TCPOpen → LookupServer → implicit TLS (+ client cert) → greeting → CAPABILITY (from greeting if present) → STARTTLS if configured → `DoAuthentication()` unless PREAUTH → refresh CAPABILITY from the tagged OK → `_PostAuthenticate()`. — [Sources_Common/Mail/INET_Clients/CINETClient.cpp:679-863](Sources_Common/Mail/INET_Clients/CINETClient.cpp)
- `DoAuthentication()` computes UI-context flags (`rmail`, `rprefs`, `radbk`, `rfilter`, `rhttp`) from `typeid(*mOwner)`. It then runs `while (CMailControl::PromptUser(acct_auth, ..., first))`. Inside, `switch(GetAuthenticatorType())` runs eNone→`DoAnonymousAuthentication`, ePlainText→`DoPlainAuthentication`, eSSL→`DoExternalAuthentication` and ePlugin→`DoPluginAuthentication`. A `CINETException` retries once without SASL-IR if it was used. Otherwise it shows the error and loops back to the prompt. — [CINETClient.cpp:967-1052](Sources_Common/Mail/INET_Clients/CINETClient.cpp)
- `DoPlainAuthentication()` applies PRECIS. It chooses LOGIN, AUTHENTICATE PLAIN (with or without initial response, depending on `mAuthInitialClientData`) or AUTHENTICATE LOGIN. It base64-encodes via `::base64_encode` unless `mAuthBase64==false` (ACAP). Logging is suppressed via `StValueChanger<bool> value(mAllowLog, CLog::AllowAuthenticationLog())`. — [CINETClient.cpp:1109-1246](Sources_Common/Mail/INET_Clients/CINETClient.cpp)
- `DoExternalAuthentication()` sends `AUTHENTICATE EXTERNAL` with `=` as the IR, or an empty continuation response. This is the simplest template for a built-in one-shot mechanism such as XOAUTH2/OAUTHBEARER. — [CINETClient.cpp:1249-1286](Sources_Common/Mail/INET_Clients/CINETClient.cpp)
- `GetAuthCommand()` defaults to `AUTHENTICATE`, and protocols override it (POP3 `AUTH`). — [CINETClient.cpp:873-876](Sources_Common/Mail/INET_Clients/CINETClient.cpp)
- `DoPluginAuthentication()` checks for a `-PLUS` variant in `mCapability` (substring search). It calls `plugin->DoAuthentication(..., *mStream, mLog, mLineData, cINETBufferLen, capability, serverSupportsPlus, mAuthInitialClientData)`. On failure it fakes a NO response and throws `CINETException::err_NoResponse`. — [CINETClient.cpp:1289-1340](Sources_Common/Mail/INET_Clients/CINETClient.cpp)
- Inside `CAuthPlugin::DoAuthentication`, the first command is `a AUTHENTICATE <mech> [IR]` for IMAP/IMSP/ACAP (quoted for ACAP), `AUTH <mech>` for SMTP/POP3, and `AUTHENTICATE "<mech>"` for ManageSieve. SASL-IR is used only for IMAP/IMSP **and only for mechanisms starting with "SCRAM-"**. Errors send `*` to cancel. Unsolicited `* CAPABILITY` lines are captured for IMAP/IMSP. — [Sources_Common/Plugins/Authentication/CAuthPlugin.cpp:251-413](Sources_Common/Plugins/Authentication/CAuthPlugin.cpp)
- `INETProcess()` reads lines until a tagged response **or a continuation (`+`)**. It treats `+` as success: `if (!INETCheckLastResponse(cTagOK) && !INETCheckLastResponse(cPlusLabel)) throw`. — [CINETClient.cpp:1748-1830](Sources_Common/Mail/INET_Clients/CINETClient.cpp)
- `INETSendString(NULL, ...)` means "flush and process the server response" (wait for continuation). — [CINETClient.cpp:1870-1927](Sources_Common/Mail/INET_Clients/CINETClient.cpp)
- SASL capability flags live in `CINETClient`: `mLoginAllowed`, `mAuthLoginAllowed`, `mAuthPlainAllowed`, `mAuthAnonAllowed`, `mAuthInitialClientData`, `mAuthBase64` and `mSTARTTLSAllowed`, plus the raw `mCapability` string. — [Sources_Common/Mail/INET_Clients/CINETClient.h:170-181](Sources_Common/Mail/INET_Clients/CINETClient.h); defaults at [CINETClient.cpp:275-279](Sources_Common/Mail/INET_Clients/CINETClient.cpp)
- IMAP: `mAuthLoginAllowed/mAuthPlainAllowed/mAuthAnonAllowed` come from `CheckUntagged("AUTH=LOGIN"/"AUTH=PLAIN"/"AUTH=ANONYMOUS")`, and `mAuthInitialClientData` from `SASL-IR`. It is forced true for IMAP4rev2. — [Sources_Common/Mail/INET_Clients/IMAP/CIMAPClient.cpp:398-420](Sources_Common/Mail/INET_Clients/IMAP/CIMAPClient.cpp); constants at [IMAP/CIMAPCommon.cpp:46, 83-85](Sources_Common/Mail/INET_Clients/IMAP/CIMAPCommon.cpp)
- IMAP maps response codes `[UNAVAILABLE]`, `[AUTHENTICATIONFAILED]`, `[AUTHORIZATIONFAILED]` and `[EXPIRED]` to resource strings. — [CINETClient.cpp:2231-2234](Sources_Common/Mail/INET_Clients/CINETClient.cpp)
- ManageSieve: `mAuthInitialClientData = true` is always set. `_ProcessCapability` tokenises the `SASL` capability but only recognises PLAIN and ANONYMOUS. — [Sources_Common/Mail/INET_Clients/SIEVE/SIEVE/CSIEVEClient.cpp:80, 215-240](Sources_Common/Mail/INET_Clients/SIEVE/SIEVE/CSIEVEClient.cpp)
- POP3: `_ProcessCapability` parses only `STLS` and `AUTH-RESP-CODE` and concatenates the CAPA lines into `mCapability`. The CAPA `SASL` mechanism list is not parsed. `DoPlainAuthentication` is overridden to USER/PASS or APOP (no AUTH PLAIN). The plugin path is inherited from `CINETClient`. — [Sources_Common/Offline/Mail/CPOP3Client.cpp:215-232, 360-390](Sources_Common/Offline/Mail/CPOP3Client.cpp)
- ACAP: `mAuthInitialClientData = true` and `mAuthBase64 = false`. IMSP parses AUTH=LOGIN/PLAIN. — [ACAP/CACAPClient.cpp:132-133, 312-314](Sources_Common/Mail/INET_Clients/ACAP/CACAPClient.cpp), [IMSP/CIMSPClient.cpp:222-223](Sources_Common/Mail/INET_Clients/IMSP/CIMSPClient.cpp)
- SMTP (`CSMTPSender`, not a `CINETClient`) parses the EHLO `AUTH` line into `mAUTHTypes` (upper-cased tokens). — [Sources_Common/Mail/INET_Clients/SMTP/CSMTPSender.cpp:1531-1539](Sources_Common/Mail/INET_Clients/SMTP/CSMTPSender.cpp)
- Before AUTH, SMTP checks that `GetAuthenticator().GetSASLID()` is in `mAUTHTypes` (PLAIN falls back to LOGIN). Otherwise it fails with `cSMTPErrorNoAUTHType`. After a successful AUTH it re-sends EHLO. — [CSMTPSender.cpp:674-715](Sources_Common/Mail/INET_Clients/SMTP/CSMTPSender.cpp)
- `SMTPDoAuthentication()` has its own `PromptUser` loop. It calls `CPasswordManager::AddPassword` on success and shows a `CStopAlertRsrcTxtTask` on failure. — [CSMTPSender.cpp:1855-1905](Sources_Common/Mail/INET_Clients/SMTP/CSMTPSender.cpp)
- `SMTPAuthenticate()`: ePlainText requires TLS and sends `AUTH PLAIN <b64>` with the initial response or `AUTH LOGIN`. eSSL sends `AUTH EXTERNAL =`. ePlugin calls `plugin->DoAuthentication(..., mStream, mLog, mLineData, cSMTPBufferLen, capability)` with no SASL-IR. — [CSMTPSender.cpp:1914-2070](Sources_Common/Mail/INET_Clients/SMTP/CSMTPSender.cpp)
- Buffer sizes: `cINETBufferLen = 8192` but `cSMTPBufferLen = 1024`. — [Sources_Common/Mail/INET_Clients/CINETCommon.h:62](Sources_Common/Mail/INET_Clients/CINETCommon.h), [CSMTPSender.cpp:67](Sources_Common/Mail/INET_Clients/SMTP/CSMTPSender.cpp)
- `LookupServer()` builds the socket descriptor as server + authenticator descriptor + TLS type, and asks the plugin whether it needs a CNAME. — [CINETClient.cpp:608-674](Sources_Common/Mail/INET_Clients/CINETClient.cpp); SMTP equivalent at [CSMTPSender.cpp:1362-1405](Sources_Common/Mail/INET_Clients/SMTP/CSMTPSender.cpp)

### Inferences
- A built-in `DoOAuth2Authentication()` in `CINETClient` (~80–120 lines) can follow `DoExternalAuthentication`. It would send `AUTHENTICATE XOAUTH2`/`OAUTHBEARER` plus the base64 IR when `mAuthInitialClientData` is set, otherwise wait for `+ ` and send the payload.
  - **Pitfall:** because `INETProcess` accepts `+` as success, an XOAUTH2/OAUTHBEARER failure would not be detected. In that case the server returns `+ <base64 JSON error>` and expects an empty client line (or `AQ==` for OAUTHBEARER) before the tagged NO. The new code must check `PLUS_TEST(mLastResponse.code)` after sending the token, send the empty/`\x01` response, and process again to get the NO.
  - The same applies to ManageSieve (always IR). POP3 `AUTH XOAUTH2` with an IR is in practice limited to 255 octets per RFC 5034, so POP3 needs the continuation form (protocol detail from general knowledge, not repo).
- SMTP: a new `case CAuthenticator::eOAuth2` in `SMTPAuthenticate()` (~60–80 lines). The XOAUTH2 base64 payload can exceed 1024 bytes, so it must be written directly to `mStream` rather than through `mLineData` (1024 bytes). The `GetSASLID()` check at CSMTPSender.cpp:685-695 needs `GetSASLID()` to return "XOAUTH2" or "OAUTHBEARER", whichever the server advertises. The best design is to choose dynamically: prefer OAUTHBEARER if advertised, else XOAUTH2.
- Mechanism selection: IMAP/POP3/Sieve keep no list of advertised mechanisms. Add `mAuthXOAuth2Allowed`/`mAuthOAuthBearerAllowed` flags (IMAP: `CheckUntagged("AUTH=XOAUTH2")`/`"AUTH=OAUTHBEARER"`; Sieve: tokens in the `SASL` capability; POP3: parse the CAPA `SASL` line). This is ~5–15 lines per protocol.
- `_InitCapability` in each protocol must reset the new flags.

### Gaps
- I did not inspect `CIMSPClient`/`CACAPClient` beyond the flags (legacy protocols; OAuth2 is likely irrelevant there).
- I did not verify how `INETParseResponse` handles a `+ <base64>` continuation payload (whether the payload is kept in `mLastResponse` for decoding the JSON error).
- LDAP (`CLDAPClient.cpp:369, 449`) uses its own bind path and was not analysed.

## Q3. How do the HTTP/WebDAV clients (CalDAV, CardDAV, WebDAV prefs) authenticate?

### Takeaway
HTTP clients use an `http::CHTTPAuthorization` object (Basic or Digest) owned by the session. It is created lazily after a 401 by a per-client `GetAuthorization()` that switches on the account's authenticator type. A Bearer class (`CHTTPAuthorizationBearer`, ~30 lines) plugs straight into this design. The logic is duplicated in three clients.

### Cited Findings
- `CHTTPAuthorization` is an abstract class with `GenerateAuthorization(std::ostream&, const CHTTPRequestResponse*)`. — [Sources_Common/HTTP/HTTPClient/CHTTPAuthorization.h](Sources_Common/HTTP/HTTPClient/CHTTPAuthorization.h)
- `CHTTPAuthorizationBasic::GenerateAuthorization` writes `Authorization: Basic base64(user:pswd)`. — [Sources_Common/HTTP/HTTPClient/CHTTPAuthorizationBasic.cpp](Sources_Common/HTTP/HTTPClient/CHTTPAuthorizationBasic.cpp)
- Digest exists in `CHTTPAuthorizationDigest.{h,cpp}`. `CHTTPAuthenticate.h` is an empty file (0 lines). — [Sources_Common/HTTP/HTTPClient/](Sources_Common/HTTP/HTTPClient/)
- `CHTTPSession` owns `CHTTPAuthorization* mAuthorization`. `WriteHeaderToStream` calls `GetAuthorization()->GenerateAuthorization(os, request)` when one is present. — [Sources_Common/HTTP/HTTPClient/CHTTPSession.cpp:57-78](Sources_Common/HTTP/HTTPClient/CHTTPSession.cpp), [CHTTPSession.h:41-70](Sources_Common/HTTP/HTTPClient/CHTTPSession.h)
- `CWebDAVCalendarClient::DoSession()` loops as follows:
  - send the request;
  - on 401, delete the old authorization and show the error (except the first time);
  - call `GetAuthorization(first_time, WWW-Authenticate headers)` and retry;
  - on success, call `CPasswordManager::AddPassword`.
  - [Sources_Common/Calendar_Store/Clients/CWebDAVCalendarClient.cpp:1837-1900](Sources_Common/Calendar_Store/Clients/CWebDAVCalendarClient.cpp)
- `GetAuthorization()` calls `CMailControl::PromptUser(..., rhttp=true, first_time)`, then switches on the type: eNone→NULL, ePlainText→Basic (PRECIS), eSSL→NULL, ePlugin→Digest only if the SASL ID is "DIGEST-MD5", otherwise NULL ("Currently not supported with http"). It returns `(CHTTPAuthorization*)-1` on cancel. `mAuthUniqueness` records the authenticator uniqueness, and `CheckCurrentAuthorization()` drops the cached authorization if the account changed. — [CWebDAVCalendarClient.cpp:1935-2016, 131-145](Sources_Common/Calendar_Store/Clients/CWebDAVCalendarClient.cpp)
- The same pattern is duplicated in `CWebDAVVCardClient::DoSession/GetAuthorization` ([Sources_Common/VCard_Store/Clients/CWebDAVVCardClient.cpp:1690, 1787](Sources_Common/VCard_Store/Clients/CWebDAVVCardClient.cpp)) and `CWebDAVPrefsClient` ([Sources_Common/Preferences_Store/Clients/CWebDAVPrefsClient.cpp:589, 686](Sources_Common/Preferences_Store/Clients/CWebDAVPrefsClient.cpp)).
- The WebDAV clients derive from `CINETClient` (e.g. `class CCalendarClient : public CINETClient`; `CWebDAVCalendarClient : public CCalendarClient, public http::webdav::CWebDAVSession`) and use `mLogType = CLog::eLogHTTP`. — [Calendar_Store/Clients/CCalendarClient.h:42](Sources_Common/Calendar_Store/Clients/CCalendarClient.h), [CWebDAVCalendarClient.h:35](Sources_Common/Calendar_Store/Clients/CWebDAVCalendarClient.h), [CWebDAVCalendarClient.cpp:99](Sources_Common/Calendar_Store/Clients/CWebDAVCalendarClient.cpp)

### Inferences
- Bearer support needs four pieces:
  - a new `CHTTPAuthorizationBearer(token)` (~40 lines incl. header);
  - a `case CAuthenticator::eOAuth2` in the three `GetAuthorization()` functions that obtains/refreshes a token and returns the Bearer object (~15 lines each);
  - for "auth on first request", the clients could pre-set the Bearer authorization before the first request, because the current design waits for a 401 (one wasted round trip, acceptable);
  - on a 401 with an existing Bearer authorization, force-refresh the token once before prompting; RFC 6750 `WWW-Authenticate: Bearer error="invalid_token"` could be parsed.
- Refactoring the three duplicated `GetAuthorization` functions into one helper is worthwhile but optional.

### Gaps
- I did not check whether `CHTTPRequestResponse` supports arbitrary POST bodies with `application/x-www-form-urlencoded` (needed if the native HTTP stack is reused for token requests). It has `CHTTPDataString`, so it probably can, but this is unverified.

## Q4. How are passwords obtained and stored; where are account preferences serialised?

### Takeaway
Passwords come from four sources, tried in this order:
1. an in-memory value already in `CAuthenticatorUserPswd`;
2. an admin initial password;
3. a session password cache;
4. the `CPasswordManager` (on Linux: an optional encrypted keyring file, libsodium Argon2id + XSalsa20-Poly1305 `crypto_secretbox`, V2 format).

If none of these yields a password, a modal `CUserPswdTask` dialog asks for it. "Save password" writes the password into the preferences file, obfuscated with a fixed-key cipher (`eEncryptSimplemUTF7`, key "Modified-UTF7"). That is not real protection. Account data are serialised as s-expression strings via `GetInfo/SetInfo`, with `CFutureItems` providing forward-compatible extra fields.

### Cited Findings
- `CMailControl::PromptUser()`: if `!acct_auth->RequiresUserPswd()` it returns `first`. This means non-password authenticators get exactly one attempt ("NB Must fail after first attempt as there is no way to reset parameters"). On the first call with a non-empty password it returns true without a dialog. — [Sources_Common/Mail/Control/CMailControl.cpp:944-970](Sources_Common/Mail/Control/CMailControl.cpp)
- `DoUserIDPassword()` tries the admin `mInitialPswd`, then `CINETProtocol::GetCachedPswd(uid)` (when the `mRemoteCachePswd` pref is set), then `CPasswordManager::GetManager()->GetPassword(acct, ...)`. Otherwise it shows `CUserPswdTask(uid, pswd, save_user, save_pswd, title, server, method, secure, icon)`. Changes mark the matching account list dirty. — [CMailControl.cpp:973-1194](Sources_Common/Mail/Control/CMailControl.cpp)
- `CUserPswdTask` is a `CTaskSync` (synchronous hand-off from network thread to UI thread). — [Sources_Common/Tasks/CTaskClasses.h:51-72](Sources_Common/Tasks/CTaskClasses.h), [Sources_Common/Tasks/CTaskQueue.h:107-118](Sources_Common/Tasks/CTaskQueue.h)
- `CPasswordManager` is an abstract singleton with `GetPassword(acct, pswd)` and `AddPassword(acct, pswd)`. — [Sources_Common/Mail/Control/CPasswordManager.h](Sources_Common/Mail/Control/CPasswordManager.h)
- `CPasswordManagerKeyring` (Linux):
  - file `<user cwd>/.keyring`;
  - V2 header `MULBERRY-KEYRING-V2`;
  - Argon2id (`crypto_pwhash`, OPSLIMIT/MEMLIMIT_MODERATE) + `crypto_secretbox_easy`;
  - written with umask 077;
  - V1 MD5+RC4 is migrated;
  - entries are keyed by `GetAccountURL()` = scheme (`imap:`, `imaps:`, `smtp:`, `managesieve:`, `https://` for DAV, ...) + user + "@" + server;
  - every Get/Add re-reads, decrypts and rewrites the whole file (Argon2 each time).
  - [Sources_Common/Mail/Control/CPasswordManagerKeyring.cpp:63-149, 196-400](Sources_Common/Mail/Control/CPasswordManagerKeyring.cpp)
- The keyring is only activated if the file already exists: at startup `CMulberryApp` asks for the passphrase via `CGetPassphraseDialog`. Grep found no code or UI that creates a new keyring. `AddPassword` is a no-op while `!mActive`. — [Linux/Sources/Application/General/CMulberryApp.cpp:684-697](Linux/Sources/Application/General/CMulberryApp.cpp)
- There is also a `CPasswordManagerKeychain` (macOS). No libsecret/KWallet/D-Bus Secret Service integration was found. — [Sources_Common/Mail/Control/](Sources_Common/Mail/Control/)
- After a successful logon, `CINETProtocol::Logon()` stores the password in the session cache and calls `CPasswordManager::AddPassword`, then records `SetAuthenticatorUniqueness(...)`. — [Sources_Common/Mail/Protocols/CINETProtocol.cpp:427-468](Sources_Common/Mail/Protocols/CINETProtocol.cpp)
- Similar `AddPassword` calls exist in CAdbkProtocol.cpp:523-524, CLDAPClient.cpp:468-469, CSMTPSender.cpp:1881, CCalendarProtocol.cpp:496-497, CWebDAVCalendarClient.cpp:207/1744/1892 and CWebDAVVCardClient.cpp:206/1597/1744 (grep results).
- `CAuthenticatorUserPswd::GetInfo()` writes `"uid" "<pswd encrypted eEncryptSimplemUTF7 if mSavePswd && !CAdminLock::mLockSavePswd>" saveUID savePswd <future>`. `SetInfo` decrypts and clears the password if `!mSavePswd`. — [CAuthenticator.cpp:299-349](Sources_Common/Application/Preferences/CAuthenticator.cpp)
- `eEncryptSimplemUTF7` sets the key to the constant `"Modified-UTF7"` and calls `encrypt_cipher`. — [Sources_Common/Utilities/cdstring.cpp:1599-1635](Sources_Common/Utilities/cdstring.cpp), [Sources_Common/Utilities/encrypt.cpp:83](Sources_Common/Utilities/encrypt.cpp)
- `CINETAccount::GetInfo()` writes: name, server type descriptor, server IP, logon-at-start, `((authenticator info))`, `(cwd)`, TLS type descriptor, use-client-cert, client-cert fingerprint, future items. `SetInfo` parses the same with version gates. — [Sources_Common/Application/Preferences/CINETAccount.cpp:168-285](Sources_Common/Application/Preferences/CINETAccount.cpp)
- Server types include eIMAP, ePOP3, eSMTP, eWebDAVPrefs, eIMSP, eACAP, eLDAP, eManageSIEVE, eHTTPCalendar, eWebDAVCalendar, eCalDAVCalendar and eCardDAVAdbk. — [CINETAccount.h:38-59](Sources_Common/Application/Preferences/CINETAccount.h)
- Account lists are written to the prefs map via `WRITETOMAP(mMailAccounts, cMailAccountsKey_2_0)`, `mSMTPAccounts`, `mCalendarAccounts`, etc. — [Sources_Common/Application/Preferences/CPreferencesRW.cpp:136-137, 524, 762-766](Sources_Common/Application/Preferences/CPreferencesRW.cpp)
- `CFutureItems` (a `cdstrvect`) keeps unknown trailing items for forward compatibility. — [Sources_Common/Application/Preferences/CFutureItems.h:25-34](Sources_Common/Application/Preferences/CFutureItems.h)

### Inferences
- Refresh tokens are long-lived secrets. They should **not** go into `CAuthenticator*::GetInfo()`: the prefs file only gets fixed-key obfuscation, and prefs can be stored on remote servers (eWebDAVPrefs, IMSP/ACAP "remote prefs"), which would upload them.
- Better: store refresh tokens via `CPasswordManager`, under a distinct key (e.g. `oauth2:` + `GetAccountURL()`, or a new `GetSecret/AddSecret(key)` API). Store only non-secret settings in prefs (provider/issuer, client id, scopes, UID/e-mail, maybe token endpoint).
- The keyring currently needs to exist beforehand. A "create keyring" path, or a libsecret backend (FreeBSD: `pkg install libsecret`), is a separate decision. Minimal approach: allow `AddPassword` to create the keyring after prompting for a new passphrase.
- Size: the keyring API extension is ~50–100 lines. The new authenticator's `GetInfo/SetInfo` is ~60 lines.
- `PromptUser` returns `first` for non-UserPswd authenticators. That suits OAuth2 if token acquisition (refresh, or an interactive browser flow) happens *before* the SASL attempt, inside a new branch (e.g. `if (acct_auth is CAuthenticatorOAuth2) return CMailControl::DoOAuth2(...)`). The interactive browser flow must run on the UI thread or through a `CTaskSync`, like `CUserPswdTask`, because network clients run off the UI thread.
- Multiple connections per account (IMAP clones, SMTP, Sieve) authenticate independently. The token cache and the refresh must therefore be per account (or per issuer+user) and protected by a mutex, so that parallel refreshes do not happen.

### Gaps
- I did not trace the threading model in detail (which thread runs `CINETClient::Logon` on Linux). The comment in `CAuthPlugin::DoAuthentication` about "multithreaded access" and the `CTaskSync` usage suggest network ops run on worker threads.
- I did not verify that `CAdminLock::mLockSavePswd` also covers other secrets.

## Q5. What happens on auth failure, dropped connections and errors?

### Takeaway
Auth failure loops back to the user/password prompt, but only for user/password authenticators; other authenticators get a single attempt. Dropped connections go through `INETTryRecoverError` → `INETRecoverReconnect` → `CMailControl::MboxServerReconnect`. That reconnects and re-authenticates with the in-memory credentials, without a prompt. For OAuth2, a re-auth needs a fresh or valid access token each time, so expiry must be checked before every SASL attempt.

### Cited Findings
- In `DoAuthentication()`, a `CINETException` shows `INETDisplayError(ex, "Error::INET::OSErrLogon", "Error::INET::NoBadLogon")` and goes round the `PromptUser` loop. When the SASL-IR attempt fails, it retries once without IR. — [CINETClient.cpp:1020-1045](Sources_Common/Mail/INET_Clients/CINETClient.cpp)
- In `Logon()`, if `!auth_done` it closes the connection and throws `err_BadResponse`. Other exceptions show the error, close the connection and reset the DNS descriptor. — [CINETClient.cpp:804-862](Sources_Common/Mail/INET_Clients/CINETClient.cpp)
- `INETTryRecoverError()`:
  - NO/BAD responses: show the error and continue.
  - Parse errors: abort.
  - TCP aborts: if logged on, `TCPOpen()` + `INETRecoverReconnect()`.
  - `err_TCPFailed`: unrecoverable.
  - [CINETClient.cpp:2400-2468](Sources_Common/Mail/INET_Clients/CINETClient.cpp)
- `CIMAPClient::INETRecoverReconnect()` calls `CMailControl::MboxServerReconnect(GetMboxOwner())`. `INETDisplayError` suppresses network errors while waiting to reconnect. — [Sources_Common/Mail/INET_Clients/IMAP/CIMAPClient.cpp:4252-4272](Sources_Common/Mail/INET_Clients/IMAP/CIMAPClient.cpp)
- `CINETProtocol::Logon()` calls `SetNoRecovery(false)` before logging in, and caches the password after success. — [CINETProtocol.cpp:427-468](Sources_Common/Mail/Protocols/CINETProtocol.cpp)
- The SMTP failure path shows `CStopAlertRsrcTxtTask(nobad_id, mLineData)` and re-prompts. — [CSMTPSender.cpp:1885-1900](Sources_Common/Mail/INET_Clients/SMTP/CSMTPSender.cpp)
- The HTTP 401 path deletes the authorization, shows `DisplayHTTPError` (not on the first round) and re-prompts. — [CWebDAVCalendarClient.cpp:1850-1880](Sources_Common/Calendar_Store/Clients/CWebDAVCalendarClient.cpp)

### Inferences
- OAuth2 retry policy, suggested:
  1. Before each SASL/HTTP attempt, use the cached access token if it is not expiring within ~60 s, otherwise refresh.
  2. On a SASL failure with an OAuth2 authenticator, invalidate the access token, refresh once, and retry once.
  3. If the refresh fails with `invalid_grant`, drop the refresh token and start the interactive browser flow (UI task). Cancel means fail.

  This needs `PromptUser` to return `true` for a second attempt for OAuth2 (it currently returns `first`), or a dedicated loop. About 50–80 lines in CMailControl plus small changes in the three loops (CINETClient, CSMTPSender, WebDAV GetAuthorization).
- Long-lived IMAP connections (IDLE) do not need to re-authenticate when the token expires. Only new connections and reconnects do. Some servers (e.g. Microsoft) may drop sessions on token expiry (general knowledge, not verified); the reconnect path then covers it.

### Gaps
- I did not trace `CMailControl::MboxServerReconnect` and `CMboxProtocol` recovery (CMboxProtocol.cpp:949, 2273) in detail.

## Q6. What building blocks exist: HTTP/TLS client for token POSTs, libcurl, JSON, base64url, SHA-256, RNG, browser launching, local listener?

### Takeaway
The building blocks are:
- **TLS/HTTP:** OpenSSL is a hard build dependency. The app has its own TLS socket with a certificate-acceptance UI. A native HTTP/1.1 client stack exists, but it is entangled with `CINETClient`/accounts.
- **libcurl:** the system libcurl is already linked, but only into `libHTMLView` when the HTML viewer is enabled (the `Fetcher` uses the curl multi interface, GET-only, for remote images).
- **Crypto/encoding helpers:** libsodium (required) provides `randombytes_buf`. OpenSSL provides SHA-256 and RAND_bytes. A standard base64 exists; base64url does not.
- **JSON:** no parser exists in Mulberry's own code.
- **Browser launching:** `CURLHelpersMap::LaunchURL` (url.helpers map, then `xdg-open` fallback).
- **Local listener:** none (`CTCPSocket::TCPWaitConnection` is a stub).

### Cited Findings
- OpenSSL is required at configure time ("mulberry and SMIME and SSL plugins require OpenSSL"). libsodium is required ("required for keyring encryption"). — [configure.in:215-246, 453-457](configure.in)
- libcurl: `configure.in` checks `pkg-config libcurl` only inside the HTML viewer block and adds it to `HTMLVIEW_SYS_CFLAGS/LIBS`. A missing libcurl is an error unless `--disable-html-viewer` is used. The generated `Libraries/HTMLView/Makefile` has `-L/usr/local/lib -lcurl`. — [configure.in:528-537](configure.in), [Libraries/HTMLView/Makefile:23](Libraries/HTMLView/Makefile)
- `Libraries/HTMLView/src/Fetcher.cpp` uses `curl_global_init` (call_once) and `curl_multi_init`. Per transfer it sets:
  - `CURLOPT_PROTOCOLS_STR "http,https"`, `FOLLOWLOCATION`, `MAXREDIRS`;
  - `NETRC_IGNORED`, `DISALLOW_USERNAME_IN_URL`;
  - `USERAGENT`, `ACCEPT_ENCODING ""`, timeouts, `MAXFILESIZE_LARGE`, `FAILONERROR`;
  - a write callback, and `VERBOSE` + `DEBUGFUNCTION` for debugging.
  - It is GET-only and asynchronous (multi). — [Libraries/HTMLView/src/Fetcher.cpp:15, 52, 82-92, 195-242](Libraries/HTMLView/src/Fetcher.cpp)
- The native HTTP stack is `Sources_Common/HTTP/HTTPClient` (`CHTTPRequestResponse`, `CHTTPSession`, `CHTTPDataString`, `CHTTPDecompress`, `CHTTPDefinitions`) with WebDAV/CalDAV/CardDAV request classes on top. — [Sources_Common/HTTP/](Sources_Common/HTTP/)
- The request transport is driven by `CINETClient`-derived clients via `mStream` (`DoRequest` writes headers to `mStream`). — [CWebDAVCalendarClient.cpp:2018-2035](Sources_Common/Calendar_Store/Clients/CWebDAVCalendarClient.cpp)
- `CTLSSocket` (OpenSSL):
  - `SSL_CTX_new(TLS_client_method())`;
  - `CCertificateManager::LoadSSLRootCerts(m_ctx)`;
  - a verify callback collects errors;
  - after the handshake, `CCertificateManager::CheckServerCertificate(mOriginalName, mCertErrors, server_cert)`, otherwise `err_TCPSSLCertNoAccept`;
  - `CAcceptCertTask` / `CAcceptCertDialog` lets the user accept a certificate once or save it;
  - client certificates via `TLSSetClientCert`;
  - channel binding via `TLSGetChannelBinding`.
  - [Sources_Common/Mail/Network/CTLSSocket.cpp:231-306, 643-660, 704-729, 854](Sources_Common/Mail/Network/CTLSSocket.cpp), [CTLSSocket.h:35-100](Sources_Common/Mail/Network/CTLSSocket.h)
- No local listener: `CTCPSocket::TCPWaitConnection()` only sets states. It has a `// listen here` comment and no `listen()`/`accept()` calls. A grep for `listen(`/`accept(` in Sources_Common and Linux/Sources found nothing else. — [Sources_Common/Mail/Network/CTCPSocket.cpp:1596-1612](Sources_Common/Mail/Network/CTCPSocket.cpp)
- JSON: a case-insensitive grep for "json" in Sources_Common, Linux/Sources, Plug-ins and Libraries (excluding vendor/ACE) found only unrelated hits in litehtml/harfbuzz. There is no JSON parser. An XML library (`Libraries/XMLLib`) exists for WebDAV.
- base64: `Sources_Common/Utilities/base64.h` exposes `char* base64_encode(const unsigned char*, size_t)` and `unsigned char* base64_decode(const char*, size_t&)`. There is also a plugin-side `Plug-ins/Common/kbase64.cpp`. No base64url helper was found. — [Sources_Common/Utilities/base64.h:24-25](Sources_Common/Utilities/base64.h)
- RNG: libsodium `randombytes_buf` is used in CMessage.cpp:468, CMIMESupport.cpp:357, CLocalClient.cpp:3002, encrypt.cpp:93 and CPasswordManagerKeyring.cpp. OpenSSL `RAND_bytes` is used in the SCRAM plugin.
- SHA-256 is in the SCRAM-SHA-256 plugin and in `CCertificate.cpp` (grep hits). The app links OpenSSL/libcrypto, so `EVP_Digest(..., EVP_sha256())` or libsodium `crypto_hash_sha256` are both available.
- Browser launching:
  - `CMulberryApp::LaunchURL(url)` calls `CURLHelpersMap::LaunchURL`.
  - That looks up the URL scheme in the url.helpers map (/usr/lib, /usr/local/lib, /etc, an extra path, ~/.url.helpers) and runs the `snprintf`'d command via `JSimpleProcess::Create(&execd, cmd, kTrue)`.
  - The fallback is `"xdg-open " + url` via `JSimpleProcess`.
  - [Linux/Sources/Support/CURLHelpersMap.cpp:91-141](Linux/Sources/Support/CURLHelpersMap.cpp), [Linux/Sources/Application/General/CMulberryApp.cpp:1877-1880](Linux/Sources/Application/General/CMulberryApp.cpp)
- Attachments also fall back to `xdg-open` via `JExecute`. — [Sources_Common/Mail/Attachments/CAttachment.cpp:2214](Sources_Common/Mail/Attachments/CAttachment.cpp)

### Inferences
- Token endpoint POSTs: the simplest route is libcurl easy-interface synchronous POSTs from a new `Sources_Common/OAuth2/` module (~150–250 lines: form-encoded body, TLS verification left at curl defaults, timeouts, response capture). The libcurl check then has to move out of the HTML-viewer-only block in `configure.in` into a general requirement, and the main binary must link `-lcurl` (Linux/Makefile.in).
  - Trade-off: curl uses the system CA store, not Mulberry's `CCertificateManager` accept/save UI. That is fine and arguably better for well-known IdPs (Google/Microsoft).
  - Reusing the native HTTP stack would need a standalone `CINETClient`-less transport. That would be more invasive.
- PKCE (RFC 7636 S256) needs base64url without padding (~15 lines, a transform of `base64_encode` output), SHA-256 (libsodium `crypto_hash_sha256` or OpenSSL EVP) and 32 random bytes (`randombytes_buf`). All dependencies are present.
- JSON: there is no parser, so one must be added. Token responses are flat objects, so a small hand-written parser (~150–250 lines) or a vendored single-header library (e.g. nlohmann/json, picojson) would work. Vendoring matches the existing `Libraries/vendor` and `Libraries/litehtml` approach.
- Loopback redirect (RFC 8252):
  - Needs a new minimal listener (~150–200 lines): `socket/bind 127.0.0.1:0/listen/accept`, read one HTTP GET, parse `code`/`state`, reply with a small HTML page.
  - It must run in a worker thread or be polled from the JX idle loop, with a timeout and a cancel dialog.
  - `CTCPSocket` cannot be reused as is: the listener is a stub.
  - Alternative for desktop: the device authorization grant (RFC 8628) avoids the listener and the browser launch (the user opens the URL), at the cost of polling.
- Launching the authorize URL: `CMulberryApp::LaunchURL`/`CURLHelpersMap::LaunchURL` exists. Authorize URLs contain `&`. `JSimpleProcess::Create` with a command string probably tokenises without a shell, but that is unverified (it is a JX library function). If it went through a shell, `&` would be a problem. A direct `posix_spawnp("xdg-open", {url})` avoids any quoting risk.

### Gaps
- I did not verify JX's `JSimpleProcess::Create(cmd-string)` parsing semantics (shell or argv split).
- I did not check whether `Linux/Makefile.in` already links curl indirectly through `libHTMLView.a`. The static archive does not carry `-lcurl`, so the main link must already add `HTMLVIEW_SYS_LIBS`; this is plausible but unverified.

## Q7. Where would an "OAuth2" choice appear in the Preferences dialog (Linux/JX)?

### Takeaway
All account types (IMAP, POP3, SMTP, Sieve, remote prefs, address books/LDAP, calendars) embed the same `CPrefsAccountAuth` tab. It has a "Method:" popup with the items: "Plain Text", sorted auth-plugin descriptors, a separator, "Anonymous", "SSL Client Certificate". A card file holds three sub-panels keyed by UI type (UserPswd, Kerberos, Anonymous). Adding "OAuth2" means one popup item, plus either reusing the Plain Text panel (User ID + Save User ID; password fields irrelevant) or adding a small fourth card for provider/client-id. These are minimal GUI changes, which suits the later FLTK rewrite.

### Cited Findings
- `CPrefsAccountAuth::OnCreate` builds the JXLayout: "Method:" `HPopupMenu mAuthPopup`, `JXCardFile mAuthSubPanel`, "Secure:" TLS popup, "Use Client Certificate" checkbox + cert popup. It adds panels `CPrefsAuthPlainText` (card 1), `CPrefsAuthKerberos` (2) and `CPrefsAuthAnonymous` (3). — [Linux/Sources/Application/Preferences_Dialog/Sub-panels/Account_Panels/Authentication_Panels/CPrefsAccountAuth.cpp:48-110](Linux/Sources/Application/Preferences_Dialog/Sub-panels/Account_Panels/Authentication_Panels/CPrefsAccountAuth.cpp)
- `BuildAuthPopup()` builds the menu: `"Plain Text %r"` + plugin descriptors (sorted) + `" %l |Anonymous %r |SSL Client Certificate %r"`. — [CPrefsAccountAuth.cpp:265-321](Linux/Sources/Application/Preferences_Dialog/Sub-panels/Account_Panels/Authentication_Panels/CPrefsAccountAuth.cpp)
- `SetAuthPanel()` maps a plugin UI type, or the "Plain Text" literal, to a card index. — [CPrefsAccountAuth.cpp:235-263](Linux/Sources/Application/Preferences_Dialog/Sub-panels/Account_Panels/Authentication_Panels/CPrefsAccountAuth.cpp)
- `UpdateData()` calls `account->GetAuthenticator().SetDescriptor(popup text)` ("None" for the Anonymous item) and then `mCurrentPanel->UpdateAuth(...)`. — [CPrefsAccountAuth.cpp:200-233](Linux/Sources/Application/Preferences_Dialog/Sub-panels/Account_Panels/Authentication_Panels/CPrefsAccountAuth.cpp)
- `CPrefsAuthPlainText` has "User ID:" `CTextInputField`, "Save User ID" and "Save Password" checkboxes. — [Authentication_Panels/CPrefsAuthPlainText.cpp:41-60](Linux/Sources/Application/Preferences_Dialog/Sub-panels/Account_Panels/Authentication_Panels/CPrefsAuthPlainText.cpp)
- `CPrefsAccountAuth` is instantiated by CPrefsMailAccount.cpp:32, CPrefsSMTPAccount.cpp:33, CPrefsPOP3Account, CPrefsSIEVEAccount, CPrefsRemoteAccount, CPrefsAdbkAccount, CPrefsLDAPAccount and CPrefsRemoteCalendarAccount (grep). — [Linux/Sources/Application/Preferences_Dialog/Sub-panels/Account_Panels/](Linux/Sources/Application/Preferences_Dialog/Sub-panels/Account_Panels/)
- The JX layout source is `auth.fd` in the same directory. MacOS/ and Win32/ have parallel panel trees (CPrefsAccountAuth/CPrefsAuth*), which are not built on FreeBSD. — [Authentication_Panels/auth.fd](Linux/Sources/Application/Preferences_Dialog/Sub-panels/Account_Panels/Authentication_Panels/auth.fd)

### Inferences
- Minimal GUI plan:
  - Add `" |OAuth2 %r"` before the separator in `BuildAuthPopup`, plus a mapping in `SetAuthPanel` and `SetData` (select the item when the type is `eOAuth2`). ~15 lines.
  - Reuse card 1 (User ID = e-mail address) or add `CPrefsAuthOAuth2` (User ID + provider popup "Google/Microsoft/Custom" + optional client id), ~120 lines incl. JXLayout.
  - Put provider defaults (endpoints, scopes, client ids) in a data table in Sources_Common, not in the GUI, so the FLTK rewrite only needs a popup and a field.
  - The interactive "Sign in…" action can happen lazily at first connection (via the PromptUser hook), so no GUI button is strictly required.
- Because `SetAuthPanel` derives the card from `CAuthPlugin::EAuthPluginUIType`, an OAuth2 plugin with a new UI type would need edits in CAuthPlugin.h, CAuthPluginDLL.h, CAuthenticator.cpp and this panel. That is more coupling than a built-in `eOAuth2` type.

### Gaps
- I did not inspect how `CPrefsAuthPanel::SetAuth/UpdateAuth` cast the letter (likely `static_cast<CAuthenticatorUserPswd*>`). If OAuth2 reuses card 1 with a non-UserPswd letter, that cast would be unsafe, so it must be checked.

## Q8. How does logging work, and what controls authentication data in logs?

### Takeaway
`CLog` has per-protocol log types: IMAP, POP3, IMSP, ACAP, SMTP, HTTP, Plugin, Filters, Exceptions. There is no separate "auth" type. A global flag `CLog::AllowAuthenticationLog()` ("Show Authentication Details" checkbox in the Logging Options dialog, default off) controls whether SASL exchanges are logged. The SASL paths honour it. **The HTTP request-header logging does not:** WebDAV `DoRequest` logs the full request headers, including `Authorization:`, whenever HTTP logging is on. A Bearer token would leak there unless redacted.

### Cited Findings
- `CLog::ELogType { eLogIMAP, eLogPOP3, eLogIMSP, eLogACAP, eLogSMTP, eLogHTTP, eLogPlugin, eLogFilters, eLogExceptions }`. `SLogOptions` has `mAuthentication`. `static bool AllowAuthenticationLog()`. — [Sources_Common/Mail/CLog.h:37-61, 105-106, 133](Sources_Common/Mail/CLog.h)
- `sAllowAuthenticationLog` defaults to false and is stored with the log options (bit `cAuthenticationBit` / "Authentication" section). — [Sources_Common/Mail/CLog.cpp:45, 214, 344, 444, 504](Sources_Common/Mail/CLog.cpp)
- At startup, a "LoggingInsecure" caution alert is shown if any logs are active. — [Linux/Sources/Application/General/CMulberryApp.cpp:676-681](Linux/Sources/Application/General/CMulberryApp.cpp)
- The UI checkbox "Show Authentication Details" is `mAuthentication`. — [Linux/Sources/Application/Preferences_Dialog/CLoggingOptionsDialog.cpp:120-123, 208, 221](Linux/Sources/Application/Preferences_Dialog/CLoggingOptionsDialog.cpp)
- SASL code uses `StValueChanger<bool> value(mAllowLog, CLog::AllowAuthenticationLog())`. — [CINETClient.cpp:1058, 1112, 1252](Sources_Common/Mail/INET_Clients/CINETClient.cpp); [CSMTPSender.cpp:1954, 2023](Sources_Common/Mail/INET_Clients/SMTP/CSMTPSender.cpp); [CPOP3Client.cpp:198, 218](Sources_Common/Offline/Mail/CPOP3Client.cpp)
- Plugin exchanges are logged only `if (CLog::AllowAuthenticationLog())`. — [CAuthPlugin.cpp:298-302, 325, 370, 386](Sources_Common/Plugins/Authentication/CAuthPlugin.cpp)
- WebDAV `DoRequest()`: `if (mAllowLog) { mLog.LogEntry("<-------- BEGIN HTTP REQUEST -------->"); hdrs.ConvertEndl(); mLog.AddEntry(hdrs); }`, where `hdrs = request->GetRequestHeader()`. `GetRequestHeader` includes the Authorization header produced by `CHTTPSession::WriteHeaderToStream`. No redaction or `AllowAuthenticationLog()` check is visible in this path. — [CWebDAVCalendarClient.cpp:2018-2030](Sources_Common/Calendar_Store/Clients/CWebDAVCalendarClient.cpp), [CHTTPRequestResponse.cpp:122-158](Sources_Common/HTTP/HTTPClient/CHTTPRequestResponse.cpp), [CHTTPSession.cpp:76-78](Sources_Common/HTTP/HTTPClient/CHTTPSession.cpp)
- Offline playback logging also checks `AllowAuthenticationLog()`. — [Sources_Common/Offline/Recording/CMailRecord.cpp:1721 ff.](Sources_Common/Offline/Recording/CMailRecord.cpp)

### Inferences
- Requirements for OAuth2:
  1. The SASL XOAUTH2/OAUTHBEARER send must happen under the same `StValueChanger(mAllowLog, ...)` guard. Better still, never log the token even when "Show Authentication Details" is on: log `AUTHENTICATE XOAUTH2 <redacted>` and decode/log only the server's JSON error.
  2. Add `Authorization:` header redaction in the three WebDAV `DoRequest()` functions, or centrally in `GetRequestHeader` used for logging. This also fixes the existing Basic credential leak (~10–20 lines).
  3. Token-endpoint traffic (curl) must not enable `CURLOPT_VERBOSE` with headers/bodies. A dedicated log type (e.g. `eLogOAuth`, or reuse `eLogHTTP`) should log only endpoint, status and error codes, never `code`, `code_verifier`, `refresh_token` or `access_token`.
  4. The `LoggingInsecure` alert logic needs no change.
- Adding a new `ELogType` touches CLog.cpp bit tables (`cLogBits`), the Linux logging dialog and the Mac/Win versions. Reusing `eLogHTTP` avoids that.

### Gaps
- I did not verify whether `CINETClient::SendString` (IMAP command send path) logs outgoing data only `if (mAllowLog)`. It is assumed from the `StValueChanger` pattern.

## Q9. Overall integration points and size estimate

### Takeaway
OAuth2 fits best as a **built-in authenticator type** (`CAuthenticator::eOAuth2`, letter `CAuthenticatorOAuth2`), not as a dlopen plugin. The plugin ABI is constrained by 255-byte password buffers and three UI types, and plugins are never consulted for HTTP. New code goes in a shared `Sources_Common` OAuth2 module plus small branches at roughly 10 existing call sites. Rough total: about 1,500–2,300 new or changed lines, excluding a vendored JSON library.

### Cited Findings
- The call sites that switch on the authenticator type (all need an `eOAuth2` case) are:
  - CINETClient.cpp:638-654 (LookupServer) and 999-1017 (DoAuthentication);
  - CSMTPSender.cpp:1387-1400 and 1914-2070;
  - CWebDAVCalendarClient.cpp:1935-2010, CWebDAVVCardClient.cpp:1787, CWebDAVPrefsClient.cpp:686;
  - CLDAPClient.cpp:369, 449;
  - CMailControl.cpp:944-970 (PromptUser);
  - CPasswordManagerKeyring.cpp:74-149 (GetAccountURL, for UID);
  - CPrefsAccountAuth.cpp:200-321.
  - (Grep results above.)
- `CINETAccount::GetAuthenticatorUserPswd()` blindly casts the letter to `CAuthenticatorUserPswd*` and is used in many places, e.g. BURL/URLAUTH in CSMTPSender.cpp:228-233, 451-456. A new letter type must not be reached through those casts. Either guard them by `RequiresUserPswd()` or make `CAuthenticatorOAuth2` derive from `CAuthenticatorUserPswd` (UID reuse; password unused). — [Sources_Common/Application/Preferences/CINETAccount.h:138-141](Sources_Common/Application/Preferences/CINETAccount.h), [CSMTPSender.cpp:228-233](Sources_Common/Mail/INET_Clients/SMTP/CSMTPSender.cpp)

### Inferences
Estimated work items (lines are rough estimates):
1. `CAuthenticatorOAuth2` + enum/descriptor/SASL-ID plumbing + Get/SetInfo + copy/compare: ~200.
   - Deriving from `CAuthenticatorUserPswd` minimises churn: `RequiresUserPswd()` stays true for UID purposes. But `PromptUser`/`DoUserIDPassword` would then show a password dialog, so they need an explicit OAuth2 branch.
2. `Sources_Common/OAuth2/` module, ~900–1300 lines:
   - provider table (Google/Microsoft/custom: auth/token endpoints, scopes, client id/secret), ~100;
   - PKCE + state + base64url, ~60;
   - loopback listener, ~200;
   - browser launch, reusing `CURLHelpersMap::LaunchURL` or posix_spawn, ~20;
   - token POST via libcurl, ~200;
   - JSON parse (small parser ~200, or vendored library);
   - token cache with expiry + per-account mutex + refresh logic, ~200;
   - keyring storage of the refresh token, ~80;
   - UI task (`CTaskSync`) for "waiting for browser sign-in / cancel", ~80 Sources_Common + Linux dialog ~100.
3. SASL: built-in XOAUTH2 and OAUTHBEARER payload builders, plus `CINETClient::DoOAuth2Authentication` with error-continuation handling: ~150. Capability flags in IMAP/POP3/Sieve: ~40. SMTP case with a direct stream write (1024-byte buffer limit): ~80.
4. HTTP: `CHTTPAuthorizationBearer` ~40, plus three `GetAuthorization` cases ~45, plus Authorization-header log redaction ~20.
5. `PromptUser`/retry policy changes: ~80.
6. Preferences GUI (JX): popup item + optional small card: ~50–150.
7. Build: make libcurl a general dependency in `configure.in` (currently HTML-viewer-only) and link it into the main binary. Add new source files to `Linux/Make.files`/Makefile.in.

Risks to watch:
- XOAUTH2 failure continuation is accepted as success by `INETProcess` (Q2).
- SMTP 1024-byte buffer.
- 255-byte plugin password buffers, if the plugin route were chosen.
- Refresh token storage must avoid the prefs file and remote prefs (Q4).
- Authorization header logging (Q8).
- Concurrent refresh across cloned IMAP connections.

### Gaps
- Provider-specific requirements (Google/Microsoft client registration, loopback rules, scopes such as `https://mail.google.com/`, `https://outlook.office.com/IMAP.AccessAsUser.All`, `offline_access`) are outside this local-code scope and are covered by other researchers.
- The line estimates are judgement-based, not measured.
