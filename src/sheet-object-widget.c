/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * sheet-object-widget.c: SheetObject wrappers for simple gtk widgets.
 *
 * Copyright (C) 2000 Jody Goldberg (jgoldberg@home.com)
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
#include <config.h>
#include <gnome.h>
#include <libgnorba/gnorba.h>
#include <gdk/gdkkeysyms.h>
#include <math.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "gnumeric-type-util.h"
#include "dependent.h"
#include "sheet-control-gui.h"
#include "sheet-object-widget.h"
#include "sheet-object-impl.h"
#include "expr.h"
#include "value.h"
#include "ranges.h"
#include "selection.h"
#include "workbook-edit.h"
#include "workbook.h"
#include "gnumeric-expr-entry.h"
#include "dialogs.h"

#define SHEET_OBJECT_CONFIG_KEY "sheet-object-config-dialog"

#define SHEET_OBJECT_WIDGET_TYPE     (sheet_object_widget_get_type ())
#define SHEET_OBJECT_WIDGET(obj)     (GTK_CHECK_CAST((obj), SHEET_OBJECT_WIDGET_TYPE, SheetObjectWidget))
#define SHEET_OBJECT_WIDGET_CLASS(k) (GTK_CHECK_CLASS_CAST ((k), SHEET_OBJECT_WIDGET_TYPE, SheetObjectWidgetClass))
#define IS_SHEET_WIDGET_OBJECT(o)    (GTK_CHECK_TYPE((o), SHEET_OBJECT_WIDGET_TYPE))
#define SOW_CLASS(so)	 	     (SHEET_OBJECT_WIDGET_CLASS (GTK_OBJECT(so)->klass))

#define SOW_MAKE_TYPE(n1, n2, fn_config, fn_set_sheet, fn_clear_sheet,\
		      fn_clone, fn_write_xml, fn_read_xml) \
static void \
sheet_widget_ ## n1 ## _class_init (GtkObjectClass *object_class) \
{ \
	SheetObjectWidgetClass *sow_class = SHEET_OBJECT_WIDGET_CLASS (object_class); \
	SheetObjectClass *so_class = SHEET_OBJECT_CLASS (object_class); \
	sow_class->create_widget = & sheet_widget_ ## n1 ## _create_widget; \
	so_class->user_config = fn_config; \
	so_class->assign_to_sheet = fn_set_sheet; \
	so_class->remove_from_sheet = fn_clear_sheet; \
	so_class->clone = fn_clone; \
	so_class->write_xml = fn_write_xml; \
	so_class->read_xml = fn_read_xml; \
	object_class->destroy = & sheet_widget_ ## n1 ## _destroy; \
} \
static GNUMERIC_MAKE_TYPE(sheet_widget_ ## n1, "SheetWidget" #n2, SheetWidget ## n2, \
			  &sheet_widget_ ## n1 ## _class_init, \
			  NULL, sheet_object_widget_get_type ()) \
