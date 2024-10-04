/*
 * Copyright (c) 2016, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include "mcomp.h"
#include "mv.h"
#include "av1_common.h"
#include "coding_unit.h"
#include "block_structures.h"
#include "av1me.h"
#include "aom_dsp_rtcd.h"
#include "rd_cost.h"
// ============================================================================
//  Cost of motion vectors
// ============================================================================
// TODO(any): Adaptively adjust the regularization strength based on image size
// and motion activity instead of using hard-coded values. It seems like we
// roughly half the lambda for each increase in resolution
// These are multiplier used to perform regularization in motion compensation
// when x->mv_cost_type is set to MV_COST_L1.
// LOWRES
#define SSE_LAMBDA_LOWRES 2 // Used by mv_cost_err_fn
// #define SAD_LAMBDA_LOWRES 32 // Used by mvsad_err_cost during full pixel search
// MIDRES
#define SSE_LAMBDA_MIDRES 0 // Used by mv_cost_err_fn
// #define SAD_LAMBDA_MIDRES 15 // Used by mvsad_err_cost during full pixel search
// HDRES
#define SSE_LAMBDA_HDRES 1 // Used by mv_cost_err_fn
// #define SAD_LAMBDA_HDRES 8 // Used by mvsad_err_cost during full pixel search

// Returns the cost of using the current mv during the motion search. This is
// used when var is used as the error metric.
#define PIXEL_TRANSFORM_ERROR_SCALE 4
static INLINE int svt_mv_err_cost(const MV *mv, const MV *ref_mv, const int *mvjcost, const int *const mvcost[2],
                                  int error_per_bit, MV_COST_TYPE mv_cost_type) {
    const MV diff     = {mv->row - ref_mv->row, mv->col - ref_mv->col};
    const MV abs_diff = {abs(diff.row), abs(diff.col)};

    switch (mv_cost_type) {
    case MV_COST_ENTROPY:
        if (mvcost) {
            return (int)ROUND_POWER_OF_TWO_64(
                (int64_t)svt_mv_cost(&diff, mvjcost, mvcost) * error_per_bit,
                RDDIV_BITS + AV1_PROB_COST_SHIFT - RD_EPB_SHIFT + PIXEL_TRANSFORM_ERROR_SCALE);
        }
        return 0;
    case MV_COST_L1_LOWRES: return (SSE_LAMBDA_LOWRES * (abs_diff.row + abs_diff.col)) >> 3;
    case MV_COST_L1_MIDRES: return (SSE_LAMBDA_MIDRES * (abs_diff.row + abs_diff.col)) >> 3;
    case MV_COST_L1_HDRES: return (SSE_LAMBDA_HDRES * (abs_diff.row + abs_diff.col)) >> 3;
    case MV_COST_OPT: {
        return (int)ROUND_POWER_OF_TWO_64(
            (int64_t)((abs_diff.row + abs_diff.col) << 8) * error_per_bit,
            RDDIV_BITS + AV1_PROB_COST_SHIFT - RD_EPB_SHIFT + PIXEL_TRANSFORM_ERROR_SCALE);
    }
    case MV_COST_NONE: return 0;
    default: assert(0 && "Invalid rd_cost_type"); return 0;
    }
}

static INLINE int svt_mv_err_cost_(const MV *mv, const MV_COST_PARAMS *mv_cost_params) {
    return svt_mv_err_cost(mv,
                           mv_cost_params->ref_mv,
                           mv_cost_params->mvjcost,
                           mv_cost_params->mvcost,
                           mv_cost_params->error_per_bit,
                           mv_cost_params->mv_cost_type);
}

// =============================================================================
//  Subpixel Motion Search: Translational
// =============================================================================
#define INIT_SUBPEL_STEP_SIZE (4)

/*
 * To avoid the penalty for crossing cache-line read, preload the reference
 * area in a small buffer, which is aligned to make sure there won't be crossing
 * cache-line read while reading from this buffer. This reduced the cpu
 * cycles spent on reading ref data in sub-pixel filter functions.
 * TODO: Currently, since sub-pixel search range here is -3 ~ 3, copy 22 rows x
 * 32 cols area that is enough for 16x16 macroblock. Later, for SPLITMV, we
 * could reduce the area.
 */

