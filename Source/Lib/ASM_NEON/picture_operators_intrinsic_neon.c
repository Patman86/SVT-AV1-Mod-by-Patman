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
#include "definitions.h"
#include "mem_neon.h"
#include "transpose_neon.h"
#include "pack_unpack_c.h"

static INLINE void residual_kernel4_neon(const uint8_t *restrict input, const uint32_t input_stride,
                                         const uint8_t *restrict pred, const uint32_t pred_stride, int16_t *residual,
                                         const uint32_t residual_stride, const uint32_t area_height) {
    uint32x2_t in, pr;
    int64x2_t  re;

    uint32_t y = area_height;

    do {
        in = vdup_n_u32(*(uint32_t *)(input + 0 * input_stride));
        in = vset_lane_u32(*(uint32_t *)(input + 1 * input_stride), in, 1);
        pr = vdup_n_u32(*(uint32_t *)(pred + 0 * pred_stride));
        pr = vset_lane_u32(*(uint32_t *)(pred + 1 * pred_stride), pr, 1);

        re = vreinterpretq_s64_u16(vsubl_u8(vreinterpret_u8_u32(in), vreinterpret_u8_u32(pr)));
        vst1q_lane_s64((int64_t *)residual, re, 0);
        vst1q_lane_s64((int64_t *)(residual + residual_stride), re, 1);

        input += 2 * input_stride;
        pred += 2 * pred_stride;
        residual += 2 * residual_stride;
        y -= 2;
    } while (y);
}

static INLINE void residual_kernel_neon(const uint8_t *restrict input, const uint8_t *restrict pred,
                                        int16_t *residual) {
    const uint8x8_t in = vld1_u8(input);
    const uint8x8_t pr = vld1_u8(pred);
    const int16x8_t re = vreinterpretq_s16_u16(vsubl_u8(in, pr));
    vst1q_s16(residual, re);
}

static INLINE void residual_kernel8_neon(const uint8_t *restrict input, const uint32_t input_stride,
                                         const uint8_t *restrict pred, const uint32_t pred_stride, int16_t *residual,
                                         const uint32_t residual_stride, const uint32_t area_height) {
    uint32_t y = area_height;
    int      i;
    do {
        for (i = 0; i < 8; i += 8) { residual_kernel_neon(input + i, pred + i, residual + i); }

        input += input_stride;
        pred += pred_stride;
        residual += residual_stride;
        y -= 1;
    } while (y);
}

static INLINE void residual_kernel16_neon(const uint8_t *restrict input, const uint32_t input_stride,
                                          const uint8_t *restrict pred, const uint32_t pred_stride, int16_t *residual,
                                          const uint32_t residual_stride, const uint32_t area_height) {
    uint32_t y = area_height;
    int      i;
    do {
        for (i = 0; i < 16; i += 8) { residual_kernel_neon(input + i, pred + i, residual + i); }

        input += input_stride;
        pred += pred_stride;
        residual += residual_stride;
        y -= 1;
    } while (y);
}

static INLINE void residual_kernel32_neon(const uint8_t *restrict input, const uint32_t input_stride,
                                          const uint8_t *restrict pred, const uint32_t pred_stride, int16_t *residual,
                                          const uint32_t residual_stride, const uint32_t area_height) {
    uint32_t y = area_height;
    int      i;
    do {
        for (i = 0; i < 32; i += 8) { residual_kernel_neon(input + i, pred + i, residual + i); }

        input += input_stride;
        pred += pred_stride;
        residual += residual_stride;
        y -= 1;
    } while (y);
}

static INLINE void residual_kernel64_neon(const uint8_t *restrict input, const uint32_t input_stride,
                                          const uint8_t *restrict pred, const uint32_t pred_stride, int16_t *residual,
                                          const uint32_t residual_stride, const uint32_t area_height) {
    uint32_t y = area_height;
    int      i;
    do {
        for (i = 0; i < 64; i += 8) { residual_kernel_neon(input + i, pred + i, residual + i); }

        input += input_stride;
        pred += pred_stride;
        residual += residual_stride;
        y -= 1;
    } while (y);
}

static INLINE void residual_kernel128_neon(const uint8_t *restrict input, const uint32_t input_stride,
                                           const uint8_t *restrict pred, const uint32_t pred_stride, int16_t *residual,
                                           const uint32_t residual_stride, const uint32_t area_height) {
    uint32_t y = area_height;
    int      i;
    do {
        for (i = 0; i < 128; i += 8) { residual_kernel_neon(input + i, pred + i, residual + i); }

        input += input_stride;
        pred += pred_stride;
        residual += residual_stride;
        y -= 1;
    } while (y);
}

void svt_residual_kernel8bit_neon(uint8_t *input, uint32_t input_stride, uint8_t *pred, uint32_t pred_stride,
                                  int16_t *residual, uint32_t residual_stride, uint32_t area_width,
                                  uint32_t area_height) {
    switch (area_width) {
    case 4: {
        residual_kernel4_neon(input, input_stride, pred, pred_stride, residual, residual_stride, area_height);
        break;
    }

    case 8: {
        residual_kernel8_neon(input, input_stride, pred, pred_stride, residual, residual_stride, area_height);
        break;
    }

    case 16: {
        residual_kernel16_neon(input, input_stride, pred, pred_stride, residual, residual_stride, area_height);
        break;
    }

    case 32: {
        residual_kernel32_neon(input, input_stride, pred, pred_stride, residual, residual_stride, area_height);
        break;
    }

    case 64: {
        residual_kernel64_neon(input, input_stride, pred, pred_stride, residual, residual_stride, area_height);
        break;
    }

    default: // 128
    {
        residual_kernel128_neon(input, input_stride, pred, pred_stride, residual, residual_stride, area_height);
        break;
    }
    }
}

static void hadamard8x8_one_pass(int16x8_t *a) {
    const int16x8_t b0 = vaddq_s16(a[0], a[1]);
    const int16x8_t b1 = vsubq_s16(a[0], a[1]);
    const int16x8_t b2 = vaddq_s16(a[2], a[3]);
    const int16x8_t b3 = vsubq_s16(a[2], a[3]);
    const int16x8_t b4 = vaddq_s16(a[4], a[5]);
    const int16x8_t b5 = vsubq_s16(a[4], a[5]);
    const int16x8_t b6 = vaddq_s16(a[6], a[7]);
    const int16x8_t b7 = vsubq_s16(a[6], a[7]);

    const int16x8_t c0 = vaddq_s16(b0, b2);
    const int16x8_t c1 = vaddq_s16(b1, b3);
    const int16x8_t c2 = vsubq_s16(b0, b2);
    const int16x8_t c3 = vsubq_s16(b1, b3);
    const int16x8_t c4 = vaddq_s16(b4, b6);
    const int16x8_t c5 = vaddq_s16(b5, b7);
    const int16x8_t c6 = vsubq_s16(b4, b6);
    const int16x8_t c7 = vsubq_s16(b5, b7);

    a[0] = vaddq_s16(c0, c4);
    a[1] = vsubq_s16(c2, c6);
    a[2] = vsubq_s16(c0, c4);
    a[3] = vaddq_s16(c2, c6);
    a[4] = vaddq_s16(c3, c7);
    a[5] = vsubq_s16(c3, c7);
    a[6] = vsubq_s16(c1, c5);
    a[7] = vaddq_s16(c1, c5);
}

void svt_aom_hadamard_8x8_neon(const int16_t *src_diff, ptrdiff_t src_stride, int32_t *coeff) {
    int16x8_t a[8];

    a[0] = vld1q_s16(src_diff);
    a[1] = vld1q_s16(src_diff + src_stride);
    a[2] = vld1q_s16(src_diff + 2 * src_stride);
    a[3] = vld1q_s16(src_diff + 3 * src_stride);
    a[4] = vld1q_s16(src_diff + 4 * src_stride);
    a[5] = vld1q_s16(src_diff + 5 * src_stride);
    a[6] = vld1q_s16(src_diff + 6 * src_stride);
    a[7] = vld1q_s16(src_diff + 7 * src_stride);

    hadamard8x8_one_pass(a);
    transpose_elems_inplace_s16_8x8(a + 0, a + 1, a + 2, a + 3, a + 4, a + 5, a + 6, a + 7);
    hadamard8x8_one_pass(a);

    store_s16q_to_tran_low(coeff + 0, a[0]);
    store_s16q_to_tran_low(coeff + 8, a[1]);
    store_s16q_to_tran_low(coeff + 16, a[2]);
    store_s16q_to_tran_low(coeff + 24, a[3]);
    store_s16q_to_tran_low(coeff + 32, a[4]);
    store_s16q_to_tran_low(coeff + 40, a[5]);
    store_s16q_to_tran_low(coeff + 48, a[6]);
    store_s16q_to_tran_low(coeff + 56, a[7]);
}

