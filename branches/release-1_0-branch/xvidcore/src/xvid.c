/*****************************************************************************
 *
 *  XVID MPEG-4 VIDEO CODEC
 *  - Native API implementation  -
 *
 *  Copyright(C) 2001-2003 Peter Ross <pross@xvid.org>
 *
 *  This program is free software ; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation ; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY ; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program ; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 * $Id: xvid.c,v 1.48.2.4 2004-06-05 23:08:01 edgomez Exp $
 *
 ****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "xvid.h"
#include "decoder.h"
#include "encoder.h"
#include "bitstream/cbp.h"
#include "dct/idct.h"
#include "dct/fdct.h"
#include "image/colorspace.h"
#include "image/interpolate8x8.h"
#include "image/reduced.h"
#include "utils/mem_transfer.h"
#include "utils/mbfunctions.h"
#include "quant/quant.h"
#include "motion/motion.h"
#include "motion/sad.h"
#include "utils/emms.h"
#include "utils/timer.h"
#include "bitstream/mbcoding.h"
#include "image/qpel.h"
#include "image/postprocessing.h"

#if defined(_DEBUG)
unsigned int xvid_debug = 0; /* xvid debug mask */
#endif

#if defined(ARCH_IS_IA32)
#if defined(_MSC_VER)
#	include <windows.h>
#else
#	include <signal.h>
#	include <setjmp.h>

	static jmp_buf mark;

	static void
	sigill_handler(int signal)
	{
	   longjmp(mark, 1);
	}
#endif


/*
 * Calls the funcptr, and returns whether SIGILL (illegal instruction) was
 * signalled
 *
 * Return values:
 *  -1 : could not determine
 *   0 : SIGILL was *not* signalled
 *   1 : SIGILL was signalled
 */

int
sigill_check(void (*func)())
{
#if defined(_MSC_VER)
	_try {
		func();
	}
	_except(EXCEPTION_EXECUTE_HANDLER) {

		if (_exception_code() == STATUS_ILLEGAL_INSTRUCTION)
			return 1;
	}
	return 0;
#else
    void * old_handler;
    int jmpret;


    old_handler = signal(SIGILL, sigill_handler);
    if (old_handler == SIG_ERR)
    {
        return -1;
    }

    jmpret = setjmp(mark);
    if (jmpret == 0)
    {
        func();
    }

    signal(SIGILL, old_handler);

    return jmpret;
#endif
}
#endif


/* detect cpu flags  */
static unsigned int
detect_cpu_flags()
{
	/* enable native assembly optimizations by default */
	unsigned int cpu_flags = XVID_CPU_ASM;

#if defined(ARCH_IS_IA32)
	cpu_flags |= check_cpu_features();
	if ((cpu_flags & XVID_CPU_SSE) && sigill_check(sse_os_trigger))
		cpu_flags &= ~XVID_CPU_SSE;

	if ((cpu_flags & XVID_CPU_SSE2) && sigill_check(sse2_os_trigger))
		cpu_flags &= ~XVID_CPU_SSE2;
#endif

#if defined(ARCH_IS_PPC)
#if defined(ARCH_IS_PPC_ALTIVEC)
	cpu_flags |= XVID_CPU_ALTIVEC;
#endif
#endif

	return cpu_flags;
}


/*****************************************************************************
 * XviD Init Entry point
 *
 * Well this function initialize all internal function pointers according
 * to the CPU features forced by the library client or autodetected (depending
 * on the XVID_CPU_FORCE flag). It also initializes vlc coding tables and all
 * image colorspace transformation tables.
 *
 * Returned value : XVID_ERR_OK
 *                  + API_VERSION in the input XVID_INIT_PARAM structure
 *                  + core build  "   "    "       "               "
 *
 ****************************************************************************/


