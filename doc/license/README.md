# UTPL/Transcoder Software Licensing System

The UTPL/Transcoder licensing system implements offline licenses using strong symmetric and
asymmetric cryptography. License files are encrypted and encoded in a secure format so they can be
transmitted safely by email.

| Document | Contents |
| --- | --- |
| This page | License types, system components, cryptography, and key storage. |
| [license-format.md](license-format.md) | License file format, field reference, and how keys are embedded in the binary. |
| [license-tools.md](license-tools.md) | The `utpl/license-toolkit` container and the four command-line tools. |

## Supported license types

- **Timed license** – a license with a fixed duration and an expiration date. Timed licenses support
  time-based models such as a **subscription license**, which grants access for a specific
  subscription period.

Licenses are additionally either **offline-capable** or **online**. An online license requires a
reachable license server and permits a bounded number of concurrent sessions (`max_sessions`); an
offline license does not contact any service.

## System components

- **Embedded cryptographic keys** – the public key and the encryption key are placed into the
  transcoder binary as a separate ELF section. See [license-format.md](license-format.md#key-embedding).
- **License management tools** – generate a key set, generate a license, sign a license, and display
  license details. See [license-tools.md](license-tools.md).

## Cryptographic algorithms

- **AES-256-GCM** – AES in GCM mode at the maximum key length. GCM adds authentication to the
  encrypted data, preventing unauthorized modification.
- **ED25519** – fast, compact, and currently considered secure, at a security level equivalent to
  4096-bit RSA. Widely used in software applications.

## Security boundaries

Offline license security relies on the secrecy of the encryption material, license file
authentication, and code obfuscation. Without an external security factor such as a hardware key,
smart card, or verification server, the strength of the solution is inherently limited. Treat this as
a deterrent against casual copying, not as a defence against a determined attacker with the binary.

## Key storage

The **license encryption key** and the **public verification key** are stored in the repository. They
are not secret in security terms. The build selects development or production keys by CMake build
type: **Debug** uses development keys, **Release** uses production keys.

```
transcoder/license/
|-- dev
|   `-- keys
|       |-- license_ed25519_private_key.der
|       |-- license_ed25519_public_key.der
|       `-- license_encryption_key.bin
|-- prod
|   `-- keys
|       |-- license_ed25519_public_key.der
|       `-- license_encryption_key.bin
```

### Private key protection

The **production private key must never be stored in the repository.** Its secrecy is critical: an
unauthorized party holding it can generate valid licenses without the product owner's consent.

Development keys are **not** embedded in the Release build, so a Release transcoder will not accept a
license signed with a development key.

**Do not** place key bytes, license contents, tokens, credentials, or cloud secrets into
documentation, logs, test fixtures, or issue reports.
