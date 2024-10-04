/*
* Copyright(c) 2019 Intel Corporation
* Copyright (c) 2016, Alliance for Open Media. All rights reserved
*
* This source code is subject to the terms of the BSD 2 Clause License and
* the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
* was not distributed with this source code in the LICENSE file, you can
* obtain it at https://www.aomedia.org/license/software-license. If the Alliance for Open
* Media Patent License 1.0 was not distributed with this source code in the
* PATENTS file, you can obtain it at https://www.aomedia.org/license/patent-license.
*/

#include <stdlib.h>

#include "enc_inter_prediction.h"
//#include "convolve.h"
#include "aom_dsp_rtcd.h"
#include "rd_cost.h"
#include "resize.h"
#include "av1me.h"

void                 svt_aom_get_recon_pic(PictureControlSet *pcs, EbPictureBufferDesc **recon_ptr, Bool is_highbd);
EbPictureBufferDesc *svt_aom_get_ref_pic_buffer(PictureControlSet *pcs, uint8_t is_highbd, uint8_t list_idx,
                                                uint8_t ref_idx) {
    (void)is_highbd;
    return ((EbReferenceObject *)(pcs->ref_pic_ptr_array[list_idx][ref_idx]->object_ptr))->reference_picture;
}
static INLINE MV clamp_mv_to_umv_border_sb(const MacroBlockD *xd, const MV *src_mv, int32_t bw, int32_t bh,
                                           int32_t ss_x, int32_t ss_y) {
    // If the MV points so far into the UMV border that no visible pixels
    // are used for reconstruction, the subpel part of the MV can be
    // discarded and the MV limited to 16 pixels with equivalent results.
    const int32_t spel_left   = (AOM_INTERP_EXTEND + bw) << SUBPEL_BITS;
    const int32_t spel_right  = spel_left - SUBPEL_SHIFTS;
    const int32_t spel_top    = (AOM_INTERP_EXTEND + bh) << SUBPEL_BITS;
    const int32_t spel_bottom = spel_top - SUBPEL_SHIFTS;
    MV            clamped_mv = {(int16_t)(src_mv->row * (1 << (1 - ss_y))), (int16_t)(src_mv->col * (1 << (1 - ss_x)))};
    assert(ss_x <= 1);
    assert(ss_y <= 1);

    clamp_mv(&clamped_mv,
             xd->mb_to_left_edge * (1 << (1 - ss_x)) - spel_left,
             xd->mb_to_right_edge * (1 << (1 - ss_x)) + spel_right,
             xd->mb_to_top_edge * (1 << (1 - ss_y)) - spel_top,
             xd->mb_to_bottom_edge * (1 << (1 - ss_y)) + spel_bottom);

    return clamped_mv;
}

static void av1_make_masked_scaled_inter_predictor(uint8_t *src_ptr, uint8_t *src_ptr_2b, uint32_t src_stride,
                                                   uint8_t *dst_ptr, uint32_t dst_stride, BlockSize bsize,
                                                   uint8_t bwidth, uint8_t bheight, InterpFilter interp_filters,
                                                   const SubpelParams *subpel_params, const ScaleFactors *sf,
                                                   ConvolveParams *conv_params, InterInterCompoundData *comp_data,
                                                   uint8_t *seg_mask, uint8_t bitdepth, uint8_t plane,
                                                   uint8_t use_intrabc, uint8_t is_16bit) {
    //We come here when we have a prediction done using regular path for the ref0 stored in conv_param.dst.
    //use regular path to generate a prediction for ref1 into  a temporary buffer,
    //then  blend that temporary buffer with that from  the first reference.

#define INTER_PRED_BYTES_PER_PIXEL 2
    DECLARE_ALIGNED(32, uint8_t, tmp_buf[INTER_PRED_BYTES_PER_PIXEL * MAX_SB_SQUARE]);
#undef INTER_PRED_BYTES_PER_PIXEL
    //uint8_t *tmp_dst =  tmp_buf;
    const int tmp_buf_stride = MAX_SB_SIZE;

    CONV_BUF_TYPE *org_dst        = conv_params->dst; //save the ref0 prediction pointer
    int            org_dst_stride = conv_params->dst_stride;
    CONV_BUF_TYPE *tmp_buf16      = (CONV_BUF_TYPE *)tmp_buf;
    conv_params->dst              = tmp_buf16;
    conv_params->dst_stride       = tmp_buf_stride;
    assert(conv_params->do_average == 0);

    if (bitdepth > EB_EIGHT_BIT || is_16bit) {
        // for super-res, the reference frame block might be 2x than predictor in maximum
        // for reference scaling, it might be 4x since both width and height is scaled 2x
        // should pack enough buffer for scaled reference
        DECLARE_ALIGNED(16, uint16_t, src16[PACKED_BUFFER_SIZE * 4]);
        uint16_t *src_ptr_10b = src16;
        int32_t   src_stride16;
        if (src_ptr_2b) {
            // pack the reference into temp 16bit buffer
            uint8_t  offset       = INTERPOLATION_OFFSET;
            uint32_t width_scale  = 1;
            uint32_t height_scale = 1;
            if (av1_is_scaled(sf)) {
                width_scale  = sf->x_scale_fp != REF_NO_SCALE ? 2 : 1;
                height_scale = sf->y_scale_fp != REF_NO_SCALE ? 2 : 1;
            }
            // optimize stride from MAX_SB_SIZE to bwidth to minimum the block buffer size
            src_stride16 = bwidth * width_scale + (offset << 1);
            // 16-byte align of src16
            if (src_stride16 % 8)
                src_stride16 = ALIGN_POWER_OF_TWO(src_stride16, 3);

            svt_aom_pack_block(src_ptr - offset - (offset * src_stride),
                               src_stride,
                               src_ptr_2b - offset - (offset * src_stride),
                               src_stride,
                               src16,
                               src_stride16,
                               bwidth * width_scale + (offset << 1),
                               bheight * height_scale + (offset << 1));
            src_ptr_10b = src16 + offset + (offset * src_stride16);
        } else {
            src_ptr_10b  = (uint16_t *)src_ptr;
            src_stride16 = src_stride;
        }
        svt_highbd_inter_predictor(src_ptr_10b,
                                   src_stride16,
                                   (uint16_t *)dst_ptr,
                                   dst_stride,
                                   subpel_params,
                                   sf,
                                   bwidth,
                                   bheight,
                                   conv_params,
                                   interp_filters,
                                   use_intrabc,
                                   bitdepth);
    } else {
        svt_inter_predictor(src_ptr,
                            src_stride,
                            dst_ptr,
                            dst_stride,
                            subpel_params,
                            sf,
                            bwidth,
                            bheight,
                            conv_params,
                            interp_filters,
                            use_intrabc);
    }

    if (!plane && comp_data->type == COMPOUND_DIFFWTD) {
        //CHKN  for DIFF: need to compute the mask  comp_data->seg_mask is the output computed from the two preds org_dst and tmp_buf16
        //for WEDGE the mask is fixed from the table based on wedge_sign/index
        svt_av1_build_compound_diffwtd_mask_d16(seg_mask,
                                                comp_data->mask_type,
                                                org_dst,
                                                org_dst_stride,
                                                tmp_buf16,
                                                tmp_buf_stride,
                                                bheight,
                                                bwidth,
                                                conv_params,
                                                bitdepth);
    }

    svt_aom_build_masked_compound_no_round(dst_ptr,
                                           dst_stride,
                                           org_dst,
                                           org_dst_stride,
                                           tmp_buf16,
                                           tmp_buf_stride,
                                           comp_data,
                                           seg_mask,
                                           bsize,
                                           bheight,
                                           bwidth,
                                           conv_params,
                                           bitdepth,
                                           is_16bit);
}

static const uint8_t bsize_curvfit_model_cat_lookup[BlockSizeS_ALL] = {0, 0, 0, 0, 1, 1, 1, 2, 2, 2, 3,
                                                                       3, 3, 3, 3, 3, 0, 0, 1, 1, 2, 2};
static int           sse_norm_curvfit_model_cat_lookup(double sse_norm) { return (sse_norm > 16.0); }
static const double  interp_rgrid_curv[4][65] = {
    {
        0.000000,    0.000000,    0.000000,    0.000000,    0.000000,    0.000000,    0.000000,    0.000000,
        0.000000,    0.000000,    0.000000,    23.801499,   28.387688,   33.388795,   42.298282,   41.525408,
        51.597692,   49.566271,   54.632979,   60.321507,   67.730678,   75.766165,   85.324032,   96.600012,
        120.839562,  173.917577,  255.974908,  354.107573,  458.063476,  562.345966,  668.568424,  772.072881,
        878.598490,  982.202274,  1082.708946, 1188.037853, 1287.702240, 1395.588773, 1490.825830, 1584.231230,
        1691.386090, 1766.822555, 1869.630904, 1926.743565, 2002.949495, 2047.431137, 2138.486068, 2154.743767,
        2209.242472, 2277.593051, 2290.996432, 2307.452938, 2343.567091, 2397.654644, 2469.425868, 2558.591037,
        2664.860422, 2787.944296, 2927.552932, 3083.396602, 3255.185579, 3442.630134, 3645.440541, 3863.327072,
        4096.000000,
    },
    {
        0.000000,    0.000000,    0.000000,    0.000000,    0.000000,    0.000000,    0.000000,    0.000000,
        0.000000,    0.000000,    0.000000,    8.998436,    9.439592,    9.731837,    10.865931,   11.561347,
        12.578139,   14.205101,   16.770584,   19.094853,   21.330863,   23.298907,   26.901921,   34.501017,
        57.891733,   112.234763,  194.853189,  288.302032,  380.499422,  472.625309,  560.226809,  647.928463,
        734.155122,  817.489721,  906.265783,  999.260562,  1094.489206, 1197.062998, 1293.296825, 1378.926484,
        1472.760990, 1552.663779, 1635.196884, 1692.451951, 1759.741063, 1822.162720, 1916.515921, 1966.686071,
        2031.647506, 2033.700134, 2087.847688, 2161.688858, 2242.536028, 2334.023491, 2436.337802, 2549.665519,
        2674.193198, 2810.107395, 2957.594666, 3116.841567, 3288.034655, 3471.360486, 3667.005616, 3875.156602,
        4096.000000,
    },
    {
        0.000000,    0.000000,    0.000000,    0.000000,    0.000000,    0.000000,    0.000000,    0.000000,
        0.000000,    0.000000,    0.000000,    2.377584,    2.557185,    2.732445,    2.851114,    3.281800,
        3.765589,    4.342578,    5.145582,    5.611038,    6.642238,    7.945977,    11.800522,   17.346624,
        37.501413,   87.216800,   165.860942,  253.865564,  332.039345,  408.518863,  478.120452,  547.268590,
        616.067676,  680.022540,  753.863541,  834.529973,  919.489191,  1008.264989, 1092.230318, 1173.971886,
        1249.514122, 1330.510941, 1399.523249, 1466.923387, 1530.533471, 1586.515722, 1695.197774, 1746.648696,
        1837.136959, 1909.075485, 1975.074651, 2060.159200, 2155.335095, 2259.762505, 2373.710437, 2497.447898,
        2631.243895, 2775.367434, 2930.087523, 3095.673170, 3272.393380, 3460.517161, 3660.313520, 3872.051464,
        4096.000000,
    },
    {
        0.000000,    0.000000,    0.000000,    0.000000,    0.000000,    0.000000,    0.000000,    0.000000,
        0.000000,    0.000000,    0.000000,    0.296997,    0.342545,    0.403097,    0.472889,    0.614483,
        0.842937,    1.050824,    1.326663,    1.717750,    2.530591,    3.582302,    6.995373,    9.973335,
        24.042464,   56.598240,   113.680735,  180.018689,  231.050567,  266.101082,  294.957934,  323.326511,
        349.434429,  380.443211,  408.171987,  441.214916,  475.716772,  512.900000,  551.186939,  592.364455,
        624.527378,  661.940693,  679.185473,  724.800679,  764.781792,  873.050019,  950.299001,  939.292954,
        1052.406153, 1033.893184, 1112.182406, 1219.174326, 1337.296681, 1471.648357, 1622.492809, 1790.093491,
        1974.713858, 2176.617364, 2396.067465, 2633.327614, 2888.661266, 3162.331876, 3454.602899, 3765.737789,
        4096.000000,
    },
};

static const double interp_dgrid_curv[2][65] = {
    {
        16.000000, 15.962891, 15.925174, 15.886888, 15.848074, 15.808770, 15.769015, 15.728850, 15.688313, 15.647445,
        15.606284, 15.564870, 15.525918, 15.483820, 15.373330, 15.126844, 14.637442, 14.184387, 13.560070, 12.880717,
        12.165995, 11.378144, 10.438769, 9.130790,  7.487633,  5.688649,  4.267515,  3.196300,  2.434201,  1.834064,
        1.369920,  1.035921,  0.775279,  0.574895,  0.427232,  0.314123,  0.233236,  0.171440,  0.128188,  0.092762,
        0.067569,  0.049324,  0.036330,  0.027008,  0.019853,  0.015539,  0.011093,  0.008733,  0.007624,  0.008105,
        0.005427,  0.004065,  0.003427,  0.002848,  0.002328,  0.001865,  0.001457,  0.001103,  0.000801,  0.000550,
        0.000348,  0.000193,  0.000085,  0.000021,  0.000000,
    },
    {
        16.000000, 15.996116, 15.984769, 15.966413, 15.941505, 15.910501, 15.873856, 15.832026, 15.785466, 15.734633,
        15.679981, 15.621967, 15.560961, 15.460157, 15.288367, 15.052462, 14.466922, 13.921212, 13.073692, 12.222005,
        11.237799, 9.985848,  8.898823,  7.423519,  5.995325,  4.773152,  3.744032,  2.938217,  2.294526,  1.762412,
        1.327145,  1.020728,  0.765535,  0.570548,  0.425833,  0.313825,  0.232959,  0.171324,  0.128174,  0.092750,
        0.067558,  0.049319,  0.036330,  0.027008,  0.019853,  0.015539,  0.011093,  0.008733,  0.007624,  0.008105,
        0.005427,  0.004065,  0.003427,  0.002848,  0.002328,  0.001865,  0.001457,  0.001103,  0.000801,  0.000550,
        0.000348,  0.000193,  0.000085,  0.000021,  -0.000000,
    },
};

/*
  Precalucation factors to interp_cubic()
    interp_cubic() OUT is: p[1] + 0.5 * x * (p[2] - p[0] +
                      x * (2.0 * p[0] - 5.0 * p[1] + 4.0 * p[2] - p[3] +
                      x * (3.0 * (p[1] - p[2]) + p[3] - p[0])));
  Precalucation:
    interp_cubic() OUT is: D + x * (C + x * (B + x * A))
    For precalculated factors:
    double A = 0.5 *(3.0 * (p[1] - p[2]) + p[3] - p[0]);
    double B = 0.5 *(2.0 * p[0] - 5.0 * p[1] + 4.0 * p[2] - p[3]);
    double C = 0.5 * (p[2] - p[0]);
    double D = p[1];

    Precalculated values of array factors:
    A is: (0 to sizeof(ARRAY[])-1)
    B is: (0 to sizeof(ARRAY[A][])-4)
    PRECALC[A][B][0] = 0.5 *(3.0 * (ARRAY[A][B+1] - ARRAY[A][B+2]) + ARRAY[A][B+3] - ARRAY[A][B])
    PRECALC[A][B][1] = 0.5 *(2.0 * p[0] - 5.0 * ARRAY[A][B+1] + 4.0 * ARRAY[A][B+2]) - ARRAY[A][B+3]);
    PRECALC[A][B][2] = 0.5 * (ARRAY[A][B+2] - ARRAY[A][B]);
    PRECALC[A][B][3] = ARRAY[A][B+1]
*/

static void av1_model_rd_curvfit(BlockSize bsize, double sse_norm, double xqr, double *rate_f, double *distbysse_f) {
    const double x_start = -15.5;
    const double x_end   = 16.5;
    const double x_step  = 0.5;
    const double epsilon = 1e-6;
    const int    rcat    = bsize_curvfit_model_cat_lookup[bsize];
    const int    dcat    = sse_norm_curvfit_model_cat_lookup(sse_norm);

    xqr             = AOMMAX(xqr, x_start + x_step + epsilon);
    xqr             = AOMMIN(xqr, x_end - x_step - epsilon);
    const double x  = (xqr - x_start) / x_step;
    const int    xi = (int)floor(x);
    assert(xi > 0);

    const double *prate = &interp_rgrid_curv[rcat][(xi - 1)];
    *rate_f             = prate[1];
    const double *pdist = &interp_dgrid_curv[dcat][(xi - 1)];
    *distbysse_f        = pdist[1];
}

// Fits a curve for rate and distortion using as feature:
// log2(sse_norm/qstep^2)
static void model_rd_with_curvfit(PictureControlSet *pcs, BlockSize plane_bsize, int64_t sse, int num_samples,
                                  int *rate, int64_t *dist, ModeDecisionContext *ctx, uint32_t rdmult) {
    (void)plane_bsize;
    const int           dequant_shift   = 3;
    int32_t             current_q_index = pcs->ppcs->frm_hdr.quantization_params.base_q_idx;
    SequenceControlSet *scs             = pcs->scs;
    Dequants *const     dequants        = ctx->hbd_md ? &scs->enc_ctx->deq_bd : &scs->enc_ctx->deq_8bit;
    int16_t             quantizer       = dequants->y_dequant_qtx[current_q_index][1];

    const int qstep = AOMMAX(quantizer >> dequant_shift, 1);

    if (sse == 0) {
        if (rate)
            *rate = 0;
        if (dist)
            *dist = 0;
        return;
    }
#ifdef ARCH_X86_64
    aom_clear_system_state();
#endif
    const double sse_norm = (double)sse / num_samples;
    const double xqr      = (double)svt_log2f((uint32_t)sse_norm / (qstep * qstep));

    double rate_f, dist_by_sse_norm_f;
    av1_model_rd_curvfit(plane_bsize, sse_norm, xqr, &rate_f, &dist_by_sse_norm_f);

    const double dist_f = dist_by_sse_norm_f * sse_norm;
    int          rate_i = (int)((rate_f * num_samples) + 0.5);
    int64_t      dist_i = (int64_t)((dist_f * num_samples) + 0.5);
#ifdef ARCH_X86_64
    aom_clear_system_state();
#endif

    // Check if skip is better
    if (rate_i == 0) {
        dist_i = sse << 4;
    } else if (RDCOST(rdmult, rate_i, dist_i) >= RDCOST(rdmult, 0, sse << 4)) {
        rate_i = 0;
        dist_i = sse << 4;
    }

    if (rate)
        *rate = rate_i;
    if (dist)
        *dist = dist_i;
}

/**
 * Compute the element-wise difference of the squares of 2 arrays.
 *
 * d: Difference of the squares of the inputs: a**2 - b**2
 * a: First input array
 * b: Second input array
 * N: Number of elements
 *
 * 'd', 'a', and 'b' are contiguous.
 *
 * The result is saturated to signed 16 bits.
 */
void svt_av1_wedge_compute_delta_squares_c(int16_t *d, const int16_t *a, const int16_t *b, int N) {
    int i;

    for (i = 0; i < N; i++) d[i] = clamp(a[i] * a[i] - b[i] * b[i], INT16_MIN, INT16_MAX);
}

/**
 * Choose the mask sign for a compound predictor.
 *
 * ds:    Difference of the squares of the residuals.
 *        r0**2 - r1**2
 * m:     The blending mask
 * N:     Number of pixels
 * limit: Pre-computed threshold value.
 *        MAX_MASK_VALUE/2 * (sum(r0**2) - sum(r1**2))
 *
 * 'ds' and 'm' are contiguous.
 *
 * Returns true if the negated mask has lower SSE compared to the positive
 * mask. Computation is based on:
 *  Sum((mask*r0 + (MAX_MASK_VALUE-mask)*r1)**2)
 *                                     >
 *                                Sum(((MAX_MASK_VALUE-mask)*r0 + mask*r1)**2)
 *
 *  which can be simplified to:
 *
 *  Sum(mask*(r0**2 - r1**2)) > MAX_MASK_VALUE/2 * (sum(r0**2) - sum(r1**2))
 *
 *  The right hand side does not depend on the mask, and needs to be passed as
 *  the 'limit' parameter.
 *
 *  After pre-computing (r0**2 - r1**2), which is passed in as 'ds', the left
 *  hand side is simply a scalar product between an int16_t and uint8_t vector.
 *
 *  Note that for efficiency, ds is stored on 16 bits. Real input residuals
 *  being small, this should not cause a noticeable issue.
 */
int8_t svt_av1_wedge_sign_from_residuals_c(const int16_t *ds, const uint8_t *m, int N, int64_t limit) {
    int64_t acc = 0;

    do { acc += *ds++ * *m++; } while (--N);

    return acc > limit;
}

static void pick_wedge(PictureControlSet *pcs, ModeDecisionContext *ctx, const BlockSize bsize, const uint8_t *const p0,
                       const int16_t *const residual1, const int16_t *const diff10, int8_t *const best_wedge_sign,
                       int8_t *const best_wedge_index) {
    uint8_t              hbd_md      = ctx->hbd_md == EB_DUAL_BIT_MD ? EB_8_BIT_MD : ctx->hbd_md;
    uint32_t             full_lambda = hbd_md ? ctx->full_lambda_md[EB_10_BIT_MD] : ctx->full_lambda_md[EB_8_BIT_MD];
    EbPictureBufferDesc *src_pic     = hbd_md ? pcs->input_frame16bit : pcs->ppcs->enhanced_pic;
    const int            bw          = block_size_wide[bsize];
    const int            bh          = block_size_high[bsize];
    const int            N           = bw * bh;
    assert(N >= 64);
    int       rate;
    int64_t   dist;
    int64_t   best_rd     = INT64_MAX;
    int8_t    wedge_types = (1 << svt_aom_get_wedge_bits_lookup(bsize));
    const int bd_round    = 0;
    DECLARE_ALIGNED(32, int16_t, residual0[MAX_SB_SQUARE]); // src - pred0
    if (hbd_md) {
        uint16_t *src_buf_hbd = (uint16_t *)src_pic->buffer_y + (ctx->blk_org_x + src_pic->org_x) +
            (ctx->blk_org_y + src_pic->org_y) * src_pic->stride_y;
        svt_aom_highbd_subtract_block(bh,
                                      bw,
                                      residual0,
                                      bw,
                                      (uint8_t *)src_buf_hbd /*src->buf*/,
                                      src_pic->stride_y /*src->stride*/,
                                      (uint8_t *)p0,
                                      bw,
                                      EB_TEN_BIT);
    } else {
        uint8_t *src_buf = src_pic->buffer_y + (ctx->blk_org_x + src_pic->org_x) +
            (ctx->blk_org_y + src_pic->org_y) * src_pic->stride_y;
        svt_aom_subtract_block(bh, bw, residual0, bw, src_buf /*src->buf*/, src_pic->stride_y /*src->stride*/, p0, bw);
    }
    int64_t sign_limit = ((int64_t)svt_aom_sum_squares_i16(residual0, N) -
                          (int64_t)svt_aom_sum_squares_i16(residual1, N)) *
        (1 << WEDGE_WEIGHT_BITS) / 2;
    int16_t *ds = residual0;

    svt_av1_wedge_compute_delta_squares(ds, residual0, residual1, N);

    for (int8_t wedge_index = 0; wedge_index < wedge_types; ++wedge_index) {
        const uint8_t *mask = svt_aom_get_contiguous_soft_mask(wedge_index, 0, bsize);

        int8_t wedge_sign = svt_av1_wedge_sign_from_residuals(ds, mask, N, sign_limit);

        mask         = svt_aom_get_contiguous_soft_mask(wedge_index, wedge_sign, bsize);
        uint64_t sse = svt_av1_wedge_sse_from_residuals(residual1, diff10, mask, N);
        sse          = ROUND_POWER_OF_TWO(sse, bd_round);

        int64_t rd = sse;
        if (ctx->inter_comp_ctrls.use_rate) {
            model_rd_with_curvfit(pcs, bsize, sse, N, &rate, &dist, ctx, full_lambda);

            rd = RDCOST(full_lambda, rate, dist);
        } else {
            rd = sse;
        }
        if (rd < best_rd) {
            *best_wedge_index = wedge_index;
            *best_wedge_sign  = wedge_sign;
            best_rd           = rd;
        }
    }
}

// Choose the best wedge index the specified sign
int64_t pick_wedge_fixed_sign(PictureControlSet *pcs, ModeDecisionContext *ctx, const BlockSize bsize,
                              const int16_t *const residual1, const int16_t *const diff10, const int8_t wedge_sign,
                              int8_t *const best_wedge_index) {
    //const MACROBLOCKD *const xd = &x->e_mbd;

    uint32_t  full_lambda = ctx->hbd_md ? ctx->full_lambda_md[EB_10_BIT_MD] : ctx->full_lambda_md[EB_8_BIT_MD];
    const int bw          = block_size_wide[bsize];
    const int bh          = block_size_high[bsize];
    const int N           = bw * bh;
    assert(N >= 64);
    int     rate;
    int64_t dist;
    int64_t best_rd     = INT64_MAX;
    int8_t  wedge_types = (1 << svt_aom_get_wedge_bits_lookup(bsize));
    //const int hbd = 0;// is_cur_buf_hbd(xd);
    const int bd_round = 0;
    for (int8_t wedge_index = 0; wedge_index < wedge_types; ++wedge_index) {
        const uint8_t *mask = svt_aom_get_contiguous_soft_mask(wedge_index, wedge_sign, bsize);
        uint64_t       sse  = svt_av1_wedge_sse_from_residuals(residual1, diff10, mask, N);
        sse                 = ROUND_POWER_OF_TWO(sse, bd_round);
        int64_t rd          = sse;
        if (ctx->inter_intra_comp_ctrls.use_rd_model) {
            model_rd_with_curvfit(pcs, bsize, sse, N, &rate, &dist, ctx, full_lambda);
            rate += ctx->md_rate_est_ctx->wedge_idx_fac_bits[bsize][wedge_index];
            rd = RDCOST(full_lambda, rate, dist);
        }
        if (rd < best_rd) {
            *best_wedge_index = wedge_index;
            best_rd           = rd;
        }
    }
    return best_rd; //- RDCOST(x->rdmult, x->wedge_idx_cost[bsize][*best_wedge_index], 0);
}

static void pick_interinter_wedge(PictureControlSet *pcs, ModeDecisionContext *ctx,
                                  InterInterCompoundData *interinter_comp, const BlockSize bsize,
                                  const uint8_t *const p0, const int16_t *const residual1,
                                  const int16_t *const diff10) {
    int8_t wedge_index = -1;
    int8_t wedge_sign  = 0;

    assert(is_interinter_compound_used(COMPOUND_WEDGE, bsize));
    //TODO: OMK+CHKN to check on FIX_RATE_E_WEDGE

    pick_wedge(pcs, ctx, bsize, p0, residual1, diff10, &wedge_sign, &wedge_index);
    interinter_comp->wedge_sign  = wedge_sign;
    interinter_comp->wedge_index = wedge_index;
}

static void pick_interinter_seg(PictureControlSet *pcs, ModeDecisionContext *ctx,
                                InterInterCompoundData *interinter_comp, const BlockSize bsize, const uint8_t *const p0,
                                const uint8_t *const p1, const int16_t *const residual1, const int16_t *const diff10) {
    uint8_t           hbd_md      = ctx->hbd_md == EB_DUAL_BIT_MD ? EB_8_BIT_MD : ctx->hbd_md;
    uint32_t          full_lambda = hbd_md ? ctx->full_lambda_md[EB_10_BIT_MD] : ctx->full_lambda_md[EB_8_BIT_MD];
    const int         bw          = block_size_wide[bsize];
    const int         bh          = block_size_high[bsize];
    const int         N           = 1 << num_pels_log2_lookup[bsize];
    int               rate;
    int64_t           dist;
    DIFFWTD_MASK_TYPE cur_mask_type;
    int64_t           best_rd        = INT64_MAX;
    DIFFWTD_MASK_TYPE best_mask_type = 0;
    // cppcheck-suppress unassignedVariable
    DECLARE_ALIGNED(16, uint8_t, seg_mask0[2 * MAX_SB_SQUARE]);
    // cppcheck-suppress unassignedVariable
    DECLARE_ALIGNED(16, uint8_t, seg_mask1[2 * MAX_SB_SQUARE]);

    const int bd_round = 0;
    // try each mask type and its inverse
    for (cur_mask_type = 0; cur_mask_type < DIFFWTD_MASK_TYPES; cur_mask_type++) {
        uint8_t *const temp_mask = cur_mask_type ? seg_mask1 : seg_mask0;
        // build mask and inverse
        if (hbd_md)
            svt_av1_build_compound_diffwtd_mask_highbd(temp_mask, cur_mask_type, p0, bw, p1, bw, bh, bw, EB_TEN_BIT);
        else
            svt_av1_build_compound_diffwtd_mask(temp_mask, cur_mask_type, p0, bw, p1, bw, bh, bw);

        // compute rd for mask
        const uint64_t sse = svt_av1_wedge_sse_from_residuals(residual1, diff10, temp_mask, N);
        int64_t        rd0;

        if (ctx->inter_comp_ctrls.use_rate) {
            model_rd_with_curvfit(pcs, bsize, ROUND_POWER_OF_TWO(sse, bd_round), N, &rate, &dist, ctx, full_lambda);

            rd0 = RDCOST(full_lambda, rate, dist);
        } else {
            rd0 = sse;
        }
        if (rd0 < best_rd) {
            best_mask_type = cur_mask_type;
            best_rd        = rd0;
        }
    }

    interinter_comp->mask_type = best_mask_type;
}

static void pick_interinter_mask(PictureControlSet *pcs, ModeDecisionContext *ctx,
                                 InterInterCompoundData *interinter_comp, const BlockSize bsize,
                                 const uint8_t *const p0, const uint8_t *const p1, const int16_t *const residual1,
                                 const int16_t *const diff10) {
    if (interinter_comp->type == COMPOUND_WEDGE)
        pick_interinter_wedge(pcs, ctx, interinter_comp, bsize, p0, residual1, diff10);
    else if (interinter_comp->type == COMPOUND_DIFFWTD)
        pick_interinter_seg(pcs, ctx, interinter_comp, bsize, p0, p1, residual1, diff10);
    else
        assert(0);
}

//
int64_t svt_aom_highbd_sse_c(const uint8_t *a8, int a_stride, const uint8_t *b8, int b_stride, int width, int height) {
    int       y, x;
    int64_t   sse = 0;
    uint16_t *a   = (uint16_t *)a8; //CONVERT_TO_SHORTPTR(a8);
    uint16_t *b   = (uint16_t *)b8; //CONVERT_TO_SHORTPTR(b8);
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            const int32_t diff = (int32_t)(a[x]) - (int32_t)(b[x]);
            sse += diff * diff;
        }

        a += a_stride;
        b += b_stride;
    }
    return sse;
}

