/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * filter.c: support for filters
 *
 * Copyright (C) 2002-2004 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
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
#include "sheet-filter.h"

#include "workbook.h"
#include "sheet.h"
#include "sheet-private.h"
#include "cell.h"
#include "expr.h"
#include "value.h"
#include "sheet-control-gui.h"
#include "sheet-object-impl.h"
#include "gnumeric-pane.h"
#include "gnumeric-canvas.h"
#include "dependent.h"
#include "ranges.h"
#include "str.h"
#include "number-match.h"
#include "dialogs.h"
#include "regutf8.h"
#include "style-color.h"

#include <libfoocanvas/foo-canvas-widget.h>
#include <gtk/gtk.h>
#include <gdk/gdkevents.h>
#include <gdk/gdkkeysyms.h>
#include <gsf/gsf-impl-utils.h>
#include <glib/gi18n.h>

typedef struct {
	SheetObject parent;

	GnmFilterCondition *cond;
	GnmFilter   	   *filter;
} GnmFilterField;

typedef struct {
	SheetObjectClass s_object_class;
} GnmFilterFieldClass;

#define FILTER_FIELD_TYPE     (filter_field_get_type ())
#define FILTER_FIELD(obj)     (G_TYPE_CHECK_INSTANCE_CAST((obj), FILTER_FIELD_TYPE, GnmFilterField))

#define	VIEW_ITEM_ID	"view-item"
#define ARROW_ID	"arrow"
#define	FIELD_ID	"field"
#define	WBCG_ID		"wbcg"

static GType filter_field_get_type (void);


GnmFilterCondition *
gnm_filter_condition_new_single (GnmFilterOp op, GnmValue *v)
{
	GnmFilterCondition *res = g_new0 (GnmFilterCondition, 1);
	res->op[0] = op;	res->op[1] = GNM_FILTER_UNUSED;
	res->value[0] = v;
	return res;
}

GnmFilterCondition *
gnm_filter_condition_new_double (GnmFilterOp op0, GnmValue *v0,
				 gboolean join_with_and,
				 GnmFilterOp op1, GnmValue *v1)
{
	GnmFilterCondition *res = g_new0 (GnmFilterCondition, 1);
	res->op[0] = op0;	res->op[1] = op1;
	res->is_and = join_with_and;
	res->value[0] = v0;	res->value[1] = v1;
	return res;
}

GnmFilterCondition *
gnm_filter_condition_new_bucket (gboolean top, gboolean absolute, unsigned n)
{
	GnmFilterCondition *res = g_new0 (GnmFilterCondition, 1);
	res->op[0] = GNM_FILTER_OP_TOP_N | (top ? 0 : 1) | (absolute ? 0 : 2);
	res->count = n;
	return res;
}

void
gnm_filter_condition_unref (GnmFilterCondition *cond)
{
	g_return_if_fail (cond != NULL);
	if (cond->value[0] != NULL)
		value_release (cond->value[0]);
	if (cond->value[1] != NULL)
		value_release (cond->value[1]);
}

/**********************************************************************************/

static int
filter_field_index (GnmFilterField const *field)
{
	return field->parent.anchor.cell_bound.start.col -
		field->filter->r.start.col;
}

static void
filter_field_finalize (GObject *object)
{
	GnmFilterField *field = FILTER_FIELD (object);
	GObjectClass *parent;

	g_return_if_fail (field != NULL);

	if (field->cond != NULL) {
		gnm_filter_condition_unref (field->cond);
		field->cond = NULL;
	}

	parent = g_type_class_peek (SHEET_OBJECT_TYPE);
	parent->finalize (object);
}

/* Cut and paste from gtkwindow.c */
static void
do_focus_change (GtkWidget *widget, gboolean in)
{
	GdkEventFocus fevent;

	g_object_ref (widget);

	if (in)
		GTK_WIDGET_SET_FLAGS (widget, GTK_HAS_FOCUS);
	else
		GTK_WIDGET_UNSET_FLAGS (widget, GTK_HAS_FOCUS);

	fevent.type = GDK_FOCUS_CHANGE;
	fevent.window = widget->window;
	fevent.in = in;

	gtk_widget_event (widget, (GdkEvent *)&fevent);

	g_object_notify (G_OBJECT (widget), "has_focus");

	g_object_unref (widget);
}

static void
filter_popup_destroy (GtkWidget *popup, GtkWidget *list)
{
	do_focus_change (list, FALSE);
	gtk_widget_destroy (popup);
}

static gint
cb_filter_key_press (GtkWidget *popup, GdkEventKey *event, GtkWidget *list)
{
	if (event->keyval != GDK_Escape)
		return FALSE;
	filter_popup_destroy (popup, list);
	return TRUE;
}

static gint
cb_filter_button_press (GtkWidget *popup, GdkEventButton *event,
			GtkWidget *list)
{
	/* A press outside the popup cancels */
	if (event->window != popup->window) {
		filter_popup_destroy (popup, list);
		return TRUE;
	}
	return FALSE;
}

