#ifndef GNUMERIC_CELL_COMMENT_H
#define GNUMERIC_CELL_COMMENT_H

#include <glib.h>
#include "gnumeric.h"
#include "str.h"

/**
 * CellComment:
 *
 * Holds the comment string as well as the GnomeCanvasItem marker
 * that appears on the spreadsheet
 */
struct _CellComment {
	String          *comment;
	int             timer_tag;
	void            *window;

	/* A list of GnomeCanvasItems, one per SheetView */
	GList           *realized_list;
};

/* Management routines for comments */
void        cell_set_comment             (Cell *cell, const char *str);
char       *cell_get_comment             (Cell *cell);
void        cell_comment_destroy         (Cell *cell);
void        cell_comment_reposition      (Cell *cell);

/* Deprecated : Cells should be view independent */
void        cell_realize                 (Cell *cell); /* stictly for comments */
void        cell_unrealize               (Cell *cell); /* stictly for comments */

#endif /* GNUMERIC_CELL_H */
