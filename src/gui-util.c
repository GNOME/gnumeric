/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gnumeric-util.c:  Various GUI utility functions.
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 */
#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include "gnumeric.h"
#include "gui-util.h"

#include "workbook-control-gui-priv.h"
#include "gutils.h"
#include "parse-util.h"
#include "style.h"
#include "style-color.h"
#include "error-info.h"
#include "value.h"
#include "number-match.h"
#include "format.h"
#include "application.h"
#include "workbook.h"

#include <string.h>
#include <gal/widgets/widget-color-combo.h>
#include <glade/glade.h>

#ifdef WITH_BONOBO
#	include <bonobo.h>
#endif

#include <gdk/gdkkeysyms.h>
#include <libgnome/gnome-help.h>

gboolean
gnumeric_dialog_question_yes_no (WorkbookControlGUI *wbcg,
                                 gchar const *message,
                                 gboolean default_answer)
{
	GtkWidget *dialog = gtk_message_dialog_new (wbcg_toplevel (wbcg),
		GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_MESSAGE_QUESTION,
		GTK_BUTTONS_YES_NO,
		message);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog),
		default_answer ? GTK_RESPONSE_YES : GTK_RESPONSE_NO);
	return gnumeric_dialog_run (wbcg, GTK_DIALOG (dialog)) == GTK_RESPONSE_YES;
}
/*
 * TODO:
 * Get rid of trailing newlines /whitespace.
 */
void
gnumeric_notice (WorkbookControlGUI *wbcg, GtkMessageType type, char const *str)
{
	GtkWidget *dialog;

	dialog = gtk_message_dialog_new (wbcg == NULL ? NULL : wbcg_toplevel (wbcg),
                                         GTK_DIALOG_DESTROY_WITH_PARENT, type,
					 GTK_BUTTONS_OK, str);
	gtk_label_set_use_markup (GTK_LABEL (GTK_MESSAGE_DIALOG (dialog)->label), TRUE);

	gnumeric_dialog_run (wbcg, GTK_DIALOG (dialog));
}

void
gnumeric_notice_nonmodal (GtkWindow *parent, GtkWidget **ref, GtkMessageType type, char const *str)
{
	GtkWidget *dialog;

	if (*ref != NULL)
		gtk_widget_destroy (*ref);

	*ref = dialog = gtk_message_dialog_new (parent, GTK_DIALOG_DESTROY_WITH_PARENT, type,
					 GTK_BUTTONS_OK, str);

	gtk_signal_connect_object (GTK_OBJECT (dialog), "response",
				   GTK_SIGNAL_FUNC (gtk_widget_destroy),
				   GTK_OBJECT (dialog));
	g_signal_connect (G_OBJECT (dialog),
		"destroy",
		G_CALLBACK (gtk_widget_destroyed), ref);

	gtk_widget_show (dialog);

	return;
}


static void
fsel_dialog_finish (GtkWidget *widget)
{
	gtk_widget_hide (widget);
	gtk_main_quit ();
}

static void
fsel_dir_handle_ok (GtkWidget *widget, gboolean *result)
{
	GtkFileSelection *fsel;

	fsel = GTK_FILE_SELECTION (gtk_widget_get_ancestor (widget, GTK_TYPE_FILE_SELECTION));
	fsel_dialog_finish (GTK_WIDGET (fsel));
	*result = TRUE;
}

