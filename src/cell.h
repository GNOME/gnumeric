#ifndef GNUMERIC_CELL_H
#define GNUMERIC_CELL_H

#include <glib.h>
#include "gnumeric.h"
#include "dependent.h"

typedef enum {
    /* MUST BE > 0xffff for dependent */
    /* Cell has an expression (Can we use base.expr == NULL ?)*/
    CELL_HAS_EXPRESSION	   = 0x10000,

    /* Cell is linked into the sheet */
    CELL_IN_SHEET_LIST	   = 0x20000
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
Cell       *cell_copy                    (Cell const *cell);
void        cell_destroy                 (Cell *cell);
void        cell_relocate                (Cell *cell, ExprRewriteInfo *rwinfo);
void        cell_eval_content            (Cell *cell);
void        cell_content_changed         (Cell *cell);

/**
 * Cell state checking
 */
#define	    cell_needs_recalc(cell)	((cell)->base.flags & DEPENDENT_QUEUED_FOR_RECALC)
#define	    cell_expr_is_linked(cell)	((cell)->base.flags & DEPENDENT_IN_EXPR_LIST)
#define	    cell_has_expr(cell)		((cell)->base.flags & CELL_HAS_EXPRESSION)
#define	    cell_is_linked(cell)	((cell)->base.flags & CELL_IN_SHEET_LIST)
CellComment*	 cell_has_comment	(Cell const *cell);
gboolean	 cell_is_blank		(Cell const *cell);
Value *		 cell_is_error		(Cell const *cell);
gboolean	 cell_is_number		(Cell const *cell);
gboolean	 cell_is_zero		(Cell const *cell);
gboolean	 cell_is_partial_array	(Cell const *cell);
ExprArray const *cell_is_array		(Cell const *cell);
StyleHAlignFlags cell_default_halign	(Cell const *v, MStyle const *mstyle);

/**
 * Utilities to assign the contents of a cell
 */
void        cell_set_text                (Cell *c, char const *text);
void        cell_assign_value            (Cell *c, Value *v, StyleFormat *fmt);
void        cell_set_value               (Cell *c, Value *v, StyleFormat *fmt);
void        cell_set_expr_and_value      (Cell *c, ExprTree *expr, Value *v,
					  StyleFormat *opt_fmt);
void        cell_set_expr                (Cell *c, ExprTree *expr,
					  StyleFormat *opt_fmt);
void	    cell_set_expr_unsafe 	 (Cell *cell, ExprTree *expr,
					  StyleFormat *opt_fmt);
void        cell_set_array_formula       (Sheet *sheet, int rowa, int cola,
					  int rowb, int colb,
					  ExprTree *expr,
					  gboolean queue_recalc);

/**
 * Manipulate Cell attributes
 */
MStyle     *cell_get_mstyle              (Cell const *cell);
void        cell_set_mstyle              (Cell const *cell, MStyle *mstyle);

char *      cell_get_format              (Cell const *cell);
void        cell_set_format              (Cell *cell, char const *format);

void        cell_make_value              (Cell *cell);
void        cell_render_value            (Cell *cell, gboolean dynamic_width);

#endif /* GNUMERIC_CELL_H */
