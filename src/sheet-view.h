#ifndef GNUMERIC_SHEET_VIEW_H
#define GNUMERIC_SHEET_VIEW_H

#include "gnumeric.h"
#include <gtk/gtkobject.h>

struct _SheetView {
	GObject  base;

	Sheet	 	*sheet;
	WorkbookView	*wbv;
	GPtrArray	*controls;

	GList		*ants;	/* animated cursors */

	/* an ordered list of Ranges, the first of which corresponds to the
	 * range sheet->cursor.base_corner:move_corner
	 */
	GList	*selections;
	CellPos	 edit_pos;	/* Cell that would be edited */
	CellPos	 edit_pos_real;	/* Even in the middle of a merged cell */

	struct {
		/* Static corner to rubber band the selection range around */
		CellPos	 base_corner;
		/* Corner that is moved when the selection range is extended */
		CellPos	 move_corner;
	} cursor;

	/* state flags */
	gboolean         enable_insert_rows		: 1;
	gboolean         enable_insert_cols		: 1;
	gboolean         enable_insert_cells		: 1;
	gboolean	 selection_content_changed	: 1;
	gboolean	 reposition_selection		: 1;
	struct {
		gboolean location;
		gboolean content; /* entered content NOT value */
		gboolean format;
	} edit_pos_changed;
	guint            auto_expr_timer;
};

typedef struct {
	GObjectClass   g_object_class;
} SheetViewClass;

#define SHEET_VIEW_TYPE     (sheet_view_get_type ())
#define SHEET_VIEW(obj)     (G_TYPE_CHECK_INSTANCE_CAST ((obj), SHEET_VIEW_TYPE, SheetView))
#define IS_SHEET_VIEW(o)    (G_TYPE_CHECK_INSTANCE_TYPE ((o), SHEET_VIEW_TYPE))
#define SHEET_VIEW_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), SHEET_VIEW_TYPE, SheetViewClass))

/* Lifecycle */
GType	   sheet_view_get_type	 (void);
SheetView *sheet_view_new	 (Sheet *sheet, WorkbookView *wbv);
void	   sv_attach_control	(SheetView *sv, SheetControl *sc);
void	   sv_detach_control	(SheetControl *sc);
void	   sv_weak_ref		(SheetView *sv, SheetView **ptr);
void	   sv_weak_unref	(SheetView **ptr);
void	   sv_update		(SheetView *sv);

/* Information */
Sheet	     *sv_sheet (SheetView const *sv);
WorkbookView *sv_wbv   (SheetView const *sv);
gboolean      sv_is_region_empty_or_selected (SheetView const *sv,
					      Range const *r);

/* Manipulation */
void	 sv_flag_status_update_pos   (SheetView *sv, CellPos const *pos);
void	 sv_flag_status_update_range (SheetView *sv, Range const *r);
void	 sv_flag_format_update_range (SheetView *sv, Range const *r);
void	 sv_flag_selection_change    (SheetView *sv);

void	 sv_unant		(SheetView *sv);
void	 sv_ant			(SheetView *sv, GList *ranges);
gboolean sv_selection_copy	(SheetView *sv, WorkbookControl *wbc);
gboolean sv_selection_cut	(SheetView *sv, WorkbookControl *wbc);

void	 sv_make_cell_visible	(SheetView *sv, int col, int row,
				 gboolean couple_panes);
void	 sv_redraw_range	(SheetView *sv, Range const *r);
void	 sv_redraw_headers	(SheetView const *sheet,
				 gboolean col, gboolean row,
				 Range const* r /* optional == NULL */);
void     sv_cursor_set		(SheetView *sv,
				 CellPos const *edit,
				 int base_col, int base_row,
				 int move_col, int move_row,
				 Range const *cursor_bound);
void     sv_set_edit_pos	(SheetView *sv, CellPos const *pos);

#define SHEET_VIEW_FOREACH_CONTROL(sv, control, code)				\
do {										\
	int j;									\
	GPtrArray *controls = (sv)->controls;					\
	if (controls != NULL) /* Reverse is important during destruction */	\
		for (j = controls->len; j-- > 0 ;) {				\
			SheetControl *control =					\
				g_ptr_array_index (controls, j);		\
			code							\
		}								\
} while (0)

#endif /* GNUMERIC_SHEET_VIEW_H */
