#ifndef _HPACK_HUFF_H
#define _HPACK_HUFF_H

#include <stdint.h>

int huff_enc(const char *s, char *out);
int encode_string(const char *s);
int huff_dec(const uint8_t *huff, int hlen, char *out, int olen);

#endif
