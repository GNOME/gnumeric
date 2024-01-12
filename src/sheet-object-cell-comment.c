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
#include <gnumeric.h>
#include <sheet-object-cell-comment.h>

#include <gnm-pane-impl.h>
#include <sheet-object-impl.h>
#include <sheet.h>
#include <sheet-view.h>
#include <sheet-merge.h>
#include <sheet-control-gui-priv.h>
#include <dialogs/dialogs.h>
#include <gui-util.h>
#include <goffice/goffice.h>

#include <string.h>
#include <libxml/globals.h>
#include <gsf/gsf-impl-utils.h>

struct _GnmComment {
	SheetObject	base;

	char *author, *text;
	PangoAttrList  *markup;
};
typedef SheetObjectClass GnmCommentClass;
static GObjectClass *cell_comment_parent_class;
enum {
	CC_PROP_0,
	CC_PROP_TEXT,
	CC_PROP_AUTHOR,
	CC_PROP_MARKUP
};

typedef struct {
	SheetObjectView base;

	GdkRGBA comment_indicator_color;
	int comment_indicator_size;
} CommentView;
typedef SheetObjectViewClass CommentViewClass;
static GocItemClass *comment_view_parent_class;

static void
comment_view_reload_style (CommentView *cv)
{
	GocItem *item = GOC_ITEM (cv);
	GnmPane *pane = GNM_PANE (item->canvas);
	GValue *v;
	const char *name;

	name = "comment-indicator.color";
	v = g_hash_table_lookup (pane->object_style, name);
	if (v == NULL) {
		GtkStyleContext *context;
		GdkRGBA color;

		context = goc_item_get_style_context (item);
		gnm_style_context_get_color (context, GTK_STATE_FLAG_NORMAL,
					     &color);
		gnm_css_debug_color ("comment-indicator.color", &color);

		v = g_new0 (GValue, 1);
		g_value_init (v, GDK_TYPE_RGBA);
		g_value_set_boxed (v, &color);
		g_hash_table_insert (pane->object_style, g_strdup (name), v);
	}
	cv->comment_indicator_color = *(GdkRGBA const *)g_value_get_boxed (v);


	name = "comment-indicator.size";
	v = g_hash_table_lookup (pane->object_style, name);
	if (v == NULL) {
		int size;

		gtk_widget_style_get (GTK_WIDGET (pane),
				      "comment-indicator-size",
				      &size,
				      NULL);
		gnm_css_debug_int ("comment-indicator.size", size);

		v = g_new0 (GValue, 1);
		g_value_init (v, G_TYPE_INT);
		g_value_set_int (v, size);
		g_hash_table_insert (pane->object_style, g_strdup (name), v);
	}
	cv->comment_indicator_size = g_value_get_int (v);
}

static void
comment_view_set_bounds (SheetObjectView *sov, double const *coords, gboolean visible)
{
	CommentView *cv = (CommentView *)sov;
	GocPoints *points = goc_points_new (3);
	GocItem *item = sheet_object_view_get_item (sov);
	if (visible) {
		SheetObject *so = sheet_object_view_get_so (sov);
		SheetControlGUI const *scg = GNM_SIMPLE_CANVAS (item->canvas)->scg;
		double scale;
		gint64 x, y, dx;
		int far_col;
		GnmRange const *r = gnm_sheet_merge_is_corner (so->sheet,
			&so->anchor.cell_bound.start);

		scale = 1. / item->canvas->pixels_per_unit;

		if (r != NULL)
			far_col = 1 + r->end.col;
		else
			far_col = 1 + so->anchor.cell_bound.start.col;

		/* TODO : This could be optimized using the offsets associated with the visible region */
		/* Add 1 to y because we measure from start, x is measured from end, so
		 * it does not need it */
		y = scg_colrow_distance_get (scg, FALSE, 0, so->anchor.cell_bound.start.row)+ 1;
		points->points[0].y = scale * y;
		points->points[1].y = scale * y;
		points->points[2].y = scale * y + cv->comment_indicator_size;

		dx = cv->comment_indicator_size;
		x = scg_colrow_distance_get (scg, TRUE, 0, far_col);
		points->points[0].x = scale * x - dx;
		points->points[1].x = scale * x;
		points->points[2].x = scale * x;

		goc_item_set (item, "points", points, NULL);
		goc_points_unref (points);
		goc_item_show (GOC_ITEM (sov));
	} else
		goc_item_hide (GOC_ITEM (sov));
}

static gboolean
comment_view_button_released (GocItem *item, int button, double x, double y)
{
	SheetObject *so;
	int ix, iy;

	if (button != 1)
		return FALSE;

	gnm_canvas_get_screen_position (item->canvas, x, y, &ix, &iy);
	so = sheet_object_view_get_so (GNM_SO_VIEW (item));
	scg_comment_display (GNM_PANE (item->canvas)->simple.scg,
	                     GNM_CELL_COMMENT (so),
			     ix, iy);

	return TRUE;
}

