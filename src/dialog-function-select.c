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
	GtkWidget *widget, *dlg ;
	GtkBox *dialog_box ;
	Workbook *wb ;
	GPtrArray *cats ;
	GtkCList *function_list ;
	GtkCList *cl_funcs ;
	GtkCList *cl_cats ;
	int selected_func ;  /* An Entry */
	int selected_cat  ;  /* An Array */
} STATE ;

static void function_select_create (STATE *state) ;

static void
category_select_row (GtkCList *clist, gint row, gint col,
		     GdkEvent *event, STATE *state)
{
	state->selected_cat = row ;
/*	printf ("Row %d selected\n", row) ; */
	gtk_container_remove (GTK_CONTAINER(state->dialog_box), 
			      state->widget) ;
	state->selected_func = 0 ;
	function_select_create (state) ;
/*	gtk_clist_select_row (state->cl_cats, row, 0) ; */
	gtk_widget_show_all (GTK_WIDGET(state->dialog_box)) ;
}

static void
function_select_row (GtkCList *clist, gint row, gint col,
		     GdkEvent *event, STATE *state)
{
/* 	printf ("Function %d\n", row) ;  */
	if (state->selected_func == row) {
/*		printf ("Double click\n") ; */
		gtk_signal_emit_by_name (GTK_OBJECT(state->dlg),
					 "clicked", 0) ;
	}
	state->selected_func = row ;
	gtk_container_remove (GTK_CONTAINER(state->dialog_box),
			      state->widget) ;
	function_select_create (state) ;
/*	gtk_clist_select_row (state->cl_funcs, row, 0) ; */
	gtk_widget_show_all (GTK_WIDGET(state->dialog_box)) ;
}

static GtkWidget *
create_description (FunctionDefinition *fd)
{
	GtkBox  *vbox ;
	TOKENISED_HELP *tok ;
	#define TEXT_WIDTH 80

	tok = tokenised_help_new (fd) ;

	vbox = GTK_BOX (gtk_vbox_new (0, 0)) ;
	{ /* Syntax label */
		GtkLabel *label =
			GTK_LABEL(gtk_label_new (tokenised_help_find (tok, "SYNTAX"))) ;
		gtk_box_pack_start (vbox, GTK_WIDGET(label),
				    TRUE, TRUE, 0) ;
	}

	{ /* Description */
		GtkText *text ;
		char *txt = tokenised_help_find (tok, "DESCRIPTION") ;
		text = GTK_TEXT (gtk_text_new (NULL, NULL)) ;
		gtk_text_set_editable (text, FALSE) ;
		gtk_text_insert (text, NULL, NULL, NULL,
				 txt, strlen(txt)) ;
		gtk_box_pack_start (vbox, GTK_WIDGET(text),
				    TRUE, TRUE, 0) ;
	}

	tokenised_help_destroy (tok) ;
	return GTK_WIDGET (vbox) ;
}