void svt_aom_hadamard_16x16_neon(const int16_t *src_diff, ptrdiff_t src_stride, int32_t *coeff) {
    int idx;

    for (idx = 0; idx < 4; ++idx) {
        // src_diff: 9 bit, dynamic range [-255, 255]
        const int16_t *src_ptr = src_diff + (idx >> 1) * 8 * src_stride + (idx & 0x01) * 8;
        svt_aom_hadamard_8x8_neon(src_ptr, src_stride, coeff + idx * 64);
    }

    /* Generate 8x4=32 coefficient values in one iteration. With 8 iterations,
    a total of 256 coefficient values will be generated. */
    for (idx = 0; idx < 64; idx += 8) {
        const int16x8_t a0 = load_tran_low_to_s16q(coeff);
        const int16x8_t a1 = load_tran_low_to_s16q(coeff + 64);
        const int16x8_t a2 = load_tran_low_to_s16q(coeff + 128);
        const int16x8_t a3 = load_tran_low_to_s16q(coeff + 192);

        const int16x8_t b0 = vshrq_n_s16(vaddq_s16(a0, a1), 1);
        const int16x8_t b1 = vshrq_n_s16(vsubq_s16(a0, a1), 1);
        const int16x8_t b2 = vshrq_n_s16(vaddq_s16(a2, a3), 1);
        const int16x8_t b3 = vshrq_n_s16(vsubq_s16(a2, a3), 1);

        const int16x8_t c0 = vaddq_s16(b0, b2);
        const int16x8_t c1 = vaddq_s16(b1, b3);
        const int16x8_t c2 = vsubq_s16(b0, b2);
        const int16x8_t c3 = vsubq_s16(b1, b3);

        store_s16q_to_tran_low(coeff + 0, c0);
        store_s16q_to_tran_low(coeff + 64, c1);
        store_s16q_to_tran_low(coeff + 128, c2);
        store_s16q_to_tran_low(coeff + 192, c3);
        coeff += 8;
    }
}

void svt_aom_hadamard_32x32_neon(const int16_t *src_diff, ptrdiff_t src_stride, tran_low_t *coeff) {
    int idx;
    for (idx = 0; idx < 4; ++idx) {
        // src_diff: 9 bit, dynamic range [-255, 255]
        const int16_t *src_ptr = src_diff + (idx >> 1) * 16 * src_stride + (idx & 0x01) * 16;
        svt_aom_hadamard_16x16_neon(src_ptr, src_stride, coeff + idx * 256);
    }

    for (idx = 0; idx < 256; idx += 8) {
        const int16x8_t a0 = load_tran_low_to_s16q(coeff);
        const int16x8_t a1 = load_tran_low_to_s16q(coeff + 256);
        const int16x8_t a2 = load_tran_low_to_s16q(coeff + 512);
        const int16x8_t a3 = load_tran_low_to_s16q(coeff + 768);

        const int16x8_t b0 = vshrq_n_s16(vaddq_s16(a0, a1), 2);
        const int16x8_t b1 = vshrq_n_s16(vsubq_s16(a0, a1), 2);
        const int16x8_t b2 = vshrq_n_s16(vaddq_s16(a2, a3), 2);
        const int16x8_t b3 = vshrq_n_s16(vsubq_s16(a2, a3), 2);

        const int16x8_t c0 = vaddq_s16(b0, b2);
        const int16x8_t c1 = vaddq_s16(b1, b3);
        const int16x8_t c2 = vsubq_s16(b0, b2);
        const int16x8_t c3 = vsubq_s16(b1, b3);

        store_s16_to_tran_low(coeff + 0 + 0, vget_low_s16(c0));
        store_s16_to_tran_low(coeff + 0 + 4, vget_high_s16(c0));
        store_s16_to_tran_low(coeff + 256 + 0, vget_low_s16(c1));
        store_s16_to_tran_low(coeff + 256 + 4, vget_high_s16(c1));
        store_s16_to_tran_low(coeff + 512 + 0, vget_low_s16(c2));
        store_s16_to_tran_low(coeff + 512 + 4, vget_high_s16(c2));
        store_s16_to_tran_low(coeff + 768 + 0, vget_low_s16(c3));
        store_s16_to_tran_low(coeff + 768 + 4, vget_high_s16(c3));

        coeff += 8;
    }
}

static INLINE uint32x4_t full_distortion_kernel4_neon_intrin(const uint16_t *const input, const uint16_t *const recon) {
    const uint16x4_t in      = vld1_u16(input);
    const uint16x4_t re      = vld1_u16(recon);
    const uint16x4_t max     = vmax_u16(in, re);
    const uint16x4_t min     = vmin_u16(in, re);
    const uint16x4_t diff    = vsub_u16(max, min);
    const uint32x4_t diff_32 = vmull_u16(diff, diff);
    return diff_32;
}

static INLINE uint32x4_t full_distortion_kernel8_neon_intrin(uint16x8_t in, uint16x8_t re) {
    const uint16x8_t max     = vmaxq_u16(in, re);
    const uint16x8_t min     = vminq_u16(in, re);
    const uint16x8_t diff    = vsubq_u16(max, min);
    const uint32x4_t diff_32 = vpaddq_u32(vmull_u16(vget_low_u16(diff), vget_low_u16(diff)),
                                          vmull_high_u16(diff, diff));
    return diff_32;
}

uint64_t svt_full_distortion_kernel16_bits_neon(uint8_t *input, uint32_t input_offset, uint32_t input_stride,
                                                uint8_t *recon, int32_t recon_offset, uint32_t recon_stride,
                                                uint32_t area_width, uint32_t area_height) {
    const uint32_t leftover    = area_width & 7; // area_width % 8
    int64x2_t      sum64       = vdupq_n_s64(0);
    uint16_t      *input_16bit = (uint16_t *)input;
    uint16_t      *recon_16bit = (uint16_t *)recon;
    input_16bit += input_offset;
    recon_16bit += recon_offset;

    const uint32x4_t zero = vdupq_n_u32(0);

    if (leftover) { // (leftover == 4)
        const uint16_t       *inp     = input_16bit + area_width - leftover;
        const uint16_t       *rec     = recon_16bit + area_width - leftover;
        const uint16_t *const inp_end = inp + area_height * input_stride;

        do {
            const uint32x4_t sum32 = full_distortion_kernel4_neon_intrin(inp, rec);
            inp += input_stride;
            rec += recon_stride;

            sum64 = vaddq_s64(sum64,
                              vaddq_s64(vreinterpretq_s64_u32(vzip1q_u32(sum32, zero)),
                                        vreinterpretq_s64_u32(vzip2q_u32(sum32, zero))));
        } while (inp < inp_end);
    }

    area_width -= leftover; // area width will be now a multiple of 8

    if (area_width) {
        const uint16_t *inp = input_16bit;
        const uint16_t *rec = recon_16bit;

        if (area_width == 8) {
            const uint16_t *const inp_end = inp + area_height * input_stride;
            do {
                const uint32x4_t sum32 = full_distortion_kernel8_neon_intrin(vld1q_u16(inp), vld1q_u16(rec));
                inp += input_stride;
                rec += recon_stride;

                sum64 = vaddq_s64(sum64,
                                  vaddq_s64(vreinterpretq_s64_u32(vzip1q_u32(sum32, zero)),
                                            vreinterpretq_s64_u32(vzip2q_u32(sum32, zero))));
            } while (inp < inp_end);

        } else if (area_width == 16) {
            const uint16_t *const inp_end = inp + area_height * input_stride;
            do {
                const uint32x4_t sum32 = vaddq_u32(
                    full_distortion_kernel8_neon_intrin(vld1q_u16(inp), vld1q_u16(rec)),
                    full_distortion_kernel8_neon_intrin(vld1q_u16(inp + 8), vld1q_u16(rec + 8)));
                inp += input_stride;
                rec += recon_stride;
                sum64 = vaddq_s64(sum64,
                                  vaddq_s64(vreinterpretq_s64_u32(vzip1q_u32(sum32, zero)),
                                            vreinterpretq_s64_u32(vzip2q_u32(sum32, zero))));
            } while (inp < inp_end);
        } else {
            for (uint32_t h = 0; h < area_height; h++) {
                uint32_t w = 0;

                for (; w + 16 < area_width; w += 16) {
                    const uint32x4_t sum32_0 = full_distortion_kernel8_neon_intrin(vld1q_u16(inp + w),
                                                                                   vld1q_u16(rec + w));
                    const uint32x4_t sum32_1 = full_distortion_kernel8_neon_intrin(vld1q_u16(inp + w + 8),
                                                                                   vld1q_u16(rec + w + 8));

                    sum64 = vaddq_s64(sum64,
                                      vaddq_s64(vaddq_s64(vreinterpretq_s64_u32(vzip1q_u32(sum32_0, zero)),
                                                          vreinterpretq_s64_u32(vzip2q_u32(sum32_0, zero))),
                                                vaddq_s64(vreinterpretq_s64_u32(vzip1q_u32(sum32_1, zero)),
                                                          vreinterpretq_s64_u32(vzip2q_u32(sum32_1, zero)))));
                }

                for (; w < area_width; w += 8) {
                    const uint32x4_t sum32 = full_distortion_kernel8_neon_intrin(vld1q_u16(inp + w),
                                                                                 vld1q_u16(rec + w));
                    sum64                  = vaddq_s64(sum64,
                                      vaddq_s64(vreinterpretq_s64_u32(vzip1q_u32(sum32, zero)),
                                                vreinterpretq_s64_u32(vzip2q_u32(sum32, zero))));
                }
                inp += input_stride;
                rec += recon_stride;
            }
        }
    }

    return vgetq_lane_s64(sum64, 0) + vgetq_lane_s64(sum64, 1);
}

