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

struct str {
	char  *ptr;
	size_t len;
};

struct hdr {
	struct str n; /* name */
	struct str v; /* value */
};

struct dyn {
	int size;   /* allocated size, max allowed for <len> */
	int len;    /* used size, sum of n+v+32 */
	int head;   /* next offset to be used */
	int tail;   /* oldest offset used */
	int entries; /* wrapping position, size/32 */
	struct hdr h[0]; /* the headers themselves */
};

/* dynamic table entry, usable for tables up to 4GB long and values of 64kB-1.
 * The model can be improved by using offsets relative to the table entry's end
 * or to the end of the area, or by moving the descriptors at the end of the
 * table and the data at the beginning. This entry is 16 bytes long, which is
 * half the bookkeeping planned by the HPACK spec. Thus it saves 16 bytes per
 * header, meaning that even with a single header, 16 extra bytes can be stored
 * (ie one such descriptor).
 *
 * Principle: the table is stored in a contiguous array containing both the
 * descriptors and the contents. Descriptors are stored at the beginning of the
 * array while contents are stored starting from the end. Most of the time there
 * is enough room left in the table to insert a new header, thanks to the
 * savings on the descriptor size. Thus by inserting headers from the end it's
 * possible to maximize the delay before a collision of DTEs and data. In order
 * to always insert from the right, we need to keep a reference to the latest
 * inserted element and look before it. Each cell has a preamble (possibly empty)
 * and a trailer (possibly empty). The last inserted cell's predecessor's trailer
 * contains the available space before this cell. When this cell is alone, the
 * predecessor must be the "root", a dummy descriptor describing the available
 * space and serving as a list head. Its trailer is the free space after the
 * descriptors, and it preamble is the free space before the descriptors (hence
 * at the end of the area) if different. Thus most often during insertions, the
 * root's trailer is reduced and the root becomes the previous entry of the last
 * inserted cell.
 *
 * When there's no more room, and cells are deleted, this creates free space at
 * the end of the area which is assigned to the last remaining cell's trailer.
 * if this room is enough to store the new header, it is used, and this newly
 * opened hole can remain contiguous as previous headers are deleted.
 *
 * When no more room remains available there, the following algorithm should
 * apply :
 *   1) look for room on last->prev->trailer (current work area)
 *   2) look for room on root->trailer (beginning of area)
 *   3) look for room on root->prev->trailer (end of area)
 *   4) depending on usage statistics (eg: few headers, lots of data), it might
 *      be worth scanning the table
 *   5) otherwise, the table has to be defragmented. For this it is
 *      reconstructed with all used elements packed at the end
 *
 * The defragmentation should be rare ; a study on live data shows on average
 * 29.2 bytes used per header field. This plus the 32 bytes overhead fix an
 * average of 66.9 header fields per 4kB table. This brings a 1070 bytes saving
 * using the current storage description, ensuring that oldest headers are
 * linearly removed by the sender before fragmentation occurs. This means that
 * for all smaller header fields there will not be any requirement to defragment
 * the area and most of the time it will even be possible to copy the old values
 * directly within the buffer after creating a new entry. On average within the
 * available space there will be enough room to store 1070/(29.2+16)=23 extra
 * headers without switching to another place.
 *
 * In order to limit the risk of collision between data and descriptors, it
 * could be worth switching steps 1 and 2 above, at the expense of causing
 * more fragmentation. A better option could consist in keep the algorithm
 * above except if writing a header results in less than 2-3 new headers being
 * available in the descriptor table and the other option is possible as well.
 *
 * The root node appears at index 0 and stores a few elements used by the area :
 *   - addr: starting point in bytes = #allocated entries * sizeof(dte)
 *   - nlen: number of used entries
 *   - vlen: next insertion index
 *   - tlen: the real tlen, free space covers area from addr to addr+tlen
 *   - plen: sum of all nlen+vlen
 *   - prev,next: linking to last and first cells indexes.
 *
 * A typical table with 9 entries allocated and 7 used will look like this :
 *
 *  0 1 2 3 4 5 6 7 8 9
 * +-+-+-+-+-+-+-+-+-+-+
 * |R|4|3|2|1| | |7|6|5|
 * +-+-+-+-+-+-+-+-+-+-+
 *    <------ ^
 *      index
 *
 * A more compact approach is possible with 8 bytes per descriptor, using only
 * 16 bit offsets, with only addr, tlen, prev, next, but requiring to store the
 * \0 after both the name and the value in each cell. This would provide a 22
 * bytes saving per header, or about 36% of the area on average.
 */


