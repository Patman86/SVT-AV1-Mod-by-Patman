/*
* Copyright(c) 2024-2025 Psychovisual Experts Group
*
* This source code is subject to the terms of the BSD 2 Clause License and
* the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
* was not distributed with this source code in the LICENSE file, you can
* obtain it at https://www.aomedia.org/license/software-license. If the Alliance for Open
* Media Patent License 1.0 was not distributed with this source code in the
* PATENTS file, you can obtain it at https://www.aomedia.org/license/patent-license.
*/

#include <math.h>
#include <stdbool.h>
#include "utility.h"
#include "ac_bias.h"
#include "aom_dsp_rtcd.h"
#include "me_context.h"
#include "segmentation.h"
#include "sequence_control_set.h"
#include "pcs.h"
#include "md_process.h"

// size must be multipies of 8; Make sure you pass the offset matrix for the correct size
uint64_t qm_satd_no_rshift_c(const TranLow *input_coeffs, const TranLow *recon_coeffs,
                             const QmVal *satd_bias_qmatrix, const uint16_t size) {
    uint64_t satd_dist = 0;

    if (satd_bias_qmatrix != NULL) {
        for (size_t k = 0; k < size; k++)
            satd_dist += ABS(input_coeffs[k] - recon_coeffs[k]) * satd_bias_qmatrix[k];
    }
    else {
        for (size_t k = 0; k < size; k++)
            satd_dist += ABS(input_coeffs[k] - recon_coeffs[k]) * 32;
    }

    return satd_dist;
}

/* Regular version of "AC Bias"
 *
 * Based on adding an "energy gap" term to each candidate block's distortion, which is the difference
 * of the "energy" (SATD - SAD) of the source and recon blocks
 *
 * 1.0 is disabled for effective_energy_bias
 * 0.0 is disabled for effective_satd_bias
 */
uint64_t svt_psy_distortion(const uint8_t *input, const uint32_t input_stride, const uint8_t *recon,
                            const uint32_t recon_stride, const uint32_t width, const uint32_t height,
                            const double effective_ac_bias, const double effective_energy_bias, const double effective_satd_bias,
                            const QmVal *satd_bias_qmatrix) {
    uint64_t energy_gap = 0;
    uint64_t satd_dist = 0;
    int32_t input_coeffs[64];
    int32_t recon_coeffs[64];
    int16_t block_as_16bit[64];
    int32_t input_energy;
    int32_t recon_energy;

    if (width >= 8 && height >= 8) { /* >8x8 */
        for (uint32_t j = 0; j < height; j += 8) {
            for (uint32_t i = 0; i < width; i += 8) {
                const uint8_t *block_input = input + j * input_stride + i;
                const uint8_t *recon_input = recon + j * recon_stride + i;

                for (int h = 0; h < 8; h++) {
                    for (int w = 0; w < 8; w++) { block_as_16bit[h * 8 + w] = block_input[w]; }

                    block_input += input_stride;
                }

                svt_aom_hadamard_8x8(block_as_16bit, 8, input_coeffs);

                // input_energy = ((svt_aom_satd(input_coeffs, 64) + 2) >> 2) - ((input_coeffs[0] + 2) >> 2);
                input_energy = lrint((svt_aom_satd(input_coeffs, 64) - input_coeffs[0]) * (effective_energy_bias * 0.25));

                for (int h = 0; h < 8; h++) {
                    for (int w = 0; w < 8; w++) { block_as_16bit[h * 8 + w] = recon_input[w]; }

                    recon_input += recon_stride;
                }

                svt_aom_hadamard_8x8(block_as_16bit, 8, recon_coeffs);

                // recon_energy = ((svt_aom_satd(recon_coeffs, 64) + 2) >> 2) - ((recon_coeffs[0] + 2) >> 2);
                recon_energy = ((svt_aom_satd(recon_coeffs, 64) - recon_coeffs[0] + 2) >> 2);

                energy_gap += abs(input_energy - recon_energy);

                if (effective_satd_bias)
                    satd_dist += qm_satd_no_rshift(input_coeffs, recon_coeffs, satd_bias_qmatrix + 16, 64);
            }
        }
    } else {
        for (uint32_t j = 0; j < height; j += 4) { /* 4x4, 4x8, 4x16, 8x4, and 16x4 */
            for (uint32_t i = 0; i < width; i += 4) {
                const uint8_t *block_input = input + j * input_stride + i;
                const uint8_t *recon_input = recon + j * recon_stride + i;

                for (int h = 0; h < 4; h++) {
                    for (int w = 0; w < 4; w++) { block_as_16bit[h * 4 + w] = block_input[w]; }

                    block_input += input_stride;
                }

                svt_aom_hadamard_4x4(block_as_16bit, 4, input_coeffs);

                // input_energy = (svt_aom_satd(input_coeffs, 16) << 1) - input_coeffs[0];
                input_energy = lrint(((svt_aom_satd(input_coeffs, 16) << 1) - input_coeffs[0]) * effective_energy_bias);

                for (int h = 0; h < 4; h++) {
                    for (int w = 0; w < 4; w++) { block_as_16bit[h * 4 + w] = recon_input[w]; }

                    recon_input += recon_stride;
                }

                svt_aom_hadamard_4x4(block_as_16bit, 4, recon_coeffs);

                recon_energy = (svt_aom_satd(recon_coeffs, 16) << 1) - recon_coeffs[0];

                energy_gap += abs(input_energy - recon_energy);

                if (effective_satd_bias)
                    satd_dist += qm_satd_no_rshift(input_coeffs, recon_coeffs, satd_bias_qmatrix, 16);
            }
        }
    }

    return llrint(energy_gap * effective_ac_bias + satd_dist * (effective_satd_bias * ((double)1/32)));
}