void svt_full_distortion_kernel32_bits_neon(int32_t *coeff, uint32_t coeff_stride, int32_t *recon_coeff,
                                            uint32_t recon_coeff_stride, uint64_t distortion_result[DIST_CALC_TOTAL],
                                            uint32_t area_width, uint32_t area_height) {
    int64x2_t sum1      = vdupq_n_s64(0);
    int64x2_t sum2      = vdupq_n_s64(0);
    uint32_t  row_count = area_height;
    do {
        int32_t *coeff_temp       = coeff;
        int32_t *recon_coeff_temp = recon_coeff;

        uint32_t col_count = area_width / 4;
        do {
            int32x4_t x0 = vld1q_s32(coeff_temp);
            int32x4_t y0 = vld1q_s32(recon_coeff_temp);

            int32x2_t x_lo = vget_low_s32(x0);
            int32x2_t x_hi = vget_high_s32(x0);
            int32x2_t y_lo = vget_low_s32(y0);
            int32x2_t y_hi = vget_high_s32(y0);

            sum2 = vmlal_s32(sum2, x_lo, x_lo);
            sum2 = vmlal_s32(sum2, x_hi, x_hi);

            int32x2_t x_lo_sub = vsub_s32(x_lo, y_lo);
            int32x2_t x_hi_sub = vsub_s32(x_hi, y_hi);

            int64x2_t x_lo_wide = vmull_s32(x_lo_sub, x_lo_sub);
            int64x2_t x_hi_wide = vmull_s32(x_hi_sub, x_hi_sub);

            sum1 = vaddq_s64(sum1, x_lo_wide);
            sum1 = vaddq_s64(sum1, x_hi_wide);

            coeff_temp += 4;
            recon_coeff_temp += 4;
        } while (--col_count);

        coeff += coeff_stride;
        recon_coeff += recon_coeff_stride;
        row_count -= 1;
    } while (row_count > 0);

    int64x2_t tmp  = vcombine_s64(vget_high_s64(sum1), vget_low_s64(sum1));
    int64x2_t tmp2 = vaddq_s64(sum1, tmp);
    tmp            = vcombine_s64(vget_high_s64(sum2), vget_low_s64(sum2));
    int64x2_t tmp3 = vaddq_s64(sum2, tmp);
    tmp3           = vcombine_s64(vget_low_s64(tmp2), vget_low_s64(tmp3));

    vst1q_s64((int64_t *)distortion_result, tmp3);
}

static INLINE void unpack_and_2bcompress_32_neon(uint16_t *in16b_buffer, uint8_t *out8b_buffer, uint8_t *out2b_buffer,
                                                 uint32_t width_rep) {
    const uint16x8_t ymm_00ff = vdupq_n_u16(0x00FF);
    const uint16x8_t msk_2b   = vdupq_n_u16(0x0003); //0000.0000.0000.0011

    const uint32x4_t msk0 = vdupq_n_u32(0x000000C0); //1100.0000
    const uint32x4_t msk1 = vdupq_n_u32(0x00000030); //0011.0000
    const uint32x4_t msk2 = vdupq_n_u32(0x0000000C); //0000.1100

    for (uint32_t w = 0; w < width_rep; w++) {
        const uint16x8_t in1 = vld1q_u16(in16b_buffer + w * 16);
        const uint16x8_t in2 = vld1q_u16(in16b_buffer + w * 16 + 8);

        const uint16x8_t tmp_2b1 = vandq_u16(in1, msk_2b); //0000.0011.1111.1111 -> 0000.0000.0000.0011
        const uint16x8_t tmp_2b2 = vandq_u16(in2, msk_2b);
        const uint32x4_t tmp_2b  = vreinterpretq_u32_u8(vcombine_u8(vqmovn_u16(tmp_2b1), vqmovn_u16(tmp_2b2)));

        const uint32x4_t ext0 = vshrq_n_u32(
            tmp_2b,
            24); //0000.0011.0000.0000.0000.0000.0000.0000 -> 0000.0000.0000.0000.0000.0000.0000.0011
        const uint32x4_t ext1 = vandq_u32(
            vshrq_n_u32(tmp_2b, 14),
            msk2); //0000.0000.0000.0011.0000.0000.0000.0000 -> 0000.0000.0000.0000.0000.0000.0000.1100
        const uint32x4_t ext2 = vandq_u32(
            vshrq_n_u32(tmp_2b, 4),
            msk1); //0000.0000.0000.0000.0000.0011.0000.0000 -> 0000.0000.0000.0000.0000.0000.0011.0000
        const uint32x4_t ext3 = vandq_u32(
            vshlq_n_u32(tmp_2b, 6),
            msk0); //0000.0000.0000.0000.0000.0000.0000.0011 -> 0000.0000.0000.0000.0000.0000.1100.0000

        const uint32x4_t ext0123 = vorrq_u32(vorrq_u32(ext0, ext1),
                                             vorrq_u32(ext2, ext3)); //0000.0000.0000.0000.0000.0000.1111.1111

        const uint32_t ext0123_packed32 = vget_lane_u32(
            vreinterpret_u32_u8(vqmovn_u16(vcombine_u16(vqmovn_u32(ext0123), vdup_n_u16(0)))), 0);
        *((uint32_t *)(out2b_buffer + w * 4)) = ext0123_packed32;

        const uint8x16_t out8_u8 = vcombine_u8(vqmovn_u16(vandq_u16(vshrq_n_u16(in1, 2), ymm_00ff)),
                                               vqmovn_u16(vandq_u16(vshrq_n_u16(in2, 2), ymm_00ff)));

        vst1q_u8(out8b_buffer + w * 16, out8_u8);
    }
}

static INLINE void svt_unpack_and_2bcompress_remainder(uint16_t *in16b_buffer, uint8_t *out8b_buffer,
                                                       uint8_t *out2b_buffer, uint32_t width) {
    uint32_t col;
    uint16_t in_pixel;
    uint8_t  tmp_pixel;

    uint32_t w_m4  = (width / 4) * 4;
    uint32_t w_rem = width - w_m4;

    for (col = 0; col < w_m4; col += 4) {
        uint8_t compressed_unpacked_pixel = 0;
        //+0
        in_pixel                  = in16b_buffer[col + 0];
        out8b_buffer[col + 0]     = (uint8_t)(in_pixel >> 2);
        tmp_pixel                 = (uint8_t)(in_pixel << 6);
        compressed_unpacked_pixel = compressed_unpacked_pixel | ((tmp_pixel >> 0) & 0xC0); //1100.0000

        //+1
        in_pixel                  = in16b_buffer[col + 1];
        out8b_buffer[col + 1]     = (uint8_t)(in_pixel >> 2);
        tmp_pixel                 = (uint8_t)(in_pixel << 6);
        compressed_unpacked_pixel = compressed_unpacked_pixel | ((tmp_pixel >> 2) & 0x30); //0011.0000

        //+2
        in_pixel                  = in16b_buffer[col + 2];
        out8b_buffer[col + 2]     = (uint8_t)(in_pixel >> 2);
        tmp_pixel                 = (uint8_t)(in_pixel << 6);
        compressed_unpacked_pixel = compressed_unpacked_pixel | ((tmp_pixel >> 4) & 0x0C); //0000.1100

        //+3
        in_pixel                  = in16b_buffer[col + 3];
        out8b_buffer[col + 3]     = (uint8_t)(in_pixel >> 2);
        tmp_pixel                 = (uint8_t)(in_pixel << 6);
        compressed_unpacked_pixel = compressed_unpacked_pixel | ((tmp_pixel >> 6) & 0x03); //0000.0011

        out2b_buffer[col / 4] = compressed_unpacked_pixel;
    }

    //we can have up to 3 pixels remaining
    if (w_rem > 0) {
        uint8_t compressed_unpacked_pixel = 0;
        //+0
        in_pixel                  = in16b_buffer[col + 0];
        out8b_buffer[col + 0]     = (uint8_t)(in_pixel >> 2);
        tmp_pixel                 = (uint8_t)(in_pixel << 6);
        compressed_unpacked_pixel = compressed_unpacked_pixel | ((tmp_pixel >> 0) & 0xC0); //1100.0000

        if (w_rem > 1) {
            //+1
            in_pixel                  = in16b_buffer[col + 1];
            out8b_buffer[col + 1]     = (uint8_t)(in_pixel >> 2);
            tmp_pixel                 = (uint8_t)(in_pixel << 6);
            compressed_unpacked_pixel = compressed_unpacked_pixel | ((tmp_pixel >> 2) & 0x30); //0011.0000
        }
        if (w_rem > 2) {
            //+2
            in_pixel                  = in16b_buffer[col + 2];
            out8b_buffer[col + 2]     = (uint8_t)(in_pixel >> 2);
            tmp_pixel                 = (uint8_t)(in_pixel << 6);
            compressed_unpacked_pixel = compressed_unpacked_pixel | ((tmp_pixel >> 4) & 0x0C); //0000.1100
        }

        out2b_buffer[col / 4] = compressed_unpacked_pixel;
    }
}