static gboolean
comment_view_button_pressed (GocItem *item, int button, double x, double y)
{
	return TRUE;
}

static gboolean
comment_view_button2_pressed (GocItem *item, int button, double x, double y)
{
	SheetObject *so;
	GnmRange const *r;
	SheetControlGUI *scg;
	if (button !=1)
		return FALSE;

	scg = GNM_PANE (item->canvas)->simple.scg;
	so = sheet_object_view_get_so (GNM_SO_VIEW (item));
	r = sheet_object_get_range (so);
	dialog_cell_comment (scg->wbcg, so->sheet, &r->start);
	return TRUE;
}

static gboolean
comment_view_enter_notify (GocItem *item, double x, double y)
{
	int ix, iy;
	SheetObject *so;

	gnm_widget_set_cursor_type (GTK_WIDGET (item->canvas), GDK_ARROW);

	gnm_canvas_get_screen_position (item->canvas, x, y, &ix, &iy);
	so = sheet_object_view_get_so (GNM_SO_VIEW (item));

	scg_comment_select (GNM_PANE (item->canvas)->simple.scg,
			    GNM_CELL_COMMENT (so),
			    ix, iy);
	return TRUE;
}

static gboolean
comment_view_leave_notify (GocItem *item, double x, double y)
{
	scg_comment_unselect (GNM_PANE (item->canvas)->simple.scg,
	                      GNM_CELL_COMMENT (sheet_object_view_get_so (GNM_SO_VIEW (item))));
	return TRUE;
}

static void
comment_view_class_init (SheetObjectViewClass *sov_klass)
{
	GocItemClass *item_klass = (GocItemClass *) sov_klass;

	comment_view_parent_class = g_type_class_peek_parent (sov_klass);

	sov_klass->set_bounds	= comment_view_set_bounds;

	item_klass->button_pressed = comment_view_button_pressed;
	item_klass->button_released = comment_view_button_released;
	item_klass->button2_pressed = comment_view_button2_pressed;
	item_klass->enter_notify = comment_view_enter_notify;
	item_klass->leave_notify = comment_view_leave_notify;
}

static GSF_CLASS (CommentView, comment_view,
	comment_view_class_init, NULL,
	GNM_SO_VIEW_TYPE)

static void
cell_comment_finalize (GObject *object)
{
	GnmComment *cc = GNM_CELL_COMMENT (object);

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

	cell_comment_parent_class->finalize (object);
}

static void
cell_comment_set_property (GObject *obj, guint param_id,
			   GValue const *value, GParamSpec *pspec)
{
	GnmComment *cc = GNM_CELL_COMMENT (obj);

	switch (param_id) {
	case CC_PROP_TEXT:
		g_free (cc->text);
		cc->text = g_value_dup_string (value);
		break;
	case CC_PROP_AUTHOR:
		g_free (cc->author);
		cc->author = g_value_dup_string (value);
		break;
	case CC_PROP_MARKUP:
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
}

static void
cell_comment_get_property (GObject *obj, guint param_id,
			   GValue  *value,  GParamSpec *pspec)
{
	GnmComment *cc = GNM_CELL_COMMENT (obj);
	switch (param_id) {
	case CC_PROP_TEXT:
		g_value_set_string (value, cc->text);
		break;
	case CC_PROP_AUTHOR:
		g_value_set_string (value, cc->author);
		break;
	case CC_PROP_MARKUP:
		g_value_set_boxed (value, cc->markup);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		break;
	}
}

static SheetObjectView *
cell_comment_new_view (SheetObject *so, SheetObjectViewContainer *container)
{
	GnmPane	*pane = GNM_PANE (container);
	GocItem	*view = goc_item_new (pane->grid_items,
				      comment_view_get_type (),
				      NULL);
	CommentView *cv = (CommentView *)view;
	GOStyle *style = go_styled_object_get_style (
		GO_STYLED_OBJECT (goc_item_new (GOC_GROUP (view),
			GOC_TYPE_POLYGON, NULL)));

	comment_view_reload_style (cv);

	style->line.dash_type = GO_LINE_NONE;
	style->fill.pattern.back = go_color_from_gdk_rgba (&cv->comment_indicator_color, NULL);
	return gnm_pane_object_register (so, view, FALSE);
}

static void
cell_comment_write_xml_sax (SheetObject const *so, GsfXMLOut *output,
			    GnmConventions const *convs)
{
	GnmComment const *cc = GNM_CELL_COMMENT (so);
	if (NULL != cc->author)
		gsf_xml_out_add_cstr (output, "Author", cc->author);
	if (NULL != cc->text) {
		gsf_xml_out_add_cstr (output, "Text", cc->text);
		if (NULL != cc->markup) {
			GOFormat *fmt = go_format_new_markup	(cc->markup, TRUE);
			gsf_xml_out_add_cstr (output, "TextFormat",
					      go_format_as_XL (fmt));
			go_format_unref (fmt);
		}
	}
}

static void
cell_comment_prep_sax_parser (SheetObject *so, GsfXMLIn *xin,
			      xmlChar const **attrs,
			      GnmConventions const *convs)
{
	GnmComment *cc = GNM_CELL_COMMENT (so);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (!strcmp (attrs[0], "Text"))
			cc->text = g_strdup (attrs[1]);
		else if (!strcmp (attrs[0], "Author"))
			cc->author = g_strdup (attrs[1]);
		else if (!strcmp (attrs[0], "TextFormat")) {
			GOFormat * fmt = go_format_new_from_XL (attrs[1]);
			if (go_format_is_markup (fmt))
				g_object_set (G_OBJECT (cc),
					      "markup", go_format_get_markup (fmt),
					      NULL);
			go_format_unref (fmt);
		}
	}
}

