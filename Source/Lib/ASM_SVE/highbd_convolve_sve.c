/*
 * Copyright (c) 2024, Alliance for Open Media. All rights reserved
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
#include "definitions.h"
#include "filter.h"
#include "inter_prediction.h"
#include "mem_neon.h"
#include "neon_sve_bridge.h"
#include "sum_neon.h"
#include "utility.h"

DECLARE_ALIGNED(16, static const uint16_t, kDotProdTbl[32]) = {
    // clang-format off
    0, 1, 2, 3, 1, 2, 3, 4, 2, 3, 4, 5, 3, 4, 5, 6,
    4, 5, 6, 7, 5, 6, 7, 0, 6, 7, 0, 1, 7, 0, 1, 2,
    // clang-format on
};

static INLINE uint16x8_t convolve8_8_x(int16x8_t s0[8], int16x8_t filter, int64x2_t offset, uint16x8_t max) {
    int64x2_t sum[8];
    sum[0] = svt_sdotq_s16(offset, s0[0], filter);
    sum[1] = svt_sdotq_s16(offset, s0[1], filter);
    sum[2] = svt_sdotq_s16(offset, s0[2], filter);
    sum[3] = svt_sdotq_s16(offset, s0[3], filter);
    sum[4] = svt_sdotq_s16(offset, s0[4], filter);
    sum[5] = svt_sdotq_s16(offset, s0[5], filter);
    sum[6] = svt_sdotq_s16(offset, s0[6], filter);
    sum[7] = svt_sdotq_s16(offset, s0[7], filter);

    sum[0] = vpaddq_s64(sum[0], sum[1]);
    sum[2] = vpaddq_s64(sum[2], sum[3]);
    sum[4] = vpaddq_s64(sum[4], sum[5]);
    sum[6] = vpaddq_s64(sum[6], sum[7]);

    int32x4_t sum0123 = vcombine_s32(vmovn_s64(sum[0]), vmovn_s64(sum[2]));
    int32x4_t sum4567 = vcombine_s32(vmovn_s64(sum[4]), vmovn_s64(sum[6]));

    uint16x8_t res = vcombine_u16(vqrshrun_n_s32(sum0123, FILTER_BITS), vqrshrun_n_s32(sum4567, FILTER_BITS));

    return vminq_u16(res, max);
}

static INLINE void highbd_convolve_x_sr_8tap_sve(const uint16_t *src, int src_stride, uint16_t *dst, int dst_stride,
                                                 int width, int height, const int16_t *y_filter_ptr, int bd) {
    const uint16x8_t max = vdupq_n_u16((1 << bd) - 1);
    // This shim allows to do only one rounding shift instead of two.
    const int64x2_t offset = vcombine_s64(vdup_n_s64(1 << (ROUND0_BITS - 1)), vdup_n_s64(0));

    const int16x8_t filter = vld1q_s16(y_filter_ptr);

    do {
        const int16_t *s = (const int16_t *)src;
        uint16_t      *d = dst;
        int            w = width;

        do {
            int16x8_t s0[8], s1[8], s2[8], s3[8];
            load_s16_8x8(s + 0 * src_stride, 1, &s0[0], &s0[1], &s0[2], &s0[3], &s0[4], &s0[5], &s0[6], &s0[7]);
            load_s16_8x8(s + 1 * src_stride, 1, &s1[0], &s1[1], &s1[2], &s1[3], &s1[4], &s1[5], &s1[6], &s1[7]);
            load_s16_8x8(s + 2 * src_stride, 1, &s2[0], &s2[1], &s2[2], &s2[3], &s2[4], &s2[5], &s2[6], &s2[7]);
            load_s16_8x8(s + 3 * src_stride, 1, &s3[0], &s3[1], &s3[2], &s3[3], &s3[4], &s3[5], &s3[6], &s3[7]);

            uint16x8_t d0 = convolve8_8_x(s0, filter, offset, max);
            uint16x8_t d1 = convolve8_8_x(s1, filter, offset, max);
            uint16x8_t d2 = convolve8_8_x(s2, filter, offset, max);
            uint16x8_t d3 = convolve8_8_x(s3, filter, offset, max);

            store_u16_8x4(d, dst_stride, d0, d1, d2, d3);

            s += 8;
            d += 8;
            w -= 8;
        } while (w != 0);
        src += 4 * src_stride;
        dst += 4 * dst_stride;
        height -= 4;
    } while (height != 0);
}

// clang-format off
DECLARE_ALIGNED(16, static const uint16_t, kDeinterleaveTbl[8]) = {
  0, 2, 4, 6, 1, 3, 5, 7,
};
// clang-format on

static INLINE uint16x4_t convolve4_4_x(int16x8_t s0, int16x8_t filter, int64x2_t offset, uint16x8x2_t permute_tbl,
                                       uint16x4_t max) {
    int16x8_t permuted_samples0 = svt_tbl_s16(s0, permute_tbl.val[0]);
    int16x8_t permuted_samples1 = svt_tbl_s16(s0, permute_tbl.val[1]);

    int64x2_t sum01 = svt_svdot_lane_s16(offset, permuted_samples0, filter, 0);
    int64x2_t sum23 = svt_svdot_lane_s16(offset, permuted_samples1, filter, 0);

    int32x4_t  sum0123 = vcombine_s32(vmovn_s64(sum01), vmovn_s64(sum23));
    uint16x4_t res     = vqrshrun_n_s32(sum0123, FILTER_BITS);

    return vmin_u16(res, max);
}

static INLINE uint16x8_t convolve4_8_x(int16x8_t s0[4], int16x8_t filter, int64x2_t offset, uint16x8_t tbl,
                                       uint16x8_t max) {
    int64x2_t sum04 = svt_svdot_lane_s16(offset, s0[0], filter, 0);
    int64x2_t sum15 = svt_svdot_lane_s16(offset, s0[1], filter, 0);
    int64x2_t sum26 = svt_svdot_lane_s16(offset, s0[2], filter, 0);
    int64x2_t sum37 = svt_svdot_lane_s16(offset, s0[3], filter, 0);

    int32x4_t sum0415 = vcombine_s32(vmovn_s64(sum04), vmovn_s64(sum15));
    int32x4_t sum2637 = vcombine_s32(vmovn_s64(sum26), vmovn_s64(sum37));

    uint16x8_t res = vcombine_u16(vqrshrun_n_s32(sum0415, FILTER_BITS), vqrshrun_n_s32(sum2637, FILTER_BITS));
    res            = svt_tbl_u16(res, tbl);

    return vminq_u16(res, max);
}

static INLINE void highbd_convolve_x_sr_4tap_sve(const uint16_t *src, int src_stride, uint16_t *dst, int dst_stride,
                                                 int width, int height, const int16_t *x_filter_ptr, int bd) {
    // This shim allows to do only one rounding shift instead of two.
    const int64x2_t offset = vdupq_n_s64(1 << (ROUND0_BITS - 1));

    const int16x4_t x_filter = vld1_s16(x_filter_ptr + 2);
    const int16x8_t filter   = vcombine_s16(x_filter, vdup_n_s16(0));

    if (width == 4) {
        const uint16x4_t max         = vdup_n_u16((1 << bd) - 1);
        uint16x8x2_t     permute_tbl = vld1q_u16_x2(kDotProdTbl);

        const int16_t *s = (const int16_t *)(src);

        do {
            int16x8_t s0, s1, s2, s3;
            load_s16_8x4(s, src_stride, &s0, &s1, &s2, &s3);

            uint16x4_t d0 = convolve4_4_x(s0, filter, offset, permute_tbl, max);
            uint16x4_t d1 = convolve4_4_x(s1, filter, offset, permute_tbl, max);
            uint16x4_t d2 = convolve4_4_x(s2, filter, offset, permute_tbl, max);
            uint16x4_t d3 = convolve4_4_x(s3, filter, offset, permute_tbl, max);

            store_u16_4x4(dst, dst_stride, d0, d1, d2, d3);

            s += 4 * src_stride;
            dst += 4 * dst_stride;
            height -= 4;
        } while (height != 0);
    } else {
        const uint16x8_t max = vdupq_n_u16((1 << bd) - 1);
        uint16x8_t       idx = vld1q_u16(kDeinterleaveTbl);

        do {
            const int16_t *s = (const int16_t *)(src);
            uint16_t      *d = dst;
            int            w = width;

            do {
                int16x8_t s0[4], s1[4], s2[4], s3[4];
                load_s16_8x4(s + 0 * src_stride, 1, &s0[0], &s0[1], &s0[2], &s0[3]);
                load_s16_8x4(s + 1 * src_stride, 1, &s1[0], &s1[1], &s1[2], &s1[3]);
                load_s16_8x4(s + 2 * src_stride, 1, &s2[0], &s2[1], &s2[2], &s2[3]);
                load_s16_8x4(s + 3 * src_stride, 1, &s3[0], &s3[1], &s3[2], &s3[3]);

                uint16x8_t d0 = convolve4_8_x(s0, filter, offset, idx, max);
                uint16x8_t d1 = convolve4_8_x(s1, filter, offset, idx, max);
                uint16x8_t d2 = convolve4_8_x(s2, filter, offset, idx, max);
                uint16x8_t d3 = convolve4_8_x(s3, filter, offset, idx, max);

                store_u16_8x4(d, dst_stride, d0, d1, d2, d3);

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

void svt_av1_highbd_convolve_x_sr_sve(const uint16_t *src, int src_stride, uint16_t *dst, int dst_stride, int w, int h,
                                      const InterpFilterParams *filter_params_x,
                                      const InterpFilterParams *filter_params_y, const int subpel_x_qn,
                                      const int subpel_y_qn, ConvolveParams *conv_params, int bd) {
    if (w == 2 || h == 2) {
        svt_av1_highbd_convolve_x_sr_c(src,
                                       src_stride,
                                       dst,
                                       dst_stride,
                                       w,
                                       h,
                                       filter_params_x,
                                       filter_params_y,
                                       subpel_x_qn,
                                       subpel_y_qn,
                                       conv_params,
                                       bd);
        return;
    }

    const int x_filter_taps = get_filter_tap(filter_params_x, subpel_x_qn);

    if (x_filter_taps == 6) {
        svt_av1_highbd_convolve_x_sr_neon(src,
                                          src_stride,
                                          dst,
                                          dst_stride,
                                          w,
                                          h,
                                          filter_params_x,
                                          filter_params_y,
                                          subpel_x_qn,
                                          subpel_y_qn,
                                          conv_params,
                                          bd);
        return;
    }

    const int      horiz_offset = filter_params_x->taps / 2 - 1;
    const int16_t *x_filter_ptr = av1_get_interp_filter_subpel_kernel(*filter_params_x, subpel_x_qn & SUBPEL_MASK);

    src -= horiz_offset;

    if (x_filter_taps == 8) {
        highbd_convolve_x_sr_8tap_sve(src, src_stride, dst, dst_stride, w, h, x_filter_ptr, bd);
        return;
    }

    highbd_convolve_x_sr_4tap_sve(src + 2, src_stride, dst, dst_stride, w, h, x_filter_ptr, bd);
}
