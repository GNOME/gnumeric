/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_SHEET_FILTER_H_
# define _GNM_SHEET_FILTER_H_

#include "gnumeric.h"

G_BEGIN_DECLS

typedef enum {
	GNM_FILTER_UNUSED	= -1,

	GNM_FILTER_OP_EQUAL	= 0,	/* exact match, no regecxp */
	GNM_FILTER_OP_GT,
	GNM_FILTER_OP_LT,
	GNM_FILTER_OP_GTE,
	GNM_FILTER_OP_LTE,
	GNM_FILTER_OP_NOT_EQUAL,	/* exact match, no regecxp */

	GNM_FILTER_OP_BLANKS		= 0x20,
	GNM_FILTER_OP_NON_BLANKS	= 0x21,

	GNM_FILTER_OP_TOP_N		= 0x30,
	GNM_FILTER_OP_BOTTOM_N		= 0x31,
	GNM_FILTER_OP_TOP_N_PERCENT	= 0x32,
	GNM_FILTER_OP_BOTTOM_N_PERCENT	= 0x33,
	GNM_FILTER_OP_BOTTOM_MASK	= 0x01,
	GNM_FILTER_OP_PERCENT_MASK	= 0x02,

	/* Added in 1.7.7 */
	GNM_FILTER_OP_GT_AVERAGE	= 0x40,
	GNM_FILTER_OP_LT_AVERAGE	= 0x41,
	GNM_FILTER_OP_WITHIN_STDDEV	= 0x50,
	GNM_FILTER_OP_OUTSIDE_STDDEV	= 0x51,

	GNM_FILTER_OP_MATCH		= 0x60,	/* regexp */
	GNM_FILTER_OP_NO_MATCH		= 0x61,	/* regexp */

	GNM_FILTER_OP_TYPE_OP		= 0x00,
	GNM_FILTER_OP_TYPE_BLANKS	= 0x20,
	GNM_FILTER_OP_TYPE_BUCKETS	= 0x30,
	GNM_FILTER_OP_TYPE_AVERAGE	= 0x40,
	GNM_FILTER_OP_TYPE_STDDEV	= 0x50,
	GNM_FILTER_OP_TYPE_MATCH	= 0x60,
	GNM_FILTER_OP_TYPE_MASK		= 0x70
} GnmFilterOp;

struct _GnmFilterCondition {
	GnmFilterOp  op[2];
	GnmValue    *value[2];
	gboolean is_and;
	float	 count;
};

struct _GnmFilter {
	Sheet *sheet;
	GnmRange  r;

	GPtrArray *fields;
	gboolean   is_active;
};

GnmFilterCondition *gnm_filter_condition_new_single (GnmFilterOp op, GnmValue *v);
GnmFilterCondition *gnm_filter_condition_new_double (GnmFilterOp op1, GnmValue *v1,
						     gboolean join_with_and,
						     GnmFilterOp op2, GnmValue *v2);
GnmFilterCondition *gnm_filter_condition_new_bucket (gboolean top,
						     gboolean absolute,
						     float n);
GnmFilterCondition *gnm_filter_condition_dup 	    (GnmFilterCondition const *src);
void		    gnm_filter_condition_unref 	    (GnmFilterCondition *cond);

GnmFilter 		 *gnm_filter_new	    (Sheet *sheet, GnmRange const *r);
GnmFilter 		 *gnm_filter_dup	    (GnmFilter const *src,
						     Sheet *sheet);
void	   		  gnm_filter_free	    (GnmFilter *filter);
void	   		  gnm_filter_remove	    (GnmFilter *filter);
GnmFilterCondition const *gnm_filter_get_condition  (GnmFilter const *filter, unsigned i);
void	   		  gnm_filter_set_condition  (GnmFilter *filter, unsigned i,
						     GnmFilterCondition *cond,
						     gboolean apply);
gboolean		  gnm_filter_overlaps_range (GnmFilter const *filter, GnmRange const *r);
gboolean		  gnm_filter_overlaps_range (GnmFilter const *filter, GnmRange const *r);

void gnm_sheet_filter_guess_region  (Sheet *sheet, GnmRange *region);
void gnm_sheet_filter_insdel_colrow (Sheet *sheet,
				     gboolean is_cols, gboolean is_insert,
				     int start, int count);

G_END_DECLS

#endif /* _GNM_SHEET_FILTER_H_ */
