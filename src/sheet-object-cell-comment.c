/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * sheet-object-cell-comment.c: A SheetObject to support cell comments.
 *
 * Copyright (C) 2000 Jody Goldberg (jody@gnome.org)
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

#include "sheet-object-impl.h"
#include "sheet.h"
#include "sheet-view.h"
#include "sheet-merge.h"
#include "sheet-control-gui-priv.h"
#include "dialogs.h"


#include <libxml/globals.h>
#include <gsf/gsf-impl-utils.h>
#include <gal/widgets/e-cursors.h>
#include <libfoocanvas/foo-canvas-polygon.h>

struct _CellComment {
	SheetObject	s_object;

	char *author, *text;
};
typedef struct {
	SheetObjectClass s_object_class;
} CellCommentClass;

static void
cell_comment_finalize (GObject *object)
{
	CellComment *cc = CELL_COMMENT (object);
	GObjectClass *parent;

	g_return_if_fail (cc != NULL);

	if (cc->author != NULL) {
		g_free (cc->author);
		cc->author = NULL;
	}
	if (cc->text != NULL) {
		g_free (cc->text);
		cc->text = NULL;
	}

	/* If this comment is being displayed we shut down nicely */
	SHEET_FOREACH_CONTROL (cc->s_object.sheet, view, control,
		scg_comment_unselect ((SheetControlGUI *) control, cc););

	parent = g_type_class_peek (SHEET_OBJECT_TYPE);
	if (parent != NULL && parent->finalize != NULL)
		parent->finalize (object);
}

#define TRIANGLE_WIDTH 6
static FooCanvasPoints *
comment_get_points (SheetControl *sc, SheetObject *so)
{
	FooCanvasPoints *points;
	int x, y, i, far_col;
	Range const *r;
	SheetControlGUI *scg = SHEET_CONTROL_GUI (sc);

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
		foo_canvas_c2w (FOO_CANVAS (scg_pane (scg, 0)),
				  points->coords [i*2],
				  points->coords [i*2+1],
				  &(points->coords [i*2]),
				  &(points->coords [i*2+1]));

	return points;
}

static void
cell_comment_update_bounds (SheetObject *so, GObject *view)
{
	FooCanvasItem *item = FOO_CANVAS_ITEM (view);
	FooCanvasPoints *points = comment_get_points (
		sheet_object_view_control (view), so);
	foo_canvas_item_set (item, "points", points, NULL);
	foo_canvas_points_free (points);

	if (so->is_visible)
		foo_canvas_item_show (item);
	else
		foo_canvas_item_hide (item);
}

static int
cell_comment_event (FooCanvasItem *view, GdkEvent *event, SheetControlGUI *scg)
{
	CellComment *cc;
	SheetObject *so;
	Range const *r;

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
		e_cursor_set_widget (view->canvas, E_CURSOR_ARROW);
		scg_comment_select (scg, cc);
		break;

	case GDK_LEAVE_NOTIFY:
		scg_comment_unselect (scg, cc);
		break;

	case GDK_2BUTTON_PRESS:
		r = sheet_object_range_get (so);
		dialog_cell_comment(scg->wbcg, so->sheet, &r->start);
 		break;
 
	default:
		return FALSE;
	}
	return TRUE;
}

static GObject *
cell_comment_new_view (SheetObject *so, SheetControl *sc, gpointer key)
{
	GnmCanvas *gcanvas = ((GnumericPane *)key)->gcanvas;
	FooCanvasPoints *points;
	FooCanvasGroup *group;
	FooCanvasItem *item = NULL;
	SheetControlGUI *scg = SHEET_CONTROL_GUI (sc);
	CellComment *cc = CELL_COMMENT (so);

	g_return_val_if_fail (cc != NULL, NULL);
	g_return_val_if_fail (scg != NULL, NULL);
	g_return_val_if_fail (key != NULL, NULL);

	group = FOO_CANVAS_GROUP (FOO_CANVAS (gcanvas)->root);
	points = comment_get_points (sc, so);
	item = foo_canvas_item_new (
		group,		FOO_TYPE_CANVAS_POLYGON,
		"points",	points,
		"fill_color",	"red",
		NULL);
	foo_canvas_points_free (points);

	/* Do not use the standard handler, comments are not movable */
	g_signal_connect (G_OBJECT (item),
		"event",
		G_CALLBACK (cell_comment_event), scg);

	return G_OBJECT (item);
}

