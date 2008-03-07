/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_SHEET_OBJECT_H_
# define _GNM_SHEET_OBJECT_H_

#include "gnumeric.h"
#include <gtk/gtkselection.h>
#include <gsf/gsf-output.h>
#include <goffice/utils/go-undo.h>

G_BEGIN_DECLS

/***********************************************************
 * Move to goffice during 1.7 */

typedef enum {
	GOD_ANCHOR_DIR_UNKNOWN    = 0xFF,
	GOD_ANCHOR_DIR_UP_LEFT    = 0x00,
	GOD_ANCHOR_DIR_UP_RIGHT   = 0x01,
	GOD_ANCHOR_DIR_DOWN_LEFT  = 0x10,
	GOD_ANCHOR_DIR_DOWN_RIGHT = 0x11,

	GOD_ANCHOR_DIR_NONE_MASK  = 0x00,
	GOD_ANCHOR_DIR_H_MASK 	  = 0x01,
	GOD_ANCHOR_DIR_RIGHT	  = 0x01,
	GOD_ANCHOR_DIR_V_MASK	  = 0x10,
	GOD_ANCHOR_DIR_DOWN  	  = 0x10
} GODrawingAnchorDir;
typedef struct _GODrawingAnchor {
	int			pos_pts [4];	/* position in points */
	GODrawingAnchorDir	direction;
} GODrawingAnchor;
/***********************************************************/

struct _SheetObjectAnchor {
	GODrawingAnchor	base;

	GnmRange cell_bound; /* cellpos containing corners */
	float	 offset [4]; /* offsets from the top left (in LTR of cell_bound) */
};

#define SHEET_OBJECT_TYPE     (sheet_object_get_type ())
#define SHEET_OBJECT(obj)     (G_TYPE_CHECK_INSTANCE_CAST((obj), SHEET_OBJECT_TYPE, SheetObject))
#define IS_SHEET_OBJECT(o)    (G_TYPE_CHECK_INSTANCE_TYPE((o), SHEET_OBJECT_TYPE))
GType sheet_object_get_type (void);

#define SHEET_OBJECT_IMAGEABLE_TYPE  (sheet_object_imageable_get_type ())
#define SHEET_OBJECT_IMAGEABLE(o)     (G_TYPE_CHECK_INSTANCE_CAST ((o), SHEET_OBJECT_IMAGEABLE_TYPE, SheetObjectImageableIface))
#define IS_SHEET_OBJECT_IMAGEABLE(o)  (G_TYPE_CHECK_INSTANCE_TYPE ((o), SHEET_OBJECT_IMAGEABLE_TYPE))

GType sheet_object_imageable_get_type (void);

#define SHEET_OBJECT_EXPORTABLE_TYPE  (sheet_object_exportable_get_type ())
#define SHEET_OBJECT_EXPORTABLE(o)     (G_TYPE_CHECK_INSTANCE_CAST ((o), SHEET_OBJECT_EXPORTABLE_TYPE, SheetObjectExportableIface))
#define IS_SHEET_OBJECT_EXPORTABLE(o)  (G_TYPE_CHECK_INSTANCE_TYPE ((o), SHEET_OBJECT_EXPORTABLE_TYPE))

GType sheet_object_exportable_get_type (void);

gboolean      sheet_object_set_sheet	 (SheetObject *so, Sheet *sheet);
Sheet	     *sheet_object_get_sheet	 (SheetObject const *so);
void	      sheet_object_clear_sheet	 (SheetObject *so);

SheetObject  *sheet_object_dup		 (SheetObject const *so);
gboolean      sheet_object_can_print	 (SheetObject const *so);

void	     sheet_object_get_editor	 (SheetObject *so, SheetControl *sc);
void	     sheet_object_populate_menu  (SheetObject *so, GPtrArray *actions);

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
GSList *sheet_objects_get        (Sheet const *sheet, GnmRange const *r,
				  GType t);
void    sheet_objects_dup	 (Sheet const *src, Sheet *dst, GnmRange *range);

void     sheet_object_direction_set (SheetObject *so, gdouble const *coords);
gboolean sheet_object_rubber_band_directly (SheetObject const *so);

/* Anchor utilities */
void sheet_object_anchor_to_pts	(SheetObjectAnchor const *anchor,
				 Sheet const *sheet, double *res_pts);
void sheet_object_anchor_init	(SheetObjectAnchor *anchor,
				 GnmRange const *cell_bound,
				 float const	*offset,
				 GODrawingAnchorDir direction);
SheetObjectAnchor *
     sheet_object_anchor_dup	(SheetObjectAnchor const *src);
void sheet_object_anchor_assign	(SheetObjectAnchor *dst,
				 SheetObjectAnchor const *src);

/* Image rendering */
GtkTargetList *sheet_object_get_target_list (SheetObject const *so);
void sheet_object_write_image 	(SheetObject const *so,
				 char const *format,
				 double resolution,
				 GsfOutput *output,
				 GError **err);

/* Object export */
GtkTargetList *sheet_object_exportable_get_target_list (SheetObject const *so);
void sheet_object_write_object 	(SheetObject const *so,
				 char const *format,
				 GsfOutput *output, GError **err);

/* cairo rendering */
void sheet_object_draw_cairo (SheetObject const *so, cairo_t *cr, gboolean rtl);

/* management routine to register all the builtin object types */
void sheet_objects_init (void);
G_END_DECLS

#endif /* _GNM_SHEET_OBJECT_H_ */