int64_t svt_aom_sse_c(const uint8_t *a, int a_stride, const uint8_t *b, int b_stride, int width, int height) {
    int     y, x;
    int64_t sse = 0;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            const int32_t diff = abs(a[x] - b[x]);
            sse += diff * diff;
        }

        a += a_stride;
        b += b_stride;
    }
    return sse;
}
void model_rd_for_sb_with_curvfit(PictureControlSet *pcs, ModeDecisionContext *ctx, BlockSize bsize, int bw, int bh,
                                  uint8_t *src_buf, uint32_t src_stride, uint8_t *pred_buf, uint32_t pred_stride,
                                  int plane_from, int plane_to, int mi_row, int mi_col, int *out_rate_sum,
                                  int64_t *out_dist_sum, int *plane_rate, int64_t *plane_sse, int64_t *plane_dist) {
    (void)mi_row;
    (void)mi_col;
    // Note our transform coeffs are 8 times an orthogonal transform.
    // Hence quantizer step is also 8 times. To get effective quantizer
    // we need to divide by 8 before sending to modeling function.
    const int bd_round = 0;

    uint32_t full_lambda = ctx->hbd_md ? ctx->full_lambda_md[EB_10_BIT_MD] : ctx->full_lambda_md[EB_8_BIT_MD];

    int64_t rate_sum = 0;
    int64_t dist_sum = 0;

    for (int plane = plane_from; plane <= plane_to; ++plane) {
        int32_t         subsampling = plane == 0 ? 0 : 1;
        const BlockSize plane_bsize = get_plane_block_size(bsize, subsampling, subsampling);
        int64_t         dist, sse;
        int             rate;
        if (ctx->hbd_md) // CCODE
            sse = svt_aom_highbd_sse(src_buf, src_stride, pred_buf, pred_stride, bw, bh);
        else
            sse = svt_aom_sse(src_buf, src_stride, pred_buf, pred_stride, bw, bh);

        sse = ROUND_POWER_OF_TWO(sse, bd_round);
        model_rd_with_curvfit(pcs, plane_bsize, sse, bw * bh, &rate, &dist, ctx, full_lambda);

        rate_sum += rate;
        dist_sum += dist;

        if (plane_rate)
            plane_rate[plane] = rate;
        if (plane_sse)
            plane_sse[plane] = sse;
        if (plane_dist)
            plane_dist[plane] = dist;
    }

    *out_rate_sum = (int)rate_sum;
    *out_dist_sum = dist_sum;
}

extern void svt_av1_predict_intra_block(STAGE stage, const BlockGeom *blk_geom, MacroBlockD *xd, int32_t wpx,
                                        int32_t hpx, TxSize tx_size, PredictionMode mode, int32_t angle_delta,
                                        int32_t use_palette, PaletteInfo *palette_info,
                                        FilterIntraMode filter_intra_mode, uint8_t *topNeighArray,
                                        uint8_t *leftNeighArray, EbPictureBufferDesc *recon_buffer, int32_t col_off,
                                        int32_t row_off, int32_t plane, BlockSize bsize, uint32_t tu_org_x_pict,
                                        uint32_t tu_org_y_pict, uint32_t bl_org_x_pict, uint32_t bl_org_y_pict,
                                        uint32_t bl_org_x_mb, uint32_t bl_org_y_mb, SeqHeader *seq_header_ptr);
extern void svt_av1_predict_intra_block_16bit(EbBitDepth bit_depth, STAGE stage, const BlockGeom *blk_geom,
                                              MacroBlockD *xd, int32_t wpx, int32_t hpx, TxSize tx_size,
                                              PredictionMode mode, int32_t angle_delta, int32_t use_palette,
                                              PaletteInfo *palette_info, FilterIntraMode filter_intra_mode,
                                              uint16_t *topNeighArray, uint16_t *leftNeighArray,
                                              EbPictureBufferDesc *recon_buffer, int32_t col_off, int32_t row_off,
                                              int32_t plane, BlockSize bsize, uint32_t tu_org_x_pict,
                                              uint32_t tu_org_y_pict, uint32_t bl_org_x_pict, uint32_t bl_org_y_pict,
                                              uint32_t bl_org_x_mb, uint32_t bl_org_y_mb, SeqHeader *seq_header_ptr);

struct build_prediction_ctxt {
    const AV1_COMMON *cm;
    int               mi_row;
    int               mi_col;
    uint8_t         **tmp_buf;
    int               tmp_width;
    int               tmp_height;
    int              *tmp_stride;
    int               mb_to_far_edge;
    int               ss_x;
    int               ss_y;

    PictureControlSet   *pcs;
    MvUnit               mv_unit;
    uint16_t             pu_origin_x;
    uint16_t             pu_origin_y;
    EbPictureBufferDesc *ref_pic_list0;
    EbPictureBufferDesc  prediction_ptr;
    uint16_t             dst_origin_x;
    uint16_t             dst_origin_y;
    Bool                 perform_chroma;
};
// input: log2 of length, 0(4), 1(8), ...
static const int max_neighbor_obmc[6] = {0, 1, 2, 3, 4, 4};

typedef void (*overlappable_nb_visitor_t)(uint8_t is16bit, MacroBlockD *xd, int rel_mi_pos, uint8_t nb_mi_size,
                                          MbModeInfo *nb_mi, void *fun_ctxt, const int num_planes);

static INLINE void foreach_overlappable_nb_above(uint8_t is16bit, const AV1_COMMON *cm, MacroBlockD *xd, int mi_col,
                                                 int nb_max, overlappable_nb_visitor_t fun, void *fun_ctxt) {
    const int num_planes = 2;
    if (!xd->up_available)
        return;

    int nb_count = 0;

    // prev_row_mi points into the mi array, starting at the beginning of the
    // previous row.
    ModeInfo **prev_row_mi = xd->mi - mi_col - 1 * xd->mi_stride;
    const int  end_col     = AOMMIN(mi_col + xd->n4_w, cm->mi_cols);
    uint8_t    mi_step;
    for (int above_mi_col = mi_col; above_mi_col < end_col && nb_count < nb_max; above_mi_col += mi_step) {
        ModeInfo /*MbModeInfo*/ **above_mi = prev_row_mi + above_mi_col;
        mi_step = AOMMIN(mi_size_wide[above_mi[0]->mbmi.block_mi.bsize], mi_size_wide[BLOCK_64X64]);
        // If we're considering a block with width 4, it should be treated as
        // half of a pair of blocks with chroma information in the second. Move
        // above_mi_col back to the start of the pair if needed, set above_mbmi
        // to point at the block with chroma information, and set mi_step to 2 to
        // step over the entire pair at the end of the iteration.
        if (mi_step == 1) {
            above_mi_col &= ~1;
            above_mi = prev_row_mi + above_mi_col + 1;
            mi_step  = 2;
        }
        if (is_neighbor_overlappable(&(*above_mi)->mbmi)) {
            ++nb_count;

            fun(is16bit,
                xd,
                above_mi_col - mi_col,
                AOMMIN(xd->n4_w, mi_step),
                &(*above_mi)->mbmi,
                fun_ctxt,
                num_planes);
        }
    }
}

static INLINE void foreach_overlappable_nb_left(uint8_t is16bit, const AV1_COMMON *cm, MacroBlockD *xd, int mi_row,
                                                int nb_max, overlappable_nb_visitor_t fun, void *fun_ctxt) {
    const int num_planes = 2;
    if (!xd->left_available)
        return;

    int nb_count = 0;

    // prev_col_mi points into the mi array, starting at the top of the
    // previous column

    ModeInfo **prev_col_mi = xd->mi - 1 - mi_row * xd->mi_stride;
    const int  end_row     = AOMMIN(mi_row + xd->n4_h, cm->mi_rows);
    uint8_t    mi_step;
    for (int left_mi_row = mi_row; left_mi_row < end_row && nb_count < nb_max; left_mi_row += mi_step) {
        ModeInfo **left_mi = prev_col_mi + left_mi_row * xd->mi_stride;
        mi_step            = AOMMIN(mi_size_high[left_mi[0]->mbmi.block_mi.bsize], mi_size_high[BLOCK_64X64]);
        if (mi_step == 1) {
            left_mi_row &= ~1;
            left_mi = prev_col_mi + (left_mi_row + 1) * xd->mi_stride;
            mi_step = 2;
        }
        if (is_neighbor_overlappable(&(*left_mi)->mbmi)) {
            ++nb_count;

            fun(is16bit, xd, left_mi_row - mi_row, AOMMIN(xd->n4_h, mi_step), &(*left_mi)->mbmi, fun_ctxt, num_planes);
        }
    }
}

static void av1_setup_build_prediction_by_above_pred(MacroBlockD *xd, int rel_mi_col, uint8_t above_mi_width,
                                                     MbModeInfo *above_mbmi, struct build_prediction_ctxt *ctxt,
                                                     const int num_planes, uint8_t is16bit) {
    (void)num_planes;
    const int above_mi_col = ctxt->mi_col + rel_mi_col;

    //use above mbmi  to set up the reference object from where to read

    ctxt->mv_unit.mv[0].x        = above_mbmi->block_mi.mv[0].as_mv.col;
    ctxt->mv_unit.mv[0].y        = above_mbmi->block_mi.mv[0].as_mv.row;
    ctxt->mv_unit.pred_direction = UNI_PRED_LIST_0;

    uint8_t ref_idx_l0   = get_ref_frame_idx(above_mbmi->block_mi.ref_frame[0]);
    uint8_t list_idx0    = get_list_idx(above_mbmi->block_mi.ref_frame[0]);
    ctxt->ref_pic_list0  = svt_aom_get_ref_pic_buffer(ctxt->pcs, is16bit, list_idx0, ref_idx_l0);
    xd->mb_to_left_edge  = 8 * MI_SIZE * (-above_mi_col);
    xd->mb_to_right_edge = ctxt->mb_to_far_edge + (xd->n4_w - rel_mi_col - above_mi_width) * MI_SIZE * 8;
}
static void av1_setup_build_prediction_by_left_pred(MacroBlockD *xd, int rel_mi_row, uint8_t left_mi_height,
                                                    MbModeInfo *left_mbmi, struct build_prediction_ctxt *ctxt,
                                                    const int num_planes, uint8_t is16bit) {
    (void)num_planes;
    const int left_mi_row = ctxt->mi_row + rel_mi_row;

    ctxt->mv_unit.mv[0].x        = left_mbmi->block_mi.mv[0].as_mv.col;
    ctxt->mv_unit.mv[0].y        = left_mbmi->block_mi.mv[0].as_mv.row;
    ctxt->mv_unit.pred_direction = UNI_PRED_LIST_0;

    uint8_t ref_idx_l0 = get_ref_frame_idx(left_mbmi->block_mi.ref_frame[0]);
    uint8_t list_idx0  = get_list_idx(left_mbmi->block_mi.ref_frame[0]);

    ctxt->ref_pic_list0   = svt_aom_get_ref_pic_buffer(ctxt->pcs, is16bit, list_idx0, ref_idx_l0);
    xd->mb_to_top_edge    = 8 * MI_SIZE * (-left_mi_row);
    xd->mb_to_bottom_edge = ctxt->mb_to_far_edge + (xd->n4_h - rel_mi_row - left_mi_height) * MI_SIZE * 8;
}
static EbErrorType get_single_prediction_for_obmc_luma_hbd(SequenceControlSet *scs, uint32_t interp_filters,
                                                           MacroBlockD *xd, MvUnit *mv_unit, uint16_t pu_origin_x,
                                                           uint16_t pu_origin_y, uint8_t bwidth, uint8_t bheight,
                                                           EbPictureBufferDesc *ref_pic_list0,
                                                           EbPictureBufferDesc *prediction_ptr, uint16_t dst_origin_x,
                                                           uint16_t dst_origin_y, uint8_t bit_depth) {
    EbErrorType return_error = EB_ErrorNone;
    uint8_t     is_compound  = 0;
    DECLARE_ALIGNED(32, uint16_t, tmp_dstY[128 * 128]); //move this to context if stack does not hold.

    MV             mv;
    uint8_t       *src_ptr_8b;
    uint8_t       *src_ptr_2b;
    uint16_t      *dst_ptr;
    int32_t        src_stride;
    int32_t        dst_stride;
    ConvolveParams conv_params;

    mv.col = mv_unit->mv[REF_LIST_0].x;
    mv.row = mv_unit->mv[REF_LIST_0].y;

    //List0-Y
    assert(ref_pic_list0 != NULL);
    src_stride  = ref_pic_list0->stride_y;
    dst_stride  = prediction_ptr->stride_y;
    conv_params = get_conv_params_no_round(0, 0, 0, tmp_dstY, 128, is_compound, bit_depth);

    ScaleFactors sf;
    svt_av1_setup_scale_factors_for_frame(
        &sf, ref_pic_list0->width, ref_pic_list0->height, prediction_ptr->width, prediction_ptr->height);
    src_ptr_8b = ref_pic_list0->buffer_y + ref_pic_list0->org_x + ref_pic_list0->org_y * ref_pic_list0->stride_y;
    src_ptr_2b = ref_pic_list0->buffer_bit_inc_y + ref_pic_list0->org_x +
        ref_pic_list0->org_y * ref_pic_list0->stride_bit_inc_y;
    dst_ptr = (uint16_t *)prediction_ptr->buffer_y + prediction_ptr->org_x + dst_origin_x +
        (prediction_ptr->org_y + dst_origin_y) * prediction_ptr->stride_y;

    svt_aom_enc_make_inter_predictor(scs,
                                     src_ptr_8b,
                                     src_ptr_2b,
                                     (uint8_t *)dst_ptr,
                                     (int16_t)pu_origin_y,
                                     (int16_t)pu_origin_x,
                                     mv,
                                     &sf,
                                     &conv_params,
                                     interp_filters,
                                     NULL,
                                     NULL,
                                     ref_pic_list0->width,
                                     ref_pic_list0->height,
                                     bwidth,
                                     bheight,
                                     xd->bsize,
                                     xd,
                                     src_stride,
                                     dst_stride,
                                     0, // plane
                                     0, // ss_y
                                     0, // ss_x
                                     bit_depth, // bit_depth
                                     0, // use_intrabc
                                     0, // is_masked_compound
                                     TRUE); // is16bit
    return return_error;
}
static EbErrorType get_single_prediction_for_obmc_chroma_hbd(SequenceControlSet *scs, uint32_t interp_filters,
                                                             MacroBlockD *xd, MvUnit *mv_unit, uint16_t pu_origin_x,
                                                             uint16_t pu_origin_y, uint8_t bwidth, uint8_t bheight,
                                                             EbPictureBufferDesc *ref_pic_list0,
                                                             EbPictureBufferDesc *prediction_ptr, uint16_t dst_origin_x,
                                                             uint16_t dst_origin_y, int32_t ss_x, int32_t ss_y,
                                                             uint8_t bit_depth) {
    EbErrorType return_error = EB_ErrorNone;
    uint8_t     is_compound  = 0;

    DECLARE_ALIGNED(32, uint16_t, tmp_dstCb[64 * 64]);
    DECLARE_ALIGNED(32, uint16_t, tmp_dstCr[64 * 64]);

    MV             mv;
    uint8_t       *src_ptr_8b;
    uint8_t       *src_ptr_2b;
    uint16_t      *dst_ptr;
    int32_t        src_stride;
    int32_t        dst_stride;
    ConvolveParams conv_params;

    mv.col = mv_unit->mv[REF_LIST_0].x;
    mv.row = mv_unit->mv[REF_LIST_0].y;
    assert(ref_pic_list0 != NULL);
    //List0-Cb
    src_stride  = ref_pic_list0->stride_cb;
    dst_stride  = prediction_ptr->stride_cb;
    conv_params = get_conv_params_no_round(0, 0, 0, tmp_dstCb, 64, is_compound, bit_depth);

    ScaleFactors sf;
    svt_av1_setup_scale_factors_for_frame(
        &sf, ref_pic_list0->width, ref_pic_list0->height, prediction_ptr->width, prediction_ptr->height);
    int pu_origin_y_chroma = ((pu_origin_y >> 3) << 3) >> ss_y;
    int pu_origin_x_chroma = ((pu_origin_x >> 3) << 3) >> ss_x;

    src_ptr_8b = ref_pic_list0->buffer_cb + (ref_pic_list0->org_x >> ss_x) +
        (ref_pic_list0->org_y >> ss_y) * ref_pic_list0->stride_cb;
    src_ptr_2b = ref_pic_list0->buffer_bit_inc_cb + (ref_pic_list0->org_x >> ss_x) +
        (ref_pic_list0->org_y >> ss_y) * ref_pic_list0->stride_bit_inc_cb;
    dst_ptr = (uint16_t *)prediction_ptr->buffer_cb + ((prediction_ptr->org_x + ((dst_origin_x >> 3) << 3)) >> ss_x) +
        ((prediction_ptr->org_y + ((dst_origin_y >> 3) << 3)) >> ss_y) * prediction_ptr->stride_cb;

    svt_aom_enc_make_inter_predictor(scs,
                                     src_ptr_8b,
                                     src_ptr_2b,
                                     (uint8_t *)dst_ptr,
                                     (int16_t)pu_origin_y_chroma,
                                     (int16_t)pu_origin_x_chroma,
                                     mv,
                                     &sf,
                                     &conv_params,
                                     interp_filters,
                                     NULL,
                                     NULL,
                                     ref_pic_list0->width,
                                     ref_pic_list0->height,
                                     bwidth,
                                     bheight,
                                     xd->bsize,
                                     xd,
                                     src_stride,
                                     dst_stride,
                                     1, // plane
                                     ss_y,
                                     ss_x,
                                     bit_depth, // bit_depth
                                     0, // use_intrabc
                                     0, // is_masked_compound
                                     TRUE); // is16bit

    //List0-Cr
    src_stride  = ref_pic_list0->stride_cr;
    dst_stride  = prediction_ptr->stride_cr;
    conv_params = get_conv_params_no_round(0, 0, 0, tmp_dstCr, 64, is_compound, bit_depth);

    src_ptr_8b = ref_pic_list0->buffer_cr + (ref_pic_list0->org_x >> ss_x) +
        (ref_pic_list0->org_y >> ss_y) * ref_pic_list0->stride_cr;
    src_ptr_2b = ref_pic_list0->buffer_bit_inc_cr + (ref_pic_list0->org_x >> ss_x) +
        (ref_pic_list0->org_y >> ss_y) * ref_pic_list0->stride_cr;
    dst_ptr = (uint16_t *)prediction_ptr->buffer_cr + ((prediction_ptr->org_x + ((dst_origin_x >> 3) << 3)) >> ss_x) +
        ((prediction_ptr->org_y + ((dst_origin_y >> 3) << 3)) >> ss_y) * prediction_ptr->stride_cr;
    svt_aom_enc_make_inter_predictor(scs,
                                     src_ptr_8b,
                                     src_ptr_2b,
                                     (uint8_t *)dst_ptr,
                                     (int16_t)pu_origin_y_chroma,
                                     (int16_t)pu_origin_x_chroma,
                                     mv,
                                     &sf,
                                     &conv_params,
                                     interp_filters,
                                     NULL,
                                     NULL,
                                     ref_pic_list0->width,
                                     ref_pic_list0->height,
                                     bwidth,
                                     bheight,
                                     xd->bsize,
                                     xd,
                                     src_stride,
                                     dst_stride,
                                     2, // plane
                                     ss_y,
                                     ss_x,
                                     bit_depth, // bit_depth
                                     0, // use_intrabc
                                     0, // is_masked_compound
                                     TRUE); // is16bit
    return return_error;
}

static EbErrorType get_single_prediction_for_obmc_luma(SequenceControlSet *scs, uint32_t interp_filters,
                                                       MacroBlockD *xd, MvUnit *mv_unit, uint16_t pu_origin_x,
                                                       uint16_t pu_origin_y, uint8_t bwidth, uint8_t bheight,
                                                       EbPictureBufferDesc *ref_pic_list0,
                                                       EbPictureBufferDesc *prediction_ptr, uint16_t dst_origin_x,
                                                       uint16_t dst_origin_y) {
    EbErrorType return_error = EB_ErrorNone;
    const int   is_compound  = 0;
    DECLARE_ALIGNED(32, uint16_t, tmp_dstY[128 * 128]); //move this to context if stack does not hold.

    MV                 mv;
    uint8_t           *src_ptr;
    uint8_t           *dst_ptr;
    int32_t            src_stride;
    int32_t            dst_stride;
    ConvolveParams     conv_params;
    InterpFilterParams filter_params_x, filter_params_y;

    mv.col = mv_unit->mv[REF_LIST_0].x;
    mv.row = mv_unit->mv[REF_LIST_0].y;

    //List0-Y
    assert(ref_pic_list0 != NULL);
    src_stride = ref_pic_list0->stride_y;
    dst_stride = prediction_ptr->stride_y;

    conv_params = get_conv_params_no_round(0, 0, 0, tmp_dstY, 128, is_compound, EB_EIGHT_BIT);
    av1_get_convolve_filter_params(interp_filters, &filter_params_x, &filter_params_y, bwidth, bheight);

    ScaleFactors sf;
    svt_av1_setup_scale_factors_for_frame(
        &sf, ref_pic_list0->width, ref_pic_list0->height, prediction_ptr->width, prediction_ptr->height);
    src_ptr = ref_pic_list0->buffer_y + ((ref_pic_list0->org_x + (ref_pic_list0->org_y) * ref_pic_list0->stride_y));
    dst_ptr = prediction_ptr->buffer_y +
        ((prediction_ptr->org_x + dst_origin_x + (prediction_ptr->org_y + dst_origin_y) * prediction_ptr->stride_y));

    svt_aom_enc_make_inter_predictor(scs,
                                     src_ptr,
                                     NULL,
                                     dst_ptr,
                                     (int16_t)pu_origin_y,
                                     (int16_t)pu_origin_x,
                                     mv,
                                     &sf,
                                     &conv_params,
                                     interp_filters,
                                     NULL,
                                     NULL,
                                     ref_pic_list0->width,
                                     ref_pic_list0->height,
                                     bwidth,
                                     bheight,
                                     xd->bsize,
                                     xd,
                                     src_stride,
                                     dst_stride,
                                     0, // plane
                                     0, // ss_y
                                     0, // ss_x
                                     EB_EIGHT_BIT, // bit_depth
                                     0, // use_intrabc
                                     0, // is_masked_compound
                                     0); // is16bit
    return return_error;
}

static EbErrorType get_single_prediction_for_obmc_chroma(SequenceControlSet *scs, uint32_t interp_filters,
                                                         MacroBlockD *xd, MvUnit *mv_unit, uint16_t pu_origin_x,
                                                         uint16_t pu_origin_y, uint8_t bwidth, uint8_t bheight,
                                                         EbPictureBufferDesc *ref_pic_list0,
                                                         EbPictureBufferDesc *prediction_ptr, uint16_t dst_origin_x,
                                                         uint16_t dst_origin_y, int32_t ss_x, int32_t ss_y) {
    EbErrorType return_error = EB_ErrorNone;
    uint8_t     is_compound  = 0;

    DECLARE_ALIGNED(32, uint16_t, tmp_dstCb[64 * 64]);
    DECLARE_ALIGNED(32, uint16_t, tmp_dstCr[64 * 64]);

    MV                 mv;
    uint8_t           *src_ptr;
    uint8_t           *dst_ptr;
    int32_t            src_stride;
    int32_t            dst_stride;
    ConvolveParams     conv_params;
    InterpFilterParams filter_params_x, filter_params_y;
    //List0-Y

    mv.col = mv_unit->mv[REF_LIST_0].x;
    mv.row = mv_unit->mv[REF_LIST_0].y;
    assert(ref_pic_list0 != NULL);
    //List0-Cb
    src_stride = ref_pic_list0->stride_cb;
    dst_stride = prediction_ptr->stride_cb;

    conv_params = get_conv_params_no_round(0, 0, 0, tmp_dstCb, 64, is_compound, EB_EIGHT_BIT);

    av1_get_convolve_filter_params(interp_filters, &filter_params_x, &filter_params_y, bwidth, bheight);

    ScaleFactors sf;
    svt_av1_setup_scale_factors_for_frame(
        &sf, ref_pic_list0->width, ref_pic_list0->height, prediction_ptr->width, prediction_ptr->height);
    int pu_origin_y_chroma = ((pu_origin_y >> 3) << 3) >> ss_y;
    int pu_origin_x_chroma = ((pu_origin_x >> 3) << 3) >> ss_x;

    src_ptr = ref_pic_list0->buffer_cb +
        ((((ref_pic_list0->org_x) >> ss_x) + ((ref_pic_list0->org_y) >> ss_y) * ref_pic_list0->stride_cb));
    dst_ptr = prediction_ptr->buffer_cb +
        ((((prediction_ptr->org_x + ((dst_origin_x >> 3) << 3)) >> ss_x) +
          ((prediction_ptr->org_y + ((dst_origin_y >> 3) << 3)) >> ss_y) * prediction_ptr->stride_cb));

    svt_aom_enc_make_inter_predictor(scs,
                                     src_ptr,
                                     NULL,
                                     dst_ptr,
                                     (int16_t)pu_origin_y_chroma,
                                     (int16_t)pu_origin_x_chroma,
                                     mv,
                                     &sf,
                                     &conv_params,
                                     interp_filters,
                                     NULL,
                                     NULL,
                                     ref_pic_list0->width,
                                     ref_pic_list0->height,
                                     bwidth,
                                     bheight,
                                     xd->bsize,
                                     xd,
                                     src_stride,
                                     dst_stride,
                                     1, // plane
                                     ss_y,
                                     ss_x,
                                     EB_EIGHT_BIT, // bit_depth
                                     0, // use_intrabc
                                     0, // is_masked_compound
                                     0); // is16bit

    //List0-Cr
    src_stride = ref_pic_list0->stride_cr;
    dst_stride = prediction_ptr->stride_cr;

    conv_params = get_conv_params_no_round(0, 0, 0, tmp_dstCr, 64, is_compound, EB_EIGHT_BIT);
    av1_get_convolve_filter_params(interp_filters, &filter_params_x, &filter_params_y, bwidth, bheight);

    src_ptr = ref_pic_list0->buffer_cr +
        ((((ref_pic_list0->org_x) >> ss_x) + ((ref_pic_list0->org_y) >> ss_y) * ref_pic_list0->stride_cr));
    dst_ptr = prediction_ptr->buffer_cr +
        ((((prediction_ptr->org_x + ((dst_origin_x >> 3) << 3)) >> ss_x) +
          ((prediction_ptr->org_y + ((dst_origin_y >> 3) << 3)) >> ss_y) * prediction_ptr->stride_cr));
    svt_aom_enc_make_inter_predictor(scs,
                                     src_ptr,
                                     NULL,
                                     dst_ptr,
                                     (int16_t)pu_origin_y_chroma,
                                     (int16_t)pu_origin_x_chroma,
                                     mv,
                                     &sf,
                                     &conv_params,
                                     interp_filters,
                                     NULL,
                                     NULL,
                                     ref_pic_list0->width,
                                     ref_pic_list0->height,
                                     bwidth,
                                     bheight,
                                     xd->bsize,
                                     xd,
                                     src_stride,
                                     dst_stride,
                                     2, // plane
                                     ss_y,
                                     ss_x,
                                     EB_EIGHT_BIT, // bit_depth
                                     0, // use_intrabc
                                     0, // is_masked_compound
                                     0); // is16bit
    return return_error;
}
static INLINE void build_prediction_by_above_pred(uint8_t is16bit, MacroBlockD *xd, int rel_mi_col,
                                                  uint8_t above_mi_width, MbModeInfo *above_mbmi, void *fun_ctxt,
                                                  const int num_planes) {
    struct build_prediction_ctxt *ctxt         = (struct build_prediction_ctxt *)fun_ctxt;
    const int                     above_mi_col = ctxt->mi_col + rel_mi_col;
    int                           mi_x, mi_y;
    MbModeInfo                    backup_mbmi = *above_mbmi;
    SequenceControlSet           *scs         = ctxt->pcs->scs;
    av1_setup_build_prediction_by_above_pred(xd, rel_mi_col, above_mi_width, &backup_mbmi, ctxt, num_planes, is16bit);

    ctxt->prediction_ptr.org_x = ctxt->prediction_ptr.org_y = 0;
    ctxt->prediction_ptr.buffer_y                           = ctxt->tmp_buf[0];
    ctxt->prediction_ptr.buffer_cb                          = ctxt->tmp_buf[1];
    ctxt->prediction_ptr.buffer_cr                          = ctxt->tmp_buf[2];
    ctxt->prediction_ptr.stride_y                           = ctxt->tmp_stride[0];
    ctxt->prediction_ptr.stride_cb                          = ctxt->tmp_stride[1];
    ctxt->prediction_ptr.stride_cr                          = ctxt->tmp_stride[2];
    ctxt->prediction_ptr.width                              = (uint16_t)ctxt->tmp_width;
    ctxt->prediction_ptr.height                             = (uint16_t)ctxt->tmp_height;

    ctxt->dst_origin_x = rel_mi_col << MI_SIZE_LOG2;
    ctxt->dst_origin_y = 0;

    mi_x = above_mi_col << MI_SIZE_LOG2;
    mi_y = ctxt->mi_row << MI_SIZE_LOG2;

    const BlockSize bsize = xd->bsize;

    for (int j = 0; j < num_planes; ++j) {
        int subsampling_x = j > 0 ? ctxt->ss_x : 0;
        int subsampling_y = j > 0 ? ctxt->ss_y : 0;

        int bw = (above_mi_width * MI_SIZE) >> subsampling_x;
        int bh = clamp(
            block_size_high[bsize] >> (subsampling_y + 1), 4, block_size_high[BLOCK_64X64] >> (subsampling_y + 1));

        if (svt_av1_skip_u4x4_pred_in_obmc(bsize, 0, subsampling_x, subsampling_y))
            continue;

        if (j == 0)
            if (is16bit)
                get_single_prediction_for_obmc_luma_hbd(scs,
                                                        above_mbmi->block_mi.interp_filters,
                                                        xd,
                                                        &ctxt->mv_unit,
                                                        mi_x,
                                                        mi_y,
                                                        bw,
                                                        bh,
                                                        ctxt->ref_pic_list0,
                                                        &ctxt->prediction_ptr,
                                                        ctxt->dst_origin_x,
                                                        ctxt->dst_origin_y,
                                                        ctxt->ref_pic_list0->bit_depth);
            else
                get_single_prediction_for_obmc_luma(scs,
                                                    above_mbmi->block_mi.interp_filters,
                                                    xd,
                                                    &ctxt->mv_unit,
                                                    mi_x,
                                                    mi_y,
                                                    bw,
                                                    bh,
                                                    ctxt->ref_pic_list0,
                                                    &ctxt->prediction_ptr,
                                                    ctxt->dst_origin_x,
                                                    ctxt->dst_origin_y);
        else if (is16bit)
            get_single_prediction_for_obmc_chroma_hbd(scs,
                                                      above_mbmi->block_mi.interp_filters,
                                                      xd,
                                                      &ctxt->mv_unit,
                                                      mi_x,
                                                      mi_y,
                                                      bw,
                                                      bh,
                                                      ctxt->ref_pic_list0,
                                                      &ctxt->prediction_ptr,
                                                      ctxt->dst_origin_x,
                                                      ctxt->dst_origin_y,
                                                      ctxt->ss_x,
                                                      ctxt->ss_y,
                                                      ctxt->ref_pic_list0->bit_depth);
        else
            get_single_prediction_for_obmc_chroma(scs,
                                                  above_mbmi->block_mi.interp_filters,
                                                  xd,
                                                  &ctxt->mv_unit,
                                                  mi_x,
                                                  mi_y,
                                                  bw,
                                                  bh,
                                                  ctxt->ref_pic_list0,
                                                  &ctxt->prediction_ptr,
                                                  ctxt->dst_origin_x,
                                                  ctxt->dst_origin_y,
                                                  ctxt->ss_x,
                                                  ctxt->ss_y);
    }
}
static INLINE void build_prediction_by_left_pred(uint8_t is16bit, MacroBlockD *xd, int rel_mi_row,
                                                 uint8_t left_mi_height, MbModeInfo *left_mbmi, void *fun_ctxt,
                                                 const int num_planes) {
    struct build_prediction_ctxt *ctxt        = (struct build_prediction_ctxt *)fun_ctxt;
    const int                     left_mi_row = ctxt->mi_row + rel_mi_row;
    int                           mi_x, mi_y;
    MbModeInfo                    backup_mbmi = *left_mbmi;
    SequenceControlSet           *scs         = ctxt->pcs->scs;
    av1_setup_build_prediction_by_left_pred(xd, rel_mi_row, left_mi_height, &backup_mbmi, ctxt, num_planes, is16bit);

    mi_x = ctxt->mi_col << MI_SIZE_LOG2;
    mi_y = left_mi_row << MI_SIZE_LOG2;

    ctxt->prediction_ptr.org_x = ctxt->prediction_ptr.org_y = 0;
    ctxt->prediction_ptr.buffer_y                           = ctxt->tmp_buf[0];
    ctxt->prediction_ptr.buffer_cb                          = ctxt->tmp_buf[1];
    ctxt->prediction_ptr.buffer_cr                          = ctxt->tmp_buf[2];
    ctxt->prediction_ptr.stride_y                           = ctxt->tmp_stride[0];
    ctxt->prediction_ptr.stride_cb                          = ctxt->tmp_stride[1];
    ctxt->prediction_ptr.stride_cr                          = ctxt->tmp_stride[2];
    ctxt->prediction_ptr.width                              = (uint16_t)ctxt->tmp_width;
    ctxt->prediction_ptr.height                             = (uint16_t)ctxt->tmp_height;

    ctxt->dst_origin_x = 0;
    ctxt->dst_origin_y = rel_mi_row << MI_SIZE_LOG2;

    const BlockSize bsize = xd->bsize;

    for (int j = 0; j < num_planes; ++j) {
        int subsampling_x = j > 0 ? ctxt->ss_x : 0;
        int subsampling_y = j > 0 ? ctxt->ss_y : 0;

        int bw = clamp(
            block_size_wide[bsize] >> (subsampling_x + 1), 4, block_size_wide[BLOCK_64X64] >> (subsampling_x + 1));
        int bh = (left_mi_height << MI_SIZE_LOG2) >> subsampling_y;

        if (svt_av1_skip_u4x4_pred_in_obmc(bsize, 1, subsampling_x, subsampling_y))
            continue;

        if (j == 0)
            if (is16bit)
                get_single_prediction_for_obmc_luma_hbd(scs,
                                                        left_mbmi->block_mi.interp_filters,
                                                        xd,
                                                        &ctxt->mv_unit,
                                                        mi_x,
                                                        mi_y,
                                                        bw,
                                                        bh,
                                                        ctxt->ref_pic_list0,
                                                        &ctxt->prediction_ptr,
                                                        ctxt->dst_origin_x,
                                                        ctxt->dst_origin_y,
                                                        ctxt->ref_pic_list0->bit_depth);
            else
                get_single_prediction_for_obmc_luma(scs,
                                                    left_mbmi->block_mi.interp_filters,
                                                    xd,
                                                    &ctxt->mv_unit,
                                                    mi_x,
                                                    mi_y,
                                                    bw,
                                                    bh,
                                                    ctxt->ref_pic_list0,
                                                    &ctxt->prediction_ptr,
                                                    ctxt->dst_origin_x,
                                                    ctxt->dst_origin_y);
        else if (is16bit)
            get_single_prediction_for_obmc_chroma_hbd(scs,
                                                      left_mbmi->block_mi.interp_filters,
                                                      xd,
                                                      &ctxt->mv_unit,
                                                      mi_x,
                                                      mi_y,
                                                      bw,
                                                      bh,
                                                      ctxt->ref_pic_list0,
                                                      &ctxt->prediction_ptr,
                                                      ctxt->dst_origin_x,
                                                      ctxt->dst_origin_y,
                                                      ctxt->ss_x,
                                                      ctxt->ss_y,
                                                      ctxt->ref_pic_list0->bit_depth);
        else
            get_single_prediction_for_obmc_chroma(scs,
                                                  left_mbmi->block_mi.interp_filters,
                                                  xd,
                                                  &ctxt->mv_unit,
                                                  mi_x,
                                                  mi_y,
                                                  bw,
                                                  bh,
                                                  ctxt->ref_pic_list0,
                                                  &ctxt->prediction_ptr,
                                                  ctxt->dst_origin_x,
                                                  ctxt->dst_origin_y,
                                                  ctxt->ss_x,
                                                  ctxt->ss_y);
    }
}
static void build_prediction_by_above_preds(Bool perform_chroma, BlockSize bsize, PictureControlSet *pcs,
                                            MacroBlockD *xd, int mi_row, int mi_col, uint8_t *tmp_buf[MAX_MB_PLANE],
                                            int tmp_stride[MAX_MB_PLANE], uint8_t is16bit) {
    if (!xd->up_available)
        return;

    // Adjust mb_to_bottom_edge to have the correct value for the OBMC
    // prediction block. This is half the height of the original block,
    // except for 128-wide blocks, where we only use a height of 32.
    int this_height = xd->n4_h * MI_SIZE;
    int pred_height = AOMMIN(this_height / 2, 32);
    xd->mb_to_bottom_edge += (this_height - pred_height) * 8;

    struct build_prediction_ctxt ctxt;

    ctxt.cm             = pcs->ppcs->av1_cm;
    ctxt.mi_row         = mi_row;
    ctxt.mi_col         = mi_col;
    ctxt.tmp_buf        = tmp_buf;
    ctxt.tmp_width      = pcs->ppcs->enhanced_pic->width;
    ctxt.tmp_height     = pcs->ppcs->enhanced_pic->height;
    ctxt.tmp_stride     = tmp_stride;
    ctxt.mb_to_far_edge = xd->mb_to_right_edge;
    ctxt.ss_x           = pcs->ppcs->av1_cm->subsampling_x;
    ctxt.ss_y           = pcs->ppcs->av1_cm->subsampling_y;

    ctxt.pcs            = pcs;
    ctxt.perform_chroma = perform_chroma;
    xd->bsize           = bsize;

    foreach_overlappable_nb_above(is16bit,
                                  pcs->ppcs->av1_cm,
                                  xd,
                                  mi_col,
                                  max_neighbor_obmc[mi_size_wide_log2[bsize]],
                                  build_prediction_by_above_pred,
                                  &ctxt);

    xd->mb_to_left_edge  = -((mi_col * MI_SIZE) * 8);
    xd->mb_to_right_edge = ctxt.mb_to_far_edge;
    xd->mb_to_bottom_edge -= (this_height - pred_height) * 8;
}
static void build_prediction_by_left_preds(Bool perform_chroma, BlockSize bsize, PictureControlSet *pcs,
                                           MacroBlockD *xd, int mi_row, int mi_col, uint8_t *tmp_buf[MAX_MB_PLANE],
                                           int tmp_stride[MAX_MB_PLANE], uint8_t is16bit) {
    if (!xd->left_available)
        return;

    // Adjust mb_to_right_edge to have the correct value for the OBMC
    // prediction block. This is half the width of the original block,
    // except for 128-wide blocks, where we only use a width of 32.
    int this_width = xd->n4_w * MI_SIZE;
    int pred_width = AOMMIN(this_width / 2, 32);
    xd->mb_to_right_edge += (this_width - pred_width) * 8;

    struct build_prediction_ctxt ctxt;

    ctxt.cm             = pcs->ppcs->av1_cm;
    ctxt.mi_row         = mi_row;
    ctxt.mi_col         = mi_col;
    ctxt.tmp_buf        = tmp_buf;
    ctxt.tmp_width      = pcs->ppcs->enhanced_pic->width;
    ctxt.tmp_height     = pcs->ppcs->enhanced_pic->height;
    ctxt.tmp_stride     = tmp_stride;
    ctxt.mb_to_far_edge = xd->mb_to_bottom_edge;
    ctxt.ss_x           = pcs->ppcs->av1_cm->subsampling_x;
    ctxt.ss_y           = pcs->ppcs->av1_cm->subsampling_y;

    ctxt.pcs            = pcs;
    ctxt.perform_chroma = perform_chroma;

    xd->bsize = bsize;

    foreach_overlappable_nb_left(is16bit,
                                 pcs->ppcs->av1_cm,
                                 xd,
                                 mi_row,
                                 max_neighbor_obmc[mi_size_high_log2[bsize]],
                                 build_prediction_by_left_pred,
                                 &ctxt);

    xd->mb_to_top_edge = -((mi_row * MI_SIZE) * 8);
    xd->mb_to_right_edge -= (this_width - pred_width) * 8;
    xd->mb_to_bottom_edge = ctxt.mb_to_far_edge;
}
struct obmc_inter_pred_ctxt {
    uint8_t **adjacent;
    int      *adjacent_stride;
    uint8_t  *final_dst_ptr_y;
    uint16_t  final_dst_stride_y;
    uint8_t  *final_dst_ptr_u;
    uint16_t  final_dst_stride_u;
    uint8_t  *final_dst_ptr_v;
    uint16_t  final_dst_stride_v;
    uint32_t  component_mask;
};