static void
function_select_create (STATE *state)
{
	GtkWidget *box, *vbox ;
	GtkWidget *scroll ;
	GtkAdjustment *hadj    ;
	FunctionDefinition *fd = NULL ;
	int lp, max ;
	GtkCList *cl ;
	FUNCTION_CATEGORY *cat = g_ptr_array_index (state->cats, state->selected_cat) ;
	#define USIZE_WIDTH  100
	#define USIZE_HEIGHT 150

	vbox = gtk_vbox_new (0, 0) ;
	box  = gtk_hbox_new (0, 0) ;

	{ /* The Category List */
		state->cl_cats = cl = GTK_CLIST (gtk_clist_new(1)) ;
		gtk_clist_column_titles_hide(cl) ;
		
		for (lp=0;lp<state->cats->len;lp++) {
			gchar *cols[1] ;
			FUNCTION_CATEGORY *fc = g_ptr_array_index (state->cats, lp) ;
			cols[0] = fc->name ;
			gtk_clist_append (cl, cols) ;
			if (lp == state->selected_cat)
				gtk_clist_select_row (cl, lp, 0) ;
		}
		gtk_clist_set_selection_mode (cl,GTK_SELECTION_SINGLE) ;
		gtk_signal_connect (GTK_OBJECT (cl), "select_row",
				    GTK_SIGNAL_FUNC (category_select_row),
				    state);
		gtk_widget_set_usize (GTK_WIDGET(cl), USIZE_WIDTH, USIZE_HEIGHT) ;
		scroll = gtk_scrolled_window_new (NULL, NULL);
		gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
						GTK_POLICY_AUTOMATIC,
						GTK_POLICY_AUTOMATIC);
		gtk_container_add (GTK_CONTAINER (scroll), GTK_WIDGET(cl));
		gtk_box_pack_start (GTK_BOX(box), GTK_WIDGET(scroll),
				    TRUE, TRUE, 0) ;
	}
	
	{ /* The Function List */
		FunctionDefinition *fn = cat->functions ;
		
		state->cl_funcs = cl = GTK_CLIST (gtk_clist_new(1)) ;
		gtk_clist_column_titles_hide(cl) ;

		max = 0 ;
		for (lp = 0; fn[lp].name; lp++) {
			gchar *cols[1] ;
			cols[0] = fn[lp].name ;
			gtk_clist_append (cl, cols) ;
			if (lp == state->selected_func) {
				fd = &fn[lp] ;
				gtk_clist_select_row (cl, lp, 0) ;
			}
			max++ ;
		}
		gtk_clist_set_selection_mode (cl,GTK_SELECTION_SINGLE) ;
		gtk_signal_connect (GTK_OBJECT (cl), "select_row",
				    GTK_SIGNAL_FUNC (function_select_row),
				    state);
		gtk_widget_set_usize (GTK_WIDGET(cl), USIZE_WIDTH, USIZE_HEIGHT) ;
		scroll = gtk_scrolled_window_new (NULL, NULL);
		gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
						GTK_POLICY_AUTOMATIC,
						GTK_POLICY_AUTOMATIC);
		gtk_container_add (GTK_CONTAINER (scroll), GTK_WIDGET(cl));
		gtk_box_pack_start (GTK_BOX(box), GTK_WIDGET(scroll),
				    TRUE, TRUE, 0) ;
	}

	gtk_box_pack_start (GTK_BOX(vbox), GTK_WIDGET(box),
			    TRUE, TRUE, 0) ;
	{
		GtkWidget *description ;

		if (fd && fd->help) {
			description = create_description (fd) ;
			gtk_box_pack_start (GTK_BOX(vbox), description,
					    TRUE, TRUE, 0) ;
		}
		
	}

	state->widget = vbox ;
	gtk_box_pack_start (state->dialog_box, vbox,
			    FALSE, FALSE, 0) ;

	gtk_widget_show_all (GTK_WIDGET(state->dialog_box)) ;

/*      FIXME: Something to keep currently selected function in scope
	needs to be done.
	hadj = gtk_scrolled_window_get_hadjustment (GTK_SCROLLED_WINDOW (scroll)) ;
	gtk_adjustment_set_value (hadj, ((float)state->selected_func)) ;
	gtk_scrolled_window_set_hadjustment (GTK_SCROLLED_WINDOW (scroll),
					     hadj) ;
	max=state->cats->len ;
	hadj = gtk_scrolled_window_get_hadjustment (GTK_SCROLLED_WINDOW (scroll)) ;
	gtk_adjustment_set_value (hadj, ((float)state->selected_cat)) ;
	gtk_scrolled_window_set_hadjustment (GTK_SCROLLED_WINDOW (scroll),
	hadj) ; */

}

/**
 * Main entry point for the Cell Sort dialog box
 **/
FunctionDefinition *
dialog_function_select (Workbook *wb)
{
	GtkWidget *dialog ;
	STATE state ;
	FunctionDefinition *ans = NULL ;

	g_return_val_if_fail (wb, NULL) ;

	state.wb   = wb ;
	state.cats = get_function_categories () ;
	state.selected_cat  = 0 ;
	state.selected_func = 0 ;

	dialog = gnome_dialog_new (_("Formula Selection"),
				   GNOME_STOCK_BUTTON_OK,
				   GNOME_STOCK_BUTTON_CANCEL,
				   NULL);
	
	gnome_dialog_set_parent (GNOME_DIALOG (dialog),
				 GTK_WINDOW (wb->toplevel)) ;
	gtk_window_set_modal (GTK_WINDOW (dialog), TRUE) ;

	state.dlg        = dialog ;
	state.dialog_box = GTK_BOX(GNOME_DIALOG (dialog)->vbox) ;

	function_select_create (&state) ;

	if (gnome_dialog_run (GNOME_DIALOG(dialog)) == 0) {
		FUNCTION_CATEGORY *cat = g_ptr_array_index (state.cats,
							    state.selected_cat) ;
		ans = &cat->functions[state.selected_func] ;
	}
	
	gtk_object_destroy (GTK_OBJECT (dialog));
	return ans ;
}
