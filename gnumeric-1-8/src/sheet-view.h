/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_SHEET_VIEW_H_
# define _GNM_SHEET_VIEW_H_

#include "gnumeric.h"
#include <glib-object.h>

G_BEGIN_DECLS

typedef enum {
	GNM_SHEET_VIEW_NORMAL_MODE,
	GNM_SHEET_VIEW_PAGE_BREAK_MODE,
	GNM_SHEET_VIEW_LAYOUT_MODE
} GnmSheetViewMode;

struct _SheetView {
	GObject  base;

	Sheet	 	*sheet;
	WorkbookView	*sv_wbv;
	GPtrArray	*controls;

	GList		*ants;	/* animated cursors */

	/* an ordered list of Ranges, the first of which corresponds to the
	 * a normalized version of SheetView::{cursor.base_corner:move_corner}
	 */
	GSList		*selections;
	GnmCellPos	 edit_pos;	/* Cell that would be edited */
	GnmCellPos	 edit_pos_real;	/* Even in the middle of a merged cell */
	int		 first_tab_col;	/* where to jump to after an Enter */

	struct {
		/* Static corner to rubber band the selection range around */
		GnmCellPos	 base_corner;
		/* Corner that is moved when the selection range is extended */
		GnmCellPos	 move_corner;
	} cursor;

	GnmCellPos initial_top_left;
	GnmCellPos frozen_top_left;
	GnmCellPos unfrozen_top_left;

	/* state flags */
	unsigned char enable_insert_rows;
	unsigned char enable_insert_cols;
	unsigned char enable_insert_cells;
	unsigned char reposition_selection;

	GnmSheetViewMode	view_mode;

	/* TODO : these should be replaced with Dependents when we support
	 * format based dependents
	 */
	unsigned char selection_content_changed;
	struct {
		unsigned char location;
		unsigned char content; /* entered content NOT value */
		unsigned char style;
	} edit_pos_changed;
	guint            auto_expr_timer;
};

typedef GObjectClass SheetViewClass;

#define SHEET_VIEW_TYPE     (sheet_view_get_type ())
#define SHEET_VIEW(obj)     (G_TYPE_CHECK_INSTANCE_CAST ((obj), SHEET_VIEW_TYPE, SheetView))
#define IS_SHEET_VIEW(o)    (G_TYPE_CHECK_INSTANCE_TYPE ((o), SHEET_VIEW_TYPE))
#define SHEET_VIEW_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), SHEET_VIEW_TYPE, SheetViewClass))

/* Lifecycle */
GType	      sheet_view_get_type (void);
SheetView    *sheet_view_new	  (Sheet *sheet, WorkbookView *wbv);
void	      sv_attach_control	  (SheetView *sv, SheetControl *sc);
void	      sv_detach_control	  (SheetControl *sc);
void	      sv_weak_ref	  (SheetView *sv, SheetView **ptr);
void	      sv_weak_unref	  (SheetView **ptr);
void	      sv_update		  (SheetView *sv);
void          sv_dispose          (SheetView *sv);

/* Information */
Sheet	     *sv_sheet		(SheetView const *sv);
WorkbookView *sv_wbv		(SheetView const *sv);
gboolean      sv_is_frozen	(SheetView const *sv);
GnmFilter    *sv_first_selection_in_filter   (SheetView const *sv);
gboolean      sv_is_region_empty_or_selected (SheetView const *sv,
					      GnmRange const *r);

/* Manipulation */
void	 sv_flag_status_update_pos   (SheetView *sv, GnmCellPos const *pos);
void	 sv_flag_status_update_range (SheetView *sv, GnmRange const *r);
void	 sv_flag_style_update_range  (SheetView *sv, GnmRange const *r);
void	 sv_flag_selection_change    (SheetView *sv);

void	 sv_unant		(SheetView *sv);
void	 sv_ant			(SheetView *sv, GList *ranges);
gboolean sv_selection_copy	(SheetView *sv, WorkbookControl *wbc);
gboolean sv_selection_cut	(SheetView *sv, WorkbookControl *wbc);

void	 sv_make_cell_visible	(SheetView *sv, int col, int row,
				 gboolean couple_panes);
void	 sv_redraw_range	(SheetView *sv, GnmRange const *r);
void	 sv_redraw_headers	(SheetView const *sheet,
				 gboolean col, gboolean row,
				 GnmRange const* r /* optional == NULL */);
void     sv_cursor_set		(SheetView *sv,
				 GnmCellPos const *edit,
				 int base_col, int base_row,
				 int move_col, int move_row,
				 GnmRange const *cursor_bound);
void     sv_set_edit_pos	(SheetView *sv, GnmCellPos const *pos);

void	 sv_freeze_panes	(SheetView *sv,
				 GnmCellPos const *frozen_top_left,
				 GnmCellPos const *unfrozen_top_left);
void	 sv_panes_insdel_colrow (SheetView *sv, gboolean is_cols,
				 gboolean is_insert, int start, int count);
void	 sv_set_initial_top_left(SheetView *sv, int col, int row);

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

G_END_DECLS

#endif /* _GNM_SHEET_VIEW_H_ */