static gint
cb_filter_button_release (GtkWidget *popup, GdkEventButton *event,
			  GtkTreeView *list)
{
	GtkTreeIter  iter;
	GnmFilterField *field;
	GnmFilterCondition *cond = NULL;
	WorkbookControlGUI *wbcg;
	GtkWidget *event_widget = gtk_get_event_widget ((GdkEvent *) event);
	int field_num;

	/* A release inside list accepts */
	if (event_widget != GTK_WIDGET (list))
		return FALSE;

	field = g_object_get_data (G_OBJECT (list), FIELD_ID);
	wbcg  = g_object_get_data (G_OBJECT (list), WBCG_ID);
	if (field != NULL &&
	    gtk_tree_selection_get_selected (gtk_tree_view_get_selection (list),
					     NULL, &iter)) {
		char	*label;
		GnmValue   *val;
		int	 type;
		gboolean set_condition = TRUE;

		gtk_tree_model_get (gtk_tree_view_get_model (list), &iter,
				    0, &label, 1, &val, 2, &type,
				    -1);

		field_num = filter_field_index (field);
		switch (type) {
		case  0 : cond = gnm_filter_condition_new_single (
				  GNM_FILTER_OP_EQUAL, value_dup (val));
			break;
		case  1 : cond = NULL;	break; /* unfilter */
		case  2 : /* Custom */
			set_condition = FALSE;
			dialog_auto_filter (wbcg, field->filter, field_num,
					    TRUE, field->cond);
			break;
		case  3 : cond = gnm_filter_condition_new_single (
				  GNM_FILTER_OP_BLANKS, NULL);
			break;
		case  4 : cond = gnm_filter_condition_new_single (
				  GNM_FILTER_OP_NON_BLANKS, NULL);
			break;
		case 10 : /* Top 10 */
			set_condition = FALSE;
			dialog_auto_filter (wbcg, field->filter, field_num,
					    FALSE, field->cond);
			break;

		default :
			set_condition = FALSE;
			g_warning ("Unknown type %d", type);
		};

		g_free (label);

		if (set_condition) {
			gnm_filter_set_condition (field->filter, field_num,
						  cond, TRUE);
			sheet_update (field->filter->sheet);
		}
	}
	filter_popup_destroy (popup, GTK_WIDGET (list));
	return TRUE;
}

static gboolean
cb_filter_motion_notify_event (GtkWidget *widget, GdkEventMotion *event,
			       GtkTreeView *list)
{
	GtkTreePath *path;
	if (event->x >= 0 && event->y >= 0 &&
	    event->x < widget->allocation.width &&
	    event->y < widget->allocation.height &&
	    gtk_tree_view_get_path_at_pos (list, event->x, event->y,
					   &path, NULL, NULL, NULL)) {
		gtk_tree_selection_select_path (gtk_tree_view_get_selection (list), path);
		gtk_tree_path_free (path);
	}
	return TRUE;
}

typedef struct {
	gboolean has_blank;
	GHashTable *hash;
} UniqueCollection;

static GnmValue *
cb_collect_unique (Sheet *sheet, int col, int row, GnmCell *cell,
		   UniqueCollection *uc)
{
	if (cell_is_blank (cell))
		uc->has_blank = TRUE;
	else
		g_hash_table_replace (uc->hash, cell->value, cell->value);

	return NULL;
}
static void
cb_copy_hash_to_array (GnmValue *key, gpointer value, gpointer sorted)
{
	g_ptr_array_add (sorted, key);
}

static GtkListStore *
collect_unique_elements (GnmFilterField *field,
			 GtkTreePath **clip, GtkTreePath **select)
{
	UniqueCollection uc;
	GtkTreeIter	 iter;
	GtkListStore *model;
	GPtrArray    *sorted = g_ptr_array_new ();
	unsigned i;
	gboolean is_custom = FALSE;
	GnmRange	 r = field->filter->r;
	GnmValue const *check = NULL;
	GnmValue	    *check_num = NULL; /* XL stores numbers as string @$^!@$ */

	if (field->cond != NULL &&
	    field->cond->op[0] == GNM_FILTER_OP_EQUAL &&
	    field->cond->op[1] == GNM_FILTER_UNUSED) {
		check = field->cond->value[0];
		if (check->type == VALUE_STRING)
			check_num = format_match_number (check->v_str.val->str, NULL,
				workbook_date_conv (field->filter->sheet->workbook));
	}

	model = gtk_list_store_new (3, G_TYPE_STRING, G_TYPE_POINTER, G_TYPE_INT);
	gtk_list_store_append (model, &iter);
	gtk_list_store_set (model, &iter, 0, _("(All)"),	   1, NULL, 2, 1, -1);
	if (field->cond == NULL || field->cond->op[0] == GNM_FILTER_UNUSED)
		*select = gtk_tree_model_get_path (GTK_TREE_MODEL (model), &iter);

	gtk_list_store_append (model, &iter);
	gtk_list_store_set (model, &iter, 0, _("(Top 10...)"),     1, NULL, 2, 10,-1);
	if (field->cond != NULL &&
	    (GNM_FILTER_OP_TYPE_MASK & field->cond->op[0]) == GNM_FILTER_OP_TOP_N)
		*select = gtk_tree_model_get_path (GTK_TREE_MODEL (model), &iter);

	/* default to this we can easily revamp later */
	gtk_list_store_append (model, &iter);
	gtk_list_store_set (model, &iter, 0, _("(Custom...)"),     1, NULL, 2, 2, -1);
	if (*select == NULL) {
		is_custom = TRUE;
		*select = gtk_tree_model_get_path (GTK_TREE_MODEL (model), &iter);
	}

	r.start.row++;
	/* r.end.row =  XL actually extend to the first non-empty element in the list */
	r.end.col = r.start.col += filter_field_index (field);
	uc.has_blank = FALSE;
#warning "FIXME: is this really the type of equality (and hash) we want here?"
	uc.hash = g_hash_table_new (
			(GHashFunc) value_hash, (GEqualFunc) value_equal);
	sheet_foreach_cell_in_range (field->filter->sheet,
		CELL_ITER_ALL,
		r.start.col, r.start.row, r.end.col, r.end.row,
		(CellIterFunc)&cb_collect_unique, &uc);

	g_hash_table_foreach (uc.hash,
		(GHFunc) cb_copy_hash_to_array, sorted);
	qsort (&g_ptr_array_index (sorted, 0),
	       sorted->len, sizeof (GnmValue *), value_cmp);
	for (i = 0; i < sorted->len ; i++) {
		GnmValue const *v = g_ptr_array_index (sorted, i);
		gtk_list_store_append (model, &iter);
		gtk_list_store_set (model, &iter,
			0, value_peek_string (v),
			1, v,
			2, 0,
			-1);
		if (i == 10)
			*clip = gtk_tree_model_get_path (GTK_TREE_MODEL (model),
							 &iter);
		if (check != NULL) {
			if (value_compare (check, v, TRUE) == IS_EQUAL ||
			    (check_num != NULL && value_compare (check_num, v, TRUE) == IS_EQUAL)) {
				gtk_tree_path_free (*select);
				*select = gtk_tree_model_get_path (GTK_TREE_MODEL (model), &iter);
			}
		}
	}

	if (uc.has_blank) {
		gtk_list_store_append (model, &iter);
		gtk_list_store_set (model, &iter, 0, _("(Blanks...)"),	   1, NULL, 2, 3, -1);
		if (field->cond != NULL &&
		    field->cond->op[0] == GNM_FILTER_OP_BLANKS)
			*select = gtk_tree_model_get_path (GTK_TREE_MODEL (model), &iter);

		gtk_list_store_append (model, &iter);
		gtk_list_store_set (model, &iter, 0, _("(Non Blanks...)"), 1, NULL, 2, 4, -1);
		if (field->cond != NULL &&
		    field->cond->op[0] == GNM_FILTER_OP_NON_BLANKS)
			*select = gtk_tree_model_get_path (GTK_TREE_MODEL (model), &iter);
	} else if (is_custom && field->cond != NULL &&
		   (GNM_FILTER_OP_TYPE_MASK & field->cond->op[0]) == GNM_FILTER_OP_BLANKS) {
		gtk_tree_path_free (*select);
		*select = NULL;
	}

	g_hash_table_destroy (uc.hash);
	g_ptr_array_free (sorted, TRUE);

	if (check_num != NULL)
		value_release (check_num);

	return model;
}