static void
fsel_handle_ok (GtkWidget *widget, gboolean *result)
{
	GtkFileSelection *fsel;
	gchar const *file_name;

	fsel = GTK_FILE_SELECTION (gtk_widget_get_ancestor (widget, GTK_TYPE_FILE_SELECTION));
	file_name = gtk_file_selection_get_filename (fsel);

	/* Change into directory if that's what user selected */
	if (g_file_test (file_name, G_FILE_TEST_IS_DIR)) {
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
	g_signal_connect (G_OBJECT (fsel->ok_button),
		"clicked",
		G_CALLBACK (fsel_handle_ok), &result);
	g_signal_connect (G_OBJECT (fsel->cancel_button),
		"clicked",
		G_CALLBACK (fsel_handle_cancel), NULL);
	g_signal_connect (G_OBJECT (fsel),
		"key_press_event",
		G_CALLBACK (fsel_key_event), NULL);
	g_signal_connect (G_OBJECT (fsel),
		"delete_event",
		G_CALLBACK (fsel_delete_event), NULL);
	gtk_widget_show_all (GTK_WIDGET (fsel));
	gtk_grab_add (GTK_WIDGET (fsel));
	gtk_main ();

	return result;
}

gboolean
gnumeric_dialog_dir_selection (WorkbookControlGUI *wbcg, GtkFileSelection *fsel)
{
	gboolean result = FALSE;

	gtk_window_set_modal (GTK_WINDOW (fsel), TRUE);
	gnumeric_set_transient (wbcg, GTK_WINDOW (fsel));
	g_signal_connect (G_OBJECT (fsel->ok_button),
		"clicked",
		G_CALLBACK (fsel_dir_handle_ok), &result);
	g_signal_connect (G_OBJECT (fsel->cancel_button),
		"clicked",
		G_CALLBACK (fsel_handle_cancel), NULL);
	g_signal_connect (G_OBJECT (fsel),
		"key_press_event",
		G_CALLBACK (fsel_key_event), NULL);
	g_signal_connect (G_OBJECT (fsel),
		"delete_event",
		G_CALLBACK (fsel_delete_event), NULL);
	gtk_widget_show_all (GTK_WIDGET (fsel));
	gtk_grab_add (GTK_WIDGET (fsel));
	gtk_main ();

	return result;
}

/**
 * gnumeric_dialog_run
 *
 * Pop up a dialog as child of a workbook.
 */
gint
gnumeric_dialog_run (WorkbookControlGUI *wbcg, GtkDialog *dialog)
{
	gint      result;

	g_return_val_if_fail (GTK_IS_DIALOG (dialog), GTK_RESPONSE_NONE);

	if (wbcg) {
		g_return_val_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg), GTK_RESPONSE_NONE);

		wbcg_set_transient (wbcg, GTK_WINDOW (dialog));
	}

	result = gtk_dialog_run (dialog);
	gtk_widget_destroy (GTK_WIDGET (dialog));
	return result;
}

#define ERROR_INFO_MAX_LEVEL 9
#define ERROR_INFO_TAG_NAME "errorinfotag%i"

static void
insert_error_info (GtkTextBuffer* text, ErrorInfo *error, gint level)
{
	gchar *message = (gchar *) error_info_peek_message (error);
	GSList *details_list, *l;
	GtkTextIter start, last;
	gchar *tag_name = g_strdup_printf (ERROR_INFO_TAG_NAME, 
					   MIN (level, ERROR_INFO_MAX_LEVEL)); 
	if (message == NULL)
		message = g_strdup (_("Multiple errors\n"));
	else 
		message = g_strdup_printf ("%s\n", message);
	gtk_text_buffer_get_bounds (text, &start, &last);
	gtk_text_buffer_insert_with_tags_by_name (text, &last,
						  message, -1,
						  tag_name, NULL);
	g_free (tag_name);
	g_free (message);
	details_list = error_info_peek_details (error);
	for (l = details_list; l != NULL; l = l->next) {
		ErrorInfo *detail_error = l->data;
		insert_error_info (text, detail_error, level + 1);
	}
	return;
}

/**
 * gnumeric_error_info_dialog_new
 *
 */
