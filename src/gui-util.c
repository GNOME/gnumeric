/*
 * gnumeric-util.c:  Various GUI utility functions.
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 */
#include <config.h>
#include "workbook-control-gui-priv.h"
#include "gui-util.h"
#include "gutils.h"
#include "parse-util.h"
#include "style.h"
#include "error-info.h"

#include <string.h>
#include <gal/widgets/e-colors.h>
#include <glade/glade.h>

#ifdef ENABLE_BONOBO
#	include <bonobo.h>
#	include "workbook-private.h"
#endif

#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-util.h>
#include <libgnome/gnome-i18n.h>

gboolean
gnumeric_dialog_question_yes_no (WorkbookControlGUI *wbcg,
                                 const gchar *message,
                                 gboolean default_answer)
{
	GtkWidget *dialog, *default_button;

	dialog = gnome_message_box_new (
	         message,
	         GNOME_MESSAGE_BOX_QUESTION,
	         GNOME_STOCK_BUTTON_YES,
	         GNOME_STOCK_BUTTON_NO,
	         NULL);
	if (default_answer) {
		default_button = (GtkWidget *) GNOME_DIALOG (dialog)->buttons->data;
	} else {
		default_button = (GtkWidget *) GNOME_DIALOG (dialog)->buttons->next->data;
	}
	gtk_widget_grab_focus (default_button);

	return gnumeric_dialog_run (wbcg, GNOME_DIALOG (dialog)) == 0;
}

static void
fsel_dialog_finish (GtkWidget *widget)
{
	gtk_widget_hide (widget);
	gtk_main_quit ();
}

static void
fsel_handle_ok (GtkWidget *widget, gboolean *result)
{
	GtkFileSelection *fsel;
	gchar *file_name;

	fsel = GTK_FILE_SELECTION (gtk_widget_get_ancestor (widget, GTK_TYPE_FILE_SELECTION));
	file_name = gtk_file_selection_get_filename (fsel);

	/* Change into directory if that's what user selected */
	if (g_file_test (file_name, G_FILE_TEST_ISDIR)) {
		gint name_len;
		gchar *dir_name;

		name_len = strlen (file_name);
		if (name_len < 1 || file_name [name_len - 1] != '/') {
			/* The file selector needs a '/' at the end of a directory name */
			dir_name = g_strconcat (file_name, "/", NULL);
		} else {
			dir_name = g_strdup (file_name);
		}
		gtk_file_selection_set_filename (fsel, dir_name);
		g_free (dir_name);
	} else {
		fsel_dialog_finish (GTK_WIDGET (fsel));
		*result = TRUE;
	}
}

static void
fsel_handle_cancel (GtkWidget *widget, gpointer user_data)
{
	GtkWidget *fsel;

	fsel = gtk_widget_get_ancestor (widget, GTK_TYPE_FILE_SELECTION);
	fsel_dialog_finish (fsel);
}

static gint
fsel_delete_event (GtkWidget *widget, GdkEventAny *event)
{
	fsel_dialog_finish (widget);
	return TRUE;
}

static gint
fsel_key_event (GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
	if (event->keyval == GDK_Escape) {
		gtk_signal_emit_stop_by_name (GTK_OBJECT (widget), "key_press_event");
		fsel_dialog_finish (widget);
		return TRUE;
	}

	return FALSE;
}

gboolean
gnumeric_dialog_file_selection (WorkbookControlGUI *wbcg, GtkFileSelection *fsel)
{
	gboolean result = FALSE;

	gtk_window_set_modal (GTK_WINDOW (fsel), TRUE);
	gnumeric_set_transient (wbcg, GTK_WINDOW (fsel));
	gtk_signal_connect (GTK_OBJECT (fsel->ok_button), "clicked",
	                    GTK_SIGNAL_FUNC (fsel_handle_ok), &result);
	gtk_signal_connect (GTK_OBJECT (fsel->cancel_button), "clicked",
	                    GTK_SIGNAL_FUNC (fsel_handle_cancel), NULL);
	gtk_signal_connect (GTK_OBJECT (fsel), "key_press_event",
	                    GTK_SIGNAL_FUNC (fsel_key_event), NULL);
	gtk_signal_connect (GTK_OBJECT (fsel), "delete_event",
	                    GTK_SIGNAL_FUNC (fsel_delete_event), NULL);
	gtk_widget_show_all (GTK_WIDGET (fsel));
	gtk_grab_add (GTK_WIDGET (fsel));
	gtk_main ();

	return result;
}