static void
cb_focus_changed (GtkWindow *toplevel)
{
#if 0
	g_warning (gtk_window_has_toplevel_focus (toplevel) ? "focus" : "no focus");
#endif
}

static void
cb_filter_button_pressed (GtkButton *button, FooCanvasItem *view)
{
	GnmPane		*pane = GNM_CANVAS (view->canvas)->pane;
	SheetControlGUI *scg = pane->gcanvas->simple.scg;
	SheetObject	*so = sheet_object_view_get_so (SHEET_OBJECT_VIEW (view));
	GnmFilterField	*field = FILTER_FIELD (so);
	GtkWidget *frame, *popup, *list, *container;
	int root_x, root_y;
	GtkListStore  *model;
	GtkTreeViewColumn *column;
	GtkTreePath	  *clip = NULL, *select = NULL;
	GtkRequisition	req;

	popup = gtk_window_new (GTK_WINDOW_POPUP);
	model = collect_unique_elements (field, &clip, &select);
	column = gtk_tree_view_column_new_with_attributes ("ID",
			gtk_cell_renderer_text_new (), "text", 0,
			NULL);
	list = gtk_tree_view_new_with_model (GTK_TREE_MODEL (model));
	gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (list), FALSE);
	gtk_tree_view_append_column (GTK_TREE_VIEW (list), column);
	gtk_widget_size_request (GTK_WIDGET (list), &req);
	g_object_set_data (G_OBJECT (list), FIELD_ID, field);
	g_object_set_data (G_OBJECT (list), WBCG_ID, scg_get_wbcg (scg));
	g_signal_connect (G_OBJECT (wbcg_toplevel (scg_get_wbcg (scg))),
		"notify::has-toplevel-focus",
		G_CALLBACK (cb_focus_changed), list);

	frame = gtk_frame_new (NULL);
	gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_OUT);

	if (clip != NULL) {
		GdkRectangle  rect;
		GtkWidget *sw = gtk_scrolled_window_new (
			gtk_tree_view_get_hadjustment (GTK_TREE_VIEW (list)),
			gtk_tree_view_get_vadjustment (GTK_TREE_VIEW (list)));
		gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
						GTK_POLICY_AUTOMATIC,
						GTK_POLICY_ALWAYS);
		gtk_tree_view_get_background_area (GTK_TREE_VIEW (list),
						   clip, NULL, &rect);
		gtk_tree_path_free (clip);

		gtk_widget_set_size_request (list, req.width, rect.y);
		gtk_container_add (GTK_CONTAINER (sw), list);
		container = sw;
	} else
		container = list;

	gtk_container_add (GTK_CONTAINER (frame), container);

	/* do the popup */
	gtk_window_set_decorated (GTK_WINDOW (popup), FALSE);
	gdk_window_get_origin (GTK_WIDGET (pane->gcanvas)->window,
		&root_x, &root_y);
	gtk_window_move (GTK_WINDOW (popup),
		root_x + scg_colrow_distance_get (scg, TRUE,
			pane->gcanvas->first.col,
			field->parent.anchor.cell_bound.start.col + 1) - req.width,
		root_y + scg_colrow_distance_get (scg, FALSE,
			pane->gcanvas->first.row,
			field->filter->r.start.row + 1));

	gtk_container_add (GTK_CONTAINER (popup), frame);

	g_signal_connect (popup,
		"key_press_event",
		G_CALLBACK (cb_filter_key_press), list);
	g_signal_connect (popup,
		"button_press_event",
		G_CALLBACK (cb_filter_button_press), list);
	g_signal_connect (popup,
		"button_release_event",
		G_CALLBACK (cb_filter_button_release), list);
	g_signal_connect (list,
		"motion_notify_event",
		G_CALLBACK (cb_filter_motion_notify_event), list);

	gtk_widget_show_all (popup);

	/* after we show the window setup the selection (showing the list
	 * clears the selection) */
	if (select != NULL) {
		gtk_tree_selection_select_path (
			gtk_tree_view_get_selection (GTK_TREE_VIEW (list)),
			select);
		gtk_tree_path_free (select);
	}

	gtk_widget_grab_focus (GTK_WIDGET (list));
	do_focus_change (GTK_WIDGET (list), TRUE);

	gtk_grab_add (popup);
	gdk_pointer_grab (popup->window, TRUE,
		GDK_BUTTON_PRESS_MASK |
		GDK_BUTTON_RELEASE_MASK |
		GDK_POINTER_MOTION_MASK,
		NULL, NULL, GDK_CURRENT_TIME);
}

