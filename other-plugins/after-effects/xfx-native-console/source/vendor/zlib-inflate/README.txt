The source files in this directory are based on the zlib compression library, version 1.3.1
available from https://www.zlib.net/ .

The files other than crc32.c are a subset of the complete zlib library and have not been changed
except for the addition of a comment at the top of each file, noting its association with the
zlib license and the version number. crc32.c is a small compatible implementation added because
the inflate subset needs the public CRC-32 entry points.

The applicable license is included as zlib-LICENSE.txt in this directory and also appears at
the top of zlib.h.
