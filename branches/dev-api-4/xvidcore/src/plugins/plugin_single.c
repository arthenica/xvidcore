/*****************************************************************************
 *
 *  XviD Standard Plugins
 *  - single-pass bitrate controller implementation -
 *
 *  Copyright(C) 2002      Benjamin Lambert <foxer@hotmail.com>
 *               2002-2003 Edouard Gomez <ed.gomez@free.fr>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * $Id: plugin_single.c,v 1.1.2.4 2003-07-24 12:00:06 Isibaar Exp $
 *
 ****************************************************************************/


#include <limits.h>

#include "../xvid.h"
#include "../image/image.h"

#define DEFAULT_INITIAL_QUANTIZER 8

#define DEFAULT_BITRATE 900000	/* 900kbps */
#define DEFAULT_DELAY_FACTOR 16
#define DEFAULT_AVERAGING_PERIOD 100
#define DEFAULT_BUFFER 100

typedef struct
{
	int reaction_delay_factor;
	int averaging_period;
	int buffer;

	int bytes_per_sec;
	double target_framesize;

	double time;
	int64_t total_size;
	int rtn_quant;

	double sequence_quality;
	double avg_framesize;
	double quant_error[31];

    double fq_error;
}
rc_single_t;


static int
get_initial_quant(unsigned int bitrate)
{

#if defined(DEFAULT_INITIAL_QUANTIZER)
    return (DEFAULT_INITIAL_QUANTIZER);
#else
	int i;

	const unsigned int bitrate_quant[31] = {
		UINT_MAX
	};

	for (i = 30; i >= 0; i--) {
		if (bitrate > bitrate_quant[i])
			continue;
	}

	return (i + 1);
#endif
}

static int
rc_single_create(xvid_plg_create_t * create,
			  rc_single_t ** handle)
{
	xvid_plugin_single_t *param = (xvid_plugin_single_t *) create->param;
	rc_single_t *rc;
	int i;

	/*
	 * single needs to caclculate the average frame size. In order to do that,
	 * we really need valid fps
	 */
	if (create->fincr == 0) {
		return XVID_ERR_FAIL;
	}

	/* Allocate context struct */
	if ((rc = malloc(sizeof(rc_single_t))) == NULL)
		return (XVID_ERR_MEMORY);

	/* Constants */
	rc->bytes_per_sec =	(param->bitrate > 0) ? param->bitrate / 8 : DEFAULT_BITRATE / 8;
	rc->target_framesize =(double) rc->bytes_per_sec / ((double) create->fbase / create->fincr);
	rc->reaction_delay_factor =	(param->reaction_delay_factor > 0) ? param->reaction_delay_factor : DEFAULT_DELAY_FACTOR;
	rc->averaging_period = (param->averaging_period > 0) ? param->averaging_period : DEFAULT_AVERAGING_PERIOD;
	rc->buffer = (param->buffer > 0) ? param->buffer : DEFAULT_BUFFER;

	rc->time = 0;
	rc->total_size = 0;
	rc->rtn_quant = get_initial_quant(param->bitrate);

	/* Reset quant error accumulators */
	for (i = 0; i < 31; i++)
		rc->quant_error[i] = 0.0;

	/* Last bunch of variables */
	rc->sequence_quality = 2.0 / (double) rc->rtn_quant;
	rc->avg_framesize = rc->target_framesize;

    rc->fq_error = 0;

	/* Bind the RC */
	*handle = rc;

	/* A bit of debug info */
	DPRINTF(XVID_DEBUG_RC, "bytes_per_sec: %i\n", rc->bytes_per_sec);
	DPRINTF(XVID_DEBUG_RC, "frame rate   : %f\n", (double) create->fbase / create->fincr);
	DPRINTF(XVID_DEBUG_RC, "target_framesize: %f\n", rc->target_framesize);

	return (0);
}


static int
rc_single_destroy(rc_single_t * rc,
			   xvid_plg_destroy_t * destroy)
{
	free(rc);
	return (0);
}