static void
filter_field_arrow_format (GnmFilterField *field, GtkWidget *arrow)
{
	gtk_arrow_set (GTK_ARROW (arrow),
		field->cond != NULL ? GTK_ARROW_RIGHT : GTK_ARROW_DOWN,
		GTK_SHADOW_IN);
	gtk_widget_modify_fg (arrow, GTK_STATE_NORMAL,
		field->cond != NULL ? &gs_yellow : &gs_black);
}

static void
filter_view_destroy (SheetObjectView *sov)
{
	gtk_object_destroy (GTK_OBJECT (sov));
}
static void
filter_view_set_bounds (SheetObjectView *sov, double const *coords, gboolean visible)
{
	FooCanvasItem *view = FOO_CANVAS_ITEM (sov);

	if (visible) {
		double h, x;
		/* clip vertically */
		h = (coords[3] - coords[1]);
		if (h > 20.)
			h = 20.;

		/* maintain squareness horzontally */
		x = coords[2] - h;
		if (x < coords[0])
			x = coords[0];

		/* NOTE : far point is EXCLUDED so we add 1 */
		foo_canvas_item_set (view,
			"x",	x,
			"y",	coords [3] - h,
			"width",  coords [2] - x,
			"height", h + 1.,
			NULL);

		foo_canvas_item_show (view);
	} else
		foo_canvas_item_hide (view);
}

static void
filter_foo_view_init (SheetObjectViewIface *sov_iface)
{
	sov_iface->destroy	= filter_view_destroy;
	sov_iface->set_bounds	= filter_view_set_bounds;
}
typedef FooCanvasWidget		FilterFooView;
typedef FooCanvasWidgetClass	FilterFooViewClass;
static GSF_CLASS_FULL (FilterFooView, filter_foo_view,
	NULL, NULL,
	FOO_TYPE_CANVAS_WIDGET, 0,
	GSF_INTERFACE (filter_foo_view_init, SHEET_OBJECT_VIEW_TYPE))

static SheetObjectView *
filter_field_new_view (SheetObject *so, SheetObjectViewContainer *container)
{
	GnmCanvas *gcanvas = ((GnmPane *)container)->gcanvas;
	GtkWidget *arrow, *view_widget = gtk_button_new ();
	GnmFilterField *field = (GnmFilterField *) so;
	FooCanvasItem *view_item = foo_canvas_item_new (gcanvas->object_views,
		filter_foo_view_get_type (),
		"widget",	view_widget,
		"size_pixels",	FALSE,
		NULL);

	GTK_WIDGET_UNSET_FLAGS (view_widget, GTK_CAN_FOCUS);
	arrow = gtk_arrow_new (field->cond != NULL ? GTK_ARROW_RIGHT : GTK_ARROW_DOWN,
			       GTK_SHADOW_IN);
	filter_field_arrow_format (field, arrow);
	gtk_container_add (GTK_CONTAINER (view_widget), arrow);

	g_object_set_data (G_OBJECT (view_widget), VIEW_ITEM_ID, view_item);
	g_object_set_data (G_OBJECT (view_item), ARROW_ID, arrow);
	g_signal_connect (view_widget,
		"pressed",
		G_CALLBACK (cb_filter_button_pressed), view_item);
	gtk_widget_show_all (view_widget);

	return gnm_pane_object_register (so, view_item, FALSE);
}

static void
filter_field_init (SheetObject *so)
{
	/* keep the arrows from wandering with their cells */
	so->flags &= ~SHEET_OBJECT_MOVE_WITH_CELLS;
}

static void
filter_field_class_init (GObjectClass *object_class)
{
	SheetObjectClass *sheet_object_class = SHEET_OBJECT_CLASS (object_class);

	/* Object class method overrides */
	object_class->finalize = filter_field_finalize;

	/* SheetObject class method overrides */
	sheet_object_class->new_view	  = filter_field_new_view;
	sheet_object_class->read_xml_dom  = NULL;
	sheet_object_class->write_xml_dom = NULL;
	sheet_object_class->write_xml_sax = NULL;
	sheet_object_class->print         = NULL;
	sheet_object_class->copy          = NULL;
}

