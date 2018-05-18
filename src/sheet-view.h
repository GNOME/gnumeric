#ifndef _GNM_SHEET_VIEW_H_
# define _GNM_SHEET_VIEW_H_

#include <gnumeric.h>
#include <glib-object.h>

G_BEGIN_DECLS

typedef enum {
	GNM_SHEET_VIEW_NORMAL_MODE,
	GNM_SHEET_VIEW_PAGE_BREAK_MODE,
	GNM_SHEET_VIEW_LAYOUT_MODE
} GnmSheetViewMode;

struct _SheetView {
	GObject  base;

	Sheet		*sheet;
	WorkbookView	*sv_wbv;
	GPtrArray	*controls;

	GList		*ants;	/* animated cursors */

	/* an ordered list of Ranges, the first of which corresponds to the
	 * a normalized version of SheetView::{cursor.base_corner:move_corner}
	 */
	GSList		*selections;
	GSList		*selections_simplified;
	int              selection_mode; /* GnmSelectionMode */

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

typedef struct {
	GObjectClass parent_class;
} SheetViewClass;

#define GNM_SHEET_VIEW_TYPE     (gnm_sheet_view_get_type ())
#define GNM_SHEET_VIEW(obj)     (G_TYPE_CHECK_INSTANCE_CAST ((obj), GNM_SHEET_VIEW_TYPE, SheetView))
#define GNM_IS_SHEET_VIEW(o)    (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_SHEET_VIEW_TYPE))

/* Lifecycle */
GType	      gnm_sheet_view_get_type (void);
SheetView    *gnm_sheet_view_new	  (Sheet *sheet, WorkbookView *wbv);
void	      gnm_sheet_view_attach_control  (SheetView *sv, SheetControl *sc);
void	      gnm_sheet_view_detach_control  (SheetView *sv, SheetControl *sc);
void	      gnm_sheet_view_weak_ref	  (SheetView *sv, SheetView **ptr);
void	      gnm_sheet_view_weak_unref	  (SheetView **ptr);
void	      gnm_sheet_view_update	  (SheetView *sv);
void          gnm_sheet_view_dispose      (SheetView *sv);

/* Information */
Sheet	     *sv_sheet		(SheetView const *sv);
WorkbookView *sv_wbv		(SheetView const *sv);
gboolean      gnm_sheet_view_is_frozen	(SheetView const *sv);

GnmFilter      *gnm_sheet_view_editpos_in_filter (SheetView const *sv);
GnmFilter      *gnm_sheet_view_selection_intersects_filter_rows (SheetView const *sv);
GnmRange       *gnm_sheet_view_selection_extends_filter (SheetView const *sv,
					     GnmFilter const *f);
GnmSheetSlicer *gnm_sheet_view_editpos_in_slicer (SheetView const *sv);

/* Manipulation */
void	 gnm_sheet_view_flag_status_update_pos   (SheetView *sv, GnmCellPos const *pos);
void	 gnm_sheet_view_flag_status_update_range (SheetView *sv, GnmRange const *range);
void	 gnm_sheet_view_flag_style_update_range  (SheetView *sv, GnmRange const *range);
void	 gnm_sheet_view_flag_selection_change    (SheetView *sv);

void	 gnm_sheet_view_unant	     (SheetView *sv);
void	 gnm_sheet_view_ant	     (SheetView *sv, GList *ranges);
gboolean gnm_sheet_view_selection_copy	(SheetView *sv, WorkbookControl *wbc);
gboolean gnm_sheet_view_selection_cut	(SheetView *sv, WorkbookControl *wbc);

void	 gnm_sheet_view_make_cell_visible	(SheetView *sv, int col, int row,
				 gboolean couple_panes);
void	 gnm_sheet_view_redraw_range	(SheetView *sv, GnmRange const *r);
void	 gnm_sheet_view_redraw_headers	(SheetView const *sheet,
				 gboolean col, gboolean row,
				 GnmRange const* r);
void     gnm_sheet_view_resize		(SheetView *sv, gboolean force_scroll);
void     gnm_sheet_view_cursor_set		(SheetView *sv,
				 GnmCellPos const *edit,
				 int base_col, int base_row,
				 int move_col, int move_row,
				 GnmRange const *bound);
void     gnm_sheet_view_set_edit_pos	(SheetView *sv, GnmCellPos const *pos);

void	 gnm_sheet_view_freeze_panes	(SheetView *sv,
				 GnmCellPos const *frozen_top_left,
				 GnmCellPos const *unfrozen_top_left);
void	 gnm_sheet_view_panes_insdel_colrow (SheetView *sv, gboolean is_cols,
				 gboolean is_insert, int start, int count);
void	 gnm_sheet_view_set_initial_top_left(SheetView *sv, int col, int row);

#define SHEET_VIEW_FOREACH_CONTROL(sv, control, code)		\
do {								\
	int ctrlno;						\
	GPtrArray *controls = (sv)->controls;			\
	for (ctrlno = controls->len; ctrlno-- > 0 ;) {		\
		SheetControl *control =				\
			g_ptr_array_index (controls, ctrlno);	\
		code						\
	}							\
} while (0)

G_END_DECLS

#endif /* _GNM_SHEET_VIEW_H_ */