uint64_t psy_distortion_satd_bias_only(const uint8_t *input, const uint32_t input_stride, const uint8_t *recon,
                                       const uint32_t recon_stride, const uint32_t width, const uint32_t height,
                                       const double effective_satd_bias,
                                       const QmVal *satd_bias_qmatrix) {
    uint64_t satd_dist = 0;
    int32_t input_coeffs[64];
    int32_t recon_coeffs[64];
    int16_t block_as_16bit[64];

    if (width >= 8 && height >= 8) { /* >8x8 */
        for (uint32_t j = 0; j < height; j += 8) {
            for (uint32_t i = 0; i < width; i += 8) {
                const uint8_t *block_input = input + j * input_stride + i;
                const uint8_t *recon_input = recon + j * recon_stride + i;

                for (int h = 0; h < 8; h++) {
                    for (int w = 0; w < 8; w++) { block_as_16bit[h * 8 + w] = block_input[w]; }

                    block_input += input_stride;
                }

                svt_aom_hadamard_8x8(block_as_16bit, 8, input_coeffs);

                for (int h = 0; h < 8; h++) {
                    for (int w = 0; w < 8; w++) { block_as_16bit[h * 8 + w] = recon_input[w]; }

                    recon_input += recon_stride;
                }

                svt_aom_hadamard_8x8(block_as_16bit, 8, recon_coeffs);

                satd_dist += qm_satd_no_rshift(input_coeffs, recon_coeffs, satd_bias_qmatrix + 16, 64);
            }
        }
    } else {
        for (uint32_t j = 0; j < height; j += 4) { /* 4x4, 4x8, 4x16, 8x4, and 16x4 */
            for (uint32_t i = 0; i < width; i += 4) {
                const uint8_t *block_input = input + j * input_stride + i;
                const uint8_t *recon_input = recon + j * recon_stride + i;

                for (int h = 0; h < 4; h++) {
                    for (int w = 0; w < 4; w++) { block_as_16bit[h * 4 + w] = block_input[w]; }

                    block_input += input_stride;
                }

                svt_aom_hadamard_4x4(block_as_16bit, 4, input_coeffs);

                for (int h = 0; h < 4; h++) {
                    for (int w = 0; w < 4; w++) { block_as_16bit[h * 4 + w] = recon_input[w]; }

                    recon_input += recon_stride;
                }

                svt_aom_hadamard_4x4(block_as_16bit, 4, recon_coeffs);

                satd_dist += qm_satd_no_rshift(input_coeffs, recon_coeffs, satd_bias_qmatrix, 16);
            }
        }
    }

    return llrint(satd_dist * (effective_satd_bias * ((double)1/32)));
}