// Returns the subpel offset used by various subpel variance functions [m]sv[a]f
static INLINE int svt_get_subpel_part(int x) { return x & 7; }

// Gets the address of the ref buffer at subpel location (r, c), rounded to the
// nearest fullpel precision toward - \infty

static INLINE const uint8_t *svt_get_buf_from_mv(const struct svt_buf_2d *buf, const MV mv) {
    const int offset = (mv.row >> 3) * buf->stride + (mv.col >> 3);
    return &buf->buf[offset];
}

// Calculates the variance of prediction residue.
static int svt_upsampled_pref_error(MacroBlockD *xd, const struct AV1Common *const cm, const MV *this_mv,
                                    const SUBPEL_SEARCH_VAR_PARAMS *var_params, unsigned int *sse) {
    const AomVarianceFnPtr  *vfp                = var_params->vfp;
    const SUBPEL_SEARCH_TYPE subpel_search_type = var_params->subpel_search_type;

    const MSBuffers *ms_buffers  = &var_params->ms_buffers;
    const uint8_t   *src         = ms_buffers->src->buf;
    const uint8_t   *ref         = svt_get_buf_from_mv(ms_buffers->ref, *this_mv);
    const int        src_stride  = ms_buffers->src->stride;
    const int        ref_stride  = ms_buffers->ref->stride;
    const int        w           = var_params->w;
    const int        h           = var_params->h;
    const int        mi_row      = xd->mi_row;
    const int        mi_col      = xd->mi_col;
    const int        subpel_x_q3 = svt_get_subpel_part(this_mv->col);
    const int        subpel_y_q3 = svt_get_subpel_part(this_mv->row);

    unsigned int besterr;
    {
        DECLARE_ALIGNED(16, uint8_t, pred[MAX_SB_SQUARE]);

        {
            svt_aom_upsampled_pred(xd,
                                   cm,
                                   mi_row,
                                   mi_col,
                                   this_mv,
                                   pred,
                                   w,
                                   h,
                                   subpel_x_q3,
                                   subpel_y_q3,
                                   ref,
                                   ref_stride,
                                   subpel_search_type);
        }
        besterr = vfp->vf(pred, w, src, src_stride, sse);
    }

    return besterr;
}

// Estimates the variance of prediction residue using bilinear filter for fast
// search.
static INLINE int svt_estimated_pref_error(const MV *this_mv, const SUBPEL_SEARCH_VAR_PARAMS *var_params,
                                           unsigned int *sse) {
    const AomVarianceFnPtr *vfp = var_params->vfp;

    const MSBuffers *ms_buffers = &var_params->ms_buffers;
    const uint8_t   *src        = ms_buffers->src->buf;
    const uint8_t   *ref        = svt_get_buf_from_mv(ms_buffers->ref, *this_mv);
    const int        src_stride = ms_buffers->src->stride;
    const int        ref_stride = ms_buffers->ref->stride;
    //const uint8_t *second_pred = ms_buffers->second_pred;
    //const uint8_t *mask = ms_buffers->mask;
    //const int mask_stride = ms_buffers->mask_stride;
    //const int invert_mask = ms_buffers->inv_mask;

    const int subpel_x_q3 = svt_get_subpel_part(this_mv->col);
    const int subpel_y_q3 = svt_get_subpel_part(this_mv->row);

    // TODO: port other variance-related functions
    //if (second_pred == NULL) {
    return vfp->svf(ref, ref_stride, subpel_x_q3, subpel_y_q3, src, src_stride, sse);
    //}
    //else if (mask) {
    //    return vfp->msvf(ref, ref_stride, subpel_x_q3, subpel_y_q3, src, src_stride,
    //        second_pred, mask, mask_stride, invert_mask, sse);
    //}
    //else {
    //    return vfp->svaf(ref, ref_stride, subpel_x_q3, subpel_y_q3, src, src_stride,
    //        sse, second_pred);
    //}
}
#define BIAS_FP_WEIGHT 110
// Estimates whether this_mv is better than best_mv. This function incorporates
// both prediction error and residue into account. It is suffixed "fast" because
// it uses bilinear filter to estimate the prediction.
static INLINE unsigned int svt_check_better_fast(MacroBlockD *xd, const struct AV1Common *const cm, const MV *this_mv,
                                                 MV *best_mv, const SubpelMvLimits *mv_limits,
                                                 const SUBPEL_SEARCH_VAR_PARAMS *var_params,
                                                 const MV_COST_PARAMS *mv_cost_params, unsigned int *besterr,
                                                 unsigned int *sse1, int *distortion, int *has_better_mv,
                                                 int is_scaled) {
    unsigned int cost;
    if (svt_av1_is_subpelmv_in_range(mv_limits, *this_mv)) {
        unsigned int sse;
        int          thismse;
        cost = svt_mv_err_cost_(this_mv, mv_cost_params);
        if (mv_cost_params->mv_cost_type == MV_COST_OPT) {
            int64_t bestcost = *distortion + cost;
            if (bestcost > (((int64_t)*besterr * (int64_t)mv_cost_params->early_exit_th) / 1000))
                return (uint32_t)bestcost;
        }
        // TODO: add estimated func
        if (is_scaled) {
            thismse = svt_upsampled_pref_error(xd, cm, this_mv, var_params, &sse);
        } else {
            thismse = svt_estimated_pref_error(this_mv, var_params, &sse);
        }
        cost += thismse;
        int weight = 100;
        if (var_params->bias_fp && (*best_mv).col % 8 == 0 && (*best_mv).row % 8 == 0)
            weight = BIAS_FP_WEIGHT;

        if (((cost * weight) / 100) < *besterr) {
            *besterr    = cost;
            *best_mv    = *this_mv;
            *distortion = thismse;
            *sse1       = sse;
            *has_better_mv |= 1;
        }
    } else {
        cost = INT_MAX;
    }
    return cost;
}