/* One dynamic table entry descriptor */
struct dte {
	uint32_t addr;  /* storage address, relative to the dte address */
	uint16_t nlen;  /* header name length */
	uint16_t vlen;  /* header value length */
};

/* Note: the table's head plus a struct dte must be smaller than or equal to 32
 * bytes so that a single large header can always fit. Here that's 16 bytes for
 * the header, plus 8 bytes per slot.
 * Note that when <used> == 0, front, head, and wrap are undefined.
 */
struct dht {
	uint32_t size;  /* allocated table size in bytes */
	uint32_t total; /* sum of nlen + vlen in bytes */
	uint16_t front; /* slot number of the first node after the idx table */
	uint16_t wrap;  /* number of allocated slots, wraps here */
	uint16_t head;  /* last inserted slot number */
	uint16_t used;  /* number of slots in use */
	struct dte dte[0]; /* dynamic table entries */
};

/* dynamic header table. Size is sum of n+v+32 for each entry. */
static struct dht *dht;

/* static header table. [0] unused. */
static const struct hdr sh[62] = {
	[ 1] = { .n = { ":authority",                  10 }, .v = { "",               0 } },
	[ 2] = { .n = { ":method",                      7 }, .v = { "GET",            3 } },
	[ 3] = { .n = { ":method",                      7 }, .v = { "POST",           4 } },
	[ 4] = { .n = { ":path",                        5 }, .v = { "/",              1 } },
	[ 5] = { .n = { ":path",                        5 }, .v = { "/index.html",   11 } },
	[ 6] = { .n = { ":scheme",                      7 }, .v = { "http",           4 } },
	[ 7] = { .n = { ":scheme",                      7 }, .v = { "https",          5 } },
	[ 8] = { .n = { ":status",                      7 }, .v = { "200",            3 } },
	[ 9] = { .n = { ":status",                      7 }, .v = { "204",            3 } },
	[10] = { .n = { ":status",                      7 }, .v = { "206",            3 } },
	[11] = { .n = { ":status",                      7 }, .v = { "304",            3 } },
	[12] = { .n = { ":status",                      7 }, .v = { "400",            3 } },
	[13] = { .n = { ":status",                      7 }, .v = { "404",            3 } },
	[14] = { .n = { ":status",                      7 }, .v = { "500",            3 } },
	[15] = { .n = { "accept-charset",              14 }, .v = { "",               0 } },
	[16] = { .n = { "accept-encoding",             15 }, .v = { "gzip, deflate", 13 } },
	[17] = { .n = { "accept-language",             15 }, .v = { "",               0 } },
	[18] = { .n = { "accept-ranges",               13 }, .v = { "",               0 } },
	[19] = { .n = { "accept",                       6 }, .v = { "",               0 } },
	[20] = { .n = { "access-control-allow-origin", 27 }, .v = { "",               0 } },
	[21] = { .n = { "age",                          3 }, .v = { "",               0 } },
	[22] = { .n = { "allow",                        5 }, .v = { "",               0 } },
	[23] = { .n = { "authorization",               13 }, .v = { "",               0 } },
	[24] = { .n = { "cache-control",               13 }, .v = { "",               0 } },
	[25] = { .n = { "content-disposition",         19 }, .v = { "",               0 } },
	[26] = { .n = { "content-encoding",            16 }, .v = { "",               0 } },
	[27] = { .n = { "content-language",            16 }, .v = { "",               0 } },
	[28] = { .n = { "content-length",              14 }, .v = { "",               0 } },
	[29] = { .n = { "content-location",            16 }, .v = { "",               0 } },
	[30] = { .n = { "content-range",               13 }, .v = { "",               0 } },
	[31] = { .n = { "content-type",                12 }, .v = { "",               0 } },
	[32] = { .n = { "cookie",                       6 }, .v = { "",               0 } },
	[33] = { .n = { "date",                         4 }, .v = { "",               0 } },
	[34] = { .n = { "etag",                         4 }, .v = { "",               0 } },
	[35] = { .n = { "expect",                       6 }, .v = { "",               0 } },
	[36] = { .n = { "expires",                      7 }, .v = { "",               0 } },
	[37] = { .n = { "from",                         4 }, .v = { "",               0 } },
	[38] = { .n = { "host",                         4 }, .v = { "",               0 } },
	[39] = { .n = { "if-match",                     8 }, .v = { "",               0 } },
	[40] = { .n = { "if-modified-since",           17 }, .v = { "",               0 } },
	[41] = { .n = { "if-none-match",               13 }, .v = { "",               0 } },
	[42] = { .n = { "if-range",                     8 }, .v = { "",               0 } },
	[43] = { .n = { "if-unmodified-since",         19 }, .v = { "",               0 } },
	[44] = { .n = { "last-modified",               13 }, .v = { "",               0 } },
	[45] = { .n = { "link",                         4 }, .v = { "",               0 } },
	[46] = { .n = { "location",                     8 }, .v = { "",               0 } },
	[47] = { .n = { "max-forwards",                12 }, .v = { "",               0 } },
	[48] = { .n = { "proxy-authenticate",          18 }, .v = { "",               0 } },
	[49] = { .n = { "proxy-authorization",         19 }, .v = { "",               0 } },
	[50] = { .n = { "range",                        5 }, .v = { "",               0 } },
	[51] = { .n = { "referer",                      7 }, .v = { "",               0 } },
	[52] = { .n = { "refresh",                      7 }, .v = { "",               0 } },
	[53] = { .n = { "retry-after",                 11 }, .v = { "",               0 } },
	[54] = { .n = { "server",                       6 }, .v = { "",               0 } },
	[55] = { .n = { "set-cookie",                  10 }, .v = { "",               0 } },
	[56] = { .n = { "strict-transport-security",   25 }, .v = { "",               0 } },
	[57] = { .n = { "transfer-encoding",           17 }, .v = { "",               0 } },
	[58] = { .n = { "user-agent",                  10 }, .v = { "",               0 } },
	[59] = { .n = { "vary",                         4 }, .v = { "",               0 } },
	[60] = { .n = { "via",                          3 }, .v = { "",               0 } },
	[61] = { .n = { "www-authenticate",            16 }, .v = { "",               0 } },
};

