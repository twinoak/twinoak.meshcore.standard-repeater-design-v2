"""A synthetic stand-in for the RAK4630 bootloader + S140 hex, for the host test.

MBR at 0x0, a SoftDevice-sized block at 0x1000 with an S140 6.1.1 info struct, a bootloader at 0xF4000
ending in a half word (padding), a block that straddles 1 KB boundaries, blank words sprinkled everywhere (never written), and the UICR words
the real hex carries (NRFFW[0], NRFFW[1]).

    python3 make_test_hex.py <out.hex>
"""
import random
import sys


def records(base, data):
    out, upper = [], None
    for off in range(0, len(data), 16):
        a = base + off
        if a >> 16 != upper:
            upper = a >> 16
            out.append(rec(0, 4, bytes([upper >> 8, upper & 0xFF])))
        out.append(rec(a & 0xFFFF, 0, data[off:off + 16]))
    return out


def rec(addr, typ, data):
    raw = bytes([len(data), addr >> 8, addr & 0xFF, typ]) + data
    return ":" + (raw + bytes([(-sum(raw)) & 0xFF])).hex().upper()


def block(size, rng):
    words = []
    for i in range(size // 4):
        words.append(0xFFFFFFFF if i % 37 == 5 else rng.getrandbits(32))
    return bytearray(b"".join(w.to_bytes(4, "little") for w in words))


def main():
    rng = random.Random(4630)
    mbr = block(0x1000, rng)
    sd = block(0x25000, rng)
    for addr, val in ((0x3004, 0x51B1E5DB), (0x300C, 0x00B6), (0x3010, 140), (0x3014, 6001001)):
        sd[addr - 0x1000:addr - 0x1000 + 4] = val.to_bytes(4, "little")
    boot = block(0x8000, rng)[:-2]  # ends mid-word
    uicr = (0x000F4000).to_bytes(4, "little") + (0x000FE000).to_bytes(4, "little")
    lines = records(0, bytes(mbr)) + records(0x1000, bytes(sd)) + records(0xF4000, bytes(boot))
    lines += records(0x000FE3F0, bytes(block(0x420, rng)))  # not 1 KB aligned, spans two 1 KB boundaries
    lines += records(0x10001014, uicr)
    lines.append(":00000001FF")
    with open(sys.argv[1], "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