static INLINE void build_obmc_inter_pred_above(uint8_t is16bit, MacroBlockD *xd, int rel_mi_col, uint8_t above_mi_width,
                                               MbModeInfo *above_mi, void *fun_ctxt, const int num_planes) {
    (void)above_mi;
    (void)num_planes;
    struct obmc_inter_pred_ctxt *ctxt  = (struct obmc_inter_pred_ctxt *)fun_ctxt;
    const BlockSize              bsize = xd->bsize;

    const int overlap     = AOMMIN(block_size_high[bsize], block_size_high[BLOCK_64X64]) >> 1;
    int       start_plane = (ctxt->component_mask & PICTURE_BUFFER_DESC_LUMA_MASK) ? 0 : 1;
    int       end_plane   = (ctxt->component_mask & PICTURE_BUFFER_DESC_CHROMA_MASK) ? 3 : 1;
    for (int plane = start_plane; plane < end_plane; ++plane) {
        int subsampling_x = plane > 0 ? 1 : 0;
        int subsampling_y = plane > 0 ? 1 : 0;

        const int bw            = (above_mi_width * MI_SIZE) >> subsampling_x;
        const int bh            = overlap >> subsampling_y;
        const int plane_col     = (rel_mi_col * MI_SIZE) >> subsampling_x;
        const int plane_col_pos = plane_col << is16bit;

        if (svt_av1_skip_u4x4_pred_in_obmc(bsize, 0, subsampling_x, subsampling_y))
            continue;

        const int      dst_stride = plane == 0 ? ctxt->final_dst_stride_y
                 : plane == 1                  ? ctxt->final_dst_stride_u
                                               : ctxt->final_dst_stride_v;
        uint8_t *const dst        = plane == 0 ? &ctxt->final_dst_ptr_y[plane_col_pos]
                   : plane == 1                ? &ctxt->final_dst_ptr_u[plane_col_pos]
                                               : &ctxt->final_dst_ptr_v[plane_col_pos];

        const int            tmp_stride = ctxt->adjacent_stride[plane];
        const uint8_t *const tmp        = &ctxt->adjacent[plane][plane_col_pos];
        const uint8_t *const mask       = svt_av1_get_obmc_mask(bh);

        if (is16bit)
            svt_aom_highbd_blend_a64_vmask_16bit((uint16_t *)dst,
                                                 dst_stride,
                                                 (uint16_t *)dst,
                                                 dst_stride,
                                                 (uint16_t *)tmp,
                                                 tmp_stride,
                                                 mask,
                                                 bw,
                                                 bh,
                                                 10);
        else
            svt_aom_blend_a64_vmask(dst, dst_stride, dst, dst_stride, tmp, tmp_stride, mask, bw, bh);
    }
}
static INLINE void build_obmc_inter_pred_left(uint8_t is16bit, MacroBlockD *xd, int rel_mi_row, uint8_t left_mi_height,
                                              MbModeInfo *left_mi, void *fun_ctxt, const int num_planes) {
    (void)left_mi;
    (void)num_planes;
    struct obmc_inter_pred_ctxt *ctxt        = (struct obmc_inter_pred_ctxt *)fun_ctxt;
    const BlockSize              bsize       = xd->bsize;
    const int                    overlap     = AOMMIN(block_size_wide[bsize], block_size_wide[BLOCK_64X64]) >> 1;
    int                          start_plane = (ctxt->component_mask & PICTURE_BUFFER_DESC_LUMA_MASK) ? 0 : 1;
    int                          end_plane   = (ctxt->component_mask & PICTURE_BUFFER_DESC_CHROMA_MASK) ? 3 : 1;
    for (int plane = start_plane; plane < end_plane; ++plane) {
        int subsampling_x = plane > 0 ? 1 : 0;
        int subsampling_y = plane > 0 ? 1 : 0;

        const int bw            = overlap >> subsampling_x;
        const int bh            = (left_mi_height * MI_SIZE) >> subsampling_y;
        const int plane_row     = (rel_mi_row * MI_SIZE) >> subsampling_y;
        const int plane_row_pos = plane_row << is16bit;

        if (svt_av1_skip_u4x4_pred_in_obmc(bsize, 1, subsampling_x, subsampling_y))
            continue;

        const int      dst_stride = plane == 0 ? ctxt->final_dst_stride_y
                 : plane == 1                  ? ctxt->final_dst_stride_u
                                               : ctxt->final_dst_stride_v;
        uint8_t *const dst        = plane == 0 ? &ctxt->final_dst_ptr_y[plane_row_pos * dst_stride]
                   : plane == 1                ? &ctxt->final_dst_ptr_u[plane_row_pos * dst_stride]
                                               : &ctxt->final_dst_ptr_v[plane_row_pos * dst_stride];

        const int            tmp_stride = ctxt->adjacent_stride[plane];
        const uint8_t *const tmp        = &ctxt->adjacent[plane][plane_row_pos * tmp_stride];
        const uint8_t *const mask       = svt_av1_get_obmc_mask(bw);

        if (is16bit)
            svt_aom_highbd_blend_a64_hmask_16bit((uint16_t *)dst,
                                                 dst_stride,
                                                 (uint16_t *)dst,
                                                 dst_stride,
                                                 (uint16_t *)tmp,
                                                 tmp_stride,
                                                 mask,
                                                 bw,
                                                 bh,
                                                 10);
        else
            svt_aom_blend_a64_hmask(dst, dst_stride, dst, dst_stride, tmp, tmp_stride, mask, bw, bh);
    }
}
// This function combines motion compensated predictions that are generated by
// top/left neighboring blocks' inter predictors with the regular inter
// prediction. We assume the original prediction (bmc) is stored in
// xd->plane[].dst.buf
static void av1_build_obmc_inter_prediction(uint8_t *final_dst_ptr_y, uint16_t final_dst_stride_y,
                                            uint8_t *final_dst_ptr_u, uint16_t final_dst_stride_u,
                                            uint8_t *final_dst_ptr_v, uint16_t final_dst_stride_v,
                                            uint32_t component_mask, BlockSize bsize, PictureControlSet *pcs,
                                            MacroBlockD *xd, int mi_row, int mi_col, uint8_t *above[MAX_MB_PLANE],
                                            int above_stride[MAX_MB_PLANE], uint8_t *left[MAX_MB_PLANE],
                                            int left_stride[MAX_MB_PLANE], uint8_t is16bit) {
    // handle above row
    struct obmc_inter_pred_ctxt ctxt_above;

    ctxt_above.adjacent        = above;
    ctxt_above.adjacent_stride = above_stride;

    ctxt_above.final_dst_ptr_y    = final_dst_ptr_y;
    ctxt_above.final_dst_stride_y = final_dst_stride_y;
    ctxt_above.final_dst_ptr_u    = final_dst_ptr_u;
    ctxt_above.final_dst_stride_u = final_dst_stride_u;
    ctxt_above.final_dst_ptr_v    = final_dst_ptr_v;
    ctxt_above.final_dst_stride_v = final_dst_stride_v;
    ctxt_above.component_mask     = component_mask;

    foreach_overlappable_nb_above(is16bit,
                                  pcs->ppcs->av1_cm,
                                  xd,
                                  mi_col,
                                  max_neighbor_obmc[mi_size_wide_log2[bsize]],
                                  build_obmc_inter_pred_above,
                                  &ctxt_above);

    // handle left column
    struct obmc_inter_pred_ctxt ctxt_left;

    ctxt_left.adjacent        = left;
    ctxt_left.adjacent_stride = left_stride;

    ctxt_left.final_dst_ptr_y    = final_dst_ptr_y;
    ctxt_left.final_dst_stride_y = final_dst_stride_y;
    ctxt_left.final_dst_ptr_u    = final_dst_ptr_u;
    ctxt_left.final_dst_stride_u = final_dst_stride_u;
    ctxt_left.final_dst_ptr_v    = final_dst_ptr_v;
    ctxt_left.final_dst_stride_v = final_dst_stride_v;
    ctxt_left.component_mask     = component_mask;

    foreach_overlappable_nb_left(is16bit,
                                 pcs->ppcs->av1_cm,
                                 xd,
                                 mi_row,
                                 max_neighbor_obmc[mi_size_high_log2[bsize]],
                                 build_obmc_inter_pred_left,
                                 &ctxt_left);
}

void svt_av1_calc_target_weighted_pred_above_c(uint8_t is16bit, MacroBlockD *xd, int rel_mi_col, uint8_t nb_mi_width,
                                               MbModeInfo *nb_mi, void *fun_ctxt, const int num_planes) {
    (void)nb_mi;
    (void)num_planes;
    (void)is16bit;
    struct calc_target_weighted_pred_ctxt *ctxt = (struct calc_target_weighted_pred_ctxt *)fun_ctxt;

    const int            bw     = xd->n4_w << MI_SIZE_LOG2;
    const uint8_t *const mask1d = svt_av1_get_obmc_mask(ctxt->overlap);
    assert(mask1d != NULL);
    int32_t       *wsrc = ctxt->wsrc_buf + (rel_mi_col * MI_SIZE);
    int32_t       *mask = ctxt->mask_buf + (rel_mi_col * MI_SIZE);
    const uint8_t *tmp  = ctxt->tmp + rel_mi_col * MI_SIZE;

    {
        for (int row = 0; row < ctxt->overlap; ++row) {
            const uint8_t m0 = mask1d[row];
            const uint8_t m1 = AOM_BLEND_A64_MAX_ALPHA - m0;
            for (int col = 0; col < nb_mi_width * MI_SIZE; ++col) {
                wsrc[col] = m1 * tmp[col];
                mask[col] = m0;
            }
            wsrc += bw;
            mask += bw;
            tmp += ctxt->tmp_stride;
        }
    }
}

void svt_av1_calc_target_weighted_pred_left_c(uint8_t is16bit, MacroBlockD *xd, int rel_mi_row, uint8_t nb_mi_height,
                                              MbModeInfo *nb_mi, void *fun_ctxt, const int num_planes) {
    (void)nb_mi;
    (void)num_planes;
    (void)is16bit;

    struct calc_target_weighted_pred_ctxt *ctxt = (struct calc_target_weighted_pred_ctxt *)fun_ctxt;

    const int            bw     = xd->n4_w << MI_SIZE_LOG2;
    const uint8_t *const mask1d = svt_av1_get_obmc_mask(ctxt->overlap);

    int32_t       *wsrc = ctxt->wsrc_buf + (rel_mi_row * MI_SIZE * bw);
    int32_t       *mask = ctxt->mask_buf + (rel_mi_row * MI_SIZE * bw);
    const uint8_t *tmp  = ctxt->tmp + (rel_mi_row * MI_SIZE * ctxt->tmp_stride);
    assert(mask1d != NULL);
    {
        for (int row = 0; row < nb_mi_height * MI_SIZE; ++row) {
            for (int col = 0; col < ctxt->overlap; ++col) {
                const uint8_t m0 = mask1d[col];
                const uint8_t m1 = AOM_BLEND_A64_MAX_ALPHA - m0;
                wsrc[col] = (wsrc[col] >> AOM_BLEND_A64_ROUND_BITS) * m0 + (tmp[col] << AOM_BLEND_A64_ROUND_BITS) * m1;
                mask[col] = (mask[col] >> AOM_BLEND_A64_ROUND_BITS) * m0;
            }
            wsrc += bw;
            mask += bw;
            tmp += ctxt->tmp_stride;
        }
    }
}

static void av1_make_masked_warp_inter_predictor(uint8_t *src_ptr, uint8_t *src_2b_ptr, uint32_t src_stride,
                                                 uint16_t buf_width, uint16_t buf_height, uint8_t *dst_ptr,
                                                 uint32_t dst_stride, const BlockGeom *blk_geom, uint8_t bwidth,
                                                 uint8_t bheight, ConvolveParams *conv_params,
                                                 InterInterCompoundData *comp_data, uint8_t bitdepth, uint8_t plane,
                                                 uint16_t pu_origin_x, uint16_t pu_origin_y,
                                                 EbWarpedMotionParams *wm_params_l1, Bool is16bit) {
    //We come here when we have a prediction done using regular path for the ref0 stored in conv_param.dst.
    //use regular path to generate a prediction for ref1 into  a temporary buffer,
    //then  blend that temporary buffer with that from  the first reference.

    DECLARE_ALIGNED(16, uint8_t, seg_mask[2 * MAX_SB_SQUARE]);

#define INTER_PRED_BYTES_PER_PIXEL 2
    DECLARE_ALIGNED(32, uint8_t, tmp_buf[INTER_PRED_BYTES_PER_PIXEL * MAX_SB_SQUARE]);
#undef INTER_PRED_BYTES_PER_PIXEL
    uint8_t  *tmp_dst        = tmp_buf;
    const int tmp_buf_stride = MAX_SB_SIZE;

    CONV_BUF_TYPE *org_dst        = conv_params->dst; //save the ref0 prediction pointer
    int            org_dst_stride = conv_params->dst_stride;
    CONV_BUF_TYPE *tmp_buf16      = (CONV_BUF_TYPE *)tmp_buf;
    conv_params->dst              = tmp_buf16;
    conv_params->dst_stride       = tmp_buf_stride;
    assert(conv_params->do_average == 0);

    uint8_t ss_x = plane == 0 ? 0 : 1; // subsamplings
    uint8_t ss_y = plane == 0 ? 0 : 1;

    svt_av1_warp_plane(wm_params_l1,
                       (int)is16bit,
                       bitdepth,
                       src_ptr,
                       src_2b_ptr,
                       (int)buf_width,
                       (int)buf_height,
                       src_stride,
                       tmp_dst,
                       pu_origin_x,
                       pu_origin_y,
                       bwidth,
                       bheight,
                       MAX_SB_SQUARE,
                       ss_x, //int subsampling_x,
                       ss_y, //int subsampling_y,
                       conv_params);

    if (!plane && comp_data->type == COMPOUND_DIFFWTD) {
        //CHKN  for DIFF: need to compute the mask  comp_data->seg_mask is the output computed from the two preds org_dst and tmp_buf16
        //for WEDGE the mask is fixed from the table based on wedge_sign/index
        svt_av1_build_compound_diffwtd_mask_d16(seg_mask,
                                                comp_data->mask_type,
                                                org_dst,
                                                org_dst_stride,
                                                tmp_buf16,
                                                tmp_buf_stride,
                                                bheight,
                                                bwidth,
                                                conv_params,
                                                bitdepth);
    }

    svt_aom_build_masked_compound_no_round(dst_ptr,
                                           dst_stride,
                                           org_dst,
                                           org_dst_stride,
                                           tmp_buf16,
                                           tmp_buf_stride,
                                           comp_data,
                                           seg_mask,
                                           blk_geom->bsize,
                                           bheight,
                                           bwidth,
                                           conv_params,
                                           bitdepth,
                                           is16bit);
}

// This function has a structure similar to av1_build_obmc_inter_prediction
//
// The OBMC predictor is computed as:
//
//  PObmc(x,y) =
//    AOM_BLEND_A64(Mh(x),
//                  AOM_BLEND_A64(Mv(y), P(x,y), PAbove(x,y)),
//                  PLeft(x, y))
//
// Scaling up by AOM_BLEND_A64_MAX_ALPHA ** 2 and omitting the intermediate
// rounding, this can be written as:
//
//  AOM_BLEND_A64_MAX_ALPHA * AOM_BLEND_A64_MAX_ALPHA * Pobmc(x,y) =
//    Mh(x) * Mv(y) * P(x,y) +
//      Mh(x) * Cv(y) * Pabove(x,y) +
//      AOM_BLEND_A64_MAX_ALPHA * Ch(x) * PLeft(x, y)
//
// Where :
//
//  Cv(y) = AOM_BLEND_A64_MAX_ALPHA - Mv(y)
//  Ch(y) = AOM_BLEND_A64_MAX_ALPHA - Mh(y)
//
// This function computes 'wsrc' and 'mask' as:
//
//  wsrc(x, y) =
//    AOM_BLEND_A64_MAX_ALPHA * AOM_BLEND_A64_MAX_ALPHA * src(x, y) -
//      Mh(x) * Cv(y) * Pabove(x,y) +
//      AOM_BLEND_A64_MAX_ALPHA * Ch(x) * PLeft(x, y)
//
//  mask(x, y) = Mh(x) * Mv(y)
//
// These can then be used to efficiently approximate the error for any
// predictor P in the context of the provided neighbouring predictors by
// computing:
//
//  error(x, y) =
//    wsrc(x, y) - mask(x, y) * P(x, y) / (AOM_BLEND_A64_MAX_ALPHA ** 2)
//
static void calc_target_weighted_pred(PictureControlSet *pcs, ModeDecisionContext *ctx, const AV1_COMMON *cm,
                                      const MacroBlockD *xd, int mi_row, int mi_col, const uint8_t *above,
                                      int above_stride, const uint8_t *left, int left_stride) {
    if (block_size_wide[ctx->blk_geom->bsize] > ctx->obmc_ctrls.max_blk_size_to_refine ||
        block_size_high[ctx->blk_geom->bsize] > ctx->obmc_ctrls.max_blk_size_to_refine)
        return;
    uint8_t         is16bit  = 0;
    const BlockSize bsize    = ctx->blk_geom->bsize;
    const int       bw       = xd->n4_w << MI_SIZE_LOG2;
    const int       bh       = xd->n4_h << MI_SIZE_LOG2;
    int32_t        *mask_buf = ctx->mask_buf;
    int32_t        *wsrc_buf = ctx->wsrc_buf;

    const int src_scale = AOM_BLEND_A64_MAX_ALPHA * AOM_BLEND_A64_MAX_ALPHA;

    memset(wsrc_buf, 0, sizeof(int32_t) * bw * bh);
    for (int i = 0; i < bw * bh; ++i) mask_buf[i] = AOM_BLEND_A64_MAX_ALPHA;

    // handle above row
    if (xd->up_available) {
        const int overlap                          = AOMMIN(block_size_high[bsize], block_size_high[BLOCK_64X64]) >> 1;
        struct calc_target_weighted_pred_ctxt ctxt = {mask_buf, wsrc_buf, above, above_stride, overlap};

        foreach_overlappable_nb_above(is16bit,
                                      cm,
                                      (MacroBlockD *)xd,
                                      mi_col,
                                      max_neighbor_obmc[mi_size_wide_log2[bsize]],
                                      svt_av1_calc_target_weighted_pred_above,
                                      &ctxt);
    }

    for (int i = 0; i < bw * bh; ++i) {
        wsrc_buf[i] *= AOM_BLEND_A64_MAX_ALPHA;
        mask_buf[i] *= AOM_BLEND_A64_MAX_ALPHA;
    }

    // handle left column
    if (xd->left_available) {
        const int overlap                          = AOMMIN(block_size_wide[bsize], block_size_wide[BLOCK_64X64]) >> 1;
        struct calc_target_weighted_pred_ctxt ctxt = {mask_buf, wsrc_buf, left, left_stride, overlap};

        foreach_overlappable_nb_left(is16bit,
                                     cm,
                                     (MacroBlockD *)xd,
                                     mi_row,
                                     max_neighbor_obmc[mi_size_high_log2[bsize]],
                                     svt_av1_calc_target_weighted_pred_left,
                                     &ctxt);
    }

    EbPictureBufferDesc *src_pic = pcs->ppcs->enhanced_pic;
    const uint8_t       *src     = src_pic->buffer_y + (ctx->blk_org_x + src_pic->org_x) +
        (ctx->blk_org_y + src_pic->org_y) * src_pic->stride_y;

    for (int row = 0; row < bh; ++row) {
        for (int col = 0; col < bw; ++col) { wsrc_buf[col] = src[col] * src_scale - wsrc_buf[col]; }
        wsrc_buf += bw;
        src += src_pic->stride_y;
    }
}
/* perform all neigh predictions and get wighted src to be used for obmc
motion refinement
*/
void svt_aom_precompute_obmc_data(PictureControlSet *pcs, ModeDecisionContext *ctx) {
    // cppcheck-suppress unassignedVariable
    DECLARE_ALIGNED(16, uint8_t, junk_2b[6 * MAX_MB_PLANE * MAX_SB_SQUARE]);
    uint8_t *tmp_obmc_bufs[] = {
        ctx->obmc_buff_0,
        ctx->obmc_buff_1,
    };
    uint8_t *dst_buf1_8b = junk_2b + 2 * MAX_MB_PLANE * MAX_SB_SQUARE,
            *dst_buf2_8b = junk_2b + 4 * MAX_MB_PLANE * MAX_SB_SQUARE;

    uint8_t *dst_buf1[MAX_MB_PLANE], *dst_buf2[MAX_MB_PLANE];
    int      dst_stride1[MAX_MB_PLANE] = {ctx->blk_geom->bwidth, ctx->blk_geom->bwidth, ctx->blk_geom->bwidth};
    int      dst_stride2[MAX_MB_PLANE] = {ctx->blk_geom->bwidth, ctx->blk_geom->bwidth, ctx->blk_geom->bwidth};

    if (ctx->hbd_md) {
        dst_buf1[0] = (uint8_t *)((uint16_t *)tmp_obmc_bufs[0]);
        dst_buf1[1] = (uint8_t *)((uint16_t *)tmp_obmc_bufs[0] + (ctx->blk_geom->bwidth * ctx->blk_geom->bheight));
        dst_buf1[2] = (uint8_t *)((uint16_t *)tmp_obmc_bufs[0] + (ctx->blk_geom->bwidth * ctx->blk_geom->bheight) * 2);
        dst_buf2[0] = (uint8_t *)((uint16_t *)tmp_obmc_bufs[1]);
        dst_buf2[1] = (uint8_t *)((uint16_t *)tmp_obmc_bufs[1] + (ctx->blk_geom->bwidth * ctx->blk_geom->bheight));
        dst_buf2[2] = (uint8_t *)((uint16_t *)tmp_obmc_bufs[1] + (ctx->blk_geom->bwidth * ctx->blk_geom->bheight) * 2);
    } else {
        dst_buf1[0] = tmp_obmc_bufs[0];
        dst_buf1[1] = tmp_obmc_bufs[0] + (ctx->blk_geom->bwidth * ctx->blk_geom->bheight);
        dst_buf1[2] = tmp_obmc_bufs[0] + (ctx->blk_geom->bwidth * ctx->blk_geom->bheight) * 2;
        dst_buf2[0] = tmp_obmc_bufs[1];
        dst_buf2[1] = tmp_obmc_bufs[1] + (ctx->blk_geom->bwidth * ctx->blk_geom->bheight);
        dst_buf2[2] = tmp_obmc_bufs[1] + (ctx->blk_geom->bwidth * ctx->blk_geom->bheight) * 2;
    }
    int mi_row = ctx->blk_org_y >> 2;
    int mi_col = ctx->blk_org_x >> 2;
    build_prediction_by_above_preds(
        1, ctx->blk_geom->bsize, pcs, ctx->blk_ptr->av1xd, mi_row, mi_col, dst_buf1, dst_stride1, ctx->hbd_md);

    build_prediction_by_left_preds(
        1, ctx->blk_geom->bsize, pcs, ctx->blk_ptr->av1xd, mi_row, mi_col, dst_buf2, dst_stride2, ctx->hbd_md);

    if (ctx->hbd_md) {
        svt_aom_un_pack2d((uint16_t *)dst_buf1[0],
                          dst_stride1[0],
                          dst_buf1_8b,
                          dst_stride1[0],
                          junk_2b,
                          dst_stride1[0],
                          ctx->blk_geom->bwidth,
                          ctx->blk_geom->bheight);
        svt_aom_un_pack2d((uint16_t *)dst_buf2[0],
                          dst_stride2[0],
                          dst_buf2_8b,
                          dst_stride2[0],
                          junk_2b,
                          dst_stride2[0],
                          ctx->blk_geom->bwidth,
                          ctx->blk_geom->bheight);
    }
    calc_target_weighted_pred(pcs,
                              ctx,
                              pcs->ppcs->av1_cm,
                              ctx->blk_ptr->av1xd,
                              mi_row,
                              mi_col,
                              ctx->hbd_md ? dst_buf1_8b : dst_buf1[0],
                              dst_stride1[0],
                              ctx->hbd_md ? dst_buf2_8b : dst_buf2[0],
                              dst_stride2[0]);
}

