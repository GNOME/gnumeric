#ifndef GNUMERIC_CELL_H
#define GNUMERIC_CELL_H

#include "gnumeric.h"
#include "dependent.h"
#include <pango/pango.h>

typedef enum {
	/* MUST BE > 0xFFF,FFFF to avoid conflict with GnmDependent */
	/* GnmCell is linked into the sheet */
	CELL_IN_SHEET_LIST  = 0x10000000,
	/* Is the top left corner of a merged region */
	CELL_IS_MERGED	    = 0x20000000,
} GnmCellFlags;

/* Definition of a GnmCell */
#define DEP_TO_CELL(dep)	((GnmCell *)(dep))
#define CELL_TO_DEP(cell)	((GnmDependent *)(cell))
struct _GnmCell {
	GnmDependent base;

	/* Mandatory state information */
	GnmCellPos     pos;
	ColRowInfo    *col_info;
	ColRowInfo    *row_info;

	GnmValue      *value;	/* computed or entered (Must be non NULL) */
	RenderedValue *rendered_value;
};

/**
 * Manage cells
 */
GnmCell  *cell_new      (void);
GnmCell	 *cell_copy	(GnmCell const *cell);
void	  cell_destroy  (GnmCell *cell);
void	  cell_relocate (GnmCell *cell, GnmExprRewriteInfo const *rwinfo);

/**
 * GnmCell state checking
 */
#define	    cell_needs_recalc(cell)	((cell)->base.flags & DEPENDENT_NEEDS_RECALC)
#define	    cell_expr_is_linked(cell)	((cell)->base.flags & DEPENDENT_IS_LINKED)
#define	    cell_has_expr(cell)		((cell)->base.expression != NULL)
#define	    cell_is_linked(cell)	((cell)->base.flags & CELL_IN_SHEET_LIST)
#define	    cell_is_merged(cell)	((cell)->base.flags & CELL_IS_MERGED)
GnmComment *cell_has_comment_pos  (Sheet const *sheet, GnmCellPos const *pos);
GnmComment *cell_has_comment	  (GnmCell const *cell);
gboolean    cell_is_empty	  (GnmCell const *cell);
gboolean    cell_is_blank	  (GnmCell const *cell); /* empty, or "" */
GnmValue   *cell_is_error	  (GnmCell const *cell);
gboolean    cell_is_number	  (GnmCell const *cell);
gboolean    cell_is_zero	  (GnmCell const *cell);
gboolean    cell_is_partial_array (GnmCell const *cell);
GnmExprArray const *cell_is_array (GnmCell const *cell);

/**
 * Utilities to assign the contents of a cell
 */
void cell_set_text		(GnmCell *c, char const *text);
void cell_assign_value		(GnmCell *c, GnmValue *v);
void cell_set_value		(GnmCell *c, GnmValue *v);
void cell_set_expr_and_value	(GnmCell *c, GnmExpr const *expr, GnmValue *v,
				 gboolean link_expr);
void cell_set_expr		(GnmCell *c, GnmExpr const *expr);
void cell_set_expr_unsafe 	(GnmCell *cell, GnmExpr const *expr);
void cell_set_array_formula	(Sheet *sheet,
				 int cola, int rowa, int colb, int rowb,
				 GnmExpr const *expr);
void cell_convert_expr_to_value	(GnmCell *cell);

/**
 * Manipulate GnmCell attributes
 */
MStyle *cell_get_mstyle		(GnmCell const *cell);
StyleFormat *cell_get_format	(GnmCell const *cell);
void	cell_set_format		(GnmCell *cell, char const *format);

void	cell_render_value	(GnmCell *cell, gboolean dynamic_width);
int	cell_rendered_height	(GnmCell const * cell);
int	cell_rendered_width	(GnmCell const * cell);	/* excludes offset */
int	cell_rendered_offset	(GnmCell const * cell);
PangoColor const* cell_get_render_color	(GnmCell const * cell);
char *	cell_get_entered_text	(GnmCell const * cell);
char *  cell_get_rendered_text  (GnmCell *cell);

guint cellpos_hash (GnmCellPos const *key);
gint  cellpos_cmp  (GnmCellPos const *a, GnmCellPos const *b);

void cell_init (void);
void cell_shutdown (void);

#endif /* GNUMERIC_CELL_H */
