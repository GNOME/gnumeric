/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef GNUMERIC_SHEET_OBJECT_PRIV_H
#define GNUMERIC_SHEET_OBJECT_PRIV_H

#include "sheet-object.h"
#include <gsf/gsf-libxml.h>
#include <libxml/tree.h>
#include <glib-object.h>
#include <gtk/gtkmenu.h>
#include <libgnomeprint/gnome-print.h>

typedef enum {
	SHEET_OBJECT_ACTION_STATIC,
	SHEET_OBJECT_ACTION_CAN_PRESS
} SheetObjectAction;

struct _SheetObject {
	GObject            parent_object;
	SheetObjectAction  type;
	Sheet             *sheet;
	GList             *realized_list;
	SheetObjectAnchor  anchor;
	unsigned	   is_visible;
	unsigned	   move_with_cells;
};

typedef struct {
	GObjectClass parent_class;

	/* Signals */
	void (*bounds_changed) (SheetObject *so);

	/* Virtual methods */
	gboolean (*remove_from_sheet) (SheetObject	*sheet_object);
	gboolean   (*assign_to_sheet) (SheetObject	*sheet_object,
				       Sheet		*sheet);

	GObject *      (*new_view)   (SheetObject	*sheet_object,
				      SheetControl	*s_control, gpointer key);
	void        (*populate_menu) (SheetObject	*sheet_object,
				      GObject		*obj_view,
				      GtkMenu		*menu);
	void	      (*user_config) (SheetObject	*sheet_object,
				      SheetControl	*s_control);
	void           (*set_active) (SheetObject	*so,
				      GObject           *obj_view,
				      gboolean		val);
	gboolean     (*read_xml_dom) (SheetObject	*so,
				      char const *type_name,	/* for versioning */
				      XmlParseContext const *ctxt,
				      xmlNodePtr	tree);
	gboolean    (*write_xml_dom) (SheetObject const *so,
				      XmlParseContext const *ctxt,
				      xmlNodePtr	tree);
	gboolean     (*read_xml_sax) (SheetObject	*so,
				      char const *type_name,	/* for versioning */
				      XmlParseContext const *ctxt,
				      xmlNodePtr	tree);
	void        (*write_xml_sax) (SheetObject const *so, GsfXMLOut *output);

	/* Called with 0,0 set to the top, left corner of the object, and the
	 * graphics context saved */
	void                (*print) (SheetObject const *so,
				      GnomePrintContext *ctx,
				      double width, double height);
	SheetObject *       (*clone) (SheetObject const *so,
				      Sheet *sheet);

	void (*default_size)	 (SheetObject const *so,
				  double *width_pts, double *height_pts);

	void (*func_pad1)	(void);
	void (*func_pad2)	(void);
	void (*func_pad3)	(void);
	void (*func_pad4)	(void);

	gboolean rubber_band_directly; /* If false, we draw a rectangle where
					* the object is going to be layed out
					* If true, we draw the object while
					* laying it out on the sheet
					*/
	char const *xml_export_name;

	void *data_pad1;
	void *data_pad2;
	void *data_pad3;
	void *data_pad4;
} SheetObjectClass;

#define SHEET_OBJECT_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), SHEET_OBJECT_TYPE, SheetObjectClass))

#endif /* GNUMERIC_SHEET_OBJECT_PRIV_H */