/* High bit-depth version of "AC Bias" */
/* 1.0 is disabled for effective_energy_bias */
/* 0.0 is disabled for effective_satd_bias */
uint64_t svt_psy_distortion_hbd(const uint16_t *input, const uint32_t input_stride, const uint16_t *recon,
                                const uint32_t recon_stride, const uint32_t width, const uint32_t height,
                                const double effective_ac_bias, const double effective_energy_bias, const double effective_satd_bias,
                                const QmVal *satd_bias_qmatrix) {
    uint64_t energy_gap = 0;
    uint64_t satd_dist = 0;
    int32_t input_coeffs[64];
    int32_t recon_coeffs[64];
    int32_t input_energy;
    int32_t recon_energy;

    if (width >= 8 && height >= 8) { /* >8x8 */
        for (uint32_t j = 0; j < height; j += 8) {
            for (uint32_t i = 0; i < width; i += 8) {
                svt_aom_highbd_hadamard_8x8((int16_t *)input + j * input_stride + i, input_stride, input_coeffs);

                // input_energy = ((svt_aom_satd(coeffs, 64) + 2) >> 2) - ((coeffs[0] + 2) >> 2);
                input_energy = lrint((svt_aom_satd(input_coeffs, 64) - input_coeffs[0]) * (effective_energy_bias * 0.25));

                svt_aom_highbd_hadamard_8x8((int16_t *)recon + j * recon_stride + i, recon_stride, recon_coeffs);

                // recon_energy = ((svt_aom_satd(coeffs, 64) + 2) >> 2) - ((coeffs[0] + 2) >> 2);
                recon_energy = ((svt_aom_satd(recon_coeffs, 64) - recon_coeffs[0] + 2) >> 2);

                energy_gap += abs(input_energy - recon_energy);

                if (effective_satd_bias)
                    satd_dist += qm_satd_no_rshift(input_coeffs, recon_coeffs, satd_bias_qmatrix + 16, 64);
            }
        }
    } else {
        for (uint64_t j = 0; j < height; j += 4) { /* 4x4, 4x8, 4x16, 8x4, and 16x4 */
            for (uint64_t i = 0; i < width; i += 4) {
                // HBD coefficients can fit in 16 bits, so the regular Hadamard 4x4 function can be used here safely
                svt_aom_hadamard_4x4((int16_t *)input + j * input_stride + i, input_stride, input_coeffs);

                // input_energy = (svt_aom_satd(coeffs, 16) << 1) - coeffs[0];
                input_energy = lrint(((svt_aom_satd(input_coeffs, 16) << 1) - input_coeffs[0]) * effective_energy_bias);

                svt_aom_hadamard_4x4((int16_t *)recon + j * recon_stride + i, recon_stride, recon_coeffs);

                recon_energy = (svt_aom_satd(recon_coeffs, 16) << 1) - recon_coeffs[0];

                energy_gap += abs(input_energy - recon_energy);

                if (effective_satd_bias)
                    satd_dist += qm_satd_no_rshift(input_coeffs, recon_coeffs, satd_bias_qmatrix, 16);
            }
        }
    }

    // Energy is scaled to approximately match equivalent 8-bit strengths
    return llrint((energy_gap << 2) * effective_ac_bias + satd_dist * (effective_satd_bias * ((double)1/8)));
}