SheetObject * \
sheet_widget_ ## n1 ## _new(Sheet *sheet) \
{ \
	SheetObjectWidget *sow; \
\
	sow = gtk_type_new (sheet_widget_ ## n1 ## _get_type ()); \
\
	sheet_object_widget_construct (sow, sheet); \
	sheet_widget_ ##n1 ## _construct (sow, sheet); \
\
	return SHEET_OBJECT (sow); \
}

typedef struct _SheetObjectWidget SheetObjectWidget;

struct _SheetObjectWidget {
	SheetObject      parent_object;
};

typedef struct {
	SheetObjectClass parent_class;
	GtkWidget *(*create_widget)(SheetObjectWidget *, SheetControlGUI *);
} SheetObjectWidgetClass;

static SheetObjectClass *sheet_object_widget_parent_class = NULL;
static GtkObjectClass *sheet_object_widget_class = NULL;

static GtkType sheet_object_widget_get_type	(void);

static void
sheet_object_widget_set_active (SheetObject *so, gboolean val)
{
	GList  *l;

	for (l = so->realized_list; l; l = l->next){
	}
}

static GtkObject *
sheet_object_widget_new_view (SheetObject *so, SheetControlGUI *scg)
{
	GnomeCanvasItem *view_item;
	GtkWidget *view_widget =
		SOW_CLASS(so)->create_widget (SHEET_OBJECT_WIDGET (so),
					      scg);

	view_item = gnome_canvas_item_new (
		scg->object_group,
		gnome_canvas_widget_get_type (),
		"widget", view_widget,
		"size_pixels", FALSE,
		NULL);
	scg_object_widget_register (so, view_widget, view_item);
	gtk_widget_show_all (view_widget);

	return GTK_OBJECT (view_item);
}

/*
 * This implemenation moves the widget rather than
 * destroying/updating/creating the views
 */
static void
sheet_object_widget_update_bounds (SheetObject *so, GtkObject *view,
				   SheetControlGUI *scg)
{
	double coords [4];

	/* NOTE : far point is EXCLUDED so we add 1 */
 	scg_object_view_position (scg, so, coords);
	gnome_canvas_item_set (GNOME_CANVAS_ITEM (view),
		"x", coords [0], "y", coords [1],
		"width",  coords [2] - coords [0] + 1.,
		"height", coords [3] - coords [1] + 1.,
		NULL);
}

static void
sheet_object_widget_class_init (GtkObjectClass *object_class)
{
	SheetObjectClass *so_class = SHEET_OBJECT_CLASS (object_class);
	SheetObjectWidgetClass *sow_class = SHEET_OBJECT_WIDGET_CLASS (object_class);

	sheet_object_widget_class = GTK_OBJECT_CLASS (object_class);
	sheet_object_widget_parent_class = gtk_type_class (sheet_object_get_type ());

	/* SheetObject class method overrides */
	so_class->new_view = sheet_object_widget_new_view;
	so_class->update_bounds = sheet_object_widget_update_bounds;
	so_class->set_active  = sheet_object_widget_set_active;

	sow_class->create_widget = NULL;
}

static void
sheet_object_widget_construct (SheetObjectWidget *sow, Sheet *sheet)
{
	SheetObject *so = SHEET_OBJECT (sow);
	so->type = SHEET_OBJECT_ACTION_CAN_PRESS;
}

/**
 * sheet_object_widget_clone:
 * @so: The source object for that we are going to use as cloning source
 * @sheet: The destination sheet of the cloned object
 * 
 * The common part of the clone rutine of all the objects
 * 
 * Return Value: a newly created SheetObjectWidget
 **/
static SheetObjectWidget *
sheet_object_widget_clone (SheetObject const *so, Sheet *sheet)
{
	SheetObjectWidget *sow = SHEET_OBJECT_WIDGET (so);
	SheetObjectWidget *new_sow;
	
	new_sow = SHEET_OBJECT_WIDGET (gtk_type_new (GTK_OBJECT_TYPE (sow)));

	sheet_object_widget_construct (new_sow, sheet);

	return new_sow;
}

static GNUMERIC_MAKE_TYPE (sheet_object_widget,
			   "SheetObjectWidget", SheetObjectWidget,
			   &sheet_object_widget_class_init, NULL,
			   sheet_object_get_type ())

/****************************************************************************/
static GtkType sheet_widget_label_get_type (void);
#define SHEET_WIDGET_LABEL_TYPE     (sheet_widget_label_get_type ())
#define SHEET_WIDGET_LABEL(obj)     (GTK_CHECK_CAST((obj), SHEET_WIDGET_LABEL_TYPE, SheetWidgetLabel))
typedef struct {
	SheetObjectWidget	sow;
	char *label;
} SheetWidgetLabel;
typedef struct {
	SheetObjectWidgetClass	sow;
} SheetWidgetLabelClass;

static void
sheet_widget_label_construct_with_label (SheetObjectWidget *sow, const char *text)
{
	SheetWidgetLabel *swl = SHEET_WIDGET_LABEL (sow);
	
	swl->label = g_strdup (text);
}

static void
sheet_widget_label_construct (SheetObjectWidget *sow, Sheet *sheet)
{
	sheet_widget_label_construct_with_label (sow, _("Label"));
}

static void
sheet_widget_label_destroy (GtkObject *obj)
{
	SheetWidgetLabel *swl = SHEET_WIDGET_LABEL (obj);

	g_free (swl->label);
	swl->label = NULL;
	
	(*sheet_object_widget_class->destroy)(obj);
}

static GtkWidget *
sheet_widget_label_create_widget (SheetObjectWidget *sow, SheetControlGUI *sview)
{
	SheetWidgetLabel *swl = SHEET_WIDGET_LABEL (sow);

	return gtk_label_new (swl->label);
}

static SheetObject *
sheet_widget_label_clone (SheetObject const *so, Sheet *new_sheet)
{
	SheetObjectWidget *new_sow;
	
	new_sow = sheet_object_widget_clone (so, new_sheet);

	sheet_widget_label_construct_with_label (new_sow, SHEET_WIDGET_LABEL (so)->label);
	   
	return SHEET_OBJECT (new_sow);
}

static gboolean
sheet_widget_label_write_xml (SheetObject const *so,
			      XmlParseContext const *context,
			      xmlNodePtr tree)
{
	SheetWidgetLabel *swl = SHEET_WIDGET_LABEL (so);

	xml_set_value_cstr (tree, "Label", swl->label);

	return FALSE;
}

static gboolean
sheet_widget_label_read_xml (SheetObject *so,
			     XmlParseContext const *context,
			     xmlNodePtr tree)
{
	SheetWidgetLabel *swl = SHEET_WIDGET_LABEL (so);
	char *label = xmlGetProp (tree, "Label");

	if (!label) {
		g_warning ("Could not read a SheetWidgetLabel beacause it lacks a label property\n");
		return TRUE;
	}
		
	swl->label = g_strdup (label);
	xmlFree (label);

	return FALSE;
}


SOW_MAKE_TYPE(label, Label,
	      NULL,
	      NULL,
	      NULL,
	      &sheet_widget_label_clone,
	      &sheet_widget_label_write_xml,
	      &sheet_widget_label_read_xml);

/****************************************************************************/
static GtkType sheet_widget_frame_get_type (void);
#define SHEET_WIDGET_FRAME_TYPE     (sheet_widget_frame_get_type ())
#define SHEET_WIDGET_FRAME(obj)     (GTK_CHECK_CAST((obj), SHEET_WIDGET_FRAME_TYPE, SheetWidgetFrame))
typedef struct {
	SheetObjectWidget	sow;
	char *label;
} SheetWidgetFrame;
typedef struct {
	SheetObjectWidgetClass	sow;
} SheetWidgetFrameClass;

static void
sheet_widget_frame_construct_with_label (SheetObjectWidget *sow, const char *text)
{
	SheetWidgetFrame *swf = SHEET_WIDGET_FRAME (sow);

	swf->label = g_strdup (text);
}

static void
sheet_widget_frame_construct (SheetObjectWidget *sow, Sheet *sheet)
{
	sheet_widget_frame_construct_with_label (sow, _("Frame"));
}

static void
sheet_widget_frame_destroy (GtkObject *obj)
{
	SheetWidgetFrame *swf = SHEET_WIDGET_FRAME (obj);

	g_free (swf->label);
	swf->label = NULL;

	(*sheet_object_widget_class->destroy)(obj);
}

static GtkWidget *
sheet_widget_frame_create_widget (SheetObjectWidget *sow, SheetControlGUI *sview)
{
	SheetWidgetFrame *swf = SHEET_WIDGET_FRAME (sow);
	
	return gtk_frame_new (swf->label);
}

static SheetObject *
sheet_widget_frame_clone (SheetObject const *so, Sheet *new_sheet)
{
	SheetObjectWidget *new_sow;

	new_sow = sheet_object_widget_clone (so, new_sheet);

	sheet_widget_frame_construct_with_label (new_sow,
						 SHEET_WIDGET_FRAME (so)->label);

	return SHEET_OBJECT (new_sow);
}

static gboolean
sheet_widget_frame_write_xml (SheetObject const *so,
			      XmlParseContext const *context,
			      xmlNodePtr tree)
{
	SheetWidgetFrame *swf = SHEET_WIDGET_FRAME (so);

	xml_set_value_cstr (tree, "Label", swf->label);

	return FALSE;
}

static gboolean
sheet_widget_frame_read_xml (SheetObject *so,
			     XmlParseContext const *context,
			     xmlNodePtr tree)
{
	SheetWidgetFrame *swf = SHEET_WIDGET_FRAME (so);
	char *label = xmlGetProp (tree, "Label");

	if (!label) {
		g_warning ("Could not read a SheetWidgetFrame beacause it lacks a label property\n");
		return TRUE;
	}
		
	swf->label = g_strdup (label);
	xmlFree (label);

	return FALSE;
}

SOW_MAKE_TYPE(frame, Frame,
	      NULL,
	      NULL,
	      NULL,
	      &sheet_widget_frame_clone,
	      &sheet_widget_frame_write_xml,
	      &sheet_widget_frame_read_xml);

/****************************************************************************/
static GtkType sheet_widget_button_get_type (void);
#define SHEET_WIDGET_BUTTON_TYPE     (sheet_widget_button_get_type ())
#define SHEET_WIDGET_BUTTON(obj)     (GTK_CHECK_CAST((obj), SHEET_WIDGET_BUTTON_TYPE, SheetWidgetButton))
typedef struct {
	SheetObjectWidget	sow;
	char *label;
} SheetWidgetButton;
typedef struct {
	SheetObjectWidgetClass	sow;
} SheetWidgetButtonClass;

static void
sheet_widget_button_construct_with_label (SheetObjectWidget *sow, const char *text)
{
	SheetWidgetButton *swb = SHEET_WIDGET_BUTTON (sow);

	swb->label = g_strdup (text);
}

static void
sheet_widget_button_construct (SheetObjectWidget *sow, Sheet *sheet)
{
	sheet_widget_button_construct_with_label (sow, _("Button"));
}

static void
sheet_widget_button_destroy (GtkObject *obj)
{
	SheetWidgetButton *swb = SHEET_WIDGET_BUTTON (obj);

	g_free (swb->label);
	swb->label = NULL;
	
	(*sheet_object_widget_class->destroy)(obj);
}

static GtkWidget *
sheet_widget_button_create_widget (SheetObjectWidget *sow, SheetControlGUI *sview)
{
	SheetWidgetButton *swb = SHEET_WIDGET_BUTTON (sow);
	
	return gtk_button_new_with_label (swb->label);
}

static SheetObject *
sheet_widget_button_clone (SheetObject const *so, Sheet *new_sheet)
{
	SheetObjectWidget *new_sow;

	new_sow = sheet_object_widget_clone (so, new_sheet);

	sheet_widget_button_construct_with_label (new_sow, SHEET_WIDGET_BUTTON (so)->label);

	return SHEET_OBJECT (new_sow);
		
}

static gboolean
sheet_widget_button_write_xml (SheetObject const *so,
			       XmlParseContext const *context,
			       xmlNodePtr tree)
{
	SheetWidgetButton *swb = SHEET_WIDGET_BUTTON (so);

	xml_set_value_cstr (tree, "Label", swb->label);

	return FALSE;
}

static gboolean
sheet_widget_button_read_xml (SheetObject *so,
			      XmlParseContext const *context,
			      xmlNodePtr tree)
{
	SheetWidgetButton *swb = SHEET_WIDGET_BUTTON (so);
	char *label = xmlGetProp (tree, "Label");

	if (!label) {
		g_warning ("Could not read a SheetWidgetButton beacause it lacks a label property\n");
		return TRUE;
	}
		
	swb->label = g_strdup (label);
	xmlFree (label);

	return FALSE;
}

SOW_MAKE_TYPE(button, Button,
	      NULL,
	      NULL,
	      NULL,
	      &sheet_widget_button_clone,
	      &sheet_widget_button_write_xml,
      	      &sheet_widget_button_read_xml);

/****************************************************************************/
static GtkType sheet_widget_checkbox_get_type (void);
#define SHEET_WIDGET_CHECKBOX_TYPE     (sheet_widget_checkbox_get_type ())
#define SHEET_WIDGET_CHECKBOX(obj)     (GTK_CHECK_CAST((obj), SHEET_WIDGET_CHECKBOX_TYPE, SheetWidgetCheckbox))
#define DEP_TO_CHECKBOX(d_ptr)	(SheetWidgetCheckbox *)(((char *)d_ptr) - GTK_STRUCT_OFFSET(SheetWidgetCheckbox, dep))

typedef struct {
	SheetObjectWidget	sow;
	char *label;
	gboolean being_updated;

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
		GnomeCanvasWidget *item = GNOME_CANVAS_WIDGET (ptr->data);
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

	pos.sheet = dep->sheet;
	pos.eval.row = pos.eval.col = 0;
	v = eval_expr (&pos, dep->expression, EVAL_STRICT);
	result = value_get_as_bool (v, &err);
	value_release (v);
	if (!err) {
		SheetWidgetCheckbox *swc = DEP_TO_CHECKBOX(dep);

		swc->value = result;
		sheet_widget_checkbox_set_active (swc);
	}
}

static void
checkbox_set_expr (Dependent *dep, ExprTree *expr)
{
	if (expr != NULL)
		expr_tree_ref (expr);
	dependent_unlink (dep, NULL);
	expr_tree_unref (dep->expression);
	dep->expression = expr;
	dependent_changed (dep, NULL, expr != NULL);
}

static void
checkbox_debug_name (Dependent const *dep, FILE *out)
{
	fprintf (out, "Checkbox%p", dep);
}

static guint
checkbox_get_dep_type (void)
{
	static guint32 type = 0;
	if (type == 0) {
		static DependentClass klass;
		klass.eval = &checkbox_eval;
		klass.set_expr = &checkbox_set_expr;
		klass.debug_name = &checkbox_debug_name;
		type = dependent_type_register (&klass);
	}
	return type;
}

static void
sheet_widget_checkbox_construct_with_range (SheetObjectWidget *sow,
					    Sheet *sheet,
					    const Range *range)
{
	static int counter = 0;
	SheetWidgetCheckbox *swc = SHEET_WIDGET_CHECKBOX (sow);
	CellRef ref;

	g_return_if_fail (swc != NULL);

	swc->label = g_strdup_printf ("CheckBox %d", ++counter);
	swc->being_updated = FALSE;
	swc->value = FALSE;
	swc->dep.sheet = sheet;
	swc->dep.flags = checkbox_get_dep_type ();

	/* Default to the top left of the current selection */
	ref.sheet = sheet;
	ref.col = range->start.col;
	ref.row = range->start.row;
	ref.col_relative = ref.row_relative = FALSE;
	swc->dep.expression = expr_tree_new_var (&ref);
}

static void
sheet_widget_checkbox_construct (SheetObjectWidget *sow, Sheet *sheet)
{
	Range const * range = selection_first_range (sheet, TRUE);

	sheet_widget_checkbox_construct_with_range (sow, sheet, range);
}

static void
sheet_widget_checkbox_destroy (GtkObject *obj)
{
	SheetWidgetCheckbox *swc = SHEET_WIDGET_CHECKBOX (obj);

	g_return_if_fail (swc != NULL);

	g_free (swc->label);
	swc->label = NULL;
	
	dependent_unlink (&swc->dep, NULL);
	if (swc->dep.expression != NULL) {
		expr_tree_unref (swc->dep.expression);
		swc->dep.expression = NULL;
	}
	(*sheet_object_widget_class->destroy)(obj);
}

static void
sheet_widget_checkbox_toggled (GtkToggleButton *button,
			       SheetWidgetCheckbox *swc)
{
	if (swc->being_updated)
		return;
	swc->value = gtk_toggle_button_get_active (button);
	sheet_widget_checkbox_set_active (swc);

	if (swc->dep.expression && swc->dep.expression->any.oper == OPER_VAR) {
		gboolean const new_val = gtk_toggle_button_get_active (button);
		ExprVar	const *var = &swc->dep.expression->var;
		Sheet *sheet = swc->sow.parent_object.sheet;
		Cell *cell = sheet_cell_fetch (sheet, var->ref.col, var->ref.row);
		sheet_cell_set_value (cell, value_new_bool (new_val), NULL);
		workbook_recalc (sheet->workbook);
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
	gtk_signal_connect (GTK_OBJECT (button), "toggled",
			    GTK_SIGNAL_FUNC (sheet_widget_checkbox_toggled),
			    swc);

	return button;
}

static void
sheet_widget_checkbox_get_ref (SheetWidgetCheckbox *swc, const CellRef **ref)
{
	const ExprTree *tree;

	g_return_if_fail (swc != NULL);

	/* Get the cell (ref) that this checkbox is pointing to
	 * we are assuming that a checkbox can only have a reference
	 * in the form of : Sheetx!$Col$Row
	 */
	tree = swc->dep.expression;
	*ref = &tree->var.ref;
}

static SheetObject *
sheet_widget_checkbox_clone (SheetObject const *so, Sheet *new_sheet)
{
	SheetObjectWidget *new_sow;
	const CellRef *ref;

	new_sow = sheet_object_widget_clone (so, new_sheet);

	sheet_widget_checkbox_get_ref (SHEET_WIDGET_CHECKBOX (so), &ref);

	if (ref->sheet == so->sheet) {
		Range range;

		range.start.col = range.end.col = ref->col;
		range.start.row = range.end.row = ref->row;

		sheet_widget_checkbox_construct_with_range (new_sow,
							    new_sheet,
							    &range);
	} else {
		/* When the sheet of the object is different than the sheet of it's
		 * input, we point the new object to the same input as the source
		 * checkbox. Chema.
		 */
		
		/* I can't clone this objects yet cause i cant test it
		 * because setting the reference of the checkbox to an object
		 * outside of the current sheet is crashing. We first need to
		 * fix the configuration so that it can point to a cell outisde
		 * the sheet before we can clone. Chema
		 */
		g_warning ("Cloning for objects that point to an outside of sheet reference "
			   "not yet implemented\n");
		gtk_object_unref (GTK_OBJECT (new_sow));
		return NULL;
	}
	
	return SHEET_OBJECT (new_sow);
}

typedef struct {
	GtkWidget *dialog;
	GtkWidget *entry;

	WorkbookControlGUI *wbcg;
	SheetWidgetCheckbox *swc;
} CheckboxConfigState;

static gboolean
cb_checkbox_config_destroy (GtkObject *w, CheckboxConfigState *state)
{
	g_return_val_if_fail (w != NULL, FALSE);
	g_return_val_if_fail (state != NULL, FALSE);

	workbook_edit_detach_guru (state->wbcg);

	/* Handle window manger closing the dialog.
	 * This will be ignored if we are being destroyed differently.
	 */
	workbook_finish_editing (state->wbcg, FALSE);

	state->dialog = NULL;

	g_free (state);
	return FALSE;
}

static void
cb_checkbox_config_focus (GtkWidget *w, GdkEventFocus *ev, CheckboxConfigState *state)
{
	GnumericExprEntry *expr_entry = GNUMERIC_EXPR_ENTRY (state->entry);
	workbook_set_entry (state->wbcg, expr_entry);
	gnumeric_expr_entry_set_absolute (expr_entry);
}

static void
cb_checkbox_config_clicked (GnomeDialog *dialog, gint button_number,
			    CheckboxConfigState *state)
{
	if (button_number == 0) {
		SheetObject *so = SHEET_OBJECT (state->swc);
		char *text = gtk_entry_get_text (GTK_ENTRY (state->entry));

		if (text != NULL && *text) {
			ParsePos pp;
			ExprTree *expr = expr_parse_string (text,
				parse_pos_init (&pp, NULL, so->sheet, 0, 0),
				NULL, NULL);

			/* FIXME : Should we be more verbose about errors */
			if (expr != NULL && expr->any.oper == OPER_VAR)
				checkbox_set_expr (&state->swc->dep, expr);
		} else
			checkbox_set_expr (&state->swc->dep, NULL);
	}
	workbook_finish_editing (state->wbcg, FALSE);
}

static void
sheet_widget_checkbox_user_config (SheetObject *so, SheetControlGUI *scg)
{
	CheckboxConfigState *state;
	SheetWidgetCheckbox *swc = SHEET_WIDGET_CHECKBOX (so);

	g_return_if_fail (swc != NULL);

	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (scg->wbcg,
					     SHEET_OBJECT_CONFIG_KEY))
		return;
	
	state = g_new (CheckboxConfigState, 1);
	state->swc = swc;
	state->wbcg = scg->wbcg;
	state->dialog = gnome_dialog_new (_("Checkbox Configure"),
					  GNOME_STOCK_BUTTON_OK,
					  GNOME_STOCK_BUTTON_CANCEL,
					  NULL);
	state->entry = gnumeric_expr_entry_new ();
	gnumeric_expr_entry_set_scg (GNUMERIC_EXPR_ENTRY (state->entry), scg);
	if (swc->dep.expression != NULL) {
		ParsePos pp;
		char *text = expr_tree_as_string (swc->dep.expression,
			parse_pos_init (&pp, NULL, so->sheet, 0, 0));
		gnumeric_expr_entry_set_rangesel_from_text (
			GNUMERIC_EXPR_ENTRY (state->entry), text);
		g_free (text);
	}

	gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (state->dialog)->vbox),
			    state->entry, TRUE, TRUE, 5);
	gnome_dialog_set_default (GNOME_DIALOG (state->dialog), 0);

	gtk_signal_connect (GTK_OBJECT (state->entry), "focus-in-event",
			    GTK_SIGNAL_FUNC (cb_checkbox_config_focus), state);
	gtk_signal_connect (GTK_OBJECT (state->dialog), "destroy",
			    GTK_SIGNAL_FUNC (cb_checkbox_config_destroy), state);
	gtk_signal_connect (GTK_OBJECT (state->dialog), "clicked",
			    GTK_SIGNAL_FUNC (cb_checkbox_config_clicked), state);
 	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_EDITABLE(state->entry));

	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       SHEET_OBJECT_CONFIG_KEY);

	workbook_edit_attach_guru (state->wbcg, state->dialog);
	gtk_window_set_position (GTK_WINDOW (state->dialog), GTK_WIN_POS_MOUSE);
	gtk_window_set_focus (GTK_WINDOW (state->dialog),
			      GTK_WIDGET (state->entry));
	gtk_widget_show_all (state->dialog);
}