// Checks whether this_mv is better than best_mv. This function incorporates
// both prediction error and residue into account.
static AOM_FORCE_INLINE unsigned int svt_check_better(MacroBlockD *xd, const struct AV1Common *const cm,
                                                      const MV *this_mv, MV *best_mv, const SubpelMvLimits *mv_limits,
                                                      const SUBPEL_SEARCH_VAR_PARAMS *var_params,
                                                      const MV_COST_PARAMS *mv_cost_params, unsigned int *besterr,
                                                      unsigned int *sse1, int *distortion, int *is_better) {
    unsigned int cost;
    if (svt_av1_is_subpelmv_in_range(mv_limits, *this_mv)) {
        unsigned int sse;
        int          thismse;
        thismse = svt_upsampled_pref_error(xd, cm, this_mv, var_params, &sse);
        cost    = svt_mv_err_cost_(this_mv, mv_cost_params);
        cost += thismse;
        int weight = 100;
        if (var_params->bias_fp && (*best_mv).col % 8 == 0 && (*best_mv).row % 8 == 0)
            weight = BIAS_FP_WEIGHT;

        if (((cost * weight) / 100) < *besterr) {
            *besterr    = cost;
            *best_mv    = *this_mv;
            *distortion = thismse;
            *sse1       = sse;
            *is_better |= 1;
        }
    } else {
        cost = INT_MAX;
    }
    return cost;
}

static INLINE MV svt_get_best_diag_step(int step_size, unsigned int left_cost, unsigned int right_cost,
                                        unsigned int up_cost, unsigned int down_cost) {
    const MV diag_step = {up_cost <= down_cost ? -step_size : step_size,
                          left_cost <= right_cost ? -step_size : step_size};

    return diag_step;
}

static AOM_FORCE_INLINE MV svt_first_level_check(MacroBlockD *xd, const struct AV1Common *const cm, const MV this_mv,
                                                 MV *best_mv, const int hstep, const SubpelMvLimits *mv_limits,
                                                 const SUBPEL_SEARCH_VAR_PARAMS *var_params,
                                                 const MV_COST_PARAMS *mv_cost_params, unsigned int *besterr,
                                                 unsigned int *sse1, int *distortion) {
    int      dummy     = 0;
    const MV left_mv   = {this_mv.row, this_mv.col - hstep};
    const MV right_mv  = {this_mv.row, this_mv.col + hstep};
    const MV top_mv    = {this_mv.row - hstep, this_mv.col};
    const MV bottom_mv = {this_mv.row + hstep, this_mv.col};

    const unsigned int left = svt_check_better(
        xd, cm, &left_mv, best_mv, mv_limits, var_params, mv_cost_params, besterr, sse1, distortion, &dummy);
    const unsigned int right = svt_check_better(
        xd, cm, &right_mv, best_mv, mv_limits, var_params, mv_cost_params, besterr, sse1, distortion, &dummy);
    const unsigned int up = svt_check_better(
        xd, cm, &top_mv, best_mv, mv_limits, var_params, mv_cost_params, besterr, sse1, distortion, &dummy);
    const unsigned int down = svt_check_better(
        xd, cm, &bottom_mv, best_mv, mv_limits, var_params, mv_cost_params, besterr, sse1, distortion, &dummy);

    const MV diag_step = svt_get_best_diag_step(hstep, left, right, up, down);
    const MV diag_mv   = {this_mv.row + diag_step.row, this_mv.col + diag_step.col};

    // Check the diagonal direction with the best mv
    svt_check_better(
        xd, cm, &diag_mv, best_mv, mv_limits, var_params, mv_cost_params, besterr, sse1, distortion, &dummy);

    return diag_step;
}