static void chroma_plane_warped_motion_prediction_sub8x8(PictureControlSet *pcs, uint8_t compound_idx,
                                                         BlkStruct *blk_ptr, const BlockGeom *blk_geom, uint8_t bwidth,
                                                         uint8_t bheight, uint8_t is_compound, uint8_t bit_depth,
                                                         int32_t src_stride, int32_t dst_stride, uint8_t *src_ptr_l0,
                                                         uint8_t *src_ptr_l1, uint8_t *src_ptr_2b_l0,
                                                         uint8_t *src_ptr_2b_l1, uint8_t *dst_ptr,
                                                         MvReferenceFrame rf[2], MvUnit *mv_unit, Bool is16bit) {
    DECLARE_ALIGNED(32, uint16_t, tmp_dst[64 * 64]);
    // If interpoltion filter is specified at the block level, WM will always use regular. If
    // the interpolation is specified at the frame level, WM blocks will use the specified filter.
    const uint32_t     interp_filters = (pcs->ppcs->frm_hdr.interpolation_filter == SWITCHABLE)
            ? 0
            : av1_broadcast_interp_filter(pcs->ppcs->frm_hdr.interpolation_filter);
    InterpFilterParams filter_params_x, filter_params_y;

    MV      mv_q4    = clamp_mv_to_umv_border_sb(blk_ptr->av1xd,
                                         &(const MV){.col = mv_unit->mv[mv_unit->pred_direction % BI_PRED].x,
                                                             .row = mv_unit->mv[mv_unit->pred_direction % BI_PRED].y},
                                         blk_geom->bwidth_uv,
                                         blk_geom->bheight_uv,
                                         1,
                                         1);
    int32_t subpel_x = mv_q4.col & SUBPEL_MASK;
    int32_t subpel_y = mv_q4.row & SUBPEL_MASK;

    src_ptr_l0                 = src_ptr_l0 + ((mv_q4.row >> SUBPEL_BITS) * src_stride + (mv_q4.col >> SUBPEL_BITS));
    src_ptr_2b_l0              = src_ptr_2b_l0 + ((mv_q4.row >> SUBPEL_BITS) * src_stride + (mv_q4.col >> SUBPEL_BITS));
    ConvolveParams conv_params = get_conv_params_no_round(0, 0, 0, tmp_dst, 64, is_compound, bit_depth);

    av1_get_convolve_filter_params(
        interp_filters, &filter_params_x, &filter_params_y, blk_geom->bwidth_uv, blk_geom->bheight_uv);

    if (!is16bit)
        svt_aom_convolve[subpel_x != 0][subpel_y != 0][is_compound](src_ptr_l0,
                                                                    src_stride,
                                                                    dst_ptr,
                                                                    dst_stride,
                                                                    bwidth,
                                                                    bheight,
                                                                    &filter_params_x,
                                                                    &filter_params_y,
                                                                    subpel_x,
                                                                    subpel_y,
                                                                    &conv_params);
    else {
        uint16_t *src_10b;
        DECLARE_ALIGNED(16, uint16_t, packed_buf[PACKED_BUFFER_SIZE]);
        // pack the reference into temp 16bit buffer
        uint8_t offset = INTERPOLATION_OFFSET;
        int32_t stride = STRIDE_PACK;

        svt_aom_pack_block(src_ptr_l0 - offset - (offset * src_stride),
                           src_stride,
                           src_ptr_2b_l0 - offset - (offset * src_stride),
                           src_stride,
                           (uint16_t *)packed_buf,
                           stride,
                           bwidth + (offset << 1),
                           bheight + (offset << 1));

        src_10b = (uint16_t *)packed_buf + offset + (offset * stride);

        svt_aom_convolveHbd[subpel_x != 0][subpel_y != 0][is_compound](src_10b,
                                                                       stride,
                                                                       (uint16_t *)dst_ptr,
                                                                       dst_stride,
                                                                       bwidth,
                                                                       bheight,
                                                                       &filter_params_x,
                                                                       &filter_params_y,
                                                                       subpel_x,
                                                                       subpel_y,
                                                                       &conv_params,
                                                                       bit_depth);
    }

    //List1-Cb
    if (is_compound) {
        mv_q4 = clamp_mv_to_umv_border_sb(
            blk_ptr->av1xd,
            &(const MV){.col = mv_unit->mv[REF_LIST_1].x, .row = mv_unit->mv[REF_LIST_1].y},
            blk_geom->bwidth_uv,
            blk_geom->bheight_uv,
            1,
            1);
        subpel_x      = mv_q4.col & SUBPEL_MASK;
        subpel_y      = mv_q4.row & SUBPEL_MASK;
        src_ptr_l1    = src_ptr_l1 + ((mv_q4.row >> SUBPEL_BITS) * src_stride + (mv_q4.col >> SUBPEL_BITS));
        src_ptr_2b_l1 = src_ptr_2b_l1 + ((mv_q4.row >> SUBPEL_BITS) * src_stride + (mv_q4.col >> SUBPEL_BITS));

        svt_av1_dist_wtd_comp_weight_assign(&pcs->ppcs->scs->seq_header,
                                            pcs->ppcs->cur_order_hint, // cur_frame_index,
                                            pcs->ppcs->ref_order_hint[rf[0] - 1], // bck_frame_index,
                                            pcs->ppcs->ref_order_hint[rf[1] - 1], // fwd_frame_index,
                                            compound_idx,
                                            0, // order_idx,
                                            &conv_params.fwd_offset,
                                            &conv_params.bck_offset,
                                            &conv_params.use_dist_wtd_comp_avg,
                                            is_compound);
        conv_params.use_jnt_comp_avg = conv_params.use_dist_wtd_comp_avg;
        av1_get_convolve_filter_params(
            interp_filters, &filter_params_x, &filter_params_y, blk_geom->bwidth_uv, blk_geom->bheight_uv);

        conv_params.do_average = 1;
        if (!is16bit)
            svt_aom_convolve[subpel_x != 0][subpel_y != 0][is_compound](
                src_ptr_l1,
                src_stride,
                dst_ptr,
                dst_stride,
                bwidth,
                bheight,
                &filter_params_x, //puSize > 8 ? &av1RegularFilter : &av1RegularFilterW4,
                &filter_params_y, //puSize > 8 ? &av1RegularFilter : &av1RegularFilterW4,
                subpel_x,
                subpel_y,
                &conv_params);
        else {
            uint16_t *src_10b;
            DECLARE_ALIGNED(16, uint16_t, packed_buf[PACKED_BUFFER_SIZE]);
            // pack the reference into temp 16bit buffer
            uint8_t offset = INTERPOLATION_OFFSET;
            int32_t stride = STRIDE_PACK;

            svt_aom_pack_block(src_ptr_l1 - offset - (offset * src_stride),
                               src_stride,
                               src_ptr_2b_l1 - offset - (offset * src_stride),
                               src_stride,
                               (uint16_t *)packed_buf,
                               stride,
                               bwidth + (offset << 1),
                               bheight + (offset << 1));

            src_10b = (uint16_t *)packed_buf + offset + (offset * stride);

            svt_aom_convolveHbd[subpel_x != 0][subpel_y != 0][is_compound](
                src_10b,
                stride,
                (uint16_t *)dst_ptr,
                dst_stride,
                bwidth,
                bheight,
                &filter_params_x, //puSize > 8 ? &av1RegularFilter : &av1RegularFilterW4,
                &filter_params_y, //puSize > 8 ? &av1RegularFilter : &av1RegularFilterW4,
                subpel_x,
                subpel_y,
                &conv_params,
                bit_depth);
        }
    }
}

static void plane_warped_motion_prediction(PictureControlSet *pcs, uint8_t compound_idx,
                                           InterInterCompoundData *interinter_comp, uint16_t pu_origin_x,
                                           uint16_t pu_origin_y, const BlockGeom *blk_geom, uint8_t bwidth,
                                           uint8_t bheight, EbWarpedMotionParams *wm_params_l0,
                                           EbWarpedMotionParams *wm_params_l1, uint8_t is_compound, uint8_t bit_depth,
                                           int32_t src_stride, int32_t dst_stride, uint16_t buf_width,
                                           uint16_t buf_height, uint8_t ss_x, uint8_t ss_y, uint8_t *src_ptr_l0,
                                           uint8_t *src_ptr_l1, uint8_t *src_ptr_2b_l0, uint8_t *src_ptr_2b_l1,
                                           uint8_t *dst_ptr, uint8_t plane, MvReferenceFrame rf[2], Bool is16bit) {
    if (!is_compound) {
        ConvolveParams conv_params = get_conv_params_no_round(0, 0, 0, NULL, 128, is_compound, bit_depth);

        svt_av1_warp_plane(wm_params_l0,
                           (int)is16bit,
                           bit_depth,
                           src_ptr_l0,
                           src_ptr_2b_l0,
                           (int)buf_width,
                           (int)buf_height,
                           src_stride,
                           dst_ptr,
                           pu_origin_x,
                           pu_origin_y,
                           bwidth,
                           bheight,
                           dst_stride,
                           ss_x,
                           ss_y,
                           &conv_params);
    } else {
        DECLARE_ALIGNED(32, uint16_t, tmp_dstY[128 * 128]); //move this to context if stack does not hold.

        ConvolveParams conv_params = get_conv_params_no_round(0, 0, 0, tmp_dstY, 128, is_compound, bit_depth);
        svt_av1_dist_wtd_comp_weight_assign(&pcs->ppcs->scs->seq_header,
                                            pcs->ppcs->cur_order_hint, // cur_frame_index,
                                            pcs->ppcs->ref_order_hint[rf[0] - 1], // bck_frame_index,
                                            pcs->ppcs->ref_order_hint[rf[1] - 1], // fwd_frame_index,
                                            compound_idx,
                                            0, // order_idx,
                                            &conv_params.fwd_offset,
                                            &conv_params.bck_offset,
                                            &conv_params.use_dist_wtd_comp_avg,
                                            is_compound);
        conv_params.use_jnt_comp_avg = conv_params.use_dist_wtd_comp_avg;

        conv_params.do_average = 0;
        svt_av1_warp_plane(wm_params_l0,
                           (int)is16bit,
                           bit_depth,
                           src_ptr_l0,
                           src_ptr_2b_l0,
                           (int)buf_width,
                           (int)buf_height,
                           src_stride,
                           dst_ptr,
                           pu_origin_x,
                           pu_origin_y,
                           bwidth,
                           bheight,
                           dst_stride,
                           ss_x,
                           ss_y,
                           &conv_params);

        if (svt_aom_is_masked_compound_type(interinter_comp->type)) {
            av1_make_masked_warp_inter_predictor(src_ptr_l1,
                                                 src_ptr_2b_l1,

                                                 src_stride,
                                                 buf_width,
                                                 buf_height,
                                                 dst_ptr,
                                                 dst_stride,
                                                 blk_geom,
                                                 bwidth,
                                                 bheight,
                                                 &conv_params,
                                                 interinter_comp,
                                                 bit_depth,
                                                 plane,
                                                 pu_origin_x,
                                                 pu_origin_y,
                                                 wm_params_l1,
                                                 is16bit);
        } else {
            conv_params.do_average = 1;
            svt_av1_warp_plane(wm_params_l1,
                               (int)is16bit,
                               bit_depth,
                               src_ptr_l1,
                               src_ptr_2b_l1,
                               (int)buf_width,
                               (int)buf_height,
                               src_stride,
                               dst_ptr,
                               pu_origin_x,
                               pu_origin_y,
                               bwidth,
                               bheight,
                               dst_stride,
                               ss_x,
                               ss_y,
                               &conv_params);
        }
    }
}
static int32_t svt_av1_get_switchable_rate(ModeDecisionCandidateBuffer *cand_bf, const FrameHeader *const frm_hdr,
                                           ModeDecisionContext *ctx, const bool enable_dual_filter) {
    if (frm_hdr->interpolation_filter == SWITCHABLE) {
        int32_t   inter_filter_cost = 0;
        const int max_dir           = enable_dual_filter ? 2 : 1;
        for (int dir = 0; dir < max_dir; ++dir) {
            MvReferenceFrame rf[2];
            av1_set_ref_frame(rf, cand_bf->cand->ref_frame_type);
            const int32_t pred_ctx = svt_aom_get_pred_context_switchable_interp(rf[0], rf[1], ctx->blk_ptr->av1xd, dir);

            const InterpFilter filter = av1_extract_interp_filter(cand_bf->cand->interp_filters, dir);
            assert(pred_ctx < SWITCHABLE_FILTER_CONTEXTS);
            assert(filter < SWITCHABLE_FILTERS);
            inter_filter_cost += ctx->md_rate_est_ctx->switchable_interp_fac_bitss[pred_ctx][filter];
        }
        return inter_filter_cost;
    }

    return 0;
}

static void model_rd_norm(int32_t xsq_q10, int32_t *r_q10, int32_t *d_q10) {
    // NOTE: The tables below must be of the same size.

    // The functions described below are sampled at the four most significant
    // bits of x^2 + 8 / 256.

    // Normalized rate:
    // This table models the rate for a Laplacian source with given variance
    // when quantized with a uniform quantizer with given stepsize. The
    // closed form expression is:
    // Rn(x) = H(sqrt(r)) + sqrt(r)*[1 + H(r)/(1 - r)],
    // where r = exp(-sqrt(2) * x) and x = qpstep / sqrt(variance),
    // and H(x) is the binary entropy function.
    static const int32_t rate_tab_q10[] = {
        65536, 6086, 5574, 5275, 5063, 4899, 4764, 4651, 4553, 4389, 4255, 4142, 4044, 3958, 3881, 3811, 3748, 3635,
        3538,  3453, 3376, 3307, 3244, 3186, 3133, 3037, 2952, 2877, 2809, 2747, 2690, 2638, 2589, 2501, 2423, 2353,
        2290,  2232, 2179, 2130, 2084, 2001, 1928, 1862, 1802, 1748, 1698, 1651, 1608, 1530, 1460, 1398, 1342, 1290,
        1243,  1199, 1159, 1086, 1021, 963,  911,  864,  821,  781,  745,  680,  623,  574,  530,  490,  455,  424,
        395,   345,  304,  269,  239,  213,  190,  171,  154,  126,  104,  87,   73,   61,   52,   44,   38,   28,
        21,    16,   12,   10,   8,    6,    5,    3,    2,    1,    1,    1,    0,    0,
    };
    // Normalized distortion:
    // This table models the normalized distortion for a Laplacian source
    // with given variance when quantized with a uniform quantizer
    // with given stepsize. The closed form expression is:
    // Dn(x) = 1 - 1/sqrt(2) * x / sinh(x/sqrt(2))
    // where x = qpstep / sqrt(variance).
    // Note the actual distortion is Dn * variance.
    static const int32_t dist_tab_q10[] = {
        0,    0,    1,    1,    1,    2,    2,    2,    3,    3,    4,    5,    5,    6,    7,   7,   8,   9,
        11,   12,   13,   15,   16,   17,   18,   21,   24,   26,   29,   31,   34,   36,   39,  44,  49,  54,
        59,   64,   69,   73,   78,   88,   97,   106,  115,  124,  133,  142,  151,  167,  184, 200, 215, 231,
        245,  260,  274,  301,  327,  351,  375,  397,  418,  439,  458,  495,  528,  559,  587, 613, 637, 659,
        680,  717,  749,  777,  801,  823,  842,  859,  874,  899,  919,  936,  949,  960,  969, 977, 983, 994,
        1001, 1006, 1010, 1013, 1015, 1017, 1018, 1020, 1022, 1022, 1023, 1023, 1023, 1024,
    };
    static const int32_t xsq_iq_q10[] = {
        0,     4,     8,      12,     16,     20,     24,     28,     32,     40,     48,     56,     64,
        72,    80,    88,     96,     112,    128,    144,    160,    176,    192,    208,    224,    256,
        288,   320,   352,    384,    416,    448,    480,    544,    608,    672,    736,    800,    864,
        928,   992,   1120,   1248,   1376,   1504,   1632,   1760,   1888,   2016,   2272,   2528,   2784,
        3040,  3296,  3552,   3808,   4064,   4576,   5088,   5600,   6112,   6624,   7136,   7648,   8160,
        9184,  10208, 11232,  12256,  13280,  14304,  15328,  16352,  18400,  20448,  22496,  24544,  26592,
        28640, 30688, 32736,  36832,  40928,  45024,  49120,  53216,  57312,  61408,  65504,  73696,  81888,
        90080, 98272, 106464, 114656, 122848, 131040, 147424, 163808, 180192, 196576, 212960, 229344, 245728,
    };
    const int32_t tmp     = (xsq_q10 >> 2) + 8;
    const int32_t k       = get_msb(tmp) - 3;
    const int32_t xq      = (k << 3) + ((tmp >> k) & 0x7);
    const int32_t one_q10 = 1 << 10;
    const int32_t a_q10   = ((xsq_q10 - xsq_iq_q10[xq]) << 10) >> (2 + k);
    const int32_t b_q10   = one_q10 - a_q10;
    *r_q10                = (rate_tab_q10[xq] * b_q10 + rate_tab_q10[xq + 1] * a_q10) >> 10;
    *d_q10                = (dist_tab_q10[xq] * b_q10 + dist_tab_q10[xq + 1] * a_q10) >> 10;
}

void svt_av1_model_rd_from_var_lapndz(int64_t var, uint32_t n_log2, uint32_t qstep, int32_t *rate, int64_t *dist) {
    // This function models the rate and distortion for a Laplacian
    // source with given variance when quantized with a uniform quantizer
    // with given stepsize. The closed form expressions are in:
    // Hang and Chen, "Source Model for transform video coder and its
    // application - Part I: Fundamental Theory", IEEE Trans. Circ.
    // Sys. for Video Tech., April 1997.
    if (var == 0) {
        *rate = 0;
        *dist = 0;
    } else {
        int32_t               d_q10, r_q10;
        static const uint32_t MAX_XSQ_Q10 = 245727;
        const uint64_t        xsq_q10_64  = (((uint64_t)qstep * qstep << (n_log2 + 10)) + (var >> 1)) / var;
        const int32_t         xsq_q10     = (int32_t)MIN(xsq_q10_64, MAX_XSQ_Q10);
        model_rd_norm(xsq_q10, &r_q10, &d_q10);
        *rate = ROUND_POWER_OF_TWO(r_q10 << n_log2, 10 - AV1_PROB_COST_SHIFT);
        *dist = (var * (int64_t)d_q10 + 512) >> 10;
    }
}

void model_rd_from_sse(BlockSize bsize, int16_t quantizer, uint8_t bit_depth, uint64_t sse, uint32_t *rate,
                       uint64_t *dist, uint8_t simple_model_rd_from_var) {
    int32_t dequant_shift = bit_depth - 5;

    // Fast approximate the modelling function.
    if (simple_model_rd_from_var) {
        int64_t square_error = (uint64_t)sse;
        quantizer            = quantizer >> dequant_shift;

        if (quantizer < 120)
            *rate = (int32_t)((square_error * (280 - quantizer)) >> (16 - AV1_PROB_COST_SHIFT));
        else
            *rate = 0;
        *dist = (uint64_t)(square_error * quantizer) >> 8;
    } else {
        svt_av1_model_rd_from_var_lapndz(
            (uint64_t)sse, num_pels_log2_lookup[bsize], quantizer >> dequant_shift, (int32_t *)rate, (int64_t *)dist);
    }

    *dist <<= 4;
}

static void model_rd_for_sb(PictureControlSet *pcs, EbPictureBufferDesc *prediction_ptr, ModeDecisionContext *ctx,
                            int32_t plane_from, int32_t plane_to, int32_t *out_rate_sum, int64_t *out_dist_sum,
                            uint8_t bit_depth) {
    // Note our transform coeffs are 8 times an orthogonal transform.
    // Hence quantizer step is also 8 times. To get effective quantizer
    // we need to divide by 8 before sending to modeling function.
    uint64_t rate_sum = 0;
    uint64_t dist_sum = 0;

    EbPictureBufferDesc *input_pic    = bit_depth > 8 ? pcs->input_frame16bit : pcs->ppcs->enhanced_pic;
    const uint32_t       input_offset = (ctx->blk_org_y + input_pic->org_y) * input_pic->stride_y +
        (ctx->blk_org_x + input_pic->org_x);
    const uint32_t input_chroma_offset = ((ctx->blk_org_y + input_pic->org_y) * input_pic->stride_cb +
                                          (ctx->blk_org_x + input_pic->org_x)) /
        2;
    const int32_t prediction_offset = prediction_ptr->org_x + ctx->blk_geom->org_x +
        (prediction_ptr->org_y + ctx->blk_geom->org_y) * prediction_ptr->stride_y;
    const int32_t prediction_chroma_offset = (prediction_ptr->org_x + ctx->blk_geom->org_x +
                                              (prediction_ptr->org_y + ctx->blk_geom->org_y) *
                                                  prediction_ptr->stride_cb) /
        2;

    EbSpatialFullDistType spatial_full_dist_type_fun = bit_depth > 8 ? svt_full_distortion_kernel16_bits
                                                                     : svt_spatial_full_distortion_kernel;
    const uint16_t        blk_height                 = ctx->blk_geom->bheight;
    const uint8_t         shift = (ctx->ifs_ctrls.subsampled_distortion && (blk_height > 16)) ? 1 : 0;
    for (int32_t plane = plane_from; plane <= plane_to; ++plane) {
        uint64_t sse;
        uint32_t rate;
        uint64_t dist;

        switch (plane) {
        case 0:
            sse = spatial_full_dist_type_fun(input_pic->buffer_y,
                                             input_offset,
                                             input_pic->stride_y << shift,
                                             prediction_ptr->buffer_y,
                                             prediction_offset,
                                             prediction_ptr->stride_y << shift,
                                             ctx->blk_geom->bwidth,
                                             ctx->blk_geom->bheight >> shift)
                << shift;
            break;
        case 1:
            sse = spatial_full_dist_type_fun(input_pic->buffer_cb,
                                             input_chroma_offset,
                                             input_pic->stride_cb,
                                             prediction_ptr->buffer_cb,
                                             prediction_chroma_offset,
                                             prediction_ptr->stride_cb,
                                             ctx->blk_geom->bwidth_uv,
                                             ctx->blk_geom->bheight_uv);
            break;
        default:
            sse = spatial_full_dist_type_fun(input_pic->buffer_cr,
                                             input_chroma_offset,
                                             input_pic->stride_cr,
                                             prediction_ptr->buffer_cr,
                                             prediction_chroma_offset,
                                             prediction_ptr->stride_cr,
                                             ctx->blk_geom->bwidth_uv,
                                             ctx->blk_geom->bheight_uv);
            break;
        }
        if (ctx->ifs_ctrls.skip_sse_rd_model) {
            rate = 0;
            dist = sse;
            dist_sum += dist * 10;
        } else {
            SequenceControlSet *scs             = pcs->scs;
            const uint8_t       current_q_index = pcs->ppcs->frm_hdr.quantization_params.base_q_idx;
            Dequants *const     dequants        = ctx->hbd_md ? &scs->enc_ctx->deq_bd : &scs->enc_ctx->deq_8bit;
            int16_t             quantizer       = dequants->y_dequant_qtx[current_q_index][1];
            model_rd_from_sse(plane == 0 ? ctx->blk_geom->bsize : ctx->blk_geom->bsize_uv,
                              quantizer,
                              bit_depth,
                              ROUND_POWER_OF_TWO(sse, 2 * (bit_depth - 8)),
                              &rate,
                              &dist,
                              0);

            rate_sum += rate;
            dist_sum += dist;
        }
    }

    *out_rate_sum = (int32_t)rate_sum;
    *out_dist_sum = dist_sum;
}

int32_t svt_aom_is_nontrans_global_motion(BlockSize bsize, ModeDecisionCandidateBuffer *cand_bf,
                                          PictureControlSet *pcs) {
    int32_t ref;

    // First check if all modes are GLOBALMV
    if (cand_bf->cand->pred_mode != GLOBALMV && cand_bf->cand->pred_mode != GLOBAL_GLOBALMV)
        return 0;

    if (MIN(mi_size_wide[bsize], mi_size_high[bsize]) < 2)
        return 0;
    MvReferenceFrame rf[2];
    av1_set_ref_frame(rf, cand_bf->cand->ref_frame_type);
    const uint8_t is_compound = is_inter_compound_mode(cand_bf->cand->pred_mode);
    // Now check if all global motion is non translational
    for (ref = 0; ref < 1 + is_compound /*svt_aom_has_second_ref(mbmi)*/; ++ref) {
        if (pcs->ppcs->global_motion[ref ? rf[1] : rf[0]].wmtype == TRANSLATION)
            //if (xd->global_motion[mbmi->ref_frame[ref]].wmtype == TRANSLATION)
            return 0;
    }
    return 1;
}
static INLINE int32_t av1_is_interp_needed(ModeDecisionCandidateBuffer *cand_bf, PictureControlSet *pcs,
                                           BlockSize bsize) {
    /*Disable check on skip_mode_allowed (i.e. skip_mode).  If IFS is selected, skip_mode will be
     * disabled.*/
    if (cand_bf->cand->motion_mode == WARPED_CAUSAL)
        return 0;

    if (svt_aom_is_nontrans_global_motion(bsize, cand_bf, pcs))
        return 0;

    return 1;
}

