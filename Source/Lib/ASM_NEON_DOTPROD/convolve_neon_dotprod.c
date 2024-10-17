/*
 * Copyright (c) 2023, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */

#include <arm_neon.h>

#include "common_dsp_rtcd.h"
#include "convolve_neon.h"
#include "definitions.h"
#include "filter.h"
#include "mem_neon.h"
#include "utility.h"

static INLINE int16x4_t convolve4_4_x(const uint8x16_t samples, const int8x8_t filters, const uint8x16_t permute_tbl) {
    // Transform sample range to [-128, 127] for 8-bit signed dot product.
    int8x16_t samples_128 = vreinterpretq_s8_u8(vsubq_u8(samples, vdupq_n_u8(128)));

    // Permute samples ready for dot product.
    // { 0,  1,  2,  3,  1,  2,  3,  4,  2,  3,  4,  5,  3,  4,  5,  6 }
    int8x16_t perm_samples = vqtbl1q_s8(samples_128, permute_tbl);

    // Dot product constants:
    // Accumulate into 128 << FILTER_BITS to account for range transform.
    // Adding a shim of 1 << (ROUND0_BITS - 1) enables us to use a single rounding
    // right shift by FILTER_BITS - instead of a first rounding right shift by
    // ROUND0_BITS, followed by second rounding right shift by FILTER_BITS -
    // ROUND0_BITS. Halve the total because we halved the filter values.
    int32x4_t acc = vdupq_n_s32(((128 << FILTER_BITS) + (1 << ((ROUND0_BITS - 1)))) / 2);
    int32x4_t sum = vdotq_lane_s32(acc, perm_samples, filters, 0);

    // Further narrowing and packing is performed by the caller.
    return vmovn_s32(sum);
}

static INLINE uint8x8_t convolve4_8_x(const uint8x16_t samples, const int8x8_t filters,
                                      const uint8x16x2_t permute_tbl) {
    // Transform sample range to [-128, 127] for 8-bit signed dot product.
    int8x16_t samples_128 = vreinterpretq_s8_u8(vsubq_u8(samples, vdupq_n_u8(128)));

    // Permute samples ready for dot product.
    // { 0,  1,  2,  3,  1,  2,  3,  4,  2,  3,  4,  5,  3,  4,  5,  6 }
    // { 4,  5,  6,  7,  5,  6,  7,  8,  6,  7,  8,  9,  7,  8,  9, 10 }
    int8x16_t perm_samples[2] = {vqtbl1q_s8(samples_128, permute_tbl.val[0]),
                                 vqtbl1q_s8(samples_128, permute_tbl.val[1])};

    // Dot product constants:
    // Accumulate into 128 << FILTER_BITS to account for range transform.
    // Adding a shim of 1 << (ROUND0_BITS - 1) enables us to use a single rounding
    // right shift by FILTER_BITS - instead of a first rounding right shift by
    // ROUND0_BITS, followed by second rounding right shift by FILTER_BITS -
    // ROUND0_BITS. Halve the total because we halved the filter values.
    int32x4_t acc = vdupq_n_s32(((128 << FILTER_BITS) + (1 << ((ROUND0_BITS - 1)))) / 2);

    int32x4_t sum0123 = vdotq_lane_s32(acc, perm_samples[0], filters, 0);
    int32x4_t sum4567 = vdotq_lane_s32(acc, perm_samples[1], filters, 0);

    // Narrow and re-pack.
    int16x8_t sum = vcombine_s16(vmovn_s32(sum0123), vmovn_s32(sum4567));
    // We halved the filter values so -1 from right shift.
    return vqrshrun_n_s16(sum, FILTER_BITS - 1);
}

static INLINE void convolve_x_sr_4tap_neon_dotprod(const uint8_t *src, ptrdiff_t src_stride, uint8_t *dst,
                                                   ptrdiff_t dst_stride, int width, int height,
                                                   const int16_t *filter_x) {
    const int16x4_t x_filter = vld1_s16(filter_x + 2);
    // All 4-tap and bilinear filter values are even, so halve them to reduce
    // intermediate precision requirements.
    const int8x8_t filter = vshrn_n_s16(vcombine_s16(x_filter, vdup_n_s16(0)), 1);

    if (width == 4) {
        const uint8x16_t permute_tbl = vld1q_u8(kDotProdPermuteTbl);

        do {
            uint8x16_t s0, s1, s2, s3;
            load_u8_16x4(src, src_stride, &s0, &s1, &s2, &s3);

            int16x4_t t0 = convolve4_4_x(s0, filter, permute_tbl);
            int16x4_t t1 = convolve4_4_x(s1, filter, permute_tbl);
            int16x4_t t2 = convolve4_4_x(s2, filter, permute_tbl);
            int16x4_t t3 = convolve4_4_x(s3, filter, permute_tbl);
            // We halved the filter values so -1 from right shift.
            uint8x8_t d01 = vqrshrun_n_s16(vcombine_s16(t0, t1), FILTER_BITS - 1);
            uint8x8_t d23 = vqrshrun_n_s16(vcombine_s16(t2, t3), FILTER_BITS - 1);

            store_u8x4_strided_x2(dst + 0 * dst_stride, dst_stride, d01);
            store_u8x4_strided_x2(dst + 2 * dst_stride, dst_stride, d23);

            src += 4 * src_stride;
            dst += 4 * dst_stride;
            height -= 4;
        } while (height != 0);
    } else {
        const uint8x16x2_t permute_tbl = vld1q_u8_x2(kDotProdPermuteTbl);

        do {
            const uint8_t *s = src;
            uint8_t       *d = dst;
            int            w = width;

            do {
                uint8x16_t s0, s1, s2, s3;
                load_u8_16x4(s, src_stride, &s0, &s1, &s2, &s3);

                uint8x8_t d0 = convolve4_8_x(s0, filter, permute_tbl);
                uint8x8_t d1 = convolve4_8_x(s1, filter, permute_tbl);
                uint8x8_t d2 = convolve4_8_x(s2, filter, permute_tbl);
                uint8x8_t d3 = convolve4_8_x(s3, filter, permute_tbl);

                store_u8_8x4(d, dst_stride, d0, d1, d2, d3);

                s += 8;
                d += 8;
                w -= 8;
            } while (w != 0);
            src += 4 * src_stride;
            dst += 4 * dst_stride;
            height -= 4;
        } while (height != 0);
    }
}

