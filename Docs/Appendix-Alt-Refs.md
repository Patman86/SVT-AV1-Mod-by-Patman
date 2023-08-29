[Top level](../README.md)

# ALTREF and Overlay Pictures

## 1. ALTREF pictures

### Introduction

ALTREFs are non-displayable pictures that are used as reference for other
pictures. They are usually constructed using several source frames but can hold
any type of information useful for compression and the given use-case. In the
current version of SVT-AV1, temporal filtering of adjacent video frames is used
to construct some of the ALTREF pictures. The resulting temporally filtered
pictures will be encoded in place of or in addition to the original source
pictures. This methodology is especially useful for source pictures that
contain a high level of noise since the temporal filtering process produces
reference pictures with reduced noise level.

Temporal filtering is currently applied to the base layer picture and can also
be applied to layer 1 pictures of each mini-GOP (e.g. source frame positions 16
and 8 respectively in a mini-GOP in a 5-layer hierarchical prediction
structure). In addition, filtering of the key-frames and intra-only frames is
also supported.

The diagram in Figure 1 illustrates the use of five adjacent pictures: Two
past, two future and one central picture, in order to produce a single filtered
picture. Motion estimation is applied between the central picture and each
future or past pictures generating multiple motion-compensated predictions.
These are then combined using adaptive weighting (filtering) to produce the
final noise-reduced picture.

![altref_fig1](./img/altref_fig1.png)

##### Fig. 1. Example of motion estimation for temporal filtering in a temporal window consisting of 5 adjacent pictures

Since temporal filtering makes use of a number of adjacent frames, the Look
Ahead Distance (lad_mg_pictures) needs to be incremented by the number of
future frames used for ALTREF temporal filtering. When applying temporal
filtering to ALTREF pictures, an Overlay picture might be necessary. This
Overlay picture corresponds to the same original source picture but uses only
the temporally filtered version of the source picture as a reference to
reconstruct the original picture.

### Description of the temporal filtering control

Various signals are used to specify the temporal filtering settings and are
described in Table 1 below. The settings could be different based on the frame
type; however, the same set of signals is used for all frame types. The
temporal filtering flow diagram in Figure 2 below further explains how and
where each of the defined signals is used. These parameters are decided as a
function of the encoder preset (enc_mode).