uint64_t psy_distortion_satd_bias_only_hbd(const uint16_t *input, const uint32_t input_stride, const uint16_t *recon,
                                           const uint32_t recon_stride, const uint32_t width, const uint32_t height,
                                           const double effective_satd_bias,
                                           const QmVal *satd_bias_qmatrix) {
    uint64_t satd_dist = 0;
    int32_t input_coeffs[64];
    int32_t recon_coeffs[64];

    if (width >= 8 && height >= 8) { /* >8x8 */
        for (uint32_t j = 0; j < height; j += 8) {
            for (uint32_t i = 0; i < width; i += 8) {
                svt_aom_highbd_hadamard_8x8((int16_t *)input + j * input_stride + i, input_stride, input_coeffs);

                svt_aom_highbd_hadamard_8x8((int16_t *)recon + j * recon_stride + i, recon_stride, recon_coeffs);

                satd_dist += qm_satd_no_rshift(input_coeffs, recon_coeffs, satd_bias_qmatrix + 16, 64);
            }
        }
    } else {
        for (uint64_t j = 0; j < height; j += 4) { /* 4x4, 4x8, 4x16, 8x4, and 16x4 */
            for (uint64_t i = 0; i < width; i += 4) {
                // HBD coefficients can fit in 16 bits, so the regular Hadamard 4x4 function can be used here safely
                svt_aom_hadamard_4x4((int16_t *)input + j * input_stride + i, input_stride, input_coeffs);

                svt_aom_hadamard_4x4((int16_t *)recon + j * recon_stride + i, recon_stride, recon_coeffs);

                satd_dist += qm_satd_no_rshift(input_coeffs, recon_coeffs, satd_bias_qmatrix, 16);
            }
        }
    }

    // Energy is scaled to approximately match equivalent 8-bit strengths
    return llrint(satd_dist * (effective_satd_bias * ((double)1/8)));
}

/*
 * Public function that mirrors the arguments of `spatial_full_dist_type_fun()`
 *
 * 1.0 is disabled for effective_energy_bias
 * 0.0 is disabled for effective_satd_bias
 */
uint64_t get_svt_psy_full_dist(const void *s, const uint32_t so, const uint32_t sp, const void *r, const uint32_t ro,
                               const uint32_t rp, const uint32_t w, const uint32_t h, const uint8_t is_hbd,
                               const double effective_ac_bias, const double effective_energy_bias, double effective_satd_bias,
                               const QmVal *satd_bias_qmatrix) {
    if (is_hbd)
        return svt_psy_distortion_hbd((const uint16_t *)s + so, sp, (uint16_t *)r + ro, rp, w, h,
                                      effective_ac_bias, effective_energy_bias, effective_satd_bias,
                                      satd_bias_qmatrix);
    else
        return svt_psy_distortion((const uint8_t *)s + so, sp, (const uint8_t *)r + ro, rp, w, h,
                                  effective_ac_bias, effective_energy_bias, effective_satd_bias,
                                  satd_bias_qmatrix);
}

uint64_t get_psy_dist_satd_bias_only(const void *s, const uint32_t so, const uint32_t sp, const void *r, const uint32_t ro,
                                     const uint32_t rp, const uint32_t w, const uint32_t h, const uint8_t is_hbd,
                                     double effective_satd_bias,
                                     const QmVal *satd_bias_qmatrix) {
    if (is_hbd)
        return psy_distortion_satd_bias_only_hbd((const uint16_t *)s + so, sp, (uint16_t *)r + ro, rp, w, h,
                                                 effective_satd_bias,
                                                 satd_bias_qmatrix);
    else
        return psy_distortion_satd_bias_only((const uint8_t *)s + so, sp, (const uint8_t *)r + ro, rp, w, h,
                                             effective_satd_bias,
                                             satd_bias_qmatrix);
}

/*
 * Light version of "AC Bias", called by the Light-PD code paths
 *
 * Based on adjusting each block's rate so blocks with more energy (sum of AC coeffs) appear "cheaper" to the encoder,
 * thus making them more favorable to be picked by the RDO process. This tends to increase the image's total "energy"
 * (in contrast to `get_svt_psy_full_dist()` which tries to reduce the "energy gap" between source and recon)
 *
 * Much faster than `get_svt_psy_full_dist()` as it can re-use existing block coefficients instead of computing new
 * ones, but subjective visual quality benefits are significantly more modest
 */