static int
rc_single_before(rc_single_t * rc,
			  xvid_plg_data_t * data)
{
     if (data->quant <= 0) {
        if (data->zone && data->zone->mode == XVID_ZONE_QUANT) {
            rc->fq_error += (double)data->zone->increment / (double)data->zone->base;
            data->quant = (int)rc->fq_error;
            rc->fq_error -= data->quant;
        }else {
        	data->quant = rc->rtn_quant;
        }
    }
	return 0;
}


static int
rc_single_after(rc_single_t * rc,
			 xvid_plg_data_t * data)
{
	int64_t deviation;
	int rtn_quant;
	double overflow;
	double averaging_period;
	double reaction_delay_factor;
	double quality_scale;
	double base_quality;
	double target_quality;

	/* Update internal values */
	rc->time += (double) data->fincr / data->fbase;
	rc->total_size += data->length;

	if(data->type == XVID_TYPE_BVOP) 
		return (0); 

	rc->rtn_quant = data->quant;

	/* Compute the deviation from expected total size */
	deviation = (int64_t)
		((double) rc->total_size - (double) rc->bytes_per_sec * rc->time);


	if (data->quant >= 2) {

		averaging_period = (double) rc->averaging_period;

		rc->sequence_quality -= rc->sequence_quality / averaging_period;

		rc->sequence_quality +=
			2.0 / (double) data->quant / averaging_period;

		if (rc->sequence_quality < 0.1)
			rc->sequence_quality = 0.1;

		if (data->type != XVID_TYPE_IVOP) {
			reaction_delay_factor = (double) rc->reaction_delay_factor;
			rc->avg_framesize -= rc->avg_framesize / reaction_delay_factor;
			rc->avg_framesize += data->length / reaction_delay_factor;
		}

	}

	quality_scale =
		rc->target_framesize / rc->avg_framesize * rc->target_framesize /
		rc->avg_framesize;

	base_quality = rc->sequence_quality;
	if (quality_scale >= 1.0) {
		base_quality = 1.0 - (1.0 - base_quality) / quality_scale;
	} else {
		base_quality = 0.06452 + (base_quality - 0.06452) * quality_scale;
	}

	overflow = -((double) deviation / (double) rc->buffer);

	target_quality =
		base_quality + (base_quality -
						0.06452) * overflow / rc->target_framesize;

	if (target_quality > 2.0)
		target_quality = 2.0;
	else if (target_quality < 0.06452)
		target_quality = 0.06452;

	rtn_quant = (int) (2.0 / target_quality);

	if (rtn_quant > 0 && rtn_quant < 31) {
		rc->quant_error[rtn_quant - 1] += 2.0 / target_quality - rtn_quant;
		if (rc->quant_error[rtn_quant - 1] >= 1.0) {
			rc->quant_error[rtn_quant - 1] -= 1.0;
			rtn_quant++;
		}
	}

    /* prevent rapid quantization change */
	if (rtn_quant > data->quant + 1)
		rtn_quant = data->quant + 1;
	else if (rtn_quant < data->quant - 1)
		rtn_quant = data->quant - 1;

    /* limit to min/max range */
	if (rtn_quant > data->max_quant[data->type-1])
		rtn_quant = data->max_quant[data->type-1];
	else if (rtn_quant < data->min_quant[data->type-1])
		rtn_quant = data->min_quant[data->type-1];

	rc->rtn_quant = rtn_quant;

	return (0);
}



int
xvid_plugin_single(void *handle,
				int opt,
				void *param1,
				void *param2)
{
	switch (opt) {
	case XVID_PLG_INFO:
		return 0;

	case XVID_PLG_CREATE:
		return rc_single_create((xvid_plg_create_t *) param1, param2);

	case XVID_PLG_DESTROY:
		return rc_single_destroy((rc_single_t *) handle,(xvid_plg_destroy_t *) param1);

	case XVID_PLG_BEFORE:
		return rc_single_before((rc_single_t *) handle, (xvid_plg_data_t *) param1);

	case XVID_PLG_AFTER:
		return rc_single_after((rc_single_t *) handle, (xvid_plg_data_t *) param1);
	}

	return XVID_ERR_FAIL;
}
