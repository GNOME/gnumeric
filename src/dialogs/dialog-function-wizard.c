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
#include "dialogs.h"
#include "workbook.h"
#include "cell.h"
#include "expr.h"
#include "func.h"

#define INPUTS_FOR_MULTI_ARG 6

typedef struct {
	GtkWidget *dialog;
	GtkBox *dialog_box;
	GtkWidget *widget;
	Workbook *wb;
	FunctionDefinition *fd;
	GPtrArray *args;
	TokenizedHelp *tok;
} State;

static GtkWidget *
create_description (FunctionDefinition *fd)
{
	GtkBox  *vbox;
	TokenizedHelp *tok;

	tok = tokenized_help_new (fd);

	vbox = GTK_BOX (gtk_vbox_new (0, 0));
	{ /* Syntax label */
		GtkLabel *label =
			GTK_LABEL(gtk_label_new (tokenized_help_find (tok, "SYNTAX")));
		gtk_box_pack_start (vbox, GTK_WIDGET(label),
				    TRUE, TRUE, 0);
	}

	{ /* Description */
		GtkText *text;
		const char *txt = tokenized_help_find (tok, "DESCRIPTION");
		text = GTK_TEXT (gtk_text_new (NULL, NULL));
		gtk_text_set_editable (text, FALSE);
		gtk_text_insert (text, NULL, NULL, NULL,
				 txt, strlen(txt));
		gtk_text_set_word_wrap (text, TRUE);
		gtk_box_pack_start (vbox, GTK_WIDGET(text),
				    TRUE, TRUE, 0);
	}

	tokenized_help_destroy (tok);
	return GTK_WIDGET (vbox);
}

typedef struct {
	gchar   *arg_name;
	gboolean optional;
	gchar    type;
	GtkEntry *entry;
	Workbook *wb;
} ARG_DATA;

/**
 * Build descriptions of arguments
 * If fd->args == 0, make it up
 **/
static void
arg_data_list_new (State *state)
{
	gchar *copy_args;
	const gchar *syntax;
	gchar *ptr, *start = NULL;
	int i;
	int arg_max, arg_min;

	if (!state || !state->fd ||
	    !state->tok)
		return;

	state->args = g_ptr_array_new ();

	function_def_count_args (state->fd, &arg_min, &arg_max);
	if (arg_max == G_MAXINT) {
		int lp;

		for (lp = 0; lp < INPUTS_FOR_MULTI_ARG; lp++) {
			ARG_DATA *ad;

			ad = g_new (ARG_DATA, 1);
			ad->arg_name = g_strdup ("Value");
			ad->wb = state->wb;
			ad->type = '?';
			ad->optional = (lp != 0);
			ad->entry = NULL;
			g_ptr_array_add (state->args, ad);
		}
		return;
	}

	syntax = tokenized_help_find (state->tok, "SYNTAX");
	if (!syntax) {
		g_ptr_array_free (state->args, FALSE);
		state->args = NULL;
		return;
	}
	ptr = copy_args = g_strdup (syntax);
	i   = 0;
	while (*ptr) {
		if (*ptr == '(' && !start)
			start = ptr + 1;
		if (*ptr == '[' || *ptr == ']') {
			*ptr = '\0';
			if (start == ptr)
				start++;
			ptr++;
			continue;
		}
		if (*ptr == ',' || *ptr == ')') {
			if (ptr > start) {
				ARG_DATA *ad;
				ad = g_new (ARG_DATA, 1);
				ad->arg_name = g_strndup (start, (int)(ptr - start));
				ad->wb = state->wb;

				ad->type = function_def_get_arg_type (state->fd, i);
				ad->optional = (i >= arg_min);
				ad->entry = NULL;
				g_ptr_array_add (state->args, ad);
				i++;
			}
			start = ptr + 1;
		}
		ptr++;
	}

	g_free (copy_args);
}

#if 0
static void
arg_data_list_destroy (State *state)
{
	int lp;
	GPtrArray *pa;

	if (!state)
		return;
	pa = state->args;
	if (!pa)
		return;

	for (lp = 0; lp < pa->len; lp++){
		ARG_DATA *ad;

		ad = g_ptr_array_index (pa, 0);
		g_free (ad->arg_name);
		g_free (ad);
	}
	g_ptr_array_free (state->args, FALSE);
}
#endif

static void
function_input (GtkWidget *widget, ARG_DATA *ad)
{
	FunctionDefinition *fd = dialog_function_select (ad->wb);
	GtkEntry *entry = ad->entry;
	gchar *txt;
	int pos;

	if (!fd)
		return;

	/* FIXME */
	return;

	pos = gtk_editable_get_position (GTK_EDITABLE(entry));

	gtk_editable_insert_text (GTK_EDITABLE(entry),
				  txt, strlen(txt), &pos);
	g_free (txt);
}

