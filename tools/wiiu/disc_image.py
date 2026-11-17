"""Bounded XDVDFS map reader, following port/linux/src/xiso.c.

Accepts an Xbox game-partition image, a full disc, or .iso.0/.iso.1/... parts.
Only the map entries are exposed; this module never extracts arbitrary paths.
"""
import bisect
import struct
from contextlib import ExitStack
from dataclasses import dataclass
from pathlib import Path

MAGIC = b"MICROSOFT*XBOX*MEDIA"
PARTITIONS = (0, 0x0FD90000, 0x02080000, 0x18300000)
MAX_TABLE = 4 * 1024 * 1024
MAX_MAP = 384 * 1024 * 1024
RETAIL_MAPS = tuple(name + ".map" for name in (
    "a10", "a30", "a50", "b30", "b40", "c10", "c20", "c40", "d20", "d40",
    "beavercreek", "bloodgulch", "boardingaction", "carousel", "chillout", "damnation",
    "hangemhigh", "longest", "prisoner", "putput", "ratrace", "sidewinder", "ui", "wizard"))


class SetupError(Exception):
    pass


@dataclass(frozen=True)
class Entry:
    name: str
    offset: int
    size: int
    directory: bool


class DiscImage:
    def __init__(self, path):
        self.path = Path(path)
        self.stack = ExitStack()
        self.parts = []
        self.ends = []
        self.partition = 0

    def __enter__(self):
        try:
            split = self.path.name.lower().endswith((".iso.0", ".xiso.0"))
            paths = [self.path]
            if split:
                paths = [self.path.with_name(self.path.name[:-1] + str(i)) for i in range(16)]
            gap = False
            total = 0
            for path in paths:
                if not path.is_file():
                    gap = True
                    continue
                if gap:
                    raise SetupError("A split-image part is missing. Parts must be numbered .0, .1, .2, ...")
                size = path.stat().st_size
                if not size:
                    raise SetupError(f"The image part is empty: {path.name}")
                self.parts.append(self.stack.enter_context(path.open("rb")))
                total += size
                self.ends.append(total)
            if not self.parts:
                raise SetupError("The disc image could not be opened.")
            return self
        except BaseException:
            self.stack.close()
            raise

    def __exit__(self, *args):
        self.stack.close()

    def read(self, offset, size):
        if offset < 0 or size < 0 or offset > self.ends[-1] or size > self.ends[-1] - offset:
            raise SetupError("The disc image is incomplete or has an invalid file offset.")
        result = bytearray()
        while size:
            part = bisect.bisect_right(self.ends, offset)
            base = self.ends[part - 1] if part else 0
            count = min(size, self.ends[part] - offset)
            self.parts[part].seek(offset - base)
            chunk = self.parts[part].read(count)
            if len(chunk) != count:
                raise SetupError("The disc image could not be read completely.")
            result.extend(chunk)
            offset += count
            size -= count
        return bytes(result)

    def directory(self, sector, size):
        if not 0 < size <= MAX_TABLE:
            raise SetupError("The disc image has an invalid directory table.")
        table = self.read(self.partition + sector * 2048, size)
        pending, seen, entries = [(0, 0)], set(), []
        while pending:
            at, depth = pending.pop()
            if depth > 64 or at in seen or len(seen) >= 4096 or at > size - 14:
                raise SetupError("The disc image has a damaged directory tree.")
            seen.add(at)
            left, right, sector, length, attributes, name_length = struct.unpack_from("<HHIIBB", table, at)
            if left == 0xFFFF:
                continue
            if not name_length or name_length > size - at - 14:
                raise SetupError("The disc image has a damaged filename.")
            raw = table[at + 14:at + 14 + name_length]
            if any(c < 32 or c > 126 or chr(c) in '/\\:*?"<>|' for c in raw) or raw in (b".", b".."):
                raise SetupError("The disc image contains an invalid filename.")
            entries.append(Entry(raw.decode("ascii"), self.partition + sector * 2048, length, bool(attributes & 0x10)))
            if len(entries) > 256:
                raise SetupError("The disc image has too many entries in a directory.")
            if right:
                pending.append((right * 4, depth + 1))
            if left:
                pending.append((left * 4, depth + 1))
        return entries

    def maps(self, require_complete=True):
        volume = None
        for partition in PARTITIONS:
            if partition + 0x10800 > self.ends[-1]:
                continue
            header = self.read(partition + 0x10000, 2048)
            if header[:20] == MAGIC and header[0x7EC:0x800] == MAGIC:
                self.partition = partition
                volume = struct.unpack_from("<II", header, 0x14)
                break
        if volume is None:
            raise SetupError("This is not a supported original-Xbox ISO/XISO image.")
        directories = {e.name.lower(): e for e in self.directory(*volume) if e.directory}
        folder = directories.get("maps") or directories.get("maps_en")
        if folder is None:
            folder = next((e for name, e in sorted(directories.items()) if name.startswith("maps_")), None)
        if folder is None:
            raise SetupError("The disc image has no Halo maps folder.")
        files = {}
        for entry in self.directory((folder.offset - self.partition) // 2048, folder.size):
            if entry.directory:
                continue
            name = entry.name.lower()
            if name in files:
                raise SetupError(f"The disc contains duplicate filenames: {entry.name}")
            if entry.offset > self.ends[-1] or entry.size > self.ends[-1] - entry.offset:
                raise SetupError(f"The disc image is incomplete at {entry.name}.")
            if name in RETAIL_MAPS and not 0 < entry.size <= MAX_MAP:
                raise SetupError(f"Unsupported map size: {entry.name}")
            files[name] = entry
        missing = [name for name in RETAIL_MAPS if name not in files]
        if (require_complete and missing) or "ui.map" not in files:
            raise SetupError("Use a complete Xbox Halo disc. Missing maps: " + ", ".join(missing))
        return files

    def copy(self, entry, destination, progress=lambda *_: None):
        with Path(destination).open("wb") as output:
            for at in range(0, entry.size, 1024 * 1024):
                progress(at, entry.size)
                output.write(self.read(entry.offset + at, min(1024 * 1024, entry.size - at)))