static gboolean
sheet_widget_checkbox_set_sheet (SheetObject *so, Sheet *sheet)
{
	SheetWidgetCheckbox *swc = SHEET_WIDGET_CHECKBOX (so);

	g_return_val_if_fail (swc != NULL, TRUE);

	dependent_changed (&swc->dep, NULL, TRUE);
	
	return FALSE;
}

static gboolean
sheet_widget_checkbox_clear_sheet (SheetObject *so)
{
	SheetWidgetCheckbox *swc = SHEET_WIDGET_CHECKBOX (so);

	g_return_val_if_fail (swc != NULL, TRUE);

	dependent_unlink (&swc->dep, NULL);
	return FALSE;
}

static gboolean
sheet_widget_checkbox_write_xml (SheetObject const *so,
				 XmlParseContext const *context,
				 xmlNodePtr tree)
{
	SheetWidgetCheckbox *swc = SHEET_WIDGET_CHECKBOX (so);
	ParsePos pos, *pp;
	char *val;

	pp = parse_pos_init (&pos, NULL, so->sheet, 0, 0);
	val = expr_tree_as_string (swc->dep.expression, pp);

	xml_set_value_cstr (tree, "Label", swc->label);
	xml_set_value_int  (tree, "Value", swc->value);
	xml_set_value_cstr (tree, "Input", val);
	
	return FALSE;
}

