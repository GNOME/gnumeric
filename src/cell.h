#ifndef GNUMERIC_CELL_H
#define GNUMERIC_CELL_H

#include "gnumeric.h"
#include "eval.h"

typedef enum {
	/* MUST BE > 0xffff for dependent */
	/* Cell has an expression (Can we use base.expr == NULL ?)*/
	CELL_HAS_EXPRESSION = 0x010000,

	/* Cell is linked into the sheet */
	CELL_IN_SHEET_LIST  = 0x020000,

	/* Is the top left corner of a merged region */
	CELL_IS_MERGED	    = 0x040000,

	/* Cell is in the midst of a cyclic calculation */
	CELL_BEING_ITERATED = 0x080000,

	/* Cell content spans */
	CELL_CONTENT_SPANS  = 0x100000,
} CellFlags;

/* Definition of a Gnumeric Cell */
#define DEP_TO_CELL(dep)	((Cell *)(dep))
#define CELL_TO_DEP(cell)	((Dependent *)(cell))
struct _Cell {
	Dependent base;
	/* Mandatory state information */
	CellPos	     pos;
	ColRowInfo  *col_info;
	ColRowInfo  *row_info;

	Value         *value;	/* computed or entered (Must be non NULL) */
	RenderedValue *rendered_value;

	/*
	 * For exprs  = The prefered output format
	 * For values = format that parsed the input
	 *              also used to regenerate the entered
	 *              text for editing.
	 */
	StyleFormat *format;
};

/**
 * Manage cells
 */
Cell	 *cell_copy	    (Cell const *cell);
void	  cell_destroy      (Cell *cell);
void	  cell_relocate     (Cell *cell, ExprRewriteInfo *rwinfo);
gboolean  cell_eval_content (Cell *cell);

/**
 * Cell state checking
 */
#define	    cell_needs_recalc(cell)	((cell)->base.flags & DEPENDENT_NEEDS_RECALC)
#define	    cell_expr_is_linked(cell)	((cell)->base.flags & DEPENDENT_IN_EXPR_LIST)
#define	    cell_has_expr(cell)		((cell)->base.flags & CELL_HAS_EXPRESSION)
#define	    cell_is_linked(cell)	((cell)->base.flags & CELL_IN_SHEET_LIST)
#define	    cell_is_merged(cell)	((cell)->base.flags & CELL_IS_MERGED)
CellComment     *cell_has_comment_pos   (const Sheet *sheet, const CellPos *pos);
CellComment     *cell_has_comment	(Cell const *cell);
gboolean	 cell_is_blank		(Cell const *cell);
Value *		 cell_is_error		(Cell const *cell);
gboolean	 cell_is_number		(Cell const *cell);
gboolean	 cell_is_zero		(Cell const *cell);
gboolean	 cell_is_partial_array	(Cell const *cell);
ExprArray const *cell_is_array		(Cell const *cell);

/**
 * Utilities to assign the contents of a cell
 */
void cell_set_text		(Cell *c, char const *text);
void cell_assign_value		(Cell *c, Value *v, StyleFormat *fmt);
void cell_set_value		(Cell *c, Value *v, StyleFormat *fmt);
void cell_set_expr_and_value	(Cell *c, ExprTree *expr, Value *v,
				 StyleFormat *opt_fmt, gboolean link_expr);
void cell_set_expr		(Cell *c, ExprTree *expr,
				 StyleFormat *opt_fmt);
void cell_set_expr_unsafe 	(Cell *cell, ExprTree *expr,
				 StyleFormat *opt_fmt);
void cell_set_array_formula	(Sheet *sheet,
				 int cola, int rowa, int colb, int rowb,
				 ExprTree *expr);
void cell_convert_expr_to_value	(Cell *cell);

/**
 * Manipulate Cell attributes
 */
MStyle *cell_get_mstyle		(Cell const *cell);
char *	cell_get_format		(Cell const *cell);
void	cell_set_format		(Cell *cell, char const *format);

void	cell_render_value	(Cell *cell, gboolean dynamic_width);
int	cell_rendered_width	(Cell const * cell);
int	cell_rendered_offset	(Cell const * cell);
int	cell_rendered_height	(Cell const * cell);
char *	cell_get_rendered_text	(Cell const * cell);
char *	cell_get_entered_text	(Cell const * cell);

guint cellpos_hash (CellPos const *key);
gint  cellpos_cmp  (CellPos const *a, CellPos const *b);

#endif /* GNUMERIC_CELL_H */
