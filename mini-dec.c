/* mini-h2 decoder for experimentation purposes */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hpack-huff.h"

#define DHSIZE 4096
#define STATIC_SIZE 61

#define MAX_INPUT 4096

#define debug_printf(l, f, ...)  do { if (debug_mode >= (l)) printf((f), ##__VA_ARGS__); } while (0)

struct hdr {
	char *n; /* name */
	char *v; /* value */
};

struct dyn {
	int size;   /* allocated size, max allowed for <len> */
	int len;    /* used size, sum of n+v+32 */
	int head;   /* next offset to be used */
	int tail;   /* oldest offset used */
	int entries; /* wrapping position, size/32 */
	struct hdr h[0]; /* the headers themselves */
};

/* dynamic header table. Size is sum of n+v+32 for each entry. */
static struct dyn *dh;

/* static header table. [0] unused. */
static const struct hdr sh[62] = {
	[1] = { .n = ":authority", .v = "" },
	[2] = { .n = ":method", .v = "GET" },
	[3] = { .n = ":method", .v = "POST" },
	[4] = { .n = ":path", .v = "/" },
	[5] = { .n = ":path", .v = "/index.html" },
	[6] = { .n = ":scheme", .v = "http" },
	[7] = { .n = ":scheme", .v = "https" },
	[8] = { .n = ":status", .v = "200" },
	[9] = { .n = ":status", .v = "204" },
	[10] = { .n = ":status", .v = "206" },
	[11] = { .n = ":status", .v = "304" },
	[12] = { .n = ":status", .v = "400" },
	[13] = { .n = ":status", .v = "404" },
	[14] = { .n = ":status", .v = "500" },
	[15] = { .n = "accept-charset", .v = "" },
	[16] = { .n = "accept-encoding", .v = "gzip, deflate" },
	[17] = { .n = "accept-language", .v = "" },
	[18] = { .n = "accept-ranges", .v = "" },
	[19] = { .n = "accept", .v = "" },
	[20] = { .n = "access-control-allow-origin", .v = "" },
	[21] = { .n = "age", .v = "" },
	[22] = { .n = "allow", .v = "" },
	[23] = { .n = "authorization", .v = "" },
	[24] = { .n = "cache-control", .v = "" },
	[25] = { .n = "content-disposition", .v = "" },
	[26] = { .n = "content-encoding", .v = "" },
	[27] = { .n = "content-language", .v = "" },
	[28] = { .n = "content-length", .v = "" },
	[29] = { .n = "content-location", .v = "" },
	[30] = { .n = "content-range", .v = "" },
	[31] = { .n = "content-type", .v = "" },
	[32] = { .n = "cookie", .v = "" },
	[33] = { .n = "date", .v = "" },
	[34] = { .n = "etag", .v = "" },
	[35] = { .n = "expect", .v = "" },
	[36] = { .n = "expires", .v = "" },
	[37] = { .n = "from", .v = "" },
	[38] = { .n = "host", .v = "" },
	[39] = { .n = "if-match", .v = "" },
	[40] = { .n = "if-modified-since", .v = "" },
	[41] = { .n = "if-none-match", .v = "" },
	[42] = { .n = "if-range", .v = "" },
	[43] = { .n = "if-unmodified-since", .v = "" },
	[44] = { .n = "last-modified", .v = "" },
	[45] = { .n = "link", .v = "" },
	[46] = { .n = "location", .v = "" },
	[47] = { .n = "max-forwards", .v = "" },
	[48] = { .n = "proxy-authenticate", .v = "" },
	[49] = { .n = "proxy-authorization", .v = "" },
	[50] = { .n = "range", .v = "" },
	[51] = { .n = "referer", .v = "" },
	[52] = { .n = "refresh", .v = "" },
	[53] = { .n = "retry-after", .v = "" },
	[54] = { .n = "server", .v = "" },
	[55] = { .n = "set-cookie", .v = "" },
	[56] = { .n = "strict-transport-security", .v = "" },
	[57] = { .n = "transfer-encoding", .v = "" },
	[58] = { .n = "user-agent", .v = "" },
	[59] = { .n = "vary", .v = "" },
	[60] = { .n = "via", .v = "" },
	[61] = { .n = "www-authenticate", .v = "" },
};