GSF_CLASS (GnmFilterField, filter_field,
	   filter_field_class_init, filter_field_init,
	   SHEET_OBJECT_TYPE);

/*****************************************************************************/

typedef struct  {
	GnmFilterCondition const *cond;
	GnmValue		 *val[2];
	go_regex_t  regexp[2];
	GnmDateConventions const *date_conv;
} FilterExpr;

static void
filter_expr_init (FilterExpr *fexpr, unsigned i,
		  GnmFilterCondition const *cond,
		  GnmFilter const *filter)
{
	GnmValue *tmp = cond->value[i];
	fexpr->date_conv = workbook_date_conv (filter->sheet->workbook);

	if (VALUE_IS_STRING (tmp)) {
		GnmFilterOp op = cond->op[i];
		char const *str = value_peek_string (tmp);
		fexpr->val[i] = format_match_number (str, NULL, fexpr->date_conv);
		if (fexpr->val[i] != NULL)
			return;
		if ((op == GNM_FILTER_OP_EQUAL || op == GNM_FILTER_OP_NOT_EQUAL) &&
		    gnumeric_regcomp_XL (fexpr->regexp + i,  str, REG_ICASE) == REG_OK)
			return;
	}
	fexpr->val[i] = value_dup (tmp);
}

static void
filter_expr_release (FilterExpr *fexpr, unsigned i)
{
	if (fexpr->val[i] != NULL)
		value_release (fexpr->val[i]);
	else
		go_regfree (fexpr->regexp + i);
}

static gboolean
filter_expr_eval (GnmFilterOp op, GnmValue const *src, go_regex_t const *regexp,
		  GnmValue *target)
{
	GnmValDiff cmp;

	if (src == NULL) {
		char const *str = value_peek_string (target);
		regmatch_t rm;

		switch (go_regexec (regexp, str, 1, &rm, 0)) {
		case REG_NOMATCH: return op == GNM_FILTER_OP_NOT_EQUAL;
		case REG_OK: return op == GNM_FILTER_OP_EQUAL;

		default:
			g_warning ("Unexpected regexec result");
			return FALSE;
		}
	}

	cmp = value_compare (target, src, TRUE);
	switch (op) {
	case GNM_FILTER_OP_EQUAL     : return cmp == IS_EQUAL;
	case GNM_FILTER_OP_NOT_EQUAL : return cmp != IS_EQUAL;
	case GNM_FILTER_OP_GTE	: if (cmp == IS_EQUAL) return TRUE; /* fall */
	case GNM_FILTER_OP_GT	: return cmp == IS_GREATER;
	case GNM_FILTER_OP_LTE	: if (cmp == IS_EQUAL) return TRUE; /* fall */
	case GNM_FILTER_OP_LT	: return cmp == IS_LESS;
	default :
		g_warning ("Huh?");
		return FALSE;
	};
}

static GnmValue *
cb_filter_expr (Sheet *sheet, int col, int row, GnmCell *cell,
		FilterExpr const *fexpr)
{
	if (cell != NULL) {
		gboolean res = filter_expr_eval (fexpr->cond->op[0],
			fexpr->val[0], fexpr->regexp + 0, cell->value);
		if (fexpr->cond->op[1] != GNM_FILTER_UNUSED) {
			if (fexpr->cond->is_and && !res)
				goto nope;
			if (res && !fexpr->cond->is_and)
				return NULL;
			res = filter_expr_eval (fexpr->cond->op[1],
				fexpr->val[1], fexpr->regexp + 1, cell->value);
		}
		if (res)
			return NULL;
	}

 nope:
	colrow_set_visibility (sheet, FALSE, FALSE, row, row);
	return NULL;
}

/*****************************************************************************/

static GnmValue *
cb_filter_non_blanks (Sheet *sheet, int col, int row, GnmCell *cell, gpointer data)
{
	if (cell_is_blank (cell))
		colrow_set_visibility (sheet, FALSE, FALSE, row, row);
	return NULL;
}

static GnmValue *
cb_filter_blanks (Sheet *sheet, int col, int row, GnmCell *cell, gpointer data)
{
	if (!cell_is_blank (cell))
		colrow_set_visibility (sheet, FALSE, FALSE, row, row);
	return NULL;
}

/*****************************************************************************/

typedef struct {
	unsigned count;
	unsigned elements;
	gboolean find_max;
	GnmValue const **vals;
} FilterItems;

static GnmValue *
cb_filter_find_items (Sheet *sheet, int col, int row, GnmCell *cell,
		      FilterItems *data)
{
	GnmValue const *v = cell->value;
	if (data->elements >= data->count) {
		unsigned j, i = data->elements;
		GnmValDiff const cond = data->find_max ? IS_GREATER : IS_LESS;
		while (i-- > 0)
			if (value_compare (v, data->vals[i], TRUE) == cond) {
				for (j = 0; j < i ; j++)
					data->vals[j] = data->vals[j+1];
				data->vals[i] = v;
				break;
			}
	} else {
		data->vals [data->elements++] = v;
		if (data->elements == data->count) {
			qsort (data->vals, data->elements,
			       sizeof (GnmValue *),
			       data->find_max ? value_cmp : value_cmp_reverse);
		}
	}
	return NULL;
}