#define DUAL_FILTER_SET_SIZE (SWITCHABLE_FILTERS * SWITCHABLE_FILTERS)
static const int32_t filter_sets[DUAL_FILTER_SET_SIZE][2] = {
    {0, 0},
    {0, 1},
    {0, 2},
    {1, 0},
    {1, 1},
    {1, 2},
    {2, 0},
    {2, 1},
    {2, 2},
};
static void interpolation_filter_search(PictureControlSet *pcs, ModeDecisionContext *ctx,
                                        ModeDecisionCandidateBuffer *cand_bf, MvUnit mv_unit,
                                        EbPictureBufferDesc *ref_pic_list0, EbPictureBufferDesc *ref_pic_list1,
                                        uint8_t hbd_md, uint8_t bit_depth) {
    SequenceControlSet *scs                = pcs->scs;
    const FrameHeader  *frm_hdr            = &pcs->ppcs->frm_hdr;
    const uint8_t       enable_dual_filter = scs->seq_header.enable_dual_filter;
    const uint32_t      encoder_bit_depth  = scs->static_config.encoder_bit_depth;

    /* Save the orignal interp filter because the compensation is likely available
     * for that filter, and can be skipped in IFS. Also, we shouldn't overwrite
     * the pred buffer with a filter that is different from the input, because
     * the luma compensation may be skipped after the search if the best selected
     * interp filter matches in the input interp filter. */
    const uint32_t org_interp_filters = cand_bf->cand->interp_filters;

    uint32_t full_lambda_divided = hbd_md ? ctx->full_lambda_md[EB_10_BIT_MD] >> (2 * (bit_depth - 8))
                                          : ctx->full_lambda_md[EB_8_BIT_MD];

    NeighborArrayUnit *recon_neigh_y;
    NeighborArrayUnit *recon_neigh_cb;
    NeighborArrayUnit *recon_neigh_cr;

    static const uint64_t ifs_smooth_bias[] = {
        130, 130, 130, 130, 130, 130, 130, 130, 130, 130, 130, 130, 130, 130, 130, 130, 120, 120, 120, 120, 120, 120,
        120, 120, 120, 120, 120, 120, 120, 120, 120, 120, 110, 110, 110, 110, 110, 110, 110, 110, 110, 110, 110, 110,
        110, 110, 110, 110, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100};
    if (!hbd_md) {
        recon_neigh_y  = ctx->recon_neigh_y;
        recon_neigh_cb = ctx->recon_neigh_cb;
        recon_neigh_cr = ctx->recon_neigh_cr;
    } else {
        recon_neigh_y  = ctx->luma_recon_na_16bit;
        recon_neigh_cb = ctx->cb_recon_na_16bit;
        recon_neigh_cr = ctx->cr_recon_na_16bit;
    }

    assert(frm_hdr->interpolation_filter == SWITCHABLE);
    assert(av1_is_interp_needed(cand_bf, pcs, ctx->blk_geom->bsize));

    int32_t  switchable_rate = 0;
    uint64_t rd              = (uint64_t)~0;
    uint32_t best_filters    = 0;

    // Loop over allowable filter combinations and select the best one
    for (unsigned int i = 0; i < DUAL_FILTER_SET_SIZE; i++) {
        // If dual filter is disabled, only test combos that use the same horizontal and vertical filter
        if (enable_dual_filter == 0 && (filter_sets[i][0] != filter_sets[i][1]))
            continue;

        cand_bf->cand->interp_filters = av1_make_interp_filters((InterpFilter)filter_sets[i][0],
                                                                (InterpFilter)filter_sets[i][1]);

        const int32_t tmp_rs = svt_av1_get_switchable_rate(cand_bf, frm_hdr, ctx, enable_dual_filter);

        /*
         * Skip the prediction if the interp_filter matches the current interp_filter
         * for MDS1 or higher (since previously performed @ mds0), and EB_EIGHT_BIT (to do for EB_TEN_BIT).
         */
        const uint8_t is_pred_buffer_ready = (cand_bf->valid_pred &&
                                              cand_bf->cand->interp_filters == org_interp_filters &&
                                              ctx->md_stage > MD_STAGE_0 && encoder_bit_depth == EB_EIGHT_BIT);

        if (is_pred_buffer_ready == 0) {
            svt_aom_inter_prediction(scs,
                                     pcs,
                                     cand_bf->cand->interp_filters,
                                     ctx->blk_ptr,
                                     cand_bf->cand->ref_frame_type,
                                     &mv_unit,
                                     cand_bf->cand->use_intrabc,
                                     (encoder_bit_depth > EB_EIGHT_BIT) ? 0 : cand_bf->cand->motion_mode,
                                     1,
                                     ctx,
                                     cand_bf->cand->compound_idx,
                                     &cand_bf->cand->interinter_comp,
                                     recon_neigh_y,
                                     recon_neigh_cb,
                                     recon_neigh_cr,
                                     (encoder_bit_depth > EB_EIGHT_BIT) ? 0 : cand_bf->cand->is_interintra_used,
                                     cand_bf->cand->interintra_mode,
                                     cand_bf->cand->use_wedge_interintra,
                                     cand_bf->cand->interintra_wedge_index,
                                     ctx->blk_org_x,
                                     ctx->blk_org_y,
                                     ctx->blk_geom->bwidth,
                                     ctx->blk_geom->bheight,
                                     ref_pic_list0,
                                     ref_pic_list1,
                                     ctx->scratch_prediction_ptr,
                                     ctx->blk_geom->org_x,
                                     ctx->blk_geom->org_y,
                                     PICTURE_BUFFER_DESC_LUMA_MASK,
                                     hbd_md ? EB_TEN_BIT : EB_EIGHT_BIT,
                                     0); // is_16bit_pipeline
        }

        int32_t tmp_rate;
        int64_t tmp_dist;
        model_rd_for_sb(pcs,
                        is_pred_buffer_ready ? cand_bf->pred : ctx->scratch_prediction_ptr,
                        ctx,
                        0,
                        0,
                        &tmp_rate,
                        &tmp_dist,
                        hbd_md ? EB_TEN_BIT : EB_EIGHT_BIT);
        uint64_t tmp_rd = RDCOST(full_lambda_divided, tmp_rs + tmp_rate, tmp_dist);

        if (scs->vq_ctrls.sharpness_ctrls.ifs && pcs->ppcs->is_noise_level) {
            if (filter_sets[i][0] == 1 || filter_sets[i][1] == 1)
                tmp_rd = (tmp_rd * ifs_smooth_bias[pcs->picture_qp]) / 100;
        }

        // Update best interpoaltion filter
        if (tmp_rd < rd) {
            rd              = tmp_rd;
            switchable_rate = tmp_rs;
            best_filters    = cand_bf->cand->interp_filters;
        }
    }

    cand_bf->cand->interp_filters = best_filters;

    // If filters are non-zero, cannot use skip_mode. Opt to use IFS over skip_mode.
    if (cand_bf->cand->skip_mode_allowed && cand_bf->cand->interp_filters != 0)
        cand_bf->cand->skip_mode_allowed = FALSE;

    // Update fast_luma_rate to take into account switchable_rate
    cand_bf->fast_luma_rate += switchable_rate;
}
/*
    compound inter-intra:
    perform intra prediction and inter-intra blending
*/
static void inter_intra_prediction(PictureControlSet *pcs, EbPictureBufferDesc *pred_pic,
                                   InterIntraMode interintra_mode, uint8_t use_wedge_interintra,
                                   int32_t interintra_wedge_index, NeighborArrayUnit *recon_neigh_y,
                                   NeighborArrayUnit *recon_neigh_cb, NeighborArrayUnit *recon_neigh_cr,
                                   BlkStruct *blk_ptr, const BlockGeom *blk_geom, int16_t pu_origin_x,
                                   uint16_t pu_origin_y, uint16_t dst_origin_x, uint16_t dst_origin_y,
                                   uint32_t component_mask, uint8_t bit_depth, Bool is16bit) {
    int32_t start_plane = (component_mask & PICTURE_BUFFER_DESC_LUMA_MASK) ? 0 : 1;
    int32_t end_plane   = (component_mask & PICTURE_BUFFER_DESC_CHROMA_MASK) && blk_geom->has_uv ? MAX_MB_PLANE : 1;

    // temp buffer for intra pred (luma/chroma computed separately, so can re-use buffer)
    DECLARE_ALIGNED(16, uint8_t, intra_pred[MAX_SB_SQUARE]);

    uint8_t *dst;
    int32_t  dst_stride, intra_stride;
    uint32_t sb_size_luma   = pcs->scs->sb_size;
    uint32_t sb_size_chroma = pcs->scs->sb_size >> 1;

    EbPictureBufferDesc intra_pred_desc;
    intra_pred_desc.org_x = intra_pred_desc.org_y = 0;
    intra_pred_desc.stride_y                      = blk_geom->bwidth;
    intra_pred_desc.stride_cb                     = blk_geom->bwidth / 2;
    intra_pred_desc.stride_cr                     = blk_geom->bwidth / 2;
    intra_pred_desc.buffer_y                      = intra_pred;
    intra_pred_desc.buffer_cb                     = intra_pred;
    intra_pred_desc.buffer_cr                     = intra_pred;

    for (int32_t plane = start_plane; plane < end_plane; ++plane) {
        const int       ssx         = plane ? 1 : 0;
        const int       ssy         = plane ? 1 : 0;
        const BlockSize plane_bsize = get_plane_block_size(blk_geom->bsize, ssx, ssy);
        //av1_build_interintra_predictors_sbp
        uint8_t topNeighArray[(64 * 2 + 1) << 1];
        uint8_t leftNeighArray[(64 * 2 + 1) << 1];

        uint32_t blk_originx_uv = (pu_origin_x >> 3 << 3) >> 1;
        uint32_t blk_originy_uv = (pu_origin_y >> 3 << 3) >> 1;

        if (plane == 0) {
            dst = pred_pic->buffer_y +
                ((pred_pic->org_x + dst_origin_x + (pred_pic->org_y + dst_origin_y) * pred_pic->stride_y) << is16bit);
            dst_stride   = pred_pic->stride_y;
            intra_stride = intra_pred_desc.stride_y;

            if (pu_origin_y != 0)
                svt_memcpy(topNeighArray + ((uint64_t)1 << is16bit),
                           recon_neigh_y->top_array + ((uint64_t)pu_origin_x << is16bit),
                           blk_geom->bwidth * 2 << is16bit);

            if (pu_origin_x != 0) {
                uint16_t multipler = (pu_origin_y % sb_size_luma + blk_geom->bheight * 2) > sb_size_luma ? 1 : 2;
                svt_memcpy(leftNeighArray + ((uint64_t)1 << is16bit),
                           recon_neigh_y->left_array + ((uint64_t)pu_origin_y << is16bit),
                           blk_geom->bheight * multipler << is16bit);
            }

            if (pu_origin_y != 0 && pu_origin_x != 0)
                topNeighArray[0] = leftNeighArray[0] =
                    recon_neigh_y->top_left_array[(recon_neigh_y->max_pic_h + pu_origin_x - pu_origin_y) << is16bit];

        }

        else if (plane == 1) {
            dst = pred_pic->buffer_cb +
                (((pred_pic->org_x + ((dst_origin_x >> 3) << 3)) / 2 +
                  (pred_pic->org_y + ((dst_origin_y >> 3) << 3)) / 2 * pred_pic->stride_cb)
                 << is16bit);
            dst_stride   = pred_pic->stride_cb;
            intra_stride = intra_pred_desc.stride_cb;

            if (blk_originy_uv != 0)
                svt_memcpy(topNeighArray + ((uint64_t)1 << is16bit),
                           recon_neigh_cb->top_array + ((uint64_t)blk_originx_uv << is16bit),
                           blk_geom->bwidth_uv * 2 << is16bit);

            if (blk_originx_uv != 0) {
                uint16_t multipler = (blk_originy_uv % sb_size_chroma + blk_geom->bheight_uv * 2) > sb_size_chroma ? 1
                                                                                                                   : 2;
                svt_memcpy(leftNeighArray + ((uint64_t)1 << is16bit),
                           recon_neigh_cb->left_array + ((uint64_t)blk_originy_uv << is16bit),
                           blk_geom->bheight_uv * multipler << is16bit);
            }

            if (blk_originy_uv != 0 && blk_originx_uv != 0)
                topNeighArray[0] = leftNeighArray[0] =
                    recon_neigh_cb
                        ->top_left_array[(recon_neigh_cb->max_pic_h + blk_originx_uv - blk_originy_uv / 2) << is16bit];
        } else {
            dst = pred_pic->buffer_cr +
                (((pred_pic->org_x + ((dst_origin_x >> 3) << 3)) / 2 +
                  (pred_pic->org_y + ((dst_origin_y >> 3) << 3)) / 2 * pred_pic->stride_cr)
                 << is16bit);
            dst_stride   = pred_pic->stride_cr;
            intra_stride = intra_pred_desc.stride_cr;

            if (blk_originy_uv != 0)
                svt_memcpy(topNeighArray + ((uint64_t)1 << is16bit),
                           recon_neigh_cr->top_array + ((uint64_t)blk_originx_uv << is16bit),
                           blk_geom->bwidth_uv * 2 << is16bit);

            if (blk_originx_uv != 0) {
                uint16_t multipler = (blk_originy_uv % sb_size_chroma + blk_geom->bheight_uv * 2) > sb_size_chroma ? 1
                                                                                                                   : 2;
                svt_memcpy(leftNeighArray + ((uint64_t)1 << is16bit),
                           recon_neigh_cr->left_array + ((uint64_t)blk_originy_uv << is16bit),
                           blk_geom->bheight_uv * multipler << is16bit);
            }

            if (blk_originy_uv != 0 && blk_originx_uv != 0)
                topNeighArray[0] = leftNeighArray[0] =
                    recon_neigh_cr
                        ->top_left_array[(recon_neigh_cr->max_pic_h + blk_originx_uv - blk_originy_uv / 2) << is16bit];
        }
        TxSize tx_size        = blk_geom->txsize[0]; // Nader - Intra 128x128 not supported
        TxSize tx_size_Chroma = blk_geom->txsize_uv[0]; //Nader - Intra 128x128 not supported

        if (is16bit) {
            svt_av1_predict_intra_block_16bit(bit_depth,
                                              !ED_STAGE,
                                              blk_geom,
                                              blk_ptr->av1xd,
                                              plane ? blk_geom->bwidth_uv : blk_geom->bwidth,
                                              plane ? blk_geom->bheight_uv : blk_geom->bheight,
                                              plane ? tx_size_Chroma : tx_size,
                                              interintra_to_intra_mode[interintra_mode],
                                              0,
                                              0,
                                              NULL,
                                              FILTER_INTRA_MODES,
                                              (uint16_t *)topNeighArray + 1,
                                              (uint16_t *)leftNeighArray + 1,
                                              &intra_pred_desc,
                                              0,
                                              0,
                                              plane,
                                              blk_geom->bsize,
                                              dst_origin_x,
                                              dst_origin_y,
                                              pu_origin_x,
                                              pu_origin_y,
                                              0,
                                              0,
                                              &pcs->scs->seq_header);
            svt_aom_combine_interintra_highbd(interintra_mode,
                                              use_wedge_interintra,
                                              interintra_wedge_index,
                                              INTERINTRA_WEDGE_SIGN,
                                              blk_geom->bsize,
                                              plane_bsize,
                                              dst,
                                              dst_stride,
                                              dst, // Inter pred buff
                                              dst_stride, // Inter pred stride
                                              intra_pred, // Intra pred buff
                                              intra_stride, // Intra pred stride
                                              bit_depth);
        } else {
            svt_av1_predict_intra_block(!ED_STAGE,
                                        blk_geom,
                                        blk_ptr->av1xd,
                                        plane ? blk_geom->bwidth_uv : blk_geom->bwidth,
                                        plane ? blk_geom->bheight_uv : blk_geom->bheight,
                                        plane ? tx_size_Chroma : tx_size,
                                        interintra_to_intra_mode[interintra_mode],
                                        0,
                                        0,
                                        NULL,
                                        FILTER_INTRA_MODES,
                                        topNeighArray + 1,
                                        leftNeighArray + 1,
                                        &intra_pred_desc,
                                        0,
                                        0,
                                        plane,
                                        blk_geom->bsize,
                                        dst_origin_x,
                                        dst_origin_y,
                                        pu_origin_x,
                                        pu_origin_y,
                                        0,
                                        0,
                                        &pcs->scs->seq_header);

            svt_aom_combine_interintra(interintra_mode,
                                       use_wedge_interintra,
                                       interintra_wedge_index,
                                       INTERINTRA_WEDGE_SIGN,
                                       blk_geom->bsize,
                                       plane_bsize,
                                       dst,
                                       dst_stride,
                                       dst, // Inter pred buff
                                       dst_stride, // Inter pred stride
                                       intra_pred, // Intra pred buff
                                       intra_stride); // Intra pred stride
        }
    }
}

EbErrorType svt_aom_warped_motion_prediction(PictureControlSet *pcs, MvUnit *mv_unit, uint8_t ref_frame_type,
                                             uint8_t compound_idx, InterInterCompoundData *interinter_comp,
                                             uint16_t pu_origin_x, uint16_t pu_origin_y, BlkStruct *blk_ptr,
                                             const BlockGeom *blk_geom, EbPictureBufferDesc *ref_pic_list0,
                                             EbPictureBufferDesc *ref_pic_list1, EbPictureBufferDesc *prediction_ptr,
                                             uint16_t dst_origin_x, uint16_t dst_origin_y,
                                             NeighborArrayUnit *recon_neigh_y, NeighborArrayUnit *recon_neigh_cb,
                                             NeighborArrayUnit *recon_neigh_cr, ModeDecisionCandidate *cand,

                                             EbWarpedMotionParams *wm_params_l0, EbWarpedMotionParams *wm_params_l1,
                                             uint8_t bit_depth, uint32_t component_mask, Bool is_encode_pass) {
    EbErrorType         return_error      = EB_ErrorNone;
    uint8_t             is_compound       = (mv_unit->pred_direction == BI_PRED) ? 1 : 0;
    SequenceControlSet *scs               = pcs->scs;
    Bool                is_16bit_pipeline = scs->is_16bit_pipeline;
    Bool                is16bit           = (Bool)(bit_depth > EB_EIGHT_BIT) || (is_encode_pass && is_16bit_pipeline);

    int32_t  src_stride;
    int32_t  dst_stride;
    uint16_t buf_width;
    uint16_t buf_height;

    MvReferenceFrame rf[2];
    av1_set_ref_frame(rf, ref_frame_type);

    uint8_t *src_ptr_l0, *src_ptr_l1;
    uint8_t *src_ptr_2b_l0 = NULL;
    uint8_t *src_ptr_2b_l1 = NULL;
    uint8_t *dst_ptr;
    if (component_mask & PICTURE_BUFFER_DESC_LUMA_MASK) {
        if (mv_unit->pred_direction == UNI_PRED_LIST_0 || mv_unit->pred_direction == BI_PRED) {
            assert(ref_pic_list0 != NULL);
            if (is_compound)
                assert(ref_pic_list1 != NULL);
            // Y

            src_ptr_l0 = ref_pic_list0->buffer_y +
                (ref_pic_list0->org_x + ref_pic_list0->org_y * ref_pic_list0->stride_y);
            src_ptr_l1 = is_compound
                ? ref_pic_list1->buffer_y + (ref_pic_list1->org_x + ref_pic_list1->org_y * ref_pic_list1->stride_y)
                : NULL;

            src_ptr_2b_l0 = ref_pic_list0->buffer_bit_inc_y +
                (ref_pic_list0->org_x + ref_pic_list0->org_y * ref_pic_list0->stride_bit_inc_y);
            src_ptr_2b_l1 = is_compound ? ref_pic_list1->buffer_bit_inc_y +
                    (ref_pic_list1->org_x + ref_pic_list1->org_y * ref_pic_list1->stride_bit_inc_y)
                                        : NULL;
            src_stride    = ref_pic_list0->stride_y;
            buf_width     = ref_pic_list0->width;
            buf_height    = ref_pic_list0->height;
        } else { //UNI_PRED_LIST_1
            assert(ref_pic_list1 != NULL);
            // Y

            src_ptr_l0 = ref_pic_list1->buffer_y +
                (ref_pic_list1->org_x + ref_pic_list1->org_y * ref_pic_list1->stride_y);

            src_ptr_2b_l0 = ref_pic_list1->buffer_bit_inc_y +
                (ref_pic_list1->org_x + ref_pic_list1->org_y * ref_pic_list1->stride_bit_inc_y);
            src_ptr_2b_l1 = NULL;

            src_ptr_l1 = NULL;
            src_stride = ref_pic_list1->stride_y;
            buf_width  = ref_pic_list1->width;
            buf_height = ref_pic_list1->height;
        }
        dst_ptr = prediction_ptr->buffer_y +
            (is16bit ? 2 : 1) *
                (prediction_ptr->org_x + dst_origin_x +
                 (prediction_ptr->org_y + dst_origin_y) * prediction_ptr->stride_y);
        dst_stride = prediction_ptr->stride_y;

        // Warp plane
        plane_warped_motion_prediction(pcs,
                                       compound_idx,
                                       interinter_comp,
                                       pu_origin_x,
                                       pu_origin_y,
                                       blk_geom,
                                       blk_geom->bwidth,
                                       blk_geom->bheight,
                                       wm_params_l0,
                                       wm_params_l1,
                                       is_compound,
                                       bit_depth,
                                       src_stride,
                                       dst_stride,
                                       buf_width,
                                       buf_height,
                                       0,
                                       0,
                                       src_ptr_l0,
                                       src_ptr_l1,
                                       src_ptr_2b_l0,
                                       src_ptr_2b_l1,
                                       dst_ptr,
                                       0, // plane
                                       rf,
                                       is16bit);
    }

    if (blk_geom->has_uv && (component_mask & PICTURE_BUFFER_DESC_CHROMA_MASK)) {
        if (blk_geom->bwidth >= 16 && blk_geom->bheight >= 16) {
            const uint8_t ss_x = 1; // subsamplings
            const uint8_t ss_y = 1;
            // Cb
            if (mv_unit->pred_direction == UNI_PRED_LIST_0 || mv_unit->pred_direction == BI_PRED) {
                src_ptr_l0 = ref_pic_list0->buffer_cb +
                    (ref_pic_list0->org_x / 2 + (ref_pic_list0->org_y / 2) * ref_pic_list0->stride_cb);
                src_ptr_l1 = is_compound ? ref_pic_list1->buffer_cb +
                        (ref_pic_list1->org_x / 2 + (ref_pic_list1->org_y / 2) * ref_pic_list1->stride_cb)
                                         : NULL;

                src_ptr_2b_l0 = ref_pic_list0->buffer_bit_inc_cb +
                    (ref_pic_list0->org_x / 2 + (ref_pic_list0->org_y / 2) * ref_pic_list0->stride_bit_inc_cb);
                src_ptr_2b_l1 = is_compound ? ref_pic_list1->buffer_bit_inc_cb +
                        (ref_pic_list1->org_x / 2 + (ref_pic_list1->org_y / 2) * ref_pic_list1->stride_bit_inc_cb)
                                            : NULL;

                src_stride = ref_pic_list0->stride_cb;
                buf_width  = ref_pic_list0->width;
                buf_height = ref_pic_list0->height;
            } else { //UNI_PRED_LIST_1

                src_ptr_l0 = ref_pic_list1->buffer_cb +
                    (ref_pic_list1->org_x / 2 + (ref_pic_list1->org_y / 2) * ref_pic_list1->stride_cb);
                src_ptr_2b_l0 = ref_pic_list1->buffer_bit_inc_cb +
                    (ref_pic_list1->org_x / 2 + (ref_pic_list1->org_y / 2) * ref_pic_list1->stride_bit_inc_cb);
                src_ptr_2b_l1 = NULL;

                src_ptr_l1 = NULL;
                src_stride = ref_pic_list1->stride_cb;
                buf_width  = ref_pic_list1->width;
                buf_height = ref_pic_list1->height;
            }
            dst_ptr = prediction_ptr->buffer_cb +
                (is16bit ? 2 : 1) *
                    ((prediction_ptr->org_x + ((dst_origin_x >> 3) << 3)) / 2 +
                     (prediction_ptr->org_y + ((dst_origin_y >> 3) << 3)) / 2 * prediction_ptr->stride_cb);
            dst_stride = prediction_ptr->stride_cb;

            plane_warped_motion_prediction(pcs,
                                           compound_idx,
                                           interinter_comp,
                                           pu_origin_x >> ss_x,
                                           pu_origin_y >> ss_y,
                                           blk_geom,
                                           blk_geom->bwidth_uv,
                                           blk_geom->bheight_uv,
                                           wm_params_l0,
                                           wm_params_l1,
                                           is_compound,
                                           bit_depth,
                                           src_stride,
                                           dst_stride,
                                           buf_width >> ss_x,
                                           buf_height >> ss_y,
                                           ss_x,
                                           ss_y,
                                           src_ptr_l0,
                                           src_ptr_l1,
                                           src_ptr_2b_l0,
                                           src_ptr_2b_l1,
                                           dst_ptr,
                                           1, // plane
                                           rf,
                                           is16bit);

            // Cr
            if (mv_unit->pred_direction == UNI_PRED_LIST_0 || mv_unit->pred_direction == BI_PRED) {
                src_ptr_l0 = ref_pic_list0->buffer_cr +
                    (ref_pic_list0->org_x / 2 + (ref_pic_list0->org_y / 2) * ref_pic_list0->stride_cr);
                src_ptr_l1 = is_compound ? ref_pic_list1->buffer_cr +
                        (ref_pic_list1->org_x / 2 + (ref_pic_list1->org_y / 2) * ref_pic_list1->stride_cr)
                                         : NULL;

                src_ptr_2b_l0 = ref_pic_list0->buffer_bit_inc_cr +
                    (ref_pic_list0->org_x / 2 + (ref_pic_list0->org_y / 2) * ref_pic_list0->stride_bit_inc_cr);
                src_ptr_2b_l1 = is_compound ? ref_pic_list1->buffer_bit_inc_cr +
                        (ref_pic_list1->org_x / 2 + (ref_pic_list1->org_y / 2) * ref_pic_list1->stride_bit_inc_cr)
                                            : NULL;

                src_stride = ref_pic_list0->stride_cr;
                buf_width  = ref_pic_list0->width;
                buf_height = ref_pic_list0->height;
            } else { //UNI_PRED_LIST_1
                src_ptr_l0 = ref_pic_list1->buffer_cr +
                    (ref_pic_list1->org_x / 2 + (ref_pic_list1->org_y / 2) * ref_pic_list1->stride_cr);
                src_ptr_2b_l0 = ref_pic_list1->buffer_bit_inc_cr +
                    (ref_pic_list1->org_x / 2 + (ref_pic_list1->org_y / 2) * ref_pic_list1->stride_bit_inc_cr);
                src_ptr_2b_l1 = NULL;
                src_ptr_l1    = NULL;
                src_stride    = ref_pic_list1->stride_cr;
                buf_width     = ref_pic_list1->width;
                buf_height    = ref_pic_list1->height;
            }
            dst_ptr = prediction_ptr->buffer_cr +
                (is16bit ? 2 : 1) *
                    ((prediction_ptr->org_x + ((dst_origin_x >> 3) << 3)) / 2 +
                     (prediction_ptr->org_y + ((dst_origin_y >> 3) << 3)) / 2 * prediction_ptr->stride_cr);
            dst_stride = prediction_ptr->stride_cr;

            plane_warped_motion_prediction(pcs,
                                           compound_idx,
                                           interinter_comp,
                                           pu_origin_x >> ss_x,
                                           pu_origin_y >> ss_y,
                                           blk_geom,
                                           blk_geom->bwidth_uv,
                                           blk_geom->bheight_uv,
                                           wm_params_l0,
                                           wm_params_l1,
                                           is_compound,
                                           bit_depth,
                                           src_stride,
                                           dst_stride,
                                           buf_width >> ss_x,
                                           buf_height >> ss_y,
                                           ss_x,
                                           ss_y,
                                           src_ptr_l0,
                                           src_ptr_l1,
                                           src_ptr_2b_l0,
                                           src_ptr_2b_l1,
                                           dst_ptr,
                                           2, // plane
                                           rf,
                                           is16bit);

        } else { // Translation prediction when chroma block is smaller than 8x8

            // Cb
            if (mv_unit->pred_direction == UNI_PRED_LIST_0 || mv_unit->pred_direction == BI_PRED) {
                src_ptr_l0 = ref_pic_list0->buffer_cb +
                    ((ref_pic_list0->org_x + ((pu_origin_x >> 3) << 3)) / 2 +
                     (ref_pic_list0->org_y + ((pu_origin_y >> 3) << 3)) / 2 * ref_pic_list0->stride_cb);
                src_ptr_l1 = is_compound ? ref_pic_list1->buffer_cb +
                        ((ref_pic_list1->org_x + ((pu_origin_x >> 3) << 3)) / 2 +
                         (ref_pic_list1->org_y + ((pu_origin_y >> 3) << 3)) / 2 * ref_pic_list1->stride_cb)
                                         : NULL;

                src_ptr_2b_l0 = ref_pic_list0->buffer_bit_inc_cb +
                    ((ref_pic_list0->org_x + ((pu_origin_x >> 3) << 3)) / 2 +
                     (ref_pic_list0->org_y + ((pu_origin_y >> 3) << 3)) / 2 * ref_pic_list0->stride_bit_inc_cb);
                src_ptr_2b_l1 = is_compound ? ref_pic_list1->buffer_bit_inc_cb +
                        ((ref_pic_list1->org_x + ((pu_origin_x >> 3) << 3)) / 2 +
                         (ref_pic_list1->org_y + ((pu_origin_y >> 3) << 3)) / 2 * ref_pic_list1->stride_bit_inc_cb)
                                            : NULL;
                src_stride    = ref_pic_list0->stride_cb;
            } else { //UNI_PRED_LIST_1
                src_ptr_l0 = ref_pic_list1->buffer_cb +
                    ((ref_pic_list1->org_x + ((pu_origin_x >> 3) << 3)) / 2 +
                     (ref_pic_list1->org_y + ((pu_origin_y >> 3) << 3)) / 2 * ref_pic_list1->stride_cb);
                src_ptr_l1    = NULL;
                src_ptr_2b_l0 = ref_pic_list1->buffer_bit_inc_cb +
                    ((ref_pic_list1->org_x + ((pu_origin_x >> 3) << 3)) / 2 +
                     (ref_pic_list1->org_y + ((pu_origin_y >> 3) << 3)) / 2 * ref_pic_list1->stride_bit_inc_cb);
                src_ptr_2b_l1 = NULL;
                src_stride    = ref_pic_list1->stride_cb;
            }
            dst_ptr = prediction_ptr->buffer_cb +
                (is16bit ? 2 : 1) *
                    ((prediction_ptr->org_x + ((dst_origin_x >> 3) << 3)) / 2 +
                     (prediction_ptr->org_y + ((dst_origin_y >> 3) << 3)) / 2 * prediction_ptr->stride_cb);
            dst_stride = prediction_ptr->stride_cb;

            chroma_plane_warped_motion_prediction_sub8x8(pcs,
                                                         compound_idx,
                                                         blk_ptr,
                                                         blk_geom,
                                                         blk_geom->bwidth_uv,
                                                         blk_geom->bheight_uv,
                                                         is_compound,
                                                         bit_depth,
                                                         src_stride,
                                                         dst_stride,
                                                         src_ptr_l0,
                                                         src_ptr_l1,
                                                         src_ptr_2b_l0,
                                                         src_ptr_2b_l1,
                                                         dst_ptr,
                                                         rf,
                                                         mv_unit,
                                                         is16bit);

            // Cr
            if (mv_unit->pred_direction == UNI_PRED_LIST_0 || mv_unit->pred_direction == BI_PRED) {
                src_ptr_l0 = ref_pic_list0->buffer_cr +
                    ((ref_pic_list0->org_x + ((pu_origin_x >> 3) << 3)) / 2 +
                     (ref_pic_list0->org_y + ((pu_origin_y >> 3) << 3)) / 2 * ref_pic_list0->stride_cr);
                src_ptr_l1    = is_compound ? ref_pic_list1->buffer_cr +
                        ((ref_pic_list1->org_x + ((pu_origin_x >> 3) << 3)) / 2 +
                         (ref_pic_list1->org_y + ((pu_origin_y >> 3) << 3)) / 2 * ref_pic_list1->stride_cr)
                                            : NULL;
                src_ptr_2b_l0 = ref_pic_list0->buffer_bit_inc_cr +
                    ((ref_pic_list0->org_x + ((pu_origin_x >> 3) << 3)) / 2 +
                     (ref_pic_list0->org_y + ((pu_origin_y >> 3) << 3)) / 2 * ref_pic_list0->stride_bit_inc_cr);
                src_ptr_2b_l1 = is_compound ? ref_pic_list1->buffer_bit_inc_cr +
                        ((ref_pic_list1->org_x + ((pu_origin_x >> 3) << 3)) / 2 +
                         (ref_pic_list1->org_y + ((pu_origin_y >> 3) << 3)) / 2 * ref_pic_list1->stride_bit_inc_cr)
                                            : NULL;
                src_stride    = ref_pic_list0->stride_cr;
            } else { //UNI_PRED_LIST_1
                src_ptr_l0 = ref_pic_list1->buffer_cr +
                    ((ref_pic_list1->org_x + ((pu_origin_x >> 3) << 3)) / 2 +
                     (ref_pic_list1->org_y + ((pu_origin_y >> 3) << 3)) / 2 * ref_pic_list1->stride_cr);
                src_ptr_2b_l0 = ref_pic_list1->buffer_bit_inc_cr +
                    ((ref_pic_list1->org_x + ((pu_origin_x >> 3) << 3)) / 2 +
                     (ref_pic_list1->org_y + ((pu_origin_y >> 3) << 3)) / 2 * ref_pic_list1->stride_bit_inc_cr);
                src_ptr_2b_l1 = NULL;
                src_ptr_l1    = NULL;
                src_stride    = ref_pic_list1->stride_cr;
            }
            dst_ptr = prediction_ptr->buffer_cr +
                (is16bit ? 2 : 1) *
                    ((prediction_ptr->org_x + ((dst_origin_x >> 3) << 3)) / 2 +
                     (prediction_ptr->org_y + ((dst_origin_y >> 3) << 3)) / 2 * prediction_ptr->stride_cb);
            dst_stride = prediction_ptr->stride_cr;

            chroma_plane_warped_motion_prediction_sub8x8(pcs,
                                                         compound_idx,
                                                         blk_ptr,
                                                         blk_geom,
                                                         blk_geom->bwidth_uv,
                                                         blk_geom->bheight_uv,
                                                         is_compound,
                                                         bit_depth,
                                                         src_stride,
                                                         dst_stride,
                                                         src_ptr_l0,
                                                         src_ptr_l1,
                                                         src_ptr_2b_l0,
                                                         src_ptr_2b_l1,
                                                         dst_ptr,
                                                         rf,
                                                         mv_unit,
                                                         is16bit);
        }
    }

    uint8_t        is_interintra_used;
    InterIntraMode interintra_mode;
    uint8_t        use_wedge_interintra;
    int32_t        interintra_wedge_index;
    if (is_encode_pass) {
        is_interintra_used     = blk_ptr->is_interintra_used;
        interintra_mode        = blk_ptr->interintra_mode;
        use_wedge_interintra   = blk_ptr->use_wedge_interintra;
        interintra_wedge_index = blk_ptr->interintra_wedge_index;
    } else {
        is_interintra_used     = cand->is_interintra_used;
        interintra_mode        = cand->interintra_mode;
        use_wedge_interintra   = cand->use_wedge_interintra;
        interintra_wedge_index = cand->interintra_wedge_index;
    }

    if (is_interintra_used) {
        inter_intra_prediction(pcs,
                               prediction_ptr,
                               interintra_mode,
                               use_wedge_interintra,
                               interintra_wedge_index,
                               recon_neigh_y,
                               recon_neigh_cb,
                               recon_neigh_cr,
                               blk_ptr,
                               blk_geom,
                               pu_origin_x,
                               pu_origin_y,
                               dst_origin_x,
                               dst_origin_y,
                               component_mask,
                               bit_depth,
                               is16bit);
    }

    return return_error;
}

