/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef GNUMERIC_FILTER_H
#define GNUMERIC_FILTER_H

#include "gnumeric.h"
#include "expr.h"

typedef enum {
	GNM_FILTER_UNUSED	= -1,

	GNM_FILTER_OP_EQUAL	= GNM_EXPR_OP_EQUAL,
	GNM_FILTER_OP_GT	= GNM_EXPR_OP_GT,
	GNM_FILTER_OP_LT	= GNM_EXPR_OP_LT,
	GNM_FILTER_OP_GTE	= GNM_EXPR_OP_GTE,
	GNM_FILTER_OP_LTE	= GNM_EXPR_OP_LTE,
	GNM_FILTER_OP_NOT_EQUAL	= GNM_EXPR_OP_NOT_EQUAL,

	GNM_FILTER_OP_REGEXP_MATCH	= 0x10,
	GNM_FILTER_OP_REGEXP_NOT_MATCH	= 0x11,
	GNM_FILTER_OP_BLANKS		= 0x20,
	GNM_FILTER_OP_NON_BLANKS	= 0x21,
	GNM_FILTER_OP_TOP_N		= 0x40,
	GNM_FILTER_OP_BOTTOM_N		= 0x41,
	GNM_FILTER_OP_TOP_N_PERCENT	= 0x42,
	GNM_FILTER_OP_BOTTOM_N_PERCENT	= 0x43,
	GNM_FILTER_OP_TYPE_MASK		= 0x70
} GnmFilterOp;

typedef struct _GnmFilterCondition GnmFilterCondition;

GnmFilterCondition *gnm_filter_condition_new_single (GnmFilterOp op, Value *v);
GnmFilterCondition *gnm_filter_condition_new_double (GnmFilterOp op1, Value *v1,
						     gboolean join_with_and,
						     GnmFilterOp op2, Value *v2);
GnmFilterCondition *gnm_filter_condition_new_bucket (gboolean top,
						     gboolean absolute,
						     unsigned n);
void		    gnm_filter_condition_unref 	    (GnmFilterCondition *cond);

GnmFilter *gnm_filter_new		(Sheet *sheet, Range const *r);
void	   gnm_filter_free		(GnmFilter *filter);
void	   gnm_filter_remove		(GnmFilter *filter);
void	   gnm_filter_set_condition	(GnmFilter *filter, unsigned i,
					 GnmFilterCondition *cond,
					 gboolean apply);

gboolean   gnm_filter_contains_row (GnmFilter const *filter, int row);

#endif /* GNUMERIC_FILTER_H */
