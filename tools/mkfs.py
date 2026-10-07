#!/usr/bin/env python3
import argparse
import pathlib
import struct

SECTOR_SIZE = 512
FILESYSTEM_LBA = 512
MAGIC = 0x31534654
VERSION = 1
MAX_FILES = 16
NAME_SIZE = 16
HEADER = struct.Struct("<III")
ENTRY = struct.Struct("<16sII")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output")
    parser.add_argument("files", nargs="+", help="disk-name=source-path")
    args = parser.parse_args()

    files = []
    for item in args.files:
        name, separator, source = item.partition("=")
        if not separator or not name or len(name.encode("ascii")) >= NAME_SIZE:
            parser.error(f"invalid filesystem file argument: {item}")
        path = pathlib.Path(source)
        contents = path.read_bytes()
        if not contents:
            parser.error(f"empty files are not supported: {path}")
        files.append((name.encode("ascii"), contents))

    if len(files) > MAX_FILES:
        parser.error(f"at most {MAX_FILES} files are supported")

    next_lba = FILESYSTEM_LBA + 1
    entries = []
    payload = bytearray()
    for name, contents in files:
        sectors = (len(contents) + SECTOR_SIZE - 1) // SECTOR_SIZE
        entries.append((name + b"\0" * (NAME_SIZE - len(name)), next_lba, len(contents)))
        payload.extend(contents)
        payload.extend(b"\0" * (sectors * SECTOR_SIZE - len(contents)))
        next_lba += sectors

    header = bytearray(SECTOR_SIZE)
    HEADER.pack_into(header, 0, MAGIC, VERSION, len(entries))
    offset = HEADER.size
    for entry in entries:
        ENTRY.pack_into(header, offset, *entry)
        offset += ENTRY.size

    output = bytearray(header)
    output.extend(payload)
    pathlib.Path(args.output).write_bytes(output)


if __name__ == "__main__":
    main()