static gboolean
cell_comment_read_xml (SheetObject *so,
		       XmlParseContext const *ctxt, xmlNodePtr	tree)
{
	CellComment *cc = CELL_COMMENT (so);
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
cell_comment_write_xml (SheetObject const *so,
			XmlParseContext const *ctxt, xmlNodePtr tree)
{
	CellComment const *cc = CELL_COMMENT (so);
	xml_node_set_cstr (tree, "Author", cc->author);
	xml_node_set_cstr (tree, "Text", cc->text);
	return FALSE;
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

static SheetObject *
cell_comment_clone (SheetObject const *so, Sheet *sheet)
{
	CellComment *comment = CELL_COMMENT (so);
	CellComment *new_comment;

	new_comment = g_object_new (CELL_COMMENT_TYPE, NULL);

	new_comment->author = comment->author ? g_strdup (comment->author) : NULL;
	new_comment->text   = comment->text   ? g_strdup (comment->text) : NULL;

	return SHEET_OBJECT (new_comment);
}

static void
cell_comment_class_init (GObjectClass *object_class)
{
	SheetObjectClass *sheet_object_class = SHEET_OBJECT_CLASS (object_class);

	/* Object class method overrides */
	object_class->finalize = cell_comment_finalize;

	/* SheetObject class method overrides */
	sheet_object_class->update_bounds = &cell_comment_update_bounds;
	sheet_object_class->new_view	  = &cell_comment_new_view;
	sheet_object_class->read_xml      = &cell_comment_read_xml;
	sheet_object_class->write_xml     = &cell_comment_write_xml;
	sheet_object_class->print         = &cell_comment_print;
	sheet_object_class->clone         = &cell_comment_clone;
}

GSF_CLASS (CellComment, cell_comment,
	   cell_comment_class_init, NULL, SHEET_OBJECT_TYPE);

char const  *
cell_comment_author_get (CellComment const *cc)
{
	g_return_val_if_fail (IS_CELL_COMMENT (cc), NULL);
	return cc->author;
}

void
cell_comment_author_set (CellComment *cc, char const *author)
{
	char *tmp;
	g_return_if_fail (IS_CELL_COMMENT (cc));

	tmp = author ? g_strdup (author) : NULL;
	if (cc->author)
		g_free (cc->author);
	cc->author = tmp;
}

char const  *
cell_comment_text_get (CellComment const *cc)
{
	g_return_val_if_fail (IS_CELL_COMMENT (cc), NULL);
	return cc->text;
}

void
cell_comment_text_set (CellComment *cc, char const *text)
{
	char *tmp;
	g_return_if_fail (IS_CELL_COMMENT (cc));

	tmp = text ? g_strdup (text) : NULL;
	if (cc->text)
		g_free (cc->text);
	cc->text = tmp;
}

/* convenience routine */
CellComment *
cell_set_comment (Sheet *sheet, CellPos const *pos,
		  char const *author, char const *text)
{
	/* top right */
	static SheetObjectAnchorType const anchor_types [4] = {
		SO_ANCHOR_PERCENTAGE_FROM_COLROW_END,
		SO_ANCHOR_PERCENTAGE_FROM_COLROW_START,
		SO_ANCHOR_PERCENTAGE_FROM_COLROW_END,
		SO_ANCHOR_PERCENTAGE_FROM_COLROW_START
	};
	SheetObjectAnchor anchor;
	CellComment *cc;
	Range	     r;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (pos != NULL, NULL);

	cc = g_object_new (CELL_COMMENT_TYPE, NULL);
	cc->author = author ? g_strdup (author) : NULL;
	cc->text = text ? g_strdup (text) : NULL;

	r.start = r.end = *pos;
	sheet_object_anchor_init (&anchor, &r, NULL, anchor_types,
				  SO_DIR_DOWN_RIGHT);
	sheet_object_anchor_set (SHEET_OBJECT (cc), &anchor);
	sheet_object_set_sheet (SHEET_OBJECT (cc), sheet);

	/* setting the sheet added a reference */
	g_object_unref (G_OBJECT (cc));
	return cc;
}
