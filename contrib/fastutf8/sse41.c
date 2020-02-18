/*
 * MIT License
 *
 * Copyright (c) 2019 Yibo Cai
 * Copyright (c) 2019 Vsevolod Stakhov
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "config.h"
#include "fastutf8.h"
#include "platform_config.h"

#ifndef __clang__
#pragma GCC push_options
#pragma GCC target("sse4.1")
#endif

#ifndef __SSE2__
#define __SSE2__
#endif
#ifndef __SSE__
#define __SSE__
#endif
#ifndef __SSEE3__
#define __SSEE3__
#endif
#ifndef __SSE4_1__
#define __SSE4_1__
#endif

#include <smmintrin.h>

/*
 * Map high nibble of "First Byte" to legal character length minus 1
 * 0x00 ~ 0xBF --> 0
 * 0xC0 ~ 0xDF --> 1
 * 0xE0 ~ 0xEF --> 2
 * 0xF0 ~ 0xFF --> 3
 */
static const int8_t _first_len_tbl[] = {
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 3,
};

/* Map "First Byte" to 8-th item of range table (0xC2 ~ 0xF4) */
static const int8_t _first_range_tbl[] = {
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 8, 8, 8,
};

/*
 * Range table, map range index to min and max values
 * Index 0    : 00 ~ 7F (First Byte, ascii)
 * Index 1,2,3: 80 ~ BF (Second, Third, Fourth Byte)
 * Index 4    : A0 ~ BF (Second Byte after E0)
 * Index 5    : 80 ~ 9F (Second Byte after ED)
 * Index 6    : 90 ~ BF (Second Byte after F0)
 * Index 7    : 80 ~ 8F (Second Byte after F4)
 * Index 8    : C2 ~ F4 (First Byte, non ascii)
 * Index 9~15 : illegal: i >= 127 && i <= -128
 */
static const int8_t _range_min_tbl[] = {
		0x00, 0x80, 0x80, 0x80, 0xA0, 0x80, 0x90, 0x80,
		0xC2, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
};
static const int8_t _range_max_tbl[] = {
		0x7F, 0xBF, 0xBF, 0xBF, 0xBF, 0x9F, 0xBF, 0x8F,
		0xF4, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
};

/*
 * Tables for fast handling of four special First Bytes(E0,ED,F0,F4), after
 * which the Second Byte are not 80~BF. It contains "range index adjustment".
 * +------------+---------------+------------------+----------------+
 * | First Byte | original range| range adjustment | adjusted range |
 * +------------+---------------+------------------+----------------+
 * | E0         | 2             | 2                | 4              |
 * +------------+---------------+------------------+----------------+
 * | ED         | 2             | 3                | 5              |
 * +------------+---------------+------------------+----------------+
 * | F0         | 3             | 3                | 6              |
 * +------------+---------------+------------------+----------------+
 * | F4         | 4             | 4                | 8              |
 * +------------+---------------+------------------+----------------+
 */
