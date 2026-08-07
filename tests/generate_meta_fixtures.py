#!/usr/bin/env python3
"""Generate deterministic FLAC metadata fixtures under tests/reference/meta/.

Phase 3A slice 1 fixtures for the libmusicpack tag readers (musicpack_vorbis_*
and musicpack_flac_read_metadata). Files are metadata-focused and small; they
are NOT guaranteed to be decodable as audio (the block walker never touches
audio frames). No copyrighted material; images are generated locally.

Files produced:
  album-vorbis.flac   rich Vorbis comments + embedded front PNG + back JPEG
  notags.flac         FLAC with STREAMINFO + PADDING only
  bad-magic.flac      does not start with the fLaC marker
  truncated.flac      album-vorbis.flac truncated inside the comment block
  oversized.flac      metadata block declares a length past EOF
  vorbis-comment.bin  raw Vorbis Comment structure (for musicpack_vorbis_read)

Usage: python3 tests/generate_meta_fixtures.py [out-dir]
"""

import os
import struct
import sys
import zlib


TINY_JPEG = bytes.fromhex(
    "FFD8FFE000104A46494600010101006000600000FFDB004300080606070605080707"
    "070909080A0C140D0C0B0B0C1912130F141D1A1F1E1D1A1C1C20242E2720222C231C"
    "1C2837292C30313434341F27393D38323C2E333432FFC0000B080001000101011100"
    "FFC4001F0000010501010101010100000000000000000102030405060708090A0BFF"
    "C400B5100002010303020403050504040000017D0102030004110512213141061351"
    "61220714328191A1082342B1C11552D1F02433627282090A161718191A2526272829"
    "2A3435363738393A434445464748494A535455565758595A636465666768696A7374"
    "75767778797A838485868788898A92939495969798999AA2A3A4A5A6A7A8A9AAB2B3"
    "B4B5B6B7B8B9BAC2C3C4C5C6C7C8C9CAD2D3D4D5D6D7D8D9DAE1E2E3E4E5E6E7E8"
    "E9EAF1F2F3F4F5F6F7F8F9FAFFC4001F010003010101010101010101000000000000"
    "010002030405060708090A0BFFC400B5110002010302040403050404040601020301"
    "11002131410551612271328106144291A1082342B1C11552D1F02433627282090A16"
    "1718191A25262728292A3435363738393A434445464748494A535455565758595A63"
    "6465666768696A737475767778797A82838485868788898A92939495969798999AA2"
    "A3A4A5A6A7A8A9AAB2B3B4B5B6B7B8B9BAC2C3C4C5C6C7C8C9CAD2D3D4D5D6D7D8"
    "D9DAE2E3E4E5E6E7E8E9EAF2F3F4F5F6F7F8F9FAFFDA000C03010002110311003F00")


def make_png(rgb, size=8):
    raw = b"".join(b"\x00" + bytes(rgb) * size for _ in range(size))

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
            + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def vorbis_comment(vendor, tags):
    out = struct.pack("<I", len(vendor.encode("utf-8"))) + vendor.encode("utf-8")
    out += struct.pack("<I", len(tags))
    for t in tags:
        b = t.encode("utf-8")
        out += struct.pack("<I", len(b)) + b
    return out


def streaminfo():
    sr, ch, bps, total = 44100, 2, 16, 44100
    out = struct.pack(">HH", 4096, 4096)          # min/max block size
    out += struct.pack(">I", 0)[1:]               # min frame size (24 bits)
    out += struct.pack(">I", 0)[1:]               # max frame size (24 bits)
    out += bytes([(sr >> 12) & 0xFF,
                  (sr >> 4) & 0xFF,
                  ((sr & 0x0F) << 4) | ((ch - 1) << 1) | ((bps - 1) >> 4),
                  (((bps - 1) & 0x0F) << 4) | ((total >> 32) & 0x0F)])
    out += struct.pack(">I", total & 0xFFFFFFFF)  # total samples
    return out + b"\x00" * 16                     # MD5 placeholder


def block(block_type, payload, last=False):
    hdr = (block_type & 0x7F) | (0x80 if last else 0x00)
    return bytes([hdr]) + len(payload).to_bytes(3, "big") + payload


def picture_block(ptype, mime, desc, w, h, depth, colors, data):
    mb = mime.encode("utf-8")
    db = desc.encode("utf-8")
    return (struct.pack(">I", ptype)
            + struct.pack(">I", len(mb)) + mb
            + struct.pack(">I", len(db)) + db
            + struct.pack(">IIII", w, h, depth, colors)
            + struct.pack(">I", len(data)) + data)


def build_flac(blocks, audio=b"\xff\xf8\x69\x00\x00\x00\x00\x00"):
    out = b"fLaC"
    for i, (btype, payload) in enumerate(blocks):
        out += block(btype, payload, last=(i == len(blocks) - 1))
    return out + audio