/*
 * TODO:
 * Get rid of trailing newlines /whitespace.
 * Wrap overlong lines.
 */
void
gnumeric_notice (WorkbookControlGUI *wbcg, const char *type, const char *str)
{
	GtkWidget *dialog;

	dialog = gnome_message_box_new (str, type, GNOME_STOCK_BUTTON_OK, NULL);

	gnumeric_dialog_run (wbcg, GNOME_DIALOG (dialog));
}

/**
 * gnumeric_dialog_run
 *
 * Pop up a dialog as child of a workbook.
 */
gint
gnumeric_dialog_run (WorkbookControlGUI *wbcg, GnomeDialog *dialog)
{
	GtkWindow *toplevel;

	g_return_val_if_fail (GNOME_IS_DIALOG (dialog), -1);

	if (wbcg) {
		g_return_val_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg), -1);

		toplevel = wb_control_gui_toplevel (wbcg);
		if (GTK_WINDOW (dialog)->transient_parent != toplevel)
			gnome_dialog_set_parent (GNOME_DIALOG (dialog), toplevel);
	}
	return gnome_dialog_run (dialog);
}

/**
 * Data structure and callbacks for dialogs which do not use recursive
 * mainloop.  */
typedef struct {
	GtkWidget *parent_toplevel;
	gint       parent_close_id;
} DialogRunInfo;

static gboolean
on_parent_close (GnomeDialog *parent, GnomeDialog *dialog)
{
	gnome_dialog_close (dialog);
	return FALSE;
}

static gboolean
on_parent_delete (GnomeDialog *parent, GdkEvent *event, GnomeDialog *dialog)
{
	return on_parent_close (parent, dialog);
}

static gboolean
on_close (GnomeDialog *dialog,
	  DialogRunInfo *run_info)
{
	gtk_signal_disconnect(GTK_OBJECT (run_info->parent_toplevel),
			      run_info->parent_close_id);
	g_free (run_info);
	return FALSE;
}

/**
 * connect_to_parent_close
 *
 * Attach a handler to close if the parent closes.
 */
static void
connect_to_parent_close (GnomeDialog *dialog, DialogRunInfo *run_info)
{
	if (GNOME_IS_DIALOG(run_info->parent_toplevel)) {
		run_info->parent_close_id =
			gtk_signal_connect
			(GTK_OBJECT (run_info->parent_toplevel),
			 "close", (GtkSignalFunc) on_parent_close,
			 dialog);
	} else {
		run_info->parent_close_id =
			gtk_signal_connect
			(GTK_OBJECT (run_info->parent_toplevel),
			 "delete_event",
			 (GtkSignalFunc) on_parent_delete,
			 dialog);
	}
	gtk_signal_connect (GTK_OBJECT (dialog), "close",
			    (GtkSignalFunc) on_close, run_info);
}

/**
 * gnumeric_dialog_show
 * @parent             parent widget
 * @dialog             dialog
 * @click_closes       close on click
 * @close_with_parent  close when parent closes
 *
 * Pop up a dialog without a recursive main loop
 *
 * Attach a handler to close if the parent closes.
 * The parent widget does not have to be a toplevel shell - we look it
 * up here.
 */
void
gnumeric_dialog_show (WorkbookControlGUI *wbcg, GnomeDialog *dialog,
		      gboolean click_closes, gboolean close_with_parent)
{
	GtkWindow *parent = wb_control_gui_toplevel (wbcg);
	DialogRunInfo *run_info = NULL;

	g_return_if_fail(GNOME_IS_DIALOG(dialog));
	if (parent != NULL) {
		run_info = g_new0 (DialogRunInfo, 1);
		run_info->parent_toplevel =
			gtk_widget_get_toplevel (GTK_WIDGET (parent));

		gnome_dialog_set_parent
			(GNOME_DIALOG (dialog),
			 GTK_WINDOW (run_info->parent_toplevel));
		if (close_with_parent)
			connect_to_parent_close (dialog, run_info);
	}

	gnome_dialog_set_close (GNOME_DIALOG (dialog), click_closes);

	if ( ! GTK_WIDGET_VISIBLE(GTK_WIDGET(dialog)) )	/* Pop up the dialog */
		gtk_widget_show(GTK_WIDGET(dialog));
}

