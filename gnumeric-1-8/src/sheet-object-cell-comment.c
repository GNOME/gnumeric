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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "sheet-object-cell-comment.h"

#include "gnm-pane-impl.h"
#include "sheet-object-impl.h"
#include "sheet.h"
#include "sheet-view.h"
#include "sheet-merge.h"
#include "sheet-control-gui-priv.h"
#include "dialogs.h"
#include "gui-util.h"
#include <goffice/utils/go-libxml-extras.h>

#include <string.h>
#include <libxml/globals.h>
#include <gsf/gsf-impl-utils.h>
#include <goffice/cut-n-paste/foocanvas/foo-canvas-polygon.h>

struct _GnmComment {
	SheetObject	base;

	char *author, *text;
	PangoAttrList  *markup;
};
typedef SheetObjectClass GnmCommentClass;
enum {
	CC_PROP_0,
	CC_PROP_TEXT,
	CC_PROP_MARKUP
};

static GObjectClass *parent_klass;

#define TRIANGLE_WIDTH 6

static void
comment_view_destroy (SheetObjectView *sov)
{
	gtk_object_destroy (GTK_OBJECT (sov));
}
static void
comment_view_set_bounds (SheetObjectView *sov, double const *coords, gboolean visible)
{
	FooCanvasItem *view = FOO_CANVAS_ITEM (sov);
	if (visible) {
		SheetObject *so = sheet_object_view_get_so (sov);
		SheetControlGUI const *scg = GNM_SIMPLE_CANVAS (view->canvas)->scg;
		double scale;
		int x, y, dx, far_col;
		FooCanvasPoints *points = foo_canvas_points_new (3);
		GnmRange const *r = gnm_sheet_merge_is_corner (so->sheet,
			&so->anchor.cell_bound.start);

		scale = 1. / view->canvas->pixels_per_unit;

		if (r != NULL)
			far_col = 1 + r->end.col;
		else
			far_col = 1 + so->anchor.cell_bound.start.col;

		/* TODO : This could be optimized using the offsets associated with the visible region */
		/* Add 1 to y because we measure from start, x is measured from end, so
		 * it does not need it */
		y = scg_colrow_distance_get (scg, FALSE, 0, so->anchor.cell_bound.start.row)+ 1;
		points->coords[1] = scale * y;
		points->coords[3] = scale * y;
		points->coords[5] = scale * y + TRIANGLE_WIDTH;

		dx = TRIANGLE_WIDTH;
		if (so->sheet->text_is_rtl) {
			dx = -dx;
			scale = -scale;
		}
		x = scg_colrow_distance_get (scg, TRUE, 0, far_col);
		points->coords[0] = scale * x - dx;
		points->coords[2] = scale * x;
		points->coords[4] = scale * x;

		foo_canvas_item_set (view, "points", points, NULL);
		foo_canvas_points_free (points);
		foo_canvas_item_show (view);
	} else
		foo_canvas_item_hide (view);
}

static void
comment_foo_view_init (SheetObjectViewIface *sov_iface)
{
	sov_iface->destroy	= comment_view_destroy;
	sov_iface->set_bounds	= comment_view_set_bounds;
}
typedef FooCanvasPolygon	CommentFooView;
typedef FooCanvasPolygonClass	CommentFooViewClass;
static GSF_CLASS_FULL (CommentFooView, comment_foo_view,
	NULL, NULL, NULL, NULL,
	NULL, FOO_TYPE_CANVAS_POLYGON, 0,
	GSF_INTERFACE (comment_foo_view_init, SHEET_OBJECT_VIEW_TYPE))

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
	case CC_PROP_TEXT:
		g_free (cc->text);
		cc->text = g_strdup (g_value_get_string (value));
		break;
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
	case CC_PROP_TEXT :
		g_value_set_string (value, cc->text);
		break;
	case CC_PROP_MARKUP :
		g_value_set_boxed (value, cc->markup);
		break;
	default :
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		break;
	}
}