// A newer version of second level check that gives better quality.
// TODO(chiyotsai@google.com): evaluate this on subpel_search_types different
// from av1_find_best_sub_pixel_tree
static AOM_FORCE_INLINE void svt_second_level_check_v2(MacroBlockD *xd, const struct AV1Common *const cm,
                                                       const MV this_mv, MV diag_step, MV *best_mv,
                                                       const SubpelMvLimits           *mv_limits,
                                                       const SUBPEL_SEARCH_VAR_PARAMS *var_params,
                                                       const MV_COST_PARAMS *mv_cost_params, unsigned int *besterr,
                                                       unsigned int *sse1, int *distortion, int is_scaled) {
    assert(best_mv->row == this_mv.row + diag_step.row || best_mv->col == this_mv.col + diag_step.col);
    if (CHECK_MV_EQUAL(this_mv, *best_mv)) {
        return;
    } else if (this_mv.row == best_mv->row) {
        // Search away from diagonal step since diagonal search did not provide any
        // improvement
        diag_step.row *= -1;
    } else if (this_mv.col == best_mv->col) {
        diag_step.col *= -1;
    }

    const MV row_bias_mv   = {best_mv->row + diag_step.row, best_mv->col};
    const MV col_bias_mv   = {best_mv->row, best_mv->col + diag_step.col};
    const MV diag_bias_mv  = {best_mv->row + diag_step.row, best_mv->col + diag_step.col};
    int      has_better_mv = 0;
    svt_check_better(xd,
                     cm,
                     &row_bias_mv,
                     best_mv,
                     mv_limits,
                     var_params,
                     mv_cost_params,
                     besterr,
                     sse1,
                     distortion,
                     &has_better_mv);
    svt_check_better(xd,
                     cm,
                     &col_bias_mv,
                     best_mv,
                     mv_limits,
                     var_params,
                     mv_cost_params,
                     besterr,
                     sse1,
                     distortion,
                     &has_better_mv);

    // Do an additional search if the second iteration gives a better mv
    if (has_better_mv) {
        svt_check_better(xd,
                         cm,
                         &diag_bias_mv,
                         best_mv,
                         mv_limits,
                         var_params,
                         mv_cost_params,
                         besterr,
                         sse1,
                         distortion,
                         &has_better_mv);
    }
    (void)is_scaled;
}

// Gets the error at the beginning when the mv has fullpel precision
static unsigned int svt_upsampled_setup_center_error(const MV *bestmv, const SUBPEL_SEARCH_VAR_PARAMS *var_params,
                                                     const MV_COST_PARAMS *mv_cost_params, unsigned int *distortion) {
    const MSBuffers *ms_buffers = &var_params->ms_buffers;
    const uint8_t   *ref        = svt_get_buf_from_mv(ms_buffers->ref, *bestmv);
    *distortion                 = var_params->vfp->vf(
        ref, ms_buffers->ref->stride, ms_buffers->src->buf, ms_buffers->src->stride, distortion);
    return *distortion + svt_mv_err_cost_(bestmv, mv_cost_params);
}

static INLINE MV get_best_diag_step(int step_size, unsigned int left_cost, unsigned int right_cost,
                                    unsigned int up_cost, unsigned int down_cost) {
    const MV diag_step = {up_cost <= down_cost ? -step_size : step_size,
                          left_cost <= right_cost ? -step_size : step_size};

    return diag_step;
}

