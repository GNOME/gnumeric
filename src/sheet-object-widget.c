/* vim: set sw=8: */

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
#include "sheet-object-widget.h"
#include "expr.h"
#include "value.h"
#include "selection.h"
#include "workbook-edit.h"

#define SHEET_OBJECT_WIDGET_TYPE     (sheet_object_widget_get_type ())
#define SHEET_OBJECT_WIDGET(obj)     (GTK_CHECK_CAST((obj), SHEET_OBJECT_WIDGET_TYPE, SheetObjectWidget))
#define SHEET_OBJECT_WIDGET_CLASS(k) (GTK_CHECK_CLASS_CAST ((k), SHEET_OBJECT_WIDGET_TYPE, SheetObjectWidgetClass))
#define IS_SHEET_WIDGET_OBJECT(o)    (GTK_CHECK_TYPE((o), SHEET_OBJECT_WIDGET_TYPE))
#define SOW_CLASS(so)	 	     (SHEET_OBJECT_WIDGET_CLASS (GTK_OBJECT(so)->klass))

#define SOW_MAKE_TYPE(n1, n2, config) \
static void \
sheet_widget_ ## n1 ## _class_init (GtkObjectClass *object_class) \
{ \
	SheetObjectWidgetClass *sow_class = SHEET_OBJECT_WIDGET_CLASS (object_class); \
	SheetObjectClass *so_class = SHEET_OBJECT_CLASS (object_class); \
	sow_class->create_widget = & sheet_widget_ ## n1 ## _create_widget; \
	so_class->user_config = config; \
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
	GtkWidget *(*create_widget)(SheetObjectWidget *, SheetView *);
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

static GnomeCanvasItem *
sheet_object_widget_new_view (SheetObject *so, SheetView *sheet_view)
{
	GnomeCanvasItem *item;
	GtkWidget *view_widget;
	double x1, x2, y1, y2;

	sheet_object_get_bounds (so, &x1, &y1, &x2, &y2);

	view_widget = SOW_CLASS(so)->create_widget (SHEET_OBJECT_WIDGET (so),
						    sheet_view);

	item = gnome_canvas_item_new (
		sheet_view->object_group,
		gnome_canvas_widget_get_type (),
		"widget", view_widget,
		"x",      x1,
		"y",      y1,
		"width",  x2 - x1 + 1.,
		"height", y2 - y1 + 1.,
		"size_pixels", FALSE,
		NULL);

	sheet_object_widget_handle (so, view_widget, item);
	gtk_widget_show_all (view_widget);

	return item;
}

/*
 * This implemenationt moves the widget rather than
 * destroying/updating/creating the views
 */
static void
sheet_object_widget_update_bounds (SheetObject *so)
{
	GList  *l;
	double x1, y1, x2, y2;

	sheet_object_get_bounds (so, &x1, &y1, &x2, &y2);

	for (l = so->realized_list; l; l = l->next){
		GnomeCanvasItem *item = l->data;

		gnome_canvas_item_set (
			item,
			"x",      x1,
			"y",      y1,
			"width",  x2 - x1 + 1.,
			"height", y2 - y1 + 1.,
			NULL);
	}
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
	SheetObject *so;

	so = SHEET_OBJECT (sow);

	sheet_object_construct  (so, sheet);
	so->type = SHEET_OBJECT_ACTION_CAN_PRESS;
	sheet_object_set_bounds (so, 0, 0, 30, 30);
}

static GNUMERIC_MAKE_TYPE (sheet_object_widget,
			   "SheetObjectWidget", SheetObjectWidget,
			   &sheet_object_widget_class_init, NULL,
			   sheet_object_get_type ())

/**
 * sheet_object_widget_event:
 * @widget: The widget it happens on
 * @event:  The event.
 * @item:   The canvas item.
 *
 *  This handles an event on the object stored in the "sheet_object" data on
 *  the canvas item, it passes the event if button 3 is pressed to the standard
 *  sheet-object handler otherwise it passes it on.
 *
 * Return value: event handled
 */
static int
sheet_object_widget_event (GtkWidget *widget, GdkEvent *event,
			   GnomeCanvasItem *item)
{
	if (event->type == GDK_BUTTON_PRESS && event->button.button == 3) {
		SheetObject *so =
			gtk_object_get_data (GTK_OBJECT (item), "sheet_object");

		g_return_val_if_fail (so != NULL, FALSE);

		return sheet_object_canvas_event (item, event, so);
	}

	return FALSE;
}

void
sheet_object_widget_handle (SheetObject *so, GtkWidget *widget,
			    GnomeCanvasItem *item)
{
	gtk_object_set_data (GTK_OBJECT (item), "sheet_object", so);
	gtk_signal_connect  (GTK_OBJECT (widget), "event",
			     GTK_SIGNAL_FUNC (sheet_object_widget_event),
			     item);
}

/****************************************************************************/
static GtkType sheet_widget_label_get_type (void);
#define SHEET_WIDGET_LABEL_TYPE     (sheet_widget_label_get_type ())
#define SHEET_WIDGET_LABEL(obj)     (GTK_CHECK_CAST((obj), SHEET_WIDGET_LABEL_TYPE, SheetWidgetLabel))
typedef struct {
	SheetObjectWidget	sow;
} SheetWidgetLabel;
typedef struct {
	SheetObjectWidgetClass	sow;
} SheetWidgetLabelClass;

static void
sheet_widget_label_construct (SheetObjectWidget *sow, Sheet *sheet)
{
}

static void
sheet_widget_label_destroy (GtkObject *obj)
{
	(*sheet_object_widget_class->destroy)(obj);
}

static GtkWidget *
sheet_widget_label_create_widget (SheetObjectWidget *sow, SheetView *sview)
{
	return gtk_label_new ("Label");
}

SOW_MAKE_TYPE(label, Label, NULL)

/****************************************************************************/
static GtkType sheet_widget_frame_get_type (void);
#define SHEET_WIDGET_FRAME_TYPE     (sheet_widget_frame_get_type ())
#define SHEET_WIDGET_FRAME(obj)     (GTK_CHECK_CAST((obj), SHEET_WIDGET_FRAME_TYPE, SheetWidgetFrame))
typedef struct {
	SheetObjectWidget	sow;
} SheetWidgetFrame;
typedef struct {
	SheetObjectWidgetClass	sow;
} SheetWidgetFrameClass;

static void
sheet_widget_frame_construct (SheetObjectWidget *sow, Sheet *sheet)
{
}

static void
sheet_widget_frame_destroy (GtkObject *obj)
{
	(*sheet_object_widget_class->destroy)(obj);
}

static GtkWidget *
sheet_widget_frame_create_widget (SheetObjectWidget *sow, SheetView *sview)
{
	return gtk_frame_new ("Frame");
}

SOW_MAKE_TYPE(frame, Frame, NULL)

/****************************************************************************/
static GtkType sheet_widget_button_get_type (void);
#define SHEET_WIDGET_BUTTON_TYPE     (sheet_widget_button_get_type ())
#define SHEET_WIDGET_BUTTON(obj)     (GTK_CHECK_CAST((obj), SHEET_WIDGET_BUTTON_TYPE, SheetWidgetbutton))
typedef struct {
	SheetObjectWidget	sow;
} SheetWidgetButton;
typedef struct {
	SheetObjectWidgetClass	sow;
} SheetWidgetButtonClass;

static void
sheet_widget_button_construct (SheetObjectWidget *sow, Sheet *sheet)
{
}

static void
sheet_widget_button_destroy (GtkObject *obj)
{
	(*sheet_object_widget_class->destroy)(obj);
}

static GtkWidget *
sheet_widget_button_create_widget (SheetObjectWidget *sow, SheetView *sview)
{
	return gtk_button_new_with_label (_("Button"));
}

SOW_MAKE_TYPE(button, Button, NULL)

/****************************************************************************/
static GtkType sheet_widget_checkbox_get_type (void);
#define SHEET_WIDGET_CHECKBOX_TYPE     (sheet_widget_checkbox_get_type ())
#define SHEET_WIDGET_CHECKBOX(obj)     (GTK_CHECK_CAST((obj), SHEET_WIDGET_CHECKBOX_TYPE, SheetWidgetCheckbox))
#define DEP_TO_CHECKBOX(d_ptr)	(SheetWidgetCheckbox *)(((void *)d_ptr) - GTK_STRUCT_OFFSET(SheetWidgetCheckbox, dep))

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
checkbox_get_dep_type ()
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
sheet_widget_checkbox_construct (SheetObjectWidget *sow, Sheet *sheet)
{
	static int counter = 0;
	SheetWidgetCheckbox *swc = SHEET_WIDGET_CHECKBOX (sow);
	Range const * range = selection_first_range (sheet, TRUE);
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
	dependent_changed (&swc->dep, NULL, TRUE);
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
		Cell *cell = sheet_cell_fetch (swc->sow.parent_object.sheet,
					       var->ref.col, var->ref.row);
		sheet_cell_set_value (cell, value_new_bool (new_val), NULL);
	}
}

