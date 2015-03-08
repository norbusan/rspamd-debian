#include "siphash.h"
#include "siphash_internal.h"

struct sipkey *sip_tokey(struct sipkey *key, const void *src) {
	key->k[0] = SIP_U8TO64_LE((const unsigned char *)src);
	key->k[1] = SIP_U8TO64_LE((const unsigned char *)src + 8);
	return key;
} /* sip_tokey() */


void *sip_tobin(void *dst, uint64_t u64) {
	SIP_U64TO8_LE((unsigned char *)dst, u64);
	return dst;
} /* sip_tobin() */


static inline void sip_round(struct siphash *H, const int rounds) {
	int i;

	for (i = 0; i < rounds; i++) {
		H->v0 += H->v1;
		H->v1 = SIP_ROTL(H->v1, 13);
		H->v1 ^= H->v0;
		H->v0 = SIP_ROTL(H->v0, 32);

		H->v2 += H->v3;
		H->v3 = SIP_ROTL(H->v3, 16);
		H->v3 ^= H->v2;

		H->v0 += H->v3;
		H->v3 = SIP_ROTL(H->v3, 21);
		H->v3 ^= H->v0;

		H->v2 += H->v1;
		H->v1 = SIP_ROTL(H->v1, 17);
		H->v1 ^= H->v2;
		H->v2 = SIP_ROTL(H->v2, 32);
	}
} /* sip_round() */


struct siphash *sip24_init(struct siphash *H, const struct sipkey *key) {
	H->v0 = 0x736f6d6570736575ULL ^ key->k[0];
	H->v1 = 0x646f72616e646f6dULL ^ key->k[1];
	H->v2 = 0x6c7967656e657261ULL ^ key->k[0];
	H->v3 = 0x7465646279746573ULL ^ key->k[1];

	H->p = H->buf;
	H->c = 0;

	return H;
} /* sip24_init() */


struct siphash *sip24_update(struct siphash *H, const void *src, size_t len) {
	const unsigned char *p = src, *pe = p + len;
	uint64_t m;

	do {
		while (p < pe && H->p < sip_endof(H->buf))
			*H->p++ = *p++;

		if (H->p < sip_endof(H->buf))
			break;

		m = SIP_U8TO64_LE(H->buf);
		H->v3 ^= m;
		sip_round(H, 2);
		H->v0 ^= m;

		H->p = H->buf;
		H->c += 8;
	} while (p < pe);

	return H;
} /* sip24_update() */


uint64_t sip24_final(struct siphash *H) {
	char left = H->p - H->buf;
	uint64_t b = (H->c + left) << 56;

	switch (left) {
	case 7: b |= (uint64_t)H->buf[6] << 48;
	case 6: b |= (uint64_t)H->buf[5] << 40;
	case 5: b |= (uint64_t)H->buf[4] << 32;
	case 4: b |= (uint64_t)H->buf[3] << 24;
	case 3: b |= (uint64_t)H->buf[2] << 16;
	case 2: b |= (uint64_t)H->buf[1] << 8;
	case 1: b |= (uint64_t)H->buf[0] << 0;
	case 0: break;
	}

	H->v3 ^= b;
	sip_round(H, 2);
	H->v0 ^= b;
	H->v2 ^= 0xff;
	sip_round(H, 4);

	return H->v0 ^ H->v1 ^ H->v2  ^ H->v3;
} /* sip24_final() */
