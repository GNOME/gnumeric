/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * sheet-object-widget.c: SheetObject wrappers for simple gtk widgets.
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
#include <gnumeric-i18n.h>
#include "gnumeric.h"
#include "sheet-object-widget.h"

#include "gui-util.h"
#include "dependent.h"
#include "gnumeric-canvas.h"
#include "gnumeric-pane.h"
#include "sheet-control-gui.h"
#include "sheet-object-impl.h"
#include "expr.h"
#include "parse-util.h"
#include "value.h"
#include "ranges.h"
#include "selection.h"
#include "workbook-edit.h"
#include "workbook.h"
#include "sheet.h"
#include "cell.h"
#include "value.h"
#include "mathfunc.h"
#include "gnumeric-expr-entry.h"
#include "dialogs.h"
#include "widgets/gnumeric-combo-text.h"

#include <gsf/gsf-impl-utils.h>
#include <libxml/globals.h>
#include <libfoocanvas/foo-canvas-widget.h>
#include <gdk/gdkkeysyms.h>
#include <math.h>

#define SHEET_OBJECT_CONFIG_KEY "sheet-object-config-dialog"

#define SHEET_OBJECT_WIDGET_TYPE     (sheet_object_widget_get_type ())
#define SHEET_OBJECT_WIDGET(obj)     (G_TYPE_CHECK_INSTANCE_CAST((obj), SHEET_OBJECT_WIDGET_TYPE, SheetObjectWidget))
#define SHEET_OBJECT_WIDGET_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), SHEET_OBJECT_WIDGET_TYPE, SheetObjectWidgetClass))
#define IS_SHEET_WIDGET_OBJECT(o)    (G_TYPE_CHECK_INSTANCE_TYPE((o), SHEET_OBJECT_WIDGET_TYPE))
#define SOW_CLASS(so)	 	     (SHEET_OBJECT_WIDGET_CLASS (G_OBJECT_GET_CLASS(so)))

#define SOW_MAKE_TYPE(n1, n2, fn_config, fn_set_sheet, fn_clear_sheet,	\
		      fn_clone, fn_write_xml, fn_read_xml)		\
static void								\
sheet_widget_ ## n1 ## _class_init (GObjectClass *object_class)		\
{									\
	SheetObjectWidgetClass *sow_class = SHEET_OBJECT_WIDGET_CLASS (object_class); \
	SheetObjectClass *so_class = SHEET_OBJECT_CLASS (object_class);	\
	sow_class->create_widget = & sheet_widget_ ## n1 ## _create_widget; \
	so_class->user_config = fn_config;				\
	so_class->assign_to_sheet = fn_set_sheet;			\
	so_class->remove_from_sheet = fn_clear_sheet;			\
	so_class->clone = fn_clone;					\
	so_class->write_xml = fn_write_xml;				\
	so_class->read_xml = fn_read_xml;				\
	object_class->finalize = & sheet_widget_ ## n1 ## _finalize;	\
}									\
GSF_CLASS (SheetWidget ## n2, sheet_widget_ ## n1,			\
	   &sheet_widget_ ## n1 ## _class_init,				\
	   &sheet_widget_ ## n1 ## _init,				\
	   SHEET_OBJECT_WIDGET_TYPE);

typedef struct _SheetObjectWidget SheetObjectWidget;

struct _SheetObjectWidget {
	SheetObject      parent_object;
};

typedef struct {
	SheetObjectClass parent_class;
	GtkWidget *(*create_widget)(SheetObjectWidget *, SheetControlGUI *);
} SheetObjectWidgetClass;

static SheetObjectClass *sheet_object_widget_parent_class = NULL;
static GObjectClass *sheet_object_widget_class = NULL;

static GType sheet_object_widget_get_type	(void);

static GObject *
sheet_object_widget_new_view (SheetObject *so, SheetControl *sc, gpointer key)
{
	GnmCanvas *gcanvas = ((GnumericPane *)key)->gcanvas;
	GtkWidget *view_widget = SOW_CLASS(so)->create_widget (
		SHEET_OBJECT_WIDGET (so), SHEET_CONTROL_GUI (sc));
	FooCanvasItem *view_item = foo_canvas_item_new (
		gcanvas->object_group,
		foo_canvas_widget_get_type (),
		"widget", view_widget,
		"size_pixels", FALSE,
		NULL);
	gnm_pane_widget_register (so, view_widget, view_item);
	gtk_widget_show_all (view_widget);

	return G_OBJECT (view_item);
}

/*
 * This implemenation moves the widget rather than
 * destroying/updating/creating the views
 */
static void
sheet_object_widget_update_bounds (SheetObject *so, GObject *view_obj)
{
	double coords [4];
	FooCanvasItem   *view = FOO_CANVAS_ITEM (view_obj);
	SheetControlGUI	*scg  =
		SHEET_CONTROL_GUI (sheet_object_view_control (view_obj));

	/* NOTE : far point is EXCLUDED so we add 1 */
 	scg_object_view_position (scg, so, coords);
	foo_canvas_item_set (view,
		"x", coords [0], "y", coords [1],
		"width",  coords [2] - coords [0] + 1.,
		"height", coords [3] - coords [1] + 1.,
		NULL);

	if (so->is_visible)
		foo_canvas_item_show (view);
	else
		foo_canvas_item_hide (view);
}

static void
sheet_object_widget_class_init (GtkObjectClass *object_class)
{
	SheetObjectClass *so_class = SHEET_OBJECT_CLASS (object_class);
	SheetObjectWidgetClass *sow_class = SHEET_OBJECT_WIDGET_CLASS (object_class);

	sheet_object_widget_class = G_OBJECT_CLASS (object_class);
	sheet_object_widget_parent_class = g_type_class_peek (sheet_object_get_type ());

	/* SheetObject class method overrides */
	so_class->new_view	= sheet_object_widget_new_view;
	so_class->update_bounds = sheet_object_widget_update_bounds;

	sow_class->create_widget = NULL;
}

static void
sheet_object_widget_init (SheetObjectWidget *sow)
{
	SheetObject *so = SHEET_OBJECT (sow);
	so->type = SHEET_OBJECT_ACTION_CAN_PRESS;
}

static GSF_CLASS (SheetObjectWidget, sheet_object_widget,
		  sheet_object_widget_class_init,
		  sheet_object_widget_init,
		  SHEET_OBJECT_TYPE);

/****************************************************************************/
#define SHEET_WIDGET_FRAME_TYPE     (sheet_widget_frame_get_type ())
#define SHEET_WIDGET_FRAME(obj)     (G_TYPE_CHECK_INSTANCE_CAST((obj), SHEET_WIDGET_FRAME_TYPE, SheetWidgetFrame))
typedef struct {
	SheetObjectWidget	sow;
	char *label;
} SheetWidgetFrame;
typedef struct {
	SheetObjectWidgetClass	sow;
} SheetWidgetFrameClass;

static void
sheet_widget_frame_init_full (SheetWidgetFrame *swf, char const *text)
{
	swf->label = g_strdup (text);
}

static void
sheet_widget_frame_init (SheetWidgetFrame *swf)
{
	sheet_widget_frame_init_full (swf, _("Frame"));
}

static void
sheet_widget_frame_finalize (GObject *obj)
{
	SheetWidgetFrame *swf = SHEET_WIDGET_FRAME (obj);

	if (swf->label != NULL) {
		g_free (swf->label);
		swf->label = NULL;
	}

	(*sheet_object_widget_class->finalize) (obj);
}

static GtkWidget *
sheet_widget_frame_create_widget (SheetObjectWidget *sow, SheetControlGUI *sview)
{
	SheetWidgetFrame *swf = SHEET_WIDGET_FRAME (sow);

	return gtk_frame_new (swf->label);
}

static SheetObject *
sheet_widget_frame_clone (SheetObject const *src_swf, Sheet *new_sheet)
{
	SheetWidgetFrame *swf = g_object_new (SHEET_WIDGET_FRAME_TYPE, NULL);
	sheet_widget_frame_init_full (swf,
		SHEET_WIDGET_FRAME (src_swf)->label);
	return SHEET_OBJECT (swf);
}