static GtkWidget *
sheet_widget_checkbox_create_widget (SheetObjectWidget *sow, SheetView *sview)
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

typedef struct {
	GtkWidget *dialog;
	GtkWidget *entry;

	Workbook  *wb;
	SheetWidgetCheckbox *swc;
} CheckboxConfigState;

static gboolean
cb_checkbox_config_destroy (GtkObject *w, CheckboxConfigState *state)
{
	g_return_val_if_fail (w != NULL, FALSE);
	g_return_val_if_fail (state != NULL, FALSE);

	workbook_edit_detach_guru (state->wb);

	/* Handle window manger closing the dialog.
	 * This will be ignored if we are being destroyed differently.
	 */
	workbook_finish_editing (state->wb, FALSE);

	state->dialog = NULL;

	g_free (state);
	return FALSE;
}

static void
cb_checkbox_config_focus (GtkWidget *w, GdkEventFocus *ev, CheckboxConfigState *state)
{
	workbook_set_entry (state->wb, GTK_ENTRY (state->entry));
	workbook_edit_select_absolute (state->wb);
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
			char *error_string = NULL;
			ExprTree *expr = expr_parse_string (text,
				parse_pos_init (&pp, NULL, so->sheet, 0, 0),
				NULL, &error_string);

			/* FIXME : Should we be more verbose about errors */
			if (expr != NULL && expr->any.oper == OPER_VAR)
				checkbox_set_expr (&state->swc->dep, expr);
		} else
			checkbox_set_expr (&state->swc->dep, NULL);
	}
	workbook_finish_editing (state->wb, FALSE);
}

