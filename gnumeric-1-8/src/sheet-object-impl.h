/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_SHEET_OBJECT_IMPL_H_
# define _GNM_SHEET_OBJECT_IMPL_H_

G_BEGIN_DECLS

#include "sheet-object.h"
#include <gsf/gsf-libxml.h>
#include <libxml/tree.h>
#include <glib-object.h>
#include <gtk/gtkmenu.h>

typedef enum {
	SHEET_OBJECT_IS_VISIBLE	= 1 << 0,	/* user selectable */
	SHEET_OBJECT_PRINT	= 1 << 1,
	SHEET_OBJECT_CAN_PRESS	= 1 << 2,

	/* Gnumeric specific properties */
	SHEET_OBJECT_MOVE_WITH_CELLS	= 1 << 16,
	SHEET_OBJECT_SIZE_WITH_CELLS	= 1 << 17,
	SHEET_OBJECT_OBSCURED		= 1 << 18	/* cells associated with region are hidden */
} SheetObjectFlags;

struct _SheetObject {
	GObject            parent_object;
	Sheet             *sheet;
	GList             *realized_list;
	SheetObjectAnchor  anchor;
	SheetObjectFlags   flags;
};

typedef void (*SheetObjectActionFunc) (SheetObject *so, SheetControl *sc);
typedef struct {
	char const *icon;	/* optionally NULL */
	char const *label;	/* NULL for separators */
	char const *msg_domain;	/* for plugins to specify translations */
	int  submenu;		/* > 1 starts a menu, < 1 end one */
	SheetObjectActionFunc	func;
} SheetObjectAction;

typedef struct {
	GObjectClass parent_class;

	/* Signals */
	void (*bounds_changed) (SheetObject *so);
	void (*unrealized)     (SheetObject *so);

	/* Virtual methods */
	gboolean (*remove_from_sheet) (SheetObject	*sheet_object);
	gboolean   (*assign_to_sheet) (SheetObject	*sheet_object,
				       Sheet		*sheet);

	SheetObjectView	*(*new_view) (SheetObject	*sheet_object,
				      SheetObjectViewContainer *container);
	void        (*populate_menu) (SheetObject	*sheet_object,
				      GPtrArray		*actions);
	void	      (*user_config) (SheetObject	*sheet_object,
				      SheetControl	*s_control);
	gboolean     (*read_xml_dom) (SheetObject	*so,
				      char const *type_name,	/* for versioning */
				      XmlParseContext const *ctxt,
				      xmlNodePtr	tree);
	void      (*prep_sax_parser) (SheetObject *so,
				      GsfXMLIn *xin, xmlChar const **attrs);
	void        (*write_xml_sax) (SheetObject const *so, GsfXMLOut *output);
	void		    (*copy)  (SheetObject *dst,
				      SheetObject const *src);

	void	     (*default_size) (SheetObject const *so,
				      double *width_pts, double *height_pts);

	void	       (*draw_cairo) (SheetObject const *so, cairo_t *cr,
				      double width, double height);

	void          (*foreach_dep) (SheetObject *so,
				      SheetObjectForeachDepFunc func,
				      gpointer user);

	void (*func_pad1)	(void);
	void (*func_pad2)	(void);

	/*
	 * If false, we draw a rectangle where the object is going to
	 * be layed out If true, we draw the object while laying
	 * it out on the sheet.
	 */
	gboolean rubber_band_directly;

	/*
	 * If FALSE, a mouse click will select the object.
	 */
	gboolean interactive;

	char const *xml_export_name;

	void *data_pad1;
	void *data_pad2;
	void *data_pad3;
	void *data_pad4;
} SheetObjectClass;
#define SHEET_OBJECT_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), SHEET_OBJECT_TYPE, SheetObjectClass))

/***************************************************************************/

typedef struct {
	GTypeInterface base;

	void (*destroy)    (SheetObjectView *sov);
	void (*active)	   (SheetObjectView *sov, gboolean is_active);
	void (*set_bounds) (SheetObjectView *sov,
			    double const *coords, gboolean visible);
} SheetObjectViewIface;

#define SHEET_OBJECT_VIEW_TYPE		(sheet_object_view_get_type ())
#define SHEET_OBJECT_VIEW(o)		(G_TYPE_CHECK_INSTANCE_CAST((o), SHEET_OBJECT_VIEW_TYPE, SheetObjectView))
#define IS_SHEET_OBJECT_VIEW(o)		(G_TYPE_CHECK_INSTANCE_TYPE((o), SHEET_OBJECT_VIEW_TYPE))
#define SHEET_OBJECT_VIEW_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), SHEET_OBJECT_VIEW_TYPE, SheetObjectViewIface))
#define IS_SHEET_OBJECT_VIEW_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE((k), SHEET_OBJECT_VIEW_TYPE))
#define SHEET_OBJECT_VIEW_GET_CLASS(o)  (G_TYPE_INSTANCE_GET_INTERFACE ((o), SHEET_OBJECT_VIEW_TYPE, SheetObjectViewIface))

GType	     sheet_object_view_get_type	  (void);
SheetObject *sheet_object_view_get_so	  (SheetObjectView *sov);
void	     sheet_object_view_destroy	  (SheetObjectView *sov);
void	     sheet_object_view_set_bounds (SheetObjectView *sov,
					   double const *coords,
					   gboolean visible);

/***************************************************************************/

typedef struct {
	GTypeInterface base;

	/* Map between anchors and un-normalized coordinates */
	void (*anchor_to_coords) (SheetObjectViewContainer *sovc,
				  SheetObjectAnchor const *input, double *output);
	void (*coords_to_anchor) (SheetObjectViewContainer *sovc,
				  double const *input, SheetObjectAnchor *output);
} SheetObjectViewContainerIface;

#if 0
void so_vc_anchor_to_coords (SheetObjectViewContainer *sovc,
			     SheetObjectAnchor const *input, double *output);
void so_vc_coords_to_anchor (SheetObjectViewContainer *sovc,
			     double const *input, SheetObjectAnchor *output);
#endif

/***************************************************************************/

struct _SheetObjectImageableIface {
	GTypeInterface base;

	GtkTargetList *(*get_target_list) (SheetObject const *so);
	void           (*write_image)	  (SheetObject const *so,
					   char const *format,
					   double resolution,
					   GsfOutput *output,
					   GError **err);
};

/***************************************************************************/

struct _SheetObjectExportableIface {
	GTypeInterface base;

	GtkTargetList *(*get_target_list) (SheetObject const *so);
	void           (*write_object)	  (SheetObject const *so,
					   char const *format,
					   GsfOutput *output, GError **err);
};

G_END_DECLS

#endif /* _GNM_SHEET_OBJECT_IMPL_H_ */
