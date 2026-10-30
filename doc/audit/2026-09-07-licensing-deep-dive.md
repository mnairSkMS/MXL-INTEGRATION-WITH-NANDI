# Licensing Subsystem — Deep Dive

**Date:** 2026-09-07
**Scope:** every code path, artefact, and build step that participates in license
creation, embedding, loading, validation, and enforcement in this checkout.
**Evidence basis:** source read in this tree, plus direct decryption of the four shipped
`.lic` files using the key material committed alongside them. No build or live
license-server run was performed — items depending on those are marked
**integration gated**.

---

## 1. Executive summary

The transcoder ships a self-contained, home-grown licensing system built on OpenSSL
primitives:

- A license is a **PEM-wrapped JSON envelope** holding an **AES-256-GCM-encrypted**
  payload plus a detached **Ed25519 signature**.
- The **AES key** and the **Ed25519 public key** are not on disk at runtime — they are
  encrypted into a custom ELF section (`.skn`) of the `transcoder` binary itself, keyed
  by a **SHA-256 hash of the binary's own sections**. Patch the binary and the keys stop
  decrypting.
- The binary validates the license **once at startup** (`main.cpp`) and then **once per
  second for the process lifetime** (`LicenseManager`, GLib timeout source).
- Two enforcement modes: **offline** (local clock vs. `valid_since`/`ttl` only) and
  **online** (a remote license server issues a signed JWT session token that must be
  refreshed; enforces `max_sessions`).
- On invalid/expired license the manager fires a callback that stops the whole
  application.

There are **no per-feature entitlements**. The license grants time and (online only) a
session count — nothing else. Every codec, output, plugin, and API in the product is
available to any holder of any valid license.

The two most serious findings are that **the production Ed25519 signing private key is
committed to this repository** (§7.1) and that **an invalid license produces process exit
code 0** (§7.2).

---

## 2. Component map

| Layer | Location | Role |
| --- | --- | --- |
| Crypto primitives | [transcoder/lib/crypto/](../../transcoder/lib/crypto/) | OpenSSL-backed AES-GCM cipher, SHA-256 hasher, Ed25519 signature, key loading |
| License library | [transcoder/lib/license/](../../transcoder/lib/license/) | Model, envelope, serializers, signature wrapper, ELF read/hash/modify, key embed/extract |
| Application glue | [transcoder/app/src/main.cpp](../../transcoder/app/src/main.cpp), [transcoder/app/src/application/license/manager.cpp](../../transcoder/app/src/application/license/manager.cpp) | Startup load/validate; runtime enforcement loop |
| Config surface | [transcoder/app/include/application/config/application.h](../../transcoder/app/include/application/config/application.h), [parser/json/parser.cpp](../../transcoder/app/src/application/config/parser/json/parser.cpp) | `application.licenseServer.uri` |
| Management API | [api/src/transcoder_api/routers/license.py](../../api/src/transcoder_api/routers/license.py), [services/license.py](../../api/src/transcoder_api/services/license.py) | `GET/PUT/DELETE /license`: inspect, replace-and-restart, stop-and-remove, via `transcoder --license-info` |
| Operator tools | [transcoder/license/tools/](../../transcoder/license/tools/) | `keyGenerator.sh`, `licenseGenerator`, `licenseSigner`, `licenseInfo`, `keyEmbedder` |
| Key & license artefacts | [transcoder/license/dev/](../../transcoder/license/dev/), [transcoder/license/prod/](../../transcoder/license/prod/) | Committed key sets and sample licenses |
| Packaging | [docker/utpl-license-toolkit/Dockerfile](../../docker/utpl-license-toolkit/Dockerfile), [docker/utpl-transcoder/Dockerfile](../../docker/utpl-transcoder/Dockerfile) | Toolkit image; runtime image + supervisor invocation |
| Tests | [transcoder/lib/license/test/src/serialize.cpp](../../transcoder/lib/license/test/src/serialize.cpp) | One round-trip serialization unit test |

Two static libraries are produced from the same sources
([transcoder/lib/license/CMakeLists.txt](../../transcoder/lib/license/CMakeLists.txt)):

- **`transcoderLicense`** — read-only path (reader, hasher, `keyLoader`, signature,
  serializers). Linked into the shipped `transcoder` executable.
- **`toolsLicense`** — the same plus `modifier.cpp` and `keyStorer.cpp` (ELF *write*
  capability). Linked only into the offline tools and the unit test.

That split is deliberate and correct: the shipped binary contains no code that can write
a `.skn` section.