def ape_tag(items):
    """Build an APEv2 tag (header + items + footer), v2.000, matching the
    mutagen/mpcenc byte conventions: footer flag 0x80000000 ("has header"),
    header flag 0xC0000000, binary item flag 0x2, and a size field that
    counts items + footer only (the 32-byte header is not included)."""
    body = b""
    for key, value, flags in items:
        body += struct.pack("<2I", len(value), flags) + key.encode("utf-8")
        body += b"\x00" + value
    tag_size = 32 + len(body)  # items + footer; header excluded
    count = len(items)
    header = (b"APETAGEX" + struct.pack("<3I", 2000, tag_size, count)
              + struct.pack("<I", 0xC0000000) + b"\x00" * 8)
    footer = (b"APETAGEX" + struct.pack("<3I", 2000, tag_size, count)
              + struct.pack("<I", 0x80000000) + b"\x00" * 8)
    return header + body + footer


APE_PAYLOAD = b"AUDIOBYTES" * 10


def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "reference", "meta")
    os.makedirs(outdir, exist_ok=True)

    rich_tags = [
        "TITLE=Big in Japan",
        "ARTIST=Alphaville",
        "ARTIST=The Van",
        "ALBUM=Synthetic Test Album",
        "ALBUMARTIST=Alphaville",
        "TRACKNUMBER=3/12",
        "DISCNUMBER=1/1",
        "DATE=2016-09-23",
        "ORIGINALDATE=1984-06-01",
        "GENRE=Synthpop",
        "GENRE=New Wave",
        "PUBLISHER=Example Records",
        "CATALOGNUMBER=ERCD 001",
        "BARCODE=198704979941",
        "ISRC=GBK3W2503556",
        "MUSICBRAINZ_ALBUMID=11111111-2222-3333-4444-555555555555",
        "MUSICBRAINZ_RELEASEGROUPID=aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee",
        "MUSICBRAINZ_TRACKID=cccccccc-dddd-eeee-ffff-111111111111",
        "MUSICBRAINZ_RECORDINGID=12121212-3434-5656-7878-909090909090",
        "MUSICBRAINZ_RELEASETRACKID=23232323-4545-6767-8989-abababababab",
        "SOURCE=Deezer",
        "SOURCEID=3810015612",
        "COMPOSER=Example Composer",
        "PERFORMER=Example Performer",
        "AUTHOR=Example Author",
        "LYRICS=Line one\nLine two",
        "COPYRIGHT=2026 Example Records",
        "LENGTH=241.5",
        "BPM=118",
        "CUSTOM_X=survives",
    ]

    png = make_png((30, 60, 120))
    jpeg = TINY_JPEG
    comments = vorbis_comment("MusicPack test", rich_tags)

    album = build_flac([
        (0, streaminfo()),
        (4, comments),
        (6, picture_block(3, "image/png", "Front cover", 8, 8, 24, 0, png)),
        (6, picture_block(4, "image/jpeg", "Back cover", 1, 1, 24, 0, jpeg)),
    ])
    with open(os.path.join(outdir, "album-vorbis.flac"), "wb") as f:
        f.write(album)

    notags = build_flac([
        (0, streaminfo()),
        (1, b"\x00\x00\x00"),  # small PADDING block
    ])
    with open(os.path.join(outdir, "notags.flac"), "wb") as f:
        f.write(notags)

    with open(os.path.join(outdir, "bad-magic.flac"), "wb") as f:
        f.write(b"NOTFLAC" + album)

    with open(os.path.join(outdir, "truncated.flac"), "wb") as f:
        f.write(album[: len(album) // 2])

    # block header claims a 1 MiB comment block but the file ends immediately
    oversized = b"fLaC" + block(4, b"")[:1] + (1 << 20).to_bytes(3, "big")
    with open(os.path.join(outdir, "oversized.flac"), "wb") as f:
        f.write(oversized)

    with open(os.path.join(outdir, "vorbis-comment.bin"), "wb") as f:
        f.write(comments)

    # --- APEv2 fixtures ------------------------------------------------
    ape_items = [
        ("Title", b"Big in Japan", 0),
        ("Artist", b"Alphaville\x00The Van", 0),   # NUL-joined multi-value
        ("Album", b"Synthetic Test Album", 0),
        ("Track", b"3/12", 0),
        ("MusicBrainz Album Id", b"11111111-2222-3333-4444-555555555555", 0),
        ("Source", b"Deezer", 0),
        ("Cover Art (Front)", b"cover.jpg\x00" + png, 2),  # binary item
        ("CUSTOM", b"survives", 0),
    ]
    ape_full = ape_tag(ape_items)
    with open(os.path.join(outdir, "album-ape.mpc"), "wb") as f:
        f.write(APE_PAYLOAD + ape_full)
    with open(os.path.join(outdir, "ape-no-tag.mpc"), "wb") as f:
        f.write(APE_PAYLOAD)
    # truncated: payload + header + first item cut mid-value + footer, so the
    # footer's tag_size no longer matches the file (must be rejected cleanly)
    with open(os.path.join(outdir, "ape-truncated.mpc"), "wb") as f:
        f.write(APE_PAYLOAD + ape_full[:52] + ape_full[-32:])

    print("generated %s" % outdir)


if __name__ == "__main__":
    main()
