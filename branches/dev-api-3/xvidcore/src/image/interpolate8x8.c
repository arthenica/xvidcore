/**************************************************************************
 *
 *	XVID MPEG-4 VIDEO CODEC
 *	8x8 block-based halfpel interpolation
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *************************************************************************/

/**************************************************************************
 *
 *	History:
 *
 *  05.10.2002	new bilinear and qpel interpolation code - Isibaar
 *	27.12.2001	modified "compensate_halfpel"
 *	05.11.2001	initial version; (c)2001 peter ross <pross@cs.rmit.edu.au>
 *
 *************************************************************************/


#include "../portab.h"
#include "interpolate8x8.h"

// function pointers
INTERPOLATE8X8_PTR interpolate8x8_halfpel_h;
INTERPOLATE8X8_PTR interpolate8x8_halfpel_v;
INTERPOLATE8X8_PTR interpolate8x8_halfpel_hv;

INTERPOLATE8X8_AVG2_PTR interpolate8x8_avg2;
INTERPOLATE8X8_AVG4_PTR interpolate8x8_avg4;

INTERPOLATE8X8_LOWPASS_PTR interpolate8x8_lowpass_h;
INTERPOLATE8X8_LOWPASS_PTR interpolate8x8_lowpass_v;
INTERPOLATE8X8_LOWPASS_HV_PTR interpolate8x8_lowpass_hv;

INTERPOLATE8X8_6TAP_LOWPASS_PTR interpolate8x8_6tap_lowpass_h;
INTERPOLATE8X8_6TAP_LOWPASS_PTR interpolate8x8_6tap_lowpass_v;

void interpolate8x8_avg2_c(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, const uint32_t stride, const uint32_t rounding)
{
    int32_t i;
	const int32_t round = 1 - rounding;

    for(i = 0; i < 8; i++)
    {
        dst[0] = (src1[0] + src2[0] + round) >> 1;
        dst[1] = (src1[1] + src2[1] + round) >> 1;
        dst[2] = (src1[2] + src2[2] + round) >> 1;
        dst[3] = (src1[3] + src2[3] + round) >> 1;
        dst[4] = (src1[4] + src2[4] + round) >> 1;
        dst[5] = (src1[5] + src2[5] + round) >> 1;
        dst[6] = (src1[6] + src2[6] + round) >> 1;
        dst[7] = (src1[7] + src2[7] + round) >> 1;

        dst += stride;
        src1 += stride;
        src2 += stride;
    }
}

void interpolate8x8_avg4_c(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, const uint8_t *src3, const uint8_t *src4, const uint32_t stride, const uint32_t rounding)
{
    int32_t i;
	const int32_t round = 2 - rounding;

    for(i = 0; i < 8; i++)
    {
        dst[0] = (src1[0] + src2[0] + src3[0] + src4[0] + round) >> 2;
        dst[1] = (src1[1] + src2[1] + src3[1] + src4[1] + round) >> 2;
        dst[2] = (src1[2] + src2[2] + src3[2] + src4[2] + round) >> 2;
        dst[3] = (src1[3] + src2[3] + src3[3] + src4[3] + round) >> 2;
        dst[4] = (src1[4] + src2[4] + src3[4] + src4[4] + round) >> 2;
        dst[5] = (src1[5] + src2[5] + src3[5] + src4[5] + round) >> 2;
        dst[6] = (src1[6] + src2[6] + src3[6] + src4[6] + round) >> 2;
        dst[7] = (src1[7] + src2[7] + src3[7] + src4[7] + round) >> 2;
        
		dst += stride;
        src1 += stride;
        src2 += stride;
        src3 += stride;
        src4 += stride;
    }
}

// dst = interpolate(src)

void
interpolate8x8_halfpel_h_c(uint8_t * const dst,
						   const uint8_t * const src,
						   const uint32_t stride,
						   const uint32_t rounding)
{
	uint32_t i, j;

	for (j = 0; j < 8; j++) {
		for (i = 0; i < 8; i++) {

			int16_t tot =
				(int32_t) src[j * stride + i] + (int32_t) src[j * stride + i +
															  1];

			tot = (int32_t) ((tot + 1 - rounding) >> 1);
			dst[j * stride + i] = (uint8_t) tot;
		}
	}
}