// Searches the four cardinal direction for a better mv, then follows up with a
// search in the best quadrant. This uses bilinear filter to speed up the
// calculation.
static AOM_FORCE_INLINE MV first_level_check_fast(MacroBlockD *xd, const struct AV1Common *const cm, const MV this_mv,
                                                  MV *best_mv, int hstep, const SubpelMvLimits *mv_limits,
                                                  const SUBPEL_SEARCH_VAR_PARAMS *var_params,
                                                  const MV_COST_PARAMS *mv_cost_params, unsigned int *besterr,
                                                  unsigned int orgerr, unsigned int *sse1, int *distortion,
                                                  int is_scaled) {
    // Check the four cardinal directions
    const MV           left_mv = {this_mv.row, this_mv.col - hstep};
    int                dummy   = 0;
    const unsigned int left    = svt_check_better_fast(
        xd, cm, &left_mv, best_mv, mv_limits, var_params, mv_cost_params, besterr, sse1, distortion, &dummy, is_scaled);

    const MV           right_mv = {this_mv.row, this_mv.col + hstep};
    const unsigned int right    = svt_check_better_fast(xd,
                                                     cm,
                                                     &right_mv,
                                                     best_mv,
                                                     mv_limits,
                                                     var_params,
                                                     mv_cost_params,
                                                     besterr,
                                                     sse1,
                                                     distortion,
                                                     &dummy,
                                                     is_scaled);

    const MV           top_mv = {this_mv.row - hstep, this_mv.col};
    const unsigned int up     = svt_check_better_fast(
        xd, cm, &top_mv, best_mv, mv_limits, var_params, mv_cost_params, besterr, sse1, distortion, &dummy, is_scaled);

    const MV           bottom_mv = {this_mv.row + hstep, this_mv.col};
    const unsigned int down      = svt_check_better_fast(xd,
                                                    cm,
                                                    &bottom_mv,
                                                    best_mv,
                                                    mv_limits,
                                                    var_params,
                                                    mv_cost_params,
                                                    besterr,
                                                    sse1,
                                                    distortion,
                                                    &dummy,
                                                    is_scaled);

    const MV diag_step = get_best_diag_step(hstep, left, right, up, down);
    const MV diag_mv   = {this_mv.row + diag_step.row, this_mv.col + diag_step.col};
    if (*besterr >= orgerr)
        return diag_step;
    // Check the diagonal direction with the best mv
    svt_check_better_fast(
        xd, cm, &diag_mv, best_mv, mv_limits, var_params, mv_cost_params, besterr, sse1, distortion, &dummy, is_scaled);

    return diag_step;
}