static
int xvid_gbl_init(xvid_gbl_init_t * init)
{
	unsigned int cpu_flags;

	if (XVID_VERSION_MAJOR(init->version) != 1) /* v1.x.x */
		return XVID_ERR_VERSION;

	cpu_flags = (init->cpu_flags & XVID_CPU_FORCE) ? init->cpu_flags : detect_cpu_flags();

	/* Initialize the function pointers */
	idct_int32_init();
	init_vlc_tables();

	/* Fixed Point Forward/Inverse DCT transformations */
	fdct = fdct_int32;
	idct = idct_int32;

	/* Only needed on PPC Altivec archs */
	sadInit = 0;

	/* Restore FPU context : emms_c is a nop functions */
	emms = emms_c;

	/* Qpel stuff */
	xvid_QP_Funcs = &xvid_QP_Funcs_C;
	xvid_QP_Add_Funcs = &xvid_QP_Add_Funcs_C;
	xvid_Init_QP();

	/* Quantization functions */
	quant_h263_intra   = quant_h263_intra_c;
	quant_h263_inter   = quant_h263_inter_c;
	dequant_h263_intra = dequant_h263_intra_c;
	dequant_h263_inter = dequant_h263_inter_c;

	quant_mpeg_intra   = quant_mpeg_intra_c;
	quant_mpeg_inter   = quant_mpeg_inter_c;
	dequant_mpeg_intra = dequant_mpeg_intra_c;
	dequant_mpeg_inter = dequant_mpeg_inter_c;

	/* Block transfer related functions */
	transfer_8to16copy = transfer_8to16copy_c;
	transfer_16to8copy = transfer_16to8copy_c;
	transfer_8to16sub  = transfer_8to16sub_c;
	transfer_8to16subro  = transfer_8to16subro_c;
	transfer_8to16sub2 = transfer_8to16sub2_c;
	transfer_16to8add  = transfer_16to8add_c;
	transfer8x8_copy   = transfer8x8_copy_c;

	/* Interlacing functions */
	MBFieldTest = MBFieldTest_c;

	/* Image interpolation related functions */
	interpolate8x8_halfpel_h  = interpolate8x8_halfpel_h_c;
	interpolate8x8_halfpel_v  = interpolate8x8_halfpel_v_c;
	interpolate8x8_halfpel_hv = interpolate8x8_halfpel_hv_c;

	interpolate16x16_lowpass_h = interpolate16x16_lowpass_h_c;
	interpolate16x16_lowpass_v = interpolate16x16_lowpass_v_c;
	interpolate16x16_lowpass_hv = interpolate16x16_lowpass_hv_c;

	interpolate8x8_lowpass_h = interpolate8x8_lowpass_h_c;
	interpolate8x8_lowpass_v = interpolate8x8_lowpass_v_c;
	interpolate8x8_lowpass_hv = interpolate8x8_lowpass_hv_c;

	interpolate8x8_6tap_lowpass_h = interpolate8x8_6tap_lowpass_h_c;
	interpolate8x8_6tap_lowpass_v = interpolate8x8_6tap_lowpass_v_c;

	interpolate8x8_avg2 = interpolate8x8_avg2_c;
	interpolate8x8_avg4 = interpolate8x8_avg4_c;

	/* reduced resolution */
	copy_upsampled_8x8_16to8 = xvid_Copy_Upsampled_8x8_16To8_C;
	add_upsampled_8x8_16to8 = xvid_Add_Upsampled_8x8_16To8_C;
	vfilter_31 = xvid_VFilter_31_C;
	hfilter_31 = xvid_HFilter_31_C;
	filter_18x18_to_8x8 = xvid_Filter_18x18_To_8x8_C;
	filter_diff_18x18_to_8x8 = xvid_Filter_Diff_18x18_To_8x8_C;

	/* Initialize internal colorspace transformation tables */
	colorspace_init();

	/* All colorspace transformation functions User Format->YV12 */
	yv12_to_yv12    = yv12_to_yv12_c;
	rgb555_to_yv12  = rgb555_to_yv12_c;
	rgb565_to_yv12  = rgb565_to_yv12_c;
	bgr_to_yv12     = bgr_to_yv12_c;
	bgra_to_yv12    = bgra_to_yv12_c;
	abgr_to_yv12    = abgr_to_yv12_c;
	rgba_to_yv12    = rgba_to_yv12_c;
	argb_to_yv12    = argb_to_yv12_c;
	yuyv_to_yv12    = yuyv_to_yv12_c;
	uyvy_to_yv12    = uyvy_to_yv12_c;

	rgb555i_to_yv12 = rgb555i_to_yv12_c;
	rgb565i_to_yv12 = rgb565i_to_yv12_c;
	bgri_to_yv12    = bgri_to_yv12_c;
	bgrai_to_yv12   = bgrai_to_yv12_c;
	abgri_to_yv12   = abgri_to_yv12_c;
	rgbai_to_yv12   = rgbai_to_yv12_c;
	argbi_to_yv12   = argbi_to_yv12_c;
	yuyvi_to_yv12   = yuyvi_to_yv12_c;
	uyvyi_to_yv12   = uyvyi_to_yv12_c;

	/* All colorspace transformation functions YV12->User format */
	yv12_to_rgb555  = yv12_to_rgb555_c;
	yv12_to_rgb565  = yv12_to_rgb565_c;
	yv12_to_bgr     = yv12_to_bgr_c;
	yv12_to_bgra    = yv12_to_bgra_c;
	yv12_to_abgr    = yv12_to_abgr_c;
	yv12_to_rgba    = yv12_to_rgba_c;
	yv12_to_argb    = yv12_to_argb_c;
	yv12_to_yuyv    = yv12_to_yuyv_c;
	yv12_to_uyvy    = yv12_to_uyvy_c;

	yv12_to_rgb555i = yv12_to_rgb555i_c;
	yv12_to_rgb565i = yv12_to_rgb565i_c;
	yv12_to_bgri    = yv12_to_bgri_c;
	yv12_to_bgrai   = yv12_to_bgrai_c;
	yv12_to_abgri   = yv12_to_abgri_c;
	yv12_to_rgbai   = yv12_to_rgbai_c;
	yv12_to_argbi   = yv12_to_argbi_c;
	yv12_to_yuyvi   = yv12_to_yuyvi_c;
	yv12_to_uyvyi   = yv12_to_uyvyi_c;

	/* Functions used in motion estimation algorithms */
	calc_cbp   = calc_cbp_c;
	sad16      = sad16_c;
	sad8       = sad8_c;
	sad16bi    = sad16bi_c;
	sad8bi     = sad8bi_c;
	dev16      = dev16_c;
	sad16v	   = sad16v_c;
	sse8_16bit = sse8_16bit_c;

#if defined(ARCH_IS_IA32)

	if ((cpu_flags & XVID_CPU_ASM))	{
		vfilter_31 = xvid_VFilter_31_x86;
		hfilter_31 = xvid_HFilter_31_x86;
	}

	if ((cpu_flags & XVID_CPU_MMX) || (cpu_flags & XVID_CPU_MMXEXT) ||
		(cpu_flags & XVID_CPU_3DNOW) || (cpu_flags & XVID_CPU_3DNOWEXT) ||
		(cpu_flags & XVID_CPU_SSE) || (cpu_flags & XVID_CPU_SSE2))
	{
		/* Restore FPU context : emms_c is a nop functions */
		emms = emms_mmx;
	}

	if ((cpu_flags & XVID_CPU_MMX)) {

		/* Forward and Inverse Discrete Cosine Transformation functions */
		fdct = fdct_mmx_skal;
		idct = idct_mmx;

		/* Qpel stuff */
		xvid_QP_Funcs = &xvid_QP_Funcs_mmx;
		xvid_QP_Add_Funcs = &xvid_QP_Add_Funcs_mmx;

		/* Quantization related functions */
		quant_h263_intra   = quant_h263_intra_mmx;
		quant_h263_inter   = quant_h263_inter_mmx;
		dequant_h263_intra = dequant_h263_intra_mmx;
		dequant_h263_inter = dequant_h263_inter_mmx;

		quant_mpeg_intra   = quant_mpeg_intra_mmx;
		quant_mpeg_inter   = quant_mpeg_inter_mmx;
		dequant_mpeg_intra = dequant_mpeg_intra_mmx;
		dequant_mpeg_inter = dequant_mpeg_inter_mmx;

		/* Block related functions */
		transfer_8to16copy = transfer_8to16copy_mmx;
		transfer_16to8copy = transfer_16to8copy_mmx;
		transfer_8to16sub  = transfer_8to16sub_mmx;
		transfer_8to16subro  = transfer_8to16subro_mmx;
		transfer_8to16sub2 = transfer_8to16sub2_mmx;
		transfer_16to8add  = transfer_16to8add_mmx;
		transfer8x8_copy   = transfer8x8_copy_mmx;

		/* Interlacing Functions */
		MBFieldTest = MBFieldTest_mmx;

		/* Image Interpolation related functions */
		interpolate8x8_halfpel_h  = interpolate8x8_halfpel_h_mmx;
		interpolate8x8_halfpel_v  = interpolate8x8_halfpel_v_mmx;
		interpolate8x8_halfpel_hv = interpolate8x8_halfpel_hv_mmx;

		interpolate8x8_6tap_lowpass_h = interpolate8x8_6tap_lowpass_h_mmx;
		interpolate8x8_6tap_lowpass_v = interpolate8x8_6tap_lowpass_v_mmx;

		interpolate8x8_avg2 = interpolate8x8_avg2_mmx;
		interpolate8x8_avg4 = interpolate8x8_avg4_mmx;

		/* reduced resolution */
		copy_upsampled_8x8_16to8 = xvid_Copy_Upsampled_8x8_16To8_mmx;
		add_upsampled_8x8_16to8 = xvid_Add_Upsampled_8x8_16To8_mmx;
		hfilter_31 = xvid_HFilter_31_mmx;
		filter_18x18_to_8x8 = xvid_Filter_18x18_To_8x8_mmx;
		filter_diff_18x18_to_8x8 = xvid_Filter_Diff_18x18_To_8x8_mmx;

		/* image input xxx_to_yv12 related functions */
		yv12_to_yv12  = yv12_to_yv12_mmx;
		bgr_to_yv12   = bgr_to_yv12_mmx;
		bgra_to_yv12  = bgra_to_yv12_mmx;
		yuyv_to_yv12  = yuyv_to_yv12_mmx;
		uyvy_to_yv12  = uyvy_to_yv12_mmx;

		/* image output yv12_to_xxx related functions */
		yv12_to_bgr   = yv12_to_bgr_mmx;
		yv12_to_bgra  = yv12_to_bgra_mmx;
		yv12_to_yuyv  = yv12_to_yuyv_mmx;
		yv12_to_uyvy  = yv12_to_uyvy_mmx;

		yv12_to_yuyvi = yv12_to_yuyvi_mmx;
		yv12_to_uyvyi = yv12_to_uyvyi_mmx;

		/* Motion estimation related functions */
		calc_cbp = calc_cbp_mmx;
		sad16    = sad16_mmx;
		sad8     = sad8_mmx;
		sad16bi = sad16bi_mmx;
		sad8bi  = sad8bi_mmx;
		dev16    = dev16_mmx;
		sad16v	 = sad16v_mmx;
		sse8_16bit = sse8_16bit_mmx;
	}

	/* these 3dnow functions are faster than mmx, but slower than xmm. */
	if ((cpu_flags & XVID_CPU_3DNOW)) {

		emms = emms_3dn;

		/* ME functions */
		sad16bi = sad16bi_3dn;
		sad8bi  = sad8bi_3dn;

		yuyv_to_yv12  = yuyv_to_yv12_3dn;
		uyvy_to_yv12  = uyvy_to_yv12_3dn;
	}


	if ((cpu_flags & XVID_CPU_MMXEXT)) {

		/* DCT */
		fdct = fdct_xmm_skal;
		idct = idct_xmm;

		/* Interpolation */
		interpolate8x8_halfpel_h  = interpolate8x8_halfpel_h_xmm;
		interpolate8x8_halfpel_v  = interpolate8x8_halfpel_v_xmm;
		interpolate8x8_halfpel_hv = interpolate8x8_halfpel_hv_xmm;

		/* reduced resolution */
		copy_upsampled_8x8_16to8 = xvid_Copy_Upsampled_8x8_16To8_xmm;
		add_upsampled_8x8_16to8 = xvid_Add_Upsampled_8x8_16To8_xmm;

		/* Quantization */
		quant_mpeg_intra = quant_mpeg_intra_xmm;
		quant_mpeg_inter = quant_mpeg_inter_xmm;

		dequant_h263_intra = dequant_h263_intra_xmm;
		dequant_h263_inter = dequant_h263_inter_xmm;

		/* Buffer transfer */
		transfer_8to16sub2 = transfer_8to16sub2_xmm;

		/* Colorspace transformation */
		yv12_to_yv12  = yv12_to_yv12_xmm;
		yuyv_to_yv12  = yuyv_to_yv12_xmm;
		uyvy_to_yv12  = uyvy_to_yv12_xmm;

		/* ME functions */
		sad16 = sad16_xmm;
		sad8  = sad8_xmm;
		sad16bi = sad16bi_xmm;
		sad8bi  = sad8bi_xmm;
		dev16 = dev16_xmm;
		sad16v	 = sad16v_xmm;
	}

	if ((cpu_flags & XVID_CPU_3DNOW)) {

		/* Interpolation */
		interpolate8x8_halfpel_h = interpolate8x8_halfpel_h_3dn;
		interpolate8x8_halfpel_v = interpolate8x8_halfpel_v_3dn;
		interpolate8x8_halfpel_hv = interpolate8x8_halfpel_hv_3dn;
	}

	if ((cpu_flags & XVID_CPU_3DNOWEXT)) {

		/* Buffer transfer */
		transfer_8to16copy =  transfer_8to16copy_3dne;
		transfer_16to8copy = transfer_16to8copy_3dne;
		transfer_8to16sub =  transfer_8to16sub_3dne;
		transfer_8to16subro =  transfer_8to16subro_3dne;
		transfer_16to8add = transfer_16to8add_3dne;
		transfer8x8_copy = transfer8x8_copy_3dne;

		if ((cpu_flags & XVID_CPU_MMXEXT)) {
			/* Inverse DCT */
			idct =  idct_3dne;

			/* Buffer transfer */
			transfer_8to16sub2 =  transfer_8to16sub2_3dne;

			/* Interpolation */
			interpolate8x8_halfpel_h = interpolate8x8_halfpel_h_3dne;
			interpolate8x8_halfpel_v = interpolate8x8_halfpel_v_3dne;
			interpolate8x8_halfpel_hv = interpolate8x8_halfpel_hv_3dne;

			/* Quantization */
			quant_h263_intra = quant_h263_intra_3dne;		/* cmov only */
			quant_h263_inter = quant_h263_inter_3dne;
			dequant_mpeg_intra = dequant_mpeg_intra_3dne;	/* cmov only */
			dequant_mpeg_inter = dequant_mpeg_inter_3dne;
			dequant_h263_intra = dequant_h263_intra_3dne;
			dequant_h263_inter = dequant_h263_inter_3dne;

			/* ME functions */
			calc_cbp = calc_cbp_3dne;

			sad16 = sad16_3dne;
			sad8 = sad8_3dne;
			sad16bi = sad16bi_3dne;
			sad8bi = sad8bi_3dne;
			dev16 = dev16_3dne;
		}
	}

	if ((cpu_flags & XVID_CPU_SSE2)) {

		calc_cbp = calc_cbp_sse2;

		/* Quantization */
		quant_h263_intra   = quant_h263_intra_sse2;
		quant_h263_inter   = quant_h263_inter_sse2;
		dequant_h263_intra = dequant_h263_intra_sse2;
		dequant_h263_inter = dequant_h263_inter_sse2;

		/* SAD operators */
		sad16    = sad16_sse2;
		dev16    = dev16_sse2;

		/* DCT operators
		 * no iDCT because it's not "Walken matching" */
		fdct = fdct_sse2_skal;
	}
#endif /* ARCH_IS_IA32 */

#if defined(ARCH_IS_IA64)
	if ((cpu_flags & XVID_CPU_ASM)) { /* use assembler routines? */
	  idct_ia64_init();
	  fdct = fdct_ia64;
	  idct = idct_ia64;   /*not yet working, crashes */
	  interpolate8x8_halfpel_h = interpolate8x8_halfpel_h_ia64;
	  interpolate8x8_halfpel_v = interpolate8x8_halfpel_v_ia64;
	  interpolate8x8_halfpel_hv = interpolate8x8_halfpel_hv_ia64;
	  sad16 = sad16_ia64;
	  sad16bi = sad16bi_ia64;
	  sad8 = sad8_ia64;
	  dev16 = dev16_ia64;
/*	  Halfpel8_Refine = Halfpel8_Refine_ia64; */
	  quant_h263_intra = quant_h263_intra_ia64;
	  quant_h263_inter = quant_h263_inter_ia64;
	  dequant_h263_intra = dequant_h263_intra_ia64;
	  dequant_h263_inter = dequant_h263_inter_ia64;
	  transfer_8to16copy = transfer_8to16copy_ia64;
	  transfer_16to8copy = transfer_16to8copy_ia64;
	  transfer_8to16sub = transfer_8to16sub_ia64;
	  transfer_8to16sub2 = transfer_8to16sub2_ia64;
	  transfer_16to8add = transfer_16to8add_ia64;
	  transfer8x8_copy = transfer8x8_copy_ia64;
	}
#endif

#if defined(ARCH_IS_PPC)
	if ((cpu_flags & XVID_CPU_ASM))
	{
		calc_cbp = calc_cbp_ppc;
	}

	if ((cpu_flags & XVID_CPU_ALTIVEC))
	{
		calc_cbp = calc_cbp_altivec;
		fdct = fdct_altivec;
		idct = idct_altivec;
		sadInit = sadInit_altivec;
		sad16 = sad16_altivec;
		sad8 = sad8_altivec;
		dev16 = dev16_altivec;
	}
#endif

#if defined(_DEBUG)
    xvid_debug = init->debug;
#endif

    return 0;
}


