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

#define INPUTS_FOR_MULTI_ARG 6

typedef struct {
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
	#define TEXT_WIDTH 80

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
		char *txt = tokenized_help_find (tok, "DESCRIPTION");
		text = GTK_TEXT (gtk_text_new (NULL, NULL));
		gtk_text_set_editable (text, FALSE);
		gtk_text_insert (text, NULL, NULL, NULL,
				 txt, strlen(txt));
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
	gchar *ptr, *start = NULL;
	gchar *type;
	int optional = 0;

	if (!state || !state->fd ||
	    !state->tok)
		return;
	
	state->args = g_ptr_array_new ();

	type = state->fd->args;
	if (!type){
		int lp;
		for (lp=0;lp<INPUTS_FOR_MULTI_ARG;lp++){
			ARG_DATA *ad;
			ad = g_new (ARG_DATA, 1);
			ad->arg_name = g_strdup ("Value");
			ad->wb = state->wb;
			ad->type = '?';
			ad->optional = (lp!=0);
			ad->entry = NULL;
			g_ptr_array_add (state->args, ad);
		}
		return;
	}

	copy_args = tokenized_help_find (state->tok, "SYNTAX");
	if (!copy_args){
		g_ptr_array_free (state->args, FALSE);
		state->args = NULL;
	}
	copy_args = g_strdup (copy_args);

	ptr = copy_args;
	while (*ptr){
		if (*ptr=='(' && !start)
			start = ptr+1;
		if (*ptr=='[' || *ptr==']'){
			*ptr = '\0';
			if (start == ptr) start++;
			ptr++;
			continue;
		}
		if (*ptr==',' || *ptr==')'){
			if (*type=='|'){
				type++;
				optional = 1;
			}
			if (ptr > start){
				ARG_DATA *ad;
				ad = g_new (ARG_DATA, 1);
				ad->arg_name = g_strndup (start, (int)(ptr-start));
				ad->wb = state->wb;
				
				ad->type = *type;
				ad->optional = optional;
				ad->entry = NULL;
				g_ptr_array_add (state->args, ad);
			}
			type++;
			start = ptr+1;
		}
		ptr++;
	}

	g_free (copy_args);
}

static void
arg_data_list_destroy (State *state)
{
	int lp;
	GPtrArray *pa;

	if (!state) return;
	pa = state->args;
	if (!pa) return;
	for (lp=0;lp<pa->len;lp++){
		ARG_DATA *ad;
		ad = g_ptr_array_index (pa, 0);
		g_free (ad->arg_name);
		g_free (ad);
	}
	g_ptr_array_free (state->args, FALSE);
}

static void
function_input (GtkWidget *widget, ARG_DATA *ad)
{
	FunctionDefinition *fd = dialog_function_select (ad->wb);
	GtkEntry *entry = ad->entry;
	gchar *txt;
	int pos;

	if (!fd) return;
	txt = dialog_function_wizard (ad->wb, fd);
       	if (!txt || !ad->wb || !ad->wb->ea_input) return;
	
	pos = gtk_editable_get_position (GTK_EDITABLE(entry));

	gtk_editable_insert_text (GTK_EDITABLE(entry),
				  txt, strlen(txt), &pos);
	g_free (txt);
}

static GtkWidget *
function_type_input (ARG_DATA *ad)
{
	GtkBox   *box;
	GtkEntry *entry;
	GtkButton *button;
	GtkWidget *pix;
	gchar *txt = NULL, *label;

	g_return_val_if_fail (ad, NULL);

	box  = GTK_BOX (gtk_hbox_new (0, 0));

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
		txt = _("Range/Array");
		break;
	case '?':
		txt = _("Any");
		break;
	default:
		txt = _("Unknown");
		break;
	}
	gtk_box_pack_start (box, gtk_label_new (ad->arg_name),
			    TRUE, TRUE, 0);

	ad->entry = GTK_ENTRY (gtk_entry_new ());
	gtk_box_pack_start (box, GTK_WIDGET(ad->entry),
			    FALSE, FALSE, 0);

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
	
	gtk_box_pack_start (box, GTK_WIDGET(button),
			    TRUE, TRUE, 0);
	gtk_box_pack_start (box, gtk_label_new (label),
			    TRUE, TRUE, 0);
	g_free (label);

	return GTK_WIDGET(box);
}

static void
function_wizard_create (State *state)
{
	GtkWidget *vbox, *description;
	int lp;

	g_return_if_fail (state->args);
	vbox = gtk_vbox_new (0, 2);

	for (lp=0;lp<state->args->len;lp++){
		GtkWidget *widg;
		widg = function_type_input (g_ptr_array_index (state->args, lp));
		gtk_box_pack_start (state->dialog_box, widg,
				    FALSE, FALSE, 0);
	}

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
	int lp;

	g_return_val_if_fail (state, NULL);
	g_return_val_if_fail (state->fd, NULL);
	g_return_val_if_fail (state->args, NULL);

	txt = g_strconcat (state->fd->name, "(", NULL);
	for (lp=0;lp<state->args->len;lp++){
		int comma;
		ARG_DATA *ad = g_ptr_array_index (state->args, lp);
		gchar *val = gtk_entry_get_text (ad->entry);
		if (!ad->optional || strlen(val)){
			txt2 = txt;
			txt = g_strconcat (txt2, lp?",":"", val, NULL);
			g_free (txt2);
		}
	}
	txt2 = g_strconcat (txt, ")", NULL);
	g_free (txt);
	return txt2;
}

/**
 * Main entry point for the Cell Sort dialog box
 **/
char *
dialog_function_wizard (Workbook *wb, FunctionDefinition *fd)
{
	GtkWidget *dialog;
	State state;
	char *ans = NULL;

	g_return_val_if_fail (wb, NULL);

	state.wb   = wb;
	state.fd   = fd;
	state.tok  = tokenized_help_new (fd);
	state.args = NULL;
	arg_data_list_new (&state);

	/* It takes no arguments */
	if (state.args && state.args->len == 0){
		tokenized_help_destroy (state.tok);
		return get_text_value (&state);
	}

	dialog = gnome_dialog_new (_("Formula Wizard"),
				   GNOME_STOCK_BUTTON_OK,
				   GNOME_STOCK_BUTTON_CANCEL,
				   NULL);
	
	gnome_dialog_set_parent (GNOME_DIALOG (dialog),
				 GTK_WINDOW (wb->toplevel));
	gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);

	state.dialog_box = GTK_BOX(GNOME_DIALOG (dialog)->vbox);

	function_wizard_create (&state);

	if (gnome_dialog_run (GNOME_DIALOG(dialog)) == 0)
		ans = get_text_value (&state);
	
	gtk_object_destroy (GTK_OBJECT (dialog));
	tokenized_help_destroy (state.tok);
	return ans;
}
