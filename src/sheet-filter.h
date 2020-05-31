#ifndef _GNM_SHEET_FILTER_H_
# define _GNM_SHEET_FILTER_H_

#include <gnumeric.h>
#include <goffice/goffice.h>

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
	/* Next two added in 1.11.6 */
	GNM_FILTER_OP_TOP_N_PERCENT_N	= 0x34,
	GNM_FILTER_OP_BOTTOM_N_PERCENT_N	= 0x35,
	GNM_FILTER_OP_BOTTOM_MASK	= 0x01,
	GNM_FILTER_OP_REL_N_MASK	= 0x04,
	GNM_FILTER_OP_PERCENT_MASK	= 0x06,

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
	double	 count;
};

struct _GnmFilter {
	int ref_count;
	Sheet *sheet;
	GnmRange  r;

	GPtrArray *fields;
	gboolean   is_active;
};

GType               gnm_filter_condition_get_type   (void);
GnmFilterCondition *gnm_filter_condition_dup        (GnmFilterCondition const *src);
void                gnm_filter_condition_free       (GnmFilterCondition *cond);
GnmFilterCondition *gnm_filter_condition_new_single (GnmFilterOp op, GnmValue *v);
GnmFilterCondition *gnm_filter_condition_new_double (GnmFilterOp op0, GnmValue *v0,
						     gboolean join_with_and,
						     GnmFilterOp op1, GnmValue *v1);
GnmFilterCondition *gnm_filter_condition_new_bucket (gboolean top,
						     gboolean absolute,
						     gboolean rel_range,
						     double n);

GType                     gnm_filter_get_type       (void);
GnmFilter		 *gnm_filter_new	    (Sheet *sheet,
						     GnmRange const *r,
						     gboolean attach);
GnmFilter		 *gnm_filter_dup	    (GnmFilter const *src,
						     Sheet *sheet);
GnmFilter *               gnm_filter_ref            (GnmFilter *filter);
void			  gnm_filter_unref	    (GnmFilter *filter);
void			  gnm_filter_remove	    (GnmFilter *filter);
void			  gnm_filter_attach	    (GnmFilter *filter, Sheet *sheet);
GnmFilterCondition const *gnm_filter_get_condition  (GnmFilter const *filter, unsigned i);
void			  gnm_filter_set_condition  (GnmFilter *filter, unsigned i,
						     GnmFilterCondition *cond,
						     gboolean apply);
void                      gnm_filter_reapply        (GnmFilter *filter);

GnmFilter *gnm_sheet_filter_at_pos  (Sheet const *sheet, GnmCellPos const *pos);
GnmFilter *gnm_sheet_filter_intersect_rows  (Sheet const *sheet,
					     int from, int to);
GnmRange  *gnm_sheet_filter_can_be_extended (Sheet const *sheet,
					     GnmFilter const *f,
					     GnmRange const *r);
void gnm_sheet_filter_insdel_colrow (Sheet *sheet,
				     gboolean is_cols, gboolean is_insert,
				     int start, int count,
				     GOUndo **pundo);

G_END_DECLS

#endif /* _GNM_SHEET_FILTER_H_ */