static void
cell_comment_copy (SheetObject *dst, SheetObject const *src)
{
	GnmComment const *comment	= GNM_CELL_COMMENT (src);
	GnmComment	 *new_comment	= GNM_CELL_COMMENT (dst);
	new_comment->author = g_strdup (comment->author);
	new_comment->text   = g_strdup (comment->text);
	new_comment->markup = comment->markup;
	pango_attr_list_ref (new_comment->markup);
}

static void
cell_comment_class_init (GObjectClass *gobject_class)
{
	SheetObjectClass *sheet_object_class = GNM_SO_CLASS (gobject_class);

	cell_comment_parent_class = g_type_class_peek_parent (gobject_class);

	/* Object class method overrides */
	gobject_class->finalize		= cell_comment_finalize;
	gobject_class->set_property	= cell_comment_set_property;
	gobject_class->get_property	= cell_comment_get_property;
        g_object_class_install_property (gobject_class, CC_PROP_TEXT,
                 g_param_spec_string ("text", NULL, NULL, NULL,
				      GSF_PARAM_STATIC | G_PARAM_READWRITE));
        g_object_class_install_property (gobject_class, CC_PROP_AUTHOR,
                 g_param_spec_string ("author", NULL, NULL, NULL,
				      GSF_PARAM_STATIC | G_PARAM_READWRITE));
        g_object_class_install_property (gobject_class, CC_PROP_MARKUP,
                 g_param_spec_boxed ("markup", NULL, NULL,
				     PANGO_TYPE_ATTR_LIST,
				     GSF_PARAM_STATIC | G_PARAM_READWRITE));

	/* SheetObject class method overrides */
	sheet_object_class->new_view		= &cell_comment_new_view;
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
	   cell_comment_class_init, cell_comment_init, GNM_SO_TYPE)

char const  *
cell_comment_author_get (GnmComment const *cc)
{
	g_return_val_if_fail (GNM_IS_CELL_COMMENT (cc), NULL);
	return cc->author;
}

void
cell_comment_author_set (GnmComment *cc, char const *author)
{
	char *tmp;
	g_return_if_fail (GNM_IS_CELL_COMMENT (cc));

	tmp = g_strdup (author);
	g_free (cc->author);
	cc->author = tmp;
}

char const  *
cell_comment_text_get (GnmComment const *cc)
{
	g_return_val_if_fail (GNM_IS_CELL_COMMENT (cc), NULL);
	return cc->text;
}

void
cell_comment_text_set (GnmComment *cc, char const *text)
{
	char *tmp;
	g_return_if_fail (GNM_IS_CELL_COMMENT (cc));

	tmp = g_strdup (text);
	g_free (cc->text);
	cc->text = tmp;
}

/* convenience routine */
void
cell_comment_set_pos (GnmComment *cc, GnmCellPos const *pos)
{
	/* top right */
	static double const a_offsets [4] = { 1., 0., 1., 0. };
	SheetObjectAnchor anchor;
	GnmRange	  r;

	g_return_if_fail (GNM_IS_CELL_COMMENT (cc));

	r.start = r.end = *pos;
	sheet_object_anchor_init (&anchor, &r, a_offsets,
		GOD_ANCHOR_DIR_DOWN_RIGHT, GNM_SO_ANCHOR_TWO_CELLS);
	sheet_object_set_anchor (GNM_SO (cc), &anchor);
}

/**
 * cell_set_comment:
 * @sheet: #Sheet.
 * @pos: the position.
 * @author: comment author.
 * @text: comment text.
 * @markup: comment markup.
 *
 * Returns: (transfer none): the newly allocated #GnmComment.
 **/
GnmComment *
cell_set_comment (Sheet *sheet, GnmCellPos const *pos,
		  char const *author, char const *text,
		  PangoAttrList *attr)
{
	GnmComment *cc;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (pos != NULL, NULL);

	cc = g_object_new (GNM_CELL_COMMENT_TYPE, NULL);
	cc->author = g_strdup (author);
	cc->text = g_strdup (text);
	cc->markup = attr;
	if (cc->markup != NULL)
		pango_attr_list_ref (cc->markup);

	cell_comment_set_pos (cc, pos);

	sheet_object_set_sheet (GNM_SO (cc), sheet);
	/* setting the sheet added a reference */
	g_object_unref (cc);

	return cc;
}
