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

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "utility.h"
#include "sequence_control_set.h"
#include "pcs.h"
#include "md_process.h"

uint64_t svt_psy_distortion(const uint8_t *input, const uint32_t input_stride, const uint8_t *recon,
                            const uint32_t recon_stride, const uint32_t width, const uint32_t height,
                            const double effective_ac_bias, const double effective_energy_bias, double effective_satd_bias,
                            const QmVal *satd_bias_qmatrix);
uint64_t svt_psy_distortion_hbd(const uint16_t *input, const uint32_t input_stride, const uint16_t *recon,
                                const uint32_t recon_stride, const uint32_t width, const uint32_t height,
                                const double effective_ac_bias, const double effective_energy_bias, double effective_satd_bias,
                                const QmVal *satd_bias_qmatrix);

uint64_t get_svt_psy_full_dist(const void *s, uint32_t so, uint32_t sp, const void *r, uint32_t ro, uint32_t rp,
                               const uint32_t w, const uint32_t h, const uint8_t is_hbd,
                               const double effective_ac_bias, const double effective_energy_bias, double effective_satd_bias,
                               const QmVal *satd_bias_qmatrix);
uint64_t get_psy_dist_satd_bias_only(const void *s, const uint32_t so, const uint32_t sp, const void *r, const uint32_t ro,
                                     const uint32_t rp, const uint32_t w, const uint32_t h, const uint8_t is_hbd,
                                     double effective_satd_bias,
                                     const QmVal *satd_bias_qmatrix);
uint64_t svt_psy_adjust_rate_light(const int32_t *coeff, uint64_t coeff_bits, const uint32_t bwidth,
                                   const uint32_t bheight, const double ac_bias);
double   get_effective_ac_bias(const double ac_bias, const bool is_islice, const uint8_t temporal_layer_index);
double   get_psy_bias_effective_ac_bias(PictureControlSet *pcs, ModeDecisionContext *ctx);
double   get_psy_bias_effective_energy_bias(PictureControlSet *pcs, ModeDecisionContext *ctx);
double   get_effective_satd_bias(PictureControlSet *pcs, ModeDecisionContext *ctx);

#ifdef __cplusplus
}
#endif
