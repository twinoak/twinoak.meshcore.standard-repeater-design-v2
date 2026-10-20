# images/

Put the RAK4630 **bootloader + SoftDevice S140 6.1.1** Intel HEX here (one combined file, or the
bootloader and the SoftDevice as two files; they are merged). Every `.hex` in this folder is built in.

Where it comes from: RAKwireless' WisBlock repository, `bootloader/RAK4630/` (the combined
`..._s140_6.1.1.hex`; a bootloader-only hex is refused by the build, see `../tools/hex2image.py`).

The build prints what it took: file name and SHA-256, segments, CRC-32, the bootloader address and the
SoftDevice version. The same lines appear on the Walter's console (`image`). Committing the hex pins the exact
image every radio got; it is Adafruit's MIT bootloader and Nordic's redistributable SoftDevice.