// Performs a following up search after first_level_check_fast is called. This
// performs two extra chess pattern searches in the best quadrant.
static AOM_FORCE_INLINE void second_level_check_fast(MacroBlockD *xd, const struct AV1Common *const cm,
                                                     const MV this_mv, const MV diag_step, MV *best_mv, int hstep,
                                                     const SubpelMvLimits           *mv_limits,
                                                     const SUBPEL_SEARCH_VAR_PARAMS *var_params,
                                                     const MV_COST_PARAMS *mv_cost_params, unsigned int *besterr,
                                                     unsigned int *sse1, int *distortion, int is_scaled) {
    assert(diag_step.row == hstep || diag_step.row == -hstep);
    assert(diag_step.col == hstep || diag_step.col == -hstep);
    const int tr    = this_mv.row;
    const int tc    = this_mv.col;
    const int br    = best_mv->row;
    const int bc    = best_mv->col;
    int       dummy = 0;
    if (tr != br && tc != bc) {
        assert(diag_step.col == bc - tc);
        assert(diag_step.row == br - tr);
        const MV chess_mv_1 = {br, bc + diag_step.col};
        const MV chess_mv_2 = {br + diag_step.row, bc};
        svt_check_better_fast(xd,
                              cm,
                              &chess_mv_1,
                              best_mv,
                              mv_limits,
                              var_params,
                              mv_cost_params,
                              besterr,
                              sse1,
                              distortion,
                              &dummy,
                              is_scaled);

        svt_check_better_fast(xd,
                              cm,
                              &chess_mv_2,
                              best_mv,
                              mv_limits,
                              var_params,
                              mv_cost_params,
                              besterr,
                              sse1,
                              distortion,
                              &dummy,
                              is_scaled);
    } else if (tr == br && tc != bc) {
        assert(diag_step.col == bc - tc);
        // Continue searching in the best direction
        const MV bottom_long_mv = {br + hstep, bc + diag_step.col};
        const MV top_long_mv    = {br - hstep, bc + diag_step.col};
        svt_check_better_fast(xd,
                              cm,
                              &bottom_long_mv,
                              best_mv,
                              mv_limits,
                              var_params,
                              mv_cost_params,
                              besterr,
                              sse1,
                              distortion,
                              &dummy,
                              is_scaled);
        svt_check_better_fast(xd,
                              cm,
                              &top_long_mv,
                              best_mv,
                              mv_limits,
                              var_params,
                              mv_cost_params,
                              besterr,
                              sse1,
                              distortion,
                              &dummy,
                              is_scaled);

        // Search in the direction opposite of the best quadrant
        const MV rev_mv = {br - diag_step.row, bc};
        svt_check_better_fast(xd,
                              cm,
                              &rev_mv,
                              best_mv,
                              mv_limits,
                              var_params,
                              mv_cost_params,
                              besterr,
                              sse1,
                              distortion,
                              &dummy,
                              is_scaled);
    } else if (tr != br && tc == bc) {
        assert(diag_step.row == br - tr);
        // Continue searching in the best direction
        const MV right_long_mv = {br + diag_step.row, bc + hstep};
        const MV left_long_mv  = {br + diag_step.row, bc - hstep};
        svt_check_better_fast(xd,
                              cm,
                              &right_long_mv,
                              best_mv,
                              mv_limits,
                              var_params,
                              mv_cost_params,
                              besterr,
                              sse1,
                              distortion,
                              &dummy,
                              is_scaled);
        svt_check_better_fast(xd,
                              cm,
                              &left_long_mv,
                              best_mv,
                              mv_limits,
                              var_params,
                              mv_cost_params,
                              besterr,
                              sse1,
                              distortion,
                              &dummy,
                              is_scaled);

        // Search in the direction opposite of the best quadrant
        const MV rev_mv = {br, bc - diag_step.col};
        svt_check_better_fast(xd,
                              cm,
                              &rev_mv,
                              best_mv,
                              mv_limits,
                              var_params,
                              mv_cost_params,
                              besterr,
                              sse1,
                              distortion,
                              &dummy,
                              is_scaled);
    }
}