void svt_unpack_and_2bcompress_neon(uint16_t *in16b_buffer, uint32_t in16b_stride, uint8_t *out8b_buffer,
                                    uint32_t out8b_stride, uint8_t *out2b_buffer, uint32_t out2b_stride, uint32_t width,
                                    uint32_t height) {
    if (width == 32) {
        for (uint32_t h = 0; h < height; h++) {
            unpack_and_2bcompress_32_neon(
                in16b_buffer + h * in16b_stride, out8b_buffer + h * out8b_stride, out2b_buffer + h * out2b_stride, 2);
        }
    } else if (width == 64) {
        for (uint32_t h = 0; h < height; h++) {
            unpack_and_2bcompress_32_neon(
                in16b_buffer + h * in16b_stride, out8b_buffer + h * out8b_stride, out2b_buffer + h * out2b_stride, 4);
        }
    } else {
        uint32_t offset_rem   = width & 0xfffffff0;
        uint32_t offset2b_rem = offset_rem >> 2;
        uint32_t remainder    = width & 0xf;
        for (uint32_t h = 0; h < height; h++) {
            unpack_and_2bcompress_32_neon(in16b_buffer + h * in16b_stride,
                                          out8b_buffer + h * out8b_stride,
                                          out2b_buffer + h * out2b_stride,
                                          width >> 4);
            if (remainder)
                svt_unpack_and_2bcompress_remainder(in16b_buffer + h * in16b_stride + offset_rem,
                                                    out8b_buffer + h * out8b_stride + offset_rem,
                                                    out2b_buffer + h * out2b_stride + offset2b_rem,
                                                    remainder);
        }
    }
}

static INLINE void compressed_packmsb_32x2h(uint8_t *in8_bit_buffer, uint32_t in8_stride, uint8_t *inn_bit_buffer,
                                            uint32_t inn_stride, uint16_t *out16_bit_buffer, uint32_t out_stride,
                                            uint32_t height) {
    const uint8x16_t msk0 = vdupq_n_u8(0xC0); //1100.000

    // processing 2 lines for chroma
    for (uint32_t y = 0; y < height; y += 2) {
        // 2 Lines Stored in 1D format-Could be replaced by 2 _mm_loadl_epi64
        const uint8x16_t in_2_bit = vreinterpretq_u8_u64(
            vzip1q_u64(vreinterpretq_u64_u8(vld1q_u8(inn_bit_buffer)),
                       vreinterpretq_u64_u8(vld1q_u8(inn_bit_buffer + inn_stride))));

        const uint8x16_t ext0 = vandq_u8(in_2_bit, msk0);
        const uint8x16_t ext1 = vandq_u8(vreinterpretq_u8_u16(vshlq_n_u16(vreinterpretq_u16_u8(in_2_bit), 2)), msk0);
        const uint8x16_t ext2 = vandq_u8(vreinterpretq_u8_u16(vshlq_n_u16(vreinterpretq_u16_u8(in_2_bit), 4)), msk0);
        const uint8x16_t ext3 = vandq_u8(vreinterpretq_u8_u16(vshlq_n_u16(vreinterpretq_u16_u8(in_2_bit), 6)), msk0);

        const uint8x16_t ext01   = vzip1q_u8(ext0, ext1);
        const uint8x16_t ext23   = vzip1q_u8(ext2, ext3);
        const uint8x16_t ext0_15 = vreinterpretq_u8_u16(
            vzip1q_u16(vreinterpretq_u16_u8(ext01), vreinterpretq_u16_u8(ext23)));
        const uint8x16_t ext16_31 = vreinterpretq_u8_u16(
            vzip2q_u16(vreinterpretq_u16_u8(ext01), vreinterpretq_u16_u8(ext23)));

        const uint8x16_t ext01h = vzip2q_u8(ext0, ext1);
        const uint8x16_t ext23h = vzip2q_u8(ext2, ext3);

        const uint8x16_t ext32_47 = vreinterpretq_u8_u16(
            vzip1q_u16(vreinterpretq_u16_u8(ext01h), vreinterpretq_u16_u8(ext23h)));
        const uint8x16_t ext48_63 = vreinterpretq_u8_u16(
            vzip2q_u16(vreinterpretq_u16_u8(ext01h), vreinterpretq_u16_u8(ext23h)));

        const uint8x16_t in_8_bit0 = vld1q_u8(in8_bit_buffer + 0);
        const uint8x16_t in_8_bit1 = vld1q_u8(in8_bit_buffer + 16);
        const uint8x16_t in_8_bit2 = vld1q_u8(in8_bit_buffer + in8_stride);
        const uint8x16_t in_8_bit3 = vld1q_u8(in8_bit_buffer + in8_stride + 16);

        // (out_pixel | n_bit_pixel) concatenation is done with unpacklo_epi8 and unpackhi_epi8
        const uint16x8_t concat00 = vshrq_n_u16(vreinterpretq_u16_u8(vzip1q_u8(ext0_15, in_8_bit0)), 6);
        const uint16x8_t concat01 = vshrq_n_u16(vreinterpretq_u16_u8(vzip2q_u8(ext0_15, in_8_bit0)), 6);
        const uint16x8_t concat02 = vshrq_n_u16(vreinterpretq_u16_u8(vzip1q_u8(ext16_31, in_8_bit1)), 6);
        const uint16x8_t concat03 = vshrq_n_u16(vreinterpretq_u16_u8(vzip2q_u8(ext16_31, in_8_bit1)), 6);

        vst1q_u16(out16_bit_buffer + 0, concat00);
        vst1q_u16(out16_bit_buffer + 8, concat01);
        vst1q_u16(out16_bit_buffer + 16, concat02);
        vst1q_u16(out16_bit_buffer + 24, concat03);

        // (out_pixel | n_bit_pixel) concatenation is done with unpacklo_epi8 and unpackhi_epi8
        const uint16x8_t concat10 = vshrq_n_u16(vreinterpretq_u16_u8(vzip1q_u8(ext32_47, in_8_bit2)), 6);
        const uint16x8_t concat11 = vshrq_n_u16(vreinterpretq_u16_u8(vzip2q_u8(ext32_47, in_8_bit2)), 6);
        const uint16x8_t concat12 = vshrq_n_u16(vreinterpretq_u16_u8(vzip1q_u8(ext48_63, in_8_bit3)), 6);
        const uint16x8_t concat13 = vshrq_n_u16(vreinterpretq_u16_u8(vzip2q_u8(ext48_63, in_8_bit3)), 6);

        vst1q_u16(out16_bit_buffer + out_stride + 0, concat10);
        vst1q_u16(out16_bit_buffer + out_stride + 8, concat11);
        vst1q_u16(out16_bit_buffer + out_stride + 16, concat12);
        vst1q_u16(out16_bit_buffer + out_stride + 24, concat13);

        in8_bit_buffer += in8_stride << 1;
        inn_bit_buffer += inn_stride << 1;
        out16_bit_buffer += out_stride << 1;
    }
}