static INLINE uint8x8_t convolve8_8_x(uint8x16_t samples, const int8x8_t filter, const uint8x16x3_t permute_tbl) {
    // Transform sample range to [-128, 127] for 8-bit signed dot product.
    int8x16_t samples_128 = vreinterpretq_s8_u8(vsubq_u8(samples, vdupq_n_u8(128)));

    // Permute samples ready for dot product. */
    // { 0,  1,  2,  3,  1,  2,  3,  4,  2,  3,  4,  5,  3,  4,  5,  6 }
    // { 4,  5,  6,  7,  5,  6,  7,  8,  6,  7,  8,  9,  7,  8,  9, 10 }
    // { 8,  9, 10, 11,  9, 10, 11, 12, 10, 11, 12, 13, 11, 12, 13, 14 }
    int8x16_t perm_samples[3] = {vqtbl1q_s8(samples_128, permute_tbl.val[0]),
                                 vqtbl1q_s8(samples_128, permute_tbl.val[1]),
                                 vqtbl1q_s8(samples_128, permute_tbl.val[2])};

    // Dot product constants:
    // Accumulate into 128 << FILTER_BITS to account for range transform.
    // Adding a shim of 1 << (ROUND0_BITS - 1) enables us to use a single rounding
    // right shift by FILTER_BITS - instead of a first rounding right shift by
    // ROUND0_BITS, followed by second rounding right shift by FILTER_BITS -
    // ROUND0_BITS. Halve the total because we halved the filter values.
    int32x4_t acc = vdupq_n_s32(((128 << FILTER_BITS) + (1 << ((ROUND0_BITS - 1)))) / 2);

    int32x4_t sum0123 = vdotq_lane_s32(acc, perm_samples[0], filter, 0);
    sum0123           = vdotq_lane_s32(sum0123, perm_samples[1], filter, 1);

    int32x4_t sum4567 = vdotq_lane_s32(acc, perm_samples[1], filter, 0);
    sum4567           = vdotq_lane_s32(sum4567, perm_samples[2], filter, 1);

    // Narrow and re-pack.
    int16x8_t sum_s16 = vcombine_s16(vmovn_s32(sum0123), vmovn_s32(sum4567));
    // We halved the convolution filter values so - 1 from the right shift.
    return vqrshrun_n_s16(sum_s16, FILTER_BITS - 1);
}

void svt_av1_convolve_x_sr_neon_dotprod(const uint8_t *src, int32_t src_stride, uint8_t *dst, int32_t dst_stride,
                                        int32_t w, int32_t h, InterpFilterParams *filter_params_x,
                                        InterpFilterParams *filter_params_y, const int32_t subpel_x_qn,
                                        const int32_t subpel_y_qn, ConvolveParams *conv_params) {
    if (w == 2 || h == 2) {
        svt_av1_convolve_x_sr_c(src,
                                src_stride,
                                dst,
                                dst_stride,
                                w,
                                h,
                                filter_params_x,
                                filter_params_y,
                                subpel_x_qn,
                                subpel_y_qn,
                                conv_params);
        return;
    }

    const uint8_t horiz_offset = filter_params_x->taps / 2 - 1;
    src -= horiz_offset;

    const int16_t *x_filter_ptr = av1_get_interp_filter_subpel_kernel(*filter_params_x, subpel_x_qn & SUBPEL_MASK);

    int filter_taps = get_filter_tap(filter_params_x, subpel_x_qn & SUBPEL_MASK);

    if (filter_taps == 2 && w > 4) {
        convolve_x_sr_2tap_neon(src + 3, src_stride, dst, dst_stride, w, h, x_filter_ptr);
        return;
    }

    if (filter_taps <= 4) {
        convolve_x_sr_4tap_neon_dotprod(src + 2, src_stride, dst, dst_stride, w, h, x_filter_ptr);
        return;
    }

    const int16x8_t x_filter_s16 = vld1q_s16(x_filter_ptr);

    const uint8x16x3_t permute_tbl = vld1q_u8_x3(kDotProdPermuteTbl);
    // Filter values are even, so halve to reduce intermediate precision reqs.
    const int8x8_t x_filter = vshrn_n_s16(x_filter_s16, 1);

    do {
        int            width = w;
        const uint8_t *s     = src;
        uint8_t       *d     = dst;

        do {
            uint8x16_t s0, s1, s2, s3;
            load_u8_16x4(s, src_stride, &s0, &s1, &s2, &s3);

            uint8x8_t d0 = convolve8_8_x(s0, x_filter, permute_tbl);
            uint8x8_t d1 = convolve8_8_x(s1, x_filter, permute_tbl);
            uint8x8_t d2 = convolve8_8_x(s2, x_filter, permute_tbl);
            uint8x8_t d3 = convolve8_8_x(s3, x_filter, permute_tbl);

            store_u8_8x4(d, dst_stride, d0, d1, d2, d3);

            s += 8;
            d += 8;
            width -= 8;
        } while (width != 0);
        src += 4 * src_stride;
        dst += 4 * dst_stride;
        h -= 4;
    } while (h != 0);
}

static INLINE void transpose_concat_4x4(int8x8_t a0, int8x8_t a1, int8x8_t a2, int8x8_t a3, int8x16_t *b) {
    // Transpose 8-bit elements and concatenate result rows as follows:
    // a0: 00, 01, 02, 03, XX, XX, XX, XX
    // a1: 10, 11, 12, 13, XX, XX, XX, XX
    // a2: 20, 21, 22, 23, XX, XX, XX, XX
    // a3: 30, 31, 32, 33, XX, XX, XX, XX
    //
    // b: 00, 10, 20, 30, 01, 11, 21, 31, 02, 12, 22, 32, 03, 13, 23, 33

    int8x16_t a0q = vcombine_s8(a0, vdup_n_s8(0));
    int8x16_t a1q = vcombine_s8(a1, vdup_n_s8(0));
    int8x16_t a2q = vcombine_s8(a2, vdup_n_s8(0));
    int8x16_t a3q = vcombine_s8(a3, vdup_n_s8(0));

    int8x16_t a02 = vzipq_s8(a0q, a2q).val[0];
    int8x16_t a13 = vzipq_s8(a1q, a3q).val[0];

    *b = vzipq_s8(a02, a13).val[0];
}

