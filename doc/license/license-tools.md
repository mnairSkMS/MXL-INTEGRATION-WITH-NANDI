# License Tools

Four command-line tools create, sign, and inspect license files, plus a script that generates a key
set. They are packaged in the `utpl/license-toolkit` Docker image for portability.

| Tool | Purpose |
| --- | --- |
| `keyGenerator.sh` | Generate a complete key set for the licensing system. |
| `licenseGenerator` | Create an unsigned license request (`.lsr`). |
| `licenseSigner` | Sign a license request, producing a license (`.lic`). |
| `licenseInfo` | Decode and verify a signed license. |

## Prerequisites

Pull the toolkit image:

```
ghcr.io/skandha-media-services/utpl/license-toolkit
```

## Starting the toolkit container

```sh
# Prepare the shared directory structure
mkdir -p shared/license
mkdir -p shared/keys

# Copy production keys to shared directory
cp [...]/license_* shared/keys/

# Create and start a one-shot container
docker run \
    --rm -it -u $(id -u) \
    -v$(pwd)/shared:/shared \
    ghcr.io/skandha-media-services/utpl/license-toolkit \
    bash
```

**All commands below run inside the toolkit container shell.**

## Example workflow

```sh
# 1. Generate a license request
licenseGenerator \
    -l /shared/keys/license_encryption_key.bin \
    -s 2025-02-06 \
    -t 30 \
    -n "Test license" \
    -o /tmp/transcoder.lsr

# 2. Sign it
licenseSigner \
    -p /shared/keys/license_ed25519_private_key.der \
    -i /tmp/transcoder.lsr \
    -o /shared/license/transcoder.lic

# 3. View and verify it
licenseInfo \
    -l /shared/keys/license_encryption_key.bin \
    -p /shared/keys/license_ed25519_public_key.der \
    -i /shared/license/transcoder.lic
```

## `keyGenerator.sh` — generate a key set

```
Usage: keyGenerator.sh [-o output_dir] [-h]
  -o - path to output directory
  -h - shows this help
```

Without `-o`, keys are written to the current directory. Three files are produced:

- `license_ed25519_private_key.der`
- `license_ed25519_public_key.der`
- `license_encryption_key.bin`

The ED25519 keys use [DER format](http://wiki.openssl.org/index.php/DER); the AES key is raw binary.

**When production keys are rotated, the transcoder Docker images must be rebuilt** so the updated
public and encryption keys are embedded into the binary.

## `licenseGenerator` — create a license

```sh
licenseGenerator -l <license_key> -s <start_date> -t <ttl_days> -T <ttl_seconds> \
                 -i <license_id> -n <license_name> -o <output_path>
```

| Option | Meaning |
| --- | --- |
| `-l`, `--license-key` | Path to the license encryption key file. May also be supplied as a HEX string in the `LICENSE_ENCRYPTION_KEY` environment variable. |
| `-s`, `--start` | License issue date in **ISO-8601** format, e.g. `2025-01-01` or `2025-02-05T14:50:00`. |
| `-t`, `--ttl-days` | Expiry in days. Cannot be combined with `-T`. |
| `-T`, `--ttl-seconds` | Expiry in seconds. Cannot be combined with `-t`. |
| `-i`, `--id` | License ID. Optional; defaults to an auto-generated GUID. |
| `-n`, `--name` | License name. Optional; defaults to `License <ID>`. |
| `-o`, `--output` | Output path. Optional; defaults to `transcoder.lsr`. |
| `-c`, `--max-sessions` | Maximum concurrent sessions for online licenses. Default `1`. |
| `-O`, `--offline` | Enable offline use of the license. Default `false`. |

```sh
licenseGenerator \
    -O false \
    -c 4 \
    -l license_encryption_key.bin \
    -s 2025-01-01 \
    -t 365 \
    -i 12345 \
    -n "Transcoder License" \
    -o transcoder.lsr
```

## `licenseSigner` — sign a license

```sh
licenseSigner -p <private_key> -i <input_license_file> -o <output_signed_license_file>
```

| Option | Meaning |
| --- | --- |
| `-p`, `--private-key` | Path to the private key file. May also be supplied as a HEX string in the `LICENSE_PRIVATE_KEY` environment variable. |
| `-i`, `--input-file` | Input license file path. |
| `-o`, `--output-file` | Output path. Default `transcoder.lic`. |

```sh
licenseSigner \
    -p license_ed25519_private_key.der \
    -i transcoder.lsr \
    -o transcoder.lic
```

## `licenseInfo` — inspect a license

```sh
licenseInfo -l <license_key> -p <public_key> -i <input_signed_license_file>
```

| Option | Meaning |
| --- | --- |
| `-l`, `--license-key` | Path to the license encryption key file. |
| `-p`, `--public-key` | Path to the public key file. |
| `-i`, `--input-file` | Input signed license file path. |

```sh
licenseInfo -l license_encryption_key.bin -p license_ed25519_public_key.der -i transcoder.lic
```

The output shows the license details and the signature status:

```
Id:                 942b5e7a-e9cd-4402-8fb3-7070b410e11a
Name:               My License
Start date:         2025-01-22 00:00:00
Expiry date:        2025-04-22 00:00:00
Creation date:      2025-02-04 13:21:33
Max sessions:       1
Offline:            true
Signature:          Valid
```