static GnmValue *
cb_hide_unwanted_items (Sheet *sheet, int col, int row, GnmCell *cell,
			FilterItems const *data)
{
	if (cell != NULL) {
		int i = data->elements;
		GnmValue const *v = cell->value;

		while (i-- > 0)
			if (data->vals[i] == v)
				return NULL;
	}
	colrow_set_visibility (sheet, FALSE, FALSE, row, row);
	return NULL;
}

/*****************************************************************************/

typedef struct {
	gboolean	initialized, find_max;
	gnm_float	low, high;
} FilterPercentage;

static GnmValue *
cb_filter_find_percentage (Sheet *sheet, int col, int row, GnmCell *cell,
			   FilterPercentage *data)
{
	if (VALUE_IS_NUMBER (cell->value)) {
		gnm_float const v = value_get_as_float (cell->value);

		if (data->initialized) {
			if (data->low > v)
				data->low = v;
			else if (data->high < v)
				data->high = v;
		} else {
			data->initialized = TRUE;
			data->low = data->high = v;
		}
	}
	return NULL;
}

static GnmValue *
cb_hide_unwanted_percentage (Sheet *sheet, int col, int row, GnmCell *cell,
			     FilterPercentage const *data)
{
	if (cell != NULL && VALUE_IS_NUMBER (cell->value)) {
		gnm_float const v = value_get_as_float (cell->value);
		if (data->find_max) {
			if (v >= data->high)
				return NULL;
		} else {
			if (v <= data->low)
				return NULL;
		}
	}
	colrow_set_visibility (sheet, FALSE, FALSE, row, row);
	return NULL;
}
/*****************************************************************************/

static void
filter_field_apply (GnmFilterField *field)
{
	GnmFilter *filter = field->filter;
	int const col = field->parent.anchor.cell_bound.start.col;
	int const start_row = filter->r.start.row + 1;
	int const end_row = filter->r.end.row;

	if (start_row > end_row)
		return;

	if (field->cond == NULL ||
	    field->cond->op[0] == GNM_FILTER_UNUSED)
		return;
	if (0x10 >= (field->cond->op[0] & GNM_FILTER_OP_TYPE_MASK)) {
		FilterExpr data;
		data.cond = field->cond;
		filter_expr_init (&data, 0, field->cond, filter);
		if (field->cond->op[1] != GNM_FILTER_UNUSED)
			filter_expr_init (&data, 1, field->cond, field->filter);

		sheet_foreach_cell_in_range (filter->sheet,
			CELL_ITER_IGNORE_HIDDEN,
			col, start_row, col, end_row,
			(CellIterFunc) cb_filter_expr, &data);

		filter_expr_release (&data, 0);
		if (field->cond->op[1] != GNM_FILTER_UNUSED)
			filter_expr_release (&data, 1);
	} else if (field->cond->op[0] == GNM_FILTER_OP_BLANKS)
		sheet_foreach_cell_in_range (filter->sheet,
			CELL_ITER_IGNORE_HIDDEN,
			col, start_row, col, end_row,
			cb_filter_blanks, NULL);
	else if (field->cond->op[0] == GNM_FILTER_OP_NON_BLANKS)
		sheet_foreach_cell_in_range (filter->sheet,
			CELL_ITER_IGNORE_HIDDEN,
			col, start_row, col, end_row,
			cb_filter_non_blanks, NULL);
	else if (0x30 == (field->cond->op[0] & GNM_FILTER_OP_TYPE_MASK)) {
		if (field->cond->op[0] & 0x2) { /* relative */
			FilterPercentage data;
			gnm_float	 offset;

			data.find_max = (field->cond->op[0] & 0x1) ? FALSE : TRUE;
			data.initialized = FALSE;
			sheet_foreach_cell_in_range (filter->sheet,
				CELL_ITER_IGNORE_HIDDEN | CELL_ITER_IGNORE_BLANK,
				col, start_row, col, end_row,
				(CellIterFunc) cb_filter_find_percentage, &data);
			offset = (data.high - data.low) * field->cond->count / 100.;
			data.high -= offset;
			data.low  += offset;
			sheet_foreach_cell_in_range (filter->sheet,
				CELL_ITER_IGNORE_HIDDEN,
				col, start_row, col, end_row,
				(CellIterFunc) cb_hide_unwanted_percentage, &data);
		} else { /* absolute */
			FilterItems data;
			data.find_max = (field->cond->op[0] & 0x1) ? FALSE : TRUE;
			data.elements    = 0;
			data.count  = field->cond->count;
			data.vals   = g_alloca (sizeof (GnmValue *) * data.count);
			sheet_foreach_cell_in_range (filter->sheet,
				CELL_ITER_IGNORE_HIDDEN | CELL_ITER_IGNORE_BLANK,
				col, start_row, col, end_row,
				(CellIterFunc) cb_filter_find_items, &data);
			sheet_foreach_cell_in_range (filter->sheet,
				CELL_ITER_IGNORE_HIDDEN,
				col, start_row, col, end_row,
				(CellIterFunc) cb_hide_unwanted_items, &data);
		}
	} else
		g_warning ("Invalid operator %d", field->cond->op[0]);
}

static void
filter_field_set_active (GnmFilterField *field)
{
	GList *ptr;
	SheetObject *so = &field->parent;

	for (ptr = so->realized_list; ptr; ptr = ptr->next)
		filter_field_arrow_format (field,
			g_object_get_data (ptr->data, ARROW_ID));
}

/*************************************************************************/

