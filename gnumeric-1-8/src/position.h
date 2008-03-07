/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_POSITION_H_
# define _GNM_POSITION_H_

#include "gnumeric.h"

G_BEGIN_DECLS

struct _GnmEvalPos {
	GnmCellPos		  eval;
	Sheet			 *sheet;
	GnmDependent		 *dep;	 /* optionally NULL */
	GnmExprArrayCorner const *array; /* non-NULL if top level is array */
};

struct _GnmParsePos {
	GnmCellPos  eval;
	Sheet	   *sheet;
	Workbook   *wb;
};

/**
 * Used for getting a valid Sheet *from a GnmCellRef
 * Syntax is GnmCellRef, valid Sheet *
 */
#define eval_sheet(a,b)     (((a) != NULL) ? (a) : (b))

/* Initialization routines for Evaluation Positions */
GnmEvalPos  *eval_pos_init	   (GnmEvalPos *ep, Sheet *s, int col, int row);
GnmEvalPos  *eval_pos_init_pos	   (GnmEvalPos *ep, Sheet *s, GnmCellPos const *pos);
GnmEvalPos  *eval_pos_init_dep	   (GnmEvalPos *ep, GnmDependent const *dep);
GnmEvalPos  *eval_pos_init_cell	   (GnmEvalPos *ep, GnmCell const *cell);
GnmEvalPos  *eval_pos_init_editpos (GnmEvalPos *ep, SheetView const *sv);
GnmEvalPos  *eval_pos_init_sheet   (GnmEvalPos *ep, Sheet *sheet);

/* Initialization routines for Parse Positions */
GnmParsePos *parse_pos_init         (GnmParsePos *pp, Workbook *wb,
				     Sheet *sheet, int col, int row);
GnmParsePos *parse_pos_init_dep	    (GnmParsePos *pp, GnmDependent const *dep);
GnmParsePos *parse_pos_init_cell    (GnmParsePos *pp, GnmCell const *cell);
GnmParsePos *parse_pos_init_evalpos (GnmParsePos *pp, GnmEvalPos const *pos);
GnmParsePos *parse_pos_init_editpos (GnmParsePos *pp, SheetView const *sv);
GnmParsePos *parse_pos_init_sheet   (GnmParsePos *pp, Sheet *sheet);

/*****************************************************************************/

struct _GnmCellRef {
	Sheet *sheet;
	int   col, row;

	unsigned char col_relative;
	unsigned char row_relative;
};
struct _GnmRangeRef {
	GnmCellRef a, b;
};

GnmCellRef *gnm_cellref_init       (GnmCellRef *ref, Sheet *sheet,
				    int col, int row, gboolean rel);
gboolean    gnm_cellref_equal	   (GnmCellRef const *a, GnmCellRef const *b);
guint       gnm_cellref_hash	   (GnmCellRef const *cr);
void        gnm_cellref_make_abs   (GnmCellRef *dest, GnmCellRef const *src,
				    GnmEvalPos const *ep);
void        gnm_cellref_set_col_ar (GnmCellRef *cr, GnmParsePos const *pp,
				    gboolean abs_rel);
void        gnm_cellref_set_row_ar (GnmCellRef *cr, GnmParsePos const *pp,
				    gboolean abs_rel);
int         gnm_cellref_get_col	   (GnmCellRef const *cr, GnmEvalPos const *ep);
int         gnm_cellref_get_row	   (GnmCellRef const *cr, GnmEvalPos const *ep);

gboolean     gnm_rangeref_equal	   (GnmRangeRef const *a, GnmRangeRef const *b);
guint	     gnm_rangeref_hash	   (GnmRangeRef const *cr);
GnmRangeRef *gnm_rangeref_dup	   (GnmRangeRef const *cr);
void         gnm_rangeref_normalize(GnmRangeRef const *rr, GnmEvalPos const *ep,
				    Sheet **start_sheet, Sheet **end_sheet,
				    GnmRange *dest);

guint gnm_cellpos_hash		(GnmCellPos const *key);
gint  gnm_cellpos_equal		(GnmCellPos const *a, GnmCellPos const *b);
void  gnm_cellpos_init_cellref	(GnmCellPos *cp,
				 GnmCellRef const *cr, GnmCellPos const *pos);

G_END_DECLS

#endif /* _GNM_POSITION_H_ */