static gboolean
sheet_widget_frame_write_xml (SheetObject const *so,
			      XmlParseContext const *context,
			      xmlNodePtr tree)
{
	SheetWidgetFrame *swf = SHEET_WIDGET_FRAME (so);

	xml_node_set_cstr (tree, "Label", swf->label);

	return FALSE;
}

static gboolean
sheet_widget_frame_read_xml (SheetObject *so,
			     XmlParseContext const *context,
			     xmlNodePtr tree)
{
	SheetWidgetFrame *swf = SHEET_WIDGET_FRAME (so);
	gchar *label = (gchar *)xmlGetProp (tree, (xmlChar *)"Label");

	if (!label) {
		g_warning ("Could not read a SheetWidgetFrame beacause it lacks a label property.");
		return TRUE;
	}

	swf->label = g_strdup (label);
	xmlFree (label);

	return FALSE;
}

typedef struct {
  	GladeXML           *gui;
  	GtkWidget          *dialog;
  	GtkWidget          *label;

  	char               *old_label;
  	GtkWidget          *old_focus;

  	WorkbookControlGUI *wbcg;
  	SheetWidgetFrame   *swc;
  	Sheet		   *sheet;
} FrameConfigState;

static gboolean
cb_frame_config_destroy (GtkObject *w, FrameConfigState *state)
{
 	g_return_val_if_fail (w != NULL, FALSE);
 	g_return_val_if_fail (state != NULL, FALSE);

 	wbcg_edit_detach_guru (state->wbcg);

 	if (state->gui != NULL) {
 		g_object_unref (G_OBJECT (state->gui));
 		state->gui = NULL;
 	}

 	g_free (state->old_label);
 	state->old_label = NULL;
 	state->dialog = NULL;
 	g_free (state); 

 	return FALSE;
}

static void
cb_frame_config_ok_clicked (GtkWidget *button, FrameConfigState *state)
{
  	gtk_widget_destroy (state->dialog);
}

static void
cb_frame_config_cancel_clicked (GtkWidget *button, FrameConfigState *state)
{
  	GList *list;
  	SheetWidgetFrame *swc;
  	swc = state->swc;
  	if (swc->label)
  		g_free(swc->label);
  	swc->label = g_strdup(state->old_label);
  	list = swc->sow.parent_object.realized_list;
  	for(;list!=NULL;list=list->next){
		gtk_frame_set_label 
			(GTK_FRAME(FOO_CANVAS_WIDGET(list->data)->widget), 
			 state->old_label);
  	}
  
  	gtk_widget_destroy (state->dialog);
}

static void
cb_frame_label_changed(GtkWidget *entry, FrameConfigState *state)
{
  	GList *list;
  	SheetWidgetFrame *swc;
  	gchar const *text;

  	text = gtk_entry_get_text(GTK_ENTRY(entry));
  	swc = state->swc;
  	if (swc->label)
  		g_free(swc->label);
  
	swc->label = g_strdup(text);
  	for (list = swc->sow.parent_object.realized_list; list != NULL; 
	     list = list->next){
		gtk_frame_set_label
			(GTK_FRAME(FOO_CANVAS_WIDGET(list->data)->widget),
			 text);
	}
}

static void
sheet_widget_frame_user_config (SheetObject *so, SheetControl *sc)
{
  	SheetWidgetFrame *swc = SHEET_WIDGET_FRAME (so);
  	WorkbookControlGUI   *wbcg = scg_get_wbcg (SHEET_CONTROL_GUI (sc));
  	FrameConfigState *state;
  	GtkWidget *table;

  	g_return_if_fail (swc != NULL);

  	/* Only pop up one copy per workbook */
  	if (gnumeric_dialog_raise_if_exists (wbcg, SHEET_OBJECT_CONFIG_KEY))
  		return;

  	state = g_new (FrameConfigState, 1);
  	state->swc = swc;
  	state->wbcg = wbcg;
  	state->sheet = sc_sheet	(sc);
  	state->old_focus = NULL;
  	state->old_label = g_strdup(swc->label);
  	state->gui = gnm_glade_xml_new (COMMAND_CONTEXT (wbcg),
		"so-frame.glade", NULL, NULL);
  	state->dialog = glade_xml_get_widget (state->gui, "so_frame");

	table = glade_xml_get_widget(state->gui, "table");

  	state->label = glade_xml_get_widget (state->gui, "entry");

  	gtk_entry_set_text (GTK_ENTRY(state->label),swc->label);
	gtk_editable_select_region (GTK_EDITABLE(state->label),0,-1);

  	g_signal_connect (G_OBJECT(state->label),
			  "changed",
			  G_CALLBACK (cb_frame_label_changed), state);

  	g_signal_connect (G_OBJECT (state->dialog),
			  "destroy",
			  G_CALLBACK (cb_frame_config_destroy), state);

  	g_signal_connect (G_OBJECT (glade_xml_get_widget (state->gui, 
							  "ok_button")),
			  "clicked",
			  G_CALLBACK (cb_frame_config_ok_clicked), state);
  	g_signal_connect (G_OBJECT (glade_xml_get_widget (state->gui, 
							  "cancel_button")),
			  "clicked",
			  G_CALLBACK (cb_frame_config_cancel_clicked), state);
  	gnumeric_init_help_button (
  		glade_xml_get_widget (state->gui, "help_button"),
  		"so-frame.html");
  

  	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
  			       SHEET_OBJECT_CONFIG_KEY);

  	wbcg_edit_attach_guru (state->wbcg, state->dialog);

  	gtk_widget_show (state->dialog);
}

SOW_MAKE_TYPE (frame, Frame,
	       &sheet_widget_frame_user_config,
	       NULL,
	       NULL,
	       &sheet_widget_frame_clone,
	       &sheet_widget_frame_write_xml,
	       &sheet_widget_frame_read_xml);

/****************************************************************************/
#define SHEET_WIDGET_BUTTON_TYPE     (sheet_widget_button_get_type ())
#define SHEET_WIDGET_BUTTON(obj)     (G_TYPE_CHECK_INSTANCE_CAST((obj), SHEET_WIDGET_BUTTON_TYPE, SheetWidgetButton))
typedef struct {
	SheetObjectWidget	sow;
	char *label;
} SheetWidgetButton;
typedef struct {
	SheetObjectWidgetClass	sow;
} SheetWidgetButtonClass;

static void
sheet_widget_button_init_full (SheetWidgetButton *swb, char const *text)
{
	swb->label = g_strdup (text);
}

static void
sheet_widget_button_init (SheetWidgetButton *swb)
{
	sheet_widget_button_init_full (swb, _("Button"));
}

static void
sheet_widget_button_finalize (GObject *obj)
{
	SheetWidgetButton *swb = SHEET_WIDGET_BUTTON (obj);

	g_free (swb->label);
	swb->label = NULL;

	(*sheet_object_widget_class->finalize)(obj);
}

static GtkWidget *
sheet_widget_button_create_widget (SheetObjectWidget *sow, SheetControlGUI *sview)
{
	SheetWidgetButton *swb = SHEET_WIDGET_BUTTON (sow);

	return gtk_button_new_with_label (swb->label);
}

static SheetObject *
sheet_widget_button_clone (SheetObject const *src_swb, Sheet *new_sheet)
{
	SheetWidgetButton *swb = g_object_new (SHEET_WIDGET_BUTTON_TYPE, NULL);
	sheet_widget_button_init_full (swb,
		SHEET_WIDGET_BUTTON (src_swb)->label);
	return SHEET_OBJECT (swb);
}

static gboolean
sheet_widget_button_write_xml (SheetObject const *so,
			       XmlParseContext const *context,
			       xmlNodePtr tree)
{
	SheetWidgetButton *swb = SHEET_WIDGET_BUTTON (so);

	xml_node_set_cstr (tree, "Label", swb->label);

	return FALSE;
}