static void compute_subpel_params(SequenceControlSet *scs, int16_t pre_y, int16_t pre_x, MV mv,
                                  const struct ScaleFactors *const sf, uint16_t frame_width, uint16_t frame_height,
                                  uint8_t blk_width, uint8_t blk_height, MacroBlockD *av1xd, const uint32_t ss_y,
                                  const uint32_t ss_x, SubpelParams *subpel_params, int32_t *pos_y, int32_t *pos_x) {
    const int32_t is_scaled = av1_is_scaled(sf);

    if (is_scaled) {
        int orig_pos_y = (pre_y + 0) << SUBPEL_BITS;
        orig_pos_y += mv.row * (1 << (1 - ss_y));
        int orig_pos_x = (pre_x + 0) << SUBPEL_BITS;
        orig_pos_x += mv.col * (1 << (1 - ss_x));
        *pos_y = sf->scale_value_y(orig_pos_y, sf);
        *pos_x = sf->scale_value_x(orig_pos_x, sf);
        *pos_x += SCALE_EXTRA_OFF;
        *pos_y += SCALE_EXTRA_OFF;

        // Note 1: Equations of top and left are expanded from macro -AOM_LEFT_TOP_MARGIN_SCALED(ss_y),
        //      except recon ref padding in svt is 'scs->static_config.super_block_size * 2 + 32' when SR or resize is on instead of 288.
        //      since 'is_scaled' is set, 'border_in_pixels' should be constant value of 2*sb_size+32
        //
        // Note 2: for issue 1835 "Segmentation Fault With Super Resolution on Ubuntu 18.04":
        //      Limit top & left offset from AOM_INTERP_EXTEND(4) to INTERPOLATION_OFFSET(8) to avoid memory access underflow,
        //      because svt_aom_pack_block() may access src_mod - INTERPOLATION_OFFSET - (INTERPOLATION_OFFSET * src_stride) later.
        const int border_in_pixels = scs->super_block_size * 2 + 32;
        const int top              = -(((border_in_pixels >> ss_y) - INTERPOLATION_OFFSET) << SCALE_SUBPEL_BITS);
        const int left             = -(((border_in_pixels >> ss_x) - INTERPOLATION_OFFSET) << SCALE_SUBPEL_BITS);
        const int bottom           = ((frame_height >> ss_y) + AOM_INTERP_EXTEND) << SCALE_SUBPEL_BITS;
        const int right            = ((frame_width >> ss_x) + AOM_INTERP_EXTEND) << SCALE_SUBPEL_BITS;

        *pos_y = clamp(*pos_y, top, bottom);
        *pos_x = clamp(*pos_x, left, right);

        subpel_params->subpel_x = *pos_x & SCALE_SUBPEL_MASK;
        subpel_params->subpel_y = *pos_y & SCALE_SUBPEL_MASK;
        subpel_params->xs       = sf->x_step_q4;
        subpel_params->ys       = sf->y_step_q4;

        *pos_y = *pos_y >> SCALE_SUBPEL_BITS;
        *pos_x = *pos_x >> SCALE_SUBPEL_BITS;

    } else {
        MV mv_q4;

        mv_q4 = clamp_mv_to_umv_border_sb(av1xd, &mv, blk_width, blk_height, ss_x, ss_y);

        subpel_params->subpel_x = (mv_q4.col & SUBPEL_MASK) << SCALE_EXTRA_BITS;
        subpel_params->subpel_y = (mv_q4.row & SUBPEL_MASK) << SCALE_EXTRA_BITS;
        subpel_params->xs       = SCALE_SUBPEL_SHIFTS;
        subpel_params->ys       = SCALE_SUBPEL_SHIFTS;
        *pos_y                  = pre_y + (mv_q4.row >> SUBPEL_BITS);
        *pos_x                  = pre_x + (mv_q4.col >> SUBPEL_BITS);
    }
}
void tf_inter_predictor(SequenceControlSet *scs, uint8_t *src_ptr, uint8_t *dst_ptr, int16_t pre_y, int16_t pre_x,
                        MV mv, const struct ScaleFactors *const sf, ConvolveParams *conv_params,
                        InterpFilters interp_filters, uint16_t frame_width, uint16_t frame_height, uint8_t blk_width,
                        uint8_t blk_height, MacroBlockD *av1xd, int32_t src_stride, int32_t dst_stride,
                        uint8_t bit_depth, uint8_t subsamling_shift) {
    SubpelParams subpel_params;
    int32_t      pos_y, pos_x;
    uint8_t      is_highbd = (uint8_t)(bit_depth > EB_EIGHT_BIT);

    compute_subpel_params(scs,
                          pre_y,
                          pre_x,
                          mv,
                          sf,
                          frame_width,
                          frame_height,
                          blk_width,
                          blk_height,
                          av1xd,
                          0, //ss_y,
                          0, //ss_x,
                          &subpel_params,
                          &pos_y,
                          &pos_x);

    uint8_t *src_mod;
    src_mod = src_ptr + ((pos_x + (pos_y * src_stride)) << is_highbd);

    if (is_highbd) {
        uint16_t *src16 = (uint16_t *)src_mod;
        svt_highbd_inter_predictor(src16,
                                   src_stride << subsamling_shift,
                                   (uint16_t *)dst_ptr,
                                   dst_stride << subsamling_shift,
                                   &subpel_params,
                                   sf,
                                   blk_width,
                                   blk_height >> subsamling_shift,
                                   conv_params,
                                   interp_filters,
                                   0, //use_intrabc,
                                   bit_depth);
    } else {
        svt_inter_predictor(src_mod,
                            src_stride << subsamling_shift,
                            dst_ptr,
                            dst_stride << subsamling_shift,
                            &subpel_params,
                            sf,
                            blk_width,
                            blk_height >> subsamling_shift,
                            conv_params,
                            interp_filters,
                            0); // use_intrabc);
    }
}
static void enc_make_inter_predictor_light_pd0(uint8_t *src, uint8_t *dst, SubpelParams *subpel_params,
                                               ConvolveParams *conv_params, uint8_t blk_width, uint8_t blk_height,
                                               int32_t src_stride, int32_t dst_stride) {
    svt_inter_predictor_light_pd0(src, src_stride, dst, dst_stride, blk_width, blk_height, subpel_params, conv_params);
}
void svt_aom_enc_make_inter_predictor(SequenceControlSet *scs, uint8_t *src_ptr, uint8_t *src_ptr_2b, uint8_t *dst_ptr,
                                      int16_t pre_y, int16_t pre_x, MV mv, const struct ScaleFactors *const sf,
                                      ConvolveParams *conv_params, InterpFilters interp_filters,
                                      InterInterCompoundData *interinter_comp, uint8_t *seg_mask, uint16_t frame_width,
                                      uint16_t frame_height, uint8_t blk_width, uint8_t blk_height, BlockSize bsize,
                                      MacroBlockD *av1xd, int32_t src_stride, int32_t dst_stride, uint8_t plane,
                                      const uint32_t ss_y, const uint32_t ss_x, uint8_t bit_depth, uint8_t use_intrabc,
                                      uint8_t is_masked_compound, uint8_t is16bit) {
    SubpelParams subpel_params;
    int32_t      pos_y, pos_x;

    compute_subpel_params(scs,
                          pre_y,
                          pre_x,
                          mv,
                          sf,
                          frame_width,
                          frame_height,
                          blk_width,
                          blk_height,
                          av1xd,
                          ss_y,
                          ss_x,
                          &subpel_params,
                          &pos_y,
                          &pos_x);

    uint8_t *src_mod;
    uint8_t *src_mod_2b = NULL;
    if (src_ptr_2b) {
        src_mod    = src_ptr + ((pos_x + (pos_y * src_stride)));
        src_mod_2b = src_ptr_2b + ((pos_x + (pos_y * src_stride)));
    } else {
        src_mod = src_ptr + ((pos_x + (pos_y * src_stride)) << is16bit);
    }
    if (is_masked_compound) {
        conv_params->do_average = 0;
        av1_make_masked_scaled_inter_predictor(src_mod,
                                               src_mod_2b,
                                               src_stride,
                                               dst_ptr,
                                               dst_stride,
                                               bsize,
                                               blk_width,
                                               blk_height,
                                               interp_filters,
                                               &subpel_params,
                                               sf,
                                               conv_params,
                                               interinter_comp,
                                               seg_mask,
                                               bit_depth,
                                               plane ? 1 : 0,
                                               use_intrabc,
                                               is16bit);
        return;
    }
    if (is16bit) {
        // for super-res, the reference frame block might be 2x than predictor in maximum
        // for reference scaling, it might be 4x since both width and height is scaled 2x
        // should pack enough buffer for scaled reference
        DECLARE_ALIGNED(16, uint16_t, src16[PACKED_BUFFER_SIZE * 4]);
        uint16_t *src16_ptr = src16;
        int32_t   src_stride16;
        if (src_ptr_2b) {
            // pack the reference into temp 16bit buffer
            uint8_t  offset       = INTERPOLATION_OFFSET;
            uint32_t width_scale  = 1;
            uint32_t height_scale = 1;
            if (av1_is_scaled(sf)) {
                width_scale  = sf->x_scale_fp != REF_NO_SCALE ? 2 : 1;
                height_scale = sf->y_scale_fp != REF_NO_SCALE ? 2 : 1;
            }
            // optimize stride from MAX_SB_SIZE to blk_width to minimum the block buffer size
            src_stride16 = blk_width * width_scale + (offset << 1);
            // 16-byte align of src16
            if (src_stride16 % 8)
                src_stride16 = ALIGN_POWER_OF_TWO(src_stride16, 3);

            svt_aom_pack_block(src_mod - offset - (offset * src_stride),
                               src_stride,
                               src_mod_2b - offset - (offset * src_stride),
                               src_stride,
                               src16,
                               src_stride16,
                               blk_width * width_scale + (offset << 1),
                               blk_height * height_scale + (offset << 1));
            src16_ptr = src16 + offset + (offset * src_stride16);
        } else {
            src16_ptr    = (uint16_t *)src_mod;
            src_stride16 = src_stride;
        }
        svt_highbd_inter_predictor(src16_ptr, //(src_ptr_2b)? (uint16_t*)src16+ 8 + 8*128 : src16,
                                   src_stride16,
                                   (uint16_t *)dst_ptr,
                                   dst_stride,
                                   &subpel_params,
                                   sf,
                                   blk_width,
                                   blk_height,
                                   conv_params,
                                   interp_filters,
                                   use_intrabc,
                                   bit_depth);
    } else {
        svt_inter_predictor(src_mod,
                            src_stride,
                            dst_ptr,
                            dst_stride,
                            &subpel_params,
                            sf,
                            blk_width,
                            blk_height,
                            conv_params,
                            interp_filters,
                            use_intrabc);
    }
}

int32_t     is_inter_block(const BlockModeInfoEnc *mbmi);
EbErrorType svt_aom_simple_luma_unipred(SequenceControlSet *scs, ScaleFactors sf_identity, uint32_t interp_filters,
                                        BlkStruct *blk_ptr, uint8_t ref_frame_type, MvUnit *mv_unit,
                                        uint16_t pu_origin_x, uint16_t pu_origin_y, uint8_t bwidth, uint8_t bheight,
                                        EbPictureBufferDesc *ref_pic_list0, EbPictureBufferDesc *prediction_ptr,
                                        uint16_t dst_origin_x, uint16_t dst_origin_y, uint8_t bit_depth,
                                        uint8_t subsampling_shift) {
    uint8_t     is16bit      = bit_depth > EB_EIGHT_BIT;
    EbErrorType return_error = EB_ErrorNone;
    DECLARE_ALIGNED(32, uint16_t, tmp_dstY[128 * 128]); //move this to context if stack does not hold.

    uint8_t is_compound = 0;
    MV      mv;

    uint8_t           *src_ptr;
    uint8_t           *dst_ptr;
    ConvolveParams     conv_params;
    InterpFilterParams filter_params_x, filter_params_y;
    MvReferenceFrame   rf[2];
    av1_set_ref_frame(rf, ref_frame_type);
    // List0-Y
    mv.col = mv_unit->mv[REF_LIST_0].x;
    mv.row = mv_unit->mv[REF_LIST_0].y;
    assert(ref_pic_list0 != NULL);

    src_ptr = ref_pic_list0->buffer_y +
        ((ref_pic_list0->org_x + (ref_pic_list0->org_y) * ref_pic_list0->stride_y) << is16bit);
    dst_ptr = prediction_ptr->buffer_y +
        ((prediction_ptr->org_x + dst_origin_x + (prediction_ptr->org_y + dst_origin_y) * prediction_ptr->stride_y)
         << is16bit);

    int32_t dst_stride = prediction_ptr->stride_y;
    /*ScaleFactor*/
    const struct ScaleFactors *const sf = &sf_identity;
    conv_params                         = get_conv_params_no_round(0, 0, 0, tmp_dstY, 128, is_compound, bit_depth);
    av1_get_convolve_filter_params(interp_filters, &filter_params_x, &filter_params_y, bwidth, bheight);

    tf_inter_predictor(scs,
                       src_ptr,
                       dst_ptr,
                       (int16_t)pu_origin_y,
                       (int16_t)pu_origin_x,
                       mv,
                       sf,
                       &conv_params,
                       interp_filters,
                       ref_pic_list0->width,
                       ref_pic_list0->height,
                       bwidth,
                       bheight,
                       blk_ptr->av1xd,
                       ref_pic_list0->stride_y,
                       dst_stride,
                       bit_depth,
                       subsampling_shift);
    return return_error;
}
static void av1_inter_prediction_light_pd0(SequenceControlSet *scs, MvUnit *mv_unit, struct ModeDecisionContext *ctx,
                                           uint16_t pu_origin_x, uint16_t pu_origin_y, uint8_t bwidth, uint8_t bheight,
                                           EbPictureBufferDesc *ref_pic_list0, EbPictureBufferDesc *ref_pic_list1,
                                           EbPictureBufferDesc *pred, uint16_t dst_origin_x, uint16_t dst_origin_y,
                                           ScaleFactors *sf0, ScaleFactors *sf1) {
    const uint8_t is_compound = (mv_unit->pred_direction == BI_PRED) ? 1 : 0;
    DECLARE_ALIGNED(32, uint16_t, tmp_dstY[64 * 64]);
    uint8_t *src_ptr;
    uint8_t *dst_ptr = pred->buffer_y + ((pred->org_x + dst_origin_x + (pred->org_y + dst_origin_y) * pred->stride_y));
    int32_t  dst_stride        = pred->stride_y;
    ConvolveParams conv_params = get_conv_params_no_round(0, 0, 0, tmp_dstY, 64, is_compound, EB_EIGHT_BIT);

    if (mv_unit->pred_direction == UNI_PRED_LIST_0 || mv_unit->pred_direction == BI_PRED) {
        SubpelParams subpel_params = {SCALE_SUBPEL_SHIFTS, SCALE_SUBPEL_SHIFTS, 0, 0};
        int32_t      pos_y, pos_x;
        pos_x = pu_origin_x + (mv_unit->mv[REF_LIST_0].x >> 3);
        pos_y = pu_origin_y + (mv_unit->mv[REF_LIST_0].y >> 3);
        assert(ref_pic_list0 != NULL);
        if (EB_UNLIKELY(av1_is_scaled(sf0))) {
            MV mv;
            mv.col = mv_unit->mv[REF_LIST_0].x;
            mv.row = mv_unit->mv[REF_LIST_0].y;
            compute_subpel_params(scs,
                                  pu_origin_y,
                                  pu_origin_x,
                                  mv,
                                  sf0,
                                  ref_pic_list0->width,
                                  ref_pic_list0->height,
                                  bwidth,
                                  bheight,
                                  ctx->blk_ptr->av1xd,
                                  0,
                                  0,
                                  &subpel_params,
                                  &pos_y,
                                  &pos_x);
        }

        src_ptr = ref_pic_list0->buffer_y +
            ((ref_pic_list0->org_x + pos_x + (ref_pic_list0->org_y + pos_y) * ref_pic_list0->stride_y));

        enc_make_inter_predictor_light_pd0(
            src_ptr, dst_ptr, &subpel_params, &conv_params, bwidth, bheight, ref_pic_list0->stride_y, dst_stride);
    }

    if (mv_unit->pred_direction == UNI_PRED_LIST_1 || mv_unit->pred_direction == BI_PRED) {
        SubpelParams subpel_params = {SCALE_SUBPEL_SHIFTS, SCALE_SUBPEL_SHIFTS, 0, 0};
        int32_t      pos_y, pos_x;
        pos_x = pu_origin_x + (mv_unit->mv[REF_LIST_1].x >> 3);
        pos_y = pu_origin_y + (mv_unit->mv[REF_LIST_1].y >> 3);
        assert(ref_pic_list1 != NULL);
        if (EB_UNLIKELY(av1_is_scaled(sf1))) {
            MV mv;
            mv.col = mv_unit->mv[REF_LIST_1].x;
            mv.row = mv_unit->mv[REF_LIST_1].y;
            compute_subpel_params(scs,
                                  pu_origin_y,
                                  pu_origin_x,
                                  mv,
                                  sf1,
                                  ref_pic_list1->width,
                                  ref_pic_list1->height,
                                  bwidth,
                                  bheight,
                                  ctx->blk_ptr->av1xd,
                                  0,
                                  0,
                                  &subpel_params,
                                  &pos_y,
                                  &pos_x);
        }
        src_ptr = ref_pic_list1->buffer_y +
            ((ref_pic_list1->org_x + pos_x + (ref_pic_list1->org_y + pos_y) * ref_pic_list1->stride_y));

        if (is_compound) {
            conv_params.do_average            = 1;
            conv_params.use_dist_wtd_comp_avg = 0;
        }

        assert(IMPLIES(conv_params.do_average, is_compound));
        enc_make_inter_predictor_light_pd0(
            src_ptr, dst_ptr, &subpel_params, &conv_params, bwidth, bheight, ref_pic_list1->stride_y, dst_stride);
    }
}
/*
  inter prediction for light PD1
*/
void av1_inter_prediction_light_pd1(SequenceControlSet *scs, MvUnit *mv_unit, uint32_t interp_filter,
                                    struct ModeDecisionContext *ctx, uint16_t pu_origin_x, uint16_t pu_origin_y,
                                    uint8_t bwidth, uint8_t bheight, EbPictureBufferDesc *ref_pic_list0,
                                    EbPictureBufferDesc *ref_pic_list1, EbPictureBufferDesc *pred_pic,
                                    uint16_t dst_origin_x, uint16_t dst_origin_y, uint32_t component_mask,
                                    uint8_t hbd_md, ScaleFactors *sf0, ScaleFactors *sf1) {
    int32_t bit_depth   = hbd_md ? EB_TEN_BIT : EB_EIGHT_BIT;
    uint8_t is_16bit    = hbd_md ? 1 : 0;
    uint8_t is_compound = (mv_unit->pred_direction == BI_PRED) ? 1 : 0;
    DECLARE_ALIGNED(32, uint16_t, tmp_dst_y[64 * 64]);
    MV               mv;
    const BlockGeom *blk_geom = ctx->blk_geom;
    uint8_t         *src_mod;
    uint8_t         *src_mod_2b;

    // Luma prediction
    if (component_mask & PICTURE_BUFFER_DESC_LUMA_MASK) {
        ConvolveParams conv_params_y = get_conv_params_no_round(0, 0, 0, tmp_dst_y, 64, is_compound, bit_depth);
        // 4xN/Nx4 blocks have a different filter params list, but that is not relevant for LPD1 path, which
        // assumes there are no 4xN/Nx4 blocks. Assume dual_filter is disabled for LPD1.
        InterpFilterParams filter_params = av1_interp_filter_params_list[av1_extract_interp_filter(interp_filter, 0)];
        uint8_t           *dst_ptr_y     = pred_pic->buffer_y +
            ((pred_pic->org_x + dst_origin_x + (pred_pic->org_y + dst_origin_y) * pred_pic->stride_y) << is_16bit);

        if (mv_unit->pred_direction == UNI_PRED_LIST_0 || mv_unit->pred_direction == BI_PRED) {
            assert(ref_pic_list0 != NULL);
            // List0-Y
            mv.col = mv_unit->mv[REF_LIST_0].x;
            mv.row = mv_unit->mv[REF_LIST_0].y;

            SubpelParams subpel_params;
            int32_t      pos_y, pos_x;
            compute_subpel_params(scs,
                                  pu_origin_y,
                                  pu_origin_x,
                                  mv,
                                  sf0,
                                  ref_pic_list0->width,
                                  ref_pic_list0->height,
                                  bwidth,
                                  bheight,
                                  ctx->blk_ptr->av1xd,
                                  0,
                                  0,
                                  &subpel_params,
                                  &pos_y,
                                  &pos_x);

            src_mod = ref_pic_list0->buffer_y +
                ((ref_pic_list0->org_x + pos_x + (ref_pic_list0->org_y + pos_y) * ref_pic_list0->stride_y));
            src_mod_2b = ref_pic_list0->buffer_bit_inc_y +
                ((ref_pic_list0->org_x + pos_x + (ref_pic_list0->org_y + pos_y) * ref_pic_list0->stride_bit_inc_y));
            svt_inter_predictor_light_pd1(src_mod,
                                          src_mod_2b,
                                          ref_pic_list0->stride_y,
                                          dst_ptr_y,
                                          pred_pic->stride_y,
                                          bwidth,
                                          bheight,
                                          &filter_params,
                                          &filter_params,
                                          &subpel_params,
                                          &conv_params_y,
                                          bit_depth);
        }

        if (mv_unit->pred_direction == UNI_PRED_LIST_1 || mv_unit->pred_direction == BI_PRED) {
            assert(ref_pic_list1 != NULL);
            // List1-Y
            mv.col = mv_unit->mv[REF_LIST_1].x;
            mv.row = mv_unit->mv[REF_LIST_1].y;

            if (is_compound) {
                conv_params_y.do_average            = 1;
                conv_params_y.use_dist_wtd_comp_avg = 0;
            }

            SubpelParams subpel_params;
            int32_t      pos_y, pos_x;
            compute_subpel_params(scs,
                                  pu_origin_y,
                                  pu_origin_x,
                                  mv,
                                  sf1,
                                  ref_pic_list1->width,
                                  ref_pic_list1->height,
                                  bwidth,
                                  bheight,
                                  ctx->blk_ptr->av1xd,
                                  0,
                                  0,
                                  &subpel_params,
                                  &pos_y,
                                  &pos_x);
            src_mod = ref_pic_list1->buffer_y +
                ((ref_pic_list1->org_x + pos_x + (ref_pic_list1->org_y + pos_y) * ref_pic_list1->stride_y));
            src_mod_2b = ref_pic_list1->buffer_bit_inc_y +
                ((ref_pic_list1->org_x + pos_x + (ref_pic_list1->org_y + pos_y) * ref_pic_list1->stride_bit_inc_y));
            svt_inter_predictor_light_pd1(src_mod,
                                          src_mod_2b,
                                          ref_pic_list1->stride_y,
                                          dst_ptr_y,
                                          pred_pic->stride_y,
                                          bwidth,
                                          bheight,
                                          &filter_params,
                                          &filter_params,
                                          &subpel_params,
                                          &conv_params_y,
                                          bit_depth);
        }
    }

    // Chroma prediction
    if (component_mask & PICTURE_BUFFER_DESC_CHROMA_MASK) {
        uint16_t *tmp_dst_cb = tmp_dst_y;
        uint16_t *tmp_dst_cr = &tmp_dst_y[32 * 32];
        uint8_t  *dst_ptr_cb = pred_pic->buffer_cb +
            (((pred_pic->org_x + dst_origin_x) / 2 + (pred_pic->org_y + dst_origin_y) / 2 * pred_pic->stride_cb)
             << is_16bit);
        uint8_t *dst_ptr_cr = pred_pic->buffer_cr +
            (((pred_pic->org_x + dst_origin_x) / 2 + (pred_pic->org_y + dst_origin_y) / 2 * pred_pic->stride_cr)
             << is_16bit);
        ConvolveParams     conv_params_cb = get_conv_params_no_round(0, 0, 0, tmp_dst_cb, 32, is_compound, bit_depth);
        ConvolveParams     conv_params_cr = get_conv_params_no_round(0, 0, 0, tmp_dst_cr, 32, is_compound, bit_depth);
        const int16_t      pu_origin_y_chroma = pu_origin_y / 2;
        const int16_t      pu_origin_x_chroma = pu_origin_x / 2;
        InterpFilterParams filter_params_x, filter_params_y;
        av1_get_convolve_filter_params(
            interp_filter, &filter_params_x, &filter_params_y, blk_geom->bwidth_uv, blk_geom->bheight_uv);
        if (mv_unit->pred_direction == UNI_PRED_LIST_0 || mv_unit->pred_direction == BI_PRED) {
            assert(ref_pic_list0 != NULL);
            mv.col = mv_unit->mv[REF_LIST_0].x;
            mv.row = mv_unit->mv[REF_LIST_0].y;

            SubpelParams subpel_params;
            int32_t      pos_y, pos_x;
            compute_subpel_params(scs,
                                  pu_origin_y_chroma,
                                  pu_origin_x_chroma,
                                  mv,
                                  sf0,
                                  ref_pic_list0->width,
                                  ref_pic_list0->height,
                                  bwidth,
                                  bheight,
                                  ctx->blk_ptr->av1xd,
                                  1,
                                  1,
                                  &subpel_params,
                                  &pos_y,
                                  &pos_x);
            if (component_mask & PICTURE_BUFFER_DESC_Cb_FLAG) {
                src_mod = ref_pic_list0->buffer_cb +
                    ((ref_pic_list0->org_x / 2 + pos_x +
                      (ref_pic_list0->org_y / 2 + pos_y) * ref_pic_list0->stride_cb));
                src_mod_2b = ref_pic_list0->buffer_bit_inc_cb +
                    ((ref_pic_list0->org_x / 2 + pos_x +
                      (ref_pic_list0->org_y / 2 + pos_y) * ref_pic_list0->stride_bit_inc_cb));
                svt_inter_predictor_light_pd1(src_mod,
                                              src_mod_2b,
                                              ref_pic_list0->stride_cb,
                                              dst_ptr_cb,
                                              pred_pic->stride_cb,
                                              blk_geom->bwidth_uv,
                                              blk_geom->bheight_uv,
                                              &filter_params_x,
                                              &filter_params_y,
                                              &subpel_params,
                                              &conv_params_cb,
                                              bit_depth);
            }

            if (component_mask & PICTURE_BUFFER_DESC_Cr_FLAG) {
                src_mod = ref_pic_list0->buffer_cr +
                    ((ref_pic_list0->org_x / 2 + pos_x +
                      (ref_pic_list0->org_y / 2 + pos_y) * ref_pic_list0->stride_cr));
                src_mod_2b = ref_pic_list0->buffer_bit_inc_cr +
                    ((ref_pic_list0->org_x / 2 + pos_x +
                      (ref_pic_list0->org_y / 2 + pos_y) * ref_pic_list0->stride_bit_inc_cr));
                svt_inter_predictor_light_pd1(src_mod,
                                              src_mod_2b,
                                              ref_pic_list0->stride_cr,
                                              dst_ptr_cr,
                                              pred_pic->stride_cr,
                                              blk_geom->bwidth_uv,
                                              blk_geom->bheight_uv,
                                              &filter_params_x,
                                              &filter_params_y,
                                              &subpel_params,
                                              &conv_params_cr,
                                              bit_depth);
            }
        }

        if (mv_unit->pred_direction == UNI_PRED_LIST_1 || mv_unit->pred_direction == BI_PRED) {
            assert(ref_pic_list1 != NULL);

            mv.col = mv_unit->mv[REF_LIST_1].x;
            mv.row = mv_unit->mv[REF_LIST_1].y;

            if (is_compound) {
                conv_params_cb.do_average            = 1;
                conv_params_cr.do_average            = 1;
                conv_params_cb.use_dist_wtd_comp_avg = 0;
                conv_params_cr.use_dist_wtd_comp_avg = 0;
            }

            SubpelParams subpel_params;
            int32_t      pos_y, pos_x;
            compute_subpel_params(scs,
                                  pu_origin_y_chroma,
                                  pu_origin_x_chroma,
                                  mv,
                                  sf1,
                                  ref_pic_list1->width,
                                  ref_pic_list1->height,
                                  bwidth,
                                  bheight,
                                  ctx->blk_ptr->av1xd,
                                  1,
                                  1,
                                  &subpel_params,
                                  &pos_y,
                                  &pos_x);
            if (component_mask & PICTURE_BUFFER_DESC_Cb_FLAG) {
                src_mod = ref_pic_list1->buffer_cb +
                    ((ref_pic_list1->org_x / 2 + pos_x +
                      (ref_pic_list1->org_y / 2 + pos_y) * ref_pic_list1->stride_cb));
                src_mod_2b = ref_pic_list1->buffer_bit_inc_cb +
                    ((ref_pic_list1->org_x / 2 + pos_x +
                      (ref_pic_list1->org_y / 2 + pos_y) * ref_pic_list1->stride_bit_inc_cb));
                svt_inter_predictor_light_pd1(src_mod,
                                              src_mod_2b,
                                              ref_pic_list1->stride_cb,
                                              dst_ptr_cb,
                                              pred_pic->stride_cb,
                                              blk_geom->bwidth_uv,
                                              blk_geom->bheight_uv,
                                              &filter_params_x,
                                              &filter_params_y,
                                              &subpel_params,
                                              &conv_params_cb,
                                              bit_depth);
            }

            if (component_mask & PICTURE_BUFFER_DESC_Cr_FLAG) {
                src_mod = ref_pic_list1->buffer_cr +
                    ((ref_pic_list1->org_x / 2 + pos_x +
                      (ref_pic_list1->org_y / 2 + pos_y) * ref_pic_list1->stride_cr));
                src_mod_2b = ref_pic_list1->buffer_bit_inc_cr +
                    ((ref_pic_list1->org_x / 2 + pos_x +
                      (ref_pic_list1->org_y / 2 + pos_y) * ref_pic_list1->stride_bit_inc_cr));
                svt_inter_predictor_light_pd1(src_mod,
                                              src_mod_2b,
                                              ref_pic_list1->stride_cr,
                                              dst_ptr_cr,
                                              pred_pic->stride_cr,
                                              blk_geom->bwidth_uv,
                                              blk_geom->bheight_uv,
                                              &filter_params_x,
                                              &filter_params_y,
                                              &subpel_params,
                                              &conv_params_cr,
                                              bit_depth);
            }
        }
    }
}