static void
function_type_input (GtkTable *table, int row, ARG_DATA *ad)
{
	GtkButton *button;
	GtkWidget *pix;
	gchar *txt = NULL, *label;

	g_return_if_fail (ad);
	g_return_if_fail (table);

	switch (ad->type){
	case 's':
		txt = _("String");
		break;
	case 'f':
		txt = _("Number");
		break;
	case 'b':
		txt = _("Boolean");
		break;
	case 'r':
		txt = _("Range");
		break;
	case 'a':
		txt = _("Array");
		break;
	case 'A':
		txt = _("Range/Array");
		break;
	case '?':
		txt = _("Any");
		break;
	default:
		txt = _("Unknown");
		break;
	}
	gtk_table_attach_defaults (table, gtk_label_new (ad->arg_name),
				   0, 1, row, row+1);

	ad->entry = GTK_ENTRY (gtk_entry_new ());
	gtk_table_attach_defaults (table, GTK_WIDGET(ad->entry),
				   1, 2, row, row+1);

	if (ad->optional)
		label = g_strconcat ("(", txt, ")", NULL);
	else
		label = g_strconcat ("=", txt, NULL);

	button = GTK_BUTTON(gtk_button_new());
	pix = gnome_stock_pixmap_widget_new (ad->wb->toplevel,
					     GNOME_STOCK_PIXMAP_BOOK_GREEN);
	gtk_container_add (GTK_CONTAINER (button), pix);
	GTK_WIDGET_UNSET_FLAGS (button, GTK_CAN_FOCUS);
	gtk_signal_connect (GTK_OBJECT (button), "clicked",
			    GTK_SIGNAL_FUNC(function_input), ad);

	gtk_table_attach_defaults (table, GTK_WIDGET(button),
				   2, 3, row, row+1);
	gtk_table_attach_defaults (table, gtk_label_new (label),
				   3, 4, row, row+1);
	g_free (label);
}

static void
function_wizard_create (State *state)
{
	GtkWidget *vbox, *description;
	GtkTable *table;
	int lp;

	g_return_if_fail (state->args);
	vbox = gtk_vbox_new (0, 2);

	table = GTK_TABLE (gtk_table_new (3, state->args->len, FALSE));
	for (lp = 0; lp < state->args->len; lp++)
		function_type_input (table, lp, g_ptr_array_index (state->args, lp));

	gtk_box_pack_start (GTK_BOX(vbox), GTK_WIDGET (table), TRUE, TRUE, 0);

	description = create_description (state->fd);
	gtk_box_pack_start (GTK_BOX(vbox), description,
			    TRUE, TRUE, 0);

	state->widget = vbox;
	gtk_box_pack_start (state->dialog_box, vbox,
			    FALSE, FALSE, 0);
	gtk_widget_show_all (GTK_WIDGET(state->dialog_box));
}

static char*
get_text_value (State *state)
{
	gchar *txt, *txt2;
	const char *name;
	int    lp;

	g_return_val_if_fail (state != NULL, NULL);
	g_return_val_if_fail (state->fd != NULL, NULL);
	g_return_val_if_fail (state->args != NULL, NULL);

	name = function_def_get_name (state->fd);
	txt = g_strconcat (name, "(", NULL);

	for (lp = 0; lp < state->args->len; lp++) {
		ARG_DATA *ad = g_ptr_array_index (state->args, lp);
		gchar *val = gtk_entry_get_text (ad->entry);

		if (!ad->optional || strlen (val)) {
			txt2 = txt;
			txt = g_strconcat (txt2, lp?",":"", val, NULL);
			g_free (txt2);
		}
	}
	txt2 = g_strconcat (txt, ")", NULL);
	g_free (txt);
	return txt2;
}

/* Handler for destroy */
static gboolean
cb_func_wizard_destroy (GtkObject *w, State *state)
{
	g_return_val_if_fail (w != NULL, FALSE);
	g_return_val_if_fail (state != NULL, FALSE);

	if (state->tok != NULL) {
		tokenized_help_destroy (state->tok);
		state->tok = NULL;
	}
	g_free (state);
	return FALSE;
}

static void
cb_func_wizard_clicked (GnomeDialog *d, gint arg, State *state)
{
	/* Help */
	if (arg == 0)
		return;

	/* Accept for OK, reject for cancel */
	workbook_finish_editing (state->wb, arg == 1);
	gnome_dialog_close (d);
}

void
dialog_function_wizard (Workbook *wb)
{
	Sheet *sheet;
	GtkEntry *entry;
	gchar *txt;
	GtkWidget *dialog;
	State *state;
	FunctionDefinition *fd;

	g_return_if_fail (wb);

	entry = GTK_ENTRY (wb->ea_input);
	txt   = gtk_entry_get_text (entry);
	sheet = wb->current_sheet;
	if (!gnumeric_char_start_expr_p (txt[0])) {
		workbook_start_editing_at_cursor (wb, TRUE, TRUE);
		gtk_entry_set_text (entry, "=");
	} else
		workbook_start_editing_at_cursor (wb, FALSE, TRUE);

	fd = dialog_function_select (wb);
	if (fd == NULL) {
		workbook_finish_editing (wb, FALSE);
		return;
	}

	state       = g_new(State, 1);
	state->wb   = wb;
	state->fd   = fd;
	state->tok  = tokenized_help_new (fd);
	state->args = NULL;
	arg_data_list_new (state);

#if 0
	/* It takes no arguments */
	if (state->args && state->args->len == 0){
		tokenized_help_destroy (state->tok);
	}
#endif

	dialog = gnome_dialog_new (_("Formula Wizard"),
				   GNOME_STOCK_BUTTON_HELP,
				   GNOME_STOCK_BUTTON_OK,
				   GNOME_STOCK_BUTTON_CANCEL,
				   NULL);
	gtk_window_set_modal (GTK_WINDOW (dialog), FALSE);

	/* Handle destroy */
	gtk_signal_connect(GTK_OBJECT(dialog), "destroy",
			   GTK_SIGNAL_FUNC(cb_func_wizard_destroy),
			   state);
	gtk_signal_connect(GTK_OBJECT(dialog), "clicked",
			   GTK_SIGNAL_FUNC(cb_func_wizard_clicked),
			   state);

	state->dialog = dialog;
	state->dialog_box = GTK_BOX(GNOME_DIALOG (dialog)->vbox);

	function_wizard_create (state);

	gtk_entry_append_text (entry, get_text_value (state));

	gnumeric_dialog_show (wb->toplevel, GNOME_DIALOG (dialog), FALSE, TRUE);
}
