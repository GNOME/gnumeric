/*
 * dialog-function-select.c:  Implements the function selector
 *
 * Author:
 *  Michael Meeks <michael@imaginator.com>
 *
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include "dialogs.h"

#include <gui-util.h>
#include <func.h>
#include <workbook.h>
#include <str.h>

#include <ctype.h>
#include <glade/glade.h>

#define HELP_BUTTON (GNOME_CANCEL + 1)

typedef struct {
	GtkWidget *dialog;
	GtkCList  *functions;
	GtkCList  *categories;
	GtkLabel  *func_name;
	GtkLabel  *description;

	int selected_cat;
	FunctionCategory const * cat;

	int selected_func;
	FunctionDefinition const * func;
	TokenizedHelp *func_help;
} FunctionSelectState;

static void
category_list_fill (FunctionSelectState *state)
{
	GtkCList * const list = state->categories;
	int i = 0, cur_row;

	gtk_clist_freeze (list);
	gtk_clist_clear (list);

	for (i = cur_row = 0 ;; ++i) {
		gchar *cols[1];
		FunctionCategory const * const cat =
		    function_category_get_nth (i);
		if (cat == NULL)
			break;

		cols[0] = (gchar *) cat->display_name->str; /* Const cast */
		gtk_clist_append (list, cols);

		gtk_clist_set_row_data (list, i, (gpointer)cat);
	}

	gtk_clist_select_row (list, state->selected_cat, 0);
	gtk_clist_thaw (list);
	gnumeric_clist_moveto (list, state->selected_cat);
}

static gint
by_name (gconstpointer _a, gconstpointer _b)
{
	FunctionDefinition const * const a = (FunctionDefinition const * const)_a;
	FunctionDefinition const * const b = (FunctionDefinition const * const)_b;

	return strcmp (function_def_get_name (a), function_def_get_name (b));
}

static void
function_list_fill (FunctionSelectState *state)
{
	int i = 0;
	GtkCList * const list = state->functions;
	FunctionCategory const * const cat = state->cat;
	GList *tmp, *funcs;

	gtk_clist_freeze (list);
	gtk_clist_clear (list);

	funcs = g_list_sort (g_list_copy (cat->functions), by_name);
	for (tmp = funcs; tmp; tmp = tmp->next) {
		FunctionDefinition const * const func = tmp->data;
		gchar *cols[1];

		cols[0] = (gchar *)function_def_get_name (func); /* Const cast */
		gtk_clist_append (list, cols);

		gtk_clist_set_row_data (list, i++, (gpointer)func);
	}
	g_list_free (funcs);

	gtk_clist_select_row (list, state->selected_func, 0);
	gtk_clist_thaw (list);
	gnumeric_clist_moveto (list, state->selected_func);
}

static gboolean
cb_key_press (GtkCList *list, GdkEventKey *event, int *index)
{
	if (event->string && isalpha ((unsigned char)(*event->string))) {
		int i, first, next;

		first = next = -1;
		for (i = 0; i < list->rows; i++) {
			char *t[1];

			gtk_clist_get_text (list, i, 0, t);
			if (strncasecmp (t[0], event->string, 1) == 0) {
				if (first == -1)
					first = i;
				if (next == -1 && i > *index)
					next = i;
			}
		}

		i = -1;
		if (next == -1 && first != -1)
			i = first;
		else
			i = next;

		if (i >= 0) {
			gtk_clist_select_row (list, i, 0);
			gnumeric_clist_moveto (list,i);
		}
	}

	return TRUE;
}

static void
function_select_row (GtkCList *clist, gint row, gint col,
		     GdkEvent *event, FunctionSelectState *state)
{
	if (event && event->type == GDK_2BUTTON_PRESS)
		g_signal_emit_by_name (G_OBJECT (state->dialog), "clicked", 0);
	state->selected_func = row;
	state->func = gtk_clist_get_row_data (state->functions, row);

	/* This seems to happen before we fill in things.  */
	if (state->func == NULL)
		return;

	if (state->func_help != NULL)
		tokenized_help_destroy (state->func_help);
	state->func_help = tokenized_help_new (state->func);

	gtk_label_set_text (state->func_name,
			    tokenized_help_find (state->func_help, "SYNTAX"));

	gtk_label_set_text (state->description,
			    tokenized_help_find (state->func_help, "DESCRIPTION"));
}

static void
category_select_row (GtkCList *clist, gint row, gint col,
		     GdkEvent *event, FunctionSelectState *state)
{
	state->selected_cat = row;
	state->cat = function_category_get_nth (row);
	state->selected_func = 0;
	function_list_fill (state);
}

static FunctionDefinition *
dialog_function_select_impl (WorkbookControlGUI *wbcg, GladeXML *gui)
{
	int res;
	FunctionDefinition *func = NULL;
	FunctionSelectState state;

	g_return_val_if_fail (wbcg, NULL);

	state.dialog	  = glade_xml_get_widget (gui, "FunctionSelect");
	state.categories  = GTK_CLIST (glade_xml_get_widget (gui, "category_list"));
	state.functions   = GTK_CLIST (glade_xml_get_widget (gui, "function_list"));
	state.func_name	  = GTK_LABEL (glade_xml_get_widget (gui, "function_name"));
	state.description = GTK_LABEL (glade_xml_get_widget (gui, "function_description"));
	state.func_help	  = NULL;

	g_return_val_if_fail (state.dialog, NULL);
	g_return_val_if_fail (state.categories, NULL);
	g_return_val_if_fail (state.functions, NULL);
	g_return_val_if_fail (state.func_name, NULL);
	g_return_val_if_fail (state.description, NULL);

	g_signal_connect (G_OBJECT (state.categories),
		"key-press-event",
		G_CALLBACK (cb_key_press), &state.selected_cat);
	g_signal_connect (G_OBJECT (state.functions),
		"key-press-event",
		G_CALLBACK (cb_key_press), &state.selected_func);
	g_signal_connect (G_OBJECT (state.categories),
		"select-row",
		G_CALLBACK (category_select_row), &state);
	g_signal_connect (G_OBJECT (state.functions),
		"select-row",
		G_CALLBACK (function_select_row), &state);

	/* Init the selection */
	state.selected_cat  = state.selected_func = 0;
	category_list_fill (&state);

	gtk_clist_column_titles_passive (GTK_CLIST (state.categories));
	gtk_clist_column_titles_passive (GTK_CLIST (state.functions));

	do
		/* Bring up the dialog */
		res = gnumeric_dialog_run (wbcg, GTK_DIALOG (state.dialog));
	while (res == HELP_BUTTON);

	if (state.func_help != NULL)
		tokenized_help_destroy (state.func_help);

	if (res == GNOME_OK)
		func = gtk_clist_get_row_data (state.functions,
					       state.selected_func);

	/* If the user closed the dialog with prejudice,
	   its already destroyed */
	if (res >= 0)
		gnome_dialog_close (GNOME_DIALOG (state.dialog));

	return func;
}

/* Wrapper to ensure the libglade object gets removed on error */
FunctionDefinition *
dialog_function_select (WorkbookControlGUI *wbcg)
{
	GladeXML  *gui;
	FunctionDefinition * fd;

	g_return_val_if_fail (wbcg != NULL, NULL);

	gui = gnumeric_glade_xml_new (wbcg, "function-select.glade");
        if (gui == NULL)
                return NULL;

	fd = dialog_function_select_impl (wbcg, gui);

	g_object_unref (G_OBJECT (gui));

	return fd;
}