/* input line: hex chars + \n + \0 */
static char in_hex[MAX_INPUT*2+2];

/* input code */
static uint8_t in[MAX_INPUT];
static unsigned in_len;

/* debug mode : 0 = none, 1 = encoding, 2 = code */
static int debug_mode;

static void dht_dump(const struct dht *dht);

/* makes an str struct from a string and a length */
static inline struct str mkstr(const char *ptr, size_t len)
{
	struct str ret = { .ptr = (char *)ptr, .len = len };
	return ret;
}

/* copies the contents from string <str> to buffer <buf> and adds a trailing
 * zero. The caller must ensure <buf> is large enough.
 */
static inline struct str padstr(char *buf, const struct str str)
{
	struct str ret = { .ptr = buf, .len = str.len };

	memcpy(buf, str.ptr, str.len);
	buf[str.len] = 0;
	return ret;
}

/* copies <len> bytes from string <raw> to buffer <buf> and adds a trailing
 * zero. The caller must ensure <buf> is large enough.
 */
static inline struct str rawstr(char *buf, const uint8_t *raw, size_t len)
{
	struct str ret = { .ptr = buf, .len = len };

	memcpy(buf, raw, len);
	buf[len] = 0;
	return ret;
}

/* returns the slot number of the oldest entry (tail). Must not be used on an
 * empty table.
 */
static inline unsigned int dht_get_tail(const struct dht *dht)
{
	return ((dht->head + 1U < dht->used) ? dht->wrap : 0) + dht->head + 1U - dht->used;
}

/* rebuild a new dynamic header table from <dht> with an unwrapped index and
 * contents at the end. The new table is returned, the caller must not use the
 * previous one anymore. NULL may be returned if no table could be allocated.
 */
static struct dht *dht_defrag(struct dht *dht)
{
	static struct dht *alt_dht;
	uint16_t old, new;
	uint32_t addr;