static INLINE void transpose_concat_8x4(int8x8_t a0, int8x8_t a1, int8x8_t a2, int8x8_t a3, int8x16_t *b0,
                                        int8x16_t *b1) {
    // Transpose 8-bit elements and concatenate result rows as follows:
    // a0: 00, 01, 02, 03, 04, 05, 06, 07
    // a1: 10, 11, 12, 13, 14, 15, 16, 17
    // a2: 20, 21, 22, 23, 24, 25, 26, 27
    // a3: 30, 31, 32, 33, 34, 35, 36, 37
    //
    // b0: 00, 10, 20, 30, 01, 11, 21, 31, 02, 12, 22, 32, 03, 13, 23, 33
    // b1: 04, 14, 24, 34, 05, 15, 25, 35, 06, 16, 26, 36, 07, 17, 27, 37

    int8x16_t a0q = vcombine_s8(a0, vdup_n_s8(0));
    int8x16_t a1q = vcombine_s8(a1, vdup_n_s8(0));
    int8x16_t a2q = vcombine_s8(a2, vdup_n_s8(0));
    int8x16_t a3q = vcombine_s8(a3, vdup_n_s8(0));

    int8x16_t a02 = vzipq_s8(a0q, a2q).val[0];
    int8x16_t a13 = vzipq_s8(a1q, a3q).val[0];

    int8x16x2_t a0123 = vzipq_s8(a02, a13);

    *b0 = a0123.val[0];
    *b1 = a0123.val[1];
}

static INLINE int16x4_t convolve8_4_y(const int8x16_t s0, const int8x16_t s1, const int8x8_t filters) {
    // The sample range transform and permutation are performed by the caller.
    // Accumulate into 128 << FILTER_BITS to account for range transform.
    const int32x4_t acc = vdupq_n_s32(128 << FILTER_BITS);
    int32x4_t       sum = vdotq_lane_s32(acc, s0, filters, 0);
    sum                 = vdotq_lane_s32(sum, s1, filters, 1);

    // Further narrowing and packing is performed by the caller.
    return vqmovn_s32(sum);
}

static INLINE uint8x8_t convolve8_8_y(const int8x16_t s0_lo, const int8x16_t s0_hi, const int8x16_t s1_lo,
                                      const int8x16_t s1_hi, const int8x8_t filters) {
    // The sample range transform and permutation are performed by the caller.
    // Accumulate into 128 << FILTER_BITS to account for range transform.
    const int32x4_t acc = vdupq_n_s32(128 << FILTER_BITS);

    int32x4_t sum0123 = vdotq_lane_s32(acc, s0_lo, filters, 0);
    sum0123           = vdotq_lane_s32(sum0123, s1_lo, filters, 1);

    int32x4_t sum4567 = vdotq_lane_s32(acc, s0_hi, filters, 0);
    sum4567           = vdotq_lane_s32(sum4567, s1_hi, filters, 1);

    // Narrow and re-pack.
    int16x8_t sum = vcombine_s16(vqmovn_s32(sum0123), vqmovn_s32(sum4567));
    return vqrshrun_n_s16(sum, FILTER_BITS);
}