GtkWidget *
gnumeric_error_info_dialog_new (ErrorInfo *error)
{
	GtkWidget *dialog;
	GtkWidget *scrolled_window;
	GtkTextView *view;
	GtkTextBuffer *text;
	GtkMessageType mtype;
	gchar *message;
	gint bf_lim = 1;
	gint i;

	g_return_val_if_fail (error != NULL, NULL);

	message = (gchar *) error_info_peek_message (error);
	if (message == NULL)
		bf_lim++;

	mtype = GTK_MESSAGE_ERROR;
	if (error_info_peek_severity (error) < GNM_ERROR)
		mtype = GTK_MESSAGE_WARNING;
	dialog = gtk_message_dialog_new (NULL, GTK_DIALOG_DESTROY_WITH_PARENT,
					 mtype, GTK_BUTTONS_CLOSE, " ");
	gtk_widget_set_usize (dialog, 450, 250);
	scrolled_window = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window), 
					GTK_POLICY_AUTOMATIC,
					GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type
		(GTK_SCROLLED_WINDOW (scrolled_window),
		 GTK_SHADOW_ETCHED_IN);
	view = GTK_TEXT_VIEW (gtk_text_view_new ());
	gtk_text_view_set_wrap_mode (view, GTK_WRAP_WORD);
	gtk_text_view_set_editable (view, FALSE);
	gtk_text_view_set_cursor_visible (view, FALSE);

	gtk_text_view_set_pixels_below_lines
		(view, gtk_text_view_get_pixels_inside_wrap (view) + 3);
	text = gtk_text_view_get_buffer (view);
	for (i = ERROR_INFO_MAX_LEVEL; i-- > 0;) {
		gchar *tag_name = g_strdup_printf (ERROR_INFO_TAG_NAME, i);
		gtk_text_buffer_create_tag
			(text, tag_name,
			 "left_margin", i * 12,
			 "right_margin", i * 12,
			 "weight", ((i < bf_lim)
				    ? PANGO_WEIGHT_BOLD
				    : PANGO_WEIGHT_NORMAL),
			 NULL);
	}
	insert_error_info (text, error, 0);

	gtk_container_add (GTK_CONTAINER (scrolled_window), GTK_WIDGET (view));
	gtk_widget_show_all (GTK_WIDGET (scrolled_window));
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), scrolled_window, TRUE, TRUE, 0);

	return dialog;
}

/**
 * gnumeric_error_info_dialog_show
 *
 */
void
gnumeric_error_info_dialog_show (WorkbookControlGUI *wbcg, ErrorInfo *error)
{
	GtkWidget *dialog = gnumeric_error_info_dialog_new (error);
	gnumeric_set_transient (wbcg, GTK_WINDOW (dialog));
	gnumeric_dialog_run (wbcg, GTK_DIALOG (dialog));
}

static void
cb_parent_mapped (GtkWidget *parent, GtkWindow *window)
{
	if (GTK_WIDGET_MAPPED (window)) {
		gtk_window_present (window);
		gtk_signal_disconnect_by_func (GTK_OBJECT (parent),
			GTK_SIGNAL_FUNC (cb_parent_mapped),
			window);
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
/* FIXME:                                                                     */
/* 	GtkWindowPosition position = gnome_preferences_get_dialog_position(); */
	GtkWindowPosition position = GTK_WIN_POS_CENTER_ON_PARENT;

	g_return_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg));
	g_return_if_fail (GTK_IS_WINDOW (window));

	toplevel = wbcg_toplevel (wbcg);
	gtk_window_set_transient_for (window, toplevel);

	if (position == GTK_WIN_POS_NONE)
		position = GTK_WIN_POS_CENTER_ON_PARENT;
	gtk_window_set_position(window, position);

	if (!GTK_WIDGET_MAPPED (toplevel))
		g_signal_connect_after (G_OBJECT (toplevel),
			"map",
			G_CALLBACK (cb_parent_mapped), window);
}

typedef struct {
	WorkbookControlGUI *wbcg;
	GtkWidget	   *dialog;
	char const *key;
	gboolean freed;
} KeyedDialogContext;