static gboolean
sheet_widget_checkbox_read_xml (SheetObject *so,
				XmlParseContext const *context,
				xmlNodePtr tree)
{
	SheetWidgetCheckbox *swc = SHEET_WIDGET_CHECKBOX (so);
	char *label = xmlGetProp (tree, "Label");
	char *input_txt = xmlGetProp (tree, "Input");

	if (!label) {
		g_warning ("Could not read a CheckBoxWidget object because it lacks a label property");
		return TRUE;
	}
	
	swc->label = g_strdup (label);
	xmlFree (label);
	
	if (input_txt) {
		ParsePos pos, *pp;
		ExprTree *tree;

		pp = parse_pos_init (&pos, NULL, context->sheet, 0, 0);
		tree = expr_parse_string (input_txt, pp, NULL, NULL);

		if (tree == NULL) {
			g_warning ("Could not read checkbox widget object. Could not parse expr tree\n");
			xmlFree (input_txt);
			return TRUE;
		}

		swc->dep.expression = tree;
		
		xmlFree (input_txt);
	} else {
		swc->dep.expression = NULL;
	}
		
	swc->dep.sheet = context->sheet;
	swc->dep.flags = checkbox_get_dep_type ();
		
	xml_get_value_int (tree, "Value", &swc->value);

	return FALSE;
}
		
