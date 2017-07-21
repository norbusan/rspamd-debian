/*-
 * Copyright 2016 Vsevolod Stakhov
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef UTHASH_STRCASE_H_
#define UTHASH_STRCASE_H_


/* Utils for uthash tuning */
#ifndef HASH_CASELESS
#define HASH_FUNCTION(key,keylen,num_bkts,hashv,bkt) do {\
	hashv = mum(key, keylen, 0xdeadbabe); \
	bkt = (hashv) & (num_bkts-1); \
} while (0)

#define HASH_KEYCMP(a,b,len) memcmp(a,b,len)
#else
#define HASH_FUNCTION(key,keylen,num_bkts,hashv,bkt) do {\
	unsigned _len = keylen; \
	unsigned _leftover = keylen % 8; \
	unsigned _fp, _i; \
	const uint8_t* _s = (const uint8_t*)(key); \
	union { \
		struct { \
			unsigned char c1, c2, c3, c4, c5, c6, c7, c8; \
		} c; \
		uint64_t pp; \
	} _u; \
	uint64_t _r; \
	_fp = _len - _leftover; \
	_r = 0xdeadbabe; \
	for (_i = 0; _i != _fp; _i += 8) { \
		_u.c.c1 = _s[_i], _u.c.c2 = _s[_i + 1], _u.c.c3 = _s[_i + 2], _u.c.c4 = _s[_i + 3]; \
		_u.c.c5 = _s[_i + 4], _u.c.c6 = _s[_i + 5], _u.c.c7 = _s[_i + 6], _u.c.c8 = _s[_i + 7]; \
		_u.c.c1 = lc_map[_u.c.c1]; \
		_u.c.c2 = lc_map[_u.c.c2]; \
		_u.c.c3 = lc_map[_u.c.c3]; \
		_u.c.c4 = lc_map[_u.c.c4]; \
		_u.c.c1 = lc_map[_u.c.c5]; \
		_u.c.c2 = lc_map[_u.c.c6]; \
		_u.c.c3 = lc_map[_u.c.c7]; \
		_u.c.c4 = lc_map[_u.c.c8]; \
		_r = mum_hash_step (_r, _u.pp); \
	} \
	_u.pp = 0; \
	switch (_leftover) { \
	case 7: \
		_u.c.c7 = lc_map[(unsigned char)_s[_i++]]; \
	case 6: \
		_u.c.c6 = lc_map[(unsigned char)_s[_i++]]; \
	case 5: \
		_u.c.c5 = lc_map[(unsigned char)_s[_i++]]; \
	case 4: \
		_u.c.c4 = lc_map[(unsigned char)_s[_i++]]; \
	case 3: \
		_u.c.c3 = lc_map[(unsigned char)_s[_i++]]; \
	case 2: \
		_u.c.c2 = lc_map[(unsigned char)_s[_i++]]; \
	case 1: \
		_u.c.c1 = lc_map[(unsigned char)_s[_i]]; \
		_r = mum_hash_step (_r, _u.pp); \
		break; \
	} \
	hashv = mum_hash_finish (_r); \
	bkt = (hashv) & (num_bkts-1); \
} while (0)
#define HASH_KEYCMP(a,b,len) rspamd_lc_cmp(a,b,len)
#endif

#include "uthash.h"

#endif /* UTHASH_STRCASE_H_ */
