#ifndef GNUMERIC_SHEET_OBJECT_PRIV_H
#define GNUMERIC_SHEET_OBJECT_PRIV_H

#include "sheet-object.h"

typedef enum {
	SHEET_OBJECT_ACTION_STATIC,
	SHEET_OBJECT_ACTION_CAN_PRESS
} SheetObjectAction;

struct _SheetObject {
	GtkObject          parent_object;
	SheetObjectAction  type;
	Sheet             *sheet;
	GList             *realized_list;

	/* Position */
	Range	cell_bound; /* cellpos containg corners */
	float	offset [4];
	SheetObjectAnchor anchor_type [4];

	/* DEPRECATED : Private data Use sheet-control specific methods */
	GnomeCanvasPoints *bbox_points;
};

typedef struct {
	GtkObjectClass parent_class;

	/* Virtual methods */
	GtkObject *	 (*new_view) (SheetObject	*sheet_object,
				      SheetControlGUI	*s_control);
	void        (*populate_menu) (SheetObject	*sheet_object,
				      GnomeCanvasItem	*obj_view,
				      GtkMenu		*menu);
	void	      (*user_config) (SheetObject	*sheet_object,
				      SheetControlGUI	*s_control);
	void        (*update_bounds) (SheetObject	*so,
				      GtkObject		*obj_view,
				      SheetControlGUI	*s_control);
	void           (*set_active) (SheetObject	*so,
				      gboolean		val);
	gboolean	(*read_xml)  (CommandContext	*cc,
				      SheetObject	*so,
				      xmlNodePtr	tree);
	xmlNodePtr	(*write_xml) (SheetObject const *so,
				      xmlDocPtr doc, xmlNsPtr ns,
				      XmlSheetObjectWriteFn write_fn);
	void                (*print) (SheetObject const *so,
				      SheetObjectPrintInfo const *pi);
} SheetObjectClass;

#define SHEET_OBJECT_CLASS(k) (GTK_CHECK_CLASS_CAST ((k), SHEET_OBJECT_TYPE, SheetObjectClass))

#endif /* GNUMERIC_SHEET_OBJECT_PRIV_H */
