/*
 * workbook-view.c: View functions for the workbook
 *
 * Authors:
 *   Jody Goldberg
 */
#include <config.h>
#include "workbook-control-priv.h"
#include "workbook-control.h"
#include "workbook-view.h"
#include "workbook.h"
#include "history.h"
#include "workbook-private.h"
#include "gnumeric-util.h"
#include "application.h"
#include "sheet.h"
#include "str.h"
#include "format.h"
#include "expr.h"
#include "value.h"
#include "position.h"
#include "parse-util.h"
#include "gnumeric-type-util.h"

#include <gal/widgets/gtk-combo-stack.h>
#include <locale.h>

/* Persistent attribute ids */
enum {
	ARG_VIEW_HSCROLLBAR = 1,
	ARG_VIEW_VSCROLLBAR,
	ARG_VIEW_TABS
};

/* WorkbookView signals */
enum {
	SHEET_ENTERED,
	LAST_SIGNAL
};

static gint workbook_view_signals [LAST_SIGNAL] = {
	0, /* SHEET_ENTERED */
};

Workbook *
wb_view_workbook (WorkbookView *wbv)
{
	g_return_val_if_fail (IS_WORKBOOK_VIEW (wbv), NULL);
	g_return_val_if_fail (IS_WORKBOOK (wbv->wb), NULL);

	return wbv->wb;
}

Sheet *
wb_view_cur_sheet (WorkbookView *wbv)
{
	g_return_val_if_fail (IS_WORKBOOK_VIEW (wbv), NULL);

	return wbv->current_sheet;
}

void
wb_view_sheet_focus (WorkbookView *wbv, Sheet *sheet)
{
	if (wbv->current_sheet != sheet) {
		Workbook *wb = wb_view_workbook (wbv);

		/* Make sure the sheet has been attached */
		g_return_if_fail (workbook_sheet_index_get (wb, sheet) >= 0);

		WORKBOOK_VIEW_FOREACH_CONTROL (wbv, control,
			wb_control_sheet_focus (control, sheet););

		wbv->current_sheet = sheet;
	}
}

void
wb_view_set_attributev (WorkbookView *wbv, GList *list)
{
	gint length, i;

	g_return_if_fail (IS_WORKBOOK_VIEW (wbv));

	length = g_list_length(list);

	for (i = 0; i < length; i++){
		GtkArg *arg = g_list_nth_data (list, i);

		gtk_object_arg_set (GTK_OBJECT (wbv), arg, NULL);
	}
}

GtkArg *
wb_view_get_attributev (WorkbookView *wbv, guint *n_args)
{
	GtkArg *args;
	guint num;

	g_return_val_if_fail (IS_WORKBOOK_VIEW (wbv), NULL);

	args = gtk_object_query_args (WORKBOOK_VIEW_TYPE, NULL, &num);
	gtk_object_getv (GTK_OBJECT (wbv), num, args);

	*n_args = num;

	return args;
}

void
wb_view_preferred_size (WorkbookView *wbv, int w, int h)
{
	g_return_if_fail (IS_WORKBOOK_VIEW (wbv));

	/* FIXME : should we notify the controls ? */
	wbv->preferred_width = w;
	wbv->preferred_height = h;
}

void
wb_view_prefs_update (WorkbookView *view)
{
	WORKBOOK_VIEW_FOREACH_CONTROL(view, control,
		wb_control_prefs_update	(control););
}

