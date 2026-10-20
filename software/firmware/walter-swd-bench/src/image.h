// The image the bench tool writes to a new RAK: the RAK4630 bootloader + SoftDevice S140 (+ its UICR
// words), built in at compile time by tools/hex2image.py from images/*.hex.
#pragma once
#include <stdint.h>

struct ImageSegment {
  uint32_t addr;          // word aligned; flash (< 0x00100000) or UICR (0x10001000..0x10001FFF)
  uint32_t words;
  const uint32_t* data;   // 0xFFFFFFFF words are left erased, not written
};

struct Image {
  const ImageSegment* seg;
  uint32_t nseg;          // 0 = no image built in
  uint32_t dataWords;     // words that are not 0xFFFFFFFF (= words written)
  uint32_t crc32;         // zlib CRC-32 over the segments' bytes, in address order
  uint32_t bootloaderAddr;  // the image's UICR.NRFFW[0]
  uint32_t sdId;          // SoftDevice info struct: 140 for S140, 0 if none found
  uint32_t sdVersion;     // e.g. 6001001 for 6.1.1, 0 if none found
  const char* source;     // file name(s) and SHA-256, for the log
};

extern const Image kBuiltinImage;