static INLINE void compressed_packmsb_64xh(uint8_t *in8_bit_buffer, uint32_t in8_stride, uint8_t *inn_bit_buffer,
                                           uint32_t inn_stride, uint16_t *out16_bit_buffer, uint32_t out_stride,
                                           uint32_t height) {
    const uint8x16_t msk0 = vdupq_n_u8(0xC0); //1100.000

    // one row per iteration
    for (uint32_t y = 0; y < height; y++) {
        const uint8x16_t in_2_bit = vld1q_u8(inn_bit_buffer);

        const uint8x16_t ext0 = vandq_u8(in_2_bit, msk0);
        const uint8x16_t ext1 = vandq_u8(vreinterpretq_u8_u16(vshlq_n_u16(vreinterpretq_u16_u8(in_2_bit), 2)), msk0);
        const uint8x16_t ext2 = vandq_u8(vreinterpretq_u8_u16(vshlq_n_u16(vreinterpretq_u16_u8(in_2_bit), 4)), msk0);
        const uint8x16_t ext3 = vandq_u8(vreinterpretq_u8_u16(vshlq_n_u16(vreinterpretq_u16_u8(in_2_bit), 6)), msk0);

        const uint8x16_t ext01 = vzip1q_u8(ext0, ext1);
        const uint8x16_t ext23 = vzip1q_u8(ext2, ext3);

        const uint8x16_t ext0_15 = vreinterpretq_u8_u16(
            vzip1q_u16(vreinterpretq_u16_u8(ext01), vreinterpretq_u16_u8(ext23)));
        const uint8x16_t ext16_31 = vreinterpretq_u8_u16(
            vzip2q_u16(vreinterpretq_u16_u8(ext01), vreinterpretq_u16_u8(ext23)));

        const uint8x16_t ext01h = vzip2q_u8(ext0, ext1);
        const uint8x16_t ext23h = vzip2q_u8(ext2, ext3);

        const uint8x16_t ext32_47 = vreinterpretq_u8_u16(
            vzip1q_u16(vreinterpretq_u16_u8(ext01h), vreinterpretq_u16_u8(ext23h)));
        const uint8x16_t ext48_63 = vreinterpretq_u8_u16(
            vzip2q_u16(vreinterpretq_u16_u8(ext01h), vreinterpretq_u16_u8(ext23h)));

        const uint8x16_t in_8_bit0 = vld1q_u8(in8_bit_buffer + 0);
        const uint8x16_t in_8_bit1 = vld1q_u8(in8_bit_buffer + 16);
        const uint8x16_t in_8_bit2 = vld1q_u8(in8_bit_buffer + 32);
        const uint8x16_t in_8_bit3 = vld1q_u8(in8_bit_buffer + 48);

        // (out_pixel | n_bit_pixel) concatenation
        const uint16x8_t concat00 = vshrq_n_u16(vreinterpretq_u16_u8(vzip1q_u8(ext0_15, in_8_bit0)), 6);
        const uint16x8_t concat01 = vshrq_n_u16(vreinterpretq_u16_u8(vzip2q_u8(ext0_15, in_8_bit0)), 6);
        const uint16x8_t concat02 = vshrq_n_u16(vreinterpretq_u16_u8(vzip1q_u8(ext16_31, in_8_bit1)), 6);
        const uint16x8_t concat03 = vshrq_n_u16(vreinterpretq_u16_u8(vzip2q_u8(ext16_31, in_8_bit1)), 6);

        vst1q_u16(out16_bit_buffer + 0, concat00);
        vst1q_u16(out16_bit_buffer + 8, concat01);
        vst1q_u16(out16_bit_buffer + 16, concat02);
        vst1q_u16(out16_bit_buffer + 24, concat03);

        // (out_pixel | n_bit_pixel) concatenation
        const uint16x8_t concat10 = vshrq_n_u16(vreinterpretq_u16_u8(vzip1q_u8(ext32_47, in_8_bit2)), 6);
        const uint16x8_t concat11 = vshrq_n_u16(vreinterpretq_u16_u8(vzip2q_u8(ext32_47, in_8_bit2)), 6);
        const uint16x8_t concat12 = vshrq_n_u16(vreinterpretq_u16_u8(vzip1q_u8(ext48_63, in_8_bit3)), 6);
        const uint16x8_t concat13 = vshrq_n_u16(vreinterpretq_u16_u8(vzip2q_u8(ext48_63, in_8_bit3)), 6);

        vst1q_u16(out16_bit_buffer + 32, concat10);
        vst1q_u16(out16_bit_buffer + 40, concat11);
        vst1q_u16(out16_bit_buffer + 48, concat12);
        vst1q_u16(out16_bit_buffer + 56, concat13);

        in8_bit_buffer += in8_stride;
        inn_bit_buffer += inn_stride;
        out16_bit_buffer += out_stride;
    }
}

static INLINE void compressed_packmsb_64(uint8_t *in8_bit_buffer, uint8_t *inn_bit_buffer, uint16_t *out16_bit_buffer,
                                         uint32_t width_rep) {
    const uint8x16_t msk0 = vdupq_n_u8(0xC0); //1100.000

    // one row per iteration
    for (uint32_t w = 0; w < width_rep; w++) {
        const uint8x16_t in_2_bit = vld1q_u8(inn_bit_buffer);

        const uint8x16_t ext0 = vandq_u8(in_2_bit, msk0);
        const uint8x16_t ext1 = vandq_u8(vreinterpretq_u8_u16(vshlq_n_u16(vreinterpretq_u16_u8(in_2_bit), 2)), msk0);
        const uint8x16_t ext2 = vandq_u8(vreinterpretq_u8_u16(vshlq_n_u16(vreinterpretq_u16_u8(in_2_bit), 4)), msk0);
        const uint8x16_t ext3 = vandq_u8(vreinterpretq_u8_u16(vshlq_n_u16(vreinterpretq_u16_u8(in_2_bit), 6)), msk0);

        const uint8x16_t ext01 = vzip1q_u8(ext0, ext1);
        const uint8x16_t ext23 = vzip1q_u8(ext2, ext3);

        const uint8x16_t ext0_15 = vreinterpretq_u8_u16(
            vzip1q_u16(vreinterpretq_u16_u8(ext01), vreinterpretq_u16_u8(ext23)));
        const uint8x16_t ext16_31 = vreinterpretq_u8_u16(
            vzip2q_u16(vreinterpretq_u16_u8(ext01), vreinterpretq_u16_u8(ext23)));

        const uint8x16_t ext01h = vzip2q_u8(ext0, ext1);
        const uint8x16_t ext23h = vzip2q_u8(ext2, ext3);

        const uint8x16_t ext32_47 = vreinterpretq_u8_u16(
            vzip1q_u16(vreinterpretq_u16_u8(ext01h), vreinterpretq_u16_u8(ext23h)));
        const uint8x16_t ext48_63 = vreinterpretq_u8_u16(
            vzip2q_u16(vreinterpretq_u16_u8(ext01h), vreinterpretq_u16_u8(ext23h)));

        const uint8x16_t in_8_bit0 = vld1q_u8(in8_bit_buffer + 0);
        const uint8x16_t in_8_bit1 = vld1q_u8(in8_bit_buffer + 16);
        const uint8x16_t in_8_bit2 = vld1q_u8(in8_bit_buffer + 32);
        const uint8x16_t in_8_bit3 = vld1q_u8(in8_bit_buffer + 48);

        // (out_pixel | n_bit_pixel) concatenation
        const uint16x8_t concat00 = vshrq_n_u16(vreinterpretq_u16_u8(vzip1q_u8(ext0_15, in_8_bit0)), 6);
        const uint16x8_t concat01 = vshrq_n_u16(vreinterpretq_u16_u8(vzip2q_u8(ext0_15, in_8_bit0)), 6);
        const uint16x8_t concat02 = vshrq_n_u16(vreinterpretq_u16_u8(vzip1q_u8(ext16_31, in_8_bit1)), 6);
        const uint16x8_t concat03 = vshrq_n_u16(vreinterpretq_u16_u8(vzip2q_u8(ext16_31, in_8_bit1)), 6);

        vst1q_u16(out16_bit_buffer + 0, concat00);
        vst1q_u16(out16_bit_buffer + 8, concat01);
        vst1q_u16(out16_bit_buffer + 16, concat02);
        vst1q_u16(out16_bit_buffer + 24, concat03);

        // (out_pixel | n_bit_pixel) concatenation
        const uint16x8_t concat10 = vshrq_n_u16(vreinterpretq_u16_u8(vzip1q_u8(ext32_47, in_8_bit2)), 6);
        const uint16x8_t concat11 = vshrq_n_u16(vreinterpretq_u16_u8(vzip2q_u8(ext32_47, in_8_bit2)), 6);
        const uint16x8_t concat12 = vshrq_n_u16(vreinterpretq_u16_u8(vzip1q_u8(ext48_63, in_8_bit3)), 6);
        const uint16x8_t concat13 = vshrq_n_u16(vreinterpretq_u16_u8(vzip2q_u8(ext48_63, in_8_bit3)), 6);

        vst1q_u16(out16_bit_buffer + 32, concat10);
        vst1q_u16(out16_bit_buffer + 40, concat11);
        vst1q_u16(out16_bit_buffer + 48, concat12);
        vst1q_u16(out16_bit_buffer + 56, concat13);

        in8_bit_buffer += 64;
        inn_bit_buffer += 16;
        out16_bit_buffer += 64;
    }
}

