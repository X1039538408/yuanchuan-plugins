/* Minimal CRC-32 entry points for the zlib inflate subset. */
#include "zlib.h"

uLong ZEXPORT crc32(uLong crc, const Bytef *buf, uInt len) {
    if (buf == Z_NULL) return 0L;
    crc ^= 0xffffffffUL;
    while (len--) {
        crc ^= *buf++;
        for (unsigned int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320UL & (0U - (crc & 1U)));
    }
    return crc ^ 0xffffffffUL;
}

uLong ZEXPORT crc32_z(uLong crc, const Bytef *buf, z_size_t len) {
    while (len > 0) {
        const uInt chunk = len > 0xffffffffU ? 0xffffffffU : (uInt)len;
        crc = crc32(crc, buf, chunk);
        if (buf != Z_NULL) buf += chunk;
        len -= chunk;
    }
    return crc;
}