static gboolean
sheet_widget_button_read_xml (SheetObject *so,
			      XmlParseContext const *context,
			      xmlNodePtr tree)
{
	SheetWidgetButton *swb = SHEET_WIDGET_BUTTON (so);
	gchar *label = (gchar *)xmlGetProp (tree, (xmlChar *)"Label");

	if (!label) {
		g_warning ("Could not read a SheetWidgetButton beacause it lacks a label property.");
		return TRUE;
	}

	swb->label = g_strdup (label);
	xmlFree (label);

	return FALSE;
}

void
sheet_widget_button_set_label (SheetObject *so, char const *str)
{
	GList *list;
	SheetWidgetButton *swb = SHEET_WIDGET_BUTTON (so);

	puts (str);
	g_free (swb->label);
	swb->label = g_strdup (str);

 	list = swb->sow.parent_object.realized_list;
 	for (; list != NULL; list = list->next) {
 		FooCanvasWidget *item = FOO_CANVAS_WIDGET (list->data);
 		gtk_button_set_label (GTK_BUTTON (item->widget), swb->label);
 	}
}

SOW_MAKE_TYPE (button, Button,
	       NULL,
	       NULL,
	       NULL,
	       &sheet_widget_button_clone,
	       &sheet_widget_button_write_xml,
	       &sheet_widget_button_read_xml);

/****************************************************************************/
#define SHEET_WIDGET_SCROLLBAR_TYPE     (sheet_widget_scrollbar_get_type ())
#define SHEET_WIDGET_SCROLLBAR(obj)     (G_TYPE_CHECK_INSTANCE_CAST ((obj), SHEET_WIDGET_SCROLLBAR_TYPE, SheetWidgetScrollbar))
#define DEP_TO_SCROLLBAR(d_ptr)		(SheetWidgetScrollbar *)(((char *)d_ptr) - G_STRUCT_OFFSET(SheetWidgetScrollbar, dep))

typedef struct {
	SheetObjectWidget	sow;

	gboolean  being_updated;
	Dependent dep;
	GtkAdjustment *adjustment;
} SheetWidgetScrollbar;
typedef struct {
	SheetObjectWidgetClass	sow;
} SheetWidgetScrollbarClass;

static void
sheet_widget_scrollbar_set_value (SheetWidgetScrollbar *swb, gfloat new_val)
{
	if (swb->being_updated)
		return;
	swb->adjustment->value = new_val;

	swb->being_updated = TRUE;
	gtk_adjustment_value_changed (swb->adjustment);
	swb->being_updated = FALSE;
}