static int
xvid_gbl_info(xvid_gbl_info_t * info)
{
	if (XVID_VERSION_MAJOR(info->version) != 1) /* v1.x.x */
		return XVID_ERR_VERSION;

	info->actual_version = XVID_VERSION;
	info->build = "xvid-1.0.1";
	info->cpu_flags = detect_cpu_flags();

#if defined(_SMP) && defined(WIN32)
	info->num_threads = pthread_num_processors_np();;
#else
	info->num_threads = 0;
#endif

	return 0;
}


static int
xvid_gbl_convert(xvid_gbl_convert_t* convert)
{
	int width;
	int height;
	int width2;
	int height2;
	IMAGE img;

	if (XVID_VERSION_MAJOR(convert->version) != 1)   /* v1.x.x */
	      return XVID_ERR_VERSION;

#if 0
	const int flip1 = (convert->input.colorspace & XVID_CSP_VFLIP) ^ (convert->output.colorspace & XVID_CSP_VFLIP);
#endif
	width = convert->width;
	height = convert->height;
	width2 = convert->width/2;
	height2 = convert->height/2;

	switch (convert->input.csp & ~XVID_CSP_VFLIP)
	{
		case XVID_CSP_YV12 :
			img.y = convert->input.plane[0];
			img.v = (uint8_t*)convert->input.plane[0] + convert->input.stride[0]*height;
			img.u = (uint8_t*)convert->input.plane[0] + convert->input.stride[0]*height + (convert->input.stride[0]/2)*height2;
			image_output(&img, width, height, width,
						(uint8_t**)convert->output.plane, convert->output.stride,
						convert->output.csp, convert->interlacing);
			break;

		default :
			return XVID_ERR_FORMAT;
	}


	emms();
	return 0;
}