|**Category**|**Signal(s)**|**Description**|
| --- | --- | --- |
|**Filtering**|Enabled|Specifies whether the current input will be filtered or not (0: OFF, 1: ON).|
|**Filtering**|do_chroma|Specifies whether the U&V planes will be filered or not (0: filter all planes, 1: filter Y plane only).|
|**Filtering**|use_medium_filter|Specifies whether the weights generation will use approximations or not (0: do not use approximations, 1: per-block weights derivation, use an approximated exponential & log, use an approximated noise level).|
|**Filtering**|use_fast_filter|Specifies whether the weights derivation will use the distance factor (MV-based correction) and the 5x5 window error or not (0: OFF, 1: ON).|
|**Filtering**|use_fixed_point|Specifies noise-level-estimation and filtering precision (0: use float/double precision, 1: use fixed point precision).|
|**Number of reference frame(s)**|num_past_pics|Specifies the default number of frame(s) from past.|
|**Number of reference frame(s)**|num_future_pics|Specifies the default number of frame(s) from future.|
|**Number of reference frame(s)**|noise_adjust_past_pics|Specifies whether num_past_pics will be incremented or not based on the noise level of the central frame (0: OFF or 1: ON).|
|**Number of reference frame(s)**|noise_adjust_future_pics|Specifies whether num_future_pics will be incremented or not based on the noise level of the central frame (0: OFF or 1: ON).|
|**Number of reference frame(s)**|use_intra_for_noise_est|Specifies whether to use the key-frame noise level for all inputs or to re-compute the noise level for each input.|
|**Number of reference frame(s)**|activity_adjust_th|Specifies whether `num_past_pics` and `num_future_pics` will be decremented or not based on the activity of the outer reference frame(s) compared to the central frame (∞: OFF, else remove the reference frame if the cumulative differences between the histogram bins of the central frame and the histogram bins of the reference frame is higher than `activity_adjust_th`.|
|**Number of reference frame(s)**|max_num_past_pics|Specifies the maximum number of frame(s) from past (after all adjustments).|
|**Number of reference frame(s)**|max_num_future_pics|Specifies the maximum number of frame(s) from future (after all adjustments).|
|**Motion search**|hme_me_level|Specifies the accuracy of the ME search (note that ME performs a HME search, then a Full-Pel search).|
|**Motion search**|half_pel_mode|Specifies the accuracy of the Half-Pel search (0: OFF, 1: perform refinement for the 8 neighboring positions, 2/3: perform refinement for the 2 horizontal-neighboring positions and for the 2 vertical-neighboring positions, but not for all the 4 diagonal-neighboring positions = function (horizontal & vertical distortions).|
|**Motion search**|quarter_pel_mode|Specifies the accuracy of the Quarter-Pel search (0: OFF, 1: perform refinement for the 8 neighboring positions, 2/3: perform refinement for the 2 horizontal-neighboring positions and for the 2 vertical-neighboring positions, but not for all the 4 diagonal-neighboring positions = function (horizontal & vertical distortions).|
|**Motion search**|eight_pel_mode|Specifies the accuracy of the Eight-Pel search (0: OFF, 1: perform refinement for the 8 neighboring positions).|
|**Motion search**|use_8bit_subpel|Specifies whether the Sub-Pel search for a 10bit input will be performed in 8bit resolution (0: OFF, 1: ON, NA if 8bit input).|
|**Motion search**|avoid_2d_qpel|Specifies whether the Sub-Pel positions that require a 2D interpolation will be tested or not (0: OFF, 1: ON, NA if 16x16 block or if the Sub-Pel mode is set to 1).|
|**Motion search**|use_2tap|Specifies the Sub-Pel search filter type (0: regular, 1: bilinear, NA if 16x16 block or if the Sub-Pel mode is set to 1).|
|**Motion search**|sub_sampling_shift|Specifies whether sub-sampled input/prediction will be used at the distortion computation of the Sub-Pel search.|
|**Motion search**|pred_error_32x32_th|Specifies the 32x32 prediction error (after subpel) under which the subpel for the 16x16 block(s) is bypassed.|
|**Motion search**|tf_me_exit_th|Specifies whether to exit ME after HME or not (0: perform both HME and Full-Pel search, else if the HME distortion is less than me_exit_th then exit after HME (i.e. do not perform the Full-Pel search), NA if use_fast_filter is set 0).|
|**Motion search**|use_pred_64x64_only_th|Specifies whether to perform Sub-Pel search for only the 64x64 block or to use default size(s) (32x32 or/and 16x16) (∞: perform Sub-Pel search for default size(s), else if the deviation between the 64x64 ME distortion and the sum of the 4 32x32 ME distortions is less than use_pred_64x64_only_th then perform Sub-Pel search for only the 64x64 block, NA if use_fast_filter is set 0).|

### Temporal filtering data flow

The block diagram in Figure 2 outlines the flow of the temporal filtering operations.

[altref_block_diagram](./img/altref_newfig2.pdf)

##### Fig. 2. Block diagram of the temporal filtering operations. (image is too large to display in md, please click the link to see)


## Description of the main modules

### Source picture noise estimation

In order to decide temporal window length according to the content
characteristics, the amount of noise is estimated from the central source
picture. The algorithm considered is based on a simplification of the algorithm
proposed in [1]. The standard deviation (sigma) of the noise is estimated using
the Laplacian operator. Pixels that belong to an edge (i.e. as determined by
how the magnitude of the Sobel gradients compare to a predetermined threshold),
are not considered in the computation. The current noise estimation considers
only the luma component. When `use_intra_for_noise_est` is set to 1, the noise
level of the I-frame will be used for ALTREF_FRAME or ALTREF2_FRAME.

### Building the list of source pictures

As mentioned previously, the temporal filtering algorithm uses multiple frames
to generate a temporally denoised or filtered picture at the central picture
location. If enough pictures are available in the list of source picture
buffers, the number of pictures used will generally be given by the
num_past_pics and num_future_pics in addition to the central picture, unless
not enough frames are available (e.g. end of sequence).

The number of pictures will be first increased based on the noise level of the
central picture. Basically, the lower the noise of the central picture, the
widerthe temporal window (+3 on each side if noise <0.5, +2 on each side if
noise < 1.0, and +1 on each if noise < 2.0). Both sides of the window could be
adjusted or just one side depending on noise_adjust_past_pics and
noise_adjust_future_pics.

In order to account for illumination changes, which might compromise the
quality of the temporally filtered picture, an adjustment of both
`num_past_pics` and `num_future_pics` is conducted to remove cases where a
significant illumination change is found in the defined temporal window. This
algorithm first computes and accumulates the absolute difference between the
luminance histograms of adjacent pictures in the temporal window, starting from
the first past picture to the last past picture and from the first future
picture to the last future picture. Then, depending on a threshold, ahd_th, if
the cumulative difference is high enough, edge pictures will be removed. The
current threshold is chosen based on the picture width and height:ahd_th =
(width * height) * activity_adjust_th / 100

After this step, the list of pictures to use for the temporal filtering is
ready. However, given that the number of past and future frames can be
different, the index of the central picture needs to be known.

### Block-based processing

The central picture is split into 64x64 pixel non-overlapping blocks. For each
block, (num_past_pics + num_future_pics )–, motion-compensated predictions will
be determined from the adjacent frames and weighted in order to generate a
final filtered block. All blocks are then combined to build the final filtered
picture.

### Block-based motion search and compensation

The motion search consists of three steps: (1) Hierarchical Motion Estimation
(HME), (2) Full-Pel search, and (3) Sub-Pel search, and performed for only the
Luma plane.

HME is performed for each single 64x64-block, while Full-Pel search is
performed for the 85 square blocks between 8x8 and 64x64, and the Sub-Pel
search (using regular or bilinear as filter type depending on use_2tap) is
performed for only the 4 32x32-blocks and the 16 16x16-blocks.

After obtaining the motion information, an inter-depth decision between the 4
32x32-blocks and the 16 16x16-blocks is performed towards a final partitioning
for the 64x64. The latter will be considered at the final compensation (using
sharp as filter type and for all planes).

However, if the 64x64 distortion after HME is less than tf_me_exit_th, then the
Full_Pel search is bypassed and Sub-Pel search/final compensation is performed
for only the 64x64.

Also, Sub-Pel search/final compensation is performed for only 64x64 blocks, if
the deviation between the 64x64 ME distortion and the 4 32x32 ME distortions
(after the Full-Pel search) is less than use_pred_64x64_only_th.


### Compute the Decay Factor

The decay factor (`tf_decay_factor`) is derived per block/per component and
will be used at the sample-based filtering operations.

```tf_decay_factor = 2 * n_decay * n_decay * q_decay * s_decay```

The noise-decay (`n_decay`) is mainly an increasing function of the input noise
level, but is also adjusted depending on the filtering method
(`use_fast_filter`), the input resolution, and the input QP; where a higher
noise level implies a larger n_decay value and a stronger filtering. The
computations of `n_decay` are simplified when `use_fixed_point` or
`use_fast_filter` is set to 1.

The QP-decay (`q_decay`) is an increasing function of the input QP. For a high
QP, the quantization leads to a higher loss of information, and thus a stronger
filtering is less likely to distort the encoded quality, while a stronger
filtering could reduce bit rates. For a low QP, more details are expected to be
retained. Filtering is thus more conservative.

The strength decay (`s_decay`) is a function of the filtering strength that is
set in the code.

### Temporal filtering of the co-located motion compensated blocks

After multiplying each pixel of the co-located 64x64 blocks by the respective
weight, the blocks are then added and normalized to produce the final output
filtered block. These are then combined with the rest of the blocks in the
frame to produce the final temporally filtered picture.

The process of generating one filtered block is illustrated in diagram of
Figure 3. In this example, only 3 pictures are used for the temporal filtering
(`num_past_pics = 1` and `num_future_pics = 1`). Moreover, the values of the
filter weights are used for illustration purposes only and are in the range
{0,32}.

![altref_fig2](./img/altref_fig2.png)

##### Fig. 2. Example of the process of generating the filtered block from the predicted blocks of adjacent picture and their corresponding pixel weights.

## 2. Implementation of the algorithm

**Inputs**: list of picture buffer pointers to use for filtering, location of
central picture, initial filtering strength

**Outputs**: the resulting temporally filtered picture, which replaces the
location of the central pictures in the source buffer. The original source
picture is stored in an additional buffer.

**Control flags**:

#### Table 2: Control signals/flags for the ALTREF frames feature.
| **Flag**         | **Level**     |
| ---------------- | ------------- |
| tf-controls      | Sequence      |
| enable-overlays  | Sequence      |


### Implementation details

The current implementation supports 8-bit and 10-bit sources as well as 420, 422 and 444 chroma sub-sampling.
Moreover, in addition to the C versions,
SIMD implementations of some of the more computationally demanding functions are also available.

Most of the variables and structures used by the temporal filtering
process are located at the picture level, in the PictureControlSet (PCS)
structure. For example, the list of pictures is stored in the
```temp_filt_pcs_list``` pointer array.

For purposes of quality metrics computation, the original source picture
is stored in ```save_enhanced_picture_ptr``` and
```save_enhanced_picture_bit_inc_ptr``` (for high bit-depth content)
located in the PCS.

The current implementation disables temporal filtering on key-frames if
the source has been classified as screen content (```sc_content_detected```
in the PCS is 1).

Due to the fact that HME is open-loop, which means it operates on the
source pictures, HME can only use the source picture which is going to
be filtered after the filtering process has been finalized. The strategy
for synchronizing the processing of the pictures for this case is
similar to the one employed for the determination of the prediction
structure in the Picture Decision Process. The idea is to write to a
queue, the ```picture_decision_results_input_fifo_ptr```, which is
consumed by the HME process.

### Memory allocation

Three uint8_t or uint16_t buffers of size 64x64x3 are allocated: the
accumulator, predictor and counter. In addition, an extra picture buffer (or
two in case of high bit-depth content) is allocated to store the original
source. Finally, a temporary buffer is allocated for high-bit depth sources,
due to the way high bit-depth sources are stored in the encoder implementation
(see sub-section on high bit-depth considerations).

### High bit-depth considerations

For some of the operations, different but equivalent functions are
implemented for 8-bit and 10-bit sources. For 8-bit sources, uint8_t
pointers are used, while for 10-bit sources, uint16_t pointers are
used. In addition, the current implementation stores the high bit-depth
sources in two separate uint8_t buffers in the EbPictureBufferDesc
structure, for example, ```buffer_y``` for the luma 8 MSB and
```buffer_bit_inc_y``` for the luma LSB per pixel (2 in case of 10-bit).
Therefore, prior to applying the temporal filtering, in case of 10-bit
sources, a packing operation converts the two 8-bit buffers into a
single 16-bit buffer. Then, after the filtered picture is obtained, the
reverse unpacking operation is performed.

### Multi-threading

The filtering algorithm operates independently in units of 64x64 blocks
and is currently multi-threaded. The number of threads used is
controlled by the variable ```tf_segment_column_count```, which depending
the resolution of the source pictures, will allocate more or less
threads for this task. Each thread will process a certain number of
blocks.

Most of the filtering steps are multi-threaded, except the
pre-processing steps: packing (in case of high bit-depth sources) and
unpacking, estimation of noise, adjustment of strength, padding and
copying of the original source buffers. These steps are protected by a
mutex, ```temp_filt_mutex```, and a binary flag, ```temp_filt_prep_done``` in
the PCS structure.

## Signaling

If the temporally filtered picture location is of type ```ALTREF_FRAME``` or
```ALTREF2_FRAME```, the frame should not be displayed with the
```show_existing_frame``` strategy and should contain an associated Overlay
picture. In addition, the frame has the following field values in the
frame header OBU:

  - ```show_frame = 0```

  - ```showable_frame = 0```

  - ```order_hint``` = the index that corresponds to the central picture of the ALTREF frame

In contrast, the temporally filtered key-frame will have ```showable_frame```
= 1 and no Overlay picture.

## 5. Overlay picture
According to current SVT-AV1 implementation, if high level control
```enable_overlays = 1```, Overlay picture will be used to associate with
ALTREF picture with ```temporal_layer_index == 0```.

In resource_coordination, each picture (except the first picture) will be associated
with an additional potential Overlay picture.

In picture_analysis function, the processing for potential Overlay picture is
skipped because Overlay picture shares the same results as the associated
ALTREF picture.

In picture_decision function, the Overlay picture will not be used to update the
picture_decision_reorder_queue. When GOP is decided, the position of ALTREF
and Overlay pictures is clear, then the additional potential Overlay picture of
non-ALTREF will be released, and the Overlay picture of ALTREF will be initiated.

Overlay picture will not be used as reference, and Overlay picture will only
reference the associated ALTREF picture with the same picture number.

In set_frame_display_params function, ```frm_hdr->show_frame = EB_TRUE``` and
```pcs->has_show_existing = EB_FALSE``` for Overlay picture, so that the
associate ALTREF picture will not be displayed, and the reconstructed Overlay
picture will be displayed instead.

Consider the example of a five-layer prediction structure shown in Figure 4 below.
The ALTREF and Overlay picture settings are shown in Table 3.

![image1](./img/image1.png)

#### Figure 4. Example of a five-layer prediction structure.

**Example when picture 16 is ALTREF**:

| **Key Point**    | **ALTREF Picture** | **Overlay Picture** |
| ---------------- | -------------      | ------------        |
| picture_number   | 16                 | 16                  |
| is_alt_ref       | 1                  | 0                   |
| is_overlay       | 0                  | 1                   |
| show_frame       | 0                  | 1                   |
| slice_type       | B_SLICE            | P_SLICE             |


## Notes

The feature settings that are described in this document were compiled at
v1.7.0 of the code and may not reflect the current status of the code. The
description in this document represents an example showing  how features would
interact with the SVT architecture. For the most up-to-date settings, it's
recommended to review the section of the code implementing this feature.

## References

<a name = "ref-1"> </a>
\[1\] Tai, Shen-Chuan, and Shih-Ming Yang. "A fast method for image
noise estimation using Laplacian operator and adaptive edge detection."
In *2008 3rd International Symposium on Communications, Control and
Signal Processing*, pp. 1077-1081. IEEE, 2008.
