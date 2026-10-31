"""The Wii U self-updater (port/wiiu/src/wiiu_update*.c) on a desktop.

    python3 -m pytest -q tools/test_wiiu_update.py

- The decisions (wiiu_update_core.c): the manifest's checks, the Ed25519 signature (Monocypher, and one made by `openssl pkeyutl
  -sign -rawin`), the journal, and a swap and a trial with the power cut at every step, then at every step of the start after it
  (port/wiiu/tests/test_update.c). Natively with AddressSanitizer and UndefinedBehaviorSanitizer; big-endian too with
  UPDATE_TARGET='--target=powerpc-linux-gnu -fuse-ld=lld -no-pie' UPDATE_RUNNER='qemu-ppc -L /usr/powerpc-linux-gnu' (the
  opence-wiiu container).
- The HTTPS client (wiiu_update_http.c, with the vendored Mbed TLS) against local servers with a test CA: openssl s_server, and
  Python's for redirects to another host, ranges, a resumed download after a pause (HOME), a server that ignores the range, a
  damaged download, a bad certificate and a wrong name (nothing sent), a body cut short, a stall stopped within 250 ms, redirect
  loops and oversized headers.
- With OPENCE_GITHUB_TEST=1, a few read-only requests to GitHub with the console's own roots (wiiu_update_roots.h): the API's
  latest release of OpenCommunityEdition/OpenCE (one of the 60 an hour), and a release asset's redirect to its file host.
- The roots' header against its fingerprints, and the maps' format against the converter and the memory window.
"""

import hashlib
import http.server
import os
import random
import re
import shlex
import shutil
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "port/wiiu/src"
TESTS = ROOT / "port/wiiu/tests"
MBEDTLS = ROOT / "port/third_party/mbedtls"
MONOCYPHER = ROOT / "port/third_party/monocypher"
CC = os.environ.get("CC", "cc")
OPENSSL = shutil.which("openssl")


def run(command, **options):
    options.setdefault("text", True)
    return subprocess.run(command, capture_output=True, timeout=options.pop("timeout", 300), **options)


def mbedtls_objects() -> list:
    """the vendored Mbed TLS compiled for this machine, once (kept in the temporary folder by its sources' and compiler's hash)"""
    sources = sorted((MBEDTLS / "library").glob("*.c"))
    key = hashlib.sha256()
    key.update(run([CC, "--version"]).stdout.encode())
    for source in sources:
        key.update(source.name.encode())
        key.update(source.read_bytes())
    folder = Path(tempfile.gettempdir()) / f"opence-mbedtls-{key.hexdigest()[:16]}"
    folder.mkdir(exist_ok=True)

    def compile_one(source):
        obj = folder / (source.stem + ".o")
        if not obj.exists():
            partial = folder / (source.stem + f".{os.getpid()}.o")
            result = run([CC, "-std=gnu11", "-O2", "-w", f"-I{MBEDTLS / 'include'}", f"-I{MBEDTLS / 'library'}", "-c", str(source), "-o",
                          str(partial)])
            if result.returncode:
                raise RuntimeError(result.stderr)
            partial.replace(obj)
        return obj

    with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        return list(pool.map(compile_one, sources))


