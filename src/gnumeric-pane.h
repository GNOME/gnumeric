#ifndef GNUMERIC_PANE_H
#define GNUMERIC_PANE_H

#include "gui-gnumeric.h"

struct _GnumericPane {
	GList		*anted_cursors;
	int		 index;
	GnumericSheet	*gsheet;
	struct {
		GnomeCanvas *canvas;
		ItemBar     *item;
	} col, row;

	/* Lines for resizing cols and rows */
	struct {
		GtkObject         *guide;
		GtkObject         *start;
		GnomeCanvasPoints *points;
	} colrow_resize;
};

void gnumeric_pane_init		(GnumericPane *pane, SheetControlGUI *scg,
				 gboolean headers, int index);
void gnumeric_pane_release	(GnumericPane *pane);
void gnumeric_pane_set_bounds	(GnumericPane *pane,
				 int start_col, int start_row,
				 int end_col, int end_row);

void gnumeric_pane_colrow_resize_end	(GnumericPane *pane);
void gnumeric_pane_colrow_resize_start	(GnumericPane *pane,
					 gboolean is_cols, int resize_pos);
void gnumeric_pane_colrow_resize_move	(GnumericPane *pane,
					 gboolean is_cols, int resize_pos);

#endif /* GNUMERIC_PANE_H */