/* index1 -> E0, index14 -> ED */
static const int8_t _df_ee_tbl[] = {
		0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0,
};
/* index1 -> F0, index5 -> F4 */
static const int8_t _ef_fe_tbl[] = {
		0, 3, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

off_t
rspamd_fast_utf8_validate_sse41 (const unsigned char *data, size_t len)
	__attribute__((__target__("sse4.1")));

/* Return 0 - success, >0 - first error char(if RET_ERR_IDX = 1) */
off_t
rspamd_fast_utf8_validate_sse41 (const unsigned char *data, size_t len)
{
	off_t err_pos = 1;

	if (len >= 16) {
		__m128i prev_input = _mm_set1_epi8 (0);
		__m128i prev_first_len = _mm_set1_epi8 (0);

		/* Cached tables */
		const __m128i first_len_tbl =
				_mm_lddqu_si128 ((const __m128i *) _first_len_tbl);
		const __m128i first_range_tbl =
				_mm_lddqu_si128 ((const __m128i *) _first_range_tbl);
		const __m128i range_min_tbl =
				_mm_lddqu_si128 ((const __m128i *) _range_min_tbl);
		const __m128i range_max_tbl =
				_mm_lddqu_si128 ((const __m128i *) _range_max_tbl);
		const __m128i df_ee_tbl =
				_mm_lddqu_si128 ((const __m128i *) _df_ee_tbl);
		const __m128i ef_fe_tbl =
				_mm_lddqu_si128 ((const __m128i *) _ef_fe_tbl);

		__m128i error = _mm_set1_epi8 (0);

		while (len >= 16) {
			const __m128i input = _mm_lddqu_si128 ((const __m128i *) data);

			/* high_nibbles = input >> 4 */
			const __m128i high_nibbles =
					_mm_and_si128 (_mm_srli_epi16 (input, 4), _mm_set1_epi8 (0x0F));

			/* first_len = legal character length minus 1 */
			/* 0 for 00~7F, 1 for C0~DF, 2 for E0~EF, 3 for F0~FF */
			/* first_len = first_len_tbl[high_nibbles] */
			__m128i first_len = _mm_shuffle_epi8 (first_len_tbl, high_nibbles);

			/* First Byte: set range index to 8 for bytes within 0xC0 ~ 0xFF */
			/* range = first_range_tbl[high_nibbles] */
			__m128i range = _mm_shuffle_epi8 (first_range_tbl, high_nibbles);

			/* Second Byte: set range index to first_len */
			/* 0 for 00~7F, 1 for C0~DF, 2 for E0~EF, 3 for F0~FF */
			/* range |= (first_len, prev_first_len) << 1 byte */
			range = _mm_or_si128 (
					range, _mm_alignr_epi8(first_len, prev_first_len, 15));

			/* Third Byte: set range index to saturate_sub(first_len, 1) */
			/* 0 for 00~7F, 0 for C0~DF, 1 for E0~EF, 2 for F0~FF */
			__m128i tmp1, tmp2;
			/* tmp1 = saturate_sub(first_len, 1) */
			tmp1 = _mm_subs_epu8 (first_len, _mm_set1_epi8 (1));
			/* tmp2 = saturate_sub(prev_first_len, 1) */
			tmp2 = _mm_subs_epu8 (prev_first_len, _mm_set1_epi8 (1));
			/* range |= (tmp1, tmp2) << 2 bytes */
			range = _mm_or_si128 (range, _mm_alignr_epi8(tmp1, tmp2, 14));

			/* Fourth Byte: set range index to saturate_sub(first_len, 2) */
			/* 0 for 00~7F, 0 for C0~DF, 0 for E0~EF, 1 for F0~FF */
			/* tmp1 = saturate_sub(first_len, 2) */
			tmp1 = _mm_subs_epu8 (first_len, _mm_set1_epi8 (2));
			/* tmp2 = saturate_sub(prev_first_len, 2) */
			tmp2 = _mm_subs_epu8 (prev_first_len, _mm_set1_epi8 (2));
			/* range |= (tmp1, tmp2) << 3 bytes */
			range = _mm_or_si128 (range, _mm_alignr_epi8(tmp1, tmp2, 13));

			/*
			 * Now we have below range indices caluclated
			 * Correct cases:
			 * - 8 for C0~FF
			 * - 3 for 1st byte after F0~FF
			 * - 2 for 1st byte after E0~EF or 2nd byte after F0~FF
			 * - 1 for 1st byte after C0~DF or 2nd byte after E0~EF or
			 *         3rd byte after F0~FF
			 * - 0 for others
			 * Error cases:
			 *   9,10,11 if non ascii First Byte overlaps
			 *   E.g., F1 80 C2 90 --> 8 3 10 2, where 10 indicates error
			 */

			/* Adjust Second Byte range for special First Bytes(E0,ED,F0,F4) */
			/* Overlaps lead to index 9~15, which are illegal in range table */
			__m128i shift1, pos, range2;
			/* shift1 = (input, prev_input) << 1 byte */
			shift1 = _mm_alignr_epi8(input, prev_input, 15);
			pos = _mm_sub_epi8 (shift1, _mm_set1_epi8 (0xEF));
			/*
			 * shift1:  | EF  F0 ... FE | FF  00  ... ...  DE | DF  E0 ... EE |
			 * pos:     | 0   1      15 | 16  17           239| 240 241    255|
			 * pos-240: | 0   0      0  | 0   0            0  | 0   1      15 |
			 * pos+112: | 112 113    127|       >= 128        |     >= 128    |
			 */
			tmp1 = _mm_subs_epu8 (pos, _mm_set1_epi8 ((char)240));
			range2 = _mm_shuffle_epi8 (df_ee_tbl, tmp1);
			tmp2 = _mm_adds_epu8 (pos, _mm_set1_epi8 (112));
			range2 = _mm_add_epi8 (range2, _mm_shuffle_epi8 (ef_fe_tbl, tmp2));

			range = _mm_add_epi8 (range, range2);

			/* Load min and max values per calculated range index */
			__m128i minv = _mm_shuffle_epi8 (range_min_tbl, range);
			__m128i maxv = _mm_shuffle_epi8 (range_max_tbl, range);

			/* Check value range */
			error = _mm_cmplt_epi8(input, minv);
			error = _mm_or_si128(error, _mm_cmpgt_epi8(input, maxv));
			/* 5% performance drop from this conditional branch */
			if (!_mm_testz_si128(error, error)) {
				break;
			}

			prev_input = input;
			prev_first_len = first_len;

			data += 16;
			len -= 16;
			err_pos += 16;
		}

		/* Error in first 16 bytes */
		if (err_pos == 1) {
			goto do_naive;
		}

		/* Find previous token (not 80~BF) */
		int32_t token4 = _mm_extract_epi32 (prev_input, 3);
		const int8_t *token = (const int8_t *) &token4;
		int lookahead = 0;

		if (token[3] > (int8_t) 0xBF) {
			lookahead = 1;
		}
		else if (token[2] > (int8_t) 0xBF) {
			lookahead = 2;
		}
		else if (token[1] > (int8_t) 0xBF) {
			lookahead = 3;
		}

		data -= lookahead;
		len += lookahead;
		err_pos -= lookahead;
	}

	do_naive:
	if (len > 0) {
		off_t err_pos2 = rspamd_fast_utf8_validate_ref (data, len);

		if (err_pos2) {
			return err_pos + err_pos2 - 1;
		}
	}

	return 0;
}

#ifndef __clang__
#pragma GCC pop_options
#endif