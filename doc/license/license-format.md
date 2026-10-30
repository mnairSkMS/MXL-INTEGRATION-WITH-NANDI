# License File Format

License files are encoded, signed, and encrypted to ensure integrity and confidentiality. This page
describes the on-disk format, the field reference, and how verification keys are embedded into the
transcoder binary.

## File structure

```
-----BEGIN TRANSCODER LICENSE-----
<base64_encoded_json>
-----END TRANSCODER LICENSE-----
```

The Base64 payload between the markers is split into lines of at most 80 characters. Example:

```
-----BEGIN TRANSCODER LICENSE-----
eyJlbmMiOiJaemhEVEdVclZIVlVSbkZrZEdZNVNUSlVVamsxVjJRdmRsbzBXWEJq
VmxOQ2QxRnhMelIyYTI1SlozWmhZa05wZEVVM2FVeHVaamhEUjBnMmMyNDNjVFJL
T1dabE9HbHRha2RGT1hBMGJuRTVNVFpUTVN0eFNUazNTRlUyUkV0cmRGbHFZak5G
TDBOelNtbFRRVUZRVjFsMU9ERkZVMjVPU2tkSFp6aHBNWHBUVmt0cVIycFpjRzVI
UVU4NVdHaHRUSFJRUjFweFJFVlpOVU5UYWxBMVp5OUxZMUJJT0hSaEwzSmlXRUZP
VlUxV09HbHllVVJaYkdsQmNIUkdORGhqU1VSVGJVcHZla2c1WlZsUFVVODJSVFow
T1RsRVdFeHdTWGxCZVhGTFRHMHVRV3dyUlZobldIQklkemxwT0ZsbU5RPT0iLCJz
aWciOiJMVWdXcUlPMm9WQzA0ZjB6SlRDOURjYW1ucW1qM0lqd3R3MGFoYzl5Sy9j
Qm1hMStybUV6cVFQUDh2TFRuRGRYenJiMDB5NmNEMDE1ektHdzQ0NCtCUT09In0=
-----END TRANSCODER LICENSE-----
```

## Envelope

Decoding the Base64 payload yields the envelope JSON:

```json
{
  "enc": "<base64_encoded_encrypted_data>",
  "sig": "<base64_encoded_signature>"
}
```

| Field | Meaning |
| --- | --- |
| `enc` | Base64-encoded, encrypted license data. |
| `sig` | Base64-encoded ED25519 signature over the license data, used to verify integrity and authenticity. |

Example:

```json
{
  "enc": "ZzhDTGUrVHVURnFkdGY5STJUUjk1V2Qvdlo0WXBjVlNCd1FxLzR2a25JZ3ZhYkNpdEU3aUxuZjhDR0g2c243cTRKOWZlOGltakdFOXA0bnE5MTZTMStxSTk3SFU2REtrdFlqYjNFL0NzSmlTQUFQV1l1ODFFU25OSkdHZzhpMXpTVktqR2pZcG5HQU85WGhtTHRQR1pxREVZNUNTalA1Zy9LY1BIOHRhL3JiWEFOVU1WOGlyeURZbGlBcHRGNDhjSURTbUpvekg5ZVlPUU82RTZ0OTlEWExwSXlBeXFLTG0uQWwrRVhnWHBIdzlpOFlmNQ==",
  "sig": "LUgWqIO2oVC04f0zJTC9Dcamnqmj3Ijwtw0ahc9yK/cBma1+rmEzqQPP8vLTnDdXzrb00y6cD015zKGw444+BQ=="
}
```

## License data

Decrypting `enc` yields the license itself:

```json
{
  "data": {
    "id": "<license_id>",
    "name": "<license_name>",
    "ttl": <time_to_live_in_milliseconds>,
    "valid_since": <valid_since_timestamp>,
    "max_sessions": <maximal_concurrent_online_sessions>,
    "offline": <offline_capable>
  },
  "meta": {
    "created_at": <creation_timestamp>
  }
}
```

| Field | Meaning |
| --- | --- |
| `data.id` | Unique identifier for the license. |
| `data.name` | Human-readable license name. |
| `data.ttl` | Time-to-live in milliseconds. |
| `data.valid_since` | Timestamp from which the license is valid. |
| `data.max_sessions` | Maximum concurrent online sessions permitted. |
| `data.offline` | Whether the license is offline-capable and needs no online verification. |
| `meta.created_at` | Timestamp when the license was created. |

Example:

```json
{
  "data": {
    "id": "cbce5863-de81-4dd7-b87e-5ad61b4ca446",
    "name": "Transcoder License",
    "ttl": 31536000000,
    "valid_since": 1735689600000
  },
  "meta": {
    "created_at": 1738680704382
  }
}
```

## Key embedding

The public key used to verify the license signature and the AES key used to decrypt the license
payload are embedded into the transcoder binary as an independent ELF section named **`.skn`**,
holding Base64-encoded binary data:

```
 0000 6b576959 7a6d6365 627a364f 50777068  kWiYzmcebz6OPwph
 0010 2e385037 46657951 754f6478 7a2b485a  .8P7FeyQuOdxz+HZ
 0020 4e515835 35494c36 35364d6a 48735865  NQX55IL656MjHsXe
 0030 642f6465 2b6e2f47 78534261 67304253  d/de+n/GxSBag0BS
 0040 65754d38 6d53626e 72353169 722b7343  euM8mSbnr51ir+sC
 0050 61487163 6834626e 636c6851 34434d4f  aHqch4bnclhQ4CMO
 0060 436e4659 7550446c 7432686d 56516270  CnFYuPDlt2hmVQbp
 0070 356f706a 66763936 53706b4b 71507768  5opjfv96SpkKqPwh
 0080 63464c74 6e345a46 2b307046 64337459  cFLtn4ZF+0pFd3tY
 0090 3058644d 59447654 4c7a6473 75677445  0XdMYDvTLzdsugtE
 00a0 386f6356 70664257 2f766649 41657637  8ocVpfBW/vfIAev7
 00b0 5844773d 3d                          XDw==
```

### Section format

```
base64(AES-IV) "." base64(AES-256-GCM(base64(license_key) "." base64(public_key)))
```

where **AES-IV** is an initialization vector chosen randomly during key embedding.

### Section encryption key

The AES key protecting the `.skn` section is derived as the SHA-256 hash of the concatenated
contents of all **non-DWARF** and **non-STRTAB** sections of the transcoder binary:

```
sha256(sha256('.text') sha256('.data') ... sha256('.rodata'))
```

This binds the section to the exact binary, providing protection against binary modification.
AES-GCM additionally authenticates the encrypted data, so tampering is detected rather than silently
accepted.

The `keyEmbedder` tool performs this step; the CMake build runs it after linking the transcoder
executable. When production keys are rotated, the transcoder images must be rebuilt so the updated
keys are embedded.
