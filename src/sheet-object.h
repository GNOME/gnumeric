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
	SO_ANCHOR_PTS_FROM_LEFT_ANCHOR		= 0x30,
	SO_ANCHOR_PTS_FROM_TOP_ANCHOR		= 0x31,
} SheetObjectAnchor;

#define SHEET_OBJECT_TYPE     (sheet_object_get_type ())
#define SHEET_OBJECT(obj)     (GTK_CHECK_CAST((obj), SHEET_OBJECT_TYPE, SheetObject))
#define IS_SHEET_OBJECT(o)    (GTK_CHECK_TYPE((o), SHEET_OBJECT_TYPE))
GtkType      sheet_object_get_type   (void);

gboolean     sheet_object_clear_sheet (SheetObject *so);
gboolean     sheet_object_set_sheet   (SheetObject *so, Sheet *sheet);
SheetObject *sheet_object_read_xml    (XmlParseContext const *ctxt,
				       xmlNodePtr tree);
xmlNodePtr   sheet_object_write_xml   (SheetObject const *so,
				       XmlParseContext const *ctxt);
void         sheet_object_print       (SheetObject const *so,
				       GnomePrintContext *ctx,
				       double base_x, double base_y);
void         sheet_object_clone_sheet (Sheet const *src, Sheet *dst);

void	     sheet_object_realize	  (SheetObject *object);
void         sheet_object_position	  (SheetObject *so, CellPos const *pos);
void	     sheet_object_position_pixels (SheetObject const *so,
					   SheetControlGUI const *scg,
					   int *pos);
void	     sheet_object_default_size	  (SheetObject *so, double *w, double *h);
void	     sheet_object_position_pts    (SheetObject const *so, double *pos);
Range const *sheet_object_range_get	  (SheetObject const *so);
void         sheet_object_range_set	  (SheetObject *so, Range const *r,
					   float const *offsets,
					   SheetObjectAnchor const *type);

void		 sheet_object_new_view	   (SheetObject *so, SheetControlGUI *);
GtkObject	*sheet_object_get_view	   (SheetObject *so, SheetControlGUI *);
SheetObject     *sheet_object_view_obj     (GtkObject *view);
SheetControlGUI *sheet_object_view_control (GtkObject *view);

/* Object Management */
void   sheet_relocate_objects	(ExprRelocateInfo const *rinfo);
GList *sheet_get_objects	(Sheet const *sheet, Range const *r, GtkType t);
void   sheet_object_register	(void);

void     sheet_object_direction_set (SheetObject *so, gdouble *coords);
gboolean sheet_object_rubber_band_directly (SheetObject *so);
#endif /* GNUMERIC_SHEET_OBJECT_H */