---

## 3. The license artefact

### 3.1 On-disk format

Produced by `JsonLicenseFileSerializer`
([file/serializer/json.cpp](../../transcoder/lib/license/src/utpl/license/file/serializer/json.cpp)):

```
-----BEGIN TRANSCODER LICENSE-----
<base64 of the envelope JSON, wrapped>
-----END TRANSCODER LICENSE-----
```

Header and footer strings are both the literal `TRANSCODER LICENSE`; PEM wrapping is
`utils::pemFormat` / `utils::pemParse` in
[transcoder/lib/common/src/utpl/common/utils.cpp:246](../../transcoder/lib/common/src/utpl/common/utils.cpp#L246).

### 3.2 Envelope

```json
{ "enc": "<base64>", "sig": "<base64>" }
```

- `sig` — raw 64-byte Ed25519 signature **over the bytes of `enc` as decoded from base64**
  (i.e. over the ASCII string described next, not over the ciphertext directly).
- `enc` — base64 of an ASCII string of the form:

```
base64(AES-256-GCM ciphertext ‖ 16-byte GCM tag) "." base64(12-byte IV)
```

Note the ordering: **ciphertext first, IV second**, joined by `.`. This is inverted
relative to the `.skn` section format (§4.2), which puts the IV first. Both are internally
consistent, but the asymmetry is a readability trap.

### 3.3 Plaintext payload

`JsonLicenseSerializer`
([serializer/json.cpp](../../transcoder/lib/license/src/utpl/license/serializer/json.cpp)):

```json
{
  "data": {
    "id":           "<uuid or operator-supplied string>",
    "name":         "<human readable>",
    "valid_since":  <int64 ms since Unix epoch>,
    "ttl":          <int64 ms>,
    "max_sessions": <size_t>,
    "offline":      <bool>
  },
  "meta": { "created_at": <int64 ms since Unix epoch> }
}
```

Field semantics ([model.h](../../transcoder/lib/license/include/utpl/license/model.h)):

| Field | Default | Meaning |
| --- | --- | --- |
| `id` | generated GUID | Opaque identifier. Not used in any enforcement decision by the client. |
| `name` | `"License <id>"` | Logged at startup only. |
| `valid_since` | `UTPL_LICENSE_EPOCH_START` = `2020-01-01` | Start of validity, ms. |
| `ttl` | `0` (rejected by the generator) | Duration in **milliseconds**. Expiry = `valid_since + ttl`. |
| `max_sessions` | `1` | Concurrent sessions. **Server-side only** — the client never reads it for enforcement. |
| `offline` | `false` in the C++ model; **`true`** when the key is absent from a decrypted payload (backward compatibility, [serializer/json.cpp](../../transcoder/lib/license/src/utpl/license/serializer/json.cpp)) | Selects offline vs. online enforcement. |

`meta.created_at` is written and displayed but never validated.

### 3.4 What the shipped licenses actually contain

Decrypted in this session with the committed keys:

| File | Name | Valid from | Expires | `max_sessions` | `offline` | Signature |
| --- | --- | --- | --- | --- | --- | --- |
| `prod/licenses/offline.lic` | Subscription for the 7bulls.com development team | 2025-01-01 | **2035-01-01** | 1 | true | Valid |
| `prod/licenses/online.lic` | Subscription for the 7bulls.com development team | 2025-01-01 | **2035-01-01** | 2 | false | Valid |
| `dev/licenses/offline.lic` | Development license | 2025-01-01 | **2035-01-01** | 1 | true | Valid |
| `dev/licenses/online.lic` | Development license | 2025-01-01 | **2035-01-01** | 2 | false | Valid |

All four are 10-year (3652-day) licenses created 2025-03-25. The **prod offline** one is
the license baked into the test image
([docker/utpl-transcoder-test/Dockerfile:25](../../docker/utpl-transcoder-test/Dockerfile#L25))
and bind-mounted by
[docker-compose.playlist.yml:73](../../docker-compose.playlist.yml#L73).

---

## 4. Key material and binary embedding

### 4.1 The key set

`keyGenerator.sh`
([transcoder/license/tools/keyGenerator/keyGenerator.sh](../../transcoder/license/tools/keyGenerator/keyGenerator.sh))
produces three files with plain `openssl`:

| File | Content | Used by |
| --- | --- | --- |
| `license_ed25519_private_key.der` | Ed25519 private key, DER | `licenseSigner` only |
| `license_ed25519_public_key.der` | Ed25519 public key, DER | Embedded in the binary; verifies `.lic` signatures **and** license-server JWTs |
| `license_encryption_key.bin` | 32 raw random bytes | Embedded in the binary; AES-256 key for the payload |

The build selects the set by CMake build type
([transcoder/app/CMakeLists.txt:116-137](../../transcoder/app/CMakeLists.txt#L116-L137)):
**Debug → `license/dev/keys`, anything else → `license/prod/keys`**. A POST_BUILD custom
command runs `keyEmbedder` against the freshly linked `transcoder` executable. Consequence:
a Release binary rejects dev-signed licenses and vice versa, and **rotating production keys
requires rebuilding and redistributing every image**.

### 4.2 The `.skn` ELF section

Written by `ElfKeyStorer::store`
([elf/keyStorer.cpp](../../transcoder/lib/license/src/utpl/license/elf/keyStorer.cpp)),
read by `ElfKeyLoader::load`
([elf/keyLoader.cpp](../../transcoder/lib/license/src/utpl/license/elf/keyLoader.cpp)).

Section payload:

```
base64(IV) "." base64(AES-256-GCM(  base64(encryption_key) "." base64(public_key)  ))
```

The AES key protecting the section is **derived from the binary itself**
([utils/elf/hasher.cpp](../../transcoder/lib/license/src/utpl/license/utils/elf/hasher.cpp)):

```
elfHash = SHA256( SHA256(sec_1) ‖ SHA256(sec_2) ‖ … )
```

over every section with non-zero size, excluding:

- sections whose name starts with `.debug_` (skipped at read time in
  [utils/elf/reader.cpp](../../transcoder/lib/license/src/utpl/license/utils/elf/reader.cpp)),
- `SHT_STRTAB` sections,
- `SHT_DYNAMIC` sections,
- `.skn` itself (passed as an ignore-set by both storer and loader).

`elfHash` is fed to `crypto.loadKey()` as raw 32-byte AES-256 key material. Because
AES-GCM authenticates, any binary edit that changes a hashed section changes the derived
key, the GCM tag check fails, and `ElfKeyLoader::load` throws — the transcoder cannot
obtain its own keys.

Section insertion is done by shelling out to **`objcopy`**
([utils/elf/modifier.cpp](../../transcoder/lib/license/src/utpl/license/utils/elf/modifier.cpp)),
found via `boost::process::search_path`. `removeSection` runs first so the operation is
idempotent.

> **Documentation mismatch.** `doc/license/license-format.md` describes the exclusion set
> as "non-DWARF and non-STRTAB". The code also excludes `SHT_DYNAMIC`. Minor, but it
> matters to anyone reimplementing the derivation.

### 4.3 Environment-variable key injection

All three tools accept keys as **hex strings in environment variables**
([license/config.h](../../transcoder/lib/license/include/utpl/license/config.h)) instead of
file paths, which keeps the private key off disk in a signing CI job:

| Variable | Consumed by |
| --- | --- |
| `LICENSE_PRIVATE_KEY` | `licenseSigner` |
| `LICENSE_PUBLIC_KEY` | `keyEmbedder`, `licenseInfo` |
| `LICENSE_ENCRYPTION_KEY` | `keyEmbedder`, `licenseGenerator`, `licenseInfo` |

---

## 5. Runtime flow

### 5.1 Startup — [transcoder/app/src/main.cpp](../../transcoder/app/src/main.cpp)

1. `-l/--license <path>` is mandatory; existence and non-directory are checked
   ([main.cpp:91](../../transcoder/app/src/main.cpp#L91)).
2. `ElfKeyLoader::load(boost::dll::program_location())` — the process hashes **its own
   on-disk image** and decrypts `.skn`.
3. The `.lic` file is PEM-parsed into a `LicenseFile` (`enc` + `sig`).
4. `LicenseSignature::verify(enc, sig, publicKey)` — Ed25519, one-shot
   `EVP_DigestVerify`. Failure throws `GenericLicenseException("License is not valid")`.
5. `JsonLicenseSerializer::deserialize(enc, encryptionKey)` — AES-256-GCM decrypt +
   tag check + JSON parse.
6. Preliminary local validity check against `Time::getUTCTimestamp()`: expired → error,
   not-yet-valid → error. Carries a `// TODO: make sure if it has sense for online
   licenses` comment ([main.cpp:151](../../transcoder/app/src/main.cpp#L151)) — the check
   currently applies to online licenses too.
7. Logs `Have a correct license '<name>', valid until <ts>, [not ]offline capable`.
8. `Application::configure(...)` receives the license, its path, and the public key.

### 5.2 Configuration validation — [application.cpp:111-131](../../transcoder/app/src/application.cpp#L111-L131)

For a **non-offline** license only: `application.licenseServer.uri` must be present and
must parse to an explicit `http`/`https` scheme, otherwise `InvalidArgumentException`.
Offline licenses ignore the field entirely.

The config key is parsed at
[parser/json/parser.cpp:1171](../../transcoder/app/src/application/config/parser/json/parser.cpp#L1171)
and documented in [configuration-reference.md](../configuration-reference.md).

### 5.3 Enforcement loop — [manager.cpp](../../transcoder/app/src/application/license/manager.cpp)

`LicenseManager::start()` installs `g_timeout_add_seconds(1, _onCheckLicense, this)` on the
GLib main loop ([manager.cpp:432](../../transcoder/app/src/application/license/manager.cpp#L432)).
State machine: `UNKNOWN → VALID | INVALID`. **Only transitions** invoke the callback.

`Application::start` wires the callback
([application.cpp:429-438](../../transcoder/app/src/application.cpp#L429-L438)):

- `VALID` → `_transcoder.start(...)`. This is why the media pipeline only comes up on the
  *first successful tick*, roughly one second after process start.
- anything else → `Application::stop()` — API server, license manager, and transcoder are
  all torn down.

`_onCheckLicense` returns `false` for `INVALID`, which also removes the GLib source.

#### Offline path

`verifyExpiration` stays `true`; no network I/O. Each tick calls
`LicenseUtils::isValid(license, Time::getUTCTimestamp())`
([license/utils.h](../../transcoder/lib/license/include/utpl/license/utils.h)) —
`!isExpired && !isNotYetValid`, i.e. `valid_since <= now <= valid_since + ttl`. Expiry logs
*"License has expired. Requesting transcoder to stop."*; a clock before `valid_since` logs
*"The license is not yet valid. The system clock may have been tampered with."*

#### Online path

`verifyExpiration` is set to `false`
([manager.cpp:206](../../transcoder/app/src/application/license/manager.cpp#L206)) — the
local expiry test is **skipped entirely** and the server becomes the sole authority after
startup.

**Session creation** — `POST <uri>/create_session`, `Content-Type: application/json`, body
= the raw envelope JSON decoded from the `.lic` PEM (`{"enc":…,"sig":…}`).

| Status | Handling |
| --- | --- |
| `200` | Parse token (below); reset `httpAttempts`. |
| `400` | *"Your license is invalid or expired, or the number of allowed concurrent sessions has been reached."* → `INVALID`. |
| `0` (transport) | `OPERATION_TIMEDOUT` → increment `httpAttempts`, keep previous status. Any other transport error → immediate `INVALID`. |
| other | `INVALID`. |

`httpAttempts == LICENSE_SERVER_ATTEMPTS` (3) → `INVALID`. Timeout is 3000 ms.
Effective grace period for a dead server is therefore ~3 consecutive timed-out attempts.

**Token verification** — `_processTokenResponse`:

1. `content-type` must start with `application/json`; body non-empty.
2. Body must contain `token`; the JWT must have exactly 3 dot-separated parts.
3. Header `alg` must be exactly `EdDSA` — anything else, including `none`, is rejected.
4. Signature over `header.payload` verified with the **same embedded license public key**.
   (So the license server must sign JWTs with the license signing private key.)
5. Payload must contain `session_id`, which becomes `sessionToken`.
6. Refresh deadline: `serverTime` from the HTTP `Date:` header via `curl_getdate`
   (falling back to local time with a warning); `exp` from the payload (falling back to
   `serverTime + 60`); then
   `sessionTokenCheckTimestamp = monotonic_now + (exp - serverTime) * 1000 - 3000`.

**Heartbeat** — once `sessionToken` is set, ticks are no-ops until the refresh deadline,
then `POST <uri>/heartbeat/<session_id>` with an empty body.

| Status | Handling |
| --- | --- |
| `200` | Re-run token verification, refresh deadline. |
| `404` | *"Current session token is not valid"* → clear `sessionToken`; next tick opens a new session. Status stays `VALID`. |
| `0` / other | Same rules as `create_session`. |

Base URLs are joined with `boost::urls` + `std::filesystem::path`
([manager.cpp:35-47](../../transcoder/app/src/application/license/manager.cpp#L35-L47)), so a
base with or without a trailing slash works.

**There is no license server in this repository.** The endpoint contract above is inferred
purely from the client. Everything in the online path is **integration gated**.

---

## 6. Feature inventory

### 6.1 What the licensing system supports today

| # | Capability | Where | Status |
| --- | --- | --- | --- |
| F1 | PEM-armoured, email-safe license file | `file/serializer/json.cpp` | Working |
| F2 | AES-256-GCM confidentiality + integrity of the payload | `serializer/json.cpp`, `openssl/cipher.cpp` | Working |
| F3 | Ed25519 detached signature over the payload | `signature.cpp` | Working |
| F4 | Keys hidden in the binary, bound to the binary's own hash | `elf/keyStorer.cpp`, `elf/keyLoader.cpp` | Working |
| F5 | Tamper detection — patched binary cannot decrypt its own keys | `utils/elf/hasher.cpp` | Working (by construction) |
| F6 | Dev/prod key separation driven by CMake build type | `app/CMakeLists.txt:116` | Working |
| F7 | Time-boxed licenses (`valid_since` + `ttl`), subscription-style | `license/utils.h` | Working |
| F8 | Backward clock-set detection (`isNotYetValid`) | `license/utils.h` | Working, narrow |
| F9 | Offline mode — zero network dependency | `manager.cpp` | Working |
| F10 | Online mode — server session + Ed25519 JWT + heartbeat | `manager.cpp` | **Integration gated** (no server here) |
| F11 | Concurrent-session cap (`max_sessions`) | license payload | **Server-side only**; client never enforces it |
| F12 | Bounded tolerance for a transient server outage (3 × 3 s) | `manager.cpp:29-30` | Working |
| F13 | Hard stop of the whole app on license loss | `application.cpp:429` | Working |
| F14 | Key generation tool | `keyGenerator.sh` | Working |
| F15 | License request generation | `licenseGenerator` | Working |
| F16 | Detached signing (separable from generation — private key never needs to touch the generator host) | `licenseSigner` | Working |
| F17 | Inspection + signature verification | `licenseInfo` | Working |
| F18 | Key embedding | `keyEmbedder` | Working |
| F19 | Secrets via env vars instead of files | `license/config.h` | Working |
| F20 | Portable toolkit container | `docker/utpl-license-toolkit/` | Working |
| F21 | Dedicated `license` logger channel | `license/debug.h`, `application/logger.h` | Working |

### 6.2 What the licensing system does *not* do

These are absences, not defects — listed so nobody assumes coverage that isn't there.

- **No feature entitlements.** No codec, resolution, output-count, channel-count, plugin,
  or API gating. The model carries no capability field at all. Every build is fully
  featured for any valid license.
- **No node locking.** Nothing binds a license to a host, MAC, CPU ID, container, or
  install. One offline `.lic` runs on unlimited machines simultaneously.
- **No revocation for offline licenses.** Once issued, an offline license is valid until
  its `ttl` elapses. There is no CRL, no kill switch, no phone-home.
- **No usage metering or reporting.** Nothing counts channels, hours, or streams.
- **No grace/warning period.** Expiry is a cliff: the tick that crosses the boundary
  stops the application. No "expires in 7 days" warning is emitted anywhere.
- **No trusted time source.** Offline validation trusts `system_clock` unconditionally.
- **No stop reason in the control plane.** `GET /license` reports the installed
  license's status, name, expiry and mode, but nothing surfaces *why* the transcoder
  exited: a license fault exits 0 (§7.2) and Supervisor shows the program as `EXITED`.
- **No hot reload.** Replacing the license requires a process restart; `PUT /license`
  performs that restart, which takes the channel off air.
- **No license-server source** in this tree beyond the zipped copy in
  [license-server/](../../license-server/), which implements only session creation and
  heartbeat — no issuing, listing, or revocation.

---

## 7. Issues

Ordered by severity. Each is stated with its evidence location.

### 7.1 Critical — the production signing private key is in the repository

[transcoder/license/prod/keys/license_ed25519_private_key.der](../../transcoder/license/prod/keys/license_ed25519_private_key.der)

Verified in this session: deriving the public key from that private key yields a byte-exact
match for `prod/keys/license_ed25519_public_key.der` — the key embedded into every
**Release** build. Anyone with a copy of this checkout can mint unlimited, arbitrarily
long, self-signed production licenses that the shipped transcoder accepts. The AES
encryption key is committed next to it, so payloads can also be freely forged and read.

This directly contradicts the project's own documentation, which states *"The production
private key must never be stored in the repository"* and shows a `prod/keys` tree with no
private key ([doc/license/README.md](../license/README.md)).

Aggravating factors:
- [.gitignore](../../.gitignore) has no exclusion for `transcoder/license/**/keys/`, so the
  key is tracked, not accidentally left untracked.
- The build copies the whole `transcoder/` tree into the Docker build context
  ([docker/utpl-transcoder/Dockerfile:9](../../docker/utpl-transcoder/Dockerfile#L9)),
  putting the private key in the `transcoder-builder` layer. It is not in the final
  runtime stage, but it is in any exported/cached intermediate.

Remediation is not just deletion: the key must be treated as **compromised** — generate a
new prod key set, rebuild and republish all images, and re-issue every outstanding
customer license. Git history (absent from this checkout) must also be scrubbed.

### 7.2 High — an invalid license exits with status 0

[transcoder/app/src/main.cpp:186-188](../../transcoder/app/src/main.cpp#L186-L188)

```cpp
} catch (const utpl::license::GenericLicenseException &ex) {
    ERR(("Cought license related exception: {}", ex.getMessage()));

} catch (const utpl::Exception &ex) {
    ...
    ret = ex.getCode();   // <- every other handler sets ret
```

The license handler is the only catch block that does not assign `ret`, so `main` returns
`utpl::Error::NO_ERROR`. A missing, expired, corrupted, or wrongly-signed license is
therefore indistinguishable from a clean shutdown for any caller inspecting the exit code —
supervisor, CI, orchestrators, `run.sh`. Under
[supervisord.conf](../../docker/utpl-transcoder/supervisord.conf) the `transcoder` program
uses `autorestart=unexpected`, and exit code 0 is "expected", so the container stays up
with a silently dead transcoder.

### 7.3 High — unit mismatch makes the online refresh deadline collapse

[manager.cpp:163,170,188](../../transcoder/app/src/application/license/manager.cpp#L163)

`curl_getdate` returns **seconds**; JWT `exp` is **seconds**; but the fallback
`utpl::Time::getUTCTimestamp()` returns **milliseconds**
([time.cpp:10](../../transcoder/lib/common/src/utpl/common/time.cpp#L10)).

```cpp
serverTime = utpl::Time::getUTCTimestamp();          // ms  (~1.75e12)
...
this->sessionTokenCheckTimestamp = getMonotonicTimestamp() + (expirationTime - serverTime) * 1000;
```

If the server omits an unparsable/absent `Date:` header **and** supplies `exp`,
`expirationTime - serverTime ≈ -1.75e12`, the refresh deadline lands far in the past, and
the client hammers `heartbeat/<id>` **once per second** for the life of the process.

The other three combinations happen to work: with a `Date:` header both values are in
seconds, and with no `exp` the fallback `serverTime + 60` cancels the unit out. Only the
no-`Date` + `exp`-present case breaks, which is exactly the case a slightly non-standard
server hits. Integration gated in the sense that no server exists here to confirm it, but
the arithmetic is unambiguous.

### 7.4 High — blocking HTTP on the GLib main loop

[manager.cpp:432](../../transcoder/app/src/application/license/manager.cpp#L432) plus the
`httpSession->Post()` calls at lines 227 and 296.

The 1-second license tick is a GLib timeout source running on the main loop, and the online
path performs a **synchronous** cpr POST with a 3000 ms timeout inside it. An unresponsive
license server stalls the main loop for up to 3 seconds per tick, and up to 9 seconds
across the three retry attempts. Everything else on that loop — signal handling, the gRPC
API bridge, pipeline bus messages — is blocked for that duration. Offline licenses are
unaffected.

### 7.5 Medium — offline enforcement is defeated by the system clock

`LicenseUtils::isValid` compares against `Time::getUTCTimestamp()`, i.e. the host's
`system_clock`, with no monotonic anchor, no persisted high-water mark, and no signed time
source. Setting the clock to any instant inside `[valid_since, valid_since + ttl]` keeps an
expired license running indefinitely. Inside a container the operator controls this
trivially.

`isNotYetValid` catches only rollback to *before* `valid_since` — which, for the shipped
licenses with `valid_since = 2025-01-01`, is a very small window.

This is a design limit the project already acknowledges ("a deterrent against casual
copying, not a defence against a determined attacker"), but the doc frames the limit as
being about binary tampering; the clock is the cheaper attack and is not called out.

### 7.6 Medium — online mode drops local expiry entirely

[manager.cpp:206](../../transcoder/app/src/application/license/manager.cpp#L206) sets
`verifyExpiration = false` for the whole online branch. After the one-shot startup check in
`main.cpp`, a long-running online transcoder never re-examines `valid_since`/`ttl`. A
process started before expiry keeps running past it as long as the server keeps issuing
tokens. Whether that is correct depends on server behaviour that does not exist in this
tree to inspect — the `// TODO: make sure if it has sense for online licenses` in
`main.cpp` suggests the question is genuinely open.

### 7.7 Medium — OpenSSL cipher init failures are swallowed

[openssl/cipher.cpp:232,237](../../transcoder/lib/crypto/src/utpl/crypto/manager/openssl/cipher.cpp#L232)

```cpp
if (EVP_EncryptInit_ex(...) != 1) {
    utpl::Exception("Failed to initialize encryption");   // constructed, never thrown
}

if (EVP_DecryptInit(this->ctx, this->cipher, key.raw().data(), this->iv.data())) {
    utpl::Exception("Failed to initialize decryption");   // never thrown, and the test is inverted
}
```

Both statements build a temporary exception and discard it. The decrypt test is also
inverted — `EVP_DecryptInit` returns **1 on success**, so the (dead) branch fires on
success. A failed init leaves the context unusable; the error surfaces later as a confusing
`Error updating data!` or a tag-verification failure rather than at the real cause. Same
class of bug at
[openssl/signature.cpp](../../transcoder/lib/crypto/src/utpl/crypto/manager/openssl/signature.cpp)
in `EVP_DigestVerifyUpdate` (dead code for Ed25519, which always uses the one-shot buffer,
but latent for any future streaming algorithm).

### 7.8 Medium — no license stop reason in the operator surface

`GET /license` reports the installed license's status, name, expiry and mode (it runs
`transcoder --license-info`, which uses the binary's embedded keys). It cannot report
online session state, and nothing distinguishes "transcoder stopped because the license
expired" from any other stop. Given §7.2 (exit code 0), the operator's signal for a
license stop is `GET /license` returning `expired` alongside a stopped engine, plus a line
in the container log. The channel UI's `Licence` tab is not yet wired to these routes.

### 7.9 Low — `keyEmbedder` leaves the section blob on disk

[utils/elf/modifier.cpp:26-35](../../transcoder/lib/license/src/utpl/license/utils/elf/modifier.cpp#L26)

`addSection` writes `<binary path>.sec.bin`, passes it to `objcopy`, and never deletes it —
not on success, not on the exception path. After a build, the encrypted `.skn` payload sits
next to `transcoder` as `transcoder.sec.bin`. It is encrypted under the binary hash so it
leaks nothing directly, but it is stray build output that could be picked up by a packaging
glob.

### 7.10 Low — self-assignment in the `LicenseManager` constructor

[manager.cpp:388](../../transcoder/app/src/application/license/manager.cpp#L388)

```cpp
app::LicenseManager::LicenseManager() {
    this->timerId = 0;
    this->license = license;   // assigns the member to itself
```

There is no constructor parameter named `license`; the name resolves to `this->license`.
Harmless in practice (`License` is default-constructed and the real value arrives in
`configure()`), but it is a copy-paste artefact that reads as if the constructor took an
argument.

### 7.11 Low — timer source removed twice on the invalid-license path

`_onCheckLicense` invokes the callback, which reaches `Application::stop()` →
`LicenseManager::stop()` → `g_source_remove(timerId)` — and then returns `false`, which is
GLib's own request to destroy the same source. GLib tolerates destroying a source during
its dispatch, so this is not a crash, but `timerId` is left pointing at a destroyed source
and a subsequent `stop()` would emit a `g_source_remove` critical. Clearing `timerId` when
the callback returns `false` would make the ownership unambiguous.

### 7.12 Low — misleading error message for a config-version mismatch

[parser/json/parser.cpp:255](../../transcoder/app/src/application/config/parser/json/parser.cpp#L255)

```cpp
throw utpl::InvalidArgumentException("License in version " + this->_version.to_string() + " is not supported by this transcoder version");
```

This is the **configuration file** version check, not the license. An operator with an
out-of-range `config.json` gets told their license is unsupported and will go looking in
entirely the wrong place.

### 7.13 Low — dead private-key handling in `licenseGenerator`

[licenseGenerator/src/main.cpp](../../transcoder/license/tools/licenseGenerator/src/main.cpp)
declares `OPT_NAME_PRIVATE_KEY`, loads `LICENSE_PRIVATE_KEY` from the environment into a
`privateKey` unique_ptr, and never registers the option or uses the key. The separation of
generation from signing is deliberate and good — the dead code just invites someone to
"fix" it by merging the two steps, which would put the private key back on the generating
host.

### 7.14 Low — test coverage is one round trip

[transcoder/lib/license/test/src/serialize.cpp](../../transcoder/lib/license/test/src/serialize.cpp)
is the only automated licensing test. It covers serialize → deserialize for `id`, `name`,
`ttl`, `valid_since`, `created_at`. It does **not** cover:

- signature signing/verification, or rejection of a bad signature,
- `offline` and `max_sessions` round-tripping (including the absent-key → `true` default),
- expiry / not-yet-valid boundary conditions in `LicenseUtils`,
- `.skn` embed → extract, or the tamper-detection property that the whole design rests on,
- any `LicenseManager` state transition, online or offline.

The product integration suite ([tests/](../../tests/)) has no licensing tests at all; it
just consumes a `.lic` to make the transcoder start
([tests/framework/test.sh:492](../../tests/framework/test.sh#L492)), with a `# FIXME`
fallback to `/tmp/transcoder.lic` at line 807. The handbook's own verification list
([doc/codebase/22-licensing-crypto-and-security.md](../codebase/22-licensing-crypto-and-security.md))
enumerates exactly the scenarios that are missing.

### 7.15 Informational — format and documentation inconsistencies

- **IV ordering is inverted between the two containers.** License envelope is
  `ciphertext "." iv`; `.skn` section is `iv "." ciphertext`.
- **`offline` default disagrees with itself.** `LicenseModel` initialises `_offline = false`;
  a decrypted payload missing the key is treated as `true`. A license generated with the
  current tool always carries the key, so this only affects pre-`offline` licenses — but the
  two defaults point in opposite directions.
- **`.skn` hash exclusions** are documented as DWARF + STRTAB; the code also excludes
  `SHT_DYNAMIC` (§4.2).
- **`doc/license/README.md` describes a `prod/keys` tree with no private key**, which is not
  what this checkout contains (§7.1).
- **`licenseGenerator` defaults to an *online* license** (`-O` defaults to
  `LicenseModel::isOffline()` = `false`). An operator who omits `-O` produces a license that
  refuses to start unless `application.licenseServer.uri` is also configured. The tools doc
  records the default correctly but does not flag the coupling.

---

## 8. Operator quick reference

Generate a key set (once per environment):

```sh
keyGenerator.sh -o ./keys
```

Issue a license (two hosts: generation needs only the AES key; signing needs the private key):

```sh
licenseGenerator -l keys/license_encryption_key.bin \
                 -s 2026-01-01 -t 365 \
                 -n "Customer X" -c 4 -O true \
                 -o transcoder.lsr

LICENSE_PRIVATE_KEY=$(xxd -p -c0 keys/license_ed25519_private_key.der) \
  licenseSigner -i transcoder.lsr -o transcoder.lic
```

Inspect:

```sh
licenseInfo -l keys/license_encryption_key.bin \
            -p keys/license_ed25519_public_key.der \
            -i transcoder.lic
```

Deploy: mount at `/opt/utpl/etc/transcoder.lic`; supervisor passes it via
`-l` ([supervisord.conf](../../docker/utpl-transcoder/supervisord.conf)). For an online
license also set `application.licenseServer.uri` in `config.json`.

Rotate keys: regenerate, **rebuild every image** (keys are embedded at link time), re-issue
all licenses.

---

## 9. Suggested remediation order

1. **§7.1** — treat the prod private key as compromised: rotate, rebuild, re-issue, scrub
   history, add `transcoder/license/**/keys/*private*` to `.gitignore`.
2. **§7.2** — set `ret` in the license catch block. One line; unblocks all downstream
   failure detection.
3. **§7.8 + §7.2** — wire the channel UI's `Licence` tab to `/license`, and surface the
   reason for a stop once §7.2 makes it distinguishable.
4. **§7.3, §7.4** — normalise the timestamp units to milliseconds throughout
   `_processTokenResponse`, and move the HTTP calls off the main loop (cpr async, or a
   worker thread posting results back via `g_idle_add`).
5. **§7.14** — add tests for signature rejection, `offline`/`max_sessions` round-trip,
   expiry boundaries, and `.skn` embed/extract tamper detection.
6. **§7.7, §7.9, §7.10, §7.11, §7.12, §7.13** — small, independent, low-risk cleanups.
7. **§7.5, §7.6** — design decisions, not bugs. Decide explicitly whether offline
   licensing should carry a monotonic high-water mark, and whether online mode should keep
   honouring local expiry.