static void
scrollbar_eval (Dependent *dep)
{
	Value *v;
	EvalPos pos;

	v = gnm_expr_eval (dep->expression, eval_pos_init_dep (&pos, dep),
			   GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
	sheet_widget_scrollbar_set_value (DEP_TO_SCROLLBAR(dep),
		value_get_as_float (v));
	value_release (v);
}

static void
scrollbar_debug_name (Dependent const *dep, FILE *out)
{
	fprintf (out, "Scrollbar%p", dep);
}

static DEPENDENT_MAKE_TYPE (scrollbar, NULL)

static CellRef *
sheet_widget_scrollbar_get_ref (SheetWidgetScrollbar const *swb,
				CellRef *res, gboolean force_sheet)
{
	Value *target;
	g_return_val_if_fail (swb != NULL, NULL);

	if (swb->dep.expression == NULL)
		return NULL;

	target = gnm_expr_get_range (swb->dep.expression);
	if (target == NULL)
		return NULL;

	*res = target->v_range.cell.a;
	value_release (target);

	g_return_val_if_fail (!res->col_relative, NULL);
	g_return_val_if_fail (!res->row_relative, NULL);

	if (force_sheet && res->sheet == NULL)
		res->sheet = sheet_object_get_sheet (SHEET_OBJECT (swb));
	return res;
}

static void
cb_scrollbar_value_changed (GtkAdjustment *adjustment,
			    SheetWidgetScrollbar *swb)
{
	CellRef ref;

	if (swb->being_updated)
		return;

	if (sheet_widget_scrollbar_get_ref (swb, &ref, TRUE) != NULL) {
		Cell *cell = sheet_cell_fetch (ref.sheet, ref.col, ref.row);
		/* TODO : add more control for precision, XL is stupid */
		int new_val = gnumeric_fake_round (swb->adjustment->value);
		if (cell->value != NULL &&
		    cell->value->type == VALUE_INTEGER &&
		    cell->value->v_int.val == new_val)
			return;

		swb->being_updated = TRUE;
		sheet_cell_set_value (cell, value_new_int (swb->adjustment->value));
		swb->being_updated = FALSE;

		sheet_set_dirty (ref.sheet, TRUE);
		workbook_recalc (ref.sheet->workbook);
		sheet_update (ref.sheet);
	}
}

static void
sheet_widget_scrollbar_init_full (SheetWidgetScrollbar *swb, CellRef const *ref)
{
	g_return_if_fail (swb != NULL);

	swb->adjustment = GTK_ADJUSTMENT (gtk_adjustment_new (0., 0., 100., 1., 10., 1.));
	gtk_object_ref (GTK_OBJECT (swb->adjustment));
	gtk_object_sink (GTK_OBJECT (swb->adjustment));

	swb->being_updated = FALSE;
	swb->dep.sheet = NULL;
	swb->dep.flags = scrollbar_get_dep_type ();
	swb->dep.expression = (ref != NULL) ? gnm_expr_new_cellref (ref) : NULL;
	g_signal_connect (G_OBJECT (swb->adjustment),
		"value_changed",
		G_CALLBACK (cb_scrollbar_value_changed), swb);
}

static void
sheet_widget_scrollbar_init (SheetWidgetScrollbar *swb)
{
	sheet_widget_scrollbar_init_full (swb, NULL);
}

static void
sheet_widget_scrollbar_finalize (GObject *obj)
{
	SheetWidgetScrollbar *swb = SHEET_WIDGET_SCROLLBAR (obj);

	g_return_if_fail (swb != NULL);

	dependent_set_expr (&swb->dep, NULL);
	if (swb->adjustment != NULL) {
		gtk_object_unref (GTK_OBJECT (swb->adjustment));
		swb->adjustment = NULL;
	}

	(*sheet_object_widget_class->finalize)(obj);
}

static GtkWidget *
sheet_widget_scrollbar_create_widget (SheetObjectWidget *sow, SheetControlGUI *sview)
{
	SheetObject *so = SHEET_OBJECT (sow);
	SheetWidgetScrollbar *swb = SHEET_WIDGET_SCROLLBAR (sow);
	GtkWidget *bar;
	/* TODO : this is not exactly accurate, but should catch the worst of it
	 * Howver we do not have a way to handle resizes.
	 */
	gboolean is_horizontal = range_width (&so->anchor.cell_bound) > range_height (&so->anchor.cell_bound);

	g_return_val_if_fail (swb != NULL, NULL);

	bar = is_horizontal
		? gtk_hscrollbar_new (swb->adjustment)
		: gtk_vscrollbar_new (swb->adjustment);
	GTK_WIDGET_UNSET_FLAGS (bar, GTK_CAN_FOCUS);

	return bar;
}

static SheetObject *
sheet_widget_scrollbar_clone (SheetObject const *src_so, Sheet *new_sheet)
{
	SheetWidgetScrollbar *src_swb = SHEET_WIDGET_SCROLLBAR (src_so);
	SheetWidgetScrollbar *swb = g_object_new (SHEET_WIDGET_SCROLLBAR_TYPE, NULL);
	GtkAdjustment *adjust, *src_adjust;
	CellRef ref;

	sheet_widget_scrollbar_init_full (swb,
		sheet_widget_scrollbar_get_ref (src_swb, &ref, FALSE));
	adjust = swb->adjustment;
	src_adjust = src_swb->adjustment;

	adjust->lower = src_adjust->lower;
	adjust->upper = src_adjust->upper;
	adjust->value = src_adjust->value;
	adjust->step_increment = src_adjust->step_increment;
	adjust->page_increment = src_adjust->page_increment;

	return SHEET_OBJECT (swb);
}

typedef struct {
	GladeXML           *gui;
	GtkWidget          *dialog;
	GnumericExprEntry  *expression;
	GtkWidget          *min;
	GtkWidget          *max;
	GtkWidget          *inc;
	GtkWidget          *page;

	GtkWidget          *old_focus;

	WorkbookControlGUI *wbcg;
	SheetWidgetScrollbar *swb;
	Sheet		   *sheet;
} ScrollbarConfigState;

static void
cb_scrollbar_set_focus (GtkWidget *window, GtkWidget *focus_widget,
			ScrollbarConfigState *state)
{
	/* Note:  half of the set-focus action is handle by the default */
	/*        callback installed by wbcg_edit_attach_guru           */

	/* Force an update of the content in case it
	 * needs tweaking (eg make it absolute)
	 */
	if (state->old_focus != NULL && 
	    IS_GNUMERIC_EXPR_ENTRY (state->old_focus->parent)) {
		ParsePos  pp;
		GnmExpr const *expr = gnm_expr_entry_parse (
			GNUMERIC_EXPR_ENTRY (state->old_focus->parent),
			parse_pos_init (&pp, NULL, state->sheet, 0, 0),
			NULL, FALSE);
		if (expr != NULL)
			gnm_expr_unref (expr);
	}
	state->old_focus = focus_widget;
}

static gboolean
cb_scrollbar_config_destroy (GtkObject *w, ScrollbarConfigState *state)
{
	g_return_val_if_fail (w != NULL, FALSE);
	g_return_val_if_fail (state != NULL, FALSE);

	wbcg_edit_detach_guru (state->wbcg);

	if (state->gui != NULL) {
		g_object_unref (G_OBJECT (state->gui));
		state->gui = NULL;
	}

	state->dialog = NULL;
	g_free (state);

	return FALSE;
}

static void
cb_scrollbar_config_ok_clicked (GtkWidget *button, ScrollbarConfigState *state)
{
	SheetObject *so = SHEET_OBJECT (state->swb);
	ParsePos  pp;
	GnmExpr const *expr = gnm_expr_entry_parse (state->expression,
						    parse_pos_init (&pp, NULL, so->sheet, 0, 0),
						    NULL, FALSE);
	if (expr != NULL) {
		dependent_set_expr (&state->swb->dep, expr);
		gnm_expr_unref (expr);
	}

	state->swb->adjustment->lower = gtk_spin_button_get_value_as_int (
		GTK_SPIN_BUTTON (state->min));
	state->swb->adjustment->upper = gtk_spin_button_get_value_as_int (
		GTK_SPIN_BUTTON (state->max)) + 1;
	state->swb->adjustment->step_increment = gtk_spin_button_get_value_as_int (
		GTK_SPIN_BUTTON (state->inc));
	state->swb->adjustment->page_increment = gtk_spin_button_get_value_as_int (
		GTK_SPIN_BUTTON (state->page));

	gtk_adjustment_changed	(state->swb->adjustment);

	gtk_widget_destroy (state->dialog);
}

static void
cb_scrollbar_config_cancel_clicked (GtkWidget *button, ScrollbarConfigState *state)
{
	gtk_widget_destroy (state->dialog);
}

static void
sheet_widget_scrollbar_user_config (SheetObject *so, SheetControl *sc)
{
	SheetWidgetScrollbar *swb = SHEET_WIDGET_SCROLLBAR (so);
	WorkbookControlGUI   *wbcg = scg_get_wbcg (SHEET_CONTROL_GUI (sc));
	ScrollbarConfigState *state;
	GtkWidget *table;

	g_return_if_fail (swb != NULL);

	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, SHEET_OBJECT_CONFIG_KEY))
		return;

	state = g_new (ScrollbarConfigState, 1);
	state->swb = swb;
	state->wbcg = wbcg;
	state->sheet = sc_sheet	(sc);
	state->old_focus = NULL;
	state->gui = gnm_glade_xml_new (COMMAND_CONTEXT (wbcg),
		"so-scrollbar.glade", NULL, NULL);
	state->dialog = glade_xml_get_widget (state->gui, "SO-Scrollbar");

 	table = glade_xml_get_widget (state->gui, "table");

	state->expression = gnumeric_expr_entry_new (wbcg, TRUE);
	gnm_expr_entry_set_flags (state->expression,
		GNM_EE_ABS_ROW | GNM_EE_ABS_COL | GNM_EE_SHEET_OPTIONAL | GNM_EE_SINGLE_RANGE,
		GNM_EE_MASK);
	gnm_expr_entry_set_scg (state->expression, SHEET_CONTROL_GUI (sc));
	gnm_expr_entry_load_from_dep (state->expression, &swb->dep);
	gtk_table_attach (GTK_TABLE (table), GTK_WIDGET (state->expression),
			  1, 2, 0, 1,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
	gtk_widget_show (GTK_WIDGET (state->expression));

	/* TODO : This is silly, no need to be similar to XL here. */
	state->min = glade_xml_get_widget (state->gui, "spin_min");
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (state->min), swb->adjustment->lower);
	state->max = glade_xml_get_widget (state->gui, "spin_max");
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (state->max), swb->adjustment->upper);
	state->inc = glade_xml_get_widget (state->gui, "spin_increment");
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (state->inc), swb->adjustment->step_increment);
	state->page = glade_xml_get_widget (state->gui, "spin_page");
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (state->page), swb->adjustment->page_increment);

	g_signal_connect (G_OBJECT (state->dialog),
		"destroy",
		G_CALLBACK (cb_scrollbar_config_destroy), state);

	g_signal_connect (G_OBJECT (glade_xml_get_widget (state->gui, "ok_button")),
		"clicked",
		G_CALLBACK (cb_scrollbar_config_ok_clicked), state);
	g_signal_connect (G_OBJECT (glade_xml_get_widget (state->gui, "cancel_button")),
		"clicked",
		G_CALLBACK (cb_scrollbar_config_cancel_clicked), state);
	gnumeric_init_help_button (
		glade_xml_get_widget (state->gui, "help_button"),
		"so-scrollbar.html");


	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       SHEET_OBJECT_CONFIG_KEY);

	wbcg_edit_attach_guru (state->wbcg, state->dialog);
	/* Note:  half of the set-focus action is handle by the default */
	/*        callback installed by wbcg_edit_attach_guru           */
	g_signal_connect (G_OBJECT (state->dialog),
		"set-focus",
		G_CALLBACK (cb_scrollbar_set_focus), state);

	gtk_widget_show (state->dialog);
}

static gboolean
sheet_widget_scrollbar_set_sheet (SheetObject *so, Sheet *sheet)
{
	SheetWidgetScrollbar *swb = SHEET_WIDGET_SCROLLBAR (so);

	dependent_set_sheet (&swb->dep, sheet);

	return FALSE;
}

static gboolean
sheet_widget_scrollbar_clear_sheet (SheetObject *so)
{
	SheetWidgetScrollbar *swb = SHEET_WIDGET_SCROLLBAR (so);

	if (dependent_is_linked (&swb->dep))
		dependent_unlink (&swb->dep, NULL);
	swb->dep.sheet = NULL;
	return FALSE;
}

static gboolean
sheet_widget_scrollbar_write_xml (SheetObject const *so,
				 XmlParseContext const *context,
				 xmlNodePtr tree)
{
	SheetWidgetScrollbar *swb = SHEET_WIDGET_SCROLLBAR (so);

	xml_node_set_double (tree, "Min", swb->adjustment->lower, 2);
	xml_node_set_double (tree, "Max", swb->adjustment->upper-1., 2); /* allow scrolling to max */
	xml_node_set_double (tree, "Inc", swb->adjustment->step_increment, 2);
	xml_node_set_double (tree, "Page", swb->adjustment->page_increment, 2);
	xml_node_set_double  (tree, "Value", swb->adjustment->value, 2);
	if (swb->dep.expression != NULL) {
		ParsePos pos;
		char *val = gnm_expr_as_string (swb->dep.expression,
			parse_pos_init (&pos, NULL, so->sheet, 0, 0),
			gnm_expr_conventions_default);
		xml_node_set_cstr (tree, "Input", val);
	}

	return FALSE;
}