	if (alt_dht && alt_dht->size != dht->size) {
		free(alt_dht);
		alt_dht = NULL;
	}

	if (!alt_dht) {
		alt_dht = calloc(1, dht->size);
		if (!alt_dht)
			return NULL;
	}
	alt_dht->size = dht->size;
	alt_dht->total = dht->total;
	alt_dht->used = dht->used;
	alt_dht->wrap = dht->used;

	new = 0;
	addr = alt_dht->size;

	if (dht->used) {
		/* start from the tail */
		old = dht_get_tail(dht);
		do {
			alt_dht->dte[new].nlen = dht->dte[old].nlen;
			alt_dht->dte[new].vlen = dht->dte[old].vlen;
			addr -= dht->dte[old].nlen + dht->dte[old].vlen;
			alt_dht->dte[new].addr = addr;

			memcpy((void *)alt_dht + alt_dht->dte[new].addr,
			       (void *)dht + dht->dte[old].addr,
			       dht->dte[old].nlen + dht->dte[old].vlen);

			old++;
			if (old >= dht->wrap)
				old = 0;
			new++;
		} while (new < dht->used);
	}

	alt_dht->front = alt_dht->head = new - 1;

	/* FIXME: overwrite the original dht for now */
	memcpy(dht, alt_dht, dht->size);

	//tmp = alt_dht;
	//alt_dht = dht;
	//dht = tmp;

	return dht;
}

/* Purges table dht until a header field of <needed> bytes fits according to
 * the protocol (adding 32 bytes overhead). Returns non-zero on success, zero
 * on failure (ie: table empty but still not sufficient). It must only be
 * called when the table is not large enough to suit the new entry and there
 * are some entries left. In case of doubt, use dht_make_room() instead.
 */
static int __dht_make_room(struct dht *dht, unsigned int needed)
{
	unsigned int used = dht->used;
	unsigned int wrap = dht->wrap;
	unsigned int tail;

	do {
		tail = ((dht->head + 1U < used) ? wrap : 0) + dht->head + 1U - used;
		dht->total -= dht->dte[tail].nlen + dht->dte[tail].vlen;
		if (tail == dht->front)
			dht->front = dht->head;
		used--;
	} while (used && used * 32 + dht->total + needed + 32 > dht->size);

	dht->used = used;

	/* realign if empty */
	if (!used)
		dht->front = dht->head = 0;

	/* pack the table if it doesn't wrap anymore */
	if (dht->head + 1U >= used)
		dht->wrap = dht->head + 1;

	/* no need to check for 'used' here as if it doesn't fit, used==0 */
	return needed + 32 <= dht->size;
}

/* Purges table dht until a header field of <needed> bytes fits according to
 * the protocol (adding 32 bytes overhead). Returns non-zero on success, zero
 * on failure (ie: table empty but still not sufficient).
 */
static inline int dht_make_room(struct dht *dht, unsigned int needed)
{
	if (!dht->used || dht->used * 32 + dht->total + needed + 32 <= dht->size)
		return 1;

	return __dht_make_room(dht, needed);
}