void
interpolate8x8_halfpel_v_c(uint8_t * const dst,
						   const uint8_t * const src,
						   const uint32_t stride,
						   const uint32_t rounding)
{
	uint32_t i, j;

	for (j = 0; j < 8; j++) {
		for (i = 0; i < 8; i++) {
			int16_t tot = src[j * stride + i] + src[j * stride + i + stride];

			tot = ((tot + 1 - rounding) >> 1);
			dst[j * stride + i] = (uint8_t) tot;
		}
	}
}


void
interpolate8x8_halfpel_hv_c(uint8_t * const dst,
							const uint8_t * const src,
							const uint32_t stride,
							const uint32_t rounding)
{
	uint32_t i, j;

	for (j = 0; j < 8; j++) {
		for (i = 0; i < 8; i++) {
			int16_t tot =
				src[j * stride + i] + src[j * stride + i + 1] +
				src[j * stride + i + stride] + src[j * stride + i + stride +
												   1];
			tot = ((tot + 2 - rounding) >> 2);
			dst[j * stride + i] = (uint8_t) tot;
		}
	}
}

/*************************************************************
 * QPEL STUFF STARTS HERE                                    *
 *************************************************************/

#define CLIP(X,A,B) (X < A) ? (A) : ((X > B) ? (B) : (X))

void interpolate8x8_6tap_lowpass_h_c(uint8_t *dst, uint8_t *src, int32_t stride, int32_t rounding)
{
    int32_t i;
	uint8_t round_add = 16 - rounding;

    for(i = 0; i < 8; i++)
    {

        dst[0] = CLIP((((src[-2] + src[3]) + 5 * (((src[0] + src[1])<<2) - (src[-1] + src[2])) + round_add) >> 5), 0, 255);
        dst[1] = CLIP((((src[-1] + src[4]) + 5 * (((src[1] + src[2])<<2) - (src[0] + src[3])) + round_add) >> 5), 0, 255);
        dst[2] = CLIP((((src[0] + src[5]) + 5 * (((src[2] + src[3])<<2) - (src[1] + src[4])) + round_add) >> 5), 0, 255);
        dst[3] = CLIP((((src[1] + src[6]) + 5 * (((src[3] + src[4])<<2) - (src[2] + src[5])) + round_add) >> 5), 0, 255);
        dst[4] = CLIP((((src[2] + src[7]) + 5 * (((src[4] + src[5])<<2) - (src[3] + src[6])) + round_add) >> 5), 0, 255);
        dst[5] = CLIP((((src[3] + src[8]) + 5 * (((src[5] + src[6])<<2) - (src[4] + src[7])) + round_add) >> 5), 0, 255);
        dst[6] = CLIP((((src[4] + src[9]) + 5 * (((src[6] + src[7])<<2) - (src[5] + src[8])) + round_add) >> 5), 0, 255);
        dst[7] = CLIP((((src[5] + src[10]) + 5 * (((src[7] + src[8])<<2) - (src[6] + src[9])) + round_add) >> 5), 0, 255);

        dst += stride;
        src += stride;
    }
}

