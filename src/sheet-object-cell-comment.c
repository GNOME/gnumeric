/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * sheet-object-cell-comment.c: A SheetObject to support cell comments.
 *
 * Copyright (C) 2000-2004 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "sheet-object-cell-comment.h"

#include "gnumeric-simple-canvas.h"
#include "sheet-object-impl.h"
#include "sheet.h"
#include "sheet-view.h"
#include "sheet-merge.h"
#include "sheet-control-gui-priv.h"
#include "dialogs.h"
#include "gui-util.h"
#include "xml-io.h"

#include <libxml/globals.h>
#include <gsf/gsf-impl-utils.h>
#include <libfoocanvas/foo-canvas-polygon.h>

struct _GnmComment {
	SheetObject	base;

	char *author, *text;
	PangoAttrList  *markup;
};
typedef SheetObjectClass GnmCommentClass;
enum {
	CC_PROP_0,
	CC_PROP_MARKUP
};

static GObjectClass *parent_klass;

static void
cell_comment_finalize (GObject *object)
{
	GnmComment *cc = CELL_COMMENT (object);

	g_return_if_fail (cc != NULL);

	/* If this comment is being displayed we shut down nicely */
	if (cc->base.sheet != NULL) {
		SHEET_FOREACH_CONTROL (cc->base.sheet, view, control,
			scg_comment_unselect ((SheetControlGUI *) control, cc););
	}

	g_free (cc->author);
	cc->author = NULL;
	g_free (cc->text);
	cc->text = NULL;

	if (NULL != cc->markup) {
		pango_attr_list_unref (cc->markup);
		cc->markup = NULL;
	}

	parent_klass->finalize (object);
}

static void
cell_comment_set_property (GObject *obj, guint param_id,
			   GValue const *value, GParamSpec *pspec)
{
	GnmComment *cc = CELL_COMMENT (obj);
	GList *ptr;

	switch (param_id) {
	case CC_PROP_MARKUP :
		if (cc->markup != NULL)
			pango_attr_list_unref (cc->markup);
		cc->markup = g_value_peek_pointer (value);
		if (cc->markup != NULL)
			pango_attr_list_ref (cc->markup);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		return;
	}

	for (ptr = SHEET_OBJECT (cc)->realized_list; ptr != NULL; ptr = ptr->next)
		foo_canvas_item_set (ptr->data, "attributes", cc->markup, NULL);
}

static void
cell_comment_get_property (GObject *obj, guint param_id,
			   GValue  *value,  GParamSpec *pspec)
{
	GnmComment *cc = CELL_COMMENT (obj);
	switch (param_id) {
	case CC_PROP_MARKUP :
		g_value_set_boxed (value, cc->markup);
		break;
	default :
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		break;
	}
}

#define TRIANGLE_WIDTH 6
static FooCanvasPoints *
comment_get_points (SheetControlGUI *scg, SheetObject *so)
{
	FooCanvasPoints *points;
	int x, y, i, far_col;
	GnmRange const *r;

	r = sheet_merge_is_corner (so->sheet, &so->anchor.cell_bound.start);
	if (r != NULL) {
		so->anchor.cell_bound.end.col = r->end.col;
		far_col = 1 + r->end.col;
	} else
		far_col = 1 + so->anchor.cell_bound.start.col;

	/* TODO : This could be optimized using the offsets associated with the visible region */
	/* Add 1 to y because we measure from start, x is measured from end, so
	 * it does not need it */
	y = scg_colrow_distance_get (scg, FALSE, 0, so->anchor.cell_bound.start.row)+ 1;
	x = scg_colrow_distance_get (scg, TRUE, 0, far_col);

	points = foo_canvas_points_new (3);
	points->coords [0] = x - TRIANGLE_WIDTH;
	points->coords [1] = y;
	points->coords [2] = x;
	points->coords [3] = y;
	points->coords [4] = x;
	points->coords [5] = y + TRIANGLE_WIDTH;

	/* Just use pane 0 for sizing, it always exists */
	for (i = 0; i < 3; i++)
		foo_canvas_w2c_d (FOO_CANVAS (scg_pane (scg, 0)),
				  points->coords [i*2],
				  points->coords [i*2+1],
				  &(points->coords [i*2]),
				  &(points->coords [i*2+1]));

	return points;
}