static INLINE void convolve_y_sr_8tap_neon_dotprod(const uint8_t *src_ptr, int src_stride, uint8_t *dst_ptr,
                                                   int dst_stride, int w, int h, const int16_t *y_filter_ptr) {
    const int8x8_t filter = vmovn_s16(vld1q_s16(y_filter_ptr));

    const uint8x16x3_t merge_block_tbl = vld1q_u8_x3(kDotProdMergeBlockTbl);

    if (w == 4) {
        uint8x8_t t0, t1, t2, t3, t4, t5, t6;
        load_u8_8x7(src_ptr, src_stride, &t0, &t1, &t2, &t3, &t4, &t5, &t6);
        src_ptr += 7 * src_stride;

        // Transform sample range to [-128, 127] for 8-bit signed dot product.
        int8x8_t s0 = vreinterpret_s8_u8(vsub_u8(t0, vdup_n_u8(128)));
        int8x8_t s1 = vreinterpret_s8_u8(vsub_u8(t1, vdup_n_u8(128)));
        int8x8_t s2 = vreinterpret_s8_u8(vsub_u8(t2, vdup_n_u8(128)));
        int8x8_t s3 = vreinterpret_s8_u8(vsub_u8(t3, vdup_n_u8(128)));
        int8x8_t s4 = vreinterpret_s8_u8(vsub_u8(t4, vdup_n_u8(128)));
        int8x8_t s5 = vreinterpret_s8_u8(vsub_u8(t5, vdup_n_u8(128)));
        int8x8_t s6 = vreinterpret_s8_u8(vsub_u8(t6, vdup_n_u8(128)));

        int8x16_t s0123, s1234, s2345, s3456;
        transpose_concat_4x4(s0, s1, s2, s3, &s0123);
        transpose_concat_4x4(s1, s2, s3, s4, &s1234);
        transpose_concat_4x4(s2, s3, s4, s5, &s2345);
        transpose_concat_4x4(s3, s4, s5, s6, &s3456);

        do {
            uint8x8_t t7, t8, t9, t10;
            load_u8_8x4(src_ptr, src_stride, &t7, &t8, &t9, &t10);

            int8x8_t s7  = vreinterpret_s8_u8(vsub_u8(t7, vdup_n_u8(128)));
            int8x8_t s8  = vreinterpret_s8_u8(vsub_u8(t8, vdup_n_u8(128)));
            int8x8_t s9  = vreinterpret_s8_u8(vsub_u8(t9, vdup_n_u8(128)));
            int8x8_t s10 = vreinterpret_s8_u8(vsub_u8(t10, vdup_n_u8(128)));

            int8x16_t s4567, s5678, s6789, s78910;
            transpose_concat_4x4(s7, s8, s9, s10, &s78910);

            // Merge new data into block from previous iteration.
            int8x16x2_t samples_LUT = {{s3456, s78910}};
            s4567                   = vqtbl2q_s8(samples_LUT, merge_block_tbl.val[0]);
            s5678                   = vqtbl2q_s8(samples_LUT, merge_block_tbl.val[1]);
            s6789                   = vqtbl2q_s8(samples_LUT, merge_block_tbl.val[2]);

            int16x4_t d0  = convolve8_4_y(s0123, s4567, filter);
            int16x4_t d1  = convolve8_4_y(s1234, s5678, filter);
            int16x4_t d2  = convolve8_4_y(s2345, s6789, filter);
            int16x4_t d3  = convolve8_4_y(s3456, s78910, filter);
            uint8x8_t d01 = vqrshrun_n_s16(vcombine_s16(d0, d1), FILTER_BITS);
            uint8x8_t d23 = vqrshrun_n_s16(vcombine_s16(d2, d3), FILTER_BITS);

            store_u8x4_strided_x2(dst_ptr + 0 * dst_stride, dst_stride, d01);
            store_u8x4_strided_x2(dst_ptr + 2 * dst_stride, dst_stride, d23);

            // Prepare block for next iteration - re-using as much as possible.
            // Shuffle everything up four rows.
            s0123 = s4567;
            s1234 = s5678;
            s2345 = s6789;
            s3456 = s78910;

            src_ptr += 4 * src_stride;
            dst_ptr += 4 * dst_stride;
            h -= 4;
        } while (h != 0);
    } else {
        do {
            int            height = h;
            const uint8_t *s      = src_ptr;
            uint8_t       *d      = dst_ptr;

            uint8x8_t t0, t1, t2, t3, t4, t5, t6;
            load_u8_8x7(s, src_stride, &t0, &t1, &t2, &t3, &t4, &t5, &t6);
            s += 7 * src_stride;

            // Transform sample range to [-128, 127] for 8-bit signed dot product.
            int8x8_t s0 = vreinterpret_s8_u8(vsub_u8(t0, vdup_n_u8(128)));
            int8x8_t s1 = vreinterpret_s8_u8(vsub_u8(t1, vdup_n_u8(128)));
            int8x8_t s2 = vreinterpret_s8_u8(vsub_u8(t2, vdup_n_u8(128)));
            int8x8_t s3 = vreinterpret_s8_u8(vsub_u8(t3, vdup_n_u8(128)));
            int8x8_t s4 = vreinterpret_s8_u8(vsub_u8(t4, vdup_n_u8(128)));
            int8x8_t s5 = vreinterpret_s8_u8(vsub_u8(t5, vdup_n_u8(128)));
            int8x8_t s6 = vreinterpret_s8_u8(vsub_u8(t6, vdup_n_u8(128)));

            // This operation combines a conventional transpose and the sample
            // permute (see horizontal case) required before computing the dot
            // product.
            int8x16_t s0123_lo, s0123_hi, s1234_lo, s1234_hi, s2345_lo, s2345_hi, s3456_lo, s3456_hi;
            transpose_concat_8x4(s0, s1, s2, s3, &s0123_lo, &s0123_hi);
            transpose_concat_8x4(s1, s2, s3, s4, &s1234_lo, &s1234_hi);
            transpose_concat_8x4(s2, s3, s4, s5, &s2345_lo, &s2345_hi);
            transpose_concat_8x4(s3, s4, s5, s6, &s3456_lo, &s3456_hi);

            do {
                uint8x8_t t7, t8, t9, t10;
                load_u8_8x4(s, src_stride, &t7, &t8, &t9, &t10);

                int8x8_t s7  = vreinterpret_s8_u8(vsub_u8(t7, vdup_n_u8(128)));
                int8x8_t s8  = vreinterpret_s8_u8(vsub_u8(t8, vdup_n_u8(128)));
                int8x8_t s9  = vreinterpret_s8_u8(vsub_u8(t9, vdup_n_u8(128)));
                int8x8_t s10 = vreinterpret_s8_u8(vsub_u8(t10, vdup_n_u8(128)));

                int8x16_t s4567_lo, s4567_hi, s5678_lo, s5678_hi, s6789_lo, s6789_hi, s78910_lo, s78910_hi;
                transpose_concat_8x4(s7, s8, s9, s10, &s78910_lo, &s78910_hi);

                // Merge new data into block from previous iteration.
                int8x16x2_t samples_LUT_lo = {{s3456_lo, s78910_lo}};
                s4567_lo                   = vqtbl2q_s8(samples_LUT_lo, merge_block_tbl.val[0]);
                s5678_lo                   = vqtbl2q_s8(samples_LUT_lo, merge_block_tbl.val[1]);
                s6789_lo                   = vqtbl2q_s8(samples_LUT_lo, merge_block_tbl.val[2]);

                int8x16x2_t samples_LUT_hi = {{s3456_hi, s78910_hi}};
                s4567_hi                   = vqtbl2q_s8(samples_LUT_hi, merge_block_tbl.val[0]);
                s5678_hi                   = vqtbl2q_s8(samples_LUT_hi, merge_block_tbl.val[1]);
                s6789_hi                   = vqtbl2q_s8(samples_LUT_hi, merge_block_tbl.val[2]);

                uint8x8_t d0 = convolve8_8_y(s0123_lo, s0123_hi, s4567_lo, s4567_hi, filter);
                uint8x8_t d1 = convolve8_8_y(s1234_lo, s1234_hi, s5678_lo, s5678_hi, filter);
                uint8x8_t d2 = convolve8_8_y(s2345_lo, s2345_hi, s6789_lo, s6789_hi, filter);
                uint8x8_t d3 = convolve8_8_y(s3456_lo, s3456_hi, s78910_lo, s78910_hi, filter);

                store_u8_8x4(d, dst_stride, d0, d1, d2, d3);

                // Prepare block for next iteration - re-using as much as possible.
                // Shuffle everything up four rows.
                s0123_lo = s4567_lo;
                s0123_hi = s4567_hi;
                s1234_lo = s5678_lo;
                s1234_hi = s5678_hi;
                s2345_lo = s6789_lo;
                s2345_hi = s6789_hi;
                s3456_lo = s78910_lo;
                s3456_hi = s78910_hi;

                s += 4 * src_stride;
                d += 4 * dst_stride;
                height -= 4;
            } while (height != 0);
            src_ptr += 8;
            dst_ptr += 8;
            w -= 8;
        } while (w != 0);
    }
}