static void
gnm_filter_add_field (GnmFilter *filter, int i)
{
	/* pretend to fill the cell, then clip the X start later */
	static SheetObjectAnchorType const anchor_types [4] = {
		SO_ANCHOR_PERCENTAGE_FROM_COLROW_START,
		SO_ANCHOR_PERCENTAGE_FROM_COLROW_START,
		SO_ANCHOR_PERCENTAGE_FROM_COLROW_END,
		SO_ANCHOR_PERCENTAGE_FROM_COLROW_END
	};
	static float const offsets [4] = { .0, .0, 0., 0. };
	int n;
	GnmRange tmp;
	SheetObjectAnchor anchor;
	GnmFilterField *field = g_object_new (filter_field_get_type (), NULL);

	field->filter = filter;
	tmp.start.row = tmp.end.row = filter->r.start.row;
	tmp.start.col = tmp.end.col = filter->r.start.col + i;
	sheet_object_anchor_init (&anchor, &tmp, offsets, anchor_types,
				  SO_DIR_DOWN_RIGHT);
	sheet_object_set_anchor (&field->parent, &anchor);
	sheet_object_set_sheet (&field->parent, filter->sheet);

	g_ptr_array_add (filter->fields, NULL);
	for (n = filter->fields->len; --n > i ; )
		g_ptr_array_index (filter->fields, n) =
			g_ptr_array_index (filter->fields, n-1);
	g_ptr_array_index (filter->fields, n) = field;
	g_object_unref (G_OBJECT (field));
}

/**
 * gnm_filter_new :
 * @sheet :
 * @r :
 *
 * Init a filter
 **/
GnmFilter *
gnm_filter_new (Sheet *sheet, GnmRange const *r)
{
	GnmFilter	*filter;
	int i;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (r != NULL, NULL);

	filter = g_new0 (GnmFilter, 1);
	filter->sheet = sheet;

	filter->is_active = FALSE;
	filter->r = *r;
	filter->fields = g_ptr_array_new ();

	for (i = 0 ; i < range_width (r); i++)
		gnm_filter_add_field (filter, i);

	sheet->filters = g_slist_prepend (sheet->filters, filter);
	sheet->priv->filters_changed = TRUE;

	return filter;
}

void
gnm_filter_free	(GnmFilter *filter)
{
	unsigned i;

	g_return_if_fail (filter != NULL);

	for (i = 0 ; i < filter->fields->len ; i++)
		sheet_object_clear_sheet (g_ptr_array_index (filter->fields, i));
	g_ptr_array_free (filter->fields, TRUE);

	filter->fields = NULL;
	g_free (filter);
}

void
gnm_filter_remove (GnmFilter *filter)
{
	Sheet *sheet;
	int i;

	g_return_if_fail (filter != NULL);
	g_return_if_fail (filter->sheet != NULL);

	sheet = filter->sheet;
	sheet->priv->filters_changed = TRUE;
	sheet->filters = g_slist_remove (sheet->filters, filter);
	for (i = filter->r.start.row; ++i <= filter->r.end.row ; ) {
		ColRowInfo *ri = sheet_row_get (sheet, i);
		if (ri != NULL) {
			ri->in_filter = FALSE;
			colrow_set_visibility (sheet, FALSE, TRUE, i, i);
		}
	}
}

/**
 * gnm_filter_get_condition :
 * @filter :
 * @i :
 *
 **/
GnmFilterCondition const *
gnm_filter_get_condition (GnmFilter const *filter, unsigned i)
{
	GnmFilterField *field;

	g_return_val_if_fail (filter != NULL, NULL);
	g_return_val_if_fail (i < filter->fields->len, NULL);

	field = g_ptr_array_index (filter->fields, i);
	return field->cond;
}

/**
 * gnm_filter_set_condition :
 * @filter :
 * @i :
 * @cond :
 * @apply :
 *
 **/
void
gnm_filter_set_condition (GnmFilter *filter, unsigned i,
			  GnmFilterCondition *cond,
			  gboolean apply)
{
	GnmFilterField *field;
	gboolean set_infilter = FALSE;
	gboolean existing_cond = FALSE;
	int r;

	g_return_if_fail (filter != NULL);
	g_return_if_fail (i < filter->fields->len);

	field = g_ptr_array_index (filter->fields, i);

	if (field->cond != NULL) {
		existing_cond = TRUE;
		gnm_filter_condition_unref (field->cond);
	}
	field->cond = cond;
	filter_field_set_active (field);

	if (apply) {
		/* if there was an existing cond then we need to do
		 * 1) unfilter everything
		 * 2) reapply all the filters
		 * This is because we do record what elements this particular
		 * field filtered
		 */
		if (existing_cond) {
			colrow_set_visibility (filter->sheet, FALSE, TRUE,
				filter->r.start.row + 1, filter->r.end.row);
			for (i = 0 ; i < filter->fields->len ; i++)
				filter_field_apply (g_ptr_array_index (filter->fields, i));
		} else
			/* When adding a new cond all we need to do is
			 * apply that filter */
			filter_field_apply (field);
	}

	/* set the activity flag and potentially activate the
	 * in_filter flags in the rows */
	if (cond == NULL) {
		for (i = 0 ; i < filter->fields->len ; i++) {
			field = g_ptr_array_index (filter->fields, i);
			if (field->cond != NULL)
				break;
		}
		if (i >= filter->fields->len) {
			filter->is_active = FALSE;
			set_infilter = TRUE;
		}
	} else if (!filter->is_active) {
		filter->is_active = TRUE;
		set_infilter = TRUE;
	}

	if (set_infilter)
		for (r = filter->r.start.row; ++r <= filter->r.end.row ; ) {
			ColRowInfo *ri = sheet_row_fetch (filter->sheet, r);
			ri->in_filter = filter->is_active;
		}
}