class CoreTests(unittest.TestCase):
    def build(self, directory, target="", sanitizer=False):
        # (64-bit file offsets: a 32-bit program's readdir otherwise fails on a filesystem with 64-bit inodes, as under qemu-ppc)
        command = [CC, *shlex.split(target), "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-D_FILE_OFFSET_BITS=64", f"-I{SRC}",
                   f"-I{MONOCYPHER}"]
        if sanitizer:
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-sanitize-recover=all"]
        executable = Path(directory) / "test_update"
        command += [str(TESTS / "test_update.c"), str(SRC / "wiiu_update_core.c"), str(MONOCYPHER / "monocypher.c"),
                    str(MONOCYPHER / "monocypher-ed25519.c"), "-o", str(executable)]
        result = run(command)
        self.assertEqual(result.returncode, 0, result.stderr)
        return executable

    @unittest.skipIf(bool(os.environ.get("UPDATE_TARGET")), "the sanitizers are the native host's (the container's clang has no runtime for them)")
    def test_core_natively_with_sanitizers(self):
        with tempfile.TemporaryDirectory(prefix="opence-update-") as directory:
            result = run([str(self.build(directory, sanitizer=True))])
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("update core tests passed", result.stdout)
            # (every step of the swap and of the restore was cut, and the start after each)
            self.assertRegex(result.stdout, r"commit cut at each of [7-9] steps")
            self.assertIn("needing the recovery by hand", result.stdout)
            self.assertRegex(result.stdout, r"restore cut at each of 12 steps, and its start at each of 12: \d+ cases")

    @unittest.skipUnless(os.environ.get("UPDATE_TARGET"), "the big-endian run needs UPDATE_TARGET and UPDATE_RUNNER (the opence-wiiu container)")
    def test_core_big_endian(self):
        with tempfile.TemporaryDirectory(prefix="opence-update-be-") as directory:
            executable = self.build(directory, target=os.environ["UPDATE_TARGET"])
            result = run([*shlex.split(os.environ.get("UPDATE_RUNNER", "")), str(executable)], timeout=600)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("update core tests passed", result.stdout)

    @unittest.skipUnless(OPENSSL, "needs openssl")
    def test_an_openssl_signature_is_checked_as_the_console_checks_it(self):
        with tempfile.TemporaryDirectory(prefix="opence-update-sig-") as directory:
            folder = Path(directory)
            executable = self.build(directory)
            key = folder / "key.pem"
            self.assertEqual(run([OPENSSL, "genpkey", "-algorithm", "ed25519", "-out", str(key)]).returncode, 0)
            public = run([OPENSSL, "pkey", "-in", str(key), "-pubout", "-outform", "DER"], text=False)
            hexkey = public.stdout[-32:].hex()
            manifest = folder / "halo-wiiu-manifest.txt"
            manifest.write_text("halo-wiiu-manifest 1\nbuild 124\ntag wiiu-124\nmaps c1ea0000\nfile halo.wuhb 5 " + "ab" * 32 + "\n")
            signature = folder / "halo-wiiu-manifest.txt.sig"
            signed = run([OPENSSL, "pkeyutl", "-sign", "-rawin", "-inkey", str(key), "-in", str(manifest), "-out", str(signature)])
            self.assertEqual(signed.returncode, 0, signed.stderr)
            self.assertEqual(signature.stat().st_size, 64)
            self.assertEqual(run([str(executable), "verify", hexkey, str(manifest), str(signature)]).stdout.strip(), "valid")
            self.assertEqual(run([str(executable), "parse", str(manifest)]).stdout.strip(), "ok build 124 files 1")
            manifest.write_text(manifest.read_text().replace("124", "125"))
            self.assertEqual(run([str(executable), "verify", hexkey, str(manifest), str(signature)]).stdout.strip(), "invalid")


# ---------- the HTTPS client against local servers

FILE = random.Random(124).randbytes(200_000)
DAMAGED = FILE[:150_000] + bytes([FILE[150_000] ^ 1]) + FILE[150_001:]


def openssl(*arguments, cwd=None):
    result = run([OPENSSL, *arguments], cwd=cwd)
    if result.returncode:
        raise RuntimeError(" ".join(arguments) + ": " + result.stderr)