static int
cell_comment_event (FooCanvasItem *view, GdkEvent *event, GnmPane *pane)
{
	GnmComment *cc;
	SheetObject *so;
	GnmRange const *r;
	SheetControlGUI *scg;

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

	scg = pane->simple.scg;
	so = sheet_object_view_get_so (SHEET_OBJECT_VIEW (view));
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
		r = sheet_object_get_range (so);
		dialog_cell_comment (scg->wbcg, so->sheet, &r->start);
		break;

	default:
		return FALSE;
	}
	return TRUE;
}

static SheetObjectView *
cell_comment_new_view (SheetObject *so, SheetObjectViewContainer *container)
{
	GnmPane	*pane = GNM_PANE (container);
	FooCanvasItem	*view = foo_canvas_item_new (pane->grid_items,
		comment_foo_view_get_type (),
		"fill-color",	"red",
		NULL);
	/* Do not use the standard handler, comments are not movable */
	g_signal_connect (view,
		"event",
		G_CALLBACK (cell_comment_event), container);
	return gnm_pane_object_register (so, view, FALSE);
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
cell_comment_prep_sax_parser (SheetObject *so, GsfXMLIn *xin, xmlChar const **attrs)
{
	GnmComment *cc = CELL_COMMENT (so);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (!strcmp (attrs[0], "Text"))
			cc->text = g_strdup (attrs[1]);
		else if (!strcmp (attrs[0], "Author"))
			cc->author = g_strdup (attrs[1]);
	}
}

static void
cell_comment_copy (SheetObject *dst, SheetObject const *src)
{
	GnmComment const *comment	= CELL_COMMENT (src);
	GnmComment	 *new_comment	= CELL_COMMENT (dst);
	new_comment->author = g_strdup (comment->author);
	new_comment->text   = g_strdup (comment->text);
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
        g_object_class_install_property (gobject_class, CC_PROP_TEXT,
                 g_param_spec_string ("text", NULL, NULL, NULL,
				      GSF_PARAM_STATIC | G_PARAM_READWRITE));
        g_object_class_install_property (gobject_class, CC_PROP_MARKUP,
                 g_param_spec_boxed ("markup", NULL, NULL,
				     PANGO_TYPE_ATTR_LIST,
				     GSF_PARAM_STATIC | G_PARAM_READWRITE));

	/* SheetObject class method overrides */
	sheet_object_class->new_view		= &cell_comment_new_view;
	sheet_object_class->read_xml_dom	= &cell_comment_read_xml_dom;
	sheet_object_class->write_xml_sax	= &cell_comment_write_xml_sax;
	sheet_object_class->prep_sax_parser	= &cell_comment_prep_sax_parser;
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

	tmp = g_strdup (author);
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

	tmp = g_strdup (text);
	g_free (cc->text);
	cc->text = tmp;
}

/* convenience routine */
void
cell_comment_set_pos (GnmComment *cc, GnmCellPos const *pos)
{
	/* top right */
	static float const a_offsets [4] = { 1., 0., 1., 0. };
	SheetObjectAnchor anchor;
	GnmRange	  r;

	g_return_if_fail (IS_CELL_COMMENT (cc));

	r.start = r.end = *pos;
	sheet_object_anchor_init (&anchor, &r, a_offsets,
		GOD_ANCHOR_DIR_DOWN_RIGHT);
	sheet_object_set_anchor (SHEET_OBJECT (cc), &anchor);
}

GnmComment *
cell_set_comment (Sheet *sheet, GnmCellPos const *pos,
		  char const *author, char const *text)
{
	GnmComment *cc;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (pos != NULL, NULL);

	cc = g_object_new (CELL_COMMENT_TYPE, NULL);
	cc->author = g_strdup (author);
	cc->text = g_strdup (text);

	cell_comment_set_pos (cc, pos);

	sheet_object_set_sheet (SHEET_OBJECT (cc), sheet);
	/* setting the sheet added a reference */
	g_object_unref (G_OBJECT (cc));

	return cc;
}
