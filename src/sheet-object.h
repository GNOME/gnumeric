/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef GNUMERIC_SHEET_OBJECT_H
#define GNUMERIC_SHEET_OBJECT_H

#include "gnumeric.h"
#include "gui-gnumeric.h"	/* TODO : work to remove this when sheet MVC is done */
#include "xml-io.h"

#include <libgnomeprint/gnome-print.h>

typedef enum {
	SO_ANCHOR_UNKNOWN			= 0x00,
	SO_ANCHOR_PERCENTAGE_FROM_COLROW_START	= 0x10,
	SO_ANCHOR_PERCENTAGE_FROM_COLROW_END	= 0x11,

	/* TODO : implement these */
	SO_ANCHOR_PTS_FROM_COLROW_START		= 0x20,
	SO_ANCHOR_PTS_FROM_COLROW_END		= 0x21,

	/* only allowed for Anchors 2-3 to support fixed size */
	SO_ANCHOR_PTS_ABSOLUTE			= 0x30,
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
	SO_DIR_DOWN  	  = 0x10,
} SheetObjectDirection;

struct _SheetObjectAnchor {
	Range	cell_bound; /* cellpos containg corners */
	float	offset [4];
	SheetObjectAnchorType type [4];
	SheetObjectDirection direction;
};

#define SHEET_OBJECT_TYPE     (sheet_object_get_type ())
#define SHEET_OBJECT(obj)     (GTK_CHECK_CAST((obj), SHEET_OBJECT_TYPE, SheetObject))
#define IS_SHEET_OBJECT(o)    (GTK_CHECK_TYPE((o), SHEET_OBJECT_TYPE))
GtkType sheet_object_get_type (void);

void	     sheet_object_register	   (void);
gboolean     sheet_object_clear_sheet	   (SheetObject *so);
gboolean     sheet_object_set_sheet	   (SheetObject *so, Sheet *sheet);
Sheet	    *sheet_object_get_sheet	   (SheetObject const *so);

SheetObject *sheet_object_read_xml	   (XmlParseContext const *ctxt,
					    xmlNodePtr tree);
xmlNodePtr   sheet_object_write_xml	   (SheetObject const *so,
					    XmlParseContext const *ctxt);
void         sheet_object_print		   (SheetObject const *so,
					    GnomePrintContext *ctx,
					    double base_x, double base_y);
void         sheet_object_clone_sheet	   (Sheet const *src, Sheet *dst);
void	     sheet_object_realize	   (SheetObject *co);
void         sheet_object_update_bounds	   (SheetObject *so, CellPos const *p);
void	     sheet_object_default_size	   (SheetObject *so,
					    double *w, double *h);

void		 sheet_object_new_view	   (SheetObject *so, SheetControlGUI *);
GtkObject	*sheet_object_get_view	   (SheetObject *so, SheetControl *);
SheetObject     *sheet_object_view_obj     (GtkObject *view);
SheetControlGUI *sheet_object_view_control (GtkObject *view);

Range const *	sheet_object_range_get	   (SheetObject const *so);
void		sheet_object_anchor_set	   (SheetObject *so,
					    SheetObjectAnchor const *anchor);
SheetObjectAnchor const *sheet_object_anchor_get (SheetObject const *so);

void sheet_object_position_pts_get	   (SheetObject const *so, double *pos);
void sheet_object_position_pixels_get	   (SheetObject const *so,
					    SheetControl const *sc, double *pos);
void sheet_object_position_pixels_set	   (SheetObject const *so,
					    SheetControl const *sc, double const *pos);

/* Object Management */
void   sheet_relocate_objects	(ExprRelocateInfo const *rinfo);
GList *sheet_get_objects	(Sheet const *sheet, Range const *r, GtkType t);

void     sheet_object_direction_set (SheetObject *so, gdouble *coords);
gboolean sheet_object_rubber_band_directly (SheetObject const *so);

/* Anchor utilities */
void sheet_object_anchor_init	    (SheetObjectAnchor *anchor,
				     Range const *cell_bound,
				     float const	offset [4],
				     SheetObjectAnchorType const type [4],
				     SheetObjectDirection direction);
void sheet_object_anchor_cpy	    (SheetObjectAnchor *dst,
				     SheetObjectAnchor const *src);

#endif /* GNUMERIC_SHEET_OBJECT_H */