static void
cb_free_keyed_dialog_context (KeyedDialogContext *ctxt)
{
	if (ctxt->freed)
		return;
	ctxt->freed = TRUE;

	g_return_if_fail (g_object_get_data (G_OBJECT (ctxt->wbcg), ctxt->key));
	g_object_set_data (G_OBJECT (ctxt->wbcg), ctxt->key, NULL);
	g_object_set_data (G_OBJECT (ctxt->wbcg), "KeyedDialog", NULL);
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
 * the object data is managed here.
 **/
void
gnumeric_keyed_dialog (WorkbookControlGUI *wbcg, GtkWindow *dialog, const char *key)
{
	KeyedDialogContext *ctxt;

	g_return_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg));
	g_return_if_fail (GTK_IS_WINDOW (dialog));
	g_return_if_fail (key != NULL);

	wbcg_set_transient (wbcg, dialog);

	ctxt = g_new (KeyedDialogContext, 1);
	ctxt->wbcg   = wbcg;
	ctxt->dialog = GTK_WIDGET (dialog);
	ctxt->key  = key;
	ctxt->freed = FALSE;
	g_object_set_data_full (G_OBJECT (wbcg),
		key, ctxt, (GDestroyNotify) cb_free_keyed_dialog_context);
	g_object_set_data_full (G_OBJECT (dialog),
		"KeyedDialog", ctxt, (GDestroyNotify) cb_free_keyed_dialog_context);
}

/**
 * gnumeric_dialog_raise_if_exists
 *
 * @wbcg    A WorkbookControlGUI
 * @key     A key to identify the dialog
 *
 * Raise the dialog identified by key if it is registered on the wbcg.
 * Returns TRUE if dialog found, FALSE if not.
 **/
