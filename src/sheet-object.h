#ifndef _GNM_SHEET_OBJECT_H_
# define _GNM_SHEET_OBJECT_H_

#include <gnumeric.h>
#include <gsf/gsf-output.h>
#include <goffice/goffice.h>

G_BEGIN_DECLS

/***********************************************************/

typedef enum {
	GNM_SO_RESIZE_MANUAL,   /* can be manually resized */
	GNM_SO_RESIZE_AUTO, /* automatically resized like cell bounded widgets */
	GNM_SO_RESIZE_NONE /* can't be resized like some sheet components */
} GnmSOResizeMode;

typedef enum {
	GNM_SO_ANCHOR_TWO_CELLS,	/* move and size (if sizeable) with cells) */
	GNM_SO_ANCHOR_ONE_CELL,		/* move with cells */
	GNM_SO_ANCHOR_ABSOLUTE		/* anchored to the sheet */
} GnmSOAnchorMode;
GType gnm_sheet_object_anchor_mode_get_type (void);
#define GNM_SHEET_OBJECT_ANCHOR_MODE_TYPE (gnm_sheet_object_anchor_mode_get_type ())

struct _SheetObjectAnchor {
	GODrawingAnchor	base;

	GnmRange cell_bound; /* cellpos containing corners */
	double	 offset[4];  /* offsets from the top left (in LTR of cell_bound) */
	GnmSOAnchorMode mode;
};
GType sheet_object_anchor_get_type (void); /* Boxed type */

#define GNM_SO_TYPE     (sheet_object_get_type ())
#define GNM_SO(obj)     (G_TYPE_CHECK_INSTANCE_CAST((obj), GNM_SO_TYPE, SheetObject))
#define GNM_IS_SO(o)    (G_TYPE_CHECK_INSTANCE_TYPE((o), GNM_SO_TYPE))
GType sheet_object_get_type (void);

#define GNM_SO_IMAGEABLE_TYPE  (sheet_object_imageable_get_type ())
#define GNM_SO_IMAGEABLE(o)     (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_SO_IMAGEABLE_TYPE, SheetObjectImageableIface))
#define GNM_IS_SO_IMAGEABLE(o)  (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_SO_IMAGEABLE_TYPE))

GType sheet_object_imageable_get_type (void);

#define GNM_SO_EXPORTABLE_TYPE  (sheet_object_exportable_get_type ())
#define GNM_SO_EXPORTABLE(o)     (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_SO_EXPORTABLE_TYPE, SheetObjectExportableIface))
#define GNM_IS_SO_EXPORTABLE(o)  (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_SO_EXPORTABLE_TYPE))

GType sheet_object_exportable_get_type (void);

void          sheet_object_set_sheet	 (SheetObject *so, Sheet *sheet);
Sheet	     *sheet_object_get_sheet	 (SheetObject const *so);
void	      sheet_object_clear_sheet	 (SheetObject *so);

void          sheet_object_set_name      (SheetObject *so, const char *name);
void          sheet_object_set_print_flag (SheetObject *so, gboolean *print);
gboolean      sheet_object_get_print_flag (SheetObject *so);

SheetObject  *sheet_object_dup		 (SheetObject const *so);
gboolean      sheet_object_can_print	 (SheetObject const *so);
gboolean      sheet_object_can_resize	 (SheetObject const *so);
gboolean      sheet_object_can_edit	 (SheetObject const *so);

void	     sheet_object_get_editor	 (SheetObject *so, SheetControl *sc);
void	     sheet_object_populate_menu  (SheetObject *so, GPtrArray *actions);
GtkWidget *  sheet_object_build_menu     (SheetObjectView *view,
					  GPtrArray const *actions,
					  unsigned *i);