// Combines first level check and second level check when applicable. This first
// searches the four cardinal directions, and perform several
// diagonal/chess-pattern searches in the best quadrant.
static AOM_FORCE_INLINE void two_level_checks_fast(MacroBlockD *xd, const struct AV1Common *const cm, const MV this_mv,
                                                   MV *best_mv, int hstep, const SubpelMvLimits *mv_limits,
                                                   const SUBPEL_SEARCH_VAR_PARAMS *var_params,
                                                   const MV_COST_PARAMS *mv_cost_params, unsigned int *besterr,
                                                   unsigned int orgerr, unsigned int *sse1, int *distortion, int iters,
                                                   int is_scaled) {
    const MV diag_step = first_level_check_fast(xd,
                                                cm,
                                                this_mv,
                                                best_mv,
                                                hstep,
                                                mv_limits,
                                                var_params,
                                                mv_cost_params,
                                                besterr,
                                                orgerr,
                                                sse1,
                                                distortion,
                                                is_scaled);
    if (*besterr < orgerr)
        if (iters > 1) {
            second_level_check_fast(xd,
                                    cm,
                                    this_mv,
                                    diag_step,
                                    best_mv,
                                    hstep,
                                    mv_limits,
                                    var_params,
                                    mv_cost_params,
                                    besterr,
                                    sse1,
                                    distortion,
                                    is_scaled);
        }
}
extern const uint8_t svt_aom_eb_av1_var_offs[MAX_SB_SIZE];
int svt_av1_find_best_sub_pixel_tree_pruned(void *ictx, MacroBlockD *xd, const struct AV1Common *const cm,
                                            SUBPEL_MOTION_SEARCH_PARAMS *ms_params, MV start_mv, MV *bestmv,
                                            int *distortion, unsigned int *sse1, int qp, BlockSize bsize,
                                            uint8_t early_neigh_check_exit) {
    (void)ictx;
    (void)cm;
    const int                       allow_hp       = ms_params->allow_hp;
    const int                       forced_stop    = ms_params->forced_stop;
    const int                       iters_per_step = ms_params->iters_per_step;
    const SubpelMvLimits           *mv_limits      = &ms_params->mv_limits;
    const MV_COST_PARAMS           *mv_cost_params = &ms_params->mv_cost_params;
    const SUBPEL_SEARCH_VAR_PARAMS *var_params     = &ms_params->var_params;
    int                             hstep          = INIT_SUBPEL_STEP_SIZE; // Step size, initialized to 4/8=1/2 pel
    unsigned int                    besterr;
    unsigned int                    org_error;
    *bestmv = start_mv;

    const int is_scaled = 0;
    besterr = svt_upsampled_setup_center_error(bestmv, var_params, mv_cost_params, (unsigned int *)distortion);

    if (ictx != NULL && ms_params->search_stage == SPEL_ME) {
        ModeDecisionContext *ctx                                 = (ModeDecisionContext *)ictx;
        ctx->fp_me_dist[ms_params->list_idx][ms_params->ref_idx] = besterr;
    }

    if (early_neigh_check_exit)
        return besterr;
    uint64_t th_normalizer = (((var_params->w * var_params->h) >> 3) * ms_params->abs_th_mult * (qp >> 1));
    if (besterr < th_normalizer)
        return besterr;
    // How many steps to take. A round of 0 means fullpel search only, 1 means
    // half-pel, and so on.
    const int round = AOMMIN(FULL_PEL - forced_stop, 3 - !allow_hp);

    // If forced_stop is FULL_PEL, return.
    if (!round)
        return besterr;
    // Exit subpel search if the variance of the full-pel predicted samples is low (i.e. where likely interpolation will not modify the integer samples)
    const MSBuffers   *ms_buffers = &var_params->ms_buffers;
    const uint8_t     *ref        = svt_get_buf_from_mv(ms_buffers->ref, *bestmv);
    unsigned int       sse;
    const unsigned int var       = var_params->vfp->vf(ref, ms_buffers->ref->stride, svt_aom_eb_av1_var_offs, 0, &sse);
    int                block_var = ROUND_POWER_OF_TWO(var, num_pels_log2_lookup[bsize]);

    if (block_var < ms_params->pred_variance_th)
        return besterr;
    if (ms_params->skip_diag_refinement >= 4) {
        org_error = 0;
    } else {
        unsigned int demo = ms_params->skip_diag_refinement >= 2
            ? ((var_params->w >= 64 || var_params->h >= 64) ? 2 : 1)
            : 1;
        org_error         = ms_params->skip_diag_refinement ? besterr / demo : INT_MAX;
    }
    for (int iter = 0; iter < round; ++iter) {
        unsigned int prev_besterr = besterr;
        two_level_checks_fast(xd,
                              cm,
                              start_mv,
                              bestmv,
                              hstep,
                              mv_limits,
                              var_params,
                              mv_cost_params,
                              &besterr,
                              org_error,
                              sse1,
                              distortion,
                              iters_per_step,
                              is_scaled);
        hstep >>= 1;
        start_mv = *bestmv;
        if (ms_params->skip_diag_refinement && iter < QUARTER_PEL)
            org_error = MIN(org_error, besterr);
        int32_t deviation = (((int64_t)MAX(besterr, 1) - (int64_t)MAX(prev_besterr, 1)) * 100) /
            (int64_t)MAX(prev_besterr, 1);
        if (deviation >= ms_params->round_dev_th)
            return besterr;
    }
    return besterr;
}