/* tries to insert a new header <name>:<value> in front of the current head */
static void dht_insert(struct dht *dht, struct str name, struct str value)
{
	unsigned int used;
	unsigned int head;
	unsigned int prev;
	unsigned int wrap;
	unsigned int tail;
	uint32_t headroom, tailroom;

	if (!dht_make_room(dht, name.len + value.len))
		return;

	used = dht->used;
	prev = head = dht->head;
	wrap = dht->wrap;
	tail = dht_get_tail(dht);

	/* Now there is enough room in the table, that's guaranteed by the
	 * protocol, but not necessarily where we need it.
	 */

	if (!used) {
		/* easy, the table was empty */
		dht->front = dht->head = 0;
		dht->wrap  = dht->used = 1;
		dht->total = 0;
		dht->dte[head].addr = dht->size - (name.len + value.len);
		head = 0;
		goto copy;
	}

	/* compute the new head, used and wrap position */
	used++;
	head++;

	if (head >= wrap) {
		/* head is leading the entries, we either need to push the
		 * table further or to loop back to released entries. We could
		 * force to loop back when at least half of the allocatable
		 * entries are free but in practice it never happens.
		 */
		if ((sizeof(*dht) + (wrap + 1) * sizeof(dht->dte[0]) <= dht->dte[dht->front].addr))
			wrap++;
		else if (head >= used) /* there's a hole at the beginning */
			head = 0;
		else {
			/* no more room, head hits tail and the index cannot be
			 * extended, we have to realign the whole table.
			 */
			dht = dht_defrag(dht);
			wrap = dht->wrap + 1;
			head = dht->head + 1;
			prev = head - 1;
			tail = 0;
		}
	}
	else if (used >= wrap) {
		/* we've hit the tail, we need to reorganize the index so that
		 * the head is at the end (but not necessarily move the data).
		 */
		dht = dht_defrag(dht);
		wrap = dht->wrap + 1;
		head = dht->head + 1;
		prev = head - 1;
		tail = 0;
	}

	/* Now we have updated head, used and wrap, we know that there is some
	 * available room at least from the protocol's perspective. This space
	 * is split in two areas :
	 *
	 *   1: if the previous head was the front cell, the space between the
	 *      end of the index table and the front cell's address.
	 *   2: if the previous head was the front cell, the space between the
	 *      end of the tail and the end of the table ; or if the previous
	 *      head was not the front cell, the space between the end of the
	 *      tail and the head's address.
	 */
	if (prev == dht->front) {
		/* the area was contiguous */
		headroom = dht->dte[dht->front].addr - (sizeof(*dht) + wrap * sizeof(dht->dte[0]));
		tailroom = dht->size - dht->dte[tail].addr - dht->dte[tail].nlen - dht->dte[tail].vlen;
	}
	else {
		/* it's already wrapped so we can't store anything in the headroom */
		headroom = 0;
		tailroom = dht->dte[prev].addr - dht->dte[tail].addr - dht->dte[tail].nlen - dht->dte[tail].vlen;
	}

	/* We can decide to stop filling the headroom as soon as there's enough
	 * room left in the tail to suit the protocol, but tests show that in
	 * practice it almost never happens in other situations so the extra
	 * test is useless and we simply fill the headroom as long as it's
	 * available.
	 */
	if (headroom >= name.len + value.len) {
		/* install upfront and update ->front */
		dht->dte[head].addr = dht->dte[dht->front].addr - (name.len + value.len);
		dht->front = head;
	}
	else if (tailroom >= name.len + value.len) {
		dht->dte[head].addr = dht->dte[tail].addr + dht->dte[tail].nlen + dht->dte[tail].vlen + tailroom - (name.len + value.len);
	}
	else {
		/* need to defragment the table before inserting upfront */
		dht = dht_defrag(dht);
		wrap = dht->wrap + 1;
		head = dht->head + 1;
		dht->dte[head].addr = dht->dte[dht->front].addr - (name.len + value.len);
		dht->front = head;
	}

	dht->wrap = wrap;
	dht->head = head;
	dht->used = used;

 copy:
	dht->total         += name.len + value.len;
	dht->dte[head].nlen = name.len;
	dht->dte[head].vlen = value.len;

	memcpy((void *)dht + dht->dte[head].addr, name.ptr, name.len);
	memcpy((void *)dht + dht->dte[head].addr + name.len, value.ptr, value.len);
}

/* allocate a dynamic headers table of <size> bytes and return it initialized */
static inline void init_dht(struct dht *dht, uint32_t size)
{
	dht->size = size;
	dht->total = 0;
	dht->used = 0;
}

/* allocate a dynamic headers table of <size> bytes and return it initialized */
static inline void *alloc_dht(uint32_t size)
{
	struct dht *dht;

	dht = calloc(1, size);
	if (!dht)
		return dht;

	init_dht(dht, size);
	return dht;
}

/* return a pointer to the entry designated by index <idx> (starting at 1) or
 * NULL if this index is not there.
 */
static inline const struct dte *hpack_get_dte(const struct dht *dht, uint16_t idx)
{
	idx--;

	if (idx >= dht->used)
		return NULL;

	if (idx <= dht->head)
		idx = dht->head - idx;
	else
		idx = dht->head - idx + dht->wrap;

	return &dht->dte[idx];
}

