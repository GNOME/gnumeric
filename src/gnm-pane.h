#ifndef _GNM_PANE_H_
# define _GNM_PANE_H_

#include <gnumeric-fwd.h>
#include <goffice/goffice.h>

G_BEGIN_DECLS

#define GNM_PANE_TYPE	(gnm_pane_get_type ())
#define GNM_PANE(o)	(G_TYPE_CHECK_INSTANCE_CAST((o), GNM_PANE_TYPE, GnmPane))
#define GNM_IS_PANE(o)	(G_TYPE_CHECK_INSTANCE_TYPE((o), GNM_PANE_TYPE))

GType	 gnm_pane_get_type (void);
GnmPane *gnm_pane_new (SheetControlGUI *scg,
		       gboolean col_headers, gboolean row_headers, int index);

int   gnm_pane_find_col		(GnmPane const *pane, gint64 x, gint64 *col_origin);
int   gnm_pane_find_row		(GnmPane const *pane, gint64 y, gint64 *row_origin);
void  gnm_pane_redraw_range	(GnmPane *pane, GnmRange const *r);
void  gnm_pane_compute_visible_region (GnmPane *pane, gboolean full_recompute);
void  gnm_pane_bound_set	(GnmPane *pane,
				 int start_col, int start_row,
				 int end_col, int end_row);



void	gnm_pane_edit_start  (GnmPane *p);
void	gnm_pane_edit_stop   (GnmPane *p);

void	gnm_pane_size_guide_start  (GnmPane *p, gboolean vert, int colrow, gboolean is_colrow_resize);
void	gnm_pane_size_guide_motion	(GnmPane *p, gboolean vert, gint64 guide_pos);
void	gnm_pane_size_guide_stop	(GnmPane *p);

void	 gnm_pane_reposition_cursors	   (GnmPane *pane);
gboolean gnm_pane_cursor_bound_set	   (GnmPane *pane, GnmRange const *r);
gboolean gnm_pane_rangesel_bound_set	   (GnmPane *pane, GnmRange const *r);
void	 gnm_pane_rangesel_start	   (GnmPane *pane, GnmRange const *r);
void	 gnm_pane_rangesel_stop		   (GnmPane *pane);
gboolean gnm_pane_special_cursor_bound_set (GnmPane *pane, GnmRange const *r);
void	 gnm_pane_special_cursor_start	   (GnmPane *pane, int style, int button);
void	 gnm_pane_special_cursor_stop	   (GnmPane *pane);
void	 gnm_pane_mouse_cursor_set         (GnmPane *pane, GdkCursor *c);
void	 gnm_pane_expr_cursor_bound_set    (GnmPane *pane, GnmRange const *r,
					    GOColor color);
void	 gnm_pane_expr_cursor_stop	   (GnmPane *pane);

/************************************************************************/

void gnm_pane_objects_drag        (GnmPane *pane, SheetObject *so,
				   gdouble new_x, gdouble new_y,int drag_type,
				   gboolean symmetric,gboolean snap_to_grid);
void gnm_pane_object_unselect	  (GnmPane *pane, SheetObject *so);
void gnm_pane_object_update_bbox  (GnmPane *pane, SheetObject *so);
void gnm_pane_object_start_resize (GnmPane *pane, int button,
                                   guint64 x, gint64 y,
				   SheetObject *so, int drag_type,
				   gboolean is_creation);
void gnm_pane_object_autoscroll	  (GnmPane *pane, GdkDragContext *context,
				   gint x, gint y, guint time);

GocGroup *gnm_pane_object_group (GnmPane *pane);
void gnm_pane_display_object_menu (GnmPane *pane, SheetObject *so,
					   GdkEvent *event);

/* A convenience api */
SheetObjectView *gnm_pane_object_register (SheetObject *so, GocItem *view,
					   gboolean selectable);
void		 gnm_pane_widget_register (SheetObject *so, GtkWidget *w,
					   GocItem *view);

/************************************************************************/

typedef enum {
	GNM_PANE_SLIDE_X = 1,
	GNM_PANE_SLIDE_Y = 2,
	GNM_PANE_SLIDE_EXTERIOR_ONLY = 4,
	GNM_PANE_SLIDE_AT_COLROW_BOUND = 8 /* not implemented */
} GnmPaneSlideFlags;
typedef struct {
	int col, row;
	gpointer user_data;
} GnmPaneSlideInfo;
typedef gboolean (*GnmPaneSlideHandler) (GnmPane *pane, GnmPaneSlideInfo const *info);

void	 gnm_pane_slide_stop	  (GnmPane *pane);
void	 gnm_pane_slide_init	  (GnmPane *pane);
gboolean gnm_pane_handle_motion (GnmPane *pane,
				 GocCanvas    *canvas,
				 gint64 x, gint64 y,
				 GnmPaneSlideFlags   slide_flags,
				 GnmPaneSlideHandler handler,
				 gpointer user_data);

/************************************************************************/

void gnm_pane_set_direction     (GnmPane *pane, GocDirection direction);

G_END_DECLS

#endif /* _GNM_PANE_H_ */