void svt_compressed_packmsb_neon(uint8_t *in8_bit_buffer, uint32_t in8_stride, uint8_t *inn_bit_buffer,
                                 uint32_t inn_stride, uint16_t *out16_bit_buffer, uint32_t out_stride, uint32_t width,
                                 uint32_t height) {
    if (width == 32) {
        compressed_packmsb_32x2h(
            in8_bit_buffer, in8_stride, inn_bit_buffer, inn_stride, out16_bit_buffer, out_stride, height);
    } else if (width == 64) {
        compressed_packmsb_64xh(
            in8_bit_buffer, in8_stride, inn_bit_buffer, inn_stride, out16_bit_buffer, out_stride, height);
    } else {
        int32_t  leftover     = width;
        uint32_t offset8b_16b = 0;
        uint32_t offset2b     = 0;
        if (leftover >= 64) {
            uint32_t offset = width & 0xffffff40;
            for (uint32_t y = 0; y < height; y++) {
                compressed_packmsb_64(in8_bit_buffer + y * in8_stride,
                                      inn_bit_buffer + y * inn_stride,
                                      out16_bit_buffer + y * out_stride,
                                      width >> 6);
            }
            offset8b_16b += offset;
            offset2b += offset >> 2;
            leftover -= offset;
        }
        if (leftover >= 32) {
            compressed_packmsb_32x2h(in8_bit_buffer + offset8b_16b,
                                     in8_stride,
                                     inn_bit_buffer + offset2b,
                                     inn_stride,
                                     out16_bit_buffer + offset8b_16b,
                                     out_stride,
                                     height);
            offset8b_16b += 32;
            offset2b += 8;
            leftover -= 32;
        }
        if (leftover) {
            svt_compressed_packmsb_c(in8_bit_buffer + offset8b_16b,
                                     in8_stride,
                                     inn_bit_buffer + offset2b,
                                     inn_stride,
                                     out16_bit_buffer + offset8b_16b,
                                     out_stride,
                                     leftover,
                                     height);
        }
    }
}

void svt_enc_msb_pack2d_neon(uint8_t *in8_bit_buffer, uint32_t in8_stride, uint8_t *inn_bit_buffer,
                             uint16_t *out16_bit_buffer, uint32_t inn_stride, uint32_t out_stride, uint32_t width,
                             uint32_t height) {
    uint32_t count_width, count_height;

    if (width == 4) {
        for (count_height = 0; count_height < height; count_height += 2) {
            vst1_u16(out16_bit_buffer,
                     vshr_n_u16(
                         vreinterpret_u16_u8(vzip1_u8(vreinterpret_u8_u32(vdup_n_u32(*(uint32_t *)(inn_bit_buffer))),
                                                      vreinterpret_u8_u32(vdup_n_u32(*(uint32_t *)(in8_bit_buffer))))),
                         6));
            vst1_u16(out16_bit_buffer + out_stride,
                     vshr_n_u16(vreinterpret_u16_u8(vzip1_u8(
                                    vreinterpret_u8_u32(vdup_n_u32(*(uint32_t *)(inn_bit_buffer + inn_stride))),
                                    vreinterpret_u8_u32(vdup_n_u32(*(uint32_t *)(in8_bit_buffer + in8_stride))))),
                                6));

            out16_bit_buffer += (out_stride << 1);
            in8_bit_buffer += (in8_stride << 1);
            inn_bit_buffer += (inn_stride << 1);
        }
    } else if (width == 8) {
        for (count_height = 0; count_height < height; count_height += 2) {
            vst1q_u16(out16_bit_buffer,
                      vshrq_n_u16(
                          vreinterpretq_u16_u8(vzip1q_u8(
                              vcombine_u8(vreinterpret_u8_u64(vld1_u64((uint64_t *)(inn_bit_buffer))), vdup_n_u8(0)),
                              vcombine_u8(vreinterpret_u8_u64(vld1_u64((uint64_t *)(in8_bit_buffer))), vdup_n_u8(0)))),
                          6));
            vst1q_u16(
                out16_bit_buffer + out_stride,
                vshrq_n_u16(vreinterpretq_u16_u8(vzip1q_u8(
                                vcombine_u8(vreinterpret_u8_u64(vld1_u64((uint64_t *)(inn_bit_buffer + inn_stride))),
                                            vdup_n_u8(0)),
                                vcombine_u8(vreinterpret_u8_u64(vld1_u64((uint64_t *)(in8_bit_buffer + in8_stride))),
                                            vdup_n_u8(0)))),
                            6));

            out16_bit_buffer += (out_stride << 1);
            in8_bit_buffer += (in8_stride << 1);
            inn_bit_buffer += (inn_stride << 1);
        }
    } else if (width == 16) {
        for (count_height = 0; count_height < height; count_height += 2) {
            const uint8x16_t inn_bit_buffer_lo = vld1q_u8(inn_bit_buffer);
            const uint8x16_t inn_bit_buffer_hi = vld1q_u8(inn_bit_buffer + inn_stride);
            const uint8x16_t in_8bit_buffer_lo = vld1q_u8(in8_bit_buffer);
            const uint8x16_t in_8bit_buffer_hi = vld1q_u8(in8_bit_buffer + in8_stride);

            const uint16x8_t out_pixel_1 = vshrq_n_u16(
                vreinterpretq_u16_u8(vzip1q_u8(inn_bit_buffer_lo, in_8bit_buffer_lo)), 6);
            const uint16x8_t out_pixel_2 = vshrq_n_u16(
                vreinterpretq_u16_u8(vzip2q_u8(inn_bit_buffer_lo, in_8bit_buffer_lo)), 6);
            const uint16x8_t out_pixel_3 = vshrq_n_u16(
                vreinterpretq_u16_u8(vzip1q_u8(inn_bit_buffer_hi, in_8bit_buffer_hi)), 6);
            const uint16x8_t out_pixel_4 = vshrq_n_u16(
                vreinterpretq_u16_u8(vzip2q_u8(inn_bit_buffer_hi, in_8bit_buffer_hi)), 6);

            vst1q_u16(out16_bit_buffer + 0, out_pixel_1);
            vst1q_u16(out16_bit_buffer + 8, out_pixel_2);
            vst1q_u16(out16_bit_buffer + out_stride + 0, out_pixel_3);
            vst1q_u16(out16_bit_buffer + out_stride + 8, out_pixel_4);

            in8_bit_buffer += (in8_stride << 1);
            inn_bit_buffer += (inn_stride << 1);
            out16_bit_buffer += (out_stride << 1);
        }
    } else if (width == 32) {
        for (count_height = 0; count_height < height; count_height += 2) {
            const uint8x16_t inn_bit_buffer_1 = vld1q_u8(inn_bit_buffer);
            const uint8x16_t inn_bit_buffer_2 = vld1q_u8(inn_bit_buffer + 16);
            const uint8x16_t inn_bit_buffer_3 = vld1q_u8(inn_bit_buffer + inn_stride);
            const uint8x16_t inn_bit_buffer_4 = vld1q_u8(inn_bit_buffer + inn_stride + 16);

            const uint8x16_t in_8bit_buffer1 = vld1q_u8(in8_bit_buffer);
            const uint8x16_t in_8bit_buffer2 = vld1q_u8(in8_bit_buffer + 16);
            const uint8x16_t in_8bit_buffer3 = vld1q_u8(in8_bit_buffer + in8_stride);
            const uint8x16_t in_8bit_buffer4 = vld1q_u8(in8_bit_buffer + in8_stride + 16);

            const uint16x8_t out_pixel_1 = vshrq_n_u16(
                vreinterpretq_u16_u8(vzip1q_u8(inn_bit_buffer_1, in_8bit_buffer1)), 6);
            const uint16x8_t out_pixel_2 = vshrq_n_u16(
                vreinterpretq_u16_u8(vzip2q_u8(inn_bit_buffer_1, in_8bit_buffer1)), 6);
            const uint16x8_t out_pixel_3 = vshrq_n_u16(
                vreinterpretq_u16_u8(vzip1q_u8(inn_bit_buffer_2, in_8bit_buffer2)), 6);
            const uint16x8_t out_pixel_4 = vshrq_n_u16(
                vreinterpretq_u16_u8(vzip2q_u8(inn_bit_buffer_2, in_8bit_buffer2)), 6);
            const uint16x8_t out_pixel_5 = vshrq_n_u16(
                vreinterpretq_u16_u8(vzip1q_u8(inn_bit_buffer_3, in_8bit_buffer3)), 6);
            const uint16x8_t out_pixel_6 = vshrq_n_u16(
                vreinterpretq_u16_u8(vzip2q_u8(inn_bit_buffer_3, in_8bit_buffer3)), 6);
            const uint16x8_t out_pixel_7 = vshrq_n_u16(
                vreinterpretq_u16_u8(vzip1q_u8(inn_bit_buffer_4, in_8bit_buffer4)), 6);
            const uint16x8_t out_pixel_8 = vshrq_n_u16(
                vreinterpretq_u16_u8(vzip2q_u8(inn_bit_buffer_4, in_8bit_buffer4)), 6);

            vst1q_u16(out16_bit_buffer + 0, out_pixel_1);
            vst1q_u16(out16_bit_buffer + 8, out_pixel_2);
            vst1q_u16(out16_bit_buffer + 16, out_pixel_3);
            vst1q_u16(out16_bit_buffer + 24, out_pixel_4);
            vst1q_u16(out16_bit_buffer + out_stride + 0, out_pixel_5);
            vst1q_u16(out16_bit_buffer + out_stride + 8, out_pixel_6);
            vst1q_u16(out16_bit_buffer + out_stride + 16, out_pixel_7);
            vst1q_u16(out16_bit_buffer + out_stride + 24, out_pixel_8);

            in8_bit_buffer += (in8_stride << 1);
            inn_bit_buffer += (inn_stride << 1);
            out16_bit_buffer += (out_stride << 1);
        }
    } else if (width == 64) {
        for (count_height = 0; count_height < height; ++count_height) {
            const uint8x16_t inn_bit_buffer_1 = vld1q_u8(inn_bit_buffer);
            const uint8x16_t inn_bit_buffer_2 = vld1q_u8(inn_bit_buffer + 16);
            const uint8x16_t inn_bit_buffer_3 = vld1q_u8(inn_bit_buffer + 32);
            const uint8x16_t inn_bit_buffer_4 = vld1q_u8(inn_bit_buffer + 48);

            const uint8x16_t in_8bit_buffer1 = vld1q_u8(in8_bit_buffer);
            const uint8x16_t in_8bit_buffer2 = vld1q_u8(in8_bit_buffer + 16);
            const uint8x16_t in_8bit_buffer3 = vld1q_u8(in8_bit_buffer + 32);
            const uint8x16_t in_8bit_buffer4 = vld1q_u8(in8_bit_buffer + 48);

            const uint16x8_t out_pixel_1 = vshrq_n_u16(
                vreinterpretq_u16_u8(vzip1q_u8(inn_bit_buffer_1, in_8bit_buffer1)), 6);
            const uint16x8_t out_pixel_2 = vshrq_n_u16(
                vreinterpretq_u16_u8(vzip2q_u8(inn_bit_buffer_1, in_8bit_buffer1)), 6);
            const uint16x8_t out_pixel_3 = vshrq_n_u16(
                vreinterpretq_u16_u8(vzip1q_u8(inn_bit_buffer_2, in_8bit_buffer2)), 6);
            const uint16x8_t out_pixel_4 = vshrq_n_u16(
                vreinterpretq_u16_u8(vzip2q_u8(inn_bit_buffer_2, in_8bit_buffer2)), 6);
            const uint16x8_t out_pixel_5 = vshrq_n_u16(
                vreinterpretq_u16_u8(vzip1q_u8(inn_bit_buffer_3, in_8bit_buffer3)), 6);
            const uint16x8_t out_pixel_6 = vshrq_n_u16(
                vreinterpretq_u16_u8(vzip2q_u8(inn_bit_buffer_3, in_8bit_buffer3)), 6);
            const uint16x8_t out_pixel_7 = vshrq_n_u16(
                vreinterpretq_u16_u8(vzip1q_u8(inn_bit_buffer_4, in_8bit_buffer4)), 6);
            const uint16x8_t out_pixel_8 = vshrq_n_u16(
                vreinterpretq_u16_u8(vzip2q_u8(inn_bit_buffer_4, in_8bit_buffer4)), 6);

            vst1q_u16(out16_bit_buffer + 0, out_pixel_1);
            vst1q_u16(out16_bit_buffer + 8, out_pixel_2);
            vst1q_u16(out16_bit_buffer + 16, out_pixel_3);
            vst1q_u16(out16_bit_buffer + 24, out_pixel_4);
            vst1q_u16(out16_bit_buffer + 32, out_pixel_5);
            vst1q_u16(out16_bit_buffer + 40, out_pixel_6);
            vst1q_u16(out16_bit_buffer + 48, out_pixel_7);
            vst1q_u16(out16_bit_buffer + 56, out_pixel_8);

            in8_bit_buffer += in8_stride;
            inn_bit_buffer += inn_stride;
            out16_bit_buffer += out_stride;
        }
    } else {
        uint32_t in_n_stride_diff = (inn_stride << 1) - width;
        uint32_t in_8_stride_diff = (in8_stride << 1) - width;
        uint32_t out_stride_diff  = (out_stride << 1) - width;

        if (!(width & 7)) {
            for (count_height = 0; count_height < height; count_height += 2) {
                for (count_width = 0; count_width < width; count_width += 8) {
                    vst1q_u16(
                        out16_bit_buffer,
                        vshrq_n_u16(
                            vreinterpretq_u16_u8(vzip1q_u8(
                                vcombine_u8(vreinterpret_u8_u64(vld1_u64((uint64_t *)(inn_bit_buffer))), vdup_n_u8(0)),
                                vcombine_u8(vreinterpret_u8_u64(vld1_u64((uint64_t *)(in8_bit_buffer))),
                                            vdup_n_u8(0)))),
                            6));
                    vst1q_u16(
                        out16_bit_buffer + out_stride,
                        vshrq_n_u16(
                            vreinterpretq_u16_u8(vzip1q_u8(
                                vcombine_u8(vreinterpret_u8_u64(vld1_u64((uint64_t *)(inn_bit_buffer + inn_stride))),
                                            vdup_n_u8(0)),
                                vcombine_u8(vreinterpret_u8_u64(vld1_u64((uint64_t *)(in8_bit_buffer + in8_stride))),
                                            vdup_n_u8(0)))),
                            6));

                    out16_bit_buffer += 8;
                    in8_bit_buffer += 8;
                    inn_bit_buffer += 8;
                }
                in8_bit_buffer += in_8_stride_diff;
                inn_bit_buffer += in_n_stride_diff;
                out16_bit_buffer += out_stride_diff;
            }
        } else {
            for (count_height = 0; count_height < height; count_height += 2) {
                for (count_width = 0; count_width < width; count_width += 4) {
                    vst1_u16(out16_bit_buffer,
                             vshr_n_u16(vreinterpret_u16_u8(
                                            vzip1_u8(vreinterpret_u8_u32(vdup_n_u32(*(uint32_t *)(inn_bit_buffer))),
                                                     vreinterpret_u8_u32(vdup_n_u32(*(uint32_t *)(in8_bit_buffer))))),
                                        6));
                    vst1_u16(
                        out16_bit_buffer + out_stride,
                        vshr_n_u16(vreinterpret_u16_u8(vzip1_u8(
                                       vreinterpret_u8_u32(vdup_n_u32(*(uint32_t *)(inn_bit_buffer + inn_stride))),
                                       vreinterpret_u8_u32(vdup_n_u32(*(uint32_t *)(in8_bit_buffer + in8_stride))))),
                                   6));

                    out16_bit_buffer += 4;
                    in8_bit_buffer += 4;
                    inn_bit_buffer += 4;
                }
                in8_bit_buffer += in_8_stride_diff;
                inn_bit_buffer += in_n_stride_diff;
                out16_bit_buffer += out_stride_diff;
            }
        }
    }
}