int svt_av1_find_best_sub_pixel_tree(void *ictx, MacroBlockD *xd, const struct AV1Common *const cm,
                                     SUBPEL_MOTION_SEARCH_PARAMS *ms_params, MV start_mv, MV *bestmv, int *distortion,
                                     unsigned int *sse1, int qp, BlockSize bsize, uint8_t early_neigh_check_exit) {
    ModeDecisionContext *ctx            = (ModeDecisionContext *)ictx;
    const int            allow_hp       = ms_params->allow_hp;
    const int            forced_stop    = ms_params->forced_stop;
    const int            iters_per_step = ms_params->iters_per_step;

    MV_COST_PARAMS                 *mv_cost_params = &ms_params->mv_cost_params;
    const SUBPEL_SEARCH_VAR_PARAMS *var_params     = &ms_params->var_params;
    const SubpelMvLimits           *mv_limits      = &ms_params->mv_limits;

    // How many steps to take. A round of 0 means fullpel search only, 1 means
    // half-pel, and so on.
    int round = AOMMIN(FULL_PEL - forced_stop, 3 - !allow_hp);
    int hstep = INIT_SUBPEL_STEP_SIZE; // Step size, initialized to 4/8=1/2 pel

    unsigned int besterr;

    *bestmv             = start_mv;
    const int is_scaled = 0;
    besterr = svt_upsampled_setup_center_error(bestmv, var_params, mv_cost_params, (unsigned int *)distortion);
    if (ctx != NULL && ms_params->search_stage == SPEL_ME) {
        ctx->fp_me_dist[ms_params->list_idx][ms_params->ref_idx] = besterr;
        if (ctx->pd_pass == PD_PASS_1 && ctx->md_subpel_me_ctrls.mvp_th > 0) {
            unsigned int  best_mvperr  = ctx->best_fp_mvp_dist[ms_params->list_idx][ms_params->ref_idx];
            int           best_mvp_idx = ctx->best_fp_mvp_idx[ms_params->list_idx][ms_params->ref_idx];
            const int     mvp_err      = best_mvperr + 1;
            const int     me_err       = besterr + 1;
            const int32_t deviation    = ((me_err - mvp_err) * 100) / me_err;
            if (deviation >= ctx->md_subpel_me_ctrls.mvp_th)
                round = 1;
            else if (ABS(bestmv->col - ctx->mvp_array[ms_params->list_idx][ms_params->ref_idx][best_mvp_idx].col) >
                         ctx->md_subpel_me_ctrls.hp_mv_th ||
                     ABS(bestmv->row - ctx->mvp_array[ms_params->list_idx][ms_params->ref_idx][best_mvp_idx].row) >
                         ctx->md_subpel_me_ctrls.hp_mv_th) {
                round = MIN(round, 2);
            }
        }
    }
    if (early_neigh_check_exit)
        return besterr;
    // Exit subpel search if the variance of the full-pel predicted samples is low (i.e. where likely interpolation will not modify the integer samples)
    const MSBuffers   *ms_buffers = &var_params->ms_buffers;
    const uint8_t     *ref        = svt_get_buf_from_mv(ms_buffers->ref, *bestmv);
    unsigned int       sse;
    const unsigned int var       = var_params->vfp->vf(ref, ms_buffers->ref->stride, svt_aom_eb_av1_var_offs, 0, &sse);
    int                block_var = ROUND_POWER_OF_TWO(var, num_pels_log2_lookup[bsize]);

    if (block_var < ms_params->pred_variance_th)
        return besterr;
    uint64_t th_normalizer = (((var_params->w * var_params->h) >> 2) * ms_params->abs_th_mult * (qp >> 1));
    if (besterr < th_normalizer)
        return besterr;
    // If forced_stop is FULL_PEL, return.
    if (!round)
        return besterr;

    for (int iter = 0; iter < round; ++iter) {
        MV iter_center_mv = *bestmv;
        MV diag_step;
        diag_step = svt_first_level_check(
            xd, cm, iter_center_mv, bestmv, hstep, mv_limits, var_params, mv_cost_params, &besterr, sse1, distortion);

        // Check diagonal sub-pixel position
        if (!CHECK_MV_EQUAL(iter_center_mv, *bestmv) && iters_per_step > 1) {
            svt_second_level_check_v2(xd,
                                      cm,
                                      iter_center_mv,
                                      diag_step,
                                      bestmv,
                                      mv_limits,
                                      var_params,
                                      mv_cost_params,
                                      &besterr,
                                      sse1,
                                      distortion,
                                      is_scaled);
        }

        hstep >>= 1;
    }

    return besterr;
}
// =============================================================================
//  SVT Functions
// =============================================================================
int svt_aom_fp_mv_err_cost(const MV *mv, const MV_COST_PARAMS *mv_cost_params) {
    return svt_mv_err_cost_(mv, mv_cost_params);
}
