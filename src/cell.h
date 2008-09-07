/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_CELL_H_
# define _GNM_CELL_H_

#include "gnumeric.h"
#include "dependent.h"

G_BEGIN_DECLS

typedef enum {
	/* MUST BE > 0xFFF,FFFF to avoid conflict with GnmDependent */
	/* GnmCell is linked into the sheet */
	GNM_CELL_IN_SHEET_LIST  = 0x10000000,
	/* Is the top left corner of a merged region */
	GNM_CELL_IS_MERGED	    = 0x20000000,
	/* Cells expression was changed, recalc before rendering */
	GNM_CELL_HAS_NEW_EXPR    = 0x40000000
} GnmCellFlags;

/* Definition of a GnmCell */
#define GNM_DEP_TO_CELL(dep)	((GnmCell *)(dep))
#define GNM_CELL_TO_DEP(cell)	(&(cell)->base)
struct _GnmCell {
	GnmDependent base;

	/* Mandatory state information */
	GnmCellPos     pos;
	ColRowInfo    *row_info;

	GnmValue      *value;	/* computed or entered (Must be non NULL) */
	GnmRenderedValue *rendered_value;
};

/**
 * GnmCell state checking
 */
#define	    gnm_cell_needs_recalc(cell)	((cell)->base.flags & DEPENDENT_NEEDS_RECALC)
#define	    gnm_cell_expr_is_linked(cell)	((cell)->base.flags & DEPENDENT_IS_LINKED)
#define	    gnm_cell_has_expr(cell)		((cell)->base.texpr != NULL)
#define	    gnm_cell_is_merged(cell)	((cell)->base.flags & GNM_CELL_IS_MERGED)
gboolean    gnm_cell_is_empty	  (GnmCell const *cell);
gboolean    gnm_cell_is_blank	  (GnmCell const *cell); /* empty, or "" */
GnmValue   *gnm_cell_is_error	  (GnmCell const *cell);
gboolean    gnm_cell_is_number	  (GnmCell const *cell);
gboolean    gnm_cell_is_zero	  (GnmCell const *cell);

gboolean    gnm_cell_is_array	  (GnmCell const *cell);
gboolean    gnm_cell_is_nonsingleton_array (GnmCell const *cell);
GnmExprArrayCorner const *
	    gnm_cell_is_array_corner  (GnmCell const *cell);
gboolean    gnm_cell_array_bound	  (GnmCell const *cell, GnmRange *res);

#define gnm_cell_eval(cell)									     \
	do {											     \
		if (gnm_cell_needs_recalc (cell)) {						     \
			gnm_cell_eval_content (cell);						     \
			cell->base.flags &= ~(DEPENDENT_NEEDS_RECALC | (int)GNM_CELL_HAS_NEW_EXPR ); \
		}										     \
	} while (0)

/**
 * Utilities to assign the contents of a cell
 */
void gnm_cell_set_text		(GnmCell *c, char const *text);
void gnm_cell_assign_value	(GnmCell *c, GnmValue *v);
void gnm_cell_set_value		(GnmCell *c, GnmValue *v);
void gnm_cell_set_expr_and_value(GnmCell *c,
				 GnmExprTop const *texpr, GnmValue *v,
				 gboolean link_expr);
void gnm_cell_set_expr		(GnmCell *c, GnmExprTop const *texpr);
void gnm_cell_set_expr_unsafe	(GnmCell *cell, GnmExprTop const *texpr);
void gnm_cell_set_array_formula	(Sheet *sheet,
				 int cola, int rowa, int colb, int rowb,
				 GnmExprTop const *texpr);
void gnm_cell_cleanout		(GnmCell *cell);
void gnm_cell_convert_expr_to_value	(GnmCell *cell);

/**
 * Manipulate GnmCell attributes
 */
GnmStyle const *gnm_cell_get_style	(GnmCell const *cell);
GOFormat *gnm_cell_get_format		(GnmCell const *cell);
void	gnm_cell_set_format		(GnmCell *cell, char const *format);

void	gnm_cell_render_value		(GnmCell *cell, gboolean allow_variable_width);
int	gnm_cell_rendered_height	(GnmCell const * cell);
int	gnm_cell_rendered_width		(GnmCell const * cell);	/* excludes offset */
int	gnm_cell_rendered_offset	(GnmCell const * cell);
GOColor gnm_cell_get_render_color	(GnmCell const * cell);
char *	gnm_cell_get_entered_text	(GnmCell const * cell);
char *  gnm_cell_get_rendered_text	(GnmCell *cell);

G_END_DECLS

#endif /* _GNM_CELL_H_ */