void svt_av1_convolve_y_sr_neon_dotprod(const uint8_t *src, int src_stride, uint8_t *dst, int dst_stride, int w, int h,
                                        InterpFilterParams *filter_params_x, InterpFilterParams *filter_params_y,
                                        const int subpel_x_qn, const int subpel_y_qn, ConvolveParams *conv_params) {
    if (w == 2 || h == 2) {
        svt_av1_convolve_y_sr_c(src,
                                src_stride,
                                dst,
                                dst_stride,
                                w,
                                h,
                                filter_params_x,
                                filter_params_y,
                                subpel_x_qn,
                                subpel_y_qn,
                                conv_params);
        return;
    }

    const int y_filter_taps = get_filter_tap(filter_params_y, subpel_y_qn);

    if (y_filter_taps <= 6) {
        svt_av1_convolve_y_sr_neon(src,
                                   src_stride,
                                   dst,
                                   dst_stride,
                                   w,
                                   h,
                                   filter_params_x,
                                   filter_params_y,
                                   subpel_x_qn,
                                   subpel_y_qn,
                                   conv_params);
        return;
    }

    const int vert_offset = y_filter_taps / 2 - 1;
    src -= vert_offset * src_stride;

    const int16_t *y_filter_ptr = av1_get_interp_filter_subpel_kernel(*filter_params_y, subpel_y_qn & SUBPEL_MASK);

    convolve_y_sr_8tap_neon_dotprod(src, src_stride, dst, dst_stride, w, h, y_filter_ptr);
}

static INLINE int16x4_t convolve4_4_2d_h(const uint8x16_t samples, const int8x8_t filters, const uint8x16_t permute_tbl,
                                         const int32x4_t correction) {
    // Transform sample range to [-128, 127] for 8-bit signed dot product.
    int8x16_t samples_128 = vreinterpretq_s8_u8(vsubq_u8(samples, vdupq_n_u8(128)));

    // Permute samples ready for dot product.
    // { 0,  1,  2,  3,  1,  2,  3,  4,  2,  3,  4,  5,  3,  4,  5,  6 }
    int8x16_t perm_samples = vqtbl1q_s8(samples_128, permute_tbl);

    // Accumulate into 'correction' to account for range transform.
    int32x4_t sum = vdotq_lane_s32(correction, perm_samples, filters, 0);

    // We halved the convolution filter values so -1 from the right shift.
    return vshrn_n_s32(sum, ROUND0_BITS - 1);
}

static INLINE int16x8_t convolve4_8_2d_h(const uint8x16_t samples, const int8x8_t filters,
                                         const uint8x16x2_t permute_tbl, const int32x4_t correction) {
    // Transform sample range to [-128, 127] for 8-bit signed dot product.
    int8x16_t samples_128 = vreinterpretq_s8_u8(vsubq_u8(samples, vdupq_n_u8(128)));

    // Permute samples ready for dot product.
    // { 0,  1,  2,  3,  1,  2,  3,  4,  2,  3,  4,  5,  3,  4,  5,  6 }
    // { 4,  5,  6,  7,  5,  6,  7,  8,  6,  7,  8,  9,  7,  8,  9, 10 }
    int8x16_t perm_samples[2] = {vqtbl1q_s8(samples_128, permute_tbl.val[0]),
                                 vqtbl1q_s8(samples_128, permute_tbl.val[1])};

    // Accumulate into 'correction' to account for range transform.
    int32x4_t sum0123 = vdotq_lane_s32(correction, perm_samples[0], filters, 0);
    int32x4_t sum4567 = vdotq_lane_s32(correction, perm_samples[1], filters, 0);

    // Narrow and re-pack.
    // We halved the filter values so -1 from right shift.
    return vcombine_s16(vshrn_n_s32(sum0123, ROUND0_BITS - 1), vshrn_n_s32(sum4567, ROUND0_BITS - 1));
}

static INLINE void convolve_2d_sr_horiz_4tap_neon_dotprod(const uint8_t *src, ptrdiff_t src_stride, int16_t *dst,
                                                          ptrdiff_t dst_stride, int w, int h, const int16_t *filter_x) {
    const int       bd       = 8;
    const int16x4_t x_filter = vld1_s16(filter_x + 2);
    // All 4-tap and bilinear filter values are even, so halve them to reduce
    // intermediate precision requirements.
    const int8x8_t filter = vshrn_n_s16(vcombine_s16(x_filter, vdup_n_s16(0)), 1);

    // Adding a shim of 1 << (ROUND0_BITS - 1) enables us to use non-rounding
    // shifts - which are generally faster than rounding shifts on modern CPUs.
    const int32_t horiz_const = ((1 << (bd + FILTER_BITS - 1)) + (1 << (ROUND0_BITS - 1)));
    // Accumulate into 128 << FILTER_BITS to account for range transform.
    // Halve the total because we halved the filter values.
    const int32x4_t correction = vdupq_n_s32(((128 << FILTER_BITS) + horiz_const) / 2);

    if (w == 4) {
        const uint8x16_t permute_tbl = vld1q_u8(kDotProdPermuteTbl);

        do {
            uint8x16_t s0, s1, s2, s3;
            load_u8_16x4(src, src_stride, &s0, &s1, &s2, &s3);

            int16x4_t d0 = convolve4_4_2d_h(s0, filter, permute_tbl, correction);
            int16x4_t d1 = convolve4_4_2d_h(s1, filter, permute_tbl, correction);
            int16x4_t d2 = convolve4_4_2d_h(s2, filter, permute_tbl, correction);
            int16x4_t d3 = convolve4_4_2d_h(s3, filter, permute_tbl, correction);

            store_s16_4x4(dst, dst_stride, d0, d1, d2, d3);

            src += 4 * src_stride;
            dst += 4 * dst_stride;
            h -= 4;
        } while (h > 4);

        do {
            uint8x16_t s0 = vld1q_u8(src);
            int16x4_t  d0 = convolve4_4_2d_h(s0, filter, permute_tbl, correction);
            vst1_s16(dst, d0);

            src += src_stride;
            dst += dst_stride;
        } while (--h != 0);
    } else {
        const uint8x16x2_t permute_tbl = vld1q_u8_x2(kDotProdPermuteTbl);
        do {
            const uint8_t *s     = src;
            int16_t       *d     = dst;
            int            width = w;

            do {
                uint8x16_t s0, s1, s2, s3;
                load_u8_16x4(s, src_stride, &s0, &s1, &s2, &s3);

                int16x8_t d0 = convolve4_8_2d_h(s0, filter, permute_tbl, correction);
                int16x8_t d1 = convolve4_8_2d_h(s1, filter, permute_tbl, correction);
                int16x8_t d2 = convolve4_8_2d_h(s2, filter, permute_tbl, correction);
                int16x8_t d3 = convolve4_8_2d_h(s3, filter, permute_tbl, correction);

                store_s16_8x4(d, dst_stride, d0, d1, d2, d3);

                s += 8;
                d += 8;
                width -= 8;
            } while (width != 0);
            src += 4 * src_stride;
            dst += 4 * dst_stride;
            h -= 4;
        } while (h > 4);

        do {
            const uint8_t *s     = src;
            int16_t       *d     = dst;
            int            width = w;

            do {
                uint8x16_t s0 = vld1q_u8(s);
                int16x8_t  d0 = convolve4_8_2d_h(s0, filter, permute_tbl, correction);
                vst1q_s16(d, d0);

                s += 8;
                d += 8;
                width -= 8;
            } while (width != 0);
            src += src_stride;
            dst += dst_stride;
        } while (--h != 0);
    }
}