static GtkCTreeNode *
ctree_insert_error_info (GtkCTree *ctree, GtkCTreeNode *parent,
                         GtkCTreeNode *sibling, ErrorInfo *error,
                         gboolean expand)
{
	GtkCTreeNode *my_node, *last_child_node;
	gchar *message;
	gboolean child_expand;
	GList *details_list, *l;

	message = (gchar *) error_info_peek_message (error);
	if (message == NULL) {
		message = _("Multiple errors");
	}
	details_list = error_info_peek_details (error);
	my_node = gtk_ctree_insert_node (ctree, parent, sibling, &message, 0, NULL, NULL, NULL, NULL, details_list == NULL, expand);

	child_expand = details_list == NULL || details_list->next == NULL;
	last_child_node = NULL;
	for (l = details_list; l != NULL; l = l->next) {
		ErrorInfo *detail_error = l->data;

		last_child_node = ctree_insert_error_info (ctree, my_node, last_child_node,
		                                           detail_error, child_expand);
	}

	return my_node;
}

/**
 * gnumeric_error_info_dialog_show_full
 *
 */
static void
gnumeric_error_info_dialog_show_full (WorkbookControlGUI *wbcg, ErrorInfo *error)
{
	GtkWidget *dialog;
	GtkWidget *scrolled_window, *ctree;
	GtkCTreeNode *main_ctree_node;
	GtkWidget *dialog_action_area;
	GtkWidget *button_close;

	g_return_if_fail (error != NULL);

	dialog = gnome_dialog_new (_("Detailed error message"), NULL);
	gtk_widget_set_usize (dialog, 600, 300);
	gtk_window_set_policy (GTK_WINDOW (dialog), FALSE, TRUE, FALSE);

	scrolled_window = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	ctree = gtk_ctree_new (1, 0);
	gtk_ctree_set_line_style (GTK_CTREE (ctree), GTK_CTREE_LINES_NONE);
	gtk_ctree_set_expander_style (GTK_CTREE (ctree), GTK_CTREE_EXPANDER_TRIANGLE);
	main_ctree_node = ctree_insert_error_info (GTK_CTREE (ctree), NULL, NULL, error, TRUE);
	gtk_clist_set_column_auto_resize (GTK_CLIST (ctree), 0, TRUE);
	gtk_container_add (GTK_CONTAINER (scrolled_window), ctree);
	gtk_widget_show_all (GTK_WIDGET (scrolled_window));
	gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (dialog)->vbox), scrolled_window, TRUE, TRUE, 0);

	dialog_action_area = GNOME_DIALOG (dialog)->action_area;
	gtk_button_box_set_layout (GTK_BUTTON_BOX (dialog_action_area), GTK_BUTTONBOX_END);
	gtk_button_box_set_spacing (GTK_BUTTON_BOX (dialog_action_area), 8);

	gnome_dialog_append_button (GNOME_DIALOG (dialog), GNOME_STOCK_BUTTON_CLOSE);
	button_close = GTK_WIDGET (g_list_last (GNOME_DIALOG (dialog)->buttons)->data);
	GTK_WIDGET_SET_FLAGS (button_close, GTK_CAN_DEFAULT);

	gnome_dialog_set_close (GNOME_DIALOG (dialog), TRUE);
	gnumeric_dialog_run (wbcg, GNOME_DIALOG (dialog));
}

/**
 * gnumeric_error_info_dialog_show
 *
 */
void
gnumeric_error_info_dialog_show (WorkbookControlGUI *wbcg, ErrorInfo *error)
{
	GString *str;
	GList *details;
	gboolean has_extra_details;
	GtkWidget *dialog, *default_button;

	str = g_string_new (error_info_peek_message (error));
	details = error_info_peek_details (error);
	if (g_list_length (details) == 1) {
		ErrorInfo *details_error = details->data;
		const gchar *s;

		s = error_info_peek_message (details_error);
		if (s != NULL) {
			g_string_append (str, "\n\n");
			g_string_append (str, s);
		}
		has_extra_details = error_info_peek_details (details_error) != NULL;
	} else {
		has_extra_details = details != NULL;
	}
	if (has_extra_details) {
		dialog = gnome_message_box_new (
		         str->str, GNOME_MESSAGE_BOX_ERROR,
		         _("Show details"), GNOME_STOCK_BUTTON_CLOSE, NULL);
		default_button = (GtkWidget *) GNOME_DIALOG (dialog)->buttons->next->data;
	} else {
		dialog = gnome_message_box_new (
		         str->str, GNOME_MESSAGE_BOX_ERROR,
		         GNOME_STOCK_BUTTON_CLOSE, NULL);
		default_button = (GtkWidget *) GNOME_DIALOG (dialog)->buttons->data;
	}
	g_string_free (str, TRUE);
	gtk_widget_grab_focus (default_button);

	if (gnumeric_dialog_run (wbcg, GNOME_DIALOG (dialog)) == 0 &&
	    has_extra_details) {
		gnumeric_error_info_dialog_show_full (wbcg, error);
	}
}

