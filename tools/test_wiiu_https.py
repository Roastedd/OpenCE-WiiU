"""The Wii U's HTTPS GET for the server browser (port/wiiu/src/wiiu_https.c, over wiiu_update_http.c) and its random bytes
(port/wiiu/src/wiiu_entropy.c) on a desktop.

    python3 -m pytest -q tools/test_wiiu_https.py

- The request against local TLS servers with a test CA (the updater's: tools/test_wiiu_update.py): the status, the headers (Signature and
  Signature-Key among them, in any case, kept as update.h's update_response keeps them: "Name: value" and a newline, a line that does not fit
  left out), the body NUL terminated whatever it holds, under a limit, with Content-Length or chunked or to the close; an answer of any status is an
  answer, a redirect is not followed, a 204 and a 304 have no body; extra request headers sent as given and checked; no plain HTTP, no other method,
  no request at all for a certificate that is refused or when the network may not be used; a stall stopped within a quarter of a second; a request
  that goes on past its time is ended; several threads asking at once make one connection at a time.
- With OPENCE_MASTER_TEST=1 the master server itself, with the console's own roots (wiiu_update_roots.h) as the only trust: its list, its Signature and
  Signature-Key headers. OPENCE_MASTER_CAPTURE=<folder> keeps them for the harness (tools/harness/tests/test_master_protocol.py), which checks
  them with master_list.c's own code and its pinned key.
- The random bytes: the pool against a model written from its description (hashlib's BLAKE2b), and the gathering and the seed file with the test's
  clocks: the seed file made, read, replaced when short, left alone when the card may not be used, and never the output; clocks that do not move; the
  bits and bytes of 64 KiB; eight threads asking at once.
"""

import hashlib
import http.server
import os
import re
import socket
import ssl
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_wiiu_update as base  # noqa: E402

ROOT = base.ROOT
SRC = base.SRC
TESTS = base.TESTS
MBEDTLS = base.MBEDTLS
MONOCYPHER = base.MONOCYPHER
CC = base.CC
OPENSSL = base.OPENSSL
run = base.run
WARNINGS = ["-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror"]


# ---------- the random bytes

def le64(value):
    return struct.pack("<Q", value)