SOW_MAKE_TYPE(checkbox, Checkbox,
	      &sheet_widget_checkbox_user_config,
	      &sheet_widget_checkbox_set_sheet,
	      &sheet_widget_checkbox_clear_sheet,
	      &sheet_widget_checkbox_clone,
	      &sheet_widget_checkbox_write_xml,
	      &sheet_widget_checkbox_read_xml)

/****************************************************************************/
static GtkType sheet_widget_radio_button_get_type (void);
#define SHEET_WIDGET_RADIO_BUTTON_TYPE     (sheet_widget_radio_button_get_type ())
#define SHEET_WIDGET_RADIO_BUTTON(obj)     (GTK_CHECK_CAST((obj), SHEET_WIDGET_RADIO_BUTTON_TYPE, SheetWidgeRadioButton))
typedef struct {
	SheetObjectWidget	sow;
} SheetWidgetRadioButton;
typedef struct {
	SheetObjectWidgetClass	sow;
} SheetWidgetRadioButtonClass;

static void
sheet_widget_radio_button_construct (SheetObjectWidget *sow, Sheet *sheet)
{
}

static void
sheet_widget_radio_button_destroy (GtkObject *obj)
{
	(*sheet_object_widget_class->destroy)(obj);
}

static GtkWidget *
sheet_widget_radio_button_create_widget (SheetObjectWidget *sow, SheetControlGUI *sview)
{
	return gtk_radio_button_new_with_label (NULL, "RadioButton");
}

