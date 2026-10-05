#!/usr/bin/env python3
"""Generate firmware.bin for bios-updater from ASUS's update package.

bios-updater only needs the FTPR, NFTP and MDMV partitions that get sent to the ME,
so this cuts them out of Data.BIN in ASUS's
"H87-PRO-ASUS-2102_and_BIOS-updater.zip" (not in this repo; download it from
the ASUS H87-PRO support page) and writes them as firmware.bin:

    python3 generate_firmware.py /path/to/H87-PRO-ASUS-2102_and_BIOS-updater.zip

The archive and the extracted blob are verified against the SHA-256 and
CRC32 recorded below (h87me.c checks the same CRC32 when it loads the blob)
before anything is written. firmware.bin is committed to this repository, so
you only need this to build the blob yourself from the original source.
"""
import hashlib
import io
import sys
from pathlib import Path
import zipfile
import zlib

OUT = Path(__file__).resolve().parent / "firmware.bin"
ZIP_SHA = "78f4acc4ce0ddbbbd16373cd09a04f51664643ec9544ae745f8baaf9ac83e181"
PAYLOAD_SHA = "fece1dc53fcc9b16a9dd4a86c0ec52a8fef74b09607b9fa21a078a98a8b106a4"
PAYLOAD_CRC = 0xE1C61E4F  # the zlib CRC32 h87me.c checks at startup
# FTPR, NFTP and MDMV: offset and length in Data.BIN.
PARTS = ((0x47000, 0x88000), (0xCF000, 0x77000), (0x146000, 0x37000))


def unique_member(archive, suffix):
    names = [n for n in archive.namelist() if n.endswith(suffix)]
    if len(names) != 1:
        sys.exit(f"expected one archive member ending in {suffix!r}, found {len(names)}")
    return archive.read(names[0])


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    original = Path(sys.argv[1]).read_bytes()
    if hashlib.sha256(original).hexdigest() != ZIP_SHA:
        sys.exit("archive hash differs; expected the ASUS 2102 updater ZIP")
    with zipfile.ZipFile(io.BytesIO(original)) as outer:
        nested = unique_member(outer, "BIOS_updater_for_4th_Gen_Intel_Core_CPU.zip")
    with zipfile.ZipFile(io.BytesIO(nested)) as inner:
        image = unique_member(inner, "Data.BIN")
    payload = b"".join(image[o:o + l] for o, l in PARTS)
    if hashlib.sha256(payload).hexdigest() != PAYLOAD_SHA:
        sys.exit("extracted partition data does not match the recorded payload hash")
    if zlib.crc32(payload) & 0xFFFFFFFF != PAYLOAD_CRC:
        sys.exit("extracted partition data does not match the recorded payload CRC32")
    OUT.write_bytes(payload)
    print(f"wrote {OUT}: {len(payload)} bytes, SHA256 {PAYLOAD_SHA}, CRC32 {PAYLOAD_CRC:08x}")


if __name__ == "__main__":
    main()