uint64_t svt_psy_adjust_rate_light(const int32_t *coeff, uint64_t coeff_bits, const uint32_t width,
                                   const uint32_t height, const double ac_bias) {
    uint64_t       energy = 0;
    const int32_t *buf    = coeff;

    for (uint32_t j = 0; j < height; j++) {
        // Skip the DC coefficient from the calculation
        for (uint32_t i = j ? 0 : 1; i < width; i++) { energy += (uint64_t)llabs((int64_t)buf[i]); }
        buf += width;
    }

    if (energy > 0) {
        uint64_t coeff_bits_adj = (int)(energy * ac_bias * 100);

        // When the adjustment rate is greater than the rate, keep rate (coeff_bits) positive
        coeff_bits = (coeff_bits > coeff_bits_adj) ? (coeff_bits - coeff_bits_adj) : 1;
    }

    return coeff_bits;
}

double get_effective_ac_bias(const double ac_bias, const bool is_islice, const uint8_t temporal_layer_index) {
    if (is_islice)
        return ac_bias * 0.3;
    switch (temporal_layer_index) {
    case 0: return ac_bias * 0.6;
    case 1: return ac_bias * 0.8;
    case 2: return ac_bias * 0.9;
    default: return ac_bias;
    }
}

double get_psy_bias_effective_ac_bias(PictureControlSet *pcs, ModeDecisionContext *ctx) {
    uint8_t temporal_layer_index;
    if (!pcs->scs->static_config.balancing_q_bias)
        temporal_layer_index = pcs->ppcs->temporal_layer_index;
    else {
        temporal_layer_index = (pcs->ppcs->temporal_layer_index + pcs->scs->static_config.hierarchical_levels - pcs->ppcs->hierarchical_levels);
        if (pcs->ppcs->slice_type == I_SLICE)
            temporal_layer_index = 0;
    }
    const bool is_islice = pcs->ppcs->slice_type == I_SLICE;

    const double effective_ac_bias = get_effective_ac_bias(pcs->scs->static_config.ac_bias, is_islice, temporal_layer_index);
    const double effective_texture_ac_bias = get_effective_ac_bias(pcs->scs->static_config.texture_ac_bias, is_islice, temporal_layer_index);

    const uint16_t blk_variance = get_variance_for_cu_16x16_min(ctx->blk_geom, pcs->ppcs->variance[ctx->sb_index]);

    if (blk_variance >= pcs->scs->static_config.lineart_variance_thr >> 1)
        return effective_ac_bias;
    else if (blk_variance >= pcs->scs->static_config.texture_variance_thr >> 2)
        return effective_ac_bias * ((double)2/3) + effective_texture_ac_bias * ((double)1/3);
    else
        return effective_texture_ac_bias;
}

double get_psy_bias_effective_energy_bias(PictureControlSet *pcs, ModeDecisionContext *ctx) {
    const uint16_t blk_variance = get_variance_for_cu_16x16_min(ctx->blk_geom, pcs->ppcs->variance[ctx->sb_index]);

    if (blk_variance >= pcs->scs->static_config.lineart_variance_thr)
        return pcs->scs->static_config.lineart_energy_bias;
    else if (blk_variance >= pcs->scs->static_config.lineart_variance_thr >> 2)
        return 1.0;
    else if (blk_variance >= pcs->scs->static_config.texture_variance_thr >> 3)
        return (pcs->scs->static_config.texture_energy_bias - 1.0) * ((double)1/3) + 1.0;
    else
        return pcs->scs->static_config.texture_energy_bias;
}

double get_effective_satd_bias(PictureControlSet *pcs, ModeDecisionContext *ctx) {
    UNUSED(ctx);

    uint8_t temporal_layer_index;
    if (!pcs->scs->static_config.balancing_q_bias)
        temporal_layer_index = pcs->ppcs->temporal_layer_index;
    else {
        temporal_layer_index = (pcs->ppcs->temporal_layer_index + pcs->scs->static_config.hierarchical_levels - pcs->ppcs->hierarchical_levels);
        if (pcs->ppcs->slice_type == I_SLICE)
            temporal_layer_index = 0;
    }
    const bool is_islice = pcs->ppcs->slice_type == I_SLICE;

    return get_effective_ac_bias(pcs->scs->static_config.satd_bias, is_islice, temporal_layer_index);
}