static void av1_inter_prediction_obmc(PictureControlSet *pcs, BlkStruct *blk_ptr, uint8_t use_precomputed_obmc,
                                      struct ModeDecisionContext *ctx, uint16_t pu_origin_x, uint16_t pu_origin_y,
                                      EbPictureBufferDesc *pred_pic, uint16_t dst_origin_x, uint16_t dst_origin_y,
                                      uint32_t component_mask, uint8_t bit_depth, uint8_t is_16bit_pipeline) {
    uint8_t is16bit = bit_depth > EB_EIGHT_BIT || is_16bit_pipeline;

    const BlockGeom *blk_geom = get_blk_geom_mds(blk_ptr->mds_idx);

    // cppcheck-suppress unassignedVariable
    DECLARE_ALIGNED(16, uint8_t, obmc_buff_0[2 * MAX_MB_PLANE * MAX_SB_SQUARE]);
    // cppcheck-suppress unassignedVariable
    DECLARE_ALIGNED(16, uint8_t, obmc_buff_1[2 * MAX_MB_PLANE * MAX_SB_SQUARE]);
    uint8_t *dst_buf1[MAX_MB_PLANE], *dst_buf2[MAX_MB_PLANE];
    int      dst_stride1[MAX_MB_PLANE] = {blk_geom->bwidth, blk_geom->bwidth, blk_geom->bwidth};
    int      dst_stride2[MAX_MB_PLANE] = {blk_geom->bwidth, blk_geom->bwidth, blk_geom->bwidth};

    int mi_row = pu_origin_y >> 2;
    int mi_col = pu_origin_x >> 2;

    if (use_precomputed_obmc) {
        dst_buf1[0] = ctx->obmc_buff_0;
        dst_buf1[1] = ctx->obmc_buff_0 + ((blk_geom->bwidth * blk_geom->bheight) << is16bit);
        dst_buf1[2] = ctx->obmc_buff_0 + ((blk_geom->bwidth * blk_geom->bheight * 2) << is16bit);
        dst_buf2[0] = ctx->obmc_buff_1;
        dst_buf2[1] = ctx->obmc_buff_1 + ((blk_geom->bwidth * blk_geom->bheight) << is16bit);
        dst_buf2[2] = ctx->obmc_buff_1 + ((blk_geom->bwidth * blk_geom->bheight * 2) << is16bit);
    } else {
        dst_buf1[0] = obmc_buff_0;
        dst_buf1[1] = obmc_buff_0 + ((blk_geom->bwidth * blk_geom->bheight) << is16bit);
        dst_buf1[2] = obmc_buff_0 + ((blk_geom->bwidth * blk_geom->bheight * 2) << is16bit);
        dst_buf2[0] = obmc_buff_1;
        dst_buf2[1] = obmc_buff_1 + ((blk_geom->bwidth * blk_geom->bheight) << is16bit);
        dst_buf2[2] = obmc_buff_1 + ((blk_geom->bwidth * blk_geom->bheight * 2) << is16bit);

        build_prediction_by_above_preds((component_mask & PICTURE_BUFFER_DESC_CHROMA_MASK),
                                        blk_geom->bsize,
                                        pcs,
                                        blk_ptr->av1xd,
                                        mi_row,
                                        mi_col,
                                        dst_buf1,
                                        dst_stride1,
                                        is16bit);

        build_prediction_by_left_preds((component_mask & PICTURE_BUFFER_DESC_CHROMA_MASK),
                                       blk_geom->bsize,
                                       pcs,
                                       blk_ptr->av1xd,
                                       mi_row,
                                       mi_col,
                                       dst_buf2,
                                       dst_stride2,
                                       is16bit);
    }

    uint8_t *final_dst_ptr_y = pred_pic->buffer_y +
        ((pred_pic->org_x + dst_origin_x + (pred_pic->org_y + dst_origin_y) * pred_pic->stride_y) << is16bit);
    uint16_t final_dst_stride_y = pred_pic->stride_y;

    uint8_t *final_dst_ptr_u = pred_pic->buffer_cb +
        (((pred_pic->org_x + ((dst_origin_x >> 3) << 3)) / 2 +
          (pred_pic->org_y + ((dst_origin_y >> 3) << 3)) / 2 * pred_pic->stride_cb)
         << is16bit);
    uint16_t final_dst_stride_u = pred_pic->stride_cb;

    uint8_t *final_dst_ptr_v = pred_pic->buffer_cr +
        (((pred_pic->org_x + ((dst_origin_x >> 3) << 3)) / 2 +
          (pred_pic->org_y + ((dst_origin_y >> 3) << 3)) / 2 * pred_pic->stride_cr)
         << is16bit);
    uint16_t final_dst_stride_v = pred_pic->stride_cr;

    av1_build_obmc_inter_prediction(final_dst_ptr_y,
                                    final_dst_stride_y,
                                    final_dst_ptr_u,
                                    final_dst_stride_u,
                                    final_dst_ptr_v,
                                    final_dst_stride_v,
                                    component_mask,
                                    blk_geom->bsize,
                                    pcs,
                                    blk_ptr->av1xd,
                                    mi_row,
                                    mi_col,
                                    dst_buf1,
                                    dst_stride1,
                                    dst_buf2,
                                    dst_stride2,
                                    is16bit); // is16bit
}

// special treatment for chroma in 4XN/NX4 blocks if one of the neighbour blocks of the parent square is
// intra the chroma prediction will follow the normal path using the luma MV of the current nsq block which
// is the latest sub8x8. for this case: only uniPred is allowed.
//
// return whether sub8x8_inter prediction is performed
static uint8_t inter_chroma_4xn_pred(PictureControlSet *pcs, MacroBlockD *xd, MvReferenceFrame rf[2],
                                     EbPictureBufferDesc *pred_pic, uint16_t dst_origin_x, uint16_t dst_origin_y,
                                     uint16_t pu_origin_x_chroma, uint16_t pu_origin_y_chroma, MvUnit *mv_unit,
                                     uint8_t use_intrabc, uint8_t ss_x, uint8_t ss_y, const BlockSize bsize,
                                     ConvolveParams *conv_params_cb, ConvolveParams *conv_params_cr,
                                     ScaleFactors *sf_identity, uint32_t interp_filters,
                                     InterInterCompoundData *interinter_comp, uint8_t *seg_mask, uint8_t bit_depth) {
    assert(bsize < BlockSizeS_ALL);

    uint8_t is16bit = bit_depth > EB_EIGHT_BIT;

    // CHKN fill current mi from current block
    // only need to update top left mbmi for partition b/c all other MI blocks will reference the top left
    ModeInfo *mi_ptr                     = xd->mi[0];
    mi_ptr->mbmi.block_mi.use_intrabc    = use_intrabc;
    mi_ptr->mbmi.block_mi.ref_frame[0]   = rf[0];
    mi_ptr->mbmi.block_mi.interp_filters = interp_filters;
    if (mv_unit->pred_direction == UNI_PRED_LIST_0) {
        mi_ptr->mbmi.block_mi.mv[0].as_mv.col = mv_unit->mv[REF_LIST_0].x;
        mi_ptr->mbmi.block_mi.mv[0].as_mv.row = mv_unit->mv[REF_LIST_0].y;
    } else if (mv_unit->pred_direction == UNI_PRED_LIST_1) {
        mi_ptr->mbmi.block_mi.mv[0].as_mv.col = mv_unit->mv[REF_LIST_1].x;
        mi_ptr->mbmi.block_mi.mv[0].as_mv.row = mv_unit->mv[REF_LIST_1].y;
    } else {
        mi_ptr->mbmi.block_mi.mv[0].as_mv.col = mv_unit->mv[REF_LIST_0].x;
        mi_ptr->mbmi.block_mi.mv[0].as_mv.row = mv_unit->mv[REF_LIST_0].y;
        mi_ptr->mbmi.block_mi.mv[1].as_mv.col = mv_unit->mv[REF_LIST_1].x;
        mi_ptr->mbmi.block_mi.mv[1].as_mv.row = mv_unit->mv[REF_LIST_1].y;
    }

    uint8_t sub8x8_inter = (block_size_wide[bsize] < 8 && ss_x) || (block_size_high[bsize] < 8 && ss_y);

    if (use_intrabc)
        sub8x8_inter = 0;

    // For sub8x8 chroma blocks, we may be covering more than one luma block's
    // worth of pixels. Thus (mi_x, mi_y) may not be the correct coordinates for
    // the top-left corner of the prediction source - the correct top-left corner
    // is at (pre_x, pre_y).
    const int32_t row_start = (block_size_high[bsize] == 4) && ss_y ? -1 : 0;
    const int32_t col_start = (block_size_wide[bsize] == 4) && ss_x ? -1 : 0;

    if (sub8x8_inter) {
        for (int32_t row = row_start; row <= 0 && sub8x8_inter; ++row) {
            for (int32_t col = col_start; col <= 0; ++col) {
                const MbModeInfo *this_mbmi = &xd->mi[row * xd->mi_stride + col]->mbmi;
                if (!is_inter_block(&this_mbmi->block_mi))
                    sub8x8_inter = 0;
            }
        }
    }
    assert(pcs != NULL);
    if (sub8x8_inter) {
        // block size
        const int32_t   b4_w        = block_size_wide[bsize] >> ss_x;
        const int32_t   b4_h        = block_size_high[bsize] >> ss_y;
        const BlockSize plane_bsize = svt_aom_scale_chroma_bsize(bsize, ss_x, ss_y);
        assert(plane_bsize < BlockSizeS_ALL);
        const int32_t b8_w = block_size_wide[plane_bsize] >> ss_x;
        const int32_t b8_h = block_size_high[plane_bsize] >> ss_y;

        ScaleFactors scale_factors[MAX_NUM_OF_REF_PIC_LIST][REF_LIST_MAX_DEPTH];
        if (!use_intrabc && !pcs->ppcs->is_not_scaled) {
            uint32_t num_of_list_to_search = (pcs->ppcs->slice_type == P_SLICE) ? (uint32_t)REF_LIST_0
                                                                                : (uint32_t)REF_LIST_1;

            for (uint8_t list_idx = REF_LIST_0; list_idx <= num_of_list_to_search; ++list_idx) {
                uint8_t ref_idx;
                uint8_t num_of_ref_pic_to_search = (pcs->ppcs->slice_type == P_SLICE) ? pcs->ppcs->ref_list0_count
                    : (list_idx == REF_LIST_0)                                        ? pcs->ppcs->ref_list0_count
                                                                                      : pcs->ppcs->ref_list1_count;
                for (ref_idx = 0; ref_idx < num_of_ref_pic_to_search; ++ref_idx) {
                    EbPictureBufferDesc *ref_pic_ptr = svt_aom_get_ref_pic_buffer(
                        pcs, (bit_depth == EB_EIGHT_BIT && is16bit), list_idx, ref_idx);

                    svt_av1_setup_scale_factors_for_frame(&(scale_factors[list_idx][ref_idx]),
                                                          ref_pic_ptr->width,
                                                          ref_pic_ptr->height,
                                                          pcs->ppcs->enhanced_pic->width,
                                                          pcs->ppcs->enhanced_pic->height);
                }
            }
        }

        int32_t row = row_start;
        for (int32_t y = 0; y < b8_h; y += b4_h) {
            int32_t col = col_start;
            for (int32_t x = 0; x < b8_w; x += b4_w) {
                const MbModeInfo *this_mbmi = &xd->mi[row * xd->mi_stride + col]->mbmi;

                uint8_t ref_idx = get_ref_frame_idx(this_mbmi->block_mi.ref_frame[0]);
                assert(ref_idx < REF_LIST_MAX_DEPTH);

                uint8_t list_idx = (uint8_t)(this_mbmi->block_mi.ref_frame[0] == LAST_FRAME ||
                                                     this_mbmi->block_mi.ref_frame[0] == LAST2_FRAME ||
                                                     this_mbmi->block_mi.ref_frame[0] == LAST3_FRAME ||
                                                     this_mbmi->block_mi.ref_frame[0] == GOLDEN_FRAME
                                                 ? REF_LIST_0
                                                 : REF_LIST_1);

                EbPictureBufferDesc *ref_pic = svt_aom_get_ref_pic_buffer(pcs, is16bit, list_idx, ref_idx);

                const struct ScaleFactors *const sf = (use_intrabc || pcs->ppcs->is_not_scaled)
                    ? sf_identity
                    : &(scale_factors[list_idx][ref_idx]);

                // Cb
                uint8_t *src_ptr = ref_pic->buffer_cb +
                    (((ref_pic->org_x) / 2 + (ref_pic->org_y) / 2 * ref_pic->stride_cb));

                uint8_t *src_ptr_2b = NULL;
                if (ref_pic->buffer_bit_inc_cb)
                    src_ptr_2b = ref_pic->buffer_bit_inc_cb +
                        (((ref_pic->org_x) / 2 + (ref_pic->org_y) / 2 * ref_pic->stride_bit_inc_cb));
                uint8_t *dst_ptr = pred_pic->buffer_cb +
                    (((pred_pic->org_x + ((dst_origin_x >> 3) << 3)) / 2 +
                      (pred_pic->org_y + ((dst_origin_y >> 3) << 3)) / 2 * pred_pic->stride_cb)
                     << is16bit);
                dst_ptr += (x + y * pred_pic->stride_cb) << is16bit;
                int32_t dst_stride = pred_pic->stride_cb;

                svt_aom_enc_make_inter_predictor(pcs->ppcs->scs,
                                                 src_ptr,
                                                 src_ptr_2b,
                                                 dst_ptr,
                                                 pu_origin_y_chroma + y,
                                                 pu_origin_x_chroma + x,
                                                 this_mbmi->block_mi.mv[0].as_mv,
                                                 sf,
                                                 conv_params_cb,
                                                 this_mbmi->block_mi.interp_filters,
                                                 interinter_comp,
                                                 seg_mask,
                                                 ref_pic->width,
                                                 ref_pic->height,
                                                 (uint8_t)b4_w,
                                                 (uint8_t)b4_h,
                                                 bsize,
                                                 xd,
                                                 ref_pic->stride_cb,
                                                 dst_stride,
                                                 1,
                                                 ss_y,
                                                 ss_x,
                                                 bit_depth,
                                                 use_intrabc,
                                                 0,
                                                 is16bit);

                //Cr

                src_ptr = ref_pic->buffer_cr + (((ref_pic->org_x) / 2 + (ref_pic->org_y) / 2 * ref_pic->stride_cr));

                src_ptr_2b = NULL;
                if (ref_pic->buffer_bit_inc_cr)
                    src_ptr_2b = ref_pic->buffer_bit_inc_cr +
                        (((ref_pic->org_x) / 2 + (ref_pic->org_y) / 2 * ref_pic->stride_bit_inc_cr));
                dst_ptr = pred_pic->buffer_cr +
                    (((pred_pic->org_x + ((dst_origin_x >> 3) << 3)) / 2 +
                      (pred_pic->org_y + ((dst_origin_y >> 3) << 3)) / 2 * pred_pic->stride_cr)
                     << is16bit);
                dst_ptr += (x + y * pred_pic->stride_cr) << is16bit;
                dst_stride = pred_pic->stride_cr;

                svt_aom_enc_make_inter_predictor(pcs->ppcs->scs,
                                                 src_ptr,
                                                 src_ptr_2b,
                                                 dst_ptr,
                                                 pu_origin_y_chroma + y,
                                                 pu_origin_x_chroma + x,
                                                 this_mbmi->block_mi.mv[0].as_mv,
                                                 sf,
                                                 conv_params_cr,
                                                 this_mbmi->block_mi.interp_filters,
                                                 interinter_comp,
                                                 seg_mask,
                                                 ref_pic->width,
                                                 ref_pic->height,
                                                 (uint8_t)b4_w,
                                                 (uint8_t)b4_h,
                                                 bsize,
                                                 xd,
                                                 ref_pic->stride_cr,
                                                 dst_stride,
                                                 2,
                                                 ss_y,
                                                 ss_x,
                                                 bit_depth,
                                                 use_intrabc,
                                                 0,
                                                 is16bit);

                ++col;
            }
            ++row;
        }
    }

    return sub8x8_inter;
}

EbErrorType svt_aom_inter_prediction(SequenceControlSet *scs, PictureControlSet *pcs, uint32_t interp_filters,
                                     BlkStruct *blk_ptr, uint8_t ref_frame_type, MvUnit *mv_unit, uint8_t use_intrabc,
                                     MotionMode motion_mode, uint8_t use_precomputed_obmc,
                                     struct ModeDecisionContext *ctx, uint8_t compound_idx,
                                     InterInterCompoundData *interinter_comp, NeighborArrayUnit *recon_neigh_y,
                                     NeighborArrayUnit *recon_neigh_cb, NeighborArrayUnit *recon_neigh_cr,
                                     uint8_t is_interintra_used, InterIntraMode interintra_mode,
                                     uint8_t use_wedge_interintra, int32_t interintra_wedge_index, uint16_t pu_origin_x,
                                     uint16_t pu_origin_y, uint8_t bwidth, uint8_t bheight,
                                     EbPictureBufferDesc *ref_pic_list0, EbPictureBufferDesc *ref_pic_list1,
                                     EbPictureBufferDesc *pred_pic, uint16_t dst_origin_x, uint16_t dst_origin_y,
                                     uint32_t component_mask, uint8_t bit_depth, uint8_t is_16bit_pipeline) {
    uint8_t     is16bit      = bit_depth > EB_EIGHT_BIT || is_16bit_pipeline;
    EbErrorType return_error = EB_ErrorNone;
    uint8_t     is_compound  = (mv_unit->pred_direction == BI_PRED) ? 1 : 0;

    // Move these to context if stack does not hold.
    // Process chroma after luma to re-use buffer.
    DECLARE_ALIGNED(32, uint16_t, tmp_dst_y[128 * 128]);
    uint16_t *tmp_dst_cb = tmp_dst_y;
    uint16_t *tmp_dst_cr = &tmp_dst_y[64 * 64];

    // seg_mask is computed for luma and used for chroma
    DECLARE_ALIGNED(16, uint8_t, seg_mask[2 * MAX_SB_SQUARE]);

    MvReferenceFrame rf[2];
    av1_set_ref_frame(rf, ref_frame_type);

    MV mv;

    uint8_t *src_ptr;
    uint8_t *src_ptr_2b = NULL;

    int32_t fwd_offset = 0, bck_offset = 0, use_dist_wtd_comp_avg = 0;

    InterpFilterParams filter_params_x, filter_params_y;

    const BlockGeom *blk_geom = get_blk_geom_mds(blk_ptr->mds_idx);

    ScaleFactors sf_identity = scs->sf_identity;

    ScaleFactors ref0_scale_factors;
    if (pcs != NULL && !pcs->ppcs->is_not_scaled && ref_pic_list0 != NULL) {
        svt_av1_setup_scale_factors_for_frame(&ref0_scale_factors,
                                              ref_pic_list0->width,
                                              ref_pic_list0->height,
                                              pcs->ppcs->enhanced_pic->width,
                                              pcs->ppcs->enhanced_pic->height);
    }

    ScaleFactors ref1_scale_factors;
    if (pcs != NULL && !pcs->ppcs->is_not_scaled && ref_pic_list1 != NULL) {
        svt_av1_setup_scale_factors_for_frame(&ref1_scale_factors,
                                              ref_pic_list1->width,
                                              ref_pic_list1->height,
                                              pcs->ppcs->enhanced_pic->width,
                                              pcs->ppcs->enhanced_pic->height);
    }

    // Get compound info; computed once at beginning as the data is the same for luma/chroma
    if (is_compound) {
        svt_av1_dist_wtd_comp_weight_assign(&pcs->ppcs->scs->seq_header,
                                            pcs->ppcs->cur_order_hint, // cur_frame_index,
                                            pcs->ppcs->ref_order_hint[rf[0] - 1], // bck_frame_index,
                                            pcs->ppcs->ref_order_hint[rf[1] - 1], // fwd_frame_index,
                                            compound_idx,
                                            0, // order_idx,
                                            &fwd_offset,
                                            &bck_offset,
                                            &use_dist_wtd_comp_avg,
                                            is_compound);
    }

    // Perform luma prediction
    if (component_mask & PICTURE_BUFFER_DESC_LUMA_MASK) {
        const uint8_t  ss_x          = 0;
        const uint8_t  ss_y          = 0;
        ConvolveParams conv_params_y = get_conv_params_no_round(0, 0, 0, tmp_dst_y, 128, is_compound, bit_depth);
        av1_get_convolve_filter_params(interp_filters, &filter_params_x, &filter_params_y, bwidth, bheight);
        uint8_t *dst_ptr_y = pred_pic->buffer_y +
            ((pred_pic->org_x + dst_origin_x + (pred_pic->org_y + dst_origin_y) * pred_pic->stride_y) << is16bit);

        if (mv_unit->pred_direction == UNI_PRED_LIST_0 || mv_unit->pred_direction == BI_PRED) {
            assert(ref_pic_list0 != NULL);
            // List0-Y
            mv.col = mv_unit->mv[REF_LIST_0].x;
            mv.row = mv_unit->mv[REF_LIST_0].y;

            if (ref_pic_list0->buffer_bit_inc_y) {
                src_ptr = ref_pic_list0->buffer_y +
                    ((ref_pic_list0->org_x + (ref_pic_list0->org_y) * ref_pic_list0->stride_y));
                src_ptr_2b = ref_pic_list0->buffer_bit_inc_y +
                    ((ref_pic_list0->org_x + (ref_pic_list0->org_y) * ref_pic_list0->stride_bit_inc_y));
            } else {
                src_ptr = ref_pic_list0->buffer_y +
                    ((ref_pic_list0->org_x + (ref_pic_list0->org_y) * ref_pic_list0->stride_y) << is16bit);
            }
            // ScaleFactor
            const struct ScaleFactors *const sf = (use_intrabc || pcs == NULL || pcs->ppcs->is_not_scaled)
                ? &sf_identity
                : &ref0_scale_factors;

            svt_aom_enc_make_inter_predictor(scs,
                                             src_ptr,
                                             src_ptr_2b,
                                             dst_ptr_y,
                                             (int16_t)pu_origin_y,
                                             (int16_t)pu_origin_x,
                                             mv,
                                             sf,
                                             &conv_params_y,
                                             interp_filters,
                                             interinter_comp,
                                             seg_mask,
                                             ref_pic_list0->width,
                                             ref_pic_list0->height,
                                             bwidth,
                                             bheight,
                                             blk_geom->bsize,
                                             blk_ptr->av1xd,
                                             ref_pic_list0->stride_y,
                                             pred_pic->stride_y,
                                             0,
                                             ss_y,
                                             ss_x,
                                             bit_depth,
                                             use_intrabc,
                                             0,
                                             is16bit);
        }

        if (mv_unit->pred_direction == UNI_PRED_LIST_1 || mv_unit->pred_direction == BI_PRED) {
            assert(ref_pic_list1 != NULL);
            // List1-Y
            mv.col = mv_unit->mv[REF_LIST_1].x;
            mv.row = mv_unit->mv[REF_LIST_1].y;

            if (is_compound) {
                conv_params_y.do_average            = 1;
                conv_params_y.fwd_offset            = fwd_offset;
                conv_params_y.bck_offset            = bck_offset;
                conv_params_y.use_dist_wtd_comp_avg = use_dist_wtd_comp_avg;
                conv_params_y.use_jnt_comp_avg      = conv_params_y.use_dist_wtd_comp_avg;
            }

            src_ptr = ref_pic_list1->buffer_y +
                ((ref_pic_list1->org_x + (ref_pic_list1->org_y) * ref_pic_list1->stride_y));
            src_ptr_2b = ref_pic_list1->buffer_bit_inc_y +
                ((ref_pic_list1->org_x + (ref_pic_list1->org_y) * ref_pic_list1->stride_bit_inc_y));
            // ScaleFactor
            const struct ScaleFactors *const sf = (use_intrabc || pcs->ppcs->is_not_scaled) ? &sf_identity
                                                                                            : &ref1_scale_factors;

            svt_aom_enc_make_inter_predictor(scs,
                                             src_ptr,
                                             src_ptr_2b,
                                             dst_ptr_y,
                                             (int16_t)pu_origin_y,
                                             (int16_t)pu_origin_x,
                                             mv,
                                             sf,
                                             &conv_params_y,
                                             interp_filters,
                                             interinter_comp,
                                             seg_mask,
                                             ref_pic_list1->width,
                                             ref_pic_list1->height,
                                             bwidth,
                                             bheight,
                                             blk_geom->bsize,
                                             blk_ptr->av1xd,
                                             ref_pic_list1->stride_y,
                                             pred_pic->stride_y,
                                             0,
                                             ss_y,
                                             ss_x,
                                             bit_depth,
                                             use_intrabc,
                                             is_compound && svt_aom_is_masked_compound_type(interinter_comp->type),
                                             is16bit);
        }
    }

    // Perform chroma prediction
    if (blk_geom->has_uv && (component_mask & PICTURE_BUFFER_DESC_CHROMA_MASK)) {
        uint8_t *dst_ptr_cb = pred_pic->buffer_cb +
            (((pred_pic->org_x + ((dst_origin_x >> 3) << 3)) / 2 +
              (pred_pic->org_y + ((dst_origin_y >> 3) << 3)) / 2 * pred_pic->stride_cb)
             << is16bit);
        uint8_t *dst_ptr_cr = pred_pic->buffer_cr +
            (((pred_pic->org_x + ((dst_origin_x >> 3) << 3)) / 2 +
              (pred_pic->org_y + ((dst_origin_y >> 3) << 3)) / 2 * pred_pic->stride_cr)
             << is16bit);
        ConvolveParams conv_params_cb = get_conv_params_no_round(0, 0, 0, tmp_dst_cb, 64, is_compound, bit_depth);
        ConvolveParams conv_params_cr = get_conv_params_no_round(0, 0, 0, tmp_dst_cr, 64, is_compound, bit_depth);
        av1_get_convolve_filter_params(
            interp_filters, &filter_params_x, &filter_params_y, blk_geom->bwidth_uv, blk_geom->bheight_uv);
        const uint8_t ss_x               = 1; // pd->subsampling_x;
        const uint8_t ss_y               = 1; //pd->subsampling_y;
        const int16_t pu_origin_y_chroma = ((pu_origin_y >> 3) << 3) / 2;
        const int16_t pu_origin_x_chroma = ((pu_origin_x >> 3) << 3) / 2;
        uint8_t       sub8x8_inter       = 0;

        // special treatment for chroma in 4XN/NX4 blocks if one of the neighbour blocks of the parent square is
        // intra the chroma prediction will follow the normal path using the luma MV of the current nsq block which
        // is the latest sub8x8. for this case: only uniPred is allowed.
        if ((blk_geom->bwidth == 4 || blk_geom->bheight == 4) && pcs) {
            assert(!is_compound);
            sub8x8_inter = inter_chroma_4xn_pred(pcs,
                                                 blk_ptr->av1xd,
                                                 rf,
                                                 pred_pic,
                                                 dst_origin_x,
                                                 dst_origin_y,
                                                 pu_origin_x_chroma,
                                                 pu_origin_y_chroma,
                                                 mv_unit,
                                                 use_intrabc,
                                                 ss_x,
                                                 ss_y,
                                                 blk_geom->bsize,
                                                 &conv_params_cb,
                                                 &conv_params_cr,
                                                 &sf_identity,
                                                 interp_filters,
                                                 interinter_comp,
                                                 seg_mask,
                                                 bit_depth);
        }

        if ((mv_unit->pred_direction == UNI_PRED_LIST_0 || mv_unit->pred_direction == BI_PRED) && !sub8x8_inter) {
            assert(ref_pic_list0 != NULL);
            mv.col = mv_unit->mv[REF_LIST_0].x;
            mv.row = mv_unit->mv[REF_LIST_0].y;

            // ScaleFactor
            const struct ScaleFactors *const sf = (use_intrabc || pcs == NULL || pcs->ppcs->is_not_scaled)
                ? &sf_identity
                : &ref0_scale_factors;

            // List0-Cb

            if (ref_pic_list0->buffer_bit_inc_cb) {
                src_ptr = ref_pic_list0->buffer_cb +
                    (((ref_pic_list0->org_x) / 2 + (ref_pic_list0->org_y) / 2 * ref_pic_list0->stride_cb));
                src_ptr_2b = ref_pic_list0->buffer_bit_inc_cb +
                    (((ref_pic_list0->org_x) / 2 + (ref_pic_list0->org_y) / 2 * ref_pic_list0->stride_bit_inc_cb));
            } else {
                src_ptr = ref_pic_list0->buffer_cb +
                    (((ref_pic_list0->org_x) / 2 + (ref_pic_list0->org_y) / 2 * ref_pic_list0->stride_cb) << is16bit);
            }

            svt_aom_enc_make_inter_predictor(scs,
                                             src_ptr,
                                             src_ptr_2b,
                                             dst_ptr_cb,
                                             pu_origin_y_chroma,
                                             pu_origin_x_chroma,
                                             mv,
                                             sf,
                                             &conv_params_cb,
                                             interp_filters,
                                             interinter_comp,
                                             seg_mask,
                                             ref_pic_list0->width,
                                             ref_pic_list0->height,
                                             (uint8_t)blk_geom->bwidth_uv,
                                             (uint8_t)blk_geom->bheight_uv,
                                             blk_geom->bsize,
                                             blk_ptr->av1xd,
                                             ref_pic_list0->stride_cb,
                                             pred_pic->stride_cb,
                                             1,
                                             ss_y,
                                             ss_x,
                                             bit_depth,
                                             use_intrabc,
                                             0,
                                             is16bit);

            // List0-Cr

            if (ref_pic_list0->buffer_bit_inc_cr) {
                src_ptr = ref_pic_list0->buffer_cr +
                    (((ref_pic_list0->org_x) / 2 + (ref_pic_list0->org_y) / 2 * ref_pic_list0->stride_cr));
                src_ptr_2b = ref_pic_list0->buffer_bit_inc_cr +
                    (((ref_pic_list0->org_x) / 2 + (ref_pic_list0->org_y) / 2 * ref_pic_list0->stride_bit_inc_cr));

            } else {
                src_ptr = ref_pic_list0->buffer_cr +
                    (((ref_pic_list0->org_x) / 2 + (ref_pic_list0->org_y) / 2 * ref_pic_list0->stride_cr) << is16bit);
            }
            svt_aom_enc_make_inter_predictor(scs,
                                             src_ptr,
                                             src_ptr_2b,
                                             dst_ptr_cr,
                                             pu_origin_y_chroma,
                                             pu_origin_x_chroma,
                                             mv,
                                             sf,
                                             &conv_params_cr,
                                             interp_filters,
                                             interinter_comp,
                                             seg_mask,
                                             ref_pic_list0->width,
                                             ref_pic_list0->height,
                                             blk_geom->bwidth_uv,
                                             blk_geom->bheight_uv,
                                             blk_geom->bsize,
                                             blk_ptr->av1xd,
                                             ref_pic_list0->stride_cr,
                                             pred_pic->stride_cr,
                                             2,
                                             ss_y,
                                             ss_x,
                                             bit_depth,
                                             use_intrabc,
                                             0,
                                             is16bit);
        }

        if ((mv_unit->pred_direction == UNI_PRED_LIST_1 || mv_unit->pred_direction == BI_PRED) && !sub8x8_inter) {
            assert(ref_pic_list1 != NULL);

            mv.col = mv_unit->mv[REF_LIST_1].x;
            mv.row = mv_unit->mv[REF_LIST_1].y;

            // ScaleFactor
            const struct ScaleFactors *const sf = (use_intrabc || pcs->ppcs->is_not_scaled) ? &sf_identity
                                                                                            : &ref1_scale_factors;

            if (is_compound) {
                conv_params_cb.do_average            = 1;
                conv_params_cb.fwd_offset            = fwd_offset;
                conv_params_cb.bck_offset            = bck_offset;
                conv_params_cb.use_dist_wtd_comp_avg = use_dist_wtd_comp_avg;
                conv_params_cb.use_jnt_comp_avg      = conv_params_cb.use_dist_wtd_comp_avg;

                conv_params_cr.do_average            = 1;
                conv_params_cr.fwd_offset            = fwd_offset;
                conv_params_cr.bck_offset            = bck_offset;
                conv_params_cr.use_dist_wtd_comp_avg = use_dist_wtd_comp_avg;
                conv_params_cr.use_jnt_comp_avg      = conv_params_cr.use_dist_wtd_comp_avg;
            }

            // List1-Cb

            src_ptr = ref_pic_list1->buffer_cb +
                (((ref_pic_list1->org_x) / 2 + (ref_pic_list1->org_y) / 2 * ref_pic_list1->stride_cb));
            src_ptr_2b = ref_pic_list1->buffer_bit_inc_cb +
                (((ref_pic_list1->org_x) / 2 + (ref_pic_list1->org_y) / 2 * ref_pic_list1->stride_bit_inc_cb));

            svt_aom_enc_make_inter_predictor(scs,
                                             src_ptr,
                                             src_ptr_2b,
                                             dst_ptr_cb,
                                             pu_origin_y_chroma,
                                             pu_origin_x_chroma,
                                             mv,
                                             sf,
                                             &conv_params_cb,
                                             interp_filters,
                                             interinter_comp,
                                             seg_mask,
                                             ref_pic_list1->width,
                                             ref_pic_list1->height,
                                             blk_geom->bwidth_uv,
                                             blk_geom->bheight_uv,
                                             blk_geom->bsize,
                                             blk_ptr->av1xd,
                                             ref_pic_list1->stride_cb,
                                             pred_pic->stride_cb,
                                             1,
                                             ss_y,
                                             ss_x,
                                             bit_depth,
                                             use_intrabc,
                                             is_compound && svt_aom_is_masked_compound_type(interinter_comp->type),
                                             is16bit);

            // List1-Cr

            src_ptr = ref_pic_list1->buffer_cr +
                (((ref_pic_list1->org_x) / 2 + (ref_pic_list1->org_y) / 2 * ref_pic_list1->stride_cr));
            src_ptr_2b = ref_pic_list1->buffer_bit_inc_cr +
                (((ref_pic_list1->org_x) / 2 + (ref_pic_list1->org_y) / 2 * ref_pic_list1->stride_bit_inc_cr));

            svt_aom_enc_make_inter_predictor(scs,
                                             src_ptr,
                                             src_ptr_2b,
                                             dst_ptr_cr,
                                             pu_origin_y_chroma,
                                             pu_origin_x_chroma,
                                             mv,
                                             sf,
                                             &conv_params_cr,
                                             interp_filters,
                                             interinter_comp,
                                             seg_mask,
                                             ref_pic_list1->width,
                                             ref_pic_list1->height,
                                             blk_geom->bwidth_uv,
                                             blk_geom->bheight_uv,
                                             blk_geom->bsize,
                                             blk_ptr->av1xd,
                                             ref_pic_list1->stride_cr,
                                             pred_pic->stride_cr,
                                             2,
                                             ss_y,
                                             ss_x,
                                             bit_depth,
                                             use_intrabc,
                                             is_compound && svt_aom_is_masked_compound_type(interinter_comp->type),
                                             is16bit);
        }
    }

    if (is_interintra_used) {
        inter_intra_prediction(pcs,
                               pred_pic,
                               interintra_mode,
                               use_wedge_interintra,
                               interintra_wedge_index,
                               recon_neigh_y,
                               recon_neigh_cb,
                               recon_neigh_cr,
                               blk_ptr,
                               blk_geom,
                               pu_origin_x,
                               pu_origin_y,
                               dst_origin_x,
                               dst_origin_y,
                               component_mask,
                               bit_depth,
                               is16bit);
    }

    if (motion_mode == OBMC_CAUSAL) {
        assert(is_compound == 0);
        assert(blk_geom->bwidth > 4 && blk_geom->bheight > 4);
        av1_inter_prediction_obmc(pcs,
                                  blk_ptr,
                                  use_precomputed_obmc,
                                  ctx,
                                  pu_origin_x,
                                  pu_origin_y,
                                  pred_pic,
                                  dst_origin_x,
                                  dst_origin_y,
                                  component_mask,
                                  bit_depth,
                                  is_16bit_pipeline);
    }

    return return_error;
}