/**
 * gnumeric_set_transient
 * @wbcg	: The calling window
 * @window      : the transient window
 *
 * Make the window a child of the workbook in the command context, if there is
 * one.
 * The function duplicates the positioning functionality in
 * gnome_dialog_set_parent, but does not require the transient window to be
 * a GnomeDialog.
 */
void
gnumeric_set_transient (WorkbookControlGUI *wbcg, GtkWindow *window)
{
	GtkWindow *toplevel;

	g_return_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg));
	g_return_if_fail (GTK_IS_WINDOW (window));

	toplevel = wb_control_gui_toplevel (wbcg);
	gtk_window_set_transient_for (window, toplevel);

	gtk_window_set_position(window,
				gnome_preferences_get_dialog_position());
	if (gnome_preferences_get_dialog_centered()) {

		/* User wants us to center over toplevel */

		gint x, y, w, h, dialog_x, dialog_y;

		if ( ! GTK_WIDGET_VISIBLE(toplevel)) return; /* Can't get its
								size/pos */

		/* Throw out other positioning */
		gtk_window_set_position(toplevel, GTK_WIN_POS_NONE);

		gdk_window_get_origin (GTK_WIDGET(toplevel)->window, &x, &y);
		gdk_window_get_size   (GTK_WIDGET(toplevel)->window, &w, &h);

		/* The problem here is we don't know how big the dialog is.
		   So "centered" isn't really true. We'll go with
		   "kind of more or less on top" */

		dialog_x = x + w/4;
		dialog_y = y + h/4;

		gtk_widget_set_uposition(GTK_WIDGET(window), dialog_x, dialog_y);
	}
}

typedef struct {
	WorkbookControlGUI *wbcg;
	char *key;
} KeyedDialogContext;

static void
cb_remove_object_data (GtkWidget *w, KeyedDialogContext *ctxt)
{
	g_return_if_fail (
		gtk_object_get_data (
			GTK_OBJECT (ctxt->wbcg), ctxt->key) != NULL);

	gtk_object_remove_data (GTK_OBJECT (ctxt->wbcg), ctxt->key);
	g_free (ctxt);
}

/**
 * gnumeric_keyed_dialog
 *
 * @wbcg    A WorkbookControlGUI
 * @dialog  A transient window
 * @key     A key to identify the dialog
 *
 * Make dialog a transient child of wbcg, attaching to wbcg object data to
 * identify the dialog. The object data makes it possible to ensure that
 * only one dialog of a kind can be displayed for a wbcg. Deallocation of
 * the object data is managed here.  */
void
gnumeric_keyed_dialog (WorkbookControlGUI *wbcg, GtkWindow *dialog, const char *key)
{
	KeyedDialogContext *ctxt;

	g_return_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg));
	g_return_if_fail (GTK_IS_WINDOW (dialog));
	g_return_if_fail (key != NULL);

	gnumeric_set_transient (wbcg, dialog);

	ctxt = g_new (KeyedDialogContext, 1);
	ctxt->wbcg = wbcg;
	ctxt->key  = key;
	gtk_object_set_data (GTK_OBJECT (wbcg), key, dialog);

	gtk_signal_connect (
		GTK_OBJECT (dialog), "destroy",
		GTK_SIGNAL_FUNC (cb_remove_object_data), ctxt);
}

/**
 * gnumeric_dialog_raise_if_exists
 *
 * @wbcg    A WorkbookControlGUI
 * @key     A key to identify the dialog
 *
 * Raise the dialog identified by key if it is registered on the wbcg.
 * Returns TRUE if dialog found, FALSE if not.
 */
