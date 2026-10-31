#!/usr/bin/env python3
"""Writes port/wiiu/src/wiiu_update_roots.h: the certificate authorities the Wii U's updater trusts (wiiu_update.c), as DER, taken
from a CA bundle by their SHA-256 fingerprints.

    python3 tools/wiiu/update_roots.py [BUNDLE.pem]

The console's own roots (NSSL's) do not reach GitHub's file host, which chains to ISRG Root X1, nor github.com, which chains to
Sectigo's E46 root by way of USERTrust; so the updater brings its own, as the homebrew app stores do. The roots are those GitHub's
hosts chain to (checked with `openssl s_client -showcerts` on 2026-10-31), their siblings of the other key type, and DigiCert
Global Root G2, which GitHub's hosts have used before, as a spare. The bundle is the system's (Mozilla's list, as Homebrew's
ca-certificates and Debian's ship it); the fingerprints below were read from both, which agree, and a certificate whose
fingerprint is not one of them is never written. tools/test_wiiu_update.py checks the header against the fingerprints.
"""

import base64
import hashlib
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "port/wiiu/src/wiiu_update_roots.h"
BUNDLES = ["/etc/ssl/certs/ca-certificates.crt", "/opt/homebrew/etc/ca-certificates/cert.pem", "/etc/ssl/cert.pem"]

# (name, SHA-256 of the DER, valid until, what it is for)
ROOTS = [
    ("ISRG Root X1", "96bcec06264976f37460779acf28c5a7cfe8a3c0aae11a8ffcee05c0bddf08c6", "2035-06-04",
     "release-assets.githubusercontent.com, objects.githubusercontent.com (Let's Encrypt YR1 by ISRG Root YR)"),
    ("ISRG Root X2", "69729b8e15a86efc177a57afb7171dfc64add28c2fca8cf1507e34453ccb1470", "2040-09-17",
     "Let's Encrypt's ECDSA chains"),
    ("USERTrust ECC Certification Authority", "4ff460d54b9c86dabfbcfc5712e0400d2bed3fbc4d4fbdaa86e06adcd2a9ad7a", "2038-01-18",
     "github.com, api.github.com (Sectigo DV E36 by Sectigo Root E46, cross-signed)"),
    ("USERTrust RSA Certification Authority", "e793c9b02fd8aa13e21c31228accb08119643b749c898964b1746d46c3d4cbd2", "2038-01-18",
     "Sectigo's RSA chains"),
    ("Sectigo Public Server Authentication Root E46", "c90f26f0fb1b4018b22227519b5ca2b53e2ca5b3be5cf18efe1bef47380c5383", "2046-03-21",
     "github.com, api.github.com, once the cross-signature is gone"),
    ("Sectigo Public Server Authentication Root R46", "7bb647a62aeeac88bf257aa522d01ffea395e0ab45c73f93f65654ec38f25a06", "2046-03-21",
     "Sectigo's RSA chains, once the cross-signature is gone"),
    ("DigiCert Global Root G2", "cb3ccbb76031e5e0138f8dd39a23f9de47ffc35e43c1144cea27d46a5ab1cb5f", "2038-01-15",
     "a spare: GitHub's hosts have chained to DigiCert before"),
]


def certificates(text: str):
    for block in re.findall(r"-----BEGIN CERTIFICATE-----(.*?)-----END CERTIFICATE-----", text, re.S):
        yield base64.b64decode("".join(block.split()))


def header(found: dict) -> str:
    lines = [
        "/*",
        "WIIU_UPDATE_ROOTS.H (written by tools/wiiu/update_roots.py from the system's CA bundle: do not edit)",
        "",
        "The certificate authorities the Wii U's updater trusts (wiiu_update.c), as DER. A server's certificate must chain to one of",
        "them; the console's own roots are not used. Each is chosen by its SHA-256 fingerprint:",
        "",
    ]
    for name, fingerprint, until, purpose in ROOTS:
        lines.append(f"  {name}, until {until}: {purpose}")
        lines.append(f"    SHA-256 {fingerprint}")
    lines += ["*/", "", "#ifndef HALO_WIIU_UPDATE_ROOTS_H", "#define HALO_WIIU_UPDATE_ROOTS_H", "", "#include <stddef.h>", ""]
    for index, (name, fingerprint, _, _) in enumerate(ROOTS):
        der = found[fingerprint]
        lines.append(f"/* {name} */")
        lines.append(f"static const unsigned char wiiu_update_root_{index}[{len(der)}] =")
        lines.append("{")
        for start in range(0, len(der), 16):
            lines.append("\t" + " ".join(f"0x{byte:02x}," for byte in der[start:start + 16]))
        lines.append("};")
        lines.append("")
    lines.append("static const unsigned char *const wiiu_update_roots[] =")
    lines.append("{")
    lines += [f"\twiiu_update_root_{index}," for index in range(len(ROOTS))]
    lines.append("};")
    lines.append("")
    lines.append("static const size_t wiiu_update_root_sizes[] =")
    lines.append("{")
    lines += [f"\tsizeof(wiiu_update_root_{index})," for index in range(len(ROOTS))]
    lines.append("};")
    lines.append("")
    lines.append(f"#define WIIU_UPDATE_ROOT_COUNT {len(ROOTS)}u")
    lines.append("")
    lines.append("#endif")
    return "\n".join(lines) + "\n"


def main() -> int:
    bundle = Path(sys.argv[1]) if len(sys.argv) > 1 else next((Path(p) for p in BUNDLES if Path(p).is_file()), None)
    if not bundle:
        print("no CA bundle found: name one", file=sys.stderr)
        return 1
    wanted = {fingerprint for _, fingerprint, _, _ in ROOTS}
    found = {}
    for der in certificates(bundle.read_text()):
        fingerprint = hashlib.sha256(der).hexdigest()
        if fingerprint in wanted:
            found[fingerprint] = der
    missing = [name for name, fingerprint, _, _ in ROOTS if fingerprint not in found]
    if missing:
        print(f"{bundle} lacks: {', '.join(missing)}", file=sys.stderr)
        return 1
    HEADER.write_text(header(found))
    print(f"{HEADER.relative_to(ROOT)}: {len(ROOTS)} roots from {bundle}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