Bool svt_aom_calc_pred_masked_compound(PictureControlSet *pcs, ModeDecisionContext *ctx, ModeDecisionCandidate *cand) {
    SequenceControlSet  *scs     = pcs->scs;
    uint8_t              hbd_md  = ctx->hbd_md == EB_DUAL_BIT_MD ? EB_8_BIT_MD : ctx->hbd_md;
    EbPictureBufferDesc *src_pic = hbd_md ? pcs->input_frame16bit : pcs->ppcs->enhanced_pic;
    uint32_t             bwidth  = ctx->blk_geom->bwidth;
    uint32_t             bheight = ctx->blk_geom->bheight;
    EbPictureBufferDesc  pred_desc;
    pred_desc.org_x = pred_desc.org_y = 0;
    pred_desc.stride_y                = bwidth;

    EbPictureBufferDesc *ref_pic_list0;
    EbPictureBufferDesc *ref_pic_list1 = NULL;
    Mv                   mv_0;
    Mv                   mv_1;
    mv_0.as_int = cand->mv[0].as_int;
    mv_1.as_int = cand->mv[1].as_int;
    MvUnit mv_unit;
    mv_unit.mv[0] = mv_0;
    mv_unit.mv[1] = mv_1;
    MvReferenceFrame rf[2];
    av1_set_ref_frame(rf, cand->ref_frame_type);
    int8_t  ref_idx_l0 = get_ref_frame_idx(rf[0]);
    int8_t  ref_idx_l1 = rf[1] == NONE_FRAME ? get_ref_frame_idx(rf[0]) : get_ref_frame_idx(rf[1]);
    uint8_t list_idx0, list_idx1;
    list_idx0 = get_list_idx(rf[0]);
    if (rf[1] == NONE_FRAME)
        list_idx1 = get_list_idx(rf[0]);
    else
        list_idx1 = get_list_idx(rf[1]);
    assert(list_idx0 < MAX_NUM_OF_REF_PIC_LIST);
    assert(list_idx1 < MAX_NUM_OF_REF_PIC_LIST);
    if (ref_idx_l0 >= 0)
        ref_pic_list0 = svt_aom_get_ref_pic_buffer(pcs, hbd_md, list_idx0, ref_idx_l0);
    else
        ref_pic_list0 = (EbPictureBufferDesc *)NULL;

    if (ref_idx_l1 >= 0)
        ref_pic_list1 = svt_aom_get_ref_pic_buffer(pcs, hbd_md, list_idx1, ref_idx_l1);
    else
        ref_pic_list1 = (EbPictureBufferDesc *)NULL;

    //CHKN get seperate prediction of each ref(Luma only)
    //ref0 prediction
    mv_unit.pred_direction = UNI_PRED_LIST_0;

    bool found_l0 = false;
    for (int bufi = 0; bufi < ctx->cmp_store.pred0_cnt; bufi++) {
        if (mv_0.as_int == ctx->cmp_store.pred0_mv[bufi].as_int) {
            found_l0   = true;
            ctx->pred0 = ctx->cmp_store.pred0_buf[bufi];
            break;
        }
    }

    if (!found_l0) {
        svt_aom_assert_err(ctx->cmp_store.pred0_cnt < 4, "compound store full \n");
        ctx->cmp_store.pred0_mv[ctx->cmp_store.pred0_cnt].as_int = mv_0.as_int;
        ctx->pred0                                               = ctx->cmp_store.pred0_buf[ctx->cmp_store.pred0_cnt++];
    }

    pred_desc.buffer_y = ctx->pred0;

    //we call the regular inter prediction path here(no compound)
    if (!found_l0)
        svt_aom_inter_prediction(scs,
                                 pcs,
                                 0, //fixed interpolation filter for compound search
                                 ctx->blk_ptr,
                                 cand->ref_frame_type,
                                 &mv_unit,
                                 0, //use_intrabc,
                                 SIMPLE_TRANSLATION,
                                 0,
                                 0,
                                 1, //compound_idx not used
                                 NULL, // interinter_comp not used
                                 NULL,
                                 NULL,
                                 NULL,
                                 0,
                                 0,
                                 0,
                                 0,
                                 ctx->blk_org_x,
                                 ctx->blk_org_y,
                                 bwidth,
                                 bheight,
                                 ref_pic_list0,
                                 ref_pic_list1,
                                 &pred_desc, //output
                                 0, //output org_x,
                                 0, //output org_y,
                                 PICTURE_BUFFER_DESC_LUMA_MASK,
                                 hbd_md ? EB_TEN_BIT : EB_EIGHT_BIT,
                                 0); // is_16bit_pipeline

    bool found_l1 = false;
    for (int bufi = 0; bufi < ctx->cmp_store.pred1_cnt; bufi++) {
        if (mv_1.as_int == ctx->cmp_store.pred1_mv[bufi].as_int) {
            found_l1   = true;
            ctx->pred1 = ctx->cmp_store.pred1_buf[bufi];
            break;
        }
    }

    if (!found_l1) {
        svt_aom_assert_err(ctx->cmp_store.pred1_cnt < 4, "compound store full \n");
        ctx->cmp_store.pred1_mv[ctx->cmp_store.pred1_cnt].as_int = mv_1.as_int;
        ctx->pred1                                               = ctx->cmp_store.pred1_buf[ctx->cmp_store.pred1_cnt++];
    }

    //ref1 prediction
    mv_unit.pred_direction = UNI_PRED_LIST_1;
    pred_desc.buffer_y     = ctx->pred1;

    //we call the regular inter prediction path here(no compound)
    if (!found_l1)
        svt_aom_inter_prediction(scs,
                                 pcs,
                                 0, //fixed interpolation filter for compound search
                                 ctx->blk_ptr,
                                 cand->ref_frame_type,
                                 &mv_unit,
                                 0, //use_intrabc,
                                 SIMPLE_TRANSLATION,
                                 0,
                                 0,
                                 1, //compound_idx not used
                                 NULL, // interinter_comp not useds
                                 NULL,
                                 NULL,
                                 NULL,
                                 0,
                                 0,
                                 0,
                                 0,
                                 ctx->blk_org_x,
                                 ctx->blk_org_y,
                                 bwidth,
                                 bheight,
                                 ref_pic_list0,
                                 ref_pic_list1,
                                 &pred_desc, //output
                                 0, //output org_x,
                                 0, //output org_y,
                                 PICTURE_BUFFER_DESC_LUMA_MASK,
                                 hbd_md ? EB_TEN_BIT : EB_EIGHT_BIT,
                                 0); // is_16bit_pipeline

    Bool     exit_compound_prep  = FALSE;
    uint32_t pred0_to_pred1_dist = 0;

    if (hbd_md) {
        pred0_to_pred1_dist = sad_16b_kernel(
            (uint16_t *)ctx->pred0, bwidth, (uint16_t *)ctx->pred1, bwidth, bheight, bwidth);
    } else {
        pred0_to_pred1_dist = svt_nxm_sad_kernel_sub_sampled(ctx->pred0, bwidth, ctx->pred1, bwidth, bheight, bwidth);
    }

    if (pred0_to_pred1_dist < (bheight * bwidth * ctx->inter_comp_ctrls.pred0_to_pred1_mult)) {
        exit_compound_prep = TRUE;
        return exit_compound_prep;
    }

    if (hbd_md) {
        uint16_t *src_buf_hbd = (uint16_t *)src_pic->buffer_y + (ctx->blk_org_x + src_pic->org_x) +
            (ctx->blk_org_y + src_pic->org_y) * src_pic->stride_y;
        svt_aom_highbd_subtract_block(bheight,
                                      bwidth,
                                      ctx->residual1,
                                      bwidth,
                                      (uint8_t *)src_buf_hbd,
                                      src_pic->stride_y,
                                      (uint8_t *)ctx->pred1,
                                      bwidth,
                                      EB_TEN_BIT);
        svt_aom_highbd_subtract_block(bheight,
                                      bwidth,
                                      ctx->diff10,
                                      bwidth,
                                      (uint8_t *)ctx->pred1,
                                      bwidth,
                                      (uint8_t *)ctx->pred0,
                                      bwidth,
                                      EB_TEN_BIT);
    } else {
        uint8_t *src_buf = src_pic->buffer_y + (ctx->blk_org_x + src_pic->org_x) +
            (ctx->blk_org_y + src_pic->org_y) * src_pic->stride_y;
        svt_aom_subtract_block(bheight, bwidth, ctx->residual1, bwidth, src_buf, src_pic->stride_y, ctx->pred1, bwidth);
        svt_aom_subtract_block(bheight, bwidth, ctx->diff10, bwidth, ctx->pred1, bwidth, ctx->pred0, bwidth);
    }

    return exit_compound_prep;
}
void svt_aom_search_compound_diff_wedge(PictureControlSet *pcs, ModeDecisionContext *ctx, ModeDecisionCandidate *cand) {
    pick_interinter_mask(
        pcs, ctx, &cand->interinter_comp, ctx->blk_geom->bsize, ctx->pred0, ctx->pred1, ctx->residual1, ctx->diff10);
}

/*
 */
EbErrorType svt_aom_inter_pu_prediction_av1_light_pd0(uint8_t hbd_md, ModeDecisionContext *ctx, PictureControlSet *pcs,
                                                      ModeDecisionCandidateBuffer *cand_bf) {
    EbErrorType                  return_error  = EB_ErrorNone;
    EbPictureBufferDesc         *ref_pic_list0 = (EbPictureBufferDesc *)NULL;
    EbPictureBufferDesc         *ref_pic_list1 = (EbPictureBufferDesc *)NULL;
    ModeDecisionCandidate *const cand          = cand_bf->cand;

    MvUnit mv_unit;
    mv_unit.mv[0].as_int = cand->mv[0].as_int;
    mv_unit.mv[1].as_int = cand->mv[1].as_int;
    MvReferenceFrame rf[2];
    av1_set_ref_frame(rf, cand->ref_frame_type);
    const int8_t ref_idx_first  = get_ref_frame_idx(rf[0]);
    const int8_t list_idx_first = get_list_idx(rf[0]);
    mv_unit.pred_direction      = (rf[1] == NONE_FRAME) ? list_idx_first : BI_PRED;

    if (rf[1] == NONE_FRAME) {
        if (list_idx_first == 0)
            ref_pic_list0 = svt_aom_get_ref_pic_buffer(pcs, hbd_md, 0, ref_idx_first);
        else
            ref_pic_list1 = svt_aom_get_ref_pic_buffer(pcs, hbd_md, 1, ref_idx_first);
    } else {
        ref_pic_list0             = svt_aom_get_ref_pic_buffer(pcs, hbd_md, list_idx_first, ref_idx_first);
        const int8_t ref_idx_sec  = get_ref_frame_idx(rf[1]);
        const int8_t list_idx_sec = get_list_idx(rf[1]);

        ref_pic_list1 = svt_aom_get_ref_pic_buffer(pcs, hbd_md, list_idx_sec, ref_idx_sec);
    }
    SequenceControlSet *scs                = pcs->scs;
    ScaleFactors        ref0_scale_factors = scs->sf_identity;
    if (!pcs->ppcs->is_not_scaled && ref_pic_list0 != NULL) {
        svt_av1_setup_scale_factors_for_frame(&ref0_scale_factors,
                                              ref_pic_list0->width,
                                              ref_pic_list0->height,
                                              pcs->ppcs->enhanced_pic->width,
                                              pcs->ppcs->enhanced_pic->height);
    }

    ScaleFactors ref1_scale_factors = scs->sf_identity;
    if (!pcs->ppcs->is_not_scaled && ref_pic_list1 != NULL) {
        svt_av1_setup_scale_factors_for_frame(&ref1_scale_factors,
                                              ref_pic_list1->width,
                                              ref_pic_list1->height,
                                              pcs->ppcs->enhanced_pic->width,
                                              pcs->ppcs->enhanced_pic->height);
    }

    av1_inter_prediction_light_pd0(scs,
                                   &mv_unit,
                                   ctx,
                                   ctx->blk_org_x,
                                   ctx->blk_org_y,
                                   ctx->blk_geom->bwidth,
                                   ctx->blk_geom->bheight,
                                   ref_pic_list0,
                                   ref_pic_list1,
                                   cand_bf->pred,
                                   ctx->blk_geom->org_x,
                                   ctx->blk_geom->org_y,
                                   &ref0_scale_factors,
                                   &ref1_scale_factors);

    return return_error;
}
/*
   light mcp path function
*/
EbErrorType svt_aom_inter_pu_prediction_av1_light_pd1(uint8_t hbd_md, ModeDecisionContext *ctx, PictureControlSet *pcs,
                                                      ModeDecisionCandidateBuffer *cand_bf) {
    EbPictureBufferDesc         *ref_pic_list0 = (EbPictureBufferDesc *)NULL;
    EbPictureBufferDesc         *ref_pic_list1 = (EbPictureBufferDesc *)NULL;
    ModeDecisionCandidate *const cand          = cand_bf->cand;

    MvUnit mv_unit;
    mv_unit.mv[0].as_int = cand->mv[0].as_int;
    mv_unit.mv[1].as_int = cand->mv[1].as_int;
    MvReferenceFrame rf[2];
    av1_set_ref_frame(rf, cand->ref_frame_type);
    const int8_t ref_idx_first  = get_ref_frame_idx(rf[0]);
    const int8_t list_idx_first = get_list_idx(rf[0]);
    mv_unit.pred_direction      = (rf[1] == NONE_FRAME) ? list_idx_first : BI_PRED;

    if (rf[1] == NONE_FRAME) {
        if (list_idx_first == 0)
            ref_pic_list0 = svt_aom_get_ref_pic_buffer(pcs, hbd_md, 0, ref_idx_first);
        else
            ref_pic_list1 = svt_aom_get_ref_pic_buffer(pcs, hbd_md, 1, ref_idx_first);
    } else {
        ref_pic_list0             = svt_aom_get_ref_pic_buffer(pcs, hbd_md, list_idx_first, ref_idx_first);
        const int8_t ref_idx_sec  = get_ref_frame_idx(rf[1]);
        const int8_t list_idx_sec = get_list_idx(rf[1]);
        ref_pic_list1             = svt_aom_get_ref_pic_buffer(pcs, hbd_md, list_idx_sec, ref_idx_sec);
    }

    // WM disallowed in light-PD1 path so motion mode is always SIMPLE_TRANSLATION
    if (pcs->ppcs->frm_hdr.allow_warped_motion) {
        svt_aom_wm_count_samples(ctx->blk_ptr,
                                 pcs->scs->seq_header.sb_size,
                                 ctx->blk_geom,
                                 ctx->blk_org_x,
                                 ctx->blk_org_y,
                                 cand->ref_frame_type,
                                 pcs,
                                 &cand->num_proj_ref);
    }
    //for light PD1 inter prediction is Luma only for MDS0 and Chroma only for MDS3
    uint32_t            component_mask     = ctx->md_stage == MD_STAGE_0 ? PICTURE_BUFFER_DESC_LUMA_MASK
                       : ctx->lpd1_chroma_comp == COMPONENT_CHROMA       ? PICTURE_BUFFER_DESC_CHROMA_MASK
                       : ctx->lpd1_chroma_comp == COMPONENT_CHROMA_CB    ? PICTURE_BUFFER_DESC_Cb_FLAG
                                                                         : PICTURE_BUFFER_DESC_Cr_FLAG;
    SequenceControlSet *scs                = pcs->scs;
    ScaleFactors        ref0_scale_factors = scs->sf_identity;
    if (!pcs->ppcs->is_not_scaled && ref_pic_list0 != NULL) {
        svt_av1_setup_scale_factors_for_frame(&ref0_scale_factors,
                                              ref_pic_list0->width,
                                              ref_pic_list0->height,
                                              pcs->ppcs->enhanced_pic->width,
                                              pcs->ppcs->enhanced_pic->height);
    }

    ScaleFactors ref1_scale_factors = scs->sf_identity;
    if (!pcs->ppcs->is_not_scaled && ref_pic_list1 != NULL) {
        svt_av1_setup_scale_factors_for_frame(&ref1_scale_factors,
                                              ref_pic_list1->width,
                                              ref_pic_list1->height,
                                              pcs->ppcs->enhanced_pic->width,
                                              pcs->ppcs->enhanced_pic->height);
    }

    av1_inter_prediction_light_pd1(scs,
                                   &mv_unit,
                                   cand->interp_filters,
                                   ctx,
                                   ctx->blk_org_x,
                                   ctx->blk_org_y,
                                   ctx->blk_geom->bwidth,
                                   ctx->blk_geom->bheight,
                                   ref_pic_list0,
                                   ref_pic_list1,
                                   cand_bf->pred,
                                   ctx->blk_geom->org_x,
                                   ctx->blk_geom->org_y,
                                   component_mask,
                                   hbd_md,
                                   &ref0_scale_factors,
                                   &ref1_scale_factors);

    return EB_ErrorNone;
}
EbErrorType svt_aom_inter_pu_prediction_av1(uint8_t hbd_md, ModeDecisionContext *ctx, PictureControlSet *pcs,
                                            ModeDecisionCandidateBuffer *cand_bf) {
    EbErrorType                  return_error  = EB_ErrorNone;
    EbPictureBufferDesc         *ref_pic_list0 = (EbPictureBufferDesc *)NULL;
    EbPictureBufferDesc         *ref_pic_list1 = (EbPictureBufferDesc *)NULL;
    ModeDecisionCandidate *const cand          = cand_bf->cand;
    SequenceControlSet          *scs           = pcs->scs;
    MvReferenceFrame             rf[2];
    av1_set_ref_frame(rf, cand_bf->cand->ref_frame_type);
    int8_t  ref_idx_l0 = get_ref_frame_idx(rf[0]);
    int8_t  ref_idx_l1 = rf[1] == NONE_FRAME ? get_ref_frame_idx(rf[0]) : get_ref_frame_idx(rf[1]);
    uint8_t list_idx0  = get_list_idx(rf[0]);
    uint8_t list_idx1  = rf[1] == NONE_FRAME ? get_list_idx(rf[0]) : get_list_idx(rf[1]);
    assert(list_idx0 < MAX_NUM_OF_REF_PIC_LIST && list_idx1 < MAX_NUM_OF_REF_PIC_LIST);

    const uint8_t is_compound = is_inter_compound_mode(cand->pred_mode);

    Mv mv_0;
    Mv mv_1;
    mv_0.as_int = cand->mv[0].as_int;
    mv_1.as_int = cand->mv[1].as_int;
    MvUnit mv_unit;
    mv_unit.pred_direction = (rf[1] == NONE_FRAME) ? list_idx0 : BI_PRED;
    mv_unit.mv[0]          = mv_0;
    mv_unit.mv[1]          = mv_1;

    if (cand_bf->cand->use_intrabc) {
        svt_aom_get_recon_pic(pcs, &ref_pic_list0, hbd_md);
        uint32_t component_mask = (ctx->uv_ctrls.uv_mode <= CHROMA_MODE_1 && ctx->mds_skip_uv_pred == FALSE)
            ? PICTURE_BUFFER_DESC_FULL_MASK
            : PICTURE_BUFFER_DESC_LUMA_MASK;
        svt_aom_inter_prediction(scs,
                                 pcs,
                                 cand_bf->cand->interp_filters,
                                 ctx->blk_ptr,
                                 cand_bf->cand->ref_frame_type,
                                 &mv_unit,
                                 1, // use_intrabc
                                 SIMPLE_TRANSLATION,
                                 0,
                                 0,
                                 1, // 1 for avg
                                 &cand_bf->cand->interinter_comp,
                                 NULL,
                                 NULL,
                                 NULL,
                                 0,
                                 0,
                                 0,
                                 0,
                                 ctx->blk_org_x,
                                 ctx->blk_org_y,
                                 ctx->blk_geom->bwidth,
                                 ctx->blk_geom->bheight,
                                 ref_pic_list0,
                                 0, // ref_pic_list1,
                                 cand_bf->pred,
                                 ctx->blk_geom->org_x,
                                 ctx->blk_geom->org_y,
                                 component_mask,
                                 hbd_md ? EB_TEN_BIT : EB_EIGHT_BIT,
                                 0); // is_16bit_pipeline

        return return_error;
    }
    if (ref_idx_l0 >= 0) {
        ref_pic_list0 = svt_aom_get_ref_pic_buffer(pcs, hbd_md, list_idx0, ref_idx_l0);
    }

    if (ref_idx_l1 >= 0) {
        ref_pic_list1 = svt_aom_get_ref_pic_buffer(pcs, hbd_md, list_idx1, ref_idx_l1);
    }

    if (pcs->ppcs->frm_hdr.allow_warped_motion && cand->motion_mode != WARPED_CAUSAL) {
        svt_aom_wm_count_samples(ctx->blk_ptr,
                                 pcs->scs->seq_header.sb_size,
                                 ctx->blk_geom,
                                 ctx->blk_org_x,
                                 ctx->blk_org_y,
                                 cand->ref_frame_type,
                                 pcs,
                                 &cand->num_proj_ref);
    }

    uint8_t bit_depth = EB_EIGHT_BIT;
    if (scs->static_config.encoder_bit_depth > EB_EIGHT_BIT && hbd_md)
        bit_depth = scs->static_config.encoder_bit_depth;

    if (cand->motion_mode == WARPED_CAUSAL) {
        uint32_t component_mask = (ctx->uv_ctrls.uv_mode <= CHROMA_MODE_1 && ctx->mds_skip_uv_pred == FALSE)
            ? PICTURE_BUFFER_DESC_FULL_MASK
            : PICTURE_BUFFER_DESC_LUMA_MASK;

        if (ctx->md_stage >= MD_STAGE_1 && cand_bf->valid_pred && component_mask == PICTURE_BUFFER_DESC_FULL_MASK &&
            !ctx->need_hbd_comp_mds3) {
            // The mask generation for DIFFWTD compound mode is done for luma, using luma samples, so must always
            // perform luma prediction if DIFFWTD is used.
            if (is_compound == 0 || cand_bf->cand->interinter_comp.type != COMPOUND_DIFFWTD) {
                component_mask = PICTURE_BUFFER_DESC_CHROMA_MASK;
            }
        }
        NeighborArrayUnit *recon_neigh_y;
        NeighborArrayUnit *recon_neigh_cb;
        NeighborArrayUnit *recon_neigh_cr;

        if (bit_depth == EB_EIGHT_BIT) {
            recon_neigh_y  = ctx->recon_neigh_y;
            recon_neigh_cb = ctx->recon_neigh_cb;
            recon_neigh_cr = ctx->recon_neigh_cr;
        } else {
            recon_neigh_y  = ctx->luma_recon_na_16bit;
            recon_neigh_cb = ctx->cb_recon_na_16bit;
            recon_neigh_cr = ctx->cr_recon_na_16bit;
        }

        svt_aom_warped_motion_prediction(pcs,
                                         &mv_unit,
                                         cand->ref_frame_type,
                                         cand->compound_idx,
                                         &cand->interinter_comp,
                                         ctx->blk_org_x,
                                         ctx->blk_org_y,
                                         ctx->blk_ptr,
                                         ctx->blk_geom,
                                         ref_pic_list0,
                                         ref_pic_list1,
                                         cand_bf->pred,
                                         ctx->blk_geom->org_x,
                                         ctx->blk_geom->org_y,
                                         recon_neigh_y,
                                         recon_neigh_cb,
                                         recon_neigh_cr,
                                         cand,
                                         &cand->wm_params_l0,
                                         &cand->wm_params_l1,
                                         bit_depth,
                                         component_mask,
                                         FALSE);

        return return_error;
    }
    uint32_t interp_filters_0 = cand_bf->cand->interp_filters;
    if (ctx->mds_skip_ifs == FALSE && pcs->ppcs->frm_hdr.interpolation_filter == SWITCHABLE &&
        av1_is_interp_needed(cand_bf, pcs, ctx->blk_geom->bsize)) {
        // If using hybrid MD for HBD content, use 8bit pic for the IFS search
        if (ctx->hbd_md == EB_DUAL_BIT_MD && hbd_md == EB_DUAL_BIT_MD) {
            if (ref_idx_l0 >= 0)
                ref_pic_list0 = svt_aom_get_ref_pic_buffer(pcs, 0, list_idx0, ref_idx_l0);
            if (ref_idx_l1 >= 0)
                ref_pic_list1 = svt_aom_get_ref_pic_buffer(pcs, 0, list_idx1, ref_idx_l1);
        }
        assert(ref_pic_list0 != NULL);
        assert(ref_pic_list1 != NULL);

        interpolation_filter_search(pcs,
                                    ctx,
                                    cand_bf,
                                    mv_unit,
                                    ref_pic_list0,
                                    ref_pic_list1,
                                    ctx->hbd_md == EB_DUAL_BIT_MD ? EB_8_BIT_MD : ctx->hbd_md,
                                    bit_depth);

        if (ctx->hbd_md == EB_DUAL_BIT_MD && hbd_md == EB_DUAL_BIT_MD) {
            if (ref_idx_l0 >= 0)
                ref_pic_list0 = svt_aom_get_ref_pic_buffer(pcs, 1, list_idx0, ref_idx_l0);
            if (ref_idx_l1 >= 0)
                ref_pic_list1 = svt_aom_get_ref_pic_buffer(pcs, 1, list_idx1, ref_idx_l1);
        }
    }
    uint32_t           interp_filters_1 = cand_bf->cand->interp_filters;
    NeighborArrayUnit *recon_neigh_y;
    NeighborArrayUnit *recon_neigh_cb;
    NeighborArrayUnit *recon_neigh_cr;

    if (!hbd_md) {
        recon_neigh_y  = ctx->recon_neigh_y;
        recon_neigh_cb = ctx->recon_neigh_cb;
        recon_neigh_cr = ctx->recon_neigh_cr;
    } else {
        recon_neigh_y  = ctx->luma_recon_na_16bit;
        recon_neigh_cb = ctx->cb_recon_na_16bit;
        recon_neigh_cr = ctx->cr_recon_na_16bit;
    }

    uint32_t component_mask = (ctx->mds_skip_uv_pred || ctx->uv_ctrls.uv_mode == CHROMA_MODE_2)
        ? PICTURE_BUFFER_DESC_LUMA_MASK
        : PICTURE_BUFFER_DESC_FULL_MASK;

    // Skip luma prediction if
    if (ctx->md_stage >= MD_STAGE_1 && cand_bf->valid_pred &&
        interp_filters_0 == interp_filters_1) { // already performed and IFS did not change it
        if (component_mask == PICTURE_BUFFER_DESC_FULL_MASK && !ctx->need_hbd_comp_mds3) {
            // The mask generation for DIFFWTD compound mode is done for luma, using luma samples, so must always
            // perform luma prediction if DIFFWTD is used.
            if (is_compound == 0 || cand_bf->cand->interinter_comp.type != COMPOUND_DIFFWTD) {
                component_mask = PICTURE_BUFFER_DESC_CHROMA_MASK;
            }
        }
    }
    svt_aom_inter_prediction(scs,
                             pcs,
                             cand_bf->cand->interp_filters,
                             ctx->blk_ptr,
                             cand_bf->cand->ref_frame_type,
                             &mv_unit,
                             cand_bf->cand->use_intrabc,
                             cand_bf->cand->motion_mode, // MD
                             // If using 8bit MD for HBD content, can't use pre-computed OBMC to
                             // generate conformant recon
                             ctx->need_hbd_comp_mds3 ? 0 : 1,
                             ctx,
                             cand_bf->cand->compound_idx,
                             &cand_bf->cand->interinter_comp,
                             recon_neigh_y,
                             recon_neigh_cb,
                             recon_neigh_cr,
                             cand->is_interintra_used,
                             cand->interintra_mode,
                             cand->use_wedge_interintra,
                             cand->interintra_wedge_index,
                             ctx->blk_org_x,
                             ctx->blk_org_y,
                             ctx->blk_geom->bwidth,
                             ctx->blk_geom->bheight,
                             ref_pic_list0,
                             ref_pic_list1,
                             cand_bf->pred,
                             ctx->blk_geom->org_x,
                             ctx->blk_geom->org_y,
                             component_mask,
                             hbd_md ? EB_TEN_BIT : EB_EIGHT_BIT,
                             0); // is_16bit_pipeline

    return return_error;
}

EbErrorType svt_aom_inter_pu_prediction_av1_obmc(uint8_t hbd_md, ModeDecisionContext *ctx, PictureControlSet *pcs,
                                                 ModeDecisionCandidateBuffer *cand_bf) {
    EbErrorType return_error = EB_ErrorNone;

    uint32_t component_mask = (ctx->mds_skip_uv_pred || ctx->uv_ctrls.uv_mode == CHROMA_MODE_2)
        ? PICTURE_BUFFER_DESC_LUMA_MASK
        : PICTURE_BUFFER_DESC_FULL_MASK;

    av1_inter_prediction_obmc(
        pcs,
        ctx->blk_ptr,
        // If using 8bit MD for HBD content, can't use pre-computed OBMC to generate conformant recon
        ctx->need_hbd_comp_mds3 ? 0 : 1,
        ctx,
        ctx->blk_org_x,
        ctx->blk_org_y,
        cand_bf->pred,
        ctx->blk_geom->org_x,
        ctx->blk_geom->org_y,
        component_mask,
        hbd_md ? EB_TEN_BIT : EB_EIGHT_BIT,
        0); // is_16bit_pipeline

    return return_error;
}