gpointer
gnumeric_dialog_raise_if_exists (WorkbookControlGUI *wbcg, const char *key)
{
	KeyedDialogContext *ctxt;

	g_return_val_if_fail (wbcg != NULL, FALSE);
	g_return_val_if_fail (key != NULL, FALSE);

	/* Ensure we only pop up one copy per workbook */
	ctxt = g_object_get_data (G_OBJECT (wbcg), key);
	if (ctxt && GTK_IS_WINDOW (ctxt->dialog)) {
		gdk_window_raise (ctxt->dialog->window);
		return ctxt->dialog;
	} else
		return NULL;
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
void
gnumeric_editable_enters (GtkWindow *window, GtkWidget *w)
{
	g_return_if_fail (GTK_IS_WINDOW(window));

	/* because I really do not feel like changing all the calls to this routine */
	if (IS_GNUMERIC_EXPR_ENTRY (w))
		w = GTK_WIDGET (gnm_expr_entry_get_entry (GNUMERIC_EXPR_ENTRY (w)));

	g_signal_connect_swapped (G_OBJECT (w),
		"activate",
		G_CALLBACK (gtk_window_activate_default), window);
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
	g_return_if_fail (GTK_IS_WINDOW(window));
	g_return_if_fail (GTK_IS_COMBO (combo));

	gtk_combo_disable_activate (combo);
	gnumeric_editable_enters (window, GTK_WIDGET (combo->entry));
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

/**
 * gtk_button_stock_alignment_set :
 * @button :
 * ...
 *
 * gtk-1.3 hard codes the alignment of stock content to be .5,.5,0,0
 * this makes it hard to align stock icons within buttons.
 */
void
gtk_button_stock_alignment_set (GtkButton *button,
				gfloat     xalign,
				gfloat     yalign,
				gfloat     xscale,
				gfloat     yscale)
{
	g_return_if_fail (GTK_IS_BUTTON (button));

	gtk_alignment_set (GTK_ALIGNMENT (gtk_bin_get_child (GTK_BIN (button))),
		xalign, yalign, xscale, yscale);
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

static void
kill_popup_menu (GtkWidget *widget, GtkMenu *menu)
{
	g_return_if_fail (menu != NULL);
	g_return_if_fail (GTK_IS_MENU (menu));

	g_object_unref (G_OBJECT (menu));
}

void
gnumeric_popup_menu (GtkMenu *menu, GdkEventButton *event)
{
	g_return_if_fail (menu != NULL);
	g_return_if_fail (GTK_IS_MENU (menu));

	gtk_object_ref (GTK_OBJECT (menu));
	gtk_object_sink (GTK_OBJECT (menu));

	g_signal_connect (G_OBJECT (menu),
		"hide",
		G_CALLBACK (kill_popup_menu), menu);

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
	g_signal_handlers_disconnect_by_func (G_OBJECT (clist),
		G_CALLBACK (cb_clist_moveto), row);

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
		g_signal_connect (G_OBJECT (clist),
			"map",
			G_CALLBACK (cb_clist_moveto), GINT_TO_POINTER (row));
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
			rc_style->bg[i] = gs_yellow;
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
	char *f = g_build_filename (d, gladefile, NULL);
	gui = glade_xml_new (f, NULL, "gnumeric");

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

	g_signal_connect (G_OBJECT (dialog),
		"key-press-event",
		G_CALLBACK (cb_non_modal_dialog_keypress), NULL);
}

#ifdef WITH_BONOBO
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
		BONOBO_OBJREF (control),
		NULL);
	bonobo_object_unref (BONOBO_OBJECT (control));
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

static void
gnumeric_create_popup_menu_list (GSList *elements,
				 GnumericPopupMenuHandler handler,
				 gpointer user_data,
				 int display_filter,
				 int sensitive_filter,
				 GdkEventButton *event)
{
	GtkWidget *menu, *item;
	char const *trans;

	menu = gtk_menu_new ();

	for (; elements != NULL ; elements = elements->next) {
		GnumericPopupMenuElement const *element = elements->data;
		char const * const name = element->name;
		char const * const pix_name = element->pixmap;

		item = NULL;

		if (element->display_filter != 0 &&
		    !(element->display_filter & display_filter))
			continue;

		if (name != NULL && *name != '\0') {
			trans = _(name);
			item = gtk_image_menu_item_new_with_mnemonic (trans);
			if (element->sensitive_filter != 0 &&
			    (element->sensitive_filter & sensitive_filter))
				gtk_widget_set_sensitive (GTK_WIDGET (item), FALSE);
			if (pix_name != NULL) {
				GtkWidget *image = gtk_image_new_from_stock (pix_name,
                                        GTK_ICON_SIZE_MENU);
				gtk_widget_show (image);
				gtk_image_menu_item_set_image (
					GTK_IMAGE_MENU_ITEM (item),
					image);
			}
		} else {
			/* separator */
			item = gtk_menu_item_new ();
			gtk_widget_set_sensitive (item, FALSE);
		}

		if (element->index != 0) {
			g_signal_connect (G_OBJECT (item),
				"activate",
				G_CALLBACK (&popup_item_activate), user_data);
			gtk_object_set_data (
				GTK_OBJECT (item), "descriptor", (gpointer)(element));
			gtk_object_set_data (
				GTK_OBJECT (item), "handler", (gpointer)handler);
		}

		gtk_widget_show (item);
		gtk_menu_append (GTK_MENU (menu), item);
	}

	gnumeric_popup_menu (GTK_MENU (menu), event);
}

void
gnumeric_create_popup_menu (GnumericPopupMenuElement const *elements,
			    GnumericPopupMenuHandler handler,
			    gpointer user_data,
			    int display_filter, int sensitive_filter,
			    GdkEventButton *event)
{
	int i;
	GSList *tmp = NULL;

	for (i = 0; elements [i].name != NULL; i++)
		tmp = g_slist_prepend (tmp, (gpointer)(elements + i));

	tmp = g_slist_reverse (tmp);
	gnumeric_create_popup_menu_list (tmp, handler, user_data,
		display_filter, sensitive_filter, event);
	g_slist_free (tmp);
}

/**
 * color_combo_get_style_color :
 *
 * A utility wrapper to map between gal's colour combo and gnumeric's StyleColors.
 */
StyleColor *
color_combo_get_style_color (GtkWidget *color_combo)
{
	GdkColor *gdk = color_combo_get_color (COLOR_COMBO (color_combo), NULL);
	return (gdk == NULL) ? NULL
		    : style_color_new (gdk->red, gdk->green, gdk->blue);
}

GtkWidget *
gnumeric_toolbar_new (WorkbookControlGUI *wbcg,
		      GnomeUIInfo *info,
		      char const *name,
		      gint band_num,
		      gint band_position,
		      gint offset)
{
	GnomeApp *app = GNOME_APP (wbcg->toplevel);
	GtkWidget *tbar;
	BonoboDockItemBehavior behavior;

	g_return_val_if_fail (info != NULL, NULL);

	tbar = gtk_toolbar_new ();
	gtk_toolbar_set_orientation (GTK_TOOLBAR (tbar), GTK_ORIENTATION_HORIZONTAL);
	gnome_app_fill_toolbar_with_data (GTK_TOOLBAR (tbar), info,
		app->accel_group, wbcg);

	behavior = gconf_client_get_bool (application_get_gconf_client (),
		"/desktop/gnome/interface/toolbar_detachable", NULL)
		? BONOBO_DOCK_ITEM_BEH_NORMAL
		: BONOBO_DOCK_ITEM_BEH_LOCKED;

	gnome_app_add_toolbar (GNOME_APP (wbcg->toplevel), GTK_TOOLBAR (tbar),
		name, behavior, BONOBO_DOCK_TOP,
		band_num, band_position, offset);

	return tbar;
}

GtkWidget *
gnumeric_toolbar_get_widget (GtkToolbar *toolbar, int pos)
{
	GtkToolbarChild *child;
	GList *children;
	int i;

	g_return_val_if_fail (GTK_IS_TOOLBAR (toolbar), NULL);
	g_return_val_if_fail (pos > 0, NULL);

	children = GTK_TOOLBAR (toolbar)->children;

	if (!children)
		return NULL;

	i = 0;
	do {
		child = children->data;
		children = children->next;

		if (child->type == GTK_TOOLBAR_CHILD_SPACE)
			continue;

		if (i == pos)
			return child->widget;
		i++;
	} while (children);

	return NULL;
}

void
gnumeric_help_display (char const *link)
{
        g_return_if_fail (link != NULL);
	gnome_help_display ("gnumeric", link, NULL);
}


static void
cb_help (GtkWidget *button, char const *link)
{
	gnumeric_help_display (link);
}

void
gnumeric_init_help_button (GtkWidget *w, char const *link)
{
	GtkWidget *parent = gtk_widget_get_parent (w);
	if (GTK_IS_BUTTON_BOX (parent))
		gtk_button_box_set_child_secondary (GTK_BUTTON_BOX (parent),
						    w, TRUE);

	g_signal_connect (G_OBJECT (w),
		"clicked",
		G_CALLBACK (cb_help), (gpointer) link);
}

static void
gnumeric_help_pbox_goto (void *ignore, int ignore2, char const *link)
{
	gnumeric_help_display (link);
}

void
gnumeric_pbox_init_help (GtkWidget *dialog, char const *link)
{
	g_signal_connect (G_OBJECT (dialog),
		"help",
		G_CALLBACK (gnumeric_help_pbox_goto), (gpointer)link);
}

char *
gnumeric_textview_get_text (GtkTextView *text_view)
{
	GtkTextIter    start, end;
	GtkTextBuffer *buf = gtk_text_view_get_buffer (GTK_TEXT_VIEW (text_view));

	g_return_val_if_fail (buf != NULL, NULL);

	gtk_text_buffer_get_start_iter (buf, &start);
	gtk_text_buffer_get_end_iter (buf, &end);
	return gtk_text_buffer_get_text (buf, &start, &end, FALSE);
}

void
gnumeric_textview_set_text (GtkTextView *text_view, char const *txt)
{
	gtk_text_buffer_set_text (
		gtk_text_view_get_buffer (GTK_TEXT_VIEW (text_view)),
		txt, -1);
}

void
focus_on_entry (GtkEntry *entry)
{
	if (entry == NULL)
		return;
	gtk_widget_grab_focus (GTK_WIDGET(entry));
	gtk_editable_set_position (GTK_EDITABLE (entry), 0);
	gtk_entry_select_region (entry, 0, entry->text_length);
}

gboolean
entry_to_float_with_format_default (GtkEntry *entry, gnm_float *the_float, gboolean update, 
				    StyleFormat *format, gnm_float num)
{
	char const *text = gtk_entry_get_text (entry);
	gboolean need_default = (text == NULL);

	if (!need_default) {
		char *new_text = g_strdup (text);
		need_default = (0 ==  strlen (g_strstrip(new_text)));
		g_free (new_text);
	}

	if (need_default && !update) {
		*the_float = num;
		return FALSE;
	}

	if (need_default)
		float_to_entry (entry, num);

	return entry_to_float_with_format (entry, the_float, update, format);
}

gboolean
entry_to_float_with_format (GtkEntry *entry, gnm_float *the_float, gboolean update, 
			    StyleFormat *format)
{
	Value *value = format_match_number (gtk_entry_get_text (entry), format);

	if ((value == NULL) || !VALUE_IS_NUMBER (value)) {
		*the_float = 0.0;
		return TRUE;
	}
	*the_float = value_get_as_float (value);
	if (update) {
		char *tmp = format_value (format, value, NULL, 16);
		gtk_entry_set_text (entry, tmp);
		g_free (tmp);
	}

	value_release (value);
	return FALSE;
}

/**
 * entry_to_int:
 * @entry:
 * @the_int:
 * @update:
 *
 * retrieve an int from an entry field parsing all reasonable formats
 *
 **/
gboolean
entry_to_int (GtkEntry *entry, gint *the_int, gboolean update)
{
	Value *value = format_match_number (gtk_entry_get_text (entry), NULL);

	if ((value == NULL) || !(value->type == VALUE_INTEGER)) {
		*the_int = 0;
		return TRUE;
	}
	*the_int = value_get_as_int (value);
	if (update) {
		char *tmp = format_value (NULL, value, NULL, 16);
		gtk_entry_set_text (entry, tmp);
		g_free (tmp);
	}

	value_release (value);
	return FALSE;
}

/**
 * float_to_entry:
 * @entry:
 * @the_float:
 *
 **/
void
float_to_entry (GtkEntry *entry, gnm_float the_float)
{
	char        *text      = NULL;
	Value       *val = NULL;

	val = value_new_float (the_float);
	text = format_value (NULL, val, NULL, 16);
	if (text) {
		gtk_entry_set_text (entry, text);
		g_free (text);
	}
	if (val)
		value_release(val);
}

/**
 * int_to_entry:
 * @entry:
 * @the_float:
 *
 *
  **/
void
int_to_entry (GtkEntry *entry, gint the_int)
{
	char        *text      = NULL;
	Value       *val = NULL;

	val = value_new_int (the_int);
	text = format_value (NULL, val, NULL, 16);
	if (text) {
		gtk_entry_set_text (entry, text);
		g_free (text);
	}
	if (val)
		value_release(val);
	return;
}

GtkWidget *
gnumeric_load_image (char const *filename)
{
	GtkWidget *image;
	char *path;

	path = g_strconcat (GNUMERIC_ICONDIR "/", filename, NULL);
	image = gtk_image_new_from_file (path);
	g_free (path);

	if (image)
		gtk_widget_show (image);

	return image;
}

/**
 * gnumeric_load_pixbuf : utility routine to create pixbufs from file named @name.
 * looking in the gnumeric icondir.
 **/
GdkPixbuf *
gnumeric_load_pixbuf (char const *filename)
{
	GdkPixbuf *pixbuf;
	char *path;

	path = g_strconcat (GNUMERIC_ICONDIR "/", filename, NULL);
	pixbuf = gdk_pixbuf_new_from_file (path, NULL);
	g_free (path);
	return pixbuf;
}
