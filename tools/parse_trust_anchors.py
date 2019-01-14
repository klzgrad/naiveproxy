#!/usr/bin/env python3

import argparse
import re
import sys


COMMON_PEN_PREFIX = "1.3.6.1.4.1"


def parse_hex(s: str) -> bytes:
    """Parse a hex dump containing spaces, offsets, or ASCII columns."""
    # Remove common tcpdump/Wireshark offset prefixes such as:
    # 0000   aa bb cc ...
    lines = []
    for line in s.splitlines():
        # Strip an offset at the beginning of a line.
        line = re.sub(r"^\s*[0-9a-fA-F]{4,8}\s+", "", line)

        # Keep only hex byte pairs.
        hex_bytes = re.findall(r"(?<![0-9a-fA-F])([0-9a-fA-F]{2})(?![0-9a-fA-F])", line)
        if hex_bytes:
            lines.extend(hex_bytes)

    if not lines:
        # Also support a simple "aabbcc..." or "aa bb cc" input.
        compact = re.sub(r"[^0-9a-fA-F]", "", s)
        if len(compact) % 2:
            raise ValueError("Odd number of hex digits")
        return bytes.fromhex(compact)

    return bytes.fromhex("".join(lines))


def decode_base128(data: bytes, offset: int):
    """
    Decode one ASN.1 base-128 OBJECT IDENTIFIER subidentifier.

    Returns:
        (value, next_offset)
    """
    value = 0

    while True:
        if offset >= len(data):
            raise ValueError("Truncated base-128 integer")

        b = data[offset]
        offset += 1

        # The low 7 bits contain payload.
        value = (value << 7) | (b & 0x7f)

        # High bit clear => last byte.
        if not (b & 0x80):
            return value, offset


def decode_relative_oid(data: bytes) -> list[int]:
    """Decode DER RELATIVE-OID contents octets."""
    if not data:
        raise ValueError("Empty trust-anchor ID")

    values = []
    offset = 0

    while offset < len(data):
        value, offset = decode_base128(data, offset)
        values.append(value)

    return values


def decode_full_oid(data: bytes) -> str:
    """
    Trust Anchor IDs are relative to 1.3.6.1.4.1.
    """
    values = decode_relative_oid(data)
    return COMMON_PEN_PREFIX + "." + ".".join(map(str, values))


def decode_extension_data(data: bytes):
    """
    Decode trust_anchors extension_data.

    TLS syntax:

        TrustAnchorIDList = opaque<0..2^16-1>

        uint16 list_length
        uint8  id_length
        uint8[id_length] id
        ...
    """
    if len(data) < 2:
        raise ValueError("Extension data is too short")

    list_len = int.from_bytes(data[:2], "big")

    if list_len != len(data) - 2:
        raise ValueError(
            f"TrustAnchorIDList length says {list_len} bytes, "
            f"but {len(data) - 2} bytes remain"
        )

    offset = 2
    anchors = []

    while offset < len(data):
        id_len = data[offset]
        offset += 1

        if id_len == 0:
            raise ValueError(
                f"Invalid zero-length Trust Anchor ID at offset {offset - 1}"
            )

        if offset + id_len > len(data):
            raise ValueError(
                f"Trust Anchor ID at offset {offset - 1} "
                f"extends past end of extension"
            )

        raw = data[offset:offset + id_len]
        offset += id_len

        relative = decode_relative_oid(raw)

        anchors.append({
            "raw": raw,
            "relative_oid": ".".join(map(str, relative)),
            "oid": COMMON_PEN_PREFIX + "." + ".".join(map(str, relative)),
        })

    return anchors


def maybe_decode_full_extension(data: bytes):
    """
    Accept either:

      1. extension_data:
           uint16 list_len + list

      2. complete TLS Extension:
           uint16 extension_type +
           uint16 extension_length +
           extension_data
    """
    # Try extension_data first.
    if len(data) >= 2:
        list_len = int.from_bytes(data[:2], "big")
        if list_len == len(data) - 2:
            return None, decode_extension_data(data)

    # Try complete TLS extension.
    if len(data) >= 4:
        extension_type = int.from_bytes(data[:2], "big")
        extension_len = int.from_bytes(data[2:4], "big")

        if extension_len != len(data) - 4:
            raise ValueError(
                f"TLS extension length says {extension_len} bytes, "
                f"but {len(data) - 4} bytes remain"
            )

        extension_data = data[4:]
        anchors = decode_extension_data(extension_data)

        return extension_type, anchors

    raise ValueError("Input is neither valid extension_data nor a full TLS extension")


def print_result(extension_type, anchors):
    if extension_type is not None:
        print(f"Extension type: 0x{extension_type:04x} ({extension_type})")

    print(f"Trust Anchor IDs: {len(anchors)}")
    print()

    for i, anchor in enumerate(anchors, 1):
        print(f"[{i}]")
        print(f"  raw:      {anchor['raw'].hex()}")
        print(f"  relative: {anchor['relative_oid']}")
        print(f"  OID:      {anchor['oid']}")
        print()


def main():
    parser = argparse.ArgumentParser(
        description="Decode TLS trust_anchors extension"
    )
    parser.add_argument(
        "hexfile",
        nargs="?",
        help="file containing a hex dump; stdin if omitted",
    )
    args = parser.parse_args()

    if args.hexfile:
        with open(args.hexfile, "r", encoding="utf-8") as f:
            text = f.read()
    else:
        text = sys.stdin.read()

    try:
        data = parse_hex(text)
        extension_type, anchors = maybe_decode_full_extension(data)
        print_result(extension_type, anchors)
    except (ValueError, OSError) as e:
        print(f"error: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