class Model:
    """The pool as wiiu_entropy.h describes it, with hashlib"""

    def __init__(self):
        self.state = hashlib.blake2b(b"OpenCE Wii U entropy pool 1", digest_size=64).digest()
        self.counter = 0

    def mix(self, data):
        self.state = hashlib.blake2b(b"mix" + self.state + le64(self.counter) + le64(len(data)) + data, digest_size=64).digest()
        self.counter += 1

    def ratchet(self):
        self.state = hashlib.blake2b(b"ratchet" + le64(self.counter), key=self.state, digest_size=64).digest()
        self.counter += 1

    def extract(self, size):
        out = b""
        for block in range((size + 31) // 32):
            out += hashlib.blake2b(b"out" + le64(self.counter) + struct.pack("<I", block), key=self.state, digest_size=32).digest()
        self.ratchet()
        return out[:size]

    def seed(self):
        seed = hashlib.blake2b(b"seed" + le64(self.counter), key=self.state, digest_size=64).digest()
        self.ratchet()
        return seed


def fields(text):
    return dict(re.findall(r"(\w+)=(\S*)", text))


class EntropyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = Path(tempfile.mkdtemp(prefix="opence-entropy-"))
        cls.driver = cls.folder / "test_entropy"
        result = run([CC, *WARNINGS, f"-I{SRC}", f"-I{MONOCYPHER}", str(TESTS / "test_entropy.c"), str(SRC / "wiiu_entropy.c"),
                      str(MONOCYPHER / "monocypher.c"), "-lpthread", "-o", str(cls.driver)])
        if result.returncode:
            raise RuntimeError(result.stderr)

    @classmethod
    def tearDownClass(cls):
        import shutil
        shutil.rmtree(cls.folder, ignore_errors=True)

    def test_the_pool_is_what_its_description_says(self):
        result = run([str(self.driver), "kat"])
        self.assertEqual(result.returncode, 0, result.stderr)
        got = fields(result.stdout)
        model = Model()
        self.assertEqual(got["init"], model.state.hex())
        model.mix(b"abc")
        self.assertEqual(got["mixed"], model.state.hex())
        self.assertEqual(got["out80"], model.extract(80).hex())
        self.assertEqual(got["after_extract"], model.state.hex())
        model.mix(bytes(range(200)))
        model.mix(b"")
        self.assertEqual(got["after_mix200"], model.state.hex())
        self.assertEqual(got["seed"], model.seed().hex())
        self.assertEqual(got["out7"], model.extract(7).hex())
        self.assertEqual(got["counter"], str(model.counter))

    def test_the_gathering_the_seed_file_and_the_pool_in_use(self):
        folder = self.folder / "global"
        folder.mkdir()
        result = run([str(self.driver), "global", str(folder)])
        self.assertEqual(result.returncode, 0, result.stderr)
        got = fields(result.stdout)
        # 1: the first start: 48 yields with the clock read either side of each, the seed file made whole, and said to be none before
        self.assertEqual(got["start_seed_size"], "64")
        self.assertEqual(got["yields"], "48")
        self.assertEqual(got["ticks"], "97")
        self.assertEqual(got["start_log_no_seed"], "1")
        self.assertEqual(got["seed_in_first"], "0")
        self.assertEqual(got["second_differs"], "1")
        self.assertEqual(got["yields_after_second"], "48", "the clocks are gathered once, not at each call")
        # 2: the next start reads the seed and writes another that is not the first or an output
        for name in ("restart_read_seed", "seed_rewritten", "seed_not_the_output", "third_differs", "seed_changes_output"):
            self.assertEqual(got[name], "1", name)
        # 4: a short file is no seed and is replaced by a whole one
        self.assertEqual((got["short_log_no_seed_file"], got["short_replaced"]), ("1", "64"))
        # 5, 6, 7: the card not to be used, a path that cannot be written, clocks that stand still
        self.assertEqual((got["card_off_untouched"], got["card_off_bytes"]), ("1", "1"))
        self.assertEqual((got["unwritable_logged"], got["unwritable_bytes"]), ("1", "1"))
        self.assertEqual(got["still_clocks_differ"], "1")
        # 8: a save after the start that wrote a seed is not a second write; a later one is
        self.assertEqual((got["save_after_start_skipped"], got["save_later_writes"]), ("1", "1"))
        # 9: 64 KiB of output: bits half ones, bytes spread (255 degrees of freedom: 400 is 12 standard deviations out), no block twice
        self.assertAlmostEqual(float(got["ones_percent"]), 50.0, delta=0.5)
        self.assertLess(float(got["chi_square"]), 400.0)
        self.assertEqual((got["blocks"], got["repeated"]), ("2048", "0"))
        # 10: eight threads asking 200 times each: all 1600 answers different
        self.assertEqual((got["threads_outputs"], got["distinct"]), ("1600", "1600"))

    def test_the_entropy_poll_and_the_random_bytes_use_it(self):
        mbedtls = (SRC / "wiiu_mbedtls.c").read_text()
        self.assertIn("wiiu_entropy_bytes(output, length)", mbedtls)
        self.assertNotIn("state ^= state << 13", mbedtls)
        net = (SRC / "wiiu_posix_net.c").read_text()
        self.assertIn("wiiu_entropy_bytes(buffer, size)", net)
        self.assertNotIn("mbedtls_platform_entropy_poll", net)

    def test_an_existing_identity_key_is_read_and_never_made_again(self):
        # (posix_user_secret in wiiu_posix_net.c: the file is read first, and only a file that is not there or is short is made anew)
        net = (SRC / "wiiu_posix_net.c").read_text()
        body = net[net.index("int posix_user_secret"):]
        body = body[:body.index("\n}\n")]
        self.assertLess(body.index("fread(secret"), body.index("posix_random_bytes(secret"))
        self.assertIn("if (ok)\n\t\t\treturn 1;", body)


# ---------- the request

def body_of(size, seed=1):
    return hashlib.blake2b(le64(seed), digest_size=64).digest() * (size // 64 + 1)


LIST_BODY = (b'{"protocol":1,"master":{"key_id":"0123456789abcdef","time":1700000000},"servers":[' +
             b",".join(b'{"listing":"' + body_of(100, i)[:60].hex().encode() + b'"}' for i in range(20)) + b"]}")


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "test"

    def log_message(self, format, *arguments):
        pass

    def answer(self, status, body=b"", headers=(), length=True):
        self.send_response(status)
        for name, value in headers:
            self.send_header(name, value)
        if length:
            self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        server = self.server
        with server.lock:
            server.active += 1
            server.most_active = max(server.most_active, server.active)
            server.requests.append((self.path, dict(self.headers)))
        try:
            self.route()
        finally:
            with server.lock:
                server.active -= 1

    def route(self):
        path = self.path.split("?")[0]
        query = self.path.split("?")[1] if "?" in self.path else ""
        signed = [("Signature", "ed25519=QUJD"), ("Signature-Key", "0123456789abcdef"), ("Content-Type", "application/json")]
        if path == "/v1/servers":
            self.answer(200, LIST_BODY, signed)
        elif path == "/lowercase":
            self.answer(200, LIST_BODY, [("signature", "ed25519=bG93ZXI="), ("SIGNATURE-KEY", "fedcba9876543210")])
        elif path == "/chunked":
            self.send_response(200)
            for name, value in signed:
                self.send_header(name, value)
            self.send_header("Transfer-Encoding", "chunked")
            self.send_header("Connection", "close")
            self.end_headers()
            for start in range(0, len(LIST_BODY), 333):
                piece = LIST_BODY[start:start + 333]
                self.wfile.write(f"{len(piece):x}\r\n".encode() + piece + b"\r\n")
            self.wfile.write(b"0\r\n\r\n")
        elif path == "/toclose":
            self.answer(200, LIST_BODY, signed, length=False)
        elif path == "/empty":
            self.answer(200, b"", signed)
        elif path == "/binary":
            self.answer(200, b"ab\x00cd\x00", signed)
        elif path == "/moved":
            self.answer(302, b"moved", [("Location", "https://localhost:1/v1/servers"), ("Signature", "ed25519=bW92ZWQ=")])
        elif path == "/missing":
            self.answer(404, b'{"error":"no such thing"}', [("Content-Type", "application/json")])
        elif path == "/error":
            self.answer(503, b"busy", [("Retry-After", "30")])
        elif path == "/nobody":
            self.answer(204, b"", [("X-Seen", "yes")])
        elif path == "/notmodified":
            self.answer(304, b"", [("ETag", '"abc"')])
        elif path == "/sized":
            size = int(query)
            self.answer(200, body_of(size)[:size], signed)
        elif path == "/sized-chunked":
            size = int(query)
            data = body_of(size)[:size]
            self.send_response(200)
            self.send_header("Transfer-Encoding", "chunked")
            self.send_header("Connection", "close")
            self.end_headers()
            for start in range(0, size, 700):
                piece = data[start:start + 700]
                self.wfile.write(f"{len(piece):x}\r\n".encode() + piece + b"\r\n")
            self.wfile.write(b"0\r\n\r\n")
        elif path == "/echo":
            text = "".join(f"{name}: {value}\n" for name, value in self.headers.items())
            self.answer(200, (self.requestline + "\n" + text).encode())
        elif path == "/manyheaders":
            # (the first and the last are short, the ones between are 200 bytes: the 4096 bytes kept have room for the first 17)
            lines = [("Signature", "ed25519=first")] + [(f"X-Pad-{i:02d}", "p" * 200) for i in range(30)] + [("Signature-Key", "lastone")]
            self.answer(200, b"x", lines)
        elif path == "/slow":
            time.sleep(0.15)
            self.answer(200, LIST_BODY, signed)
        elif path == "/stall":
            self.send_response(200)
            self.send_header("Content-Length", "1000")
            self.end_headers()
            self.wfile.flush()
            time.sleep(3)
        elif path == "/trickle":
            self.send_response(200)
            self.send_header("Content-Length", "100")
            self.end_headers()
            for _ in range(100):
                try:
                    self.wfile.write(b"t")
                    self.wfile.flush()
                except OSError:
                    return
                time.sleep(0.2)
        else:
            self.answer(404)


def start_server(certificate, key):
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(str(certificate), str(key))
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    server.socket = context.wrap_socket(server.socket, server_side=True)
    server.requests = []
    server.lock = threading.Lock()
    server.active = 0
    server.most_active = 0
    server.daemon_threads = True
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


@unittest.skipUnless(OPENSSL, "needs openssl")
class RequestTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = Path(tempfile.mkdtemp(prefix="opence-https-"))
        base.make_certificates(cls.folder)
        cls.drivers = {}
        cls.main = start_server(cls.folder / "ecdsa.pem", cls.folder / "ecdsa.key")
        cls.untrusted = start_server(cls.folder / "untrusted.pem", cls.folder / "untrusted.key")
        cls.wrong_name = start_server(cls.folder / "wrongname.pem", cls.folder / "wrongname.key")

    @classmethod
    def tearDownClass(cls):
        import shutil
        for server in (cls.main, cls.untrusted, cls.wrong_name):
            server.shutdown()
        shutil.rmtree(cls.folder, ignore_errors=True)

    @classmethod
    def driver(cls, total_ms=None):
        key = total_ms or 0
        if key not in cls.drivers:
            path = cls.folder / f"test_https_{key}"
            extra = [f"-DWIIU_HTTPS_TOTAL_MS={total_ms}u"] if total_ms else []
            result = run([CC, *WARNINGS, *extra, f"-I{SRC}", f"-I{MBEDTLS / 'include'}", f"-I{MONOCYPHER}", str(TESTS / "test_https.c"),
                          str(SRC / "wiiu_https.c"), str(SRC / "wiiu_update_http.c"), *map(str, base.mbedtls_objects()), "-lpthread",
                          "-o", str(path)])
            if result.returncode:
                raise RuntimeError(result.stderr)
            cls.drivers[key] = path
        return cls.drivers[key]

    def url(self, path, server=None):
        return f"https://localhost:{(server or self.main).server_port}{path}"

    def get(self, path, maximum=1_000_000, server=None, roots=None, extra="", deny_after=None, total_ms=None, url=None):
        body, headers = self.folder / "body.bin", self.folder / "headers.txt"
        body.unlink(missing_ok=True)
        headers.unlink(missing_ok=True)
        arguments = [str(self.driver(total_ms)), "get", url or self.url(path, server), str(maximum), str(body), str(headers),
                     roots or str(self.folder / "ca.der"), extra]
        if deny_after is not None:
            arguments.append(str(deny_after))
        result = run(arguments, timeout=90)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        line = result.stdout.strip().splitlines()[-1]
        out = dict(re.findall(r"(\w+)=(\S*)", line.split(" error=")[0]))
        out["error"] = line.split(" error=", 1)[1] if " error=" in line else ""
        out["body_bytes"] = body.read_bytes() if body.exists() else None
        out["header_text"] = headers.read_text() if headers.exists() else None
        return out

    # the contract of update_https_request (port/linux/src/update.h, posix_update.c)

    def test_a_signed_list_comes_with_its_headers_and_a_terminated_body(self):
        got = self.get("/v1/servers")
        self.assertEqual((got["ok"], got["status"], got["nul"]), ("1", "200", "1"), got)
        self.assertEqual(got["body_bytes"], LIST_BODY)
        self.assertEqual(int(got["body"]), len(LIST_BODY))
        lines = got["header_text"].split("\n")
        self.assertIn("Signature: ed25519=QUJD", lines)
        self.assertIn("Signature-Key: 0123456789abcdef", lines)
        self.assertTrue(got["header_text"].endswith("\n"))
        self.assertTrue(got["header_text"].startswith("Server: test"), got["header_text"])

    def test_the_headers_are_found_by_master_lists_reader_in_any_case(self):
        got = self.get("/lowercase")
        self.assertIn("signature: ed25519=bG93ZXI=\n", got["header_text"])
        self.assertIn("SIGNATURE-KEY: fedcba9876543210\n", got["header_text"])

    def test_a_chunked_body_and_a_body_to_the_close(self):
        for path in ("/chunked", "/toclose"):
            got = self.get(path)
            self.assertEqual((got["ok"], got["status"]), ("1", "200"), (path, got))
            self.assertEqual(got["body_bytes"], LIST_BODY, path)
        self.assertIn("Signature-Key: 0123456789abcdef", self.get("/chunked")["header_text"])

    def test_an_empty_body_and_a_body_with_nuls(self):
        got = self.get("/empty")
        self.assertEqual((got["ok"], got["status"], got["body"], got["nul"]), ("1", "200", "0", "1"), got)
        got = self.get("/binary")
        self.assertEqual((got["body"], got["nul"], got["body_bytes"]), ("6", "1", b"ab\x00cd\x00"), got)

    def test_any_status_is_an_answer_and_a_redirect_is_not_followed(self):
        before = len(self.main.requests)
        got = self.get("/moved")
        self.assertEqual((got["ok"], got["status"], got["body_bytes"]), ("1", "302", b"moved"), got)
        self.assertIn("Location: https://localhost:1/v1/servers\n", got["header_text"])
        self.assertIn("Signature: ed25519=bW92ZWQ=\n", got["header_text"])
        self.assertEqual(len(self.main.requests), before + 1)
        got = self.get("/missing")
        self.assertEqual((got["ok"], got["status"], got["body_bytes"]), ("1", "404", b'{"error":"no such thing"}'), got)
        got = self.get("/error")
        self.assertEqual((got["ok"], got["status"], got["body_bytes"]), ("1", "503", b"busy"), got)
        self.assertIn("Retry-After: 30\n", got["header_text"])

    def test_204_and_304_have_no_body_and_keep_their_headers(self):
        got = self.get("/nobody")
        self.assertEqual((got["ok"], got["status"], got["body"], got["nul"]), ("1", "204", "0", "1"), got)
        self.assertIn("X-Seen: yes\n", got["header_text"])
        got = self.get("/notmodified")
        self.assertEqual((got["ok"], got["status"], got["body"]), ("1", "304", "0"), got)
        self.assertIn('ETag: "abc"\n', got["header_text"])

    def test_the_body_limit_is_exact_for_both_codings(self):
        for path in ("/sized", "/sized-chunked"):
            got = self.get(f"{path}?1000", maximum=1000)
            self.assertEqual((got["ok"], got["body"]), ("1", "1000"), (path, got))
            self.assertEqual(got["body_bytes"], body_of(1000)[:1000])
            got = self.get(f"{path}?1001", maximum=1000)
            self.assertEqual(got["ok"], "0", (path, got))
            self.assertIn("1000", got["error"])
            self.assertEqual(got["header_text"], None, "no headers are handed back with a failure")
        got = self.get("/sized?1048576", maximum=1024 * 1024)
        self.assertEqual((got["ok"], got["body"]), ("1", "1048576"), got)
        got = self.get("/sized?1048577", maximum=1024 * 1024)
        self.assertEqual(got["ok"], "0", got)

    def test_headers_that_do_not_fit_are_left_out_as_the_desktops_are(self):
        got = self.get("/manyheaders")
        self.assertEqual((got["ok"], got["status"]), ("1", "200"), got)
        text = got["header_text"]
        # (posix_update.c: a line is kept when what is kept and the line and two more bytes are under 4096; one that does not fit is left
        # out and the next is tried, so a short line after the long ones is kept)
        self.assertLess(len(text), 4096)
        self.assertIn("Signature: ed25519=first\n", text)
        self.assertIn("X-Pad-17: ", text)
        self.assertNotIn("X-Pad-18: ", text)
        self.assertNotIn("X-Pad-29: ", text)
        self.assertIn("Signature-Key: lastone\n", text)
        self.assertGreaterEqual(len(text) - len("Signature-Key: lastone\n") + len("X-Pad-18: ") + 200 + 2, 4096)
        self.assertTrue(text.endswith("\n"))
        for line in text.splitlines():
            if line.startswith("X-Pad-"):
                self.assertEqual(len(line), len("X-Pad-00: ") + 200, "no line is cut")

    def test_extra_request_headers_are_sent_as_given_and_checked(self):
        got = self.get("/echo", extra="If-None-Match: \"abc\"\\r\\nX-Other: 1\\r\\n")
        self.assertEqual(got["status"], "200", got)
        text = got["body_bytes"].decode()
        self.assertTrue(text.startswith("GET /echo HTTP/1.1\n"), text)
        self.assertIn('If-None-Match: "abc"\n', text)
        self.assertIn("X-Other: 1\n", text)
        self.assertIn("Host: localhost:", text)
        self.assertNotIn("Range", text)
        before = len(self.main.requests)
        for bad in ("X: a\\nInjected: 1\\r\\n", "X: a\\r\\n\\r\\nGET /v1/servers HTTP/1.1\\r\\n", "no terminator", "X: a\\rY: b\\r\\n"):
            got = self.get("/echo", extra=bad)
            self.assertEqual(got["ok"], "0", (bad, got))
            self.assertIn("header lines are not valid", got["error"])
        self.assertEqual(len(self.main.requests), before, "nothing was sent for a bad header")
        got = self.get("/echo", extra="X-Long: " + "a" * 1100 + "\\r\\n")
        self.assertEqual(got["ok"], "0", got)

    def test_only_get_is_made(self):
        before = len(self.main.requests)
        for method in ("PUT", "POST", "DELETE", "HEAD", "get"):
            result = run([str(self.driver()), "method", method, self.url("/v1/servers")])
            line = result.stdout.strip()
            self.assertIn("ok=0", line, line)
            self.assertIn("only GET", line)
        result = run([str(self.driver()), "method", "GET", self.url("/v1/servers")])
        self.assertIn("ok=0", result.stdout, "no roots are set in this mode: the test CA is not trusted, and the console's own roots refuse it")
        self.assertEqual(len(self.main.requests), before)

    def test_plain_http_is_never_made(self):
        before = len(self.main.requests)
        got = self.get("/v1/servers", url=f"http://127.0.0.1:{self.main.server_port}/v1/servers")
        self.assertEqual(got["ok"], "0", got)
        self.assertIn("not an https:// address", got["error"])
        got = self.get("/v1/servers", url="ftp://localhost/")
        self.assertEqual(got["ok"], "0", got)
        self.assertEqual(len(self.main.requests), before)

    def test_a_refused_certificate_sends_nothing_and_says_the_clock(self):
        for server, why in ((self.untrusted, "not correctly signed by the trusted CA"), (self.wrong_name, "")):
            before = len(server.requests)
            got = self.get("/v1/servers", server=server)
            self.assertEqual(got["ok"], "0", got)
            self.assertIn("certificate of localhost was refused", got["error"])
            self.assertIn(why, got["error"])
            self.assertRegex(got["error"], r"\(the clock reads 20\d\d-\d\d-\d\d \d\d:\d\d:\d\d UTC\)")
            self.assertEqual(len(server.requests), before)

    def test_the_consoles_own_roots_do_not_trust_a_test_ca(self):
        before = len(self.main.requests)
        got = self.get("/v1/servers", roots="embedded")
        self.assertEqual(got["ok"], "0", got)
        self.assertIn("certificate", got["error"])
        self.assertEqual(len(self.main.requests), before)

    def test_nothing_is_asked_when_the_network_may_not_be_used(self):
        before = len(self.main.requests)
        got = self.get("/v1/servers", deny_after=0)
        self.assertEqual(got["ok"], "0", got)
        self.assertIn("may not be used", got["error"])
        self.assertEqual(len(self.main.requests), before)

    def test_a_stall_is_stopped_within_a_quarter_of_a_second_of_the_network_going(self):
        got = self.get("/stall", deny_after=500)
        self.assertEqual(got["ok"], "0", got)
        self.assertIn("stopped", got["error"])
        # (the 500 ms to the deny, the handshake before it, and not the server's three seconds)
        self.assertLess(int(got["ms"]), 500 + 250 + 200, got)

    def test_a_request_that_goes_on_too_long_is_ended(self):
        started = time.time()
        got = self.get("/trickle", total_ms=1500)
        self.assertEqual(got["ok"], "0", got)
        self.assertIn("took too long", got["error"])
        self.assertLess(time.time() - started, 6)

    def test_threads_asking_together_make_one_connection_at_a_time(self):
        # (a server of its own: the stall and the trickle of other tests are still being served by the main one)
        server = start_server(self.folder / "ecdsa.pem", self.folder / "ecdsa.key")
        try:
            result = run([str(self.driver()), "parallel", self.url("/slow", server), "5", str(self.folder / "ca.der")], timeout=120)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("ok=15 bad=0", result.stdout)
            self.assertEqual(len(server.requests), 15)
            self.assertEqual(server.most_active, 1)
        finally:
            server.shutdown()

    def test_the_master_servers_certificates_roots_are_the_consoles(self):
        # (wiiu_https.c uses wiiu_update_roots.h whole: the roots the generator names, ISRG X1 and X2 first)
        text = (SRC / "wiiu_https.c").read_text()
        self.assertIn("wiiu_update_roots, wiiu_update_root_sizes, WIIU_UPDATE_ROOT_COUNT", text)
        header = (SRC / "wiiu_update_roots.h").read_text()
        self.assertIn("69729b8e15a86efc177a57afb7171dfc64add28c2fca8cf1507e34453ccb1470", header)

    @unittest.skipUnless(os.environ.get("OPENCE_MASTER_TEST"), "OPENCE_MASTER_TEST=1: the master server itself (a public GET)")
    def test_the_master_server_with_the_consoles_own_roots(self):
        got = self.get("/v1/servers", url="https://master.opencommunityedition.com/v1/servers", roots="embedded")
        self.assertEqual((got["ok"], got["status"]), ("1", "200"), got)
        headers = got["header_text"]
        signature = re.search(r"^Signature: (ed25519=\S+)$", headers, re.M | re.I)
        key = re.search(r"^Signature-Key: (\S+)$", headers, re.M | re.I)
        self.assertTrue(signature and key, headers)
        capture = os.environ.get("OPENCE_MASTER_CAPTURE")
        if capture:
            folder = Path(capture)
            folder.mkdir(parents=True, exist_ok=True)
            (folder / "body").write_bytes(got["body_bytes"])
            (folder / "signature").write_text(signature.group(1))
            (folder / "key").write_text(key.group(1))
            (folder / "now").write_text(str(int(time.time())))
        print(f"\nmaster: {got['body']} bytes in {got['ms']} ms, {signature.group(1)[:24]}..., key {key.group(1)}")


if __name__ == "__main__":
    unittest.main()