void svt_full_distortion_kernel_cbf_zero32_bits_neon(int32_t *coeff, uint32_t coeff_stride,
                                                     uint64_t distortion_result[DIST_CALC_TOTAL], uint32_t area_width,
                                                     uint32_t area_height) {
    uint64x2_t sum = vdupq_n_u64(0);

    uint32_t row_count = area_height;
    do {
        int32_t *coeff_temp = coeff;

        uint32_t col_count = area_width / 4;
        do {
            const int32x2_t x_lo = vld1_s32(coeff_temp + 0);
            const int32x2_t x_hi = vld1_s32(coeff_temp + 2);
            coeff_temp += 4;

            const uint64x2_t y_lo = vreinterpretq_u64_s64(vmull_s32(x_lo, x_lo));
            const uint64x2_t y_hi = vreinterpretq_u64_s64(vmull_s32(x_hi, x_hi));

            sum = vaddq_u64(sum, y_lo);
            sum = vaddq_u64(sum, y_hi);

        } while (--col_count);

        coeff += coeff_stride;
        row_count -= 1;
    } while (row_count > 0);

    const uint64x2_t temp2 = vextq_u64(sum, sum, 1);
    const uint64x2_t temp1 = vaddq_u64(sum, temp2);
    vst1q_u64(distortion_result, temp1);
}

