#!/usr/bin/env python3
"""Strict instruction/ELF contract for the single audio PS assembly object.

The ordinary Wii U checker still rejects PS everywhere else. This audit is
an exact four-function allowlist, not a waiver for forbidden instructions.
"""
import os
import re
import struct
import subprocess
import sys
from pathlib import Path

TEXT = ".text.wiiu_resampler_ps"
ZERO = ".rodata.wiiu_resampler_ps"
OBJDUMP = os.environ.get("WIIU_OBJDUMP", "powerpc-eabi-objdump")


def contract(stereo, blended):
    """Independent instruction contract, including every register and offset."""
    lines = ["lis r12,0"]
    lines += [f"lfs f{n},0(r12)" for n in range(4 if stereo else 2)]
    if blended:
        lines += [f"lfs f12,0(r{7 if stereo else 6})"]
    lines += ["li r0,12", "mtctr r0"]
    loop = len(lines) * 4
    if stereo:
        lines += ["psq_l f4,0(r5),0,0", "psq_l f5,8(r5),0,0"]
        if blended:
            lines += ["psq_l f6,0(r6),0,0", "psq_l f7,8(r6),0,0",
                      "ps_mul f6,f6,f12", "ps_mul f7,f7,f12",
                      "ps_add f4,f4,f6", "ps_add f5,f5,f7"]
        lines += ["psq_l f6,0(r3),0,0", "psq_l f7,8(r3),0,0",
                  "psq_l f8,0(r4),0,0", "psq_l f9,8(r4),0,0"]
        if blended:
            lines += ["ps_mul f6,f6,f4", "ps_mul f7,f7,f5",
                      "ps_mul f8,f8,f4", "ps_mul f9,f9,f5",
                      "ps_add f0,f0,f6", "ps_add f1,f1,f7",
                      "ps_add f2,f2,f8", "ps_add f3,f3,f9"]
        else:
            lines += ["ps_mul f10,f6,f4", "ps_mul f11,f7,f5",
                      "ps_add f0,f0,f10", "ps_add f1,f1,f11",
                      "ps_mul f10,f8,f4", "ps_mul f11,f9,f5",
                      "ps_add f2,f2,f10", "ps_add f3,f3,f11"]
    else:
        lines += ["psq_l f2,0(r4),0,0", "psq_l f3,8(r4),0,0",
                  "psq_l f4,0(r5),0,0" if blended else "psq_l f4,0(r3),0,0",
                  "psq_l f5,8(r5),0,0" if blended else "psq_l f5,8(r3),0,0"]
        if blended:
            lines += ["ps_mul f4,f4,f12", "ps_mul f5,f5,f12",
                      "ps_add f2,f2,f4", "ps_add f3,f3,f5",
                      "psq_l f6,0(r3),0,0", "psq_l f7,8(r3),0,0",
                      "ps_mul f8,f6,f2", "ps_mul f9,f7,f3",
                      "ps_add f0,f0,f8", "ps_add f1,f1,f9"]
        else:
            lines += ["ps_mul f6,f4,f2", "ps_mul f7,f5,f3",
                      "ps_add f0,f0,f6", "ps_add f1,f1,f7"]
    lines += [f"addi r{n},r{n},16" for n in range(3, (7 if blended else 6) if stereo else (6 if blended else 5))]
    lines += [f"bdnz+ @{loop}"]
    lines += [f"ps_sum0 f{n},f{n},f{n},f{n}" for n in range(4 if stereo else 2)]
    lines += ["fadds f0,f0,f1"]
    if stereo:
        lines += ["fadds f2,f2,f3"]
    output = (8 if blended else 6) if stereo else (7 if blended else 5)
    lines += [f"stfs f0,0(r{output})"]
    if stereo:
        lines += [f"stfs f2,4(r{output})"]
    return lines + ["blr"]


CONTRACTS = {
    "wiiu_resampler_ps_dot48": contract(False, False),
    "wiiu_resampler_ps_dot48_stereo": contract(True, False),
    "wiiu_resampler_ps_blended48": contract(False, True),
    "wiiu_resampler_ps_blended48_stereo": contract(True, True),
}


def cstring(data, offset):
    if not 0 <= offset < len(data) or b"\0" not in data[offset:]:
        raise ValueError("Malformed ELF string")
    return data[offset:].split(b"\0", 1)[0].decode("ascii")