/* return a pointer to the header name for entry <dte>. */
static inline struct str hpack_get_name(const struct dht *dht, const struct dte *dte)
{
	struct str ret = {
		.ptr = (void *)dht + dte->addr,
		.len = dte->nlen,
	};
	return ret;
}

/* return a pointer to the header value for entry <dte>. */
static inline struct str hpack_get_value(const struct dht *dht, const struct dte *dte)
{
	struct str ret = {
		.ptr = (void *)dht + dte->addr + dte->nlen,
		.len = dte->vlen,
	};
	return ret;
}

/* takes an idx, returns the associated name */
static inline struct str idx_to_name(const struct dht *dht, int idx)
{
	struct str dyn = { .ptr = "[dynamic_name]", 14 };
	const struct dte *dte;

	if (idx <= STATIC_SIZE)
		return sh[idx].n;

	dte = hpack_get_dte(dht, idx - STATIC_SIZE);
	if (!dte)
		return mkstr("### ERR ###", 11); // error

	dyn = hpack_get_name(dht, dte);
	return dyn;
}

/* takes an idx, returns the associated value */
static inline struct str idx_to_value(const struct dht *dht, int idx)
{
	struct str dyn = { .ptr = "[dynamic_value]", 15 };
	const struct dte *dte;

	if (idx <= STATIC_SIZE)
		return sh[idx].v;

	dte = hpack_get_dte(dht, idx - STATIC_SIZE);
	if (!dte)
		return mkstr("### ERR ###", 11); // error

	dyn = hpack_get_value(dht, dte);
	return dyn;
}