gboolean
gnumeric_dialog_raise_if_exists (WorkbookControlGUI *wbcg, char *key)
{
	GtkWidget *dialog;

	g_return_val_if_fail (wbcg != NULL, FALSE);
	g_return_val_if_fail (key != NULL, FALSE);

	/* Ensure we only pop up one copy per workbook */
	dialog = gtk_object_get_data (GTK_OBJECT (wbcg), key);
	if (dialog && GTK_IS_WINDOW (dialog)) {
		gdk_window_raise (dialog->window);
		return TRUE;
	} else
		return FALSE;
}

/**
 * gnumeric_editable_enters: Make the "activate" signal of an editable click
 * the default dialog button.
 * @window: dialog to affect.
 * @editable: Editable to affect.
 *
 * This is a literal copy of gnome_dialog_editable_enters, but not restricted
 * to GnomeDialogs.
 *
 * Normally if there's an editable widget (such as #GtkEntry) in your
 * dialog, pressing Enter will activate the editable rather than the
 * default dialog button. However, in most cases, the user expects to
 * type something in and then press enter to close the dialog. This
 * function enables that behavior.
 *
 **/
void  gnumeric_editable_enters (GtkWindow *window, GtkEditable *editable)
{
	g_return_if_fail(window != NULL);
	g_return_if_fail(editable != NULL);
	g_return_if_fail(GTK_IS_WINDOW(window));
	g_return_if_fail(GTK_IS_EDITABLE(editable));

	gtk_signal_connect_object
		(GTK_OBJECT(editable), "activate",
		 GTK_SIGNAL_FUNC(gtk_window_activate_default),
		 GTK_OBJECT(window));
}

/**
 * gnumeric_combo_enters:
 * @window: dialog to affect
 * @combo: Combo to affect
 *
 * This calls upon gnumeric_editable_enters so the dialog
 * is closed instead of the list with options popping up
 * when enter is pressed
 **/
void
gnumeric_combo_enters (GtkWindow *window, GtkCombo *combo)
{
	g_return_if_fail (window != NULL);
	g_return_if_fail (combo != NULL);

	gtk_combo_disable_activate (combo);
	gnumeric_editable_enters (window,
				  GTK_EDITABLE (combo->entry));
}

/**
 * gnumeric_toolbar_insert_with_eventbox
 * @toolbar               toolbar
 * @widget                widget to insert
 * @tooltip_text          tooltip text
 * @tooltip_private_text  longer tooltip text
 * @position              widget position in toolbar
 *
 * Packs widget in an eventbox and adds the eventbox to toolbar.
 * This lets a windowless widget (e.g. combo box) have tooltips.
 **/
void
gnumeric_toolbar_insert_with_eventbox (GtkToolbar *toolbar, GtkWidget  *widget,
				       const char *tooltip_text,
				       const char *tooltip_private_text,
				       gint        position)
{
	GtkWidget *eventbox;

	g_return_if_fail (GTK_IS_TOOLBAR (toolbar));
	g_return_if_fail (widget != NULL);
	g_return_if_fail (position >= 0);
	g_return_if_fail (position <= toolbar->num_children);

	/* An event box to receive events - this is a requirement for having
           tooltips */
	eventbox = gtk_event_box_new ();
	gtk_widget_show (widget);
	gtk_container_add (GTK_CONTAINER (eventbox), widget);
	gtk_widget_show (eventbox);
	gtk_toolbar_insert_widget (GTK_TOOLBAR (toolbar), eventbox,
				   tooltip_text, tooltip_private_text,
				   position);
}

/**
 * gnumeric_toolbar_append_with_eventbox
 * @toolbar               toolbar
 * @widget                widget to insert
 * @tooltip_text          tooltip text
 * @tooltip_private_text  longer tooltip text
 *
 * Packs widget in an eventbox and adds the eventbox to toolbar.
 * This lets a windowless widget (e.g. combo box) have tooltips.
 **/
void
gnumeric_toolbar_append_with_eventbox (GtkToolbar *toolbar, GtkWidget  *widget,
				       const char *tooltip_text,
				       const char *tooltip_private_text)
{
	GtkWidget *eventbox;

	g_return_if_fail (GTK_IS_TOOLBAR (toolbar));
	g_return_if_fail (widget != NULL);

	/* An event box to receive events - this is a requirement for having
           tooltips */
	eventbox = gtk_event_box_new ();
	gtk_widget_show (widget);
	gtk_container_add (GTK_CONTAINER (eventbox), widget);
	gtk_widget_show (eventbox);
	gtk_toolbar_append_widget (GTK_TOOLBAR (toolbar), eventbox,
				   tooltip_text, tooltip_private_text);
}

