/*
* Copyright(c) 2019 Intel Corporation
*
* This source code is subject to the terms of the BSD 2 Clause License and
* the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
* was not distributed with this source code in the LICENSE file, you can
* obtain it at https://www.aomedia.org/license/software-license. If the Alliance for Open
* Media Patent License 1.0 was not distributed with this source code in the
* PATENTS file, you can obtain it at https://www.aomedia.org/license/patent-license.
*/

#ifndef EbModeDecision_h
#define EbModeDecision_h

#include "EbDefinitions.h"
#include "EbUtility.h"
#include "EbPictureControlSet.h"
#include "EbCodingUnit.h"
#include "EbPredictionUnit.h"
#include "EbPictureBufferDesc.h"
#include "EbAdaptiveMotionVectorPrediction.h"
#include "EbPictureOperators.h"
#include "EbNeighborArrays.h"
#include "EbObject.h"

#ifdef __cplusplus
extern "C" {
#endif
#define ENABLE_AMVP_MV_FOR_RC_PU 0
#define MAX_MB_PLANE 3
#define MAX_MPM_CANDIDATES 3
#define MERGE_PENALTY 10

// Create incomplete struct definition for the following function pointer typedefs
struct ModeDecisionCandidateBuffer;
struct ModeDecisionContext;

/**************************************
    * Mode Decision Candidate
    **************************************/
typedef struct ModeDecisionCandidate {
    Mv                     mv[MAX_NUM_OF_REF_PIC_LIST];
    Mv                     pred_mv[MAX_NUM_OF_REF_PIC_LIST];
    PaletteInfo           *palette_info;
    uint32_t               interp_filters;
    EbWarpedMotionParams   wm_params_l0;
    EbWarpedMotionParams   wm_params_l1;
    InterInterCompoundData interinter_comp;
    TxType                 transform_type[MAX_TXB_COUNT];
    TxType                 transform_type_uv;
    uint16_t               num_proj_ref;
    uint8_t                palette_size[PLANE_TYPES];

    CandClass      cand_class;
    PredictionMode pred_mode;
    uint8_t        skip_mode; // skip mode_info + coeff. as defined in section 6.10.10 of the av1 text
    Bool           skip_mode_allowed;
    uint8_t        use_intrabc;

    // Intra Mode
    int8_t           angle_delta[PLANE_TYPES]; // [-3,3]
    uint8_t          filter_intra_mode;
    UvPredictionMode intra_chroma_mode; // INTRA only
    uint8_t          cfl_alpha_idx; // Index of the alpha Cb and alpha Cr combination
    uint8_t          cfl_alpha_signs; // Joint sign of alpha Cb and alpha Cr

    // Inter Mode
    uint8_t        ref_frame_type;
    uint8_t        drl_index;
    MotionMode     motion_mode;
    uint8_t        tx_depth;
    uint8_t        compound_idx;
    uint8_t        comp_group_idx;
    InterIntraMode interintra_mode;
    uint8_t        is_interintra_used;
    uint8_t        use_wedge_interintra;
    int8_t         interintra_wedge_index;
} ModeDecisionCandidate;

/**************************************
 * Function Ptrs Definitions
 **************************************/
typedef EbErrorType (*EbPredictionFunc)(uint8_t hbd_md, struct ModeDecisionContext *ctx, PictureControlSet *pcs,
                                        struct ModeDecisionCandidateBuffer *cand_bf);
typedef uint64_t (*EbFastCostFunc)(PictureControlSet *pcs, struct ModeDecisionContext *ctx,
                                   struct ModeDecisionCandidateBuffer *cand_bf, uint64_t lambda,
                                   uint64_t luma_distortion, uint64_t chroma_distortion);

typedef EbErrorType (*EbAv1FullCostFunc)(PictureControlSet *pcs, struct ModeDecisionContext *ctx,
                                         struct ModeDecisionCandidateBuffer *cand_bf, BlkStruct *blk_ptr,
                                         uint64_t y_distortion[DIST_TOTAL][DIST_CALC_TOTAL],
                                         uint64_t cb_distortion[DIST_TOTAL][DIST_CALC_TOTAL],
                                         uint64_t cr_distortion[DIST_TOTAL][DIST_CALC_TOTAL], uint64_t lambda,
                                         uint64_t *y_coeff_bits, uint64_t *cb_coeff_bits, uint64_t *cr_coeff_bits,
                                         BlockSize bsize);
/**************************************
    * Mode Decision Candidate Buffer
    **************************************/
typedef struct IntraChromacand_bf {
    uint32_t             mode;
    uint64_t             cost;
    uint64_t             distortion;
    EbPictureBufferDesc *prediction_ptr;
    EbPictureBufferDesc *residual_ptr;
} IntraChromacand_bf;

/**************************************
    * Mode Decision Candidate Buffer
    **************************************/
typedef struct ModeDecisionCandidateBuffer {
    EbDctor dctor;
    // Candidate Ptr
    ModeDecisionCandidate *cand;

    // Video Buffers
    EbPictureBufferDesc *pred;
    EbPictureBufferDesc *rec_coeff;
    EbPictureBufferDesc *residual;
    EbPictureBufferDesc *quant;

    // *Note - We should be able to combine the rec_coeff & recon_ptr pictures (they aren't needed at the same time)
    EbPictureBufferDesc *recon;

    // Costs
    uint64_t *fast_cost;
    uint64_t *full_cost;
    uint64_t *full_cost_ssim;
    uint64_t  fast_luma_rate;
    uint64_t  fast_chroma_rate;
    uint64_t  total_rate;
    uint32_t  luma_fast_dist;
    uint32_t  full_dist;
    uint16_t  cnt_nz_coeff;
    uint16_t  eob[MAX_MB_PLANE][MAX_TXB_COUNT];
    int32_t   quantized_dc[MAX_MB_PLANE][MAX_TXB_COUNT];
    uint8_t   block_has_coeff;
    uint8_t   u_has_coeff;
    uint8_t   v_has_coeff;
    uint16_t  y_has_coeff;
    bool
        valid_pred; // The prediction of SIMPLE_TRANSLATION is not valid when OBMC face-off is used (where OBMC will re-use the pred buffer of SIMPLE_TRANSLATION)
} ModeDecisionCandidateBuffer;

/**************************************
    * Extern Function Declarations
    **************************************/
extern EbErrorType svt_aom_mode_decision_cand_bf_ctor(ModeDecisionCandidateBuffer *buffer_ptr, EbBitDepth max_bitdepth,
                                                      uint8_t sb_size, uint32_t buffer_mask,
                                                      EbPictureBufferDesc *temp_residual,
                                                      EbPictureBufferDesc *temp_recon_ptr, uint64_t *fast_cost,
                                                      uint64_t *full_cost, uint64_t *full_cost_ssim_ptr);

extern EbErrorType svt_aom_mode_decision_scratch_cand_bf_ctor(ModeDecisionCandidateBuffer *buffer_ptr, uint8_t sb_size,
                                                              EbBitDepth max_bitdepth);

uint32_t product_full_mode_decision_light_pd0(struct ModeDecisionContext *ctx, BlkStruct *blk_ptr,
                                              ModeDecisionCandidateBuffer **buffer_ptr_array);
void     svt_aom_product_full_mode_decision_light_pd1(struct ModeDecisionContext *ctx, BlkStruct *blk_ptr,
                                                      PictureControlSet *pcs, uint32_t sb_addr,
                                                      ModeDecisionCandidateBuffer *cand_bf);
uint32_t svt_aom_product_full_mode_decision(struct ModeDecisionContext *ctx, BlkStruct *blk_ptr, PictureControlSet *pcs,
                                            uint32_t sb_addr, ModeDecisionCandidateBuffer **buffer_ptr_array,
                                            uint32_t candidate_total_count, uint32_t *best_candidate_index_array);
extern uint8_t svt_aom_wm_motion_refinement(PictureControlSet *pcs, struct ModeDecisionContext *ctx,
                                            ModeDecisionCandidateBuffer *cand_bf, ModeDecisionCandidate *can,
                                            uint8_t list_idx, int early_exit);
extern uint8_t svt_aom_obmc_motion_refinement(PictureControlSet *pcs, struct ModeDecisionContext *ctx,
                                              ModeDecisionCandidate *cand, uint8_t ref_list_idx, int refine_level);
EbErrorType    generate_md_stage_0_cand(SuperBlock *sb_ptr, struct ModeDecisionContext *ctx,
                                        uint32_t *fast_candidate_total_count, PictureControlSet *pcs);
void           generate_md_stage_0_cand_light_pd1(struct ModeDecisionContext *ctx, uint32_t *fast_candidate_total_count,
                                                  PictureControlSet *pcs);
EbErrorType    generate_md_stage_0_cand_light_pd0(struct ModeDecisionContext *ctx, uint32_t *fast_candidate_total_count,
                                                  PictureControlSet *pcs);

static INLINE int svt_aom_is_interintra_allowed_bsize(const BlockSize bsize) {
    return (bsize >= BLOCK_8X8) && (bsize <= BLOCK_32X32);
}

static INLINE int svt_aom_is_interintra_allowed_mode(const PredictionMode mode) {
    return (mode >= SINGLE_INTER_MODE_START) && (mode < SINGLE_INTER_MODE_END);
}

static INLINE int svt_aom_is_interintra_allowed_ref(const MvReferenceFrame rf[2]) {
    return (rf[0] > INTRA_FRAME) && (rf[1] <= INTRA_FRAME);
}
int svt_is_interintra_allowed(uint8_t enable_inter_intra, BlockSize bsize, PredictionMode mode,
                              const MvReferenceFrame ref_frame[2]);
int svt_aom_filter_intra_allowed_bsize(uint8_t enable_filter_intra, BlockSize bs);
int svt_aom_filter_intra_allowed(uint8_t enable_filter_intra, BlockSize bsize, uint8_t palette_size, uint32_t mode);

uint8_t svt_aom_is_me_data_present(uint32_t me_block_offset, uint32_t me_cand_offset, const MeSbResults *me_results,
                                   uint8_t list_idx, uint8_t ref_idx);

int32_t    svt_aom_have_newmv_in_inter_mode(PredictionMode mode);
uint8_t    svt_aom_get_max_drl_index(uint8_t refmvCnt, PredictionMode mode);
MotionMode svt_aom_obmc_motion_mode_allowed(const PictureControlSet *pcs, struct ModeDecisionContext *ctx,
                                            const BlockSize bsize, uint8_t situation, MvReferenceFrame rf0,
                                            MvReferenceFrame rf1, PredictionMode mode);
/* For intra prediction, the chroma transform type may not follow the luma type.
This function will return the intra chroma TX type to be used, which is based on TX size and chroma mode. */
TxType svt_aom_get_intra_uv_tx_type(UvPredictionMode pred_mode_uv, TxSize tx_size, int32_t reduced_tx_set);
void   svt_aom_set_tuned_blk_lambda(struct ModeDecisionContext *ctx, PictureControlSet *pcs);

typedef EbErrorType (*EB_INTRA_4x4_FAST_LUMA_COST_FUNC)(struct ModeDecisionContext *ctx, uint32_t pu_index,
                                                        ModeDecisionCandidateBuffer *cand_bf, uint64_t luma_distortion,
                                                        uint64_t lambda);

typedef EbErrorType (*EB_INTRA_4x4_FULL_LUMA_COST_FUNC)(ModeDecisionCandidateBuffer *cand_bf, uint64_t *y_distortion,
                                                        uint64_t lambda, uint64_t *y_coeff_bits,
                                                        uint32_t transform_size);

typedef EbErrorType (*EB_FULL_NXN_COST_FUNC)(PictureControlSet *pcs, ModeDecisionCandidateBuffer *cand_bf, uint32_t qp,
                                             uint64_t *y_distortion, uint64_t *cb_distortion, uint64_t *cr_distortion,
                                             uint64_t lambda, uint64_t lambda_chroma, uint64_t *y_coeff_bits,
                                             uint64_t *cb_coeff_bits, uint64_t *cr_coeff_bits, uint32_t transform_size);
struct CodingLoopContext_s;
/*
      |-------------------------------------------------------------|
      | ref_idx          0            1           2            3    |
      | List0            LAST        LAST2        LAST3        GOLD |
      | List1            BWD         ALT2         ALT               |
      |-------------------------------------------------------------|
    */
#define INVALID_REF 0xF
extern MvReferenceFrame svt_get_ref_frame_type(uint8_t list, uint8_t ref_idx);
int                     svt_aom_get_sad_per_bit(int qidx, EbBitDepth is_hbd);

int svt_av1_allow_palette(int allow_palette, BlockSize bsize);
#ifdef __cplusplus
}
#endif
#endif // EbModeDecision_h
