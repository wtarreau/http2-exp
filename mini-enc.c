/* mini-h2 encoder just for metrics - certainly bogus */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DHSIZE 8192
#define STATIC_SIZE 61

#define debug_printf(l, f, ...)  do { if (debug_mode >= (l)) printf((f), ##__VA_ARGS__); } while (0)

struct hdr {
	char *n; /* name */
	char *v; /* value */
};

struct huff {
	uint32_t c; /* code point */
	int b;      /* bits */
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

static const struct huff ht[257] = {
	[0] = { .c = 0x00001ff8, .b = 13 },
	[1] = { .c = 0x007fffd8, .b = 23 },
	[2] = { .c = 0x0fffffe2, .b = 28 },
	[3] = { .c = 0x0fffffe3, .b = 28 },
	[4] = { .c = 0x0fffffe4, .b = 28 },
	[5] = { .c = 0x0fffffe5, .b = 28 },
	[6] = { .c = 0x0fffffe6, .b = 28 },
	[7] = { .c = 0x0fffffe7, .b = 28 },
	[8] = { .c = 0x0fffffe8, .b = 28 },
	[9] = { .c = 0x00ffffea, .b = 24 },
	[10] = { .c = 0x3ffffffc, .b = 30 },
	[11] = { .c = 0x0fffffe9, .b = 28 },
	[12] = { .c = 0x0fffffea, .b = 28 },
	[13] = { .c = 0x3ffffffd, .b = 30 },
	[14] = { .c = 0x0fffffeb, .b = 28 },
	[15] = { .c = 0x0fffffec, .b = 28 },
	[16] = { .c = 0x0fffffed, .b = 28 },
	[17] = { .c = 0x0fffffee, .b = 28 },
	[18] = { .c = 0x0fffffef, .b = 28 },
	[19] = { .c = 0x0ffffff0, .b = 28 },
	[20] = { .c = 0x0ffffff1, .b = 28 },
	[21] = { .c = 0x0ffffff2, .b = 28 },
	[22] = { .c = 0x3ffffffe, .b = 30 },
	[23] = { .c = 0x0ffffff3, .b = 28 },
	[24] = { .c = 0x0ffffff4, .b = 28 },
	[25] = { .c = 0x0ffffff5, .b = 28 },
	[26] = { .c = 0x0ffffff6, .b = 28 },
	[27] = { .c = 0x0ffffff7, .b = 28 },
	[28] = { .c = 0x0ffffff8, .b = 28 },
	[29] = { .c = 0x0ffffff9, .b = 28 },
	[30] = { .c = 0x0ffffffa, .b = 28 },
	[31] = { .c = 0x0ffffffb, .b = 28 },
	[32] = { .c = 0x00000014, .b =  6 },
	[33] = { .c = 0x000003f8, .b = 10 },
	[34] = { .c = 0x000003f9, .b = 10 },
	[35] = { .c = 0x00000ffa, .b = 12 },
	[36] = { .c = 0x00001ff9, .b = 13 },
	[37] = { .c = 0x00000015, .b =  6 },
	[38] = { .c = 0x000000f8, .b =  8 },
	[39] = { .c = 0x000007fa, .b = 11 },
	[40] = { .c = 0x000003fa, .b = 10 },
	[41] = { .c = 0x000003fb, .b = 10 },
	[42] = { .c = 0x000000f9, .b =  8 },
	[43] = { .c = 0x000007fb, .b = 11 },
	[44] = { .c = 0x000000fa, .b =  8 },
	[45] = { .c = 0x00000016, .b =  6 },
	[46] = { .c = 0x00000017, .b =  6 },
	[47] = { .c = 0x00000018, .b =  6 },
	[48] = { .c = 0x00000000, .b =  5 },
	[49] = { .c = 0x00000001, .b =  5 },
	[50] = { .c = 0x00000002, .b =  5 },
	[51] = { .c = 0x00000019, .b =  6 },
	[52] = { .c = 0x0000001a, .b =  6 },
	[53] = { .c = 0x0000001b, .b =  6 },
	[54] = { .c = 0x0000001c, .b =  6 },
	[55] = { .c = 0x0000001d, .b =  6 },
	[56] = { .c = 0x0000001e, .b =  6 },
	[57] = { .c = 0x0000001f, .b =  6 },
	[58] = { .c = 0x0000005c, .b =  7 },
	[59] = { .c = 0x000000fb, .b =  8 },
	[60] = { .c = 0x00007ffc, .b = 15 },
	[61] = { .c = 0x00000020, .b =  6 },
	[62] = { .c = 0x00000ffb, .b = 12 },
	[63] = { .c = 0x000003fc, .b = 10 },
	[64] = { .c = 0x00001ffa, .b = 13 },
	[65] = { .c = 0x00000021, .b =  6 },
	[66] = { .c = 0x0000005d, .b =  7 },
	[67] = { .c = 0x0000005e, .b =  7 },
	[68] = { .c = 0x0000005f, .b =  7 },
	[69] = { .c = 0x00000060, .b =  7 },
	[70] = { .c = 0x00000061, .b =  7 },
	[71] = { .c = 0x00000062, .b =  7 },
	[72] = { .c = 0x00000063, .b =  7 },
	[73] = { .c = 0x00000064, .b =  7 },
	[74] = { .c = 0x00000065, .b =  7 },
	[75] = { .c = 0x00000066, .b =  7 },
	[76] = { .c = 0x00000067, .b =  7 },
	[77] = { .c = 0x00000068, .b =  7 },
	[78] = { .c = 0x00000069, .b =  7 },
	[79] = { .c = 0x0000006a, .b =  7 },
	[80] = { .c = 0x0000006b, .b =  7 },
	[81] = { .c = 0x0000006c, .b =  7 },
	[82] = { .c = 0x0000006d, .b =  7 },
	[83] = { .c = 0x0000006e, .b =  7 },
	[84] = { .c = 0x0000006f, .b =  7 },
	[85] = { .c = 0x00000070, .b =  7 },
	[86] = { .c = 0x00000071, .b =  7 },
	[87] = { .c = 0x00000072, .b =  7 },
	[88] = { .c = 0x000000fc, .b =  8 },
	[89] = { .c = 0x00000073, .b =  7 },
	[90] = { .c = 0x000000fd, .b =  8 },
	[91] = { .c = 0x00001ffb, .b = 13 },
	[92] = { .c = 0x0007fff0, .b = 19 },
	[93] = { .c = 0x00001ffc, .b = 13 },
	[94] = { .c = 0x00003ffc, .b = 14 },
	[95] = { .c = 0x00000022, .b =  6 },
	[96] = { .c = 0x00007ffd, .b = 15 },
	[97] = { .c = 0x00000003, .b =  5 },
	[98] = { .c = 0x00000023, .b =  6 },
	[99] = { .c = 0x00000004, .b =  5 },
	[100] = { .c = 0x00000024, .b =  6 },
	[101] = { .c = 0x00000005, .b =  5 },
	[102] = { .c = 0x00000025, .b =  6 },
	[103] = { .c = 0x00000026, .b =  6 },
	[104] = { .c = 0x00000027, .b =  6 },
	[105] = { .c = 0x00000006, .b =  5 },
	[106] = { .c = 0x00000074, .b =  7 },
	[107] = { .c = 0x00000075, .b =  7 },
	[108] = { .c = 0x00000028, .b =  6 },
	[109] = { .c = 0x00000029, .b =  6 },
	[110] = { .c = 0x0000002a, .b =  6 },
	[111] = { .c = 0x00000007, .b =  5 },
	[112] = { .c = 0x0000002b, .b =  6 },
	[113] = { .c = 0x00000076, .b =  7 },
	[114] = { .c = 0x0000002c, .b =  6 },
	[115] = { .c = 0x00000008, .b =  5 },
	[116] = { .c = 0x00000009, .b =  5 },
	[117] = { .c = 0x0000002d, .b =  6 },
	[118] = { .c = 0x00000077, .b =  7 },
	[119] = { .c = 0x00000078, .b =  7 },
	[120] = { .c = 0x00000079, .b =  7 },
	[121] = { .c = 0x0000007a, .b =  7 },
	[122] = { .c = 0x0000007b, .b =  7 },
	[123] = { .c = 0x00007ffe, .b = 15 },
	[124] = { .c = 0x000007fc, .b = 11 },
	[125] = { .c = 0x00003ffd, .b = 14 },
	[126] = { .c = 0x00001ffd, .b = 13 },
	[127] = { .c = 0x0ffffffc, .b = 28 },
	[128] = { .c = 0x000fffe6, .b = 20 },
	[129] = { .c = 0x003fffd2, .b = 22 },
	[130] = { .c = 0x000fffe7, .b = 20 },
	[131] = { .c = 0x000fffe8, .b = 20 },
	[132] = { .c = 0x003fffd3, .b = 22 },
	[133] = { .c = 0x003fffd4, .b = 22 },
	[134] = { .c = 0x003fffd5, .b = 22 },
	[135] = { .c = 0x007fffd9, .b = 23 },
	[136] = { .c = 0x003fffd6, .b = 22 },
	[137] = { .c = 0x007fffda, .b = 23 },
	[138] = { .c = 0x007fffdb, .b = 23 },
	[139] = { .c = 0x007fffdc, .b = 23 },
	[140] = { .c = 0x007fffdd, .b = 23 },
	[141] = { .c = 0x007fffde, .b = 23 },
	[142] = { .c = 0x00ffffeb, .b = 24 },
	[143] = { .c = 0x007fffdf, .b = 23 },
	[144] = { .c = 0x00ffffec, .b = 24 },
	[145] = { .c = 0x00ffffed, .b = 24 },
	[146] = { .c = 0x003fffd7, .b = 22 },
	[147] = { .c = 0x007fffe0, .b = 23 },
	[148] = { .c = 0x00ffffee, .b = 24 },
	[149] = { .c = 0x007fffe1, .b = 23 },
	[150] = { .c = 0x007fffe2, .b = 23 },
	[151] = { .c = 0x007fffe3, .b = 23 },
	[152] = { .c = 0x007fffe4, .b = 23 },
	[153] = { .c = 0x001fffdc, .b = 21 },
	[154] = { .c = 0x003fffd8, .b = 22 },
	[155] = { .c = 0x007fffe5, .b = 23 },
	[156] = { .c = 0x003fffd9, .b = 22 },
	[157] = { .c = 0x007fffe6, .b = 23 },
	[158] = { .c = 0x007fffe7, .b = 23 },
	[159] = { .c = 0x00ffffef, .b = 24 },
	[160] = { .c = 0x003fffda, .b = 22 },
	[161] = { .c = 0x001fffdd, .b = 21 },
	[162] = { .c = 0x000fffe9, .b = 20 },
	[163] = { .c = 0x003fffdb, .b = 22 },
	[164] = { .c = 0x003fffdc, .b = 22 },
	[165] = { .c = 0x007fffe8, .b = 23 },
	[166] = { .c = 0x007fffe9, .b = 23 },
	[167] = { .c = 0x001fffde, .b = 21 },
	[168] = { .c = 0x007fffea, .b = 23 },
	[169] = { .c = 0x003fffdd, .b = 22 },
	[170] = { .c = 0x003fffde, .b = 22 },
	[171] = { .c = 0x00fffff0, .b = 24 },
	[172] = { .c = 0x001fffdf, .b = 21 },
	[173] = { .c = 0x003fffdf, .b = 22 },
	[174] = { .c = 0x007fffeb, .b = 23 },
	[175] = { .c = 0x007fffec, .b = 23 },
	[176] = { .c = 0x001fffe0, .b = 21 },
	[177] = { .c = 0x001fffe1, .b = 21 },
	[178] = { .c = 0x003fffe0, .b = 22 },
	[179] = { .c = 0x001fffe2, .b = 21 },
	[180] = { .c = 0x007fffed, .b = 23 },
	[181] = { .c = 0x003fffe1, .b = 22 },
	[182] = { .c = 0x007fffee, .b = 23 },
	[183] = { .c = 0x007fffef, .b = 23 },
	[184] = { .c = 0x000fffea, .b = 20 },
	[185] = { .c = 0x003fffe2, .b = 22 },
	[186] = { .c = 0x003fffe3, .b = 22 },
	[187] = { .c = 0x003fffe4, .b = 22 },
	[188] = { .c = 0x007ffff0, .b = 23 },
	[189] = { .c = 0x003fffe5, .b = 22 },
	[190] = { .c = 0x003fffe6, .b = 22 },
	[191] = { .c = 0x007ffff1, .b = 23 },
	[192] = { .c = 0x03ffffe0, .b = 26 },
	[193] = { .c = 0x03ffffe1, .b = 26 },
	[194] = { .c = 0x000fffeb, .b = 20 },
	[195] = { .c = 0x0007fff1, .b = 19 },
	[196] = { .c = 0x003fffe7, .b = 22 },
	[197] = { .c = 0x007ffff2, .b = 23 },
	[198] = { .c = 0x003fffe8, .b = 22 },
	[199] = { .c = 0x01ffffec, .b = 25 },
	[200] = { .c = 0x03ffffe2, .b = 26 },
	[201] = { .c = 0x03ffffe3, .b = 26 },
	[202] = { .c = 0x03ffffe4, .b = 26 },
	[203] = { .c = 0x07ffffde, .b = 27 },
	[204] = { .c = 0x07ffffdf, .b = 27 },
	[205] = { .c = 0x03ffffe5, .b = 26 },
	[206] = { .c = 0x00fffff1, .b = 24 },
	[207] = { .c = 0x01ffffed, .b = 25 },
	[208] = { .c = 0x0007fff2, .b = 19 },
	[209] = { .c = 0x001fffe3, .b = 21 },
	[210] = { .c = 0x03ffffe6, .b = 26 },
	[211] = { .c = 0x07ffffe0, .b = 27 },
	[212] = { .c = 0x07ffffe1, .b = 27 },
	[213] = { .c = 0x03ffffe7, .b = 26 },
	[214] = { .c = 0x07ffffe2, .b = 27 },
	[215] = { .c = 0x00fffff2, .b = 24 },
	[216] = { .c = 0x001fffe4, .b = 21 },
	[217] = { .c = 0x001fffe5, .b = 21 },
	[218] = { .c = 0x03ffffe8, .b = 26 },
	[219] = { .c = 0x03ffffe9, .b = 26 },
	[220] = { .c = 0x0ffffffd, .b = 28 },
	[221] = { .c = 0x07ffffe3, .b = 27 },
	[222] = { .c = 0x07ffffe4, .b = 27 },
	[223] = { .c = 0x07ffffe5, .b = 27 },
	[224] = { .c = 0x000fffec, .b = 20 },
	[225] = { .c = 0x00fffff3, .b = 24 },
	[226] = { .c = 0x000fffed, .b = 20 },
	[227] = { .c = 0x001fffe6, .b = 21 },
	[228] = { .c = 0x003fffe9, .b = 22 },
	[229] = { .c = 0x001fffe7, .b = 21 },
	[230] = { .c = 0x001fffe8, .b = 21 },
	[231] = { .c = 0x007ffff3, .b = 23 },
	[232] = { .c = 0x003fffea, .b = 22 },
	[233] = { .c = 0x003fffeb, .b = 22 },
	[234] = { .c = 0x01ffffee, .b = 25 },
	[235] = { .c = 0x01ffffef, .b = 25 },
	[236] = { .c = 0x00fffff4, .b = 24 },
	[237] = { .c = 0x00fffff5, .b = 24 },
	[238] = { .c = 0x03ffffea, .b = 26 },
	[239] = { .c = 0x007ffff4, .b = 23 },
	[240] = { .c = 0x03ffffeb, .b = 26 },
	[241] = { .c = 0x07ffffe6, .b = 27 },
	[242] = { .c = 0x03ffffec, .b = 26 },
	[243] = { .c = 0x03ffffed, .b = 26 },
	[244] = { .c = 0x07ffffe7, .b = 27 },
	[245] = { .c = 0x07ffffe8, .b = 27 },
	[246] = { .c = 0x07ffffe9, .b = 27 },
	[247] = { .c = 0x07ffffea, .b = 27 },
	[248] = { .c = 0x07ffffeb, .b = 27 },
	[249] = { .c = 0x0ffffffe, .b = 28 },
	[250] = { .c = 0x07ffffec, .b = 27 },
	[251] = { .c = 0x07ffffed, .b = 27 },
	[252] = { .c = 0x07ffffee, .b = 27 },
	[253] = { .c = 0x07ffffef, .b = 27 },
	[254] = { .c = 0x07fffff0, .b = 27 },
	[255] = { .c = 0x03ffffee, .b = 26 },
	[256] = { .c = 0x3fffffff, .b = 30 }, /* EOS */
};

static char in[256];
static char huff_tmp[1024]; /* at most 32-bit per input char */

/* debug mode : 0 = none, 1 = encoding, 2 = code */
static int debug_mode;

/* proposal number : 0 = draft09 (default), 1="option3", 2="Tue, 21 Oct 2014 11:40:32 +0200", 3=Greg's */
static int proposal;

/* statistics */
static int input_bytes;
static int input_str_bytes;
static int output_bytes;
static int output_ints;
static int output_int_bytes;
static int output_huf_bytes;
static int output_huf_enc;
static int output_raw_bytes;
static int output_raw_enc;
static int output_static;
static int output_static_bytes;
static int output_dynamic;
static int output_dynamic_bytes;
static int output_static_lit;
static int output_static_lit_bytes;
static int output_dynamic_lit;
static int output_dynamic_lit_bytes;
static int output_literal;
static int output_static_lit_wo;
static int output_static_lit_wo_bytes;
static int output_dynamic_lit_wo;
static int output_dynamic_lit_wo_bytes;
static int output_literal_wo;


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

static inline pos_to_idx(const struct dyn *dh, int pos)
{
	return (dh->head + dh->entries - pos) % dh->entries + 1;
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

/* reads one line, and makes np and vp point to name and value (or empty
 * string). Returns < 0 on end of stream or error.
 */
int read_input_line(const char **np, const char **vp)
{
	char *p;
	char *n, *v;

	if (!fgets(in, sizeof(in), stdin))
		return -1;

	input_bytes += strlen(in);
	if (*in == '\n' || !*in) {
		*in = 0;
		*vp = *np = in;
		return 0;
	}

	n = in;
	v = in + 1;
	while (*v != ':' && *v && *v != '\n')
		v++;

	if (*v != ':')
		return -1;

	*(v++) = '\0';
	while (*v == ' ')
		v++;

	p = v;
	while (*p != '\n' && *p)
		p++;
	*p = '\0';

	*np = n;
	*vp = v;
	return 0;
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

/* FIXME: nothing is emitted yet */
int send_byte(uint8_t b)
{
	output_bytes++;
	return 1;
}

/* encodes <v> on <b> bits using a variable encoding, and OR the first byte
 * with byte <o>. Returns the number of bytes emitted.
 */
int send_var_int(uint8_t o, uint32_t v, int b)
{
	int sent = 0;

	if (v < ((1 << b) - 1)) {
		sent += send_byte(o | v);
		goto out;
	}

	sent += send_byte(o | ((1 << b) - 1));
	v -= ((1 << b) - 1);
	while (v >= 128) {
		sent += send_byte(128 | v);
		v >>= 7;
	}
	sent += send_byte(v);
 out:
	output_ints++;
	output_int_bytes += sent;
	return sent;
}

/* huffman-encode string <s> into the huff_tmp buffer and returns the amount
 * of output bytes.
 */
int huff_enc(const char *s)
{
	int bits = 0;

	while (*s) {
		bits += ht[*s].b;
		s++;
	}
	bits += 7;

	memset(huff_tmp, 'H', bits / 8); /*  FIXME: huffman code is not emitted yet. */
	return bits / 8;
}

/* returns the number of bytes emitted */
int encode_string(const char *s)
{
	int len;
	int i;
	int sent = 0;

	input_str_bytes += strlen(s);

	len = huff_enc(s);

	if (len < strlen(s)) {
		/* send huffman encoding */
		sent +=	send_var_int(0x80, len, 7);
		for (i = 0; i < len; i++)
			sent += send_byte(huff_tmp[i]);
		output_huf_enc++;
		output_huf_bytes += len;
		return sent;
	}

	len = strlen(s);
	sent += send_var_int(0x00, len, 7);
	for (i = 0; i < len; i++)
		sent += send_byte(s[i]);
	output_raw_enc++;
	output_raw_bytes += len;
	return sent;
}

int send_static(int idx)
{
	int sent;

	switch(proposal) {
	case 0: sent = send_var_int(0x80, idx, 7); break;
	case 1: sent = send_var_int(0x80, idx, 6); break;
	case 2: sent = send_var_int(0x30, idx, 4); break;
	case 3: sent = send_var_int(0x80, idx, 6); break;
	}
	output_static++;
	output_static_bytes += sent;
	debug_printf(1, "  => %s(%d) = %d\n", __FUNCTION__, idx, sent);
	return sent;
}

int send_dynamic(int idx)
{
	int sent;

	switch(proposal) {
	case 0: sent = send_var_int(0x80, idx + STATIC_SIZE, 7); break;
	case 1: sent = send_var_int(0xC0, idx, 6); break;
	case 2: sent = send_var_int(0x40, idx, 6); break;
	case 3: sent = send_var_int(0xC0, idx, 6); break;
	}
	output_dynamic++;
	output_dynamic_bytes += sent;
	debug_printf(1, "  => %s(%d) = %d\n", __FUNCTION__, idx, sent);
	return sent;
}

int send_static_literal(int idx, const char *v)
{
	int sent = 0;

	switch(proposal) {
	case 0: sent += send_var_int(0x40, idx, 6); break;
	case 1: sent += send_var_int(0x40, idx, 5); break;
	case 2: sent += send_var_int(0x80, idx, 6); break;
	case 3: sent += send_var_int(0x40, idx, 4); break;
	}
	sent += encode_string(v);
	output_static_lit++;
	output_static_lit_bytes += sent;
	debug_printf(1, "  => %s(%d, '%s') = %d\n", __FUNCTION__, idx, v, sent);
	return sent;
}

int send_dynamic_literal(int idx, const char *v)
{
	int sent = 0;

	switch(proposal) {
	case 0: sent += send_var_int(0x40, idx + STATIC_SIZE, 6); break;
	case 1: sent += send_var_int(0x60, idx, 5); break;
	case 2: sent += send_var_int(0xC0, idx, 6); break;
	case 3: sent += send_var_int(0x50, idx, 4); break;
	}
	sent += encode_string(v);
	output_dynamic_lit++;
	output_dynamic_lit_bytes += sent;
	debug_printf(1, "  => %s(%d, '%s') = %d\n", __FUNCTION__, idx, v, sent);
	return sent;
}

int send_literal(const char *n, const char *v)
{
	int sent = 0;

	switch(proposal) {
	case 0: sent += send_byte(0x40); break;
	case 1: sent += send_byte(0x40); break;
	case 2: sent += send_byte(0x80); break;
	case 3: sent += send_byte(0x50); break;
	}
	sent += encode_string(n);
	sent += encode_string(v);
	output_literal++;
	debug_printf(1, "  => %s('%s', '%s') = %d\n", __FUNCTION__, n, v, sent);
	return sent;
}

int send_static_literal_wo(int idx, const char *v)
{
	int sent = 0;

	switch(proposal) {
	case 0: sent += send_var_int(0x00, idx, 4); break;
	case 1: sent += send_var_int(0x00, idx, 3); break;
	case 2: sent += send_var_int(0x00, idx, 3); break;
	case 3: sent += send_var_int(0x20, idx, 4); break;
	}
	sent += encode_string(v);
	output_static_lit_wo++;
	output_static_lit_wo_bytes += sent;
	debug_printf(1, "  => %s(%d, '%s') = %d\n", __FUNCTION__, idx, v, sent);
	return sent;
}

int send_dynamic_literal_wo(int idx, const char *v)
{
	int sent = 0;

	switch(proposal) {
	case 0: sent += send_var_int(0x00, idx + STATIC_SIZE, 4); break;
	case 1: sent += send_var_int(0x08, idx, 3); break;
	case 2: sent += send_var_int(0x08, idx, 3); break;
	case 3: sent += send_var_int(0x30, idx, 4); break;
	}
	sent += encode_string(v);
	output_dynamic_lit_wo++;
	output_dynamic_lit_wo_bytes += sent;
	debug_printf(1, "  => %s(%d, '%s') = %d\n", __FUNCTION__, idx, v, sent);
	return sent;
}

int send_literal_wo(const char *n, const char *v)
{
	int sent = 0;

	switch(proposal) {
	case 0: sent += send_byte(0x00); break;
	case 1: sent += send_byte(0x00); break;
	case 2: sent += send_byte(0x00); break;
	case 3: sent += send_byte(0x30); break;
	}
	sent += encode_string(n);
	sent += encode_string(v);
	output_literal_wo++;
	debug_printf(1, "  => %s('%s', '%s') = %d\n", __FUNCTION__, n, v, sent);
	return sent;
}


int main(int argc, char **argv)
{
	const char *n, *v;
	int sn, sv; /* static name, value indexes */
	int dn, dv; /* dynamic name, value indexes */
	int dont_index;

	while (argc > 1) {
		if (strcmp(argv[1], "-d") == 0)
			debug_mode++;
		else if (strcmp(argv[1], "-dd") == 0)
			debug_mode += 2;
		else if (strcmp(argv[1], "-1") == 0)
			proposal = 1;
		else if (strcmp(argv[1], "-2") == 0)
			proposal = 2;
		else if (strcmp(argv[1], "-3") == 0)
			proposal = 3;
		argv++;
		argc--;
	}

	if (init_dyn(DHSIZE) < 0)
		exit(1);

	while (read_input_line(&n, &v) >= 0) {
		if (!*n) {
			debug_printf(1, "NEXT REQUEST. Total=%d bytes\n", output_bytes);
			continue;
		}
		debug_printf(1, "\nname=<%s> value=<%s>\n", n, v);

		if (!lookup_sh(n, v, &sn, &sv))
			sn = 0;

		if (!lookup_dh(n, v, &dn, &dv))
			dn = 0;

		debug_printf(2, "  stat_idx=%d stat_v=%d dyn_idx=%d dyn_v=%d\n", sn, sv, dn, dv);

		/* decide whether or not we have to index this one */
		dont_index = 0;
		if (sn && sn == sv) /* indexed static */
			dont_index = 1;

		if (dn && dn == dv) /* indexed dynamic */
			dont_index = 1;

		/* don't index :path which changes a lot */
		switch (sn) {
		case 4: dont_index = 1; /* :path */
		}

		/* our fixed custom headers have a name starting with "xxxx". The
		 * fixed one have a value starting with "yyyy", we can index them.
		 * The other "xxxx" one are variable and not indexed by the producer.
		 * A client will have these two lines, but a gateway will not.
		 */
		//if (strncmp(n, "xxxx", 4) == 0 && strncmp(v, "yyyy", 4) != 0)
		//	dont_index = 1;

		/* now send the best encoding */

		if (sn && sn == sv)
			send_static(sn);
		else if (dn && dn == dv)
			send_dynamic(dn);
		else if (sn && (!dn || sn <= dn) && !dont_index)
			send_static_literal(sn, v);
		else if (dn && (!sn || dn <= sn) && !dont_index)
			send_dynamic_literal(dn, v);
		else if (sn && (!dn || sn <= dn) && dont_index)
			send_static_literal_wo(sn, v);
		else if (dn && (!sn || dn <= sn) && dont_index)
			send_dynamic_literal_wo(dn, v);
		else if (!dont_index)
			send_literal(n, v);
		else
			send_literal_wo(n, v);

		if (!dont_index) {
			add_to_dyn(n, v);
			debug_printf(2, "  tail=%d ; head=%d\n", dh->tail, dh->head);
		}
	}

	debug_printf(1, "end\n\n");
	printf("------------\n");

	printf("Total input bytes : %d\n", input_bytes);
	printf("Total output bytes : %d\n", output_bytes);
	printf("Overall compression ratio : %f\n", output_bytes / (double)input_bytes);

	printf("Static indexes : %d\n", output_static);
	printf("Static index bytes : %d\n", output_static_bytes);
	printf("Dynamic indexes : %d\n", output_dynamic);
	printf("Dynamic index bytes : %d\n", output_dynamic_bytes);
	printf("Static indexed literals : %d\n", output_static_lit);
	printf("Static indexed literal bytes : %d\n", output_static_lit_bytes);
	printf("Dynamic indexed literals : %d\n", output_dynamic_lit);
	printf("Dynamic indexed literals bytes : %d\n", output_dynamic_lit_bytes);
	printf("Static indexed literals w/o idx: %d\n", output_static_lit_wo);
	printf("Static indexed literals w/o idx bytes: %d\n", output_static_lit_wo_bytes);
	printf("Dynamic indexed literals w/o idx: %d\n", output_dynamic_lit_wo);
	printf("Dynamic indexed literals w/o idx bytes: %d\n", output_dynamic_lit_wo_bytes);
	printf("Literals new name: %d\n", output_literal);
	printf("Literals new name w/o idx: %d\n", output_literal_wo);
	printf("Total encoded integers: %d\n", output_ints);
	printf("Total encoded integers bytes: %d\n", output_int_bytes);
	printf("Avg bytes per integers: %f\n", output_int_bytes / (double)output_ints);


	printf("Total input string bytes : %d\n", input_str_bytes);
	printf("Total output string bytes : %d\n", output_raw_bytes + output_huf_bytes);
	printf("Total output string huffman bytes : %d\n", output_huf_bytes);
	printf("Total output string raw bytes : %d\n", output_raw_bytes);
	printf("String compression ratio : %f\n", (output_raw_bytes + output_huf_bytes) / (double)input_str_bytes);
	printf("Total output strings : %d\n", output_raw_enc + output_huf_enc);
	printf("Total output strings huffman-encoded : %d\n", output_huf_enc);
	printf("Total output strings non-encoded : %d\n", output_raw_enc);

	return 0;
}