int
gtk_radio_group_get_selected (GSList *radio_group)
{
	GSList *l;
	int i, c;

	g_return_val_if_fail (radio_group != NULL, 0);

	c = g_slist_length (radio_group);

	for (i = 0, l = radio_group; l; l = l->next, i++){
		GtkRadioButton *button = l->data;

		if (GTK_TOGGLE_BUTTON (button)->active)
			return c - i - 1;
	}

	return 0;
}


int
gnumeric_glade_group_value (GladeXML *gui, const char *group[])
{
	int i;
	for (i = 0; group[i]; i++) {
		GtkWidget *w = glade_xml_get_widget (gui, group[i]);
		if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w)))
			return i;
	}
	return -1;
}



static char *
font_change_component_1 (const char *fontname, int idx,
			 const char *newvalue, char const **end)
{
	char *res, *dst;
	int hyphens = 0;

	dst = res = (char *)g_malloc (strlen (fontname) + strlen (newvalue) + idx + 5);
	while (*fontname && *fontname != ',') {
		if (hyphens != idx)
			*dst++ = *fontname;
		if (*fontname++ == '-') {
			if (hyphens == idx)
				*dst++ = '-';
			if (++hyphens == idx) {
				strcpy (dst, newvalue);
				dst += strlen (newvalue);
			}
		}
	}
	*end = fontname;
	if (hyphens < idx) {
		while (hyphens++ < idx)
			*dst++ = '-';
		strcpy (dst, newvalue);
		dst += strlen (newvalue);
	}
	*dst = 0;
	return res;
}

char *
x11_font_change_component (const char *fontname, int idx, const char *newvalue)
{
	char *res = 0;
	int reslen = 0;

	while (*fontname) {
		const char *end;
		char *new;
		int newlen;

		new = font_change_component_1 (fontname, idx + 1, newvalue, &end);
		newlen = strlen (new);

		res = (char *)g_realloc (res, reslen + newlen + 2);
		strcpy (res + reslen, new);
		g_free (new);
		reslen += newlen;
		fontname = end;
		if (*fontname == ',') {
			res[reslen++] = ',';
			fontname++;
		}
	}
	if (reslen) {
		res[reslen] = 0;
		return res;
	} else
		return g_strdup ("");
}

char *
x11_font_get_bold_name (const char *fontname, int units)
{
	char *f;

	/*
	 * FIXME: this scheme is poor: in some cases, the fount strength is called 'bold',
	 * whereas in some others it is 'black', in others... Look font_get_italic_name
	 */
	f = x11_font_change_component (fontname, 2, "bold");

	return f;
}

char *
x11_font_get_italic_name (const char *fontname, int units)
{
	char *f;

	f = x11_font_change_component (fontname, 3, "o");
	return f;
}

static void
kill_popup_menu (GtkWidget *widget, GtkMenu *menu)
{
	g_return_if_fail (menu != NULL);
	g_return_if_fail (GTK_IS_MENU (menu));

	gtk_object_unref (GTK_OBJECT (menu));
}

void
gnumeric_auto_kill_popup_menu_on_hide (GtkMenu *menu)
{
	g_return_if_fail (menu != NULL);
	g_return_if_fail (GTK_IS_MENU (menu));

	gtk_signal_connect (GTK_OBJECT (menu), "hide",
			    GTK_SIGNAL_FUNC (kill_popup_menu), menu);
}

void
gnumeric_popup_menu (GtkMenu *menu, GdkEventButton *event)
{
	g_return_if_fail (menu != NULL);
	g_return_if_fail (GTK_IS_MENU (menu));

	gnumeric_auto_kill_popup_menu_on_hide (menu);

	/* Do NOT pass the button used to create the menu.
	 * instead pass 0.  Otherwise bringing up a menu with
	 * the right button will disable clicking on the menu with the left.
	 */
	gtk_menu_popup (menu, NULL, NULL, 0, NULL, 0, event->time);
}

/*
 * Helper for gnumeric_clist_moveto. Ensures that we move in the same way
 * whether direct or from a callback.
 */
static void
clist_moveto (GtkCList *clist, gint row)
{
	gtk_clist_moveto (clist, row, 0, 0.1, 0.0);
	clist->focus_row = row;
}

/*
 * map handler. Disconnects itself and moves the list.
 */