/* input line: hex chars + \n + \0 */
static char in_hex[MAX_INPUT*2+2];

/* input code */
static char in[MAX_INPUT];
static int in_len;

/* debug mode : 0 = none, 1 = encoding, 2 = code */
static int debug_mode;


/* returns < 0 if error */
int init_dyn(int size)
{
	int entries = size / 32;

	dh = calloc(1, sizeof(*dh) + entries * sizeof(dh->h[0]));
	if (!dh)
		return -1;

	dh->size = size;
	dh->entries = entries;
	debug_printf(2, "allocated %d entries for %d bytes\n", entries, size);
	return 0;
}

static inline int pos_to_idx(const struct dyn *dh, int pos)
{
	return (dh->head + dh->entries - pos) % dh->entries + 1;
}

/* takes an idx, returns current table's position, it's the reverse of pos_to_idx */
static inline int idx_to_pos(const struct dyn *dh, int idx)
{
	return (dh->head + dh->entries - idx + 1) % dh->entries;
}

/* takes an idx, returns the associated name */
static inline const char *idx_to_name(int idx)
{
	if (idx <= STATIC_SIZE)
		return sh[idx].n;

return "[dynamic_name]";
	return dh->h[idx - STATIC_SIZE].n;
}

/* takes an idx, returns the associated value */
static inline const char *idx_to_value(int idx)
{
	if (idx <= STATIC_SIZE)
		return sh[idx].v;
return "[dynamic_value]";
	return dh->h[idx - STATIC_SIZE].v;
}

/* returns 0 */
int add_to_dyn(const char *n, const char *v)
{
	int ln = strlen(n);
	int lv = strlen(v);
	struct hdr *h;

	while (ln + lv + 32 + dh->len > dh->size) {
		h = &dh->h[dh->tail];
		dh->len -= strlen(h->n) + strlen(h->v) + 32;
		debug_printf(2, "====== purging %d : <%s>,<%s> ======\n", pos_to_idx(dh, dh->tail), h->n, h->v);
		free(h->n);
		free(h->v);
		h->v = h->n = NULL;
		dh->tail++;
		if (dh->tail >= dh->entries)
			dh->tail = 0;
	}
	dh->len += ln + lv + 32;

	h = &dh->h[dh->head];
	h->n = strdup(n);
	h->v = strdup(v);
	dh->head++;
	if (dh->head >= dh->entries)
		dh->head = 0;
	return 0;
}

/* returns 0 to 15 for 0..[fF], or < 0 if not hex */
static inline char hextoi(char c)
{
	c -= '0';
	if (c <= 9)
		return c;
	c -= 'A' - '0';
	if (c <= 15 - 10)
		return c + 10;
	c -= 'a' - 'A';
	if (c <= 15 - 10)
		return c + 10;
	return -1;
}

/* reads one line into <in_hex> and convert it to {in,in_len}. Returns the
 * number of bytes read. Trims the first LF found. Uses <orig> instead of
 * stdin if not NULL. Very limited bounds checking, don't use as-is.
 */
//int read_input_line(char *orig)
//{
//	char *i, *o;
//	char v1, v2;
//
//	if (orig)
//		strncpy(in_hex, orig, sizeof(in_hex));
//	else if (!fgets(in_hex, sizeof(in_hex), stdin))
//			return -1;
//
//	i = in_hex; o = in;
//	in_len = 0;
//	while (in_len < sizeof(in) &&
//	       (v1 = hextoi(i[0])) >= 0 && (v2 = hextoi(i[1])) >= 0) {
//		i += 2;
//		o[in_len++] = (v1 << 4) + v2;
//	}
//	if (*i == '\n')
//		*i = 0;
//	else if (*i && i[1] == '\n')
//		i[1] = 0;
//	return in_len;
//}