void
wb_view_auto_expr (WorkbookView *wbv, char const *name, char const *expression)
{
	char *old_num_locale, *old_monetary_locale, *old_msg_locale;
	ExprTree *new_auto_expr;
	ParsePos pp;
	ParseErr res;
	String *new_descr;

	old_num_locale = g_strdup (gnumeric_setlocale (LC_NUMERIC, NULL));
	gnumeric_setlocale (LC_NUMERIC, "C");
	old_monetary_locale = g_strdup (gnumeric_setlocale (LC_MONETARY, NULL));
	gnumeric_setlocale (LC_MONETARY, "C");
	/* FIXME : Get rid of this */
	old_msg_locale = g_strdup (textdomain (NULL));
	textdomain ("C");

	parse_pos_init (&pp, wb_view_workbook (wbv), NULL, 0, 0);
	res = gnumeric_expr_parser (expression, &pp, TRUE, FALSE, NULL,
				    &new_auto_expr);

	g_return_if_fail (res == PARSE_OK);
	g_return_if_fail (new_auto_expr != NULL);

	textdomain (old_msg_locale);
	g_free (old_msg_locale);
	gnumeric_setlocale (LC_MONETARY, old_monetary_locale);
	g_free (old_monetary_locale);
	gnumeric_setlocale (LC_NUMERIC, old_num_locale);
	g_free (old_num_locale);

	new_descr = string_get (name);

	if (wbv->auto_expr_desc)
		string_unref (wbv->auto_expr_desc);
	if (wbv->auto_expr)
		expr_tree_unref (wbv->auto_expr);

	wbv->auto_expr_desc = new_descr;
	wbv->auto_expr = new_auto_expr;
}

static void
wb_view_auto_expr_value_set (WorkbookView *wbv, char const *str)
{
	WORKBOOK_VIEW_FOREACH_CONTROL (wbv, control,
		wb_control_auto_expr_value (control, str););
}

void
wb_view_auto_expr_recalc (WorkbookView *wbv)
{
	static CellPos const cp = {0, 0};
	EvalPos	  ep;
	Value    *v;

	g_return_if_fail (IS_WORKBOOK_VIEW (wbv));
	g_return_if_fail (wbv->auto_expr != NULL);

	v = eval_expr (eval_pos_init (&ep, wbv->current_sheet, &cp),
		       wbv->auto_expr, EVAL_STRICT);
	if (v) {
		char *s = value_get_as_string (v);
		wb_view_auto_expr_value_set (wbv, s);
		g_free (s);
		value_release (v);
	} else
		wb_view_auto_expr_value_set (wbv, _("Internal ERROR"));
}

static void
wb_view_set_arg (GtkObject *object, GtkArg *arg, guint arg_id)
{
	WorkbookView *wbv = WORKBOOK_VIEW (object);

	g_return_if_fail (IS_WORKBOOK_VIEW (wbv));

	switch (arg_id) {
	case ARG_VIEW_HSCROLLBAR:
		wbv->show_horizontal_scrollbar = GTK_VALUE_BOOL (*arg);
		break;
	case ARG_VIEW_VSCROLLBAR:
		wbv->show_vertical_scrollbar = GTK_VALUE_BOOL (*arg);
		break;
	case ARG_VIEW_TABS:
		wbv->show_notebook_tabs = GTK_VALUE_BOOL (*arg);
		break;
	}
	wb_view_prefs_update (wbv);
}

static void
wb_view_get_arg (GtkObject *object, GtkArg *arg, guint arg_id)
{
	WorkbookView *wbv = WORKBOOK_VIEW (object);

	g_return_if_fail (IS_WORKBOOK_VIEW (wbv));

	switch (arg_id) {
	case ARG_VIEW_HSCROLLBAR:
		GTK_VALUE_BOOL (*arg) = wbv->show_horizontal_scrollbar;
		break;

	case ARG_VIEW_VSCROLLBAR:
		GTK_VALUE_BOOL (*arg) = wbv->show_vertical_scrollbar;
		break;

	case ARG_VIEW_TABS:
		GTK_VALUE_BOOL (*arg) = wbv->show_notebook_tabs;
		break;
	}
}

void
wb_view_attach_control (WorkbookControl *wbc)
{
	g_return_if_fail (IS_WORKBOOK_CONTROL (wbc));
	g_return_if_fail (IS_WORKBOOK_VIEW (wbc->wb_view));

	if (wbc->wb_view->wb_controls == NULL)
		wbc->wb_view->wb_controls = g_ptr_array_new ();
	g_ptr_array_add (wbc->wb_view->wb_controls, wbc);
}