static INLINE int16x8_t convolve8_8_2d_h(uint8x16_t samples, const int8x8_t filters, const int32x4_t correction,
                                         const uint8x16x3_t permute_tbl) {
    // Transform sample range to [-128, 127] for 8-bit signed dot product.
    int8x16_t samples_128 = vreinterpretq_s8_u8(vsubq_u8(samples, vdupq_n_u8(128)));

    // Permute samples ready for dot product.
    // { 0,  1,  2,  3,  1,  2,  3,  4,  2,  3,  4,  5,  3,  4,  5,  6 }
    // { 4,  5,  6,  7,  5,  6,  7,  8,  6,  7,  8,  9,  7,  8,  9, 10 }
    // { 8,  9, 10, 11,  9, 10, 11, 12, 10, 11, 12, 13, 11, 12, 13, 14 }
    int8x16_t perm_samples[3] = {vqtbl1q_s8(samples_128, permute_tbl.val[0]),
                                 vqtbl1q_s8(samples_128, permute_tbl.val[1]),
                                 vqtbl1q_s8(samples_128, permute_tbl.val[2])};

    // Accumulate dot product into 'correction' to account for range transform.
    int32x4_t sum0123 = vdotq_lane_s32(correction, perm_samples[0], filters, 0);
    sum0123           = vdotq_lane_s32(sum0123, perm_samples[1], filters, 1);

    int32x4_t sum4567 = vdotq_lane_s32(correction, perm_samples[1], filters, 0);
    sum4567           = vdotq_lane_s32(sum4567, perm_samples[2], filters, 1);

    // Narrow and re-pack.
    // We halved the convolution filter values so -1 from the right shift.
    return vcombine_s16(vshrn_n_s32(sum0123, ROUND0_BITS - 1), vshrn_n_s32(sum4567, ROUND0_BITS - 1));
}

static INLINE void convolve_2d_sr_horiz_8tap_neon_dotprod(const uint8_t *src, int src_stride, int16_t *im_block,
                                                          int im_stride, int w, int im_h, const int16_t *x_filter_ptr) {
    const int16x8_t x_filter_s16 = vld1q_s16(x_filter_ptr);
    // Filter values are even, so halve to reduce intermediate precision reqs.
    const int8x8_t x_filter = vshrn_n_s16(x_filter_s16, 1);

    const int bd = 8;
    // Adding a shim of 1 << (ROUND0_BITS - 1) enables us to use non-rounding
    // shifts - which are generally faster than rounding shifts on modern CPUs.
    const int32_t horiz_const = ((1 << (bd + FILTER_BITS - 1)) + (1 << (ROUND0_BITS - 1)));
    // Halve the total because we halved the filter values.
    const int32x4_t correction = vdupq_n_s32(((128 << FILTER_BITS) + horiz_const) / 2);

    const uint8_t *src_ptr    = src;
    int16_t       *dst_ptr    = im_block;
    int            dst_stride = im_stride;
    int            height     = im_h;

    const uint8x16x3_t permute_tbl = vld1q_u8_x3(kDotProdPermuteTbl);
    do {
        const uint8_t *s     = src_ptr;
        int16_t       *d     = dst_ptr;
        int            width = w;

        do {
            uint8x16_t s0, s1, s2, s3;
            load_u8_16x4(s, src_stride, &s0, &s1, &s2, &s3);

            int16x8_t d0 = convolve8_8_2d_h(s0, x_filter, correction, permute_tbl);
            int16x8_t d1 = convolve8_8_2d_h(s1, x_filter, correction, permute_tbl);
            int16x8_t d2 = convolve8_8_2d_h(s2, x_filter, correction, permute_tbl);
            int16x8_t d3 = convolve8_8_2d_h(s3, x_filter, correction, permute_tbl);

            store_s16_8x4(d, dst_stride, d0, d1, d2, d3);

            s += 8;
            d += 8;
            width -= 8;
        } while (width != 0);
        src_ptr += 4 * src_stride;
        dst_ptr += 4 * dst_stride;
        height -= 4;
    } while (height > 4);

    do {
        const uint8_t *s     = src_ptr;
        int16_t       *d     = dst_ptr;
        int            width = w;

        do {
            uint8x16_t s0 = vld1q_u8(s);
            int16x8_t  d0 = convolve8_8_2d_h(s0, x_filter, correction, permute_tbl);
            vst1q_s16(d, d0);

            s += 8;
            d += 8;
            width -= 8;
        } while (width != 0);
        src_ptr += src_stride;
        dst_ptr += dst_stride;
    } while (--height != 0);
}

