#ifndef GNUMERIC_POSITION_H
#define GNUMERIC_POSITION_H

#include "gnumeric.h"

struct _EvalPos {
	CellPos    eval;
	Sheet     *sheet;
	Dependent *dep; /* optionally NULL */
};

struct _ParsePos {
	CellPos   eval;
	Sheet    *sheet;
	Workbook *wb;
};

/**
 * Used for getting a valid Sheet *from a CellRef
 * Syntax is CellRef, valid Sheet *
 */
#define eval_sheet(a,b)     (((a) != NULL) ? (a) : (b))

/* Initialization routines for Evaluation Positions */
EvalPos  *eval_pos_init		(EvalPos *pp, Sheet *s, CellPos const *pos);
EvalPos  *eval_pos_init_dep	(EvalPos *eval_pos, Dependent const *dep);
EvalPos  *eval_pos_init_cell	(EvalPos *pp, Cell const *cell);
EvalPos  *eval_pos_init_sheet	(EvalPos *pp, Sheet *sheet);

/* Initialization routines for Parse Positions */
ParsePos *parse_pos_init         (ParsePos *pp, Workbook *wb,
				  Sheet *sheet, int col, int row);
ParsePos *parse_pos_init_dep	 (ParsePos *pp, Dependent const *dep);
ParsePos *parse_pos_init_cell    (ParsePos *pp, Cell const *cell);
ParsePos *parse_pos_init_evalpos (ParsePos *pp, EvalPos const *pos);
ParsePos *parse_pos_init_editpos (ParsePos *pp, SheetView const *sv);

/*****************************************************************************/

struct _CellRef {
	Sheet *sheet;
	int   col, row;

	unsigned char col_relative;
	unsigned char row_relative;
};
struct _RangeRef {
	CellRef a, b;
};

CellRef   *cellref_set          (CellRef *ref, Sheet *sheet, int col, int row,
				 gboolean rel);
gboolean  cellref_equal		(CellRef const *a, CellRef const *b);
void      cellref_make_abs	(CellRef *dest,
				 CellRef const *src,
				 EvalPos const *ep);
int       cellref_get_abs_col	(CellRef const *ref,
				 EvalPos const *pos);
int       cellref_get_abs_row	(CellRef const *cell_ref,
				 EvalPos const *src_fp);
void      cellref_get_abs_pos	(CellRef const *cell_ref,
				 CellPos const *pos,
				 CellPos *res);
guint     cellref_hash          (CellRef const *cr);

RangeRef *value_to_rangeref    (Value *v, gboolean release);
void      rangeref_normalize   (RangeRef const *ref, EvalPos const *ep,
				Sheet **start_sheet, Sheet **end_sheet, Range *dest);

#endif /* GNUMERIC_POSITION_H */