static void
cb_clist_moveto (GtkWidget *clist, gpointer row)
{
	gtk_signal_disconnect_by_func (GTK_OBJECT (clist),
				       GTK_SIGNAL_FUNC (cb_clist_moveto),
				       row);

	clist_moveto (GTK_CLIST (clist), GPOINTER_TO_INT (row));
}

/*
 * gnumeric_clist_moveto
 * @clist   clist
 * @row     row

 *
 * Scroll the viewing area of the list to the given row.
 * We do it this way because gtk_clist_moveto only works if the list is
 * mapped.
 */
void
gnumeric_clist_moveto (GtkCList *clist, gint row)
{
	if (GTK_WIDGET_DRAWABLE (clist))
		clist_moveto (clist, row);
	else
		gtk_signal_connect (GTK_OBJECT (clist), "map",
				    GTK_SIGNAL_FUNC (cb_clist_moveto),
				    GINT_TO_POINTER (row));
}

/*
 * gnumeric_clist_make_selection_visible
 * @clist   clist
 *
 * Scroll the viewing area of the list to the first selected row.
 */
void
gnumeric_clist_make_selection_visible (GtkCList *clist)
{
	guint selection_length;

	g_return_if_fail(GTK_IS_CLIST(clist));

	selection_length = g_list_length (clist->selection);
	if (selection_length > 0) {
		gint row = (gint) clist->selection->data;
		gnumeric_clist_moveto (clist, row);
	}
}

/**
 * gnumeric_option_menu_get_selected_index:
 * @optionmenu: a gtkoptionmenu
 *
 * Tries to find out (in an ugly way) the selected
 * item in @optionsmenu
 *
 * Return value: the selected index or -1 on error (or no selection?)
 **/
int
gnumeric_option_menu_get_selected_index (GtkOptionMenu *optionmenu)
{
	GtkMenu *menu;
	GtkMenuItem *selected;
	GList *iterator;
	int index = -1;
	int i = 0;

	g_return_val_if_fail (optionmenu != NULL, -1);

	menu = (GtkMenu *) gtk_option_menu_get_menu (optionmenu);
	iterator = GTK_MENU_SHELL (menu)->children;
	selected = (GtkMenuItem *) gtk_menu_get_active (menu);

	while (iterator) {

		if (iterator->data == selected) {

			index = i;
			break;
		}

		iterator = iterator->next;
		i++;
	}

	return index;
}

GtkWidget *
gnumeric_create_tooltip (void)
{
	GtkWidget *tip, *label, *frame;
	static GtkRcStyle*rc_style = NULL;

	if (rc_style == NULL) {
		int i;
		rc_style = gtk_rc_style_new ();

		for (i = 5; --i >= 0 ; ) {
			rc_style->color_flags[i] = GTK_RC_BG;
			e_color_alloc_name ("LightYellow",  &rc_style->bg[i]);
		}
	}

	tip = gtk_window_new (GTK_WINDOW_POPUP);
	if (rc_style != NULL)
		gtk_widget_modify_style (tip, rc_style);

	frame = gtk_frame_new (NULL);
	gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_OUT);
	label = gtk_label_new ("");

	gtk_container_add (GTK_CONTAINER (tip), frame);
	gtk_container_add (GTK_CONTAINER (frame), label);

	return label;
}

void
gnumeric_position_tooltip (GtkWidget *tip, int horizontal)
{
	GtkRequisition req;
	int  x, y;

	gtk_widget_size_request (tip, &req);
	gdk_window_get_pointer (NULL, &x, &y, NULL);
	if (horizontal){
		x = x - req.width/2;
		y = y - req.height - 20;
	} else {
		x = x - req.width - 20;
		y = y - req.height/2;
	}

	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;
	gtk_widget_set_uposition (gtk_widget_get_toplevel (tip), x, y);
}

GladeXML *
gnumeric_glade_xml_new (WorkbookControlGUI *wbcg, char const * gladefile)
{
	GladeXML *gui;
	char *d = gnumeric_sys_glade_dir ();
	char *f = g_concat_dir_and_file (d, gladefile);
	gui = glade_xml_new (f, NULL);

	/* Onlt report errors if the context is non-null */
	if (gui == NULL && wbcg != NULL) {
		char *msg = g_strdup_printf (_("Unable to open file '%s'"), f);
		gnumeric_error_system (COMMAND_CONTEXT (wbcg), msg);
		g_free (msg);
	}
	g_free (f);
	g_free (d);

	return gui;
}

