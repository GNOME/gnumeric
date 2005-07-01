/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef GNUMERIC_SHEET_OBJECT_H
#define GNUMERIC_SHEET_OBJECT_H

#include "gnumeric.h"
#include <gtk/gtkselection.h>
#include <libgnomeprint/gnome-print.h>
#include <gsf/gsf-output.h>

typedef enum {
	SO_ANCHOR_UNKNOWN			= 0x00,
	SO_ANCHOR_PERCENTAGE_FROM_COLROW_START	= 0x10,
	SO_ANCHOR_PERCENTAGE_FROM_COLROW_END	= 0x11,

	/* TODO : implement these */
	SO_ANCHOR_PTS_FROM_COLROW_START		= 0x20,
	SO_ANCHOR_PTS_FROM_COLROW_END		= 0x21,

	/* only allowed for Anchors 2-3 to support fixed size */
	SO_ANCHOR_PTS_ABSOLUTE			= 0x30
} SheetObjectAnchorType;

typedef enum {
	SO_DIR_UNKNOWN    = 0xFF,
	SO_DIR_UP_LEFT    = 0x00,
	SO_DIR_UP_RIGHT   = 0x01,
	SO_DIR_DOWN_LEFT  = 0x10,
	SO_DIR_DOWN_RIGHT = 0x11,

	SO_DIR_NONE_MASK  = 0x00,
	SO_DIR_H_MASK 	  = 0x01,
	SO_DIR_RIGHT	  = 0x01,
	SO_DIR_V_MASK	  = 0x10,
	SO_DIR_DOWN  	  = 0x10
} SheetObjectDirection;

struct _SheetObjectAnchor {
	GnmRange	cell_bound; /* cellpos containg corners */
	float	offset [4];
	SheetObjectAnchorType type [4];
	SheetObjectDirection direction;
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
gboolean      sheet_object_clear_sheet	 (SheetObject *so);

SheetObject  *sheet_object_dup		 (SheetObject const *so);
gboolean      sheet_object_can_print	 (SheetObject const *so);
void          sheet_object_print	 (SheetObject const *so,
					  GnomePrintContext *ctx,
					  double width, double height);
void	     sheet_object_get_editor	 (SheetObject *so, SheetControl *sc);
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

/* Object Management */
void	sheet_objects_relocate   (GnmExprRelocateInfo const *rinfo, gboolean update,
				  GnmRelocUndo *undo);
void    sheet_objects_clear      (Sheet const *sheet, GnmRange const *r, GType t);
GSList *sheet_objects_get        (Sheet const *sheet, GnmRange const *r, GType t);
void    sheet_object_clone_sheet (Sheet const *src, Sheet *dst, GnmRange *range);

void     sheet_object_direction_set (SheetObject *so, gdouble const *coords);
gboolean sheet_object_rubber_band_directly (SheetObject const *so);

/* Anchor utilities */
void sheet_object_anchor_to_pts	(SheetObjectAnchor const *anchor,
				 Sheet const *sheet, double *res_pts);
void sheet_object_anchor_init	(SheetObjectAnchor *anchor,
				 GnmRange const *cell_bound,
				 float const	offset [4],
				 SheetObjectAnchorType const type [4],
				 SheetObjectDirection direction);
void sheet_object_anchor_cpy	(SheetObjectAnchor *dst,
				 SheetObjectAnchor const *src);

/* Image rendering */
GtkTargetList *sheet_object_get_target_list (SheetObject const *so);
void sheet_object_write_image 	(SheetObject const *so, 
				 const char *format,
				 GsfOutput *output, GError **err);

/* Object export */
GtkTargetList *sheet_object_exportable_get_target_list (SheetObject const *so);
void sheet_object_write_object 	(SheetObject const *so, 
				 const char *format,
				 GsfOutput *output, GError **err);

/* management routine to register all the builtin object types */
void sheet_objects_init (void);

#endif /* GNUMERIC_SHEET_OBJECT_H */