static int
cell_comment_event (FooCanvasItem *view, GdkEvent *event, SheetControlGUI *scg)
{
	GnmComment *cc;
	SheetObject *so;
	GnmRange const *r;

	switch (event->type) {
	default:
		return FALSE;

	case GDK_BUTTON_RELEASE:
		if (event->button.button != 1)
			return FALSE;
	case GDK_ENTER_NOTIFY:
	case GDK_LEAVE_NOTIFY:
	case GDK_2BUTTON_PRESS:
		break;
	}

	so = sheet_object_view_obj (G_OBJECT (view));
	cc = CELL_COMMENT (so);

	g_return_val_if_fail (cc != NULL, FALSE);

	switch (event->type) {
	case GDK_BUTTON_RELEASE:
		scg_comment_display (scg, cc);
		break;

	case GDK_ENTER_NOTIFY:
		gnm_widget_set_cursor_type (GTK_WIDGET (view->canvas), GDK_ARROW);
		scg_comment_select (scg, cc);
		break;

	case GDK_LEAVE_NOTIFY:
		scg_comment_unselect (scg, cc);
		break;

	case GDK_2BUTTON_PRESS:
		r = sheet_object_range_get (so);
		dialog_cell_comment (scg->wbcg, so->sheet, &r->start);
 		break;

	default:
		return FALSE;
	}
	return TRUE;
}

static void
cb_comment_bounds_changed (SheetObject *so, FooCanvasItem *view)
{
	FooCanvasPoints *points = comment_get_points (
		GNM_SIMPLE_CANVAS (view->canvas)->scg, so);
	foo_canvas_item_set (view, "points", points, NULL);
	foo_canvas_points_free (points);

	if (so->is_visible)
		foo_canvas_item_show (view);
	else
		foo_canvas_item_hide (view);
}

static GObject *
cell_comment_new_view (SheetObject *so, SheetControl *sc, gpointer key)
{
	GnmCanvas *gcanvas = ((GnmPane *)key)->gcanvas;
	FooCanvasPoints	*points;
	FooCanvasGroup	*group;
	FooCanvasItem	*view = NULL;
	SheetControlGUI *scg = SHEET_CONTROL_GUI (sc);
	GnmComment *cc = CELL_COMMENT (so);

	g_return_val_if_fail (cc != NULL, NULL);
	g_return_val_if_fail (scg != NULL, NULL);
	g_return_val_if_fail (key != NULL, NULL);

	group = FOO_CANVAS_GROUP (FOO_CANVAS (gcanvas)->root);
	points = comment_get_points (scg, so);
	view = foo_canvas_item_new (
		group,		FOO_TYPE_CANVAS_POLYGON,
		"points",	points,
		"fill_color",	"red",
		NULL);
	foo_canvas_points_free (points);

	/* Do not use the standard handler, comments are not movable */
	g_signal_connect (view,
		"event",
		G_CALLBACK (cell_comment_event), scg);
	cb_comment_bounds_changed (so, view);
	g_signal_connect_object (so,
		"bounds-changed",
		G_CALLBACK (cb_comment_bounds_changed), view, 0);

	return G_OBJECT (view);
}

static gboolean
cell_comment_read_xml_dom (SheetObject *so, char const *typename,
			   XmlParseContext const *ctxt, xmlNodePtr	tree)
{
	GnmComment *cc = CELL_COMMENT (so);
	xmlChar *author = xmlGetProp (tree, (xmlChar *)"Author");
	xmlChar *text = xmlGetProp (tree, (xmlChar *)"Text");

	if (author != NULL) {
		cell_comment_author_set (cc, (char *)author);
		xmlFree (author);
	}
	if (text != NULL) {
		cell_comment_text_set (cc, (char *)text);
		xmlFree (text);
	}

	return FALSE;
}

static gboolean
cell_comment_write_xml_dom (SheetObject const *so, XmlParseContext const *ctxt,
			    xmlNodePtr tree)
{
	GnmComment const *cc = CELL_COMMENT (so);
	xml_node_set_cstr (tree, "Author", cc->author);
	xml_node_set_cstr (tree, "Text", cc->text);
	return FALSE;
}
static void
cell_comment_write_xml_sax (SheetObject const *so, GsfXMLOut *output)
{
	GnmComment const *cc = CELL_COMMENT (so);
	if (NULL != cc->author)
		gsf_xml_out_add_cstr (output, "Author", cc->author);
	if (NULL != cc->text)
		gsf_xml_out_add_cstr (output, "Text", cc->text);
}

static void
cell_comment_print (SheetObject const *so, GnomePrintContext *ctx,
		    double base_x, double base_y)
{
	/*
	 * Nothing in here. This function is here to suppress a warning
	 * about an unprintable sheet object.
	 */
}