static INLINE void convolve_2d_sr_6tap_neon_dotprod(const uint8_t *src, int src_stride, uint8_t *dst, int dst_stride,
                                                    int w, int h, const int16_t *x_filter_ptr,
                                                    const int16_t *y_filter_ptr) {
    const int16x8_t y_filter = vld1q_s16(y_filter_ptr);
    // Filter values are even, so halve to reduce intermediate precision reqs.
    const int8x8_t x_filter = vshrn_n_s16(vld1q_s16(x_filter_ptr), 1);

    const int bd = 8;
    // Adding a shim of 1 << (ROUND0_BITS - 1) enables us to use non-rounding
    // shifts - which are generally faster than rounding shifts on modern CPUs.
    const int32_t horiz_const = ((1 << (bd + FILTER_BITS - 1)) + (1 << (ROUND0_BITS - 1)));
    // Accumulate into 128 << FILTER_BITS to account for range transform.
    // Halve the total because we halved the filter values.
    const int32x4_t    correction  = vdupq_n_s32(((128 << FILTER_BITS) + horiz_const) / 2);
    const int16x8_t    vert_const  = vdupq_n_s16(1 << (bd - 1));
    const uint8x16x3_t permute_tbl = vld1q_u8_x3(kDotProdPermuteTbl);

    do {
        const uint8_t *s      = src;
        uint8_t       *d      = dst;
        int            height = h;

        uint8x16_t h_s0, h_s1, h_s2, h_s3, h_s4;
        load_u8_16x5(s, src_stride, &h_s0, &h_s1, &h_s2, &h_s3, &h_s4);
        s += 5 * src_stride;

        int16x8_t v_s0 = convolve8_8_2d_h(h_s0, x_filter, correction, permute_tbl);
        int16x8_t v_s1 = convolve8_8_2d_h(h_s1, x_filter, correction, permute_tbl);
        int16x8_t v_s2 = convolve8_8_2d_h(h_s2, x_filter, correction, permute_tbl);
        int16x8_t v_s3 = convolve8_8_2d_h(h_s3, x_filter, correction, permute_tbl);
        int16x8_t v_s4 = convolve8_8_2d_h(h_s4, x_filter, correction, permute_tbl);

        do {
            uint8x16_t h_s5, h_s6, h_s7, h_s8;
            load_u8_16x4(s, src_stride, &h_s5, &h_s6, &h_s7, &h_s8);

            int16x8_t v_s5 = convolve8_8_2d_h(h_s5, x_filter, correction, permute_tbl);
            int16x8_t v_s6 = convolve8_8_2d_h(h_s6, x_filter, correction, permute_tbl);
            int16x8_t v_s7 = convolve8_8_2d_h(h_s7, x_filter, correction, permute_tbl);
            int16x8_t v_s8 = convolve8_8_2d_h(h_s8, x_filter, correction, permute_tbl);

            uint8x8_t d0 = convolve6_8_2d_v(v_s0, v_s1, v_s2, v_s3, v_s4, v_s5, y_filter, vert_const);
            uint8x8_t d1 = convolve6_8_2d_v(v_s1, v_s2, v_s3, v_s4, v_s5, v_s6, y_filter, vert_const);
            uint8x8_t d2 = convolve6_8_2d_v(v_s2, v_s3, v_s4, v_s5, v_s6, v_s7, y_filter, vert_const);
            uint8x8_t d3 = convolve6_8_2d_v(v_s3, v_s4, v_s5, v_s6, v_s7, v_s8, y_filter, vert_const);

            store_u8_8x4(d, dst_stride, d0, d1, d2, d3);

            v_s0 = v_s4;
            v_s1 = v_s5;
            v_s2 = v_s6;
            v_s3 = v_s7;
            v_s4 = v_s8;

            s += 4 * src_stride;
            d += 4 * dst_stride;
            height -= 4;
        } while (height != 0);
        src += 8;
        dst += 8;
        w -= 8;
    } while (w != 0);
}

static INLINE void convolve_2d_sr_4tap_neon_dotprod(const uint8_t *src, int src_stride, uint8_t *dst, int dst_stride,
                                                    int w, int h, const int16_t *x_filter_ptr,
                                                    const int16_t *y_filter_ptr) {
    const int       bd         = 8;
    const int16x8_t vert_const = vdupq_n_s16(1 << (bd - 1));

    const int16x4_t y_filter     = vld1_s16(y_filter_ptr + 2);
    const int16x4_t x_filter_s16 = vld1_s16(x_filter_ptr + 2);
    // All 4-tap and bilinear filter values are even, so halve them to reduce
    // intermediate precision requirements.
    const int8x8_t x_filter = vshrn_n_s16(vcombine_s16(x_filter_s16, vdup_n_s16(0)), 1);

    // Adding a shim of 1 << (ROUND0_BITS - 1) enables us to use non-rounding
    // shifts - which are generally faster than rounding shifts on modern CPUs.
    const int32_t horiz_const = ((1 << (bd + FILTER_BITS - 1)) + (1 << (ROUND0_BITS - 1)));
    // Accumulate into 128 << FILTER_BITS to account for range transform.
    // Halve the total because we halved the filter values.
    const int32x4_t correction = vdupq_n_s32(((128 << FILTER_BITS) + horiz_const) / 2);

    if (w == 4) {
        const uint8x16_t permute_tbl = vld1q_u8(kDotProdPermuteTbl);

        uint8x16_t h_s0, h_s1, h_s2;
        load_u8_16x3(src, src_stride, &h_s0, &h_s1, &h_s2);

        int16x4_t v_s0 = convolve4_4_2d_h(h_s0, x_filter, permute_tbl, correction);
        int16x4_t v_s1 = convolve4_4_2d_h(h_s1, x_filter, permute_tbl, correction);
        int16x4_t v_s2 = convolve4_4_2d_h(h_s2, x_filter, permute_tbl, correction);

        src += 3 * src_stride;

        do {
            uint8x16_t h_s3, h_s4, h_s5, h_s6;
            load_u8_16x4(src, src_stride, &h_s3, &h_s4, &h_s5, &h_s6);

            int16x4_t v_s3 = convolve4_4_2d_h(h_s3, x_filter, permute_tbl, correction);
            int16x4_t v_s4 = convolve4_4_2d_h(h_s4, x_filter, permute_tbl, correction);
            int16x4_t v_s5 = convolve4_4_2d_h(h_s5, x_filter, permute_tbl, correction);
            int16x4_t v_s6 = convolve4_4_2d_h(h_s6, x_filter, permute_tbl, correction);

            int16x4_t d0 = convolve4_4_2d_v(v_s0, v_s1, v_s2, v_s3, y_filter);
            int16x4_t d1 = convolve4_4_2d_v(v_s1, v_s2, v_s3, v_s4, y_filter);
            int16x4_t d2 = convolve4_4_2d_v(v_s2, v_s3, v_s4, v_s5, y_filter);
            int16x4_t d3 = convolve4_4_2d_v(v_s3, v_s4, v_s5, v_s6, y_filter);

            uint8x8_t d01 = vqmovun_s16(vsubq_s16(vcombine_s16(d0, d1), vert_const));
            uint8x8_t d23 = vqmovun_s16(vsubq_s16(vcombine_s16(d2, d3), vert_const));

            store_u8x4_strided_x2(dst + 0 * dst_stride, dst_stride, d01);
            store_u8x4_strided_x2(dst + 2 * dst_stride, dst_stride, d23);

            v_s0 = v_s4;
            v_s1 = v_s5;
            v_s2 = v_s6;

            src += 4 * src_stride;
            dst += 4 * dst_stride;
            h -= 4;
        } while (h != 0);
    } else {
        const uint8x16x2_t permute_tbl = vld1q_u8_x2(kDotProdPermuteTbl);

        do {
            int            height = h;
            const uint8_t *s      = src;
            uint8_t       *d      = dst;

            uint8x16_t h_s0, h_s1, h_s2;
            load_u8_16x3(src, src_stride, &h_s0, &h_s1, &h_s2);

            int16x8_t v_s0 = convolve4_8_2d_h(h_s0, x_filter, permute_tbl, correction);
            int16x8_t v_s1 = convolve4_8_2d_h(h_s1, x_filter, permute_tbl, correction);
            int16x8_t v_s2 = convolve4_8_2d_h(h_s2, x_filter, permute_tbl, correction);

            s += 3 * src_stride;

            do {
                uint8x16_t h_s3, h_s4, h_s5, h_s6;
                load_u8_16x4(s, src_stride, &h_s3, &h_s4, &h_s5, &h_s6);

                int16x8_t v_s3 = convolve4_8_2d_h(h_s3, x_filter, permute_tbl, correction);
                int16x8_t v_s4 = convolve4_8_2d_h(h_s4, x_filter, permute_tbl, correction);
                int16x8_t v_s5 = convolve4_8_2d_h(h_s5, x_filter, permute_tbl, correction);
                int16x8_t v_s6 = convolve4_8_2d_h(h_s6, x_filter, permute_tbl, correction);

                uint8x8_t d0 = convolve4_8_2d_v(v_s0, v_s1, v_s2, v_s3, y_filter, vert_const);
                uint8x8_t d1 = convolve4_8_2d_v(v_s1, v_s2, v_s3, v_s4, y_filter, vert_const);
                uint8x8_t d2 = convolve4_8_2d_v(v_s2, v_s3, v_s4, v_s5, y_filter, vert_const);
                uint8x8_t d3 = convolve4_8_2d_v(v_s3, v_s4, v_s5, v_s6, y_filter, vert_const);

                store_u8_8x4(d, dst_stride, d0, d1, d2, d3);

                v_s0 = v_s4;
                v_s1 = v_s5;
                v_s2 = v_s6;

                s += 4 * src_stride;
                d += 4 * dst_stride;
                height -= 4;
            } while (height != 0);
            src += 8;
            dst += 8;
            w -= 8;
        } while (w != 0);
    }
}