SOW_MAKE_TYPE(radio_button, RadioButton,
	      NULL,
	      NULL,
	      NULL,
	      NULL,
	      NULL,
	      NULL)

/****************************************************************************/
static GtkType sheet_widget_list_get_type (void);
#define SHEET_WIDGET_LIST_TYPE     (sheet_widget_list_get_type ())
#define SHEET_WIDGET_LIST(obj)     (GTK_CHECK_CAST((obj), SHEET_WIDGET_LIST_TYPE, SheetWidgeList))
typedef struct {
	SheetObjectWidget	sow;
} SheetWidgetList;
typedef struct {
	SheetObjectWidgetClass	sow;
} SheetWidgetListClass;

static void
sheet_widget_list_construct (SheetObjectWidget *sow, Sheet *sheet)
{
}

static void
sheet_widget_list_destroy (GtkObject *obj)
{
	(*sheet_object_widget_class->destroy)(obj);
}

static GtkWidget *
sheet_widget_list_create_widget (SheetObjectWidget *sow, SheetControlGUI *sview)
{
    return gtk_list_new ();
}

SOW_MAKE_TYPE(list, List,
	      NULL,
	      NULL,
	      NULL,	      
	      NULL,
	      NULL,
	      NULL)

/****************************************************************************/
static GtkType sheet_widget_combo_get_type (void);
#define SHEET_WIDGET_COMBO_TYPE     (sheet_widget_combo_get_type ())
#define SHEET_WIDGET_COMBO(obj)     (GTK_CHECK_CAST((obj), SHEET_WIDGET_COMBO_TYPE, SheetWidgetCombo))
typedef struct {
	SheetObjectWidget	sow;
} SheetWidgetCombo;
typedef struct {
	SheetObjectWidgetClass	sow;
} SheetWidgetComboClass;

static void
sheet_widget_combo_construct (SheetObjectWidget *sow, Sheet *sheet)
{
}

static void
sheet_widget_combo_destroy (GtkObject *obj)
{
	(*sheet_object_widget_class->destroy)(obj);
}

static GtkWidget *
sheet_widget_combo_create_widget (SheetObjectWidget *sow, SheetControlGUI *sview)
{
	return gtk_combo_new ();
}

SOW_MAKE_TYPE(combo, Combo,
	      NULL,
	      NULL,
	      NULL,
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
	SHEET_WIDGET_LABEL_TYPE;
	SHEET_WIDGET_BUTTON_TYPE;
	SHEET_WIDGET_CHECKBOX_TYPE;
	SHEET_WIDGET_RADIO_BUTTON_TYPE;
	SHEET_WIDGET_LIST_TYPE;
	SHEET_WIDGET_FRAME_TYPE;
	SHEET_WIDGET_COMBO_TYPE;
}