def make_certificates(folder: Path):
    """a test CA with three servers' certificates (ECDSA and RSA for localhost, ECDSA for another name), and another CA's for
    localhost"""
    for ca in ("ca", "other-ca"):
        openssl("req", "-x509", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:P-256", "-nodes", "-keyout", str(folder / f"{ca}.key"),
                "-out", str(folder / f"{ca}.pem"), "-days", "2", "-subj", f"/CN=OpenCE {ca}", "-addext", "basicConstraints=critical,CA:TRUE",
                "-addext", "keyUsage=critical,keyCertSign,cRLSign")
    openssl("x509", "-in", str(folder / "ca.pem"), "-outform", "DER", "-out", str(folder / "ca.der"))
    servers = [("ecdsa", "ec", "localhost", "ca"), ("rsa", "rsa", "localhost", "ca"), ("wrongname", "ec", "otherhost.test", "ca"),
               ("untrusted", "ec", "localhost", "other-ca")]
    for name, kind, host, ca in servers:
        key_options = ["-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:P-256"] if kind == "ec" else ["-newkey", "rsa:2048"]
        openssl("req", *key_options, "-nodes", "-keyout", str(folder / f"{name}.key"), "-out", str(folder / f"{name}.csr"), "-subj", f"/CN={host}")
        extensions = folder / f"{name}.ext"
        extensions.write_text(f"subjectAltName=DNS:{host}\nbasicConstraints=CA:FALSE\nkeyUsage=critical,digitalSignature"
                              f"{',keyEncipherment' if kind == 'rsa' else ''}\nextendedKeyUsage=serverAuth\n")
        openssl("x509", "-req", "-in", str(folder / f"{name}.csr"), "-CA", str(folder / f"{ca}.pem"), "-CAkey", str(folder / f"{ca}.key"),
                "-CAcreateserial", "-out", str(folder / f"{name}.pem"), "-days", "2", "-extfile", str(extensions))


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "test"

    def log_message(self, format, *arguments):
        pass

    def answer(self, status, body=b"", headers=()):
        self.send_response(status)
        for name, value in headers:
            self.send_header(name, value)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)

    def ranged(self, data):
        wanted = re.match(r"bytes=(\d+)-$", self.headers.get("Range", ""))
        if not wanted:
            return self.answer(200, data)
        first = int(wanted.group(1))
        if first >= len(data):
            return self.answer(416, b"", [("Content-Range", f"bytes */{len(data)}")])
        self.answer(206, data[first:], [("Content-Range", f"bytes {first}-{len(data) - 1}/{len(data)}")])

    def do_GET(self):
        self.server.requests.append((self.path, self.headers.get("Range")))
        path = self.path.split("?")[0]
        other = f"https://localhost:{self.server.other_port}"
        if path == "/file":
            self.ranged(FILE)
        elif path == "/damaged":
            self.ranged(DAMAGED)
        elif path == "/redirect":
            self.answer(302, b"moved", [("Location", f"{other}/file")])
        elif path == "/dl":
            # (a fresh link each time, as GitHub's to its file host)
            self.answer(302, b"", [("Location", f"{other}/file?signature={len(self.server.requests)}")])
        elif path == "/dl-norange":
            self.answer(302, b"", [("Location", f"{other}/norange")])
        elif path == "/relative":
            self.answer(301, b"", [("Location", "/file")])
        elif path == "/loop":
            self.answer(302, b"", [("Location", "/loop")])
        elif path == "/plain-http":
            self.answer(302, b"", [("Location", f"http://localhost:{self.server.server_port}/file")])
        elif path == "/norange":
            self.answer(200, FILE)
        elif path == "/notfound":
            self.answer(404, b"no such asset")
        elif path == "/wrongrange":
            self.answer(206, FILE[5:], [("Content-Range", f"bytes 5-{len(FILE) - 1}/{len(FILE)}")])
        elif path == "/bigheaders":
            self.answer(200, b"x", [("X-Padding", "p" * 8000)] * 5)
        elif path == "/longline":
            self.answer(200, b"x", [("X-Padding", "p" * 9000)])
        elif path == "/chunked":
            self.send_response(200)
            self.send_header("Transfer-Encoding", "chunked")
            self.send_header("Connection", "close")
            self.end_headers()
            for start in range(0, len(FILE), 7001):
                piece = FILE[start:start + 7001]
                self.wfile.write(f"{len(piece):x};name=value\r\n".encode() + piece + b"\r\n")
            self.wfile.write(b"0\r\nTrailer: here\r\n\r\n")
        elif path == "/truncated":
            self.send_response(200)
            self.send_header("Content-Length", "100000")
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(FILE[:50000])
            self.wfile.flush()
            self.close_connection = True
        elif path == "/stall":
            self.send_response(200)
            self.send_header("Content-Length", "1000")
            self.end_headers()
            self.wfile.flush()
            time.sleep(3)
        else:
            self.answer(404)


def start_server(certificate: Path, key: Path):
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(str(certificate), str(key))
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    server.socket = context.wrap_socket(server.socket, server_side=True)
    server.requests = []
    server.daemon_threads = True
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


def free_port():
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


