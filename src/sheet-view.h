#ifndef GNUMERIC_SHEET_VIEW_H
#define GNUMERIC_SHEET_VIEW_H

#include "gnumeric.h"
#include <gtk/gtkobject.h>

struct _SheetView {
	GtkObject  base;

	Sheet	  *s;
	GPtrArray *controls;

	CellPos	 edit_pos;	/* Cell that would be edited */
	CellPos	 edit_pos_real;	/* Even in the middle of a merged cell */

	struct {
		/* Static corner to rubber band the selection range around */
		CellPos	 base_corner;
		/* Corner that is moved when the selection range is extended */
		CellPos	 move_corner;
	} cursor;

	GList  *selections; /* The set of selected ranges in LIFO order */
	GList  *ants; /* set of animated cursors */

	CellPos initial_top_left;
	CellPos frozen_top_left;
	CellPos unfrozen_top_left;

	/* preferences */
	gboolean    display_formulas;
	gboolean    hide_zero;
	gboolean    hide_grid;
	gboolean    hide_col_header;
	gboolean    hide_row_header;

	gboolean    display_outlines;
	gboolean    outline_symbols_below;
	gboolean    outline_symbols_right;

};

typedef struct {
	GtkObjectClass   gtk_object_class;
} SheetViewClass;

#define SHEET_VIEW_TYPE     (sheet_view_get_type ())
#define SHEET_VIEW(obj)     (G_TYPE_CHECK_INSTANCE_CAST ((obj), SHEET_VIEW_TYPE, SheetView))
#define IS_SHEET_VIEW(o)    (G_TYPE_CHECK_INSTANCE_TYPE ((o), SHEET_VIEW_TYPE))
#define SHEET_VIEW_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), SHEET_VIEW_TYPE, SheetViewClass))

/* Lifecycle */
GType	   sheet_view_get_type	 (void);
SheetView *sheet_view_new	 (Sheet *sheet);
void	   sheet_view_init       (SheetView *sv, Sheet *sheet);
void	   s_view_attach_control (SheetView *sv, SheetControl *sc);
void	   s_view_detach_control (SheetControl *sc);

/* Information */
Sheet	*s_view_sheet (SheetView *sv);

#define SHEET_VIEW_FOREACH_CONTROL(sv, control, code)				\
do {										\
	int j;									\
	GPtrArray *s_controls = sv->s_controls;					\
	if (s_controls != NULL) /* Reverse is important during destruction */	\
		for (j = s_controls->len; j-- > 0 ;) {				\
			SheetControl *control =					\
				g_ptr_array_index (s_controls, j);		\
			code							\
		}								\
} while (0)

#endif /* GNUMERIC_SHEET_VIEW_H */