void svt_av1_convolve_2d_sr_neon_dotprod(const uint8_t *src, int src_stride, uint8_t *dst, int dst_stride, int w, int h,
                                         InterpFilterParams *filter_params_x, InterpFilterParams *filter_params_y,
                                         const int subpel_x_qn, const int subpel_y_qn, ConvolveParams *conv_params) {
    if (w == 2 || h == 2) {
        svt_av1_convolve_2d_sr_c(src,
                                 src_stride,
                                 dst,
                                 dst_stride,
                                 w,
                                 h,
                                 filter_params_x,
                                 filter_params_y,
                                 subpel_x_qn,
                                 subpel_y_qn,
                                 conv_params);
        return;
    }

    const int      y_filter_taps  = get_filter_tap(filter_params_y, subpel_y_qn);
    const int      x_filter_taps  = get_filter_tap(filter_params_x, subpel_x_qn);
    const int      clamped_y_taps = y_filter_taps < 4 ? 4 : y_filter_taps;
    const int      im_h           = h + clamped_y_taps - 1;
    const int      im_stride      = MAX_SB_SIZE;
    const int      vert_offset    = clamped_y_taps / 2 - 1;
    const int      horiz_offset   = filter_params_x->taps / 2 - 1;
    const uint8_t *src_ptr        = src - vert_offset * src_stride - horiz_offset;

    const int16_t *x_filter_ptr = av1_get_interp_filter_subpel_kernel(*filter_params_x, subpel_x_qn & SUBPEL_MASK);
    const int16_t *y_filter_ptr = av1_get_interp_filter_subpel_kernel(*filter_params_y, subpel_y_qn & SUBPEL_MASK);

    if (x_filter_taps == 2 && y_filter_taps == 2 && w > 4) {
        convolve_2d_sr_2tap_neon(src, src_stride, dst, dst_stride, w, h, x_filter_ptr, y_filter_ptr);
        return;
    }

    if (x_filter_taps >= 6 && y_filter_taps == 6) {
        convolve_2d_sr_6tap_neon_dotprod(src_ptr, src_stride, dst, dst_stride, w, h, x_filter_ptr, y_filter_ptr);
        return;
    }

    if (x_filter_taps <= 4 && y_filter_taps <= 4) {
        convolve_2d_sr_4tap_neon_dotprod(src_ptr + 2, src_stride, dst, dst_stride, w, h, x_filter_ptr, y_filter_ptr);
        return;
    }

    DECLARE_ALIGNED(16, int16_t, im_block[(MAX_SB_SIZE + SUBPEL_TAPS - 1) * MAX_SB_SIZE]);

    if (x_filter_taps <= 4) {
        convolve_2d_sr_horiz_4tap_neon_dotprod(src_ptr + 2, src_stride, im_block, im_stride, w, im_h, x_filter_ptr);
    } else {
        convolve_2d_sr_horiz_8tap_neon_dotprod(src_ptr, src_stride, im_block, im_stride, w, im_h, x_filter_ptr);
    }

    const int16x8_t y_filter = vld1q_s16(y_filter_ptr);

    if (clamped_y_taps <= 4) {
        convolve_2d_sr_vert_4tap_neon(im_block, im_stride, dst, dst_stride, w, h, y_filter_ptr);
    } else if (clamped_y_taps == 6) {
        convolve_2d_sr_vert_6tap_neon(im_block, im_stride, dst, dst_stride, w, h, y_filter);
    } else {
        convolve_2d_sr_vert_8tap_neon(im_block, im_stride, dst, dst_stride, w, h, y_filter);
    }
}