@unittest.skipUnless(OPENSSL, "needs openssl")
class HttpTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = Path(tempfile.mkdtemp(prefix="opence-update-http-"))
        make_certificates(cls.folder)
        cls.driver = cls.folder / "test_update_http"
        result = run([CC, "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", f"-I{SRC}", f"-I{MBEDTLS / 'include'}",
                      str(TESTS / "test_update_http.c"), str(SRC / "wiiu_update_http.c"), *map(str, mbedtls_objects()), "-o", str(cls.driver)])
        if result.returncode:
            raise RuntimeError(result.stderr)
        cls.main = start_server(cls.folder / "ecdsa.pem", cls.folder / "ecdsa.key")
        cls.other = start_server(cls.folder / "rsa.pem", cls.folder / "rsa.key")
        cls.main.other_port = cls.other.server_port
        cls.other.other_port = cls.main.server_port
        cls.wrong_name = start_server(cls.folder / "wrongname.pem", cls.folder / "wrongname.key")
        cls.untrusted = start_server(cls.folder / "untrusted.pem", cls.folder / "untrusted.key")
        for server in (cls.wrong_name, cls.untrusted):
            server.other_port = cls.main.server_port
        # openssl's own server, serving a file as it is (HTTP/1.0, no length: the body runs to the close)
        (cls.folder / "plain.bin").write_bytes(FILE[:30000])
        cls.s_server_port = free_port()
        cls.s_server = subprocess.Popen([OPENSSL, "s_server", "-accept", str(cls.s_server_port), "-cert", str(cls.folder / "ecdsa.pem"),
                                         "-key", str(cls.folder / "ecdsa.key"), "-WWW", "-quiet"], cwd=cls.folder,
                                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(100):
            try:
                with socket.create_connection(("127.0.0.1", cls.s_server_port), timeout=0.2):
                    break
            except OSError:
                time.sleep(0.05)

    @classmethod
    def tearDownClass(cls):
        cls.s_server.terminate()
        cls.s_server.wait(timeout=10)
        for server in (cls.main, cls.other, cls.wrong_name, cls.untrusted):
            server.shutdown()
        shutil.rmtree(cls.folder, ignore_errors=True)

    def roots(self):
        return str(self.folder / "ca.der")

    def url(self, path, server=None):
        return f"https://localhost:{(server or self.main).server_port}{path}"

    def drive(self, *arguments, roots=None):
        result = run([str(self.driver), arguments[0], roots or self.roots(), *map(str, arguments[1:])], timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        line = result.stdout.strip().splitlines()[-1]
        fields = dict(re.findall(r"(\w+)=(\S*)", line.split(" error=")[0]))
        fields["error"] = line.split(" error=", 1)[1] if " error=" in line else ""
        return fields

    def get(self, path, start=0, most=10_000_000, stop_after=None, server=None):
        out = self.folder / "body.bin"
        arguments = ["get", self.url(path, server), start, most, out] + ([stop_after] if stop_after is not None else [])
        fields = self.drive(*arguments)
        fields["body"] = out.read_bytes() if out.exists() else b""
        return fields

    def test_openssl_s_server(self):
        fields = self.get("/plain.bin", server=type("port", (), {"server_port": self.s_server_port}))
        self.assertEqual((fields["got"], fields["status"]), ("0", "200"), fields)
        self.assertEqual(fields["body"], FILE[:30000])

    def test_a_redirect_to_another_host_and_key_type(self):
        fields = self.get("/redirect")
        self.assertEqual((fields["got"], fields["status"], fields["redirects"]), ("0", "200", "1"), fields)
        self.assertEqual(fields["body"], FILE)
        self.assertEqual(fields["total"], str(len(FILE)))

    def test_a_relative_redirect(self):
        fields = self.get("/relative")
        self.assertEqual((fields["got"], fields["status"], fields["redirects"]), ("0", "200", "1"), fields)
        self.assertEqual(fields["body"], FILE)

    def test_a_range(self):
        fields = self.get("/file", start=150_000)
        self.assertEqual((fields["got"], fields["status"], fields["start"], fields["offset"]), ("0", "206", "206", "150000"), fields)
        self.assertEqual(fields["total"], str(len(FILE)))
        self.assertEqual(fields["body"], FILE[150_000:])

    def test_a_chunked_body(self):
        fields = self.get("/chunked")
        self.assertEqual((fields["got"], fields["status"]), ("0", "200"), fields)
        self.assertEqual(fields["body"], FILE)

    def test_not_found_is_an_answer(self):
        fields = self.get("/notfound")
        self.assertEqual((fields["got"], fields["status"], fields["received"]), ("0", "404", "0"), fields)
        out = self.folder / "fetched.bin"
        fields = self.drive("fetch", self.url("/notfound"), 4096, out)
        self.assertEqual(fields["got"], "-1")
        self.assertIn("answered 404", fields["error"])

    def test_a_small_file_into_memory(self):
        out = self.folder / "fetched.bin"
        fields = self.drive("fetch", self.url("/redirect"), 300_000, out)
        self.assertEqual((fields["got"], fields["size"]), ("0", str(len(FILE))), fields)
        self.assertEqual(out.read_bytes(), FILE)
        fields = self.drive("fetch", self.url("/file"), 4096, out)
        self.assertEqual(fields["got"], "-1", fields)

    def test_a_bad_certificate_is_refused_before_anything_is_sent(self):
        before = len(self.untrusted.requests)
        fields = self.get("/file", server=self.untrusted)
        self.assertEqual(fields["got"], "-1", fields)
        self.assertIn("certificate of localhost was refused", fields["error"])
        self.assertIn("not correctly signed by the trusted CA", fields["error"])
        self.assertEqual(len(self.untrusted.requests), before)

    def test_a_certificate_for_another_name_is_refused(self):
        before = len(self.wrong_name.requests)
        fields = self.get("/file", server=self.wrong_name)
        self.assertEqual(fields["got"], "-1", fields)
        self.assertIn("refused", fields["error"])
        self.assertRegex(fields["error"], "name|CN")
        self.assertEqual(len(self.wrong_name.requests), before)

    def test_a_body_cut_short(self):
        fields = self.get("/truncated")
        self.assertEqual(fields["got"], "-1", fields)
        self.assertIn("broke off after 50000 of 100000 bytes", fields["error"])

    def test_a_stall_is_stopped_within_a_quarter_of_a_second(self):
        fields = self.get("/stall", stop_after=400)
        self.assertEqual((fields["got"], fields["stopped"]), ("-1", "1"), fields)
        self.assertLess(int(fields["after_stop_ms"]), 250, fields)

    def test_redirects_have_a_limit_and_must_stay_https(self):
        fields = self.get("/loop")
        self.assertEqual(fields["got"], "-1")
        self.assertIn("more than 5 redirects", fields["error"])
        fields = self.get("/plain-http")
        self.assertEqual(fields["got"], "-1")
        self.assertIn("not an https:// address", fields["error"])

    def test_headers_have_a_limit(self):
        fields = self.get("/bigheaders")
        self.assertEqual(fields["got"], "-1")
        self.assertIn("headers are longer than", fields["error"])
        fields = self.get("/longline")
        self.assertEqual(fields["got"], "-1")
        self.assertIn("too long", fields["error"])

    def test_a_range_from_elsewhere_and_a_body_too_large_are_refused(self):
        fields = self.get("/wrongrange", start=1000)
        self.assertEqual(fields["got"], "-1")
        self.assertIn("does not start at byte 1000", fields["error"])
        fields = self.get("/file", most=1000)
        self.assertEqual(fields["got"], "-1")
        self.assertIn("more than the 1000 asked for", fields["error"])

    def download(self, path, part, *pause, size=len(FILE), sha256=None):
        new = part.with_suffix(".new")
        new.unlink(missing_ok=True)
        fields = self.drive("download", self.url(path), part, new, size, sha256 or hashlib.sha256(FILE).hexdigest(), *pause)
        return fields, new

    def test_a_download_paused_by_home_goes_on_with_a_range_after_a_fresh_redirect(self):
        part = self.folder / "home.part"
        part.unlink(missing_ok=True)
        hits = len(self.main.requests)
        fields, new = self.download("/dl", part, 65536, 400)
        self.assertEqual((fields["result"], fields["pauses"]), ("1", "1"), fields)
        self.assertEqual(new.read_bytes(), FILE)
        self.assertFalse(part.exists())
        # the redirect asked for twice (its link expires), and the second request a range from where the card's copy ended
        redirects = [request for request in self.main.requests[hits:] if request[0] == "/dl"]
        self.assertEqual(len(redirects), 2, self.main.requests[hits:])
        ranges = [request[1] for request in self.other.requests if request[1]]
        self.assertTrue(any(re.fullmatch(r"bytes=(65536|131072)-", value) for value in ranges), ranges)

    def test_a_download_resumed_from_an_earlier_run(self):
        part = self.folder / "earlier.part"
        part.write_bytes(FILE[:120_000])
        before = len(self.other.requests)
        fields, new = self.download("/dl", part)
        self.assertEqual(fields["result"], "1", fields)
        self.assertEqual(new.read_bytes(), FILE)
        self.assertIn("bytes=120000-", [request[1] for request in self.other.requests[before:]])

    def test_a_server_that_ignores_the_range_starts_the_file_again(self):
        part = self.folder / "norange.part"
        part.write_bytes(FILE[:50_000])
        fields, new = self.download("/dl-norange", part)
        self.assertEqual(fields["result"], "1", fields)
        self.assertEqual(new.read_bytes(), FILE)

    def test_a_damaged_download_is_refused_and_deleted(self):
        part = self.folder / "damaged.part"
        part.unlink(missing_ok=True)
        fields, new = self.download("/damaged", part)
        self.assertEqual(fields["result"], "-1", fields)
        self.assertIn("SHA-256 differs", fields["error"])
        self.assertFalse(part.exists() or new.exists())
        # a part that is not the file's beginning is found out at its end, not trusted
        part.write_bytes(DAMAGED[:160_000])
        fields, new = self.download("/file", part)
        self.assertEqual(fields["result"], "-1", fields)
        self.assertFalse(new.exists())

    def test_a_part_longer_than_the_file_is_started_again(self):
        part = self.folder / "long.part"
        part.write_bytes(FILE + b"more")
        fields, new = self.download("/file", part)
        self.assertEqual(fields["result"], "1", fields)
        self.assertEqual(new.read_bytes(), FILE)

    def test_a_cancelled_download_keeps_its_part(self):
        part = self.folder / "cancel.part"
        part.unlink(missing_ok=True)
        fields, new = self.download("/file", part, 0, 0, 65536)
        self.assertEqual((fields["result"], fields["stopped"]), ("0", "1"), fields)
        self.assertFalse(new.exists())
        self.assertTrue(part.exists() and FILE.startswith(part.read_bytes()) and part.stat().st_size >= 65536)

    def test_a_file_is_matched_by_its_size_and_hash(self):
        path = self.folder / "match.bin"
        path.write_bytes(FILE)
        digest = hashlib.sha256(FILE).hexdigest()
        self.assertEqual(run([str(self.driver), "matches", str(path), str(len(FILE)), digest]).stdout.strip(), "1")
        self.assertEqual(run([str(self.driver), "matches", str(path), str(len(FILE) - 1), digest]).stdout.strip(), "0")
        self.assertEqual(run([str(self.driver), "matches", str(path), str(len(FILE)), "0" * 64]).stdout.strip(), "0")
        # (the stop hook is asked between the pieces of 64 KB: a 200 KB file stopped after two is not read to its end)
        self.assertEqual(run([str(self.driver), "matches", str(path), str(len(FILE)), digest, "2"]).stdout.strip(), "-1")
        self.assertEqual(run([str(self.driver), "matches", str(path), str(len(FILE)), digest, "4"]).stdout.strip(), "1")

    @unittest.skipUnless(os.environ.get("OPENCE_GITHUB_TEST") == "1", "GitHub is asked only with OPENCE_GITHUB_TEST=1")
    def test_github_with_the_consoles_roots(self):
        out = self.folder / "github.json"
        fields = self.drive("get", "https://api.github.com/repos/OpenCommunityEdition/OpenCE/releases/latest", 0, 2_000_000, out,
                            roots="embedded")
        self.assertEqual((fields["got"], fields["status"]), ("0", "200"), fields)
        latest = out.read_text()
        self.assertIn('"tag_name"', latest)
        asset = re.search(r'"browser_download_url":\s*"([^"]+)"', latest)
        if asset:
            # (the release's download redirects to the file host, ISRG Root X1's: the answer's headers are enough)
            fields = self.drive("get", asset.group(1), 0, 1, out, roots="embedded")
            self.assertEqual(fields["got"], "-1", fields)
            self.assertIn("more than the 1 asked for", fields["error"])
            self.assertGreaterEqual(int(fields["redirects"]), 1, fields)
            self.assertIn("githubusercontent.com", fields["host"])


# ---------- what the build holds

class SourceTests(unittest.TestCase):
    def test_the_roots_header_has_the_roots_its_generator_names(self):
        sys.path.insert(0, str(ROOT / "tools/wiiu"))
        import update_roots

        header = (SRC / "wiiu_update_roots.h").read_text()
        arrays = re.findall(r"wiiu_update_root_(\d+)\[(\d+)\] =\n\{\n(.*?)\n\};", header, re.S)
        self.assertEqual(len(arrays), len(update_roots.ROOTS))
        for (index, size, body), (name, fingerprint, _, _) in zip(arrays, update_roots.ROOTS):
            der = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-f]{2})", body))
            self.assertEqual(len(der), int(size), name)
            self.assertEqual(hashlib.sha256(der).hexdigest(), fingerprint, name)

    def test_the_shell_starts_it_after_the_last_runs_notes_and_waits_for_it_before_the_sockets_close(self):
        shell = (SRC / "wiiu_shell.c").read_text()
        run = shell[shell.index("int wiiu_shell_run("):]
        crash = run.index("crash_note = report_previous(CRASH_FILE, CRASH_PREVIOUS_FILE")
        self.assertLess(run.index('report_file(EXIT_TRAIL_PREVIOUS_FILE, "close trail")'), crash)
        startup = run.index("relaunching = wiiu_update_startup(crash_note);")
        self.assertLess(crash, startup)
        self.assertLess(startup, run.index("error = relaunching ? 0 : start_game();"))
        stop = run.index("wiiu_update_stop(500)")
        self.assertLess(run.index("wiiu_shell_blackbox_stop(&shell_box);"), stop)
        # (outside the shader capture's close block, which tools/test_wiiu_missing_keys.py compiles on its own)
        self.assertLess(stop, run.index("\t/* Bounded wait only:"))
        self.assertLess(stop, run.index('wiiu_log("Closed");'))
        self.assertLess(stop, run.index("wiiu_posix_net_close_all()"))
        # (a run that never started the game never used the GPU)
        self.assertIn("if (foreground && started_game)\n\t\t\tGX2DrawDone();", run)

    def test_the_worker_waits_in_steps_of_a_quarter_of_a_second_at_most(self):
        for name in ("wiiu_update.c", "wiiu_update_http.c"):
            source = (SRC / name).read_text()
            for value in re.findall(r"sleep_ms\((\d+)\)", source):
                self.assertLessEqual(int(value), 250, name)
        http = (SRC / "wiiu_update_http.c").read_text()
        self.assertLessEqual(int(re.search(r"#define WAIT_STEP_MS (\d+)", http).group(1)), 100)
        # (the waits between pieces of a body that never waits are asked too)
        give = http[http.index("static int give("):http.index("static int read_body(")]
        self.assertLess(give.index("hooks->stop("), give.index("hooks->body("))

    def test_the_network_card_and_menus_gate_it(self):
        source = (SRC / "wiiu_update.c").read_text()
        network = source[source.index("static int may_use_network(void)"):]
        self.assertIn("wiiu_network_allowed() && wiiu_in_foreground()", network[:200])
        writes = source[source.index("static int download_may_write("):]
        self.assertIn("may_use_network() && at_main_menu()", writes[:300])
        menu = source[source.index("static int at_main_menu(void)"):]
        self.assertIn("wiiu_in_foreground() && load(&main_menu_now) && load(&menus_now)", menu[:200])
        # HOME off around the swap, and on again before the new build is started
        update = source[source.index("static void update_now("):source.index("static void offer_update(")]
        self.assertLess(update.index("OSEnableHomeButtonMenu(FALSE);"), update.index("wiiu_update_commit("))
        self.assertLess(update.index("wiiu_update_commit("), update.index("OSEnableHomeButtonMenu(TRUE);"))
        self.assertLess(update.index("OSEnableHomeButtonMenu(TRUE);"), update.index("relaunch();"))
        # off unless the build was given all three
        off = source[source.index("static const char *off_reason(void)"):]
        for needed in ("HALO_BUILD_NUMBER <= 0", "REPOSITORY[0]", "wiiu_update_key_from_hex(PUBLIC_KEY, key)"):
            self.assertIn(needed, off[:600])
        startup = source[source.index("int wiiu_update_startup(int crash_note)"):]
        self.assertLess(startup.index("if (off)"), startup.index("mkdir("))

    def test_the_controllers_and_the_renderer_have_the_panel(self):
        xinput = (SRC / "xinput_wiiu.c").read_text()
        state = xinput[xinput.index("DWORD WINAPI XInputGetState("):]
        self.assertLess(state.index("wiiu_update_menus(port_main_menu_active(), port_menus_active());"), state.index("wiiu_update_panel_input("))
        self.assertLess(state.index("wiiu_update_panel_input("), state.index("pad_state(&pad, &state->Gamepad);"))
        self.assertIn("int port_main_menu_active(void)\n{\n\treturn main_menu_is_active() && ui_widgets_active() ? 1 : 0;",
                      (ROOT / "port/linux/game/hud_hires_tags.c").read_text())
        gx2 = (SRC / "wiiu_gx2.c").read_text()
        present = gx2[gx2.index("static void present(const struct dk_command_present *command)"):]
        # (each screen's blit: back, the rectangle it samples and its quad, then that screen's buffer and state: wiiu-drs's blit_to)
        blits = {screen: re.search(rf"blit_to\(back, [^;]*WHBGfxGet{screen}ColourBuffer\(\)", present).start() for screen in ("TV", "DRC")}
        for screen in ("TV", "DRC"):
            panel = present.index(f"panel_draw(WHBGfxGet{screen}ColourBuffer()")
            copy = present.index(f"GX2CopyColorBufferToScanBuffer(WHBGfxGet{screen}ColourBuffer()")
            self.assertTrue(blits[screen] < panel < copy, screen)
        self.assertLess(present.index("panel_update();"), blits["TV"])

    def test_the_panels_lines_fit_its_62_columns(self):
        """every line the panel shows (wiiu_gx2.c draws 64 cells of the 3x5 font, one at each side left empty), with numbers of six
        digits and names of a dozen letters"""
        source = (SRC / "wiiu_update.c").read_text()
        texts = re.findall(r'(?:panel_show\(1, |tell_and_wait\(answer, [^"]*)((?:"(?:[^"\\]|\\.)*"\s*)+)', source)
        texts += re.findall(r'\? ("[A-Z][A-Z \':,.]+\.")', source)
        self.assertGreater(len(texts), 12)
        for text in texts:
            joined = "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', text)).replace("\\n", "\n")
            for line in joined.replace("%u.%u", "123456.7").replace("%u", "123456").replace("%s", "shaders.pack").split("\n"):
                self.assertLessEqual(len(line), 62, line)

    def test_the_maps_format_is_the_converters_and_the_windows(self):
        sys.path.insert(0, str(ROOT / "tools/wiiu"))
        source = (SRC / "wiiu_update.c").read_text()
        token = re.search(r'#define MAPS_FORMAT "([0-9a-f]+)"', source).group(1)
        converter = (ROOT / "tools/wiiu/convert_maps.py").read_text()
        rebase = re.search(r"WIIU_REBASE = \((0x[0-9A-Fa-f]+) - 0x80000000\) & 0xFFFFFFFF", converter).group(1)
        self.assertEqual(int(token, 16), (int(rebase, 16) - 0x80000000) & 0xFFFFFFFF)
        capacity = (ROOT / "port/linux/include/halo_port_capacity.h").read_text()
        window = re.search(r"#elif defined\(HALO_WIIU\)\n#define HALO_PORT_WINDOW_BASE (0x[0-9A-F]+)UL", capacity).group(1)
        self.assertEqual(int(window, 16), int(rebase, 16))
        self.assertIn(f"#define WINDOW_BASE {window}u", (SRC / "wiiu_window.c").read_text())


if __name__ == "__main__":
    unittest.main()