static gboolean
sheet_widget_scrollbar_read_xml (SheetObject *so,
				 XmlParseContext const *context,
				 xmlNodePtr tree)
{
	SheetWidgetScrollbar *swb = SHEET_WIDGET_SCROLLBAR (so);
	double tmp;
	gchar *input_txt;

	swb->dep.sheet = NULL;
	swb->dep.expression = NULL;
	swb->dep.flags = scrollbar_get_dep_type ();

	input_txt = (gchar *)xmlGetProp (tree, (xmlChar *)"Input");
	if (input_txt != NULL && *input_txt != '\0') {
		ParsePos pos;
		GnmExpr const *expr = gnm_expr_parse_str_simple (input_txt,
			parse_pos_init (&pos, NULL, context->sheet, 0, 0));

		if (expr == NULL) {
			g_warning ("Could not read scrollbar widget object. Could not parse expr.");
			xmlFree (input_txt);
			return TRUE;
		}

		swb->dep.expression = expr;

		xmlFree (input_txt);
	}

	if (xml_node_get_double (tree, "Min", &tmp))
		swb->adjustment->lower = tmp;
	if (xml_node_get_double (tree, "Max", &tmp))
		swb->adjustment->upper = tmp + 1.; /* allow scrolling to max */
	if (xml_node_get_double (tree, "Inc", &tmp))
		swb->adjustment->step_increment = tmp;
	if (xml_node_get_double (tree, "Page", &tmp))
		swb->adjustment->page_increment = tmp;
	if (xml_node_get_double  (tree, "Value", &tmp))
		swb->adjustment->value = tmp;
	gtk_adjustment_changed	(swb->adjustment);

	return FALSE;
}

void
sheet_widget_scrollbar_set_details (SheetObject *so, GnmExpr const *link,
				    int value, int min, int max, int inc, int page)
{
	SheetWidgetScrollbar *swb = SHEET_WIDGET_SCROLLBAR (so);
	g_return_if_fail (swb != NULL);
	swb->adjustment->value = value;
	swb->adjustment->lower = min;
	swb->adjustment->upper = max + 1.; /* allow scrolling to max */
	swb->adjustment->step_increment = inc;
	swb->adjustment->page_increment = page;
	if (link != NULL)
		dependent_set_expr (&swb->dep, link);
	else
		gtk_adjustment_changed (swb->adjustment);
}

SOW_MAKE_TYPE (scrollbar, Scrollbar,
	       &sheet_widget_scrollbar_user_config,
	       &sheet_widget_scrollbar_set_sheet,
	       &sheet_widget_scrollbar_clear_sheet,
	       &sheet_widget_scrollbar_clone,
	       &sheet_widget_scrollbar_write_xml,
	       &sheet_widget_scrollbar_read_xml)

/****************************************************************************/
#define SHEET_WIDGET_CHECKBOX_TYPE	(sheet_widget_checkbox_get_type ())
#define SHEET_WIDGET_CHECKBOX(obj)	(G_TYPE_CHECK_INSTANCE_CAST((obj), SHEET_WIDGET_CHECKBOX_TYPE, SheetWidgetCheckbox))
#define DEP_TO_CHECKBOX(d_ptr)		(SheetWidgetCheckbox *)(((char *)d_ptr) - G_STRUCT_OFFSET(SheetWidgetCheckbox, dep))

typedef struct {
	SheetObjectWidget	sow;

	char	 *label;
	gboolean  being_updated;
	Dependent dep;
	gboolean  value;
} SheetWidgetCheckbox;
typedef struct {
	SheetObjectWidgetClass	sow;
} SheetWidgetCheckboxClass;

static void
sheet_widget_checkbox_set_active (SheetWidgetCheckbox *swc)
{
	GList *ptr;

	swc->being_updated = TRUE;

	ptr = swc->sow.parent_object.realized_list;
	for (; ptr != NULL ; ptr = ptr->next) {
		FooCanvasWidget *item = FOO_CANVAS_WIDGET (ptr->data);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (item->widget),
					      swc->value);
	}

	swc->being_updated = FALSE;
}

static void
checkbox_eval (Dependent *dep)
{
	Value *v;
	EvalPos pos;
	gboolean err, result;

	v = gnm_expr_eval (dep->expression, eval_pos_init_dep (&pos, dep),
			   GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
	result = value_get_as_bool (v, &err);
	value_release (v);
	if (!err) {
		SheetWidgetCheckbox *swc = DEP_TO_CHECKBOX(dep);

		swc->value = result;
		sheet_widget_checkbox_set_active (swc);
	}
}

static void
checkbox_debug_name (Dependent const *dep, FILE *out)
{
	fprintf (out, "Checkbox%p", dep);
}

static DEPENDENT_MAKE_TYPE (checkbox, NULL)

static void
sheet_widget_checkbox_init_full (SheetWidgetCheckbox *swc,
				 CellRef const *ref, char const *label)
{
	static int counter = 0;

	g_return_if_fail (swc != NULL);

	swc->label = label ? g_strdup (label) : g_strdup_printf (_("CheckBox %d"), ++counter);
	swc->being_updated = FALSE;
	swc->value = FALSE;
	swc->dep.sheet = NULL;
	swc->dep.flags = checkbox_get_dep_type ();
	swc->dep.expression = (ref != NULL) ? gnm_expr_new_cellref (ref) : NULL;
}

static void
sheet_widget_checkbox_init (SheetWidgetCheckbox *swc)
{
	sheet_widget_checkbox_init_full (swc, NULL, NULL);
}

static void
sheet_widget_checkbox_finalize (GObject *obj)
{
	SheetWidgetCheckbox *swc = SHEET_WIDGET_CHECKBOX (obj);

	g_return_if_fail (swc != NULL);

	if (swc->label != NULL) {
		g_free (swc->label);
		swc->label = NULL;
	}

	dependent_set_expr (&swc->dep, NULL);

	(*sheet_object_widget_class->finalize) (obj);
}

static CellRef *
sheet_widget_checkbox_get_ref (SheetWidgetCheckbox const *swc,
			       CellRef *res, gboolean force_sheet)
{
	Value *target;
	g_return_val_if_fail (swc != NULL, NULL);

	if (swc->dep.expression == NULL)
		return NULL;

	target = gnm_expr_get_range (swc->dep.expression);
	if (target == NULL)
		return NULL;

	*res = target->v_range.cell.a;
	value_release (target);

	g_return_val_if_fail (!res->col_relative, NULL);
	g_return_val_if_fail (!res->row_relative, NULL);

	if (force_sheet && res->sheet == NULL)
		res->sheet = sheet_object_get_sheet (SHEET_OBJECT (swc));
	return res;
}

static void
cb_checkbox_toggled (GtkToggleButton *button, SheetWidgetCheckbox *swc)
{
	CellRef ref;

	if (swc->being_updated)
		return;
	swc->value = gtk_toggle_button_get_active (button);
	sheet_widget_checkbox_set_active (swc);

	if (sheet_widget_checkbox_get_ref (swc, &ref, TRUE) != NULL) {
		gboolean const new_val = gtk_toggle_button_get_active (button);
		Cell *cell = sheet_cell_fetch (ref.sheet, ref.col, ref.row);
		sheet_cell_set_value (cell, value_new_bool (new_val));
		sheet_set_dirty (ref.sheet, TRUE);
		workbook_recalc (ref.sheet->workbook);
		sheet_update (ref.sheet);
	}
}

static GtkWidget *
sheet_widget_checkbox_create_widget (SheetObjectWidget *sow, SheetControlGUI *sview)
{
	SheetWidgetCheckbox *swc = SHEET_WIDGET_CHECKBOX (sow);
	GtkWidget *button;

	g_return_val_if_fail (swc != NULL, NULL);

	button = gtk_check_button_new_with_label (swc->label);
	GTK_WIDGET_UNSET_FLAGS (button, GTK_CAN_FOCUS);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), swc->value);
	g_signal_connect (G_OBJECT (button),
		"toggled",
		G_CALLBACK (cb_checkbox_toggled), swc);

	return button;
}