static gint
cb_non_modal_dialog_keypress (GtkWidget *w, GdkEventKey *e)
{
	if(e->keyval == GDK_Escape) {
		gtk_widget_destroy (w);
		return TRUE;
	}

	return FALSE;
}

void
gnumeric_non_modal_dialog (WorkbookControlGUI *wbcg, GtkWindow *dialog)
{
	gnumeric_set_transient (wbcg, dialog);

	gtk_signal_connect (GTK_OBJECT (dialog), "key-press-event",
			    (GtkSignalFunc) cb_non_modal_dialog_keypress, NULL);
}

#ifdef ENABLE_BONOBO
/*
 * gnumeric_inject_widget_into_bonoboui :
 *
 * A quick utility routine to inject a widget into a menu/toolbar.
 */
void
gnumeric_inject_widget_into_bonoboui (WorkbookControlGUI *wbcg, GtkWidget *widget, char const *path)
{
	BonoboControl *control;

	gtk_widget_show_all (widget);
	control = bonobo_control_new (widget);
	bonobo_ui_component_object_set (
		wbcg->uic, path,
		bonobo_object_corba_objref (BONOBO_OBJECT (control)),
		NULL);
}
#endif

static void
popup_item_activate (GtkWidget *item, gpointer *user_data)
{
	GnumericPopupMenuElement const *elem =
		gtk_object_get_data (GTK_OBJECT (item), "descriptor");
	GnumericPopupMenuHandler handler =
		gtk_object_get_data (GTK_OBJECT (item), "handler");

	g_return_if_fail (elem != NULL);
	g_return_if_fail (handler != NULL);

	if (handler (elem, user_data))
		gtk_widget_destroy (gtk_widget_get_toplevel (item));
}

void
gnumeric_create_popup_menu (GnumericPopupMenuElement const *elements,
			    GnumericPopupMenuHandler handler,
			    gpointer user_data,
			    int display_filter, int sensitive_filter,
			    GdkEventButton *event)
{
	GtkWidget *menu, *item;
	int i;

	menu = gtk_menu_new ();

	for (i = 0; elements [i].name != NULL; i++) {
		char const * const name = elements [i].name;
		char const * const pix_name = elements [i].pixmap;

		item = NULL;

		if (elements [i].display_filter != 0 &&
		    !(elements [i].display_filter & display_filter))
			continue;

		if (name != NULL && *name != '\0') {
			/* ICK ! There should be a utility routine for this in gnome or gtk */
			GtkWidget *label;
			guint label_accel;

			label = gtk_accel_label_new ("");
			label_accel = gtk_label_parse_uline (
				GTK_LABEL (label), _(name));

			gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
			gtk_widget_show (label);

			item = gtk_pixmap_menu_item_new	();
			gtk_container_add (GTK_CONTAINER (item), label);

			if (label_accel != GDK_VoidSymbol) {
				gtk_widget_add_accelerator (
					item,
					"activate_item",
					gtk_menu_ensure_uline_accel_group (GTK_MENU (menu)),
					label_accel, 0,
					GTK_ACCEL_LOCKED);
			}

			if (elements [i].sensitive_filter != 0 &&
			    (elements [i].sensitive_filter & sensitive_filter))
				gtk_widget_set_sensitive (GTK_WIDGET (item), FALSE);
		} else {
			item = gtk_menu_item_new ();
			gtk_widget_set_sensitive (item, FALSE);
		}


		if (pix_name != NULL) {
			GtkWidget *pixmap =
				gnome_stock_pixmap_widget (GTK_WIDGET (item),
							   pix_name);
			gtk_widget_show (pixmap);
			gtk_pixmap_menu_item_set_pixmap (
				GTK_PIXMAP_MENU_ITEM (item),
				pixmap);
		}
		if (elements [i].index != 0) {
			gtk_signal_connect (
				GTK_OBJECT (item), "activate",
				GTK_SIGNAL_FUNC (&popup_item_activate),
				user_data);
			gtk_object_set_data (
				GTK_OBJECT (item), "descriptor", (gpointer)(elements + i));
			gtk_object_set_data (
				GTK_OBJECT (item), "handler", (gpointer)handler);
		}

		gtk_widget_show (item);
		gtk_menu_append (GTK_MENU (menu), item);
	}

	gnumeric_popup_menu (GTK_MENU (menu), event);
}