/**
 * gnm_filter_overlaps_range :
 * @filter : #GnmFilter
 * @r : #GnmRange
 *
 * Does the range filter by @filter overlap with GnmRange @r
 **/
gboolean
gnm_filter_overlaps_range (GnmFilter const *filter, GnmRange const *r)
{
	g_return_val_if_fail (filter != NULL, FALSE);

	return range_overlap (&filter->r, r);
}

/*************************************************************************/

static gboolean
sheet_cell_or_one_below_is_not_empty (Sheet *sheet, int col, int row)
{
	return !sheet_is_cell_empty (sheet, col, row) ||
		(row < (SHEET_MAX_ROWS - 1) &&
		 !sheet_is_cell_empty (sheet, col, row+1));
}

/**
 * sheet_filter_guess_region :
 * @sheet : #Sheet
 * @range : #GnmRange
 *
 **/
void
sheet_filter_guess_region (Sheet *sheet, GnmRange *region)
{
	int col;
	int end_row;
	int offset;
	
	/* check in case only one cell selected */
	if (region->start.col == region->end.col) {
		int start = region->start.col;
		/* look for previous empty column */
		for (col = start - 1; col > 0; col--)
			if (!sheet_cell_or_one_below_is_not_empty (sheet, col, region->start.row)) 
				break;
		region->start.col = col - 1;

		/* look for next empty column */
		for (col = start + 1; col < SHEET_MAX_COLS; col++)
			if (!sheet_cell_or_one_below_is_not_empty (sheet, col, region->start.row)) 
				break;
		region->end.col = col - 1;
	}		
	
	/* find first and last non-empty cells in region */
	for (col = region->start.col; col <= region->end.col; col++)
		if (sheet_cell_or_one_below_is_not_empty (sheet, col, region->start.row))
			break;

	if (col > region->end.col)
		return; /* all empty -- give up */
	region->start.col = col;
	
	for (col = region->end.col; col >= region->start.col; col--)
		if (sheet_cell_or_one_below_is_not_empty(sheet, col, region->start.row))
			break;
	region->end.col = col;
	
	/* now find length of longest column */
	for (col = region->start.col; col <= region->end.col; col++) {
		offset = 0;
		if (sheet_is_cell_empty(sheet, col, region->start.row))
			offset = 1;
		end_row = sheet_find_boundary_vertical (sheet, col,
			region->start.row + offset, col, 1, TRUE);
		if (end_row > region->end.row) 
			region->end.row = end_row;
	}
}

/**
 * sheet_filter_insdel_colrow :
 * @sheet :
 * @is_cols :
 * @is_insert :
 * @start :
 * @count :
 *
 * Adjust filters as necessary to handle col/row insertions and deletions
 **/
void
sheet_filter_insdel_colrow (Sheet *sheet, gboolean is_cols, gboolean is_insert,
			    int start, int count)
{
	GSList *ptr, *filters;
	GnmFilter *filter;

	g_return_if_fail (IS_SHEET (sheet));

	filters = g_slist_copy (sheet->filters);
	for (ptr = filters; ptr != NULL ; ptr = ptr->next) {
		filter = ptr->data;

		if (is_cols) {
			if (start > filter->r.end.col)	/* a */
				continue;
			if (is_insert) {
				filter->r.end.col += count;
				/* inserting in the middle of a filter adds
				 * fields.  Everything else just moves it */
				if (start > filter->r.start.col &&
				    start <= filter->r.end.col) {
					while (count--)
						gnm_filter_add_field (filter,
							start - filter->r.start.col + count);
				} else
					filter->r.start.col += count;
			} else {
				int start_del = start - filter->r.start.col;
				int end_del   = start_del + count;
				if (start_del <= 0) {
					start_del = 0;
					if (end_del > 0)
						filter->r.start.col = start;	/* c */
					else
						filter->r.start.col -= count;	/* b */
					filter->r.end.col -= count;
				} else {
					if ((unsigned)end_del > filter->fields->len) {
						end_del = filter->fields->len;
						filter->r.end.col = start - 1;	/* d */
					} else
						filter->r.end.col -= count;
				}

				if (filter->r.end.col < filter->r.start.col)
					filter = NULL;
				else
					while (end_del-- > start_del)
						g_ptr_array_remove_index (filter->fields, end_del);
			}
		} else {
			if (start > filter->r.end.row)
				continue;
			if (is_insert) {
				filter->r.end.row += count;
				if (start < filter->r.start.row)
					filter->r.start.row += count;
			} else {
				if (start <= filter->r.start.row) {
					filter->r.end.row -= count;
					if ((start+count) > filter->r.start.row)
						/* delete if the dropdowns are wiped */
						filter->r.start.row = filter->r.end.row + 1;
					else
						filter->r.start.row -= count;
				} else if ((start+count) > filter->r.end.row)
					filter->r.end.row = start -1;
				else
					filter->r.end.row -= count;

				if (filter->r.end.row < filter->r.start.row)
					filter = NULL;
			}
		}

		if (filter == NULL) {
			filter = ptr->data; /* we used it as a flag */
			gnm_filter_remove (filter);
			/* the objects are already gone */
			g_ptr_array_set_size (filter->fields, 0);
			gnm_filter_free (filter);
		}
	}
	if (filters != NULL)
		sheet->priv->filters_changed = TRUE;
	g_slist_free (filters);
}