static SheetObject *
sheet_widget_checkbox_clone (SheetObject const *src_so, Sheet *new_sheet)
{
	SheetWidgetCheckbox *src_swc = SHEET_WIDGET_CHECKBOX (src_so);
	SheetWidgetCheckbox *swc = g_object_new (SHEET_WIDGET_CHECKBOX_TYPE, NULL);
	CellRef ref;

	sheet_widget_checkbox_init_full (swc,
		sheet_widget_checkbox_get_ref (src_swc, &ref, FALSE),
		src_swc->label);
	swc->value = src_swc->value;

	return SHEET_OBJECT (swc);
}

typedef struct {
	GladeXML           *gui;
	GtkWidget *dialog;
	GnumericExprEntry *expression;
	GtkWidget *label;

	char *old_label;
	GtkWidget *old_focus;

	WorkbookControlGUI  *wbcg;
	SheetWidgetCheckbox *swc;
	Sheet		    *sheet;
} CheckboxConfigState;

static void
cb_checkbox_set_focus (GtkWidget *window, GtkWidget *focus_widget,
		       CheckboxConfigState *state)
{
	/* Note:  have of the set-focus action is handle by the default */
	/*        callback installed by wbcg_edit_attach_guru           */

	/* Force an update of the content in case it
	 * needs tweaking (eg make it absolute)
	 */
	if (state->old_focus != NULL && 
	    IS_GNUMERIC_EXPR_ENTRY (state->old_focus->parent)) {
		ParsePos  pp;
		GnmExpr const *expr = gnm_expr_entry_parse (
			GNUMERIC_EXPR_ENTRY (state->old_focus->parent),
			parse_pos_init (&pp, NULL, state->sheet, 0, 0),
			NULL, FALSE);
		if (expr != NULL)
			gnm_expr_unref (expr);
	}
	state->old_focus = focus_widget;
}

static gboolean
cb_checkbox_config_destroy (GtkObject *w, CheckboxConfigState *state)
{
	g_return_val_if_fail (w != NULL, FALSE);
	g_return_val_if_fail (state != NULL, FALSE);

	wbcg_edit_detach_guru (state->wbcg);

	if (state->gui != NULL) {
		g_object_unref (G_OBJECT (state->gui));
		state->gui = NULL;
	}

	g_free (state->old_label);
	state->old_label = NULL;
	state->dialog = NULL;
	g_free (state);

	return FALSE;
}

static void
cb_checkbox_config_ok_clicked (GtkWidget *button, CheckboxConfigState *state)
{
	SheetObject *so = SHEET_OBJECT (state->swc);
	ParsePos  pp;
	GnmExpr const *expr = gnm_expr_entry_parse (state->expression,
						    parse_pos_init (&pp, NULL, so->sheet, 0, 0),
						    NULL, FALSE);
	if (expr != NULL) {
		dependent_set_expr (&state->swc->dep, expr);
		gnm_expr_unref (expr);
	}

	gtk_widget_destroy (state->dialog);
}

static void
cb_checkbox_config_cancel_clicked (GtkWidget *button, CheckboxConfigState *state)
{
	sheet_widget_checkbox_set_label	(SHEET_OBJECT (state->swc),
		state->old_label);
	gtk_widget_destroy (state->dialog);
}

static void
cb_checkbox_label_changed (GtkWidget *entry, CheckboxConfigState *state)
{
	sheet_widget_checkbox_set_label	(SHEET_OBJECT (state->swc),
		gtk_entry_get_text (GTK_ENTRY (entry)));
	gtk_widget_destroy (state->dialog);
}

static void
sheet_widget_checkbox_user_config (SheetObject *so, SheetControl *sc)
{
	SheetWidgetCheckbox *swc = SHEET_WIDGET_CHECKBOX (so);
	WorkbookControlGUI  *wbcg = scg_get_wbcg (SHEET_CONTROL_GUI (sc));
	CheckboxConfigState *state;
	GtkWidget *table;

	g_return_if_fail (swc != NULL);

	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, SHEET_OBJECT_CONFIG_KEY))
		return;

	state = g_new (CheckboxConfigState, 1);
	state->swc = swc;
	state->wbcg = wbcg;
	state->sheet = sc_sheet	(sc);
	state->old_focus = NULL;
	state->old_label = g_strdup (swc->label);
	state->gui = gnm_glade_xml_new (COMMAND_CONTEXT (wbcg),
		"so-checkbox.glade", NULL, NULL);
	state->dialog = glade_xml_get_widget (state->gui, "SO-Checkbox");

 	table = glade_xml_get_widget (state->gui, "table");

	state->expression = gnumeric_expr_entry_new (wbcg, TRUE);
	gnm_expr_entry_set_flags (state->expression,
		GNM_EE_ABS_ROW | GNM_EE_ABS_COL | GNM_EE_SHEET_OPTIONAL | GNM_EE_SINGLE_RANGE,
		GNM_EE_MASK);
	gnm_expr_entry_set_scg (state->expression, SHEET_CONTROL_GUI (sc));
	gnm_expr_entry_load_from_dep (state->expression, &swc->dep);
	gtk_table_attach (GTK_TABLE (table), GTK_WIDGET (state->expression),
			  1, 2, 0, 1,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
	gtk_widget_show (GTK_WIDGET (state->expression));


 	state->label = glade_xml_get_widget (state->gui, "label_entry");

 	gtk_entry_set_text (GTK_ENTRY (state->label), swc->label);

 	g_signal_connect (G_OBJECT (state->label),
		"changed",
		G_CALLBACK (cb_checkbox_label_changed), state);
	g_signal_connect (G_OBJECT (state->dialog),
		"destroy",
		G_CALLBACK (cb_checkbox_config_destroy), state);
	g_signal_connect (G_OBJECT (glade_xml_get_widget (state->gui, "ok_button")),
		"clicked",
		G_CALLBACK (cb_checkbox_config_ok_clicked), state);
	g_signal_connect (G_OBJECT (glade_xml_get_widget (state->gui, "cancel_button")),
		"clicked",
		G_CALLBACK (cb_checkbox_config_cancel_clicked), state);
	gnumeric_init_help_button (
		glade_xml_get_widget (state->gui, "help_button"),
		"so-checkbox.html");

	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       SHEET_OBJECT_CONFIG_KEY);

	wbcg_edit_attach_guru (state->wbcg, state->dialog);
	/* Note:  half of the set-focus action is handle by the default */
	/*        callback installed by wbcg_edit_attach_guru           */
	g_signal_connect (G_OBJECT (state->dialog),
		"set-focus",
		G_CALLBACK (cb_checkbox_set_focus), state);

	gtk_widget_show (state->dialog);
}

static gboolean
sheet_widget_checkbox_set_sheet (SheetObject *so, Sheet *sheet)
{
	SheetWidgetCheckbox *swc = SHEET_WIDGET_CHECKBOX (so);

	dependent_set_sheet (&swc->dep, sheet);
	sheet_widget_checkbox_set_active (swc);

	return FALSE;
}

static gboolean
sheet_widget_checkbox_clear_sheet (SheetObject *so)
{
	SheetWidgetCheckbox *swc = SHEET_WIDGET_CHECKBOX (so);

	if (dependent_is_linked (&swc->dep))
		dependent_unlink (&swc->dep, NULL);
	swc->dep.sheet = NULL;
	return FALSE;
}