void	     sheet_object_update_bounds  (SheetObject *so, GnmCellPos const *p);
void	     sheet_object_default_size	 (SheetObject *so, double *w, double *h);
gint	     sheet_object_adjust_stacking(SheetObject *so, gint positions);
gint         sheet_object_get_stacking   (SheetObject *so);
SheetObjectView *sheet_object_new_view	 (SheetObject *so,
					  SheetObjectViewContainer *container);
SheetObjectView	*sheet_object_get_view	 (SheetObject const *so,
					  SheetObjectViewContainer *container);
GnmRange const	*sheet_object_get_range	 (SheetObject const *so);
void		 sheet_object_set_anchor (SheetObject *so,
					  SheetObjectAnchor const *anchor);

SheetObjectAnchor const *sheet_object_get_anchor (SheetObject const *so);
void sheet_object_position_pts_get (SheetObject const *so, double *coords);

void sheet_object_invalidate_sheet       (SheetObject *so,
					  Sheet const *sheet);

typedef void (*SheetObjectForeachDepFunc) (GnmDependent *dep,
					   SheetObject *so,
					   gpointer user);
void sheet_object_foreach_dep            (SheetObject *so,
					  SheetObjectForeachDepFunc func,
					  gpointer user);


/* Object Management */
void	sheet_objects_relocate   (GnmExprRelocateInfo const *rinfo,
				  gboolean update, GOUndo **pundo);
void    sheet_objects_clear      (Sheet const *sheet, GnmRange const *r,
				  GType t, GOUndo **pundo);
GOUndo *sheet_object_move_undo   (GSList *objects, gboolean objects_created);
GOUndo *sheet_object_move_do     (GSList *objects, GSList *anchors,
				  gboolean objects_created);
GSList *sheet_objects_get        (Sheet const *sheet, GnmRange const *r,
				  GType t);
void    sheet_objects_dup	 (Sheet const *src, Sheet *dst, GnmRange const *range);

void     sheet_object_direction_set (SheetObject *so, gdouble const *coords);
gboolean sheet_object_rubber_band_directly (SheetObject const *so);
void	 sheet_object_set_anchor_mode (SheetObject *so, GnmSOAnchorMode const *mode);

/* Anchor utilities */
void sheet_object_anchor_to_pts	(SheetObjectAnchor const *anchor,
				 Sheet const *sheet, double *res_pts);
void sheet_object_anchor_to_offset_pts	(SheetObjectAnchor const *anchor,
					 Sheet const *sheet, double *res_pts);
void sheet_object_anchor_init	(SheetObjectAnchor *anchor,
				 GnmRange const *r,
				 const double *offsets,
				 GODrawingAnchorDir direction,
				 GnmSOAnchorMode mode);
void sheet_object_pts_to_anchor (SheetObjectAnchor *anchor,
			     Sheet const *sheet, double const *res_pts);
SheetObjectAnchor *
     sheet_object_anchor_dup	(SheetObjectAnchor const *src);

/* Image rendering */
GtkTargetList *sheet_object_get_target_list (SheetObject const *so);
void sheet_object_write_image	(SheetObject const *so,
				 char const *format,
				 double resolution,
				 GsfOutput *output,
				 GError **err);
void sheet_object_save_as_image	(SheetObject const *so,
				 char const *format,
				 double resolution,
				 const char *url,
				 GError **err);

/* Object export */
GtkTargetList *sheet_object_exportable_get_target_list (SheetObject const *so);
void sheet_object_write_object	(SheetObject const *so,
				 char const *format,
				 GsfOutput *output, GError **err,
				 GnmConventions const *convs);

/* cairo rendering */
void sheet_object_draw_cairo (SheetObject const *so, cairo_t *cr, gboolean rtl);
void sheet_object_draw_cairo_sized (SheetObject const *so, cairo_t *cr, double width, double height);

/* management routine to register all the builtin object types */
void sheet_objects_init (void);
void sheet_objects_shutdown (void);
G_END_DECLS

#endif /* _GNM_SHEET_OBJECT_H_ */