def elf_contract(data):
    if len(data) < 52 or data[:7] != b"\x7fELF\x01\x02\x01":
        raise ValueError("Audio kernel must be big-endian ELF32")
    if struct.unpack_from(">HHI", data, 16) != (1, 20, 1):
        raise ValueError("Audio kernel must be relocatable PowerPC ELF")
    section_offset = struct.unpack_from(">I", data, 32)[0]
    size, count, names_index = struct.unpack_from(">HHH", data, 46)
    if size != 40 or not count or names_index >= count or section_offset + count * size > len(data):
        raise ValueError("Malformed ELF section table")
    sections = []
    for index in range(count):
        fields = struct.unpack_from(">10I", data, section_offset + index * size)
        name, kind, flags, address, offset, length, link, info, alignment, entry_size = fields
        if kind != 8 and offset + length > len(data):
            raise ValueError("ELF section extends outside file")
        sections.append(dict(name_offset=name, kind=kind, flags=flags, address=address, data=data[offset:offset + length],
                             length=length, link=link, info=info, entry_size=entry_size, index=index))
    names = sections[names_index]["data"]
    for section in sections:
        section["name"] = cstring(names, section["name_offset"])
    by_name = {section["name"]: section for section in sections}
    if len(by_name) != len(sections) or TEXT not in by_name or ZERO not in by_name:
        raise ValueError("Missing or duplicate audio sections")
    text, zero = by_name[TEXT], by_name[ZERO]
    if text["flags"] != 6 or zero["flags"] != 2 or zero["data"] != b"\0" * 4:
        raise ValueError("Audio text/constant section contract changed")
    for section in sections:
        if section["length"] and section["flags"] & 6 and section["name"] not in (TEXT, ZERO):
            raise ValueError("Unexpected allocated or executable section")
    symbol_tables = [section for section in sections if section["kind"] == 2]
    if len(symbol_tables) != 1:
        raise ValueError("Expected exactly one symbol table")
    table = symbol_tables[0]
    if table["entry_size"] != 16 or table["length"] % 16 or table["link"] >= count:
        raise ValueError("Malformed ELF symbol table")
    strings = sections[table["link"]]["data"]
    symbols = []
    for at in range(0, table["length"], 16):
        name, value, length, info, other, section = struct.unpack_from(">IIIBBH", table["data"], at)
        symbols.append(dict(name=cstring(strings, name), value=value, length=length, info=info, section=section))
    functions = [symbol for symbol in symbols if symbol["section"] == text["index"] and symbol["info"] & 15 != 3]
    expected_offset = 0
    for symbol, (name, instructions) in zip(functions, CONTRACTS.items()):
        if (symbol["name"], symbol["value"], symbol["length"], symbol["info"]) != (name, expected_offset, 4 * len(instructions), 0x12):
            raise ValueError("Audio function symbol or extent contract changed")
        expected_offset += symbol["length"]
    if len(functions) != 4 or text["length"] != expected_offset:
        raise ValueError("Unexpected function or executable bytes")
    if any(symbol["info"] >> 4 and symbol["section"] != text["index"] for symbol in symbols):
        raise ValueError("Unexpected global or undefined symbol")
    expected_relocations = []
    offset = 0
    for instructions in CONTRACTS.values():
        for index, instruction in enumerate(instructions):
            if instruction == "lis r12,0":
                expected_relocations.append((offset + index * 4 + 2, 6))  # R_PPC_ADDR16_HA
            elif instruction.startswith("lfs ") and instruction.endswith(",0(r12)"):
                expected_relocations.append((offset + index * 4 + 2, 4))  # R_PPC_ADDR16_LO
        offset += len(instructions) * 4
    relocations = []
    for section in sections:
        if section["kind"] not in (4, 9):
            continue
        if section["info"] >= count:
            raise ValueError("Malformed relocation target")
        target = sections[section["info"]]
        if target["name"] != TEXT:
            if target["flags"] & 6:
                raise ValueError("Unexpected allocated-section relocation")
            continue
        if section["kind"] != 4 or section["entry_size"] != 12 or section["link"] != table["index"] or section["length"] % 12:
            raise ValueError("Unsupported text relocation table")
        for at in range(0, section["length"], 12):
            address, info, addend = struct.unpack_from(">IIi", section["data"], at)
            if info >> 8 >= len(symbols):
                raise ValueError("Invalid relocation symbol")
            symbol = symbols[info >> 8]
            if symbol["section"] != zero["index"] or symbol["value"] or addend:
                raise ValueError("Text relocation must address only constant zero")
            relocations.append((address, info & 255))
    if sorted(relocations) != expected_relocations:
        raise ValueError("Audio constant relocation contract changed")
    return text["length"]


def audit(path, objdump=OBJDUMP):
    length = elf_contract(Path(path).read_bytes())
    listing = subprocess.run([objdump, "-d", "-M", "broadway", "--no-show-raw-insn", str(path)],
                             capture_output=True, text=True, check=True).stdout
    expected = {}
    offset = 0
    for instructions in CONTRACTS.values():
        for index, instruction in enumerate(instructions):
            if instruction.startswith("bdnz+ @"):
                instruction = "bdnz+ " + format(offset + int(instruction.split("@")[1]), "x")
            expected[offset + index * 4] = instruction
        offset += 4 * len(instructions)
    found = {}
    for line in listing.splitlines():
        match = re.match(r"\s*([0-9a-f]+):\s+(\S+)\s*(.*)", line)
        if not match:
            continue
        address, mnemonic, operands = match.groups()
        address = int(address, 16)
        operands = operands.split(" <", 1)[0].strip()
        instruction = mnemonic + (" " + operands if operands else "")
        if address in found or expected.get(address) != instruction:
            raise ValueError(f"Unexpected audio instruction at {address:x}: {instruction}")
        found[address] = instruction
    if found != expected or len(found) * 4 != length:
        raise ValueError("Unparsed or missing executable audio bytes")
    return {name: sum(instruction.startswith(name + " ") for instruction in found.values())
            for name in ("psq_l", "ps_mul", "ps_add", "ps_sum0")}


def main():
    if len(sys.argv) != 2:
        print("usage: wiiu_resampler_ps_audit.py AUDIO_OBJECT", file=sys.stderr)
        return 2
    try:
        result = audit(Path(sys.argv[1]))
    except (OSError, ValueError, UnicodeDecodeError, struct.error, subprocess.CalledProcessError) as error:
        print(f"{sys.argv[1]}: audio PS audit failed: {error}", file=sys.stderr)
        return 1
    print("Audio PS exact-object audit: " + ", ".join(f"{name}={count}" for name, count in result.items()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