static gboolean
sheet_widget_checkbox_write_xml (SheetObject const *so,
				 XmlParseContext const *context,
				 xmlNodePtr tree)
{
	SheetWidgetCheckbox *swc = SHEET_WIDGET_CHECKBOX (so);

	xml_node_set_cstr (tree, "Label", swc->label);
	xml_node_set_int  (tree, "Value", swc->value);
	if (swc->dep.expression != NULL) {
		ParsePos pos;
		char *val = gnm_expr_as_string (swc->dep.expression,
			parse_pos_init (&pos, NULL, so->sheet, 0, 0),
			gnm_expr_conventions_default);
		xml_node_set_cstr (tree, "Input", val);
	}

	return FALSE;
}

static gboolean
sheet_widget_checkbox_read_xml (SheetObject *so,
				XmlParseContext const *context,
				xmlNodePtr tree)
{
	SheetWidgetCheckbox *swc = SHEET_WIDGET_CHECKBOX (so);
	gchar *label = (gchar *)xmlGetProp (tree, (xmlChar *)"Label");
	gchar *input_txt;

	if (!label) {
		g_warning ("Could not read a CheckBoxWidget object because it lacks a label property");
		return TRUE;
	}

	swc->dep.sheet = NULL;
	swc->dep.expression = NULL;
	swc->dep.flags = checkbox_get_dep_type ();
	swc->label = g_strdup (label);
	xmlFree (label);

	input_txt = (gchar *)xmlGetProp (tree, (xmlChar *)"Input");
	if (input_txt != NULL && *input_txt != '\0') {
		ParsePos pos;
		GnmExpr const *expr = gnm_expr_parse_str_simple (input_txt,
			parse_pos_init (&pos, NULL, context->sheet, 0, 0));

		if (expr == NULL) {
			g_warning ("Could not read checkbox widget object. Could not parse expr.");
			xmlFree (input_txt);
			return TRUE;
		}

		swc->dep.expression = expr;

		xmlFree (input_txt);
	}

	xml_node_get_int (tree, "Value", &swc->value);

	return FALSE;
}

void
sheet_widget_checkbox_set_link (SheetObject *so, GnmExpr const *expr)
{
	SheetWidgetCheckbox *swc = SHEET_WIDGET_CHECKBOX (so);
	g_return_if_fail (swc != NULL);
	dependent_set_expr (&swc->dep, expr);
}

void
sheet_widget_checkbox_set_label	(SheetObject *so, char const *str)
{
	GList *list;
	SheetWidgetCheckbox *swc = SHEET_WIDGET_CHECKBOX (so);

	g_free (swc->label);
	swc->label = g_strdup (str);

 	list = swc->sow.parent_object.realized_list;
 	for (; list != NULL; list = list->next) {
 		FooCanvasWidget *item = FOO_CANVAS_WIDGET (list->data);
 		gtk_button_set_label (GTK_BUTTON (item->widget), swc->label);
 	}
}

SOW_MAKE_TYPE (checkbox, Checkbox,
	       &sheet_widget_checkbox_user_config,
	       &sheet_widget_checkbox_set_sheet,
	       &sheet_widget_checkbox_clear_sheet,
	       &sheet_widget_checkbox_clone,
	       &sheet_widget_checkbox_write_xml,
	       &sheet_widget_checkbox_read_xml)

/****************************************************************************/
#define SHEET_WIDGET_RADIO_BUTTON_TYPE	(sheet_widget_radio_button_get_type ())
#define SHEET_WIDGET_RADIO_BUTTON(obj)	(G_TYPE_CHECK_INSTANCE_CAST((obj), SHEET_WIDGET_RADIO_BUTTON_TYPE, SheetWidgetRadioButton))
#define DEP_TO_RADIO_BUTTON(d_ptr)	(SheetWidgetRadioButton *)(((char *)d_ptr) - G_STRUCT_OFFSET(SheetWidgetRadioButton, dep))

typedef struct {
	SheetObjectWidget	sow;

	gboolean	 being_updated;
	char		*label;
	Dependent	 dep;
} SheetWidgetRadioButton;
typedef struct {
	SheetObjectWidgetClass	sow;
} SheetWidgetRadioButtonClass;