/*****************************************************************************
 * XviD Global Entry point
 *
 * Well this function initialize all internal function pointers according
 * to the CPU features forced by the library client or autodetected (depending
 * on the XVID_CPU_FORCE flag). It also initializes vlc coding tables and all
 * image colorspace transformation tables.
 *
 ****************************************************************************/


int
xvid_global(void *handle,
		  int opt,
		  void *param1,
		  void *param2)
{
	switch(opt)
	{
		case XVID_GBL_INIT :
			return xvid_gbl_init((xvid_gbl_init_t*)param1);

        case XVID_GBL_INFO :
            return xvid_gbl_info((xvid_gbl_info_t*)param1);

		case XVID_GBL_CONVERT :
			return xvid_gbl_convert((xvid_gbl_convert_t*)param1);

		default :
			return XVID_ERR_FAIL;
	}
}

/*****************************************************************************
 * XviD Native decoder entry point
 *
 * This function is just a wrapper to all the option cases.
 *
 * Returned values : XVID_ERR_FAIL when opt is invalid
 *                   else returns the wrapped function result
 *
 ****************************************************************************/

int
xvid_decore(void *handle,
			int opt,
			void *param1,
			void *param2)
{
	switch (opt) {
	case XVID_DEC_CREATE:
		return decoder_create((xvid_dec_create_t *) param1);

	case XVID_DEC_DESTROY:
		return decoder_destroy((DECODER *) handle);

	case XVID_DEC_DECODE:
		return decoder_decode((DECODER *) handle, (xvid_dec_frame_t *) param1, (xvid_dec_stats_t*) param2);

	default:
		return XVID_ERR_FAIL;
	}
}


/*****************************************************************************
 * XviD Native encoder entry point
 *
 * This function is just a wrapper to all the option cases.
 *
 * Returned values : XVID_ERR_FAIL when opt is invalid
 *                   else returns the wrapped function result
 *
 ****************************************************************************/

int
xvid_encore(void *handle,
			int opt,
			void *param1,
			void *param2)
{
	switch (opt) {
	case XVID_ENC_ENCODE:

		return enc_encode((Encoder *) handle,
							  (xvid_enc_frame_t *) param1,
							  (xvid_enc_stats_t *) param2);

	case XVID_ENC_CREATE:
		return enc_create((xvid_enc_create_t *) param1);

	case XVID_ENC_DESTROY:
		return enc_destroy((Encoder *) handle);

	default:
		return XVID_ERR_FAIL;
	}
}