static void
sheet_widget_checkbox_user_config (SheetObject *so)
{
	CheckboxConfigState *state;
	SheetWidgetCheckbox *swc = SHEET_WIDGET_CHECKBOX (so);

	g_return_if_fail (swc != NULL);

	state = g_new (CheckboxConfigState, 1);
	state->swc = swc;
	state->wb = so->sheet->workbook;
	state->dialog = gnome_dialog_new (_("Checkbox Configure"),
					  GNOME_STOCK_BUTTON_OK, 
					  GNOME_STOCK_BUTTON_CANCEL, 
					  NULL);
	state->entry = gtk_entry_new ();
	if (swc->dep.expression != NULL) {
		ParsePos pp;
		char *text = expr_tree_as_string (swc->dep.expression,
			parse_pos_init (&pp, NULL, so->sheet, 0, 0));
		gtk_entry_set_text (GTK_ENTRY (state->entry), text);
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

	gnumeric_non_modal_dialog (state->wb, GTK_WINDOW (state->dialog));
	workbook_edit_attach_guru (state->wb, state->dialog);
	gtk_window_set_position (GTK_WINDOW (state->dialog), GTK_WIN_POS_MOUSE);
	gtk_window_set_focus (GTK_WINDOW (state->dialog),
			      GTK_WIDGET (state->entry));
	gtk_widget_show_all (state->dialog);
}

SOW_MAKE_TYPE(checkbox, Checkbox, &sheet_widget_checkbox_user_config)

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
sheet_widget_radio_button_create_widget (SheetObjectWidget *sow, SheetView *sview)
{
	return gtk_radio_button_new_with_label (NULL, "RadioButton");
}

SOW_MAKE_TYPE(radio_button, RadioButton, NULL)

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
sheet_widget_list_create_widget (SheetObjectWidget *sow, SheetView *sview)
{
    return gtk_list_new ();
}

SOW_MAKE_TYPE(list, List, NULL)

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
sheet_widget_combo_create_widget (SheetObjectWidget *sow, SheetView *sview)
{
	return gtk_combo_new ();
}

SOW_MAKE_TYPE(combo, Combo, NULL)