void interpolate8x8_lowpass_h_c(uint8_t *dst, uint8_t *src, int32_t stride, int32_t rounding)
{
    int32_t i;
	uint8_t round_add = 16 - rounding;

    for(i = 0; i < 8; i++)
    {

        dst[0] = CLIP(((7 * ((src[0]<<1) - src[2]) + 23 * src[1] + 3 * src[3] - src[4] + round_add) >> 5), 0, 255);
        dst[1] = CLIP(((19 * src[1] + 20 * src[2] - src[5] + 3 * (src[4] - src[0] - (src[3]<<1)) + round_add) >> 5), 0, 255);
        dst[2] = CLIP(((20 * (src[2] + src[3]) + (src[0]<<1) + 3 * (src[5] - ((src[1] + src[4])<<1)) - src[6] + round_add) >> 5), 0, 255);
        dst[3] = CLIP(((20 * (src[3] + src[4]) + 3 * ((src[6] + src[1]) - ((src[2] + src[5])<<1)) - (src[0] + src[7]) + round_add) >> 5), 0, 255);
        dst[4] = CLIP(((20 * (src[4] + src[5]) - 3 * (((src[3] + src[6])<<1) - (src[2] + src[7])) - (src[1] + src[8]) + round_add) >> 5), 0, 255);
        dst[5] = CLIP(((20 * (src[5] + src[6]) + (src[8]<<1) + 3 * (src[3] - ((src[4] + src[7]) << 1)) - src[2] + round_add) >> 5), 0, 255);
        dst[6] = CLIP(((19 * src[7] + 20 * src[6] + 3 * (src[4] - src[8] - (src[5] << 1)) - src[3] + round_add) >> 5), 0, 255);
        dst[7] = CLIP(((23 * src[7] + 7 * ((src[8]<<1) - src[6]) + 3 * src[5] - src[4] + round_add) >> 5), 0, 255);

        dst += stride;
        src += stride;
    }
}

void interpolate8x8_6tap_lowpass_v_c(uint8_t *dst, uint8_t *src, int32_t stride, int32_t rounding)
{
    int32_t i;
	uint8_t round_add = 16 - rounding;

    for(i = 0; i < 8; i++)
    {
        int32_t src_2 = src[-2*stride];
        int32_t src_1 = src[-stride];
        int32_t src0 = src[0];
        int32_t src1 = src[stride];
        int32_t src2 = src[2 * stride];
        int32_t src3 = src[3 * stride];
        int32_t src4 = src[4 * stride];
        int32_t src5 = src[5 * stride];
        int32_t src6 = src[6 * stride];
        int32_t src7 = src[7 * stride];
        int32_t src8 = src[8 * stride];
        int32_t src9 = src[9 * stride];
        int32_t src10 = src[10 * stride];

        dst[0]			= CLIP((((src_2 + src3) + 5 * (((src0 + src1)<<2) - (src_1 + src2)) + round_add) >> 5), 0, 255);
        dst[stride]		= CLIP((((src_1 + src4) + 5 * (((src1 + src2)<<2) - (src0 + src3)) + round_add) >> 5), 0, 255);
        dst[2 * stride] = CLIP((((src0 + src5) + 5 * (((src2 + src3)<<2) - (src1 + src4)) + round_add) >> 5), 0, 255);
        dst[3 * stride] = CLIP((((src1 + src6) + 5 * (((src3 + src4)<<2) - (src2 + src5)) + round_add) >> 5), 0, 255);
        dst[4 * stride] = CLIP((((src2 + src7) + 5 * (((src4 + src5)<<2) - (src3 + src6)) + round_add) >> 5), 0, 255);
        dst[5 * stride] = CLIP((((src3 + src8) + 5 * (((src5 + src6)<<2) - (src4 + src7)) + round_add) >> 5), 0, 255);
        dst[6 * stride] = CLIP((((src4 + src9) + 5 * (((src6 + src7)<<2) - (src5 + src8)) + round_add) >> 5), 0, 255);
        dst[7 * stride] = CLIP((((src5 + src10) + 5 * (((src7 + src8)<<2) - (src6 + src9)) + round_add) >> 5), 0, 255);

		dst++;
        src++;
    }
}

