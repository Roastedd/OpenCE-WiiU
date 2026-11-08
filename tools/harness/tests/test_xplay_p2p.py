"""Internet play's bytes on both byte orders (tools/harness/xplay.py, tests/xplay_p2p.c): the primitives that seal the tunnel's
packets and sign the listings (SHA-256, HMAC, ChaCha20-Poly1305, X25519 and Ed25519 against RFC 7748 and 8032), KCP's segments
(little-endian, whatever the machine), a public game's listing as the host makes, signs, reads and verifies it, the host-signed
listings of the master protocol's test vectors (made by the master's own implementation) and the master's signed list (the
HTTPS list a browser takes). Both byte orders must give the committed bytes (tests/xplay_golden/p2p.txt).

    tools/ppc-be/run.sh harness tools/harness/tests/test_xplay_p2p.py
"""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import xplay  # noqa: E402

LEGS = xplay.legs()
ORDER = sorted(LEGS, key=lambda name: name != "le")
UNITS = ("source/cseries/cseries.c", "port/linux/src/p2p_crypto.c", "port/third_party/monocypher/monocypher.c",
         "port/third_party/monocypher/monocypher-ed25519.c", "port/third_party/kcp/ikcp.c", "port/linux/src/server_json.c",
         "port/linux/src/master_list.c")

pytestmark = pytest.mark.skipif(not LEGS, reason="needs 32-bit x86 (-m32 or qemu-i386) or PowerPC (qemu-ppc) and clang")

NEGATIVE_CONTROLS = {
    "kcp-conversation-in-machine-order": ("port/third_party/kcp/ikcp.c",
                                          "static inline char *ikcp_encode32u(char *p, IUINT32 l)\n{\n#if IWORDS_BIG_ENDIAN || IWORDS_MUST_ALIGN",
                                          "static inline char *ikcp_encode32u(char *p, IUINT32 l)\n{\n#if 0"),
    "chacha-key-in-machine-order": ("port/linux/src/p2p_crypto.c",
                                    "\treturn (unsigned int)bytes[0] | (unsigned int)bytes[1] << 8 | (unsigned int)bytes[2] << 16 |\n\t\t(unsigned int)bytes[3] << 24;",
                                    "\treturn (unsigned int)bytes[3] | (unsigned int)bytes[2] << 8 | (unsigned int)bytes[1] << 16 |\n\t\t(unsigned int)bytes[0] << 24;"),
    "listing-sequence-little-endian": ("port/linux/src/p2p_lobby.c",
                                       "\tbytes[0] = (unsigned char)(value >> 24);\n\tbytes[1] = (unsigned char)(value >> 16);\n\tbytes[2] = (unsigned char)(value >> 8);\n\tbytes[3] = (unsigned char)value;\n}\n\nstatic unsigned long get_long",
                                       "\tbytes[3] = (unsigned char)(value >> 24);\n\tbytes[2] = (unsigned char)(value >> 16);\n\tbytes[1] = (unsigned char)(value >> 8);\n\tbytes[0] = (unsigned char)value;\n}\n\nstatic unsigned long get_long"),
}


def build(leg, mutations=()):
    return xplay.build("xplay_p2p", leg, UNITS, (f"-I{xplay.vectors_include()}",), mutations, platform=True)


@pytest.mark.parametrize("leg", ORDER)
def test_p2p_bytes(leg):
    status, output, errors = xplay.run(build(leg), leg)
    assert status == 0, errors
    xplay.check_golden("p2p", output, leg)


@pytest.mark.skipif("be" not in LEGS, reason="needs the big-endian leg")
@pytest.mark.parametrize("name", sorted(NEGATIVE_CONTROLS))
def test_negative_control(name):
    path, before, after = NEGATIVE_CONTROLS[name]
    status, output, errors = xplay.run(build("be", ((path, before, after),)), "be")
    golden = xplay.golden_path("p2p").read_text()
    assert status != 0 or output != golden, f"{name} was not caught"