static void
cell_comment_copy (SheetObject *dst, SheetObject const *src)
{
	GnmComment const *comment	= CELL_COMMENT (src);
	GnmComment	 *new_comment	= CELL_COMMENT (dst);
	new_comment->author = comment->author ? g_strdup (comment->author) : NULL;
	new_comment->text   = comment->text   ? g_strdup (comment->text) : NULL;
}

static void
cell_comment_class_init (GObjectClass *gobject_class)
{
	SheetObjectClass *sheet_object_class = SHEET_OBJECT_CLASS (gobject_class);

	parent_klass = g_type_class_peek_parent (gobject_class);

	/* Object class method overrides */
	gobject_class->finalize		= cell_comment_finalize;
	gobject_class->set_property	= cell_comment_set_property;
	gobject_class->get_property	= cell_comment_get_property;
        g_object_class_install_property (gobject_class, CC_PROP_MARKUP,
                 g_param_spec_boxed ("markup", NULL, NULL,
				     PANGO_TYPE_ATTR_LIST,
				     (G_PARAM_READABLE | G_PARAM_WRITABLE)));

	/* SheetObject class method overrides */
	sheet_object_class->new_view		= &cell_comment_new_view;
	sheet_object_class->read_xml_dom	= &cell_comment_read_xml_dom;
	sheet_object_class->write_xml_dom	= &cell_comment_write_xml_dom;
	sheet_object_class->write_xml_sax	= &cell_comment_write_xml_sax;
	sheet_object_class->print		= &cell_comment_print;
	sheet_object_class->copy		= &cell_comment_copy;
	sheet_object_class->xml_export_name = "CellComment";
}

static void
cell_comment_init (GnmComment *cc)
{
	cc->markup = NULL;
}

GSF_CLASS (GnmComment, cell_comment,
	   cell_comment_class_init, cell_comment_init, SHEET_OBJECT_TYPE);

char const  *
cell_comment_author_get (GnmComment const *cc)
{
	g_return_val_if_fail (IS_CELL_COMMENT (cc), NULL);
	return cc->author;
}

void
cell_comment_author_set (GnmComment *cc, char const *author)
{
	char *tmp;
	g_return_if_fail (IS_CELL_COMMENT (cc));

	tmp = author ? g_strdup (author) : NULL;
	if (cc->author)
		g_free (cc->author);
	cc->author = tmp;
}

char const  *
cell_comment_text_get (GnmComment const *cc)
{
	g_return_val_if_fail (IS_CELL_COMMENT (cc), NULL);
	return cc->text;
}

void
cell_comment_text_set (GnmComment *cc, char const *text)
{
	char *tmp;
	g_return_if_fail (IS_CELL_COMMENT (cc));

	tmp = text ? g_strdup (text) : NULL;
	if (cc->text)
		g_free (cc->text);
	cc->text = tmp;
}

/* convenience routine */
void
cell_comment_set_cell (GnmComment *cc, GnmCellPos const *pos)
{
	/* top right */
	static SheetObjectAnchorType const anchor_types [4] = {
		SO_ANCHOR_PERCENTAGE_FROM_COLROW_END,
		SO_ANCHOR_PERCENTAGE_FROM_COLROW_START,
		SO_ANCHOR_PERCENTAGE_FROM_COLROW_END,
		SO_ANCHOR_PERCENTAGE_FROM_COLROW_START
	};
	SheetObjectAnchor anchor;
	GnmRange	  r;

	g_return_if_fail (IS_CELL_COMMENT (cc));

	r.start = r.end = *pos;
	sheet_object_anchor_init (&anchor, &r, NULL,
		anchor_types, SO_DIR_DOWN_RIGHT);
	sheet_object_anchor_set (SHEET_OBJECT (cc), &anchor);
}

GnmComment *
cell_set_comment (Sheet *sheet, GnmCellPos const *pos,
		  char const *author, char const *text)
{
	GnmComment *cc;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (pos != NULL, NULL);

	cc = g_object_new (CELL_COMMENT_TYPE, NULL);
	cc->author = author ? g_strdup (author) : NULL;
	cc->text = text ? g_strdup (text) : NULL;

	cell_comment_set_cell (cc, pos);

	sheet_object_set_sheet (SHEET_OBJECT (cc), sheet);
	/* setting the sheet added a reference */
	g_object_unref (G_OBJECT (cc));

	return cc;
}