void interpolate8x8_lowpass_v_c(uint8_t *dst, uint8_t *src, int32_t stride, int32_t rounding)
{
    int32_t i;
	uint8_t round_add = 16 - rounding;

    for(i = 0; i < 8; i++)
    {
        int32_t src0 = src[0];
        int32_t src1 = src[stride];
        int32_t src2 = src[2 * stride];
        int32_t src3 = src[3 * stride];
        int32_t src4 = src[4 * stride];
        int32_t src5 = src[5 * stride];
        int32_t src6 = src[6 * stride];
        int32_t src7 = src[7 * stride];
        int32_t src8 = src[8 * stride];
        
        dst[0]			= CLIP(((7 * ((src0<<1) - src2) + 23 * src1 + 3 * src3 - src4 + round_add) >> 5), 0, 255);
        dst[stride]		= CLIP(((19 * src1 + 20 * src2 - src5 + 3 * (src4 - src0 - (src3 << 1)) + round_add) >> 5), 0, 255);
        dst[2 * stride] = CLIP(((20 * (src2 + src3) + (src0<<1) + 3 * (src5 - ((src1 + src4) <<1 )) - src6 + round_add) >> 5), 0, 255);
        dst[3 * stride] = CLIP(((20 * (src3 + src4) + 3 * ((src6 + src1) - ((src2 + src5)<<1)) - (src0 + src7) + round_add) >> 5), 0, 255);
        dst[4 * stride] = CLIP(((20 * (src4 + src5) + 3 * ((src2 + src7) - ((src3 + src6)<<1)) - (src1 + src8) + round_add) >> 5), 0, 255);
        dst[5 * stride] = CLIP(((20 * (src5 + src6) + (src8<<1) + 3 * (src3 - ((src4 + src7) << 1)) - src2 + round_add) >> 5), 0, 255);
        dst[6 * stride] = CLIP(((19 * src7 + 20 * src6 - src3 + 3 * (src4 - src8 - (src5 << 1)) + round_add) >> 5), 0, 255);
        dst[7 * stride] = CLIP(((7 * ((src8<<1) - src6) + 23 * src7 + 3 * src5 - src4 + round_add) >> 5), 0, 255);

		dst++;
        src++;
    }
}

void interpolate8x8_lowpass_hv_c(uint8_t *dst1, uint8_t *dst2, uint8_t *src, int32_t stride, int32_t rounding)
{
	int32_t i;
	uint8_t round_add = 16 - rounding;
	uint8_t *h_ptr = dst2;

    for(i = 0; i < 9; i++)
    {

        h_ptr[0] = CLIP(((7 * ((src[0]<<1) - src[2]) + 23 * src[1] + 3 * src[3] - src[4] + round_add) >> 5), 0, 255);
        h_ptr[1] = CLIP(((19 * src[1] + 20 * src[2] - src[5] + 3 * (src[4] - src[0] - (src[3]<<1)) + round_add) >> 5), 0, 255);
        h_ptr[2] = CLIP(((20 * (src[2] + src[3]) + (src[0]<<1) + 3 * (src[5] - ((src[1] + src[4])<<1)) - src[6] + round_add) >> 5), 0, 255);
        h_ptr[3] = CLIP(((20 * (src[3] + src[4]) + 3 * ((src[6] + src[1]) - ((src[2] + src[5])<<1)) - (src[0] + src[7]) + round_add) >> 5), 0, 255);
        h_ptr[4] = CLIP(((20 * (src[4] + src[5]) - 3 * (((src[3] + src[6])<<1) - (src[2] + src[7])) - (src[1] + src[8]) + round_add) >> 5), 0, 255);
        h_ptr[5] = CLIP(((20 * (src[5] + src[6]) + (src[8]<<1) + 3 * (src[3] - ((src[4] + src[7]) << 1)) - src[2] + round_add) >> 5), 0, 255);
        h_ptr[6] = CLIP(((19 * src[7] + 20 * src[6] + 3 * (src[4] - src[8] - (src[5] << 1)) - src[3] + round_add) >> 5), 0, 255);
        h_ptr[7] = CLIP(((23 * src[7] + 7 * ((src[8]<<1) - src[6]) + 3 * src[5] - src[4] + round_add) >> 5), 0, 255);

        h_ptr += stride;
        src += stride;
    }

	interpolate8x8_lowpass_v_c(dst1, dst2, stride, rounding);

}