static void
radio_button_eval (Dependent *dep)
{
	Value *v;
	EvalPos pos;
	gboolean err;
	int result;

	v = gnm_expr_eval (dep->expression, eval_pos_init_dep (&pos, dep),
			   GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
	result = value_get_as_int (v);
	value_release (v);
	if (!err) {
		/* FIXME : finish this when I have a better idea of a group */
		/* SheetWidgetRadioButton *swrb = DEP_TO_RADIO_BUTTON (dep); */
	}
}

static void
radio_button_debug_name (Dependent const *dep, FILE *out)
{
	fprintf (out, "RadioButton%p", dep);
}

static DEPENDENT_MAKE_TYPE (radio_button, NULL)

static void
sheet_widget_radio_button_init (SheetObjectWidget *sow)
{
	SheetWidgetRadioButton *swrb = SHEET_WIDGET_RADIO_BUTTON (sow);

	swrb->being_updated = FALSE;
	swrb->label = g_strdup (_("RadioButton"));

	swrb->dep.sheet = NULL;
	swrb->dep.flags = radio_button_get_dep_type ();
	swrb->dep.expression = NULL;
}

static void
sheet_widget_radio_button_finalize (GObject *obj)
{
	SheetWidgetRadioButton *swrb = SHEET_WIDGET_RADIO_BUTTON (obj);

	if (swrb->label != NULL) {
		g_free (swrb->label);
		swrb->label = NULL;
	}

	dependent_set_expr (&swrb->dep, NULL);
	(*sheet_object_widget_class->finalize) (obj);
}

static void
sheet_widget_radio_button_toggled (GtkToggleButton *button,
				   SheetWidgetRadioButton *swrb)
{
	if (swrb->being_updated || !gtk_toggle_button_get_active (button))
		return;
#if 0
	swrb->value = gtk_toggle_button_get_active (button);
	sheet_widget_checkbox_set_active (swrb);
#endif
}

static GtkWidget *
sheet_widget_radio_button_create_widget (SheetObjectWidget *sow, SheetControlGUI *sview)
{
	GtkWidget *w = gtk_radio_button_new_with_label (NULL, "RadioButton");
	g_signal_connect (G_OBJECT (w),
		"toggled",
		G_CALLBACK (sheet_widget_radio_button_toggled), sow);
	return w;
}

static gboolean
sheet_widget_radio_button_set_sheet (SheetObject *so, Sheet *sheet)
{
	SheetWidgetRadioButton *swrb = SHEET_WIDGET_RADIO_BUTTON (so);

	dependent_set_sheet (&swrb->dep, sheet);

	return FALSE;
}

static gboolean
sheet_widget_radio_button_clear_sheet (SheetObject *so)
{
	SheetWidgetRadioButton *swrb = SHEET_WIDGET_RADIO_BUTTON (so);

	g_return_val_if_fail (swrb != NULL, TRUE);

	if (dependent_is_linked (&swrb->dep))
		dependent_unlink (&swrb->dep, NULL);
	swrb->dep.sheet = NULL;
	return FALSE;
}

void
sheet_widget_radio_button_set_label (SheetObject *so, char const *str)
{
	GList *list;
	SheetWidgetRadioButton *swrb = SHEET_WIDGET_RADIO_BUTTON (so);

	g_free (swrb->label);
	swrb->label = g_strdup (str);

 	list = swrb->sow.parent_object.realized_list;
 	for (; list != NULL; list = list->next) {
 		FooCanvasWidget *item = FOO_CANVAS_WIDGET (list->data);
 		gtk_button_set_label (GTK_BUTTON (item->widget), swrb->label);
 	}
}

SOW_MAKE_TYPE (radio_button, RadioButton,
	       NULL,
	       sheet_widget_radio_button_set_sheet,
	       sheet_widget_radio_button_clear_sheet,
	       NULL,
	       NULL,
	       NULL)

/****************************************************************************/
#define SHEET_WIDGET_LIST_TYPE	(sheet_widget_list_get_type ())
#define SHEET_WIDGET_LIST(obj)	(G_TYPE_CHECK_INSTANCE_CAST((obj), SHEET_WIDGET_LIST_TYPE, SheetWidgetList))
#define DEP_TO_LIST(d_ptr)	(SheetWidgetList *)(((char *)d_ptr) - G_STRUCT_OFFSET(SheetWidgetList, dep))

typedef struct {
	SheetObjectWidget	sow;

	gboolean	being_updated;
	Dependent	dep;
} SheetWidgetList;
typedef struct {
	SheetObjectWidgetClass	sow;
} SheetWidgetListClass;

static void
list_eval (Dependent *dep)
{
	Value *v;
	EvalPos pos;
	gboolean err;
	int result;

	v = gnm_expr_eval (dep->expression, eval_pos_init_dep (&pos, dep),
			   GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
	result = value_get_as_int (v);
	value_release (v);
	if (!err) {
		/* SheetWidgetList *swl = DEP_TO_LIST (dep); */
	}
}

static void
list_debug_name (Dependent const *dep, FILE *out)
{
	fprintf (out, "List%p", dep);
}

static DEPENDENT_MAKE_TYPE (list, NULL)

static void
sheet_widget_list_init (SheetObjectWidget *sow)
{
	SheetWidgetList *swl = SHEET_WIDGET_LIST (sow);

	swl->being_updated = FALSE;

	swl->dep.sheet = NULL;
	swl->dep.flags = list_get_dep_type ();
	swl->dep.expression = NULL;
}

static void
sheet_widget_list_finalize (GObject *obj)
{
	SheetWidgetList *swl = SHEET_WIDGET_LIST (obj);
	dependent_set_expr (&swl->dep, NULL);
	(*sheet_object_widget_class->finalize) (obj);
}

static GtkWidget *
sheet_widget_list_create_widget (SheetObjectWidget *sow, SheetControlGUI *sview)
{
	return gtk_list_new ();
}

static gboolean
sheet_widget_list_set_sheet (SheetObject *so, Sheet *sheet)
{
	SheetWidgetList *swl = SHEET_WIDGET_LIST (so);

	dependent_set_sheet (&swl->dep, sheet);

	return FALSE;
}

static gboolean
sheet_widget_list_clear_sheet (SheetObject *so)
{
	SheetWidgetList *swl = SHEET_WIDGET_LIST (so);

	g_return_val_if_fail (swl != NULL, TRUE);

	if (dependent_is_linked (&swl->dep))
		dependent_unlink (&swl->dep, NULL);
	swl->dep.sheet = NULL;
	return FALSE;
}

SOW_MAKE_TYPE (list, List,
	       NULL,
	       sheet_widget_list_set_sheet,
	       sheet_widget_list_clear_sheet,
	       NULL,
	       NULL,
	       NULL)

/****************************************************************************/
#define SHEET_WIDGET_COMBO_TYPE     (sheet_widget_combo_get_type ())
#define SHEET_WIDGET_COMBO(obj)     (G_TYPE_CHECK_INSTANCE_CAST((obj), SHEET_WIDGET_COMBO_TYPE, SheetWidgetCombo))
#define DEP_TO_COMBO_INPUT(d_ptr)	(SheetWidgetCombo *)(((char *)d_ptr) - G_STRUCT_OFFSET(SheetWidgetCombo, input_dep))
#define DEP_TO_COMBO_OUTPUT(d_ptr)	(SheetWidgetCombo *)(((char *)d_ptr) - G_STRUCT_OFFSET(SheetWidgetCombo, output_dep))

typedef struct {
	SheetObjectWidget	sow;

	gboolean	being_updated;
	Dependent	input_dep, output_dep;
} SheetWidgetCombo;
typedef struct {
	SheetObjectWidgetClass	sow;
} SheetWidgetComboClass;

/*-----------*/
static void
combo_input_eval (Dependent *dep)
{
	Value *v;
	EvalPos pos;
	gboolean err;
	int result;

	v = gnm_expr_eval (dep->expression, eval_pos_init_dep (&pos, dep),
			   GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
	result = value_get_as_int (v);
	value_release (v);
	if (!err) {
		/* SheetWidgetCombo *swc = DEP_TO_COMBO_INPUT (dep); */
	}
}

static void
combo_input_debug_name (Dependent const *dep, FILE *out)
{
	fprintf (out, "ComboInput%p", dep);
}

static DEPENDENT_MAKE_TYPE (combo_input, NULL)

/*-----------*/
static void
combo_output_eval (Dependent *dep)
{
	Value *v;
	EvalPos pos;
	gboolean err;
	int result;

	v = gnm_expr_eval (dep->expression, eval_pos_init_dep (&pos, dep),
			   GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
	result = value_get_as_int (v);
	value_release (v);
	if (!err) {
		/* SheetWidgetCombo *swc = DEP_TO_COMBO_OUTPUT (dep); */
	}
}

static void
combo_output_debug_name (Dependent const *dep, FILE *out)
{
	fprintf (out, "ComboOutput%p", dep);
}

static DEPENDENT_MAKE_TYPE (combo_output, NULL)

/*-----------*/

static void
sheet_widget_combo_init (SheetObjectWidget *sow)
{
	SheetWidgetCombo *swc = SHEET_WIDGET_COMBO (sow);

	swc->being_updated = FALSE;

	swc->input_dep.sheet = NULL;
	swc->input_dep.flags = combo_input_get_dep_type ();
	swc->input_dep.expression = NULL;

	swc->output_dep.sheet = NULL;
	swc->output_dep.flags = combo_output_get_dep_type ();
	swc->output_dep.expression = NULL;
}

static void
sheet_widget_combo_finalize (GObject *obj)
{
	SheetWidgetCombo *swc = SHEET_WIDGET_COMBO (obj);
	dependent_set_expr (&swc->input_dep, NULL);
	dependent_set_expr (&swc->output_dep, NULL);
	(*sheet_object_widget_class->finalize) (obj);
}

static GtkWidget *
sheet_widget_combo_create_widget (SheetObjectWidget *sow, SheetControlGUI *sview)
{
	GtkWidget *w = gnm_combo_text_new (NULL);
	return w;
}

static gboolean
sheet_widget_combo_set_sheet (SheetObject *so, Sheet *sheet)
{
	SheetWidgetCombo *swc = SHEET_WIDGET_COMBO (so);

	g_return_val_if_fail (swc != NULL, TRUE);
	g_return_val_if_fail (swc->input_dep.sheet == NULL, TRUE);
	g_return_val_if_fail (swc->output_dep.sheet == NULL, TRUE);

	dependent_set_sheet (&swc->input_dep, sheet);
	dependent_set_sheet (&swc->output_dep, sheet);

	return FALSE;
}

static gboolean
sheet_widget_combo_clear_sheet (SheetObject *so)
{
	SheetWidgetCombo *swc = SHEET_WIDGET_COMBO (so);

	g_return_val_if_fail (swc != NULL, TRUE);

	if (dependent_is_linked (&swc->input_dep))
		dependent_unlink (&swc->input_dep, NULL);
	if (dependent_is_linked (&swc->output_dep))
		dependent_unlink (&swc->output_dep, NULL);
	swc->input_dep.sheet = swc->output_dep.sheet = NULL;
	return FALSE;
}

SOW_MAKE_TYPE (combo, Combo,
	       NULL,
	       sheet_widget_combo_set_sheet,
	       sheet_widget_combo_clear_sheet,
	       NULL,
	       NULL,
	       NULL)

/**************************************************************************/

/**
 * sheet_widget_init_clases:
 * @void:
 *
 * Initilize the classes for the sheet-object-widgets. We need to initalize
 * them before we try loading a sheet that might contain sheet-object-widgets
 **/
void
sheet_object_widget_register (void)
{
	SHEET_WIDGET_FRAME_TYPE;
	SHEET_WIDGET_BUTTON_TYPE;
	SHEET_WIDGET_SCROLLBAR_TYPE;
	SHEET_WIDGET_CHECKBOX_TYPE;
	SHEET_WIDGET_RADIO_BUTTON_TYPE;
	SHEET_WIDGET_LIST_TYPE;
	SHEET_WIDGET_COMBO_TYPE;
}