void
wb_view_detach_control (WorkbookControl *wbc)
{
	g_return_if_fail (IS_WORKBOOK_CONTROL (wbc));
	g_return_if_fail (IS_WORKBOOK_VIEW (wbc->wb_view));

	g_ptr_array_remove (wbc->wb_view->wb_controls, wbc);
	if (wbc->wb_view->wb_controls->len == 0) {
		g_ptr_array_free (wbc->wb_view->wb_controls, TRUE);
		wbc->wb_view->wb_controls = NULL;
	}
	wbc->wb_view = NULL;
}

static GtkObjectClass *parent_class;
static void
wb_view_destroy (GtkObject *object)
{
	WorkbookView *wbv = WORKBOOK_VIEW (object);

	if (wbv->auto_expr_desc) {
		string_unref (wbv->auto_expr_desc);
		wbv->auto_expr_desc = NULL;
	}
	if (wbv->auto_expr) {
		expr_tree_unref (wbv->auto_expr);
		wbv->auto_expr = NULL;
	}
	if (wbv->wb != NULL)
		workbook_detach_view (wbv);

	if (wbv->wb_controls != NULL) {
		WORKBOOK_VIEW_FOREACH_CONTROL (wbv, control,
	        {
			wb_view_detach_control (control);
			gtk_object_unref (GTK_OBJECT (control));
		});
		if (wbv->wb_controls != NULL)
			g_warning ("Unexpected left over controls");
	}

	GTK_OBJECT_CLASS (parent_class)->destroy (object);
}

void
workbook_view_init (WorkbookView *wbv, Workbook *optional_workbook)
{
	wbv->wb = optional_workbook ? optional_workbook : workbook_new ();
	workbook_attach_view (wbv);

	wbv->show_horizontal_scrollbar = TRUE;
	wbv->show_vertical_scrollbar = TRUE;
	wbv->show_notebook_tabs = TRUE;

	/* Set the default operation to be performed over selections */
	wbv->auto_expr      = NULL;
	wbv->auto_expr_desc = NULL;
	wb_view_auto_expr (wbv, _("Sum"), "sum(selection(0))");

	/* Guess at the current sheet */
	wbv->current_sheet = NULL;
	if (optional_workbook != NULL) {
		GList *sheets = workbook_sheets (optional_workbook);
		if (sheets != NULL) {
			wb_view_sheet_focus (wbv, sheets->data);
			g_list_free (sheets);
		}
	}
}

static void
workbook_view_ctor_class (GtkObjectClass *object_class)
{
	WorkbookViewClass *wbc_class = WORKBOOK_VIEW_CLASS (object_class);

	g_return_if_fail (wbc_class != NULL);

	parent_class = gtk_type_class (gtk_object_get_type ());

	object_class->set_arg = wb_view_set_arg;
	object_class->get_arg = wb_view_get_arg;
	object_class->destroy = wb_view_destroy;
	gtk_object_add_arg_type ("WorkbookView::show_horizontal_scrollbar",
				 GTK_TYPE_BOOL, GTK_ARG_READWRITE,
				 ARG_VIEW_HSCROLLBAR);
	gtk_object_add_arg_type ("WorkbookView::show_vertical_scrollbar",
				 GTK_TYPE_BOOL, GTK_ARG_READWRITE,
				 ARG_VIEW_VSCROLLBAR);
	gtk_object_add_arg_type ("WorkbookView::show_notebook_tabs",
				 GTK_TYPE_BOOL, GTK_ARG_READWRITE,
				 ARG_VIEW_TABS);

	workbook_view_signals [SHEET_ENTERED] =
		gtk_signal_new (
			"sheet_entered",
			GTK_RUN_LAST,
			object_class->type,
			GTK_SIGNAL_OFFSET (WorkbookViewClass,
					   sheet_entered),
			gtk_marshal_NONE__POINTER,
			GTK_TYPE_NONE,
			1,
			GTK_TYPE_POINTER);
}

GNUMERIC_MAKE_TYPE(workbook_view,
		   "WorkbookView",
		   WorkbookView,
		   workbook_view_ctor_class, NULL,
		   gtk_object_get_type ());

WorkbookView *
workbook_view_new (Workbook *optional_wb)
{
	WorkbookView *view;
	
	view = gtk_type_new (workbook_view_get_type ());
	workbook_view_init (view, optional_wb);
	return view;
}
