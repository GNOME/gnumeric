/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef GNUMERIC_SHEET_OBJECT_PRIV_H
#define GNUMERIC_SHEET_OBJECT_PRIV_H

#include "sheet-object.h"
#include <gtk/gtkobject.h>

typedef enum {
	SHEET_OBJECT_ACTION_STATIC,
	SHEET_OBJECT_ACTION_CAN_PRESS
} SheetObjectAction;

typedef enum {
	SO_DIR_UNKNOWN    = 0xFF,
	SO_DIR_UP_RIGHT   = 0x00,
	SO_DIR_UP_LEFT    = 0x01,
	SO_DIR_DOWN_RIGHT = 0x10,
	SO_DIR_DOWN_LEFT  = 0x11,

	SO_DIR_NONE_MASK  = 0x00,
	SO_DIR_LEFT_MASK  = 0x01,
	SO_DIR_DOWN_MASK  = 0x10,
} SheetObjectDirection;

struct _SheetObject {
	GtkObject          parent_object;
	SheetObjectAction  type;
	Sheet             *sheet;
	GList             *realized_list;

	/* Position */
	Range	cell_bound; /* cellpos containg corners */
	float	offset [4];
	SheetObjectAnchor anchor_type [4];
	SheetObjectDirection direction;
};

typedef struct {
	GtkObjectClass parent_class;

	/* Virtual methods */
	gboolean (*remove_from_sheet) (SheetObject	*sheet_object);
	gboolean   (*assign_to_sheet) (SheetObject	*sheet_object,
				       Sheet		*sheet);

	GtkObject *      (*new_view) (SheetObject	*sheet_object,
				      SheetControlGUI	*s_control);
	void        (*populate_menu) (SheetObject	*sheet_object,
				      GtkObject		*obj_view,
				      GtkMenu		*menu);
	void	      (*user_config) (SheetObject	*sheet_object,
				      SheetControlGUI	*s_control);
	void        (*update_bounds) (SheetObject	*so,
				      GtkObject		*obj_view,
				      SheetControlGUI	*s_control);
	void           (*set_active) (SheetObject	*so,
				      GtkObject         *obj_view,
				      gboolean		val);
	gboolean	 (*read_xml) (SheetObject	*so,
				      XmlParseContext const *ctxt,
				      xmlNodePtr	tree);
	gboolean	(*write_xml) (SheetObject const *so,
				      XmlParseContext const *ctxt,
				      xmlNodePtr	tree);
	void                (*print) (SheetObject const *so,
				      SheetObjectPrintInfo const *pi);
	SheetObject *       (*clone) (SheetObject const *so,
				      Sheet *sheet);
						
	double	 default_width_pts, default_height_pts;
	gboolean rubber_band_directly; /* If false, we draw a rectangle where
					* the object is going to be layed out
					* If true, we draw the object while
					* laying it out on the sheet
					*/
} SheetObjectClass;

#define SHEET_OBJECT_CLASS(k) (GTK_CHECK_CLASS_CAST ((k), SHEET_OBJECT_TYPE, SheetObjectClass))

#endif /* GNUMERIC_SHEET_OBJECT_PRIV_H */