/******************************************************************************************************
                                       svt_residual_kernel16bit_neon
******************************************************************************************************/
void svt_residual_kernel16bit_neon(uint16_t *input, uint32_t input_stride, uint16_t *pred, uint32_t pred_stride,
                                   int16_t *residual, uint32_t residual_stride, uint32_t area_width,
                                   uint32_t area_height) {
    if (area_width == 4) {
        for (uint32_t height = 0; height < area_height; height += 2) {
            const uint16x4_t residual64_0 = vsub_u16(vld1_u16(input), vld1_u16(pred));
            const uint16x4_t residual64_1 = vsub_u16(vld1_u16((input + input_stride)), vld1_u16((pred + pred_stride)));

            vst1_s16(residual, vreinterpret_s16_u16(residual64_0));
            vst1_s16((residual + residual_stride), vreinterpret_s16_u16(residual64_1));

            input += input_stride << 1;
            pred += pred_stride << 1;
            residual += residual_stride << 1;
        }
    } else if (area_width == 8) {
        for (uint32_t height = 0; height < area_height; height += 2) {
            const uint16x8_t residual0 = vsubq_u16(vld1q_u16(input), vld1q_u16(pred));
            const uint16x8_t residual1 = vsubq_u16(vld1q_u16((input + input_stride)), vld1q_u16((pred + pred_stride)));

            vst1q_s16(residual, vreinterpretq_s16_u16(residual0));
            vst1q_s16((residual + residual_stride), vreinterpretq_s16_u16(residual1));

            input += input_stride << 1;
            pred += pred_stride << 1;
            residual += residual_stride << 1;
        }
    } else if (area_width == 16) {
        for (uint32_t height = 0; height < area_height; height += 2) {
            const uint16x8_t residual0 = vsubq_u16(vld1q_u16(input), vld1q_u16(pred));
            const uint16x8_t residual1 = vsubq_u16(vld1q_u16((input + 8)), vld1q_u16((pred + 8)));
            const uint16x8_t residual2 = vsubq_u16(vld1q_u16((input + input_stride)), vld1q_u16((pred + pred_stride)));
            const uint16x8_t residual3 = vsubq_u16(vld1q_u16((input + input_stride + 8)),
                                                   vld1q_u16((pred + pred_stride + 8)));

            vst1q_s16(residual, vreinterpretq_s16_u16(residual0));
            vst1q_s16((residual + 8), vreinterpretq_s16_u16(residual1));
            vst1q_s16((residual + residual_stride), vreinterpretq_s16_u16(residual2));
            vst1q_s16((residual + residual_stride + 8), vreinterpretq_s16_u16(residual3));

            input += input_stride << 1;
            pred += pred_stride << 1;
            residual += residual_stride << 1;
        }
    } else if (area_width == 32) {
        for (uint32_t height = 0; height < area_height; height += 2) {
            vst1q_s16(residual, vreinterpretq_s16_u16(vsubq_u16(vld1q_u16(input), vld1q_u16(pred))));
            vst1q_s16((residual + 8), vreinterpretq_s16_u16(vsubq_u16(vld1q_u16((input + 8)), vld1q_u16((pred + 8)))));
            vst1q_s16((residual + 16),
                      vreinterpretq_s16_u16(vsubq_u16(vld1q_u16((input + 16)), vld1q_u16((pred + 16)))));
            vst1q_s16((residual + 24),
                      vreinterpretq_s16_u16(vsubq_u16(vld1q_u16((input + 24)), vld1q_u16((pred + 24)))));

            vst1q_s16(
                (residual + residual_stride),
                vreinterpretq_s16_u16(vsubq_u16(vld1q_u16((input + input_stride)), vld1q_u16((pred + pred_stride)))));
            vst1q_s16((residual + residual_stride + 8),
                      vreinterpretq_s16_u16(
                          vsubq_u16(vld1q_u16((input + input_stride + 8)), vld1q_u16((pred + pred_stride + 8)))));
            vst1q_s16((residual + residual_stride + 16),
                      vreinterpretq_s16_u16(
                          vsubq_u16(vld1q_u16((input + input_stride + 16)), vld1q_u16((pred + pred_stride + 16)))));
            vst1q_s16((residual + residual_stride + 24),
                      vreinterpretq_s16_u16(
                          vsubq_u16(vld1q_u16((input + input_stride + 24)), vld1q_u16((pred + pred_stride + 24)))));

            input += input_stride << 1;
            pred += pred_stride << 1;
            residual += residual_stride << 1;
        }
    } else if (area_width == 64) { // Branch was not tested because the encoder had max txb_size of 32
        for (uint32_t height = 0; height < area_height; height += 2) {
            vst1q_s16(residual, vreinterpretq_s16_u16(vsubq_u16(vld1q_u16(input), vld1q_u16(pred))));
            vst1q_s16((residual + 8), vreinterpretq_s16_u16(vsubq_u16(vld1q_u16((input + 8)), vld1q_u16((pred + 8)))));
            vst1q_s16((residual + 16),
                      vreinterpretq_s16_u16(vsubq_u16(vld1q_u16((input + 16)), vld1q_u16((pred + 16)))));
            vst1q_s16((residual + 24),
                      vreinterpretq_s16_u16(vsubq_u16(vld1q_u16((input + 24)), vld1q_u16((pred + 24)))));
            vst1q_s16((residual + 32),
                      vreinterpretq_s16_u16(vsubq_u16(vld1q_u16((input + 32)), vld1q_u16((pred + 32)))));
            vst1q_s16((residual + 40),
                      vreinterpretq_s16_u16(vsubq_u16(vld1q_u16((input + 40)), vld1q_u16((pred + 40)))));
            vst1q_s16((residual + 48),
                      vreinterpretq_s16_u16(vsubq_u16(vld1q_u16((input + 48)), vld1q_u16((pred + 48)))));
            vst1q_s16((residual + 56),
                      vreinterpretq_s16_u16(vsubq_u16(vld1q_u16((input + 56)), vld1q_u16((pred + 56)))));

            vst1q_s16(
                (residual + residual_stride),
                vreinterpretq_s16_u16(vsubq_u16(vld1q_u16((input + input_stride)), vld1q_u16((pred + pred_stride)))));
            vst1q_s16((residual + residual_stride + 8),
                      vreinterpretq_s16_u16(
                          vsubq_u16(vld1q_u16((input + input_stride + 8)), vld1q_u16((pred + pred_stride + 8)))));
            vst1q_s16((residual + residual_stride + 16),
                      vreinterpretq_s16_u16(
                          vsubq_u16(vld1q_u16((input + input_stride + 16)), vld1q_u16((pred + pred_stride + 16)))));
            vst1q_s16((residual + residual_stride + 24),
                      vreinterpretq_s16_u16(
                          vsubq_u16(vld1q_u16((input + input_stride + 24)), vld1q_u16((pred + pred_stride + 24)))));
            vst1q_s16((residual + residual_stride + 32),
                      vreinterpretq_s16_u16(
                          vsubq_u16(vld1q_u16((input + input_stride + 32)), vld1q_u16((pred + pred_stride + 32)))));
            vst1q_s16((residual + residual_stride + 40),
                      vreinterpretq_s16_u16(
                          vsubq_u16(vld1q_u16((input + input_stride + 40)), vld1q_u16((pred + pred_stride + 40)))));
            vst1q_s16((residual + residual_stride + 48),
                      vreinterpretq_s16_u16(
                          vsubq_u16(vld1q_u16((input + input_stride + 48)), vld1q_u16((pred + pred_stride + 48)))));
            vst1q_s16((residual + residual_stride + 56),
                      vreinterpretq_s16_u16(
                          vsubq_u16(vld1q_u16((input + input_stride + 56)), vld1q_u16((pred + pred_stride + 56)))));

            input += input_stride << 1;
            pred += pred_stride << 1;
            residual += residual_stride << 1;
        }
    } else {
        const uint32_t input_stride_diff    = 2 * input_stride - area_width;
        const uint32_t pred_stride_diff     = 2 * pred_stride - area_width;
        const uint32_t residual_stride_diff = 2 * residual_stride - area_width;

        if (!(area_width & 7)) {
            for (uint32_t height = 0; height < area_height; height += 2) {
                for (uint32_t width = 0; width < area_width; width += 8) {
                    vst1q_s16(residual, vreinterpretq_s16_u16(vsubq_u16(vld1q_u16(input), vld1q_u16(pred))));
                    vst1q_s16((residual + residual_stride),
                              vreinterpretq_s16_u16(
                                  vsubq_u16(vld1q_u16((input + input_stride)), vld1q_u16((pred + pred_stride)))));

                    input += 8;
                    pred += 8;
                    residual += 8;
                }
                input    = input + input_stride_diff;
                pred     = pred + pred_stride_diff;
                residual = residual + residual_stride_diff;
            }
        } else {
            for (uint32_t height = 0; height < area_height; height += 2) {
                for (uint32_t width = 0; width < area_width; width += 4) {
                    vst1_s16(residual,
                             vreinterpret_s16_u16(vget_low_u16(vsubq_u16(vld1q_u16(input), vld1q_u16(pred)))));
                    vst1_s16((residual + residual_stride),
                             vreinterpret_s16_u16(vget_low_u16(
                                 vsubq_u16(vld1q_u16((input + input_stride)), vld1q_u16((pred + pred_stride))))));

                    input += 4;
                    pred += 4;
                    residual += 4;
                }
                input += input_stride_diff;
                pred += pred_stride_diff;
                residual += residual_stride_diff;
            }
        }
    }
}
