#ifndef GNUMERIC_POSITION_H
#define GNUMERIC_POSITION_H

#include "gnumeric.h"

struct _EvalPos {
	GnmCellPos   eval;
	Sheet      *sheet;
	GnmDependent  *dep; /* optionally NULL */
};

struct _ParsePos {
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
EvalPos  *eval_pos_init		(EvalPos *ep, Sheet *s, GnmCellPos const *pos);
EvalPos  *eval_pos_init_dep	(EvalPos *ep, GnmDependent const *dep);
EvalPos  *eval_pos_init_cell	(EvalPos *ep, GnmCell const *cell);
EvalPos  *eval_pos_init_sheet	(EvalPos *ep, Sheet *sheet);

/* Initialization routines for Parse Positions */
ParsePos *parse_pos_init         (ParsePos *pp, Workbook *wb,
				  Sheet *sheet, int col, int row);
ParsePos *parse_pos_init_dep	 (ParsePos *pp, GnmDependent const *dep);
ParsePos *parse_pos_init_cell    (ParsePos *pp, GnmCell const *cell);
ParsePos *parse_pos_init_evalpos (ParsePos *pp, EvalPos const *pos);
ParsePos *parse_pos_init_editpos (ParsePos *pp, SheetView const *sv);
ParsePos *parse_pos_init_sheet	 (ParsePos *pp, Sheet *sheet);

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

GnmCellRef *cellref_init        (GnmCellRef *ref, Sheet *sheet, int col, int row,
				 gboolean rel);
gboolean    cellref_equal	(GnmCellRef const *a, GnmCellRef const *b);
guint       cellref_hash        (GnmCellRef const *cr);
void        cellref_make_abs	(GnmCellRef *dest,
				 GnmCellRef const *src,
				 EvalPos const *ep);
int         cellref_get_abs_col	(GnmCellRef const *ref,
				 EvalPos const *pos);
int         cellref_get_abs_row	(GnmCellRef const *cell_ref,
				 EvalPos const *src_fp);
void        cellref_get_abs_pos	(GnmCellRef const *cell_ref,
				 GnmCellPos const *pos,
				 GnmCellPos *res);

gboolean     rangeref_equal	(GnmRangeRef const *a, GnmRangeRef const *b);
guint	     rangeref_hash	(GnmRangeRef const *cr);
GnmRangeRef *rangeref_dup	(GnmRangeRef const *cr);
void         rangeref_normalize (GnmRangeRef const *ref, EvalPos const *ep,
				 Sheet **start_sheet, Sheet **end_sheet,
				 GnmRange *dest);

#endif /* GNUMERIC_POSITION_H */