/* reads one line into <in_hex> */
int read_input_line()
{
	if (!fgets(in_hex, sizeof(in_hex), stdin))
		return -1;
	return 0;
}

/* converts in_hex to {in,in_len}. Returns the number of bytes read. Trims the
 * first LF found.
 */
int decode_input_line()
{
	char *i, *o;
	char v1, v2;

	i = in_hex; o = in;
	in_len = 0;
	while (in_len < sizeof(in) &&
	       (v1 = hextoi(i[0])) >= 0 && (v2 = hextoi(i[1])) >= 0) {
		i += 2;
		o[in_len++] = (v1 << 4) + v2;
	}
	if (*i == '\n')
		*i = 0;
	else if (*i && i[1] == '\n')
		i[1] = 0;
	return in_len;
}


/* looks up <n:v> in the static table. Returns an index in the
 * static table in <ni> or 0 if none was found. Returns the same
 * index in <vi> if the value is the same, or 0 if the value
 * differs (and has to be sent as a literal). Returns non-zero
 * if an entry was found.
 */
int lookup_sh(const char *n, const char *v, int *ni, int *vi)
{
	int i;
	int b = 0;

	for (i = 1; i < sizeof(sh)/sizeof(sh[0]); i++) {
		if (strcasecmp(n, sh[i].n) == 0) {
			if (strcasecmp(v, sh[i].v) == 0) {
				*ni = *vi = i;
				return 1;
			}
			if (!b)
				b = i;
		}
	}
	if (!b)
		return 0;
	*ni = b;
	*vi = 0;
	return 1;
}

/* looks up <n:v> in the dynamic table. Returns an index in the
 * dynamic table in <ni> or 0 if none was found. Returns the same
 * index in <vi> if the value is the same, or 0 if the value
 * differs (and has to be sent as a literal). Returns non-zero
 * if an entry was found.
 */
int lookup_dh(const char *n, const char *v, int *ni, int *vi)
{
	int i;
	int b = 0;

	i = dh->head;
	while (i != dh->tail) {
		i--;
		if (i < 0)
			i = dh->entries - 1;

		if (strcasecmp(n, dh->h[i].n) == 0) {
			if (strcasecmp(v, dh->h[i].v) == 0) {
				i = pos_to_idx(dh, i);
				*ni = *vi = i;
				return 1;
			}
			if (!b)
				b = i;
		}
	}
	if (!b)
		return 0;

	b = pos_to_idx(dh, b);
	*ni = b;
	*vi = 0;
	return 1;
}

/* reads a varint from <raw>'s lowest <b> bits and <len> bytes max (raw included).
 * returns the 32-bit value on success after updating raw_in and len_in. Forces
 * len_in to -1 on truncated input.
 */
uint32_t get_var_int(const uint8_t **raw_in, int *len_in, int b)
{
	uint32_t ret = 0;
	int len = *len_in;
	const uint8_t *raw = *raw_in;
	uint8_t shift = 0;

	len--;
	ret = *(raw++) & ((1 << b) - 1);
	if (ret != ((1 << b) - 1))
		goto end;

	while (1) {
		if (!len)
			goto too_short;
		if (!(*raw & 128))
			break;
		ret += ((uint32_t)(*raw++) & 127) << shift;
		shift += 7;
		len--;
	}

	/* last 7 bits */
	if (!len)
		goto too_short;
	len--;
	ret += ((uint32_t)(*raw++) & 127) << shift;

 end:
	*raw_in = raw;
	*len_in = len;
	return ret;

 too_short:
	*len_in = -1;
	return 0;
}

/* only takes care of frames affecting the dynamic table for now. Returns 0 on
 * success or < 0 on error.
 */