static void dht_dump(const struct dht *dht)
{
	int i;
	unsigned int slot;
	char name[DHSIZE], value[DHSIZE];

	for (i = STATIC_SIZE + 1; i <= STATIC_SIZE + dht->used; i++) {
		slot = (hpack_get_dte(dht, i - STATIC_SIZE) - dht->dte);
		fprintf(stderr, "idx=%d slot=%u name=<%s> value=<%s> addr=%u-%u\n",
			i, slot,
			padstr(name, idx_to_name(dht, i)).ptr,
			padstr(value, idx_to_value(dht, i)).ptr,
			dht->dte[slot].addr, dht->dte[slot].addr+dht->dte[slot].nlen+dht->dte[slot].vlen-1);
	}
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
	char *i;
	uint8_t *o;
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
	unsigned int i;
	int b = 0;

	for (i = 1; i < sizeof(sh)/sizeof(sh[0]); i++) {
		if (strcasecmp(n, sh[i].n.ptr) == 0) {
			if (strcasecmp(v, sh[i].v.ptr) == 0) {
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

/* reads a varint from <raw>'s lowest <b> bits and <len> bytes max (raw included).
 * returns the 32-bit value on success after updating raw_in and len_in. Forces
 * len_in to (uint32_t)-1 on truncated input.
 */
uint32_t get_var_int(const uint8_t **raw_in, uint32_t *len_in, int b)
{
	uint32_t ret = 0;
	int len = *len_in;
	const uint8_t *raw = *raw_in;
	uint8_t shift = 0;

	len--;
	ret = *(raw++) & ((1 << b) - 1);
	if (ret != (uint32_t)((1 << b) - 1))
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
	*len_in = (uint32_t)-1;
	return 0;
}

/* only takes care of frames affecting the dynamic table for now. Returns 0 on
 * success or < 0 on error.
 */
int decode_frame(const uint8_t *raw, uint32_t len)
{
	uint32_t idx;
	uint32_t nlen;
	uint32_t vlen;
	uint8_t huff;
	struct str name;
	struct str value;
	int c;
	static char ntrash[16384];
	static char vtrash[16384];

	while (len) {
		c = *raw;
		if (*raw >= 0x81) {
			/* indexed header field */
			idx = get_var_int(&raw, &len, 7);
			if (len == (uint32_t)-1) // truncated
				return -1;

			name  = padstr(ntrash, idx_to_name(dht, idx));
			value = padstr(vtrash, idx_to_value(dht, idx));
			printf("%02x: p14: indexed header field\n  %s: %s\n", c, name.ptr, value.ptr);
		}
		else if (*raw >= 0x41 && *raw <= 0x7f) {
			/* literal header field with incremental indexing -- indexed name */
			idx = get_var_int(&raw, &len, 6);
			if (len == (uint32_t)-1) // truncated
				return -2;
			if (!len) // truncated
				return -3;

			name = padstr(ntrash, idx_to_name(dht, idx));
			huff = *raw & 0x80;
			vlen = get_var_int(&raw, &len, 7);
			if (len == (uint32_t)-1) // truncated
				return -4;
			if (len < vlen) // truncated
				return -5;

			raw += vlen;
			len -= vlen;

			if (huff) {
				vlen = huff_dec(raw - vlen, vlen, vtrash, sizeof(vtrash));
				if (vlen == (uint32_t)-1)
					fprintf(stderr, "1: can't decode huffman.\n");
				value = mkstr(vtrash, vlen);
			} else {
				value = rawstr(vtrash, raw - vlen, vlen);
			}
			//hpack_store(hpack_insert_before_first(dht, get_dt_used(dht) + 1, name.len, value.len), name, value);
			dht_insert(dht, name, value);
			printf("%02x: p15: literal with indexing -- name\n  %s: %s [used=%d]\n", c, name.ptr, value.ptr, dht->used);
		}
		else if (*raw == 0x40) {
			/* literal header field with incremental indexing -- literal name */
			raw++; len--;
			/* name */
			if (!len) // truncated
				return -6;

			huff = *raw & 0x80;
			nlen = get_var_int(&raw, &len, 7);
			if (len == (uint32_t)-1) // truncated
				return -7;
			if (len < nlen) // truncated
				return -8;

			raw += nlen;
			len -= nlen;

			if (huff) {
				nlen = huff_dec(raw - nlen, nlen, ntrash, sizeof(ntrash));
				if (nlen == (uint32_t)-1)
					fprintf(stderr, "2: can't decode huffman.\n");
				name = mkstr(ntrash, nlen);
			} else {
				name = rawstr(ntrash, raw - nlen, nlen);
			}

			/* value */
			if (!len) // truncated
				return -9;

			huff = *raw & 0x80;
			vlen = get_var_int(&raw, &len, 7);
			if (len == (uint32_t)-1) // truncated
				return -10;
			if (len < vlen) // truncated
				return -11;

			raw += vlen;
			len -= vlen;

			if (huff) {
				vlen = huff_dec(raw - vlen, vlen, vtrash, sizeof(vtrash));
				if (vlen == (uint32_t)-1)
					fprintf(stderr, "3: can't decode huffman.\n");
				value = mkstr(vtrash, vlen);
			} else {
				value = rawstr(vtrash, raw - vlen, vlen);
			}

			//hpack_store(hpack_insert_before_first(dht, get_dt_used(dht) + 1, name.len, value.len), name, value);
			dht_insert(dht, name, value);
			printf("%02x: p16: literal with indexing\n  %s: %s [used=%d]\n", c, name.ptr, value.ptr, dht->used);
		}
		else if (*raw >= 0x01 && *raw <= 0x0f) {
			/* literal header field without indexing -- indexed name */
			idx = get_var_int(&raw, &len, 4);
			if (len == (uint32_t)-1) // truncated
				return -12;
			if (!len) // truncated
				return -13;

			name = padstr(ntrash, idx_to_name(dht, idx));
			huff = *raw & 0x80;
			vlen = get_var_int(&raw, &len, 7);
			if (len == (uint32_t)-1) // truncated
				return -14;
			if (len < vlen) // truncated
				return -15;

			raw += vlen;
			len -= vlen;

			if (huff) {
				vlen = huff_dec(raw - vlen, vlen, vtrash, sizeof(vtrash));
				if (vlen == (uint32_t)-1)
					fprintf(stderr, "4: can't decode huffman.\n");
				value = mkstr(vtrash, vlen);
			} else {
				value = rawstr(vtrash, raw - vlen, vlen);
			}

			printf("%02x: p16: literal without indexing -- name\n  %s: %s\n", c, name.ptr, value.ptr);
		}
		else if (*raw == 0x00) {
			/* literal header field without indexing -- literal name */
			raw++; len--;

			/* name */
			if (!len) // truncated
				return -16;
			huff = *raw & 0x80;
			nlen = get_var_int(&raw, &len, 7);
			if (len == (uint32_t)-1) // truncated
				return -17;
			if (len < nlen) // truncated
				return -18;

			raw += nlen;
			len -= nlen;

			if (huff) {
				nlen = huff_dec(raw - nlen, nlen, ntrash, sizeof(ntrash));
				if (nlen == (uint32_t)-1)
					fprintf(stderr, "5: can't decode huffman.\n");
				name = mkstr(ntrash, nlen);
			} else {
				name = rawstr(ntrash, raw - nlen, nlen);
			}

			/* value */
			if (!len) // truncated
				return -19;
			huff = *raw & 0x80;
			vlen = get_var_int(&raw, &len, 7);
			if (len == (uint32_t)-1) // truncated
				return -20;
			if (len < vlen) // truncated
				return -21;

			raw += vlen;
			len -= vlen;

			if (huff) {
				vlen = huff_dec(raw - vlen, vlen, vtrash, sizeof(vtrash));
				if (vlen == (uint32_t)-1)
					fprintf(stderr, "6: can't decode huffman.\n");
				value = mkstr(vtrash, vlen);
			} else {
				value = rawstr(vtrash, raw - vlen, vlen);
			}

			printf("%02x: p17: literal without indexing\n  %s: %s\n", c, name.ptr, value.ptr);
		}
		else if (*raw >= 0x11 && *raw <= 0x1f) {
			/* literal header field never indexed -- indexed name */
			idx = get_var_int(&raw, &len, 4);
			if (len == (uint32_t)-1) // truncated
				return -22;
			if (!len) // truncated
				return -23;

			name = padstr(ntrash, idx_to_name(dht, idx));
			huff = *raw & 0x80;
			vlen = get_var_int(&raw, &len, 7);
			if (len == (uint32_t)-1) // truncated
				return -24;
			if (len < vlen) // truncated
				return -25;

			raw += vlen;
			len -= vlen;

			if (huff) {
				vlen = huff_dec(raw - vlen, vlen, vtrash, sizeof(vtrash));
				if (vlen == (uint32_t)-1)
					fprintf(stderr, "7: can't decode huffman.\n");
				value = mkstr(vtrash, vlen);
			} else {
				value = rawstr(vtrash, raw - vlen, vlen);
			}

			printf("%02x: p17: literal never indexed -- name\n  %s: %s\n", c, name.ptr, value.ptr);
		}
		else if (*raw == 0x10) {
			/* literal header field never indexed -- literal name */
			raw++; len--;

			/* name */
			if (!len) // truncated
				return -26;
			huff = *raw & 0x80;
			nlen = get_var_int(&raw, &len, 7);
			if (len == (uint32_t)-1) // truncated
				return -27;
			if (len < nlen) // truncated
				return -28;

			raw += nlen;
			len -= nlen;

			if (huff) {
				nlen = huff_dec(raw - nlen, nlen, ntrash, sizeof(ntrash));
				if (nlen == (uint32_t)-1)
					fprintf(stderr, "8: can't decode huffman.\n");
				name = mkstr(ntrash, nlen);
			} else {
				name = rawstr(ntrash, raw - nlen, nlen);
			}

			/* value */
			if (!len) // truncated
				return -29;
			huff = *raw & 0x80;
			vlen = get_var_int(&raw, &len, 7);
			if (len == (uint32_t)-1) // truncated
				return -30;
			if (len < vlen) // truncated
				return -31;

			raw += vlen;
			len -= vlen;

			if (huff) {
				vlen = huff_dec(raw - vlen, vlen, vtrash, sizeof(vtrash));
				if (vlen == (uint32_t)-1)
					fprintf(stderr, "9: can't decode huffman.\n");
				value = mkstr(vtrash, vlen);
			} else {
				value = rawstr(vtrash, raw - vlen, vlen);
			}

			printf("%02x: p18: literal never indexed\n  %s: %s\n", c, name.ptr, value.ptr);
		}
		else if (*raw >= 0x20 && *raw <= 0x3f) {
			/* max dyn table size change */
			idx = get_var_int(&raw, &len, 5);
			if (len == (uint32_t)-1) // truncated
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

	dht = alloc_dht(DHSIZE);
	if (!dht)
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
		if (argc > 1) // process only cmd line if provided
			break;
	}
	return 0;
}
