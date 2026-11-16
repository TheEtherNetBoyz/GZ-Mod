#!/usr/bin/env python3
"""Generate macOS live-state ranges from an Apple ld map.

The Windows table covers game/JSystem state plus host-side controller state
that participates in simulation. The Mac table mirrors that boundary while
leaving renderer, SDL, and allocator-owning libc++ containers outside it.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass


OBJECT_RE = re.compile(r"^\[\s*(\d+)\]\s+(.+)$")
SECTION_RE = re.compile(r"^(0x[0-9A-Fa-f]+)\s+(0x[0-9A-Fa-f]+)\s+(\S+)\s+(\S+)$")
SYMBOL_RE = re.compile(r"^(0x[0-9A-Fa-f]+)\s+(0x[0-9A-Fa-f]+)\s+\[\s*(\d+)\]\s+(.+)$")
WRITABLE_SECTIONS = {
    ("__DATA", "__data"),
    ("__DATA", "__bss"),
    ("__DATA", "__common"),
}

# These are PC presentation helpers compiled into otherwise native game
# translation units. They own libc++ allocations, not emulated/game-heap
# memory, so restoring their object bytes would revive stale malloc pointers.
HOST_ONLY_SYMBOLS = {
    "_ZN12_GLOBAL__N_16s_rainE",
    "_ZN12_GLOBAL__N_110s_snow_posE",
    "_ZN12_GLOBAL__N_111s_snow_baseE",
    "_ZN12_GLOBAL__N_113s_housi_worldE",
    "_ZN12_GLOBAL__N_113s_housi_scaleE",
    "_ZN12_GLOBAL__N_17s_cloudE",
    "_ZN12_GLOBAL__N_18s_vrkumoE",
    "_ZZN12_GLOBAL__N_110map_playerEvE7samples",
    "_ZGVZN12_GLOBAL__N_110map_playerEvE7samples",
}


@dataclass(frozen=True)
class Section:
    start: int
    size: int
    segment: str
    name: str

    def contains(self, address: int) -> bool:
        return self.start <= address < self.start + self.size


@dataclass(frozen=True)
class Symbol:
    address: int
    size: int
    object_id: int
    name: str
    section: tuple[str, str]


def runtime_name(linker_name: str) -> str:
    # Apple ld prints the Mach-O ABI prefix. HookService::resolve expects the
    # platform symbol name without that prefix.
    return linker_name[1:] if linker_name.startswith("_") else linker_name


def selected_object(path: str) -> bool:
    return path.startswith("CMakeFiles/dusklight.dir/") or path.startswith("libJSystem_")


def selected_symbol(path: str, name: str) -> bool:
    if name in HOST_ONLY_SYMBOLS:
        return False
    if selected_object(path):
        return True
    if path == "CMakeFiles/dusk_internal.dir/src/dusk/rupee_slide_tools.cpp.o":
        return True
    return (
        path == "CMakeFiles/dusk_internal.dir/src/dusk/audio/DuskAudioSystem.cpp.o"
        and "JASPoolAllocObject" in name
        and "memPool_" in name
    )


def parse_map(path: pathlib.Path) -> tuple[dict[int, str], list[Section], list[Symbol]]:
    objects: dict[int, str] = {}
    sections: list[Section] = []
    raw_symbols: list[tuple[int, int, int, str]] = []
    mode = "objects"

    with path.open("r", encoding="utf-8", errors="replace") as stream:
        for raw_line in stream:
            line = raw_line.rstrip("\n")
            if line == "# Sections:":
                mode = "sections"
                continue
            if line == "# Symbols:":
                mode = "symbols"
                continue
            if line.startswith("#") or not line:
                continue
            if mode == "objects":
                match = OBJECT_RE.match(line)
                if match:
                    objects[int(match.group(1))] = match.group(2)
            elif mode == "sections":
                match = SECTION_RE.match(line)
                if match:
                    sections.append(
                        Section(
                            int(match.group(1), 16),
                            int(match.group(2), 16),
                            match.group(3),
                            match.group(4),
                        )
                    )
            else:
                match = SYMBOL_RE.match(line)
                if match:
                    raw_symbols.append(
                        (
                            int(match.group(1), 16),
                            int(match.group(2), 16),
                            int(match.group(3)),
                            match.group(4),
                        )
                    )

    writable = [section for section in sections if (section.segment, section.name) in WRITABLE_SECTIONS]
    symbols: list[Symbol] = []
    for address, size, object_id, name in raw_symbols:
        if not size or name.startswith("literal string:") or name.startswith("ltmp"):
            continue
        section = next((item for item in writable if item.contains(address)), None)
        if section and address + size <= section.start + section.size:
            symbols.append(
                Symbol(address, size, object_id, runtime_name(name), (section.segment, section.name))
            )
    return objects, sections, symbols


def cpp_string(value: str) -> str:
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def generate(map_path: pathlib.Path, output_path: pathlib.Path) -> None:
    objects, _sections, symbols = parse_map(map_path)
    counts = Counter(symbol.name for symbol in symbols)
    selected = [
        symbol
        for symbol in symbols
        if selected_symbol(objects.get(symbol.object_id, ""), symbol.name)
    ]
    by_object_section: dict[tuple[int, tuple[str, str]], list[Symbol]] = defaultdict(list)
    for symbol in selected:
        by_object_section[(symbol.object_id, symbol.section)].append(symbol)
    for group in by_object_section.values():
        group.sort(key=lambda symbol: symbol.address)

    resolved: list[tuple[Symbol, Symbol, Symbol | None]] = []
    for symbol in selected:
        group = by_object_section[(symbol.object_id, symbol.section)]
        anchors = [candidate for candidate in group if counts[candidate.name] == 1]
        if not anchors:
            continue
        anchor = symbol if counts[symbol.name] == 1 else min(
            anchors, key=lambda candidate: abs(candidate.address - symbol.address)
        )
        checks = [candidate for candidate in anchors if candidate.name != anchor.name]
        check = min(checks, key=lambda candidate: abs(candidate.address - anchor.address)) if checks else None
        resolved.append((symbol, anchor, check))

    lines = [
        "// Generated by tools/gen-static-ranges-macos.py. Do not edit.",
        "static const StaticRange kMacStaticRanges[] = {",
    ]
    for symbol, anchor, check in sorted(resolved, key=lambda item: item[0].address):
        check_name = cpp_string(check.name) if check else "nullptr"
        check_delta = check.address - anchor.address if check else 0
        if not -(2**31) <= check_delta < 2**31:
            check_name = "nullptr"
            check_delta = 0
        offset = symbol.address - anchor.address
        if not -(2**31) <= offset < 2**31:
            continue
        lines.append(
            f"    {{{cpp_string(anchor.name)}, {offset}, {symbol.size}u, {check_name}, {check_delta}}},"
        )
    lines.append("};")
    lines.append("")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text("\n".join(lines), encoding="utf-8")

    selected_bytes = sum(symbol.size for symbol in selected)
    emitted_bytes = sum(symbol.size for symbol, _anchor, _check in resolved)
    skipped_bytes = selected_bytes - emitted_bytes
    print(
        f"macOS statics: emitted {len(resolved)} symbols / {emitted_bytes} bytes; "
        f"skipped {len(selected) - len(resolved)} unanchored symbols / {skipped_bytes} bytes"
    )
    resolved_symbols = {symbol for symbol, _anchor, _check in resolved}
    missed_by_object: Counter[str] = Counter()
    for symbol in selected:
        if symbol not in resolved_symbols:
            missed_by_object[objects.get(symbol.object_id, f"object {symbol.object_id}")] += symbol.size
    for object_path, byte_count in missed_by_object.most_common(10):
        print(f"  unanchored: {byte_count} bytes in {object_path}")
    if not resolved or skipped_bytes * 10 > selected_bytes:
        raise SystemExit("too much mutable game state was ambiguous in the linker map")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--map", required=True, type=pathlib.Path)
    parser.add_argument("--out", required=True, type=pathlib.Path)
    args = parser.parse_args()
    if not args.map.is_file():
        parser.error(f"linker map does not exist: {args.map}")
    generate(args.map, args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
