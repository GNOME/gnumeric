/*
 * dialog-function-wizard.c:  Implements the function wizard
 *
 * Author:
 *  Michael Meeks <michael@imaginator.com>
 *
 */
#include <config.h>
#include <gnome.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "gnumeric-sheet.h"
#include "dialogs.h"
#include "cell.h"
#include "expr.h"
#include "func.h"

typedef struct {
	GtkWidget *widget, *dlg;
	GtkBox    *dialog_box;
	Workbook  *wb;
	GList     *cats;
	GtkCList  *function_list;
	GtkCList  *cl_funcs;
	GtkCList  *cl_cats;
	int selected_func;  /* An Entry */
	int selected_cat;   /* An Array */
} SelectorState;

static void function_select_create (SelectorState *state);

static void
function_categories_fill (SelectorState *selector_state)
{
	GtkCList *cl = selector_state->cl_cats;
	GList *p = selector_state->cats;
	int i = 0;

	while (p) {
		FunctionCategory *fc;
		gchar *cols [1];
		
		fc = g_list_nth_data (selector_state->cats, i);
		cols[0] = fc->name;
		gtk_clist_append (cl, cols);

		if (i == selector_state->selected_cat)
			gtk_clist_select_row (cl, i, 0);

		p = g_list_next (p);
		i++;
	}
}

static void
function_definition_update (SelectorState *selector_state)
{
	FunctionCategory *cat; 
	GList *p;
	GtkCList *cl;
	FunctionDefinition *fd = NULL;
	int i, max;

	i = 0;
	cl = selector_state->cl_funcs;
	gtk_clist_freeze (cl);
	gtk_clist_clear (cl);
	
	cat = g_list_nth_data (selector_state->cats, selector_state->selected_cat);
	p = cat->functions;
	max = 0;
	
	while (p) {
		gchar *cols [1];
		FunctionDefinition *fn = p->data;
		
		cols [0] = fn [i].name;
		gtk_clist_append (cl, cols);
		
		if (i == selector_state->selected_func){
			fd = &fn [i];
			gtk_clist_select_row (cl, i, 0);
		}
		max++;
		i++;
		p = g_list_next (p);
	}
	gtk_clist_thaw (cl);
}

static void
function_select_row (GtkCList *clist, gint row, gint col,
		     GdkEvent *event, SelectorState *selector_state)
{
	if (event && event->type == GDK_2BUTTON_PRESS){
		gtk_signal_emit_by_name (GTK_OBJECT (selector_state->dlg),
					 "clicked", 0);
	}
	selector_state->selected_func = row;
	gtk_widget_show_all (GTK_WIDGET (selector_state->dialog_box));
}

static void
category_select_row (GtkCList *clist, gint row, gint col,
		     GdkEvent *event, SelectorState *state)
{
	state->selected_cat = row;
	state->selected_func = 0;
	function_definition_update (state);
	gtk_widget_show_all (GTK_WIDGET(state->dialog_box));
}

#define USIZE_WIDTH  100
#define USIZE_HEIGHT 150

static void
function_select_create (SelectorState *selector_state)
{
	GtkWidget *box, *vbox;
	GtkWidget *scroll;
	GtkCList *cl;

	vbox = gtk_vbox_new (0, 0);
	box  = gtk_hbox_new (0, 0);

	/* The Category List */
	{ 

		selector_state->cl_cats = cl = GTK_CLIST (gtk_clist_new (1));
		gtk_clist_column_titles_hide (cl);

		function_categories_fill (selector_state);
		gtk_clist_set_selection_mode (cl,GTK_SELECTION_SINGLE);
		gtk_signal_connect (GTK_OBJECT (cl), "select_row",
				    GTK_SIGNAL_FUNC (category_select_row),
				    selector_state);
		gtk_widget_set_usize (GTK_WIDGET(cl), USIZE_WIDTH, USIZE_HEIGHT);
		scroll = gtk_scrolled_window_new (NULL, NULL);
		gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
						GTK_POLICY_AUTOMATIC,
						GTK_POLICY_AUTOMATIC);
		gtk_container_add (GTK_CONTAINER (scroll), GTK_WIDGET(cl));
		gtk_box_pack_start (GTK_BOX(box), GTK_WIDGET(scroll),
				    TRUE, TRUE, 0);
	}
	
	/* The Function List */
	{ 
		selector_state->cl_funcs = cl = GTK_CLIST (gtk_clist_new(1));
		gtk_clist_column_titles_hide (cl);

		function_definition_update (selector_state);
		gtk_clist_set_selection_mode (cl,GTK_SELECTION_SINGLE);
		gtk_signal_connect (GTK_OBJECT (cl), "select_row",
				    GTK_SIGNAL_FUNC (function_select_row),
				    selector_state);
		gtk_widget_set_usize (GTK_WIDGET (cl), USIZE_WIDTH, USIZE_HEIGHT);
		scroll = gtk_scrolled_window_new (NULL, NULL);
		gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
						GTK_POLICY_AUTOMATIC,
						GTK_POLICY_AUTOMATIC);
		gtk_container_add (GTK_CONTAINER (scroll), GTK_WIDGET (cl));
		gtk_box_pack_start (GTK_BOX (box), scroll, TRUE, TRUE, 0);
	}

	gtk_box_pack_start (GTK_BOX(vbox), box, TRUE, TRUE, 0);
	
	selector_state->widget = vbox;
	gtk_box_pack_start (selector_state->dialog_box, vbox,
			    FALSE, FALSE, 0);

	gtk_widget_show_all (GTK_WIDGET(selector_state->dialog_box));
}

/**
 * Main entry point for the Cell Sort dialog box
 **/
FunctionDefinition *
dialog_function_select (Workbook *wb)
{
	GtkWidget *dialog;
	SelectorState selector_state;
	FunctionDefinition *ans = NULL;

	g_return_val_if_fail (wb, NULL);

	selector_state.wb   = wb;
	selector_state.cats = function_categories_get ();
	selector_state.selected_cat  = 0;
	selector_state.selected_func = 0;

	dialog = gnome_dialog_new (_("Formula Selection"),
				   GNOME_STOCK_BUTTON_OK,
				   GNOME_STOCK_BUTTON_CANCEL,
				   NULL);
	gtk_window_set_policy (GTK_WINDOW (dialog), TRUE, TRUE, TRUE);
	gnome_dialog_set_parent (GNOME_DIALOG (dialog),
				 GTK_WINDOW (wb->toplevel));
	gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);

	selector_state.dlg        = dialog;
	selector_state.dialog_box = GTK_BOX(GNOME_DIALOG (dialog)->vbox);

	function_select_create (&selector_state);

	if (gnome_dialog_run (GNOME_DIALOG(dialog)) == 0){
		FunctionCategory *cat = g_list_nth_data (selector_state.cats,
							 selector_state.selected_cat);
		ans = g_list_nth_data (cat->functions, selector_state.selected_func);
	}
	
	gtk_object_unref (GTK_OBJECT (dialog));
	return ans;
}