int decode_frame(const uint8_t *raw, int len)
{
	uint32_t idx;
	uint32_t nlen;
	uint32_t vlen;
	uint8_t huff;
	const char *name;
	const char *value;
	int c;
	static char ntrash[16384];
	static char vtrash[16384];

	while (len) {
		c = *raw;
		if (*raw >= 0x81) {
			/* indexed header field */
			idx = get_var_int(&raw, &len, 7);
			if (len < 0) // truncated
				return -1;
			name = value = NULL;
			printf("%02x: p14: indexed header field\n  %s: %s\n", c, idx_to_name(idx), idx_to_value(idx)); 
		}
		else if (*raw >= 0x41 && *raw <= 0x7f) {
			/* literal header field with incremental indexing -- indexed name */
			idx = get_var_int(&raw, &len, 6);
			if (len < 0) // truncated
				return -2;
			name = idx_to_name(idx);
			nlen = strlen(name);

			if (!len) // truncated
				return -3;
			huff = *raw & 0x80;
			vlen = get_var_int(&raw, &len, 7);
			if (len < 0) // truncated
				return -4;
			if (len < vlen) // truncated
				return -5;
			value = (char *)raw;
			raw += vlen;
			len -= vlen;

			if (huff) {
				vlen = huff_dec(value, vlen, vtrash, sizeof(vtrash));
				if (vlen < 0)
					fprintf(stderr, "can't decode huffman.\n");
				else
					value = vtrash;
			}
			printf("%02x: p15: literal with indexing -- name\n  %s: %s\n", c, idx_to_name(idx), value); 
		}
		else if (*raw == 0x40) {
			/* literal header field with incremental indexing -- literal name */
			raw++; len--;

			/* name */
			if (!len) // truncated
				return -6;
			huff = *raw & 0x80;
			nlen = get_var_int(&raw, &len, 7);
			if (len < 0) // truncated
				return -7;
			if (len < nlen) // truncated
				return -8;
			name = (char *)raw;
			raw += nlen;
			len -= nlen;

			if (huff) {
				nlen = huff_dec(name, nlen, ntrash, sizeof(ntrash));
				if (vlen < 0)
					fprintf(stderr, "can't decode huffman.\n");
				else
					name = ntrash;
			}

			/* value */
			if (!len) // truncated
				return -9;
			huff = *raw & 0x80;
			vlen = get_var_int(&raw, &len, 7);
			if (len < 0) // truncated
				return -10;
			if (len < vlen) // truncated
				return -11;
			value = (char *)raw;
			raw += vlen;
			len -= vlen;

			if (huff) {
				vlen = huff_dec(value, vlen, vtrash, sizeof(vtrash));
				if (vlen < 0)
					fprintf(stderr, "can't decode huffman.\n");
				else
					value = vtrash;
			}

			printf("%02x: p16: literal with indexing\n  %s: %s\n", c, name, value); 
		}
		else if (*raw >= 0x01 && *raw <= 0x0f) {
			/* literal header field without indexing -- indexed name */
			idx = get_var_int(&raw, &len, 4);
			if (len < 0) // truncated
				return -12;
			name = idx_to_name(idx);
			nlen = strlen(name);

			if (!len) // truncated
				return -13;
			huff = *raw & 0x80;
			vlen = get_var_int(&raw, &len, 7);
			if (len < 0) // truncated
				return -14;
			if (len < vlen) // truncated
				return -15;
			value = (char *)raw;
			raw += vlen;
			len -= vlen;

			if (huff) {
				vlen = huff_dec(value, vlen, vtrash, sizeof(vtrash));
				if (vlen < 0)
					fprintf(stderr, "can't decode huffman.\n");
				else
					value = vtrash;
			}

			printf("%02x: p16: literal without indexing -- name\n  %s: %s\n", c, idx_to_name(idx), value); 
		}
		else if (*raw == 0x00) {
			/* literal header field without indexing -- literal name */
			raw++; len--;

			/* name */
			if (!len) // truncated
				return -16;
			huff = *raw & 0x80;
			nlen = get_var_int(&raw, &len, 7);
			if (len < 0) // truncated
				return -17;
			if (len < nlen) // truncated
				return -18;
			name = (char *)raw;
			raw += nlen;
			len -= nlen;

			if (huff) {
				nlen = huff_dec(name, nlen, ntrash, sizeof(ntrash));
				if (vlen < 0)
					fprintf(stderr, "can't decode huffman.\n");
				else
					name = ntrash;
			}

			/* value */
			if (!len) // truncated
				return -19;
			huff = *raw & 0x80;
			vlen = get_var_int(&raw, &len, 7);
			if (len < 0) // truncated
				return -20;
			if (len < vlen) // truncated
				return -21;
			value = (char *)raw;
			raw += vlen;
			len -= vlen;

			if (huff) {
				vlen = huff_dec(value, vlen, vtrash, sizeof(vtrash));
				if (vlen < 0)
					fprintf(stderr, "can't decode huffman.\n");
				else
					value = vtrash;
			}

			printf("%02x: p17: literal without indexing\n  %s: %s\n", c, name, value); 
		}
		else if (*raw >= 0x11 && *raw <= 0x1f) {
			/* literal header field never indexed -- indexed name */
			idx = get_var_int(&raw, &len, 4);
			if (len < 0) // truncated
				return -22;
			name = idx_to_name(idx);
			nlen = strlen(name);

			if (!len) // truncated
				return -23;
			huff = *raw & 0x80;
			vlen = get_var_int(&raw, &len, 7);
			if (len < 0) // truncated
				return -24;
			if (len < vlen) // truncated
				return -25;
			value = (char *)raw;
			raw += vlen;
			len -= vlen;
			printf("%02x: p17: literal never indexed -- name\n  %s: %s\n", c, idx_to_name(idx), value); 
		}
		else if (*raw == 0x10) {
			/* literal header field never indexed -- literal name */
			raw++; len--;

			/* name */
			if (!len) // truncated
				return -26;
			huff = *raw & 0x80;
			nlen = get_var_int(&raw, &len, 7);
			if (len < 0) // truncated
				return -27;
			if (len < nlen) // truncated
				return -28;
			name = (char *)raw;
			raw += nlen;
			len -= nlen;

			if (huff) {
				nlen = huff_dec(name, nlen, ntrash, sizeof(ntrash));
				if (vlen < 0)
					fprintf(stderr, "can't decode huffman.\n");
				else
					name = ntrash;
			}

			/* value */
			if (!len) // truncated
				return -29;
			huff = *raw & 0x80;
			vlen = get_var_int(&raw, &len, 7);
			if (len < 0) // truncated
				return -30;
			if (len < vlen) // truncated
				return -31;
			value = (char *)raw;
			raw += vlen;
			len -= vlen;

			if (huff) {
				vlen = huff_dec(value, vlen, vtrash, sizeof(vtrash));
				if (vlen < 0)
					fprintf(stderr, "can't decode huffman.\n");
				else
					value = vtrash;
			}

			printf("%02x: p18: literal never indexed\n  %s: %s\n", c, name, value); 
		}
		else if (*raw >= 0x20 && *raw <= 0x3f) {
			/* max dyn table size change */
			idx = get_var_int(&raw, &len, 5);
			if (len < 0) // truncated
				return -32;
		}
		else {
			fprintf(stderr, "unhandled code 0x%02x (raw=%p, len=%d)\n", *raw, raw, len);
			return -33;
		}
	}
	return 0;
}

int main(int argc, char **argv)
{
	int ret;

	while (argc > 1) {
		if (strcmp(argv[1], "-d") == 0)
			debug_mode++;
		else if (strcmp(argv[1], "-dd") == 0)
			debug_mode += 2;
		else
			break;
		argv++;
		argc--;
	}

	if (init_dyn(DHSIZE) < 0)
		exit(1);

	if (argc > 1)
		strncpy(in_hex, argv[1], sizeof(in_hex));

	while ((argc > 1 || read_input_line() >= 0) && decode_input_line() >= 0) {
		debug_printf(1, "\nin_hex=<%s> in_len=<%d>\n", in_hex, in_len);
		ret = decode_frame(in, in_len);
		if (ret < 0) {
			printf("decoding error, stopping (%d)\n", ret);
			exit(1);
		}
		if (argc) // process only cmd line if provided
			break;
	}
	return 0;
}
