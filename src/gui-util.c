/*
 * gui-util.c:  Various GUI utility functions.
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <libgnumeric.h>
#include <gui-util.h>

#include <gutils.h>
#include <parse-util.h>
#include <style.h>
#include <style-color.h>
#include <value.h>
#include <number-match.h>
#include <gnm-format.h>
#include <application.h>
#include <workbook.h>
#include <libgnumeric.h>
#include <wbc-gtk.h>
#include <widgets/gnm-expr-entry.h>

#include <goffice/goffice.h>
#include <atk/atkrelation.h>
#include <atk/atkrelationset.h>
#include <gdk/gdkkeysyms.h>

#include <string.h>

#define ERROR_INFO_MAX_LEVEL 9
#define ERROR_INFO_TAG_NAME "errorinfotag%i"

static void
insert_error_info (GtkTextBuffer* text, GOErrorInfo *error, gint level)
{
	gchar *message = (gchar *) go_error_info_peek_message (error);
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
	details_list = go_error_info_peek_details (error);
	for (l = details_list; l != NULL; l = l->next) {
		GOErrorInfo *detail_error = l->data;
		insert_error_info (text, detail_error, level + 1);
	}
	return;
}

/**
 * gnumeric_go_error_info_list_dialog_create:
 * @errs: (element-type GOErrorInfo):
 *
 * SHOULD BE IN GOFFICE
 * Returns: (transfer full): the newly allocated dialog.
 */
static GtkWidget *
gnumeric_go_error_info_list_dialog_create (GSList *errs)
{
	GtkWidget *dialog;
	GtkWidget *scrolled_window;
	GtkTextView *view;
	GtkTextBuffer *text;
	GtkMessageType mtype;
	gint bf_lim = 1;
	gint i;
	GdkScreen *screen;
	GSList *l, *lf;
	int severity = 0, this_severity;
	gboolean message_null = TRUE;

	for (l = errs; l != NULL; l = l->next) {
		GOErrorInfo *err = l->data;
		if (go_error_info_peek_message (err)!= NULL)
			message_null = FALSE;
		this_severity = go_error_info_peek_severity (err);
		if (this_severity > severity)
			severity = this_severity;
	}
	lf = g_slist_copy (errs);
	lf = g_slist_reverse (lf);

	if (message_null)
		bf_lim++;

	mtype = GTK_MESSAGE_ERROR;
	if (severity < GO_ERROR)
		mtype = GTK_MESSAGE_WARNING;
	dialog = gtk_message_dialog_new (NULL, GTK_DIALOG_DESTROY_WITH_PARENT,
					 mtype, GTK_BUTTONS_CLOSE, " ");
	screen = gtk_widget_get_screen (dialog);
	gtk_widget_set_size_request (dialog,
				     gdk_screen_get_width (screen) / 3,
				     gdk_screen_get_width (screen) / 4);
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
		g_free (tag_name);
	}
	for (l = lf; l != NULL; l = l->next) {
		GOErrorInfo *err = l->data;
		insert_error_info (text, err, 0);
	}
	g_slist_free (lf);

	gtk_container_add (GTK_CONTAINER (scrolled_window), GTK_WIDGET (view));
	gtk_widget_show_all (GTK_WIDGET (scrolled_window));
	gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))), scrolled_window, TRUE, TRUE, 0);

	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_CLOSE);
	return dialog;
}

/**
 * gnm_go_error_info_dialog_create:
 *
 * SHOULD BE IN GOFFICE
 * Returns: (transfer full): the newly allocated dialog.
 */
GtkWidget *
gnm_go_error_info_dialog_create (GOErrorInfo *error)
{
	GSList *l = g_slist_append (NULL, error);
	GtkWidget *w = gnumeric_go_error_info_list_dialog_create (l);
	g_slist_free (l);
	return w;
}

/**
 * gnm_go_error_info_dialog_show:
 *
 */
void
gnm_go_error_info_dialog_show (GtkWindow *parent, GOErrorInfo *error)
{
	GtkWidget *dialog = gnm_go_error_info_dialog_create (error);
	go_gtk_dialog_run (GTK_DIALOG (dialog), parent);
}

/**
 * gnm_go_error_info_list_dialog_show:
 * @parent:
 * @errs: (element-type GOErrorInfo):
 *
 */
void
gnm_go_error_info_list_dialog_show (GtkWindow *parent,
					 GSList *errs)
{
	GtkWidget *dialog = gnumeric_go_error_info_list_dialog_create (errs);
	go_gtk_dialog_run (GTK_DIALOG (dialog), parent);
}


typedef struct {
	WBCGtk *wbcg;
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

	if (ctxt->wbcg) {
		WBCGtk *wbcg = ctxt->wbcg;
		ctxt->wbcg = NULL;
		g_object_set_data (G_OBJECT (wbcg), ctxt->key, NULL);
	}

	g_free (ctxt);
}

static void
cb_keyed_dialog_destroy (GtkDialog *dialog, KeyedDialogContext *ctxt)
{
	/*
	 * gtk-builder likes to hold refs on objects.  That interferes
	 * with the way we handle finalization of dialogs' state.
	 * Trigger this now.
	 */
	g_object_set_data (G_OBJECT (dialog), "state", NULL);

	ctxt->dialog = NULL;

	if (ctxt->wbcg) {
		WBCGtk *wbcg = ctxt->wbcg;
		ctxt->wbcg = NULL;
		g_object_set_data (G_OBJECT (wbcg), ctxt->key, NULL);
	}
}

static gint
cb_keyed_dialog_keypress (GtkWidget *dialog, GdkEventKey *event,
			  G_GNUC_UNUSED gpointer user)
{
	if (event->keyval == GDK_KEY_Escape) {
		gtk_widget_destroy (GTK_WIDGET (dialog));
		return TRUE;
	}
	return FALSE;
}

#define SAVE_SIZES_SCREEN_KEY "geometry-hash"

static gboolean debug_dialog_size;

static void
cb_save_sizes (GtkWidget *dialog,
	       const GtkAllocation *allocation,
	       const char *key)
{
	GdkRectangle *r;
	GdkScreen *screen = gtk_widget_get_screen (dialog);
	GHashTable *h = g_object_get_data (G_OBJECT (screen),
					   SAVE_SIZES_SCREEN_KEY);
	GdkWindow *window = gtk_widget_get_window (dialog);

	if (!h) {
		h = g_hash_table_new_full (g_str_hash, g_str_equal,
					   (GDestroyNotify)g_free,
					   (GDestroyNotify)g_free);
		/*
		 * We hang this on the screen because pixel sizes make
		 * no sense across screens.
		 *
		 * ANYONE WHO CHANGES THIS CODE TO SAVE THESE SIZES ON EXIT
		 * AND RELOADS THEM ON STARTUP WILL GET TARRED AND FEATHERED.
		 * -- MW, 20071113
		 */
		g_object_set_data_full (G_OBJECT (screen),
					SAVE_SIZES_SCREEN_KEY, h,
					(GDestroyNotify)g_hash_table_destroy);
	}

	r = go_memdup (allocation, sizeof (*allocation));
	if (window)
		gdk_window_get_position (gtk_widget_get_window (dialog), &r->x, &r->y);

	if (debug_dialog_size) {
		g_printerr ("Saving %s to %dx%d at (%d,%d)\n",
			    key, r->width, r->height, r->x, r->y);
	}
	g_hash_table_replace (h, g_strdup (key), r);
}

void
gnm_restore_window_geometry (GtkWindow *dialog, const char *key)
{
	GtkWidget *top = gtk_widget_get_toplevel (GTK_WIDGET (dialog));
	GdkScreen *screen = gtk_widget_get_screen (GTK_WIDGET (dialog));
	GHashTable *h = g_object_get_data (G_OBJECT (screen), SAVE_SIZES_SCREEN_KEY);
	GdkRectangle *allocation = h ? g_hash_table_lookup (h, key) : NULL;

	debug_dialog_size = gnm_debug_flag ("dialog-size");

	if (allocation) {
		if (debug_dialog_size)
			g_printerr ("Restoring %s to %dx%d at (%d,%d)\n",
				    key, allocation->width, allocation->height,
				    allocation->x, allocation->y);

		gtk_window_move
			(GTK_WINDOW (top),
			 allocation->x, allocation->y);
		gtk_window_set_default_size
			(GTK_WINDOW (top),
			 allocation->width, allocation->height);
	}

	g_signal_connect (G_OBJECT (dialog), "size-allocate",
			  G_CALLBACK (cb_save_sizes),
			  (gpointer)key);
}

/**
 * gnm_keyed_dialog:
 * @wbcg:    A WBCGtk
 * @dialog:  A transient window
 * @key:     A key to identify the dialog
 *
 * Make dialog a transient child of wbcg, attaching to wbcg object data to
 * identify the dialog. The object data makes it possible to ensure that
 * only one dialog of a kind can be displayed for a wbcg. Deallocation of
 * the object data is managed here.
 **/
void
gnm_keyed_dialog (WBCGtk *wbcg, GtkWindow *dialog, char const *key)
{
	KeyedDialogContext *ctxt;

	g_return_if_fail (GNM_IS_WBC_GTK (wbcg));
	g_return_if_fail (GTK_IS_WINDOW (dialog));
	g_return_if_fail (key != NULL);

	wbcg_set_transient (wbcg, dialog);

	go_dialog_guess_alternative_button_order (GTK_DIALOG (dialog));

	ctxt = g_new (KeyedDialogContext, 1);
	ctxt->wbcg = wbcg;
	ctxt->dialog = GTK_WIDGET (dialog);
	ctxt->key = key;
	ctxt->freed = FALSE;
	g_object_set_data_full (G_OBJECT (wbcg), key, ctxt,
				(GDestroyNotify)cb_free_keyed_dialog_context);
	g_signal_connect (G_OBJECT (dialog), "key_press_event",
			  G_CALLBACK (cb_keyed_dialog_keypress), NULL);
	g_signal_connect (G_OBJECT (dialog), "destroy",
			  G_CALLBACK (cb_keyed_dialog_destroy), ctxt);

	gnm_restore_window_geometry (dialog, key);
}

/**
 * gnm_dialog_raise_if_exists:
 * @wbcg:    A WBCGtk
 * @key:     A key to identify the dialog
 *
 * Raise the dialog identified by key if it is registered on the wbcg.
 *
 * Returns: (transfer none) (type GtkDialog) (nullable): existing dialog
 **/
gpointer
gnm_dialog_raise_if_exists (WBCGtk *wbcg, char const *key)
{
	KeyedDialogContext *ctxt;

	g_return_val_if_fail (wbcg != NULL, NULL);
	g_return_val_if_fail (key != NULL, NULL);

	/* Ensure we only pop up one copy per workbook */
	ctxt = g_object_get_data (G_OBJECT (wbcg), key);
	if (ctxt && ctxt->dialog && GTK_IS_WINDOW (ctxt->dialog)) {
		gdk_window_raise (gtk_widget_get_window (ctxt->dialog));
		return ctxt->dialog;
	} else
		return NULL;
}

static gboolean
cb_activate_default (GtkWindow *window)
{
	GtkWidget *dw = gtk_window_get_default_widget (window);
	/*
	 * gtk_window_activate_default has a bad habit of trying
	 * to activate the focus widget.
	 */
	return dw && gtk_widget_is_sensitive (dw) &&
		gtk_window_activate_default (window);
}


/**
 * gnm_editable_enters:
 * @window: dialog to affect.
 * @editable: Editable to affect.
 *
 * Make the "activate" signal of an editable click the default dialog button.
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
gnm_editable_enters (GtkWindow *window, GtkWidget *w)
{
	g_return_if_fail (GTK_IS_WINDOW(window));

	/* because I really do not feel like changing all the calls to this routine */
	if (GNM_EXPR_ENTRY_IS (w))
		w = GTK_WIDGET (gnm_expr_entry_get_entry (GNM_EXPR_ENTRY (w)));

	g_signal_connect_swapped (G_OBJECT (w),
		"activate",
		G_CALLBACK (cb_activate_default), window);
}

/**
 * gnm_gtk_radio_group_get_selected:
 * @radio_group: (element-type GtkRadioButton): list of radio buttons.
 *
 * Returns: the index of the selected radio button starting from list end.
 **/
int
gnm_gtk_radio_group_get_selected (GSList *radio_group)
{
	GSList *l;
	int i, c;

	g_return_val_if_fail (radio_group != NULL, 0);

	c = g_slist_length (radio_group);

	for (i = 0, l = radio_group; l; l = l->next, i++){
		GtkRadioButton *button = l->data;

		if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button)))
			return c - i - 1;
	}

	return 0;
}


int
gnm_gui_group_value (gpointer gui, char const * const group[])
{
	int i;
	for (i = 0; group[i]; i++) {
		GtkWidget *w = go_gtk_builder_get_widget (gui, group[i]);
		if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w)))
			return i;
	}
	return -1;
}

static gboolean
cb_delayed_destroy (gpointer w)
{
	gtk_widget_destroy (gtk_widget_get_toplevel (w));
	gtk_widget_destroy (w);
	g_object_unref (w);
	return FALSE;
}

static void
kill_popup_menu (GtkWidget *widget, G_GNUC_UNUSED gpointer user)
{
	/* gtk+ currently gets unhappy if we destroy here, see bug 725142 */
	g_idle_add (cb_delayed_destroy, widget);
}

/**
 * gnumeric_popup_menu:
 * @menu: #GtkMenu
 * @event: (nullable): #GdkEvent
 *
 * Bring up a popup and if @event is non-%NULL ensure that the popup is on the
 * right screen.
 **/
void
gnumeric_popup_menu (GtkMenu *menu, GdkEvent *event)
{
	g_return_if_fail (menu != NULL);
	g_return_if_fail (GTK_IS_MENU (menu));

	if (event)
		gtk_menu_set_screen (menu, gdk_event_get_screen (event));

	g_object_ref_sink (menu);
	g_signal_connect (G_OBJECT (menu),
			  "hide",
			  G_CALLBACK (kill_popup_menu), NULL);

	/* Do NOT pass the button used to create the menu.
	 * instead pass 0.  Otherwise bringing up a menu with
	 * the right button will disable clicking on the menu with the left.
	 */
	gtk_menu_popup (menu, NULL, NULL, NULL, NULL, 0,
			(event
			 ? gdk_event_get_time (event)
			 : gtk_get_current_event_time()));
}

static void
gnumeric_tooltip_set_style (GtkWidget *widget)
{
	gtk_style_context_add_class (gtk_widget_get_style_context (widget),
				     GTK_STYLE_CLASS_TOOLTIP);
	gtk_style_context_add_class (gtk_widget_get_style_context (widget),
				     "pseudo-tooltip");
	if (GTK_IS_CONTAINER (widget))
		gtk_container_forall (GTK_CONTAINER (widget),
				      (GtkCallback) (gnumeric_tooltip_set_style),
				      NULL);
}

/**
 * gnm_convert_to_tooltip:
 * @ref_widget:
 * @widget:
 *
 * Returns: (transfer none): @widget
 **/
GtkWidget *
gnm_convert_to_tooltip (GtkWidget *ref_widget, GtkWidget *widget)
{
	GtkWidget *tip, *frame;
	GdkScreen *screen = gtk_widget_get_screen (ref_widget);
	GtkWidget *toplevel = gtk_widget_get_toplevel (ref_widget);

	tip = gtk_window_new (GTK_WINDOW_POPUP);
	gtk_window_set_type_hint (GTK_WINDOW (tip),
				  GDK_WINDOW_TYPE_HINT_TOOLTIP);
	gtk_window_set_resizable (GTK_WINDOW (tip), FALSE);
	gtk_window_set_gravity (GTK_WINDOW (tip), GDK_GRAVITY_NORTH_WEST);
	gtk_window_set_screen (GTK_WINDOW (tip), screen);
	gtk_widget_set_name (tip, "gtk-tooltip");
	gtk_window_set_transient_for (GTK_WINDOW (tip), GTK_WINDOW (toplevel));

	frame = gtk_frame_new (NULL);
	gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_NONE);
	gtk_widget_show (frame);
	gtk_container_add (GTK_CONTAINER (frame), widget);
	gtk_container_add (GTK_CONTAINER (tip), frame);

	gnumeric_tooltip_set_style (tip);

	return widget;
}

/**
 * gnm_create_tooltip:
 *
 * Returns: (transfer full): the newly allocated #GtkWidget.
 **/
GtkWidget *
gnm_create_tooltip (GtkWidget *ref_widget)
{
	return gnm_convert_to_tooltip (ref_widget, gtk_label_new (""));
}

void
gnm_position_tooltip (GtkWidget *tip, int px, int py, gboolean horizontal)
{
	GtkRequisition req;

	gtk_widget_get_preferred_size (tip, &req, NULL);

	if (horizontal){
		px -= req.width / 2;
		py -= req.height + 20;
	} else {
		px -= req.width + 20;
		py -= req.height / 2;
	}

	if (px < 0)
		px = 0;
	if (py < 0)
		py = 0;

	gtk_window_move (GTK_WINDOW (gtk_widget_get_toplevel (tip)), px, py);
}

/**
 * gnm_gtk_builder_load:
 * @cc: #GOCmdContext
 * @uifile:
 *
 * Simple utility to open ui files
 * Returns: (transfer full): the newly allocated #GtkBuilder.
 **/
GtkBuilder *
gnm_gtk_builder_load (char const *uifile, char const *domain, GOCmdContext *cc)
{
	GtkBuilder *gui;
	char *f;

	if (strncmp (uifile, "res:", 4) == 0) {
		f = g_strconcat ("res:/org/gnumeric/gnumeric/",
				 uifile + 4,
				 NULL);
	} else if (g_path_is_absolute (uifile)) {
		f = g_strdup (uifile);
	} else {
		f = g_strconcat ("res:gnm:", uifile, NULL);
	}

	gui = go_gtk_builder_load (f, domain, cc);
	g_free (f);

	return gui;
}

static void
popup_item_activate (GtkWidget *item, GnmPopupMenuElement const *elem)
{
	GtkWidget *menu;
	GnmPopupMenuHandler handler;
	gpointer user_data;

	// Go to top-level menu.  This shouldn't be that hard.
	menu = item;
	while (TRUE) {
		if (GTK_IS_MENU_ITEM (menu))
			menu = gtk_widget_get_parent (menu);
		else if (GTK_IS_MENU (menu)) {
			GtkWidget *a = gtk_menu_get_attach_widget (GTK_MENU (menu));
			if (a)
				menu = a;
			else
				break;
		} else
			break;
	}
	handler = g_object_get_data (G_OBJECT (menu), "handler");
	user_data = g_object_get_data (G_OBJECT (menu), "user-data");
	g_return_if_fail (handler != NULL);

	handler (elem, user_data);
}

/**
 * gnm_create_popup_menu:
 * @elements:
 * @handler: (scope notified):
 * @user_data: user data to pass to @handler.
 * @notify: destroy notification for @user_data
 * @display_filter:
 * @sensitive_filter:
 * @event:
 **/
void
gnm_create_popup_menu (GnmPopupMenuElement const *elements,
		       GnmPopupMenuHandler handler,
		       gpointer user_data,
		       GDestroyNotify notify,
		       int display_filter, int sensitive_filter,
		       GdkEvent *event)
{
	char const *trans;
	GSList *menu_stack = NULL;
	GtkWidget *menu, *item;

	menu = gtk_menu_new ();
	g_object_set_data (G_OBJECT (menu), "handler", (gpointer)handler);
	g_object_set_data_full (G_OBJECT (menu), "user-data", user_data, notify);
	for (; NULL != elements->name ; elements++) {
		char const * const name = elements->name;
		char const * const pix_name = elements->pixmap;

		item = NULL;

		if (elements->display_filter != 0 &&
		    !(elements->display_filter & display_filter)) {
			if (elements->allocated_name) {
				g_free (elements->allocated_name);
				*(gchar **)(&elements->allocated_name) = NULL;
			}
			continue;
		}

		if (name != NULL && *name != '\0') {
			if (elements->allocated_name)
				trans = elements->allocated_name;
			else
				trans = _(name);
			item = gtk_image_menu_item_new_with_mnemonic (trans);
			if (elements->sensitive_filter != 0 &&
			    (elements->sensitive_filter & sensitive_filter))
				gtk_widget_set_sensitive (GTK_WIDGET (item), FALSE);
			if (pix_name != NULL) {
				GtkWidget *image = gtk_image_new_from_icon_name (pix_name,
										 GTK_ICON_SIZE_MENU);
				gtk_widget_show (image);
				gtk_image_menu_item_set_image (
					GTK_IMAGE_MENU_ITEM (item),
					image);
			}
			if (elements->allocated_name) {
				g_free (elements->allocated_name);
				*(gchar **)(&elements->allocated_name) = NULL;
			}
		} else if (elements->index >= 0) {
			/* separator */
			item = gtk_separator_menu_item_new ();
		}

		if (elements->index > 0) {
			g_signal_connect (G_OBJECT (item),
					  "activate",
					  G_CALLBACK (popup_item_activate),
					  (gpointer)elements);
		}
		if (NULL != item) {
			gtk_widget_show (item);
			gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
		}
	      	if (elements->index < 0) {
			if (NULL != item) {
				menu_stack = g_slist_prepend (menu_stack, menu);
				menu = gtk_menu_new ();
				gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), menu);
			} else {
				menu = menu_stack->data;
				menu_stack = g_slist_remove (menu_stack, menu);
			}
		}
	}
	gnumeric_popup_menu (GTK_MENU (menu), event);
}

void
gnm_init_help_button (GtkWidget *w, char const *lnk)
{
	go_gtk_help_button_init (w, gnm_sys_data_dir (), "gnumeric", lnk);
}

char *
gnm_textbuffer_get_text (GtkTextBuffer *buf)
{
	GtkTextIter    start, end;

	g_return_val_if_fail (buf != NULL, NULL);

	gtk_text_buffer_get_start_iter (buf, &start);
	gtk_text_buffer_get_end_iter (buf, &end);
	/* We are using slice rather than text so that the tags still match */
	return gtk_text_buffer_get_slice (buf, &start, &end, FALSE);
}

char *
gnm_textview_get_text (GtkTextView *text_view)
{
	return gnm_textbuffer_get_text
		(gtk_text_view_get_buffer (GTK_TEXT_VIEW (text_view)));
}

void
gnm_textview_set_text (GtkTextView *text_view, char const *txt)
{
	gtk_text_buffer_set_text (
		gtk_text_view_get_buffer (GTK_TEXT_VIEW (text_view)),
		txt, -1);
}

void
gnm_load_pango_attributes_into_buffer (PangoAttrList  *markup, GtkTextBuffer *buffer, gchar const *str)
{
	gchar *str_retrieved = NULL;

	if (str == NULL) {
		GtkTextIter start, end;
		gtk_text_buffer_get_start_iter (buffer, &start);
		gtk_text_buffer_get_end_iter (buffer, &end);
		str = str_retrieved = gtk_text_buffer_get_slice
			(buffer, &start, &end, TRUE);
	}

	go_load_pango_attributes_into_buffer (markup, buffer, str);

	g_free (str_retrieved);
}

#define gnmstoretexttagattrinpangoint(nameset, name, gnm_pango_attr_new)  \
	if (gnm_object_get_bool (tag, nameset)) {			  \
		int value;                                                \
		g_object_get (G_OBJECT (tag), name, &value, NULL);        \
		attr =  gnm_pango_attr_new (value);                       \
		attr->start_index = x;                                    \
		attr->end_index = y;                                      \
		pango_attr_list_change (list, attr);                      \
	}


static void
gnm_store_text_tag_attr_in_pango (PangoAttrList *list, GtkTextTag *tag, GtkTextIter *start, gchar const *text)
{
	GtkTextIter end = *start;
	gint x, y;
	PangoAttribute * attr;

	gtk_text_iter_forward_to_tag_toggle (&end, tag);
	x = g_utf8_offset_to_pointer (text, gtk_text_iter_get_offset (start)) - text;
	y = g_utf8_offset_to_pointer (text, gtk_text_iter_get_offset (&end)) - text;

	if (gnm_object_get_bool (tag, "foreground-set")) {
		GdkRGBA *color = NULL;
		g_object_get (G_OBJECT (tag), "foreground-rgba", &color, NULL);
		if (color) {
			/* dividing 0 to 1 into 65536 equal length intervals */
			attr =  pango_attr_foreground_new
				((int)(CLAMP (color->red * 65536, 0., 65535.)),
				 (int)(CLAMP (color->green * 65536, 0., 65535.)),
				 (int)(CLAMP (color->blue * 65536, 0., 65535.)));
			gdk_rgba_free (color);
			attr->start_index = x;
			attr->end_index = y;
			pango_attr_list_change (list, attr);
		}
	}

	gnmstoretexttagattrinpangoint ("style-set", "style", pango_attr_style_new)
	gnmstoretexttagattrinpangoint ("weight-set", "weight", pango_attr_weight_new)
	gnmstoretexttagattrinpangoint ("strikethrough-set", "strikethrough", pango_attr_strikethrough_new)
	gnmstoretexttagattrinpangoint ("underline-set", "underline", pango_attr_underline_new)
	gnmstoretexttagattrinpangoint ("rise-set", "rise", pango_attr_rise_new)
}

#undef gnmstoretexttagattrinpangoint

PangoAttrList *
gnm_get_pango_attributes_from_buffer (GtkTextBuffer *buffer)
{
	PangoAttrList *list = pango_attr_list_new ();
	GtkTextIter start;
	gchar *text = gnm_textbuffer_get_text (buffer);

	gtk_text_buffer_get_start_iter (buffer, &start);

	while (!gtk_text_iter_is_end (&start)) {
		if (gtk_text_iter_begins_tag (&start, NULL)) {
			GSList *ptr, *l = gtk_text_iter_get_toggled_tags (&start, TRUE);
			for (ptr = l; ptr; ptr = ptr->next)
				gnm_store_text_tag_attr_in_pango (list, ptr->data, &start, text);
		}
		gtk_text_iter_forward_to_tag_toggle (&start, NULL);
	}

	g_free (text);

	return list;
}

void
focus_on_entry (GtkEntry *entry)
{
	if (entry == NULL)
		return;
	gtk_widget_grab_focus (GTK_WIDGET(entry));
	gtk_editable_set_position (GTK_EDITABLE (entry), 0);
	gtk_editable_select_region (GTK_EDITABLE (entry), 0,
				    gtk_entry_get_text_length (entry));
}

gboolean
entry_to_float_with_format_default (GtkEntry *entry, gnm_float *the_float,
				    gboolean update,
				    GOFormat const *format, gnm_float num)
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
entry_to_float_with_format (GtkEntry *entry, gnm_float *the_float,
			    gboolean update, GOFormat const *format)
{
	GnmValue *value = format_match_number (gtk_entry_get_text (entry), format, NULL);

	*the_float = 0.0;
	if (!value)
		return TRUE;

	*the_float = value_get_as_float (value);
	if (update) {
		char *tmp;

		if (!format || go_format_is_general (format))
			tmp = value_get_as_string (value);
		else
			tmp = format_value (format, value, -1, NULL);

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
 * Retrieve an int from an entry field parsing all reasonable formats
 *
 **/
gboolean
entry_to_int (GtkEntry *entry, gint *the_int, gboolean update)
{
	GnmValue *value = format_match_number (gtk_entry_get_text (entry), NULL, NULL);
	gnm_float f;

	*the_int = 0;
	if (!value)
		return TRUE;

	f = value_get_as_float (value);
	if (f < INT_MIN || f > INT_MAX || f != (*the_int = (int)f)) {
		value_release (value);
		return TRUE;
	}

	if (update) {
		char *tmp = format_value (NULL, value, 16, NULL);
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
	GnmValue *val = value_new_float (the_float);
	char *text = format_value (NULL, val, 16, NULL);
	value_release(val);
	if (text != NULL) {
		gtk_entry_set_text (entry, text);
		g_free (text);
	}
}

/**
 * int_to_entry:
 * @entry:
 * @the_int:
 *
 *
  **/
void
int_to_entry (GtkEntry *entry, gint the_int)
{
	GnmValue *val  = value_new_int (the_int);
	char *text = format_value (NULL, val, 16, NULL);
	value_release(val);
	if (text != NULL) {
		gtk_entry_set_text (entry, text);
		g_free (text);
	}
}

static void
cb_focus_to_entry (GtkWidget *button, GtkWidget *entry)
{
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button)))
		gtk_widget_grab_focus (entry);
}

static gboolean
cb_activate_button (GtkWidget *button)
{
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), TRUE);
	return FALSE;
}

void
gnm_link_button_and_entry (GtkWidget *button, GtkWidget *entry)
{
	g_signal_connect (G_OBJECT (button),
			  "clicked", G_CALLBACK (cb_focus_to_entry),
			  entry);
	g_signal_connect_swapped (G_OBJECT (entry),
			  "focus_in_event",
			  G_CALLBACK (cb_activate_button),
			  button);
}

/* ------------------------------------------------------------------------- */

void
gnm_widget_set_cursor (GtkWidget *w, GdkCursor *cursor)
{
	gdk_window_set_cursor (gtk_widget_get_window (w), cursor);
}

void
gnm_widget_set_cursor_type (GtkWidget *w, GdkCursorType ct)
{
	GdkDisplay *display = gtk_widget_get_display (w);
	GdkCursor *cursor = gdk_cursor_new_for_display (display, ct);
	gnm_widget_set_cursor (w, cursor);
	g_object_unref (cursor);
}

/* ------------------------------------------------------------------------- */

/**
 * gnm_message_dialog_create:
 *
 * A convenience function to build HIG compliant message dialogs.
 *
 *   parent : transient parent, or %NULL for none.
 *   flags
 *   type : type of dialog
 *   primary_message : message displayed in bold
 *   secondary_message : message displayed below
 *
 * Returns: (transfer full): a GtkDialog, without buttons.
 **/

GtkWidget *
gnm_message_dialog_create (GtkWindow * parent,
				GtkDialogFlags flags,
				GtkMessageType type,
				gchar const * primary_message,
				gchar const * secondary_message)
{
	GtkWidget * dialog;
	GtkWidget * label;
	GtkWidget * hbox;
	gchar *message;
	const gchar *icon_name;
	GtkWidget *image;
	const char *title;

	dialog = gtk_dialog_new_with_buttons ("", parent, flags, NULL, NULL);

	switch (type) {
	default:
		g_warning ("Unknown GtkMessageType %d", type);
	case GTK_MESSAGE_INFO:
		icon_name = "dialog-information";
		title = _("Information");
		break;

	case GTK_MESSAGE_QUESTION:
		icon_name = "dialog-question";
		title = _("Question");
		break;

	case GTK_MESSAGE_WARNING:
		icon_name = "dialog-warning";
		title = _("Warning");
		break;

	case GTK_MESSAGE_ERROR:
		icon_name = "dialog-error";
		title = _("Error");
		break;
	}

	image = gtk_image_new_from_icon_name (icon_name, GTK_ICON_SIZE_DIALOG);
	gtk_window_set_title (GTK_WINDOW (dialog), title);

	if (primary_message) {
		if (secondary_message) {
			message = g_strdup_printf ("<b>%s</b>\n\n%s",
						   primary_message,
						   secondary_message);
		} else {
			message = g_strdup_printf ("<b>%s</b>",
						   primary_message);
		}
	} else {
		message = g_strdup_printf ("%s", secondary_message);
	}
	label = gtk_label_new (message);
	g_free (message);

	hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (hbox), label, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))), hbox, TRUE, TRUE, 0);

	gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
	gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
	gtk_misc_set_alignment (GTK_MISC (label), 0.0 , 0.0);
	gtk_box_set_spacing (GTK_BOX (hbox), 12);
	gtk_container_set_border_width (GTK_CONTAINER (hbox), 6);
	gtk_box_set_spacing (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))), 12);
	gtk_container_set_border_width (GTK_CONTAINER (dialog), 6);
	gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
	gtk_widget_show_all (GTK_WIDGET (gtk_dialog_get_content_area (GTK_DIALOG (dialog))));

	return dialog;
}

typedef struct {
	GPtrArray *objects_signals;
} GnmDialogDestroySignals;

static void
cb_gnm_dialog_setup_destroy_handlers (G_GNUC_UNUSED GtkWidget *widget,
				      GnmDialogDestroySignals *dd)
{
	GPtrArray *os = dd->objects_signals;
	int i;

	for (i = 0; i < (int)os->len; i += 2) {
		GObject *obj = g_ptr_array_index (os, i);
		gulong s = GPOINTER_TO_SIZE (g_ptr_array_index (os, i + 1));
		g_signal_handler_disconnect (obj, s);
	}

	g_ptr_array_free (os, TRUE);
	memset (dd, 0, sizeof (*dd));
	g_free (dd);
}

void
gnm_dialog_setup_destroy_handlers (GtkDialog *dialog,
				   WBCGtk *wbcg,
				   GnmDialogDestroyOptions what)
{
	GnmDialogDestroySignals *dd = g_new (GnmDialogDestroySignals, 1);
	Workbook *wb = wb_control_get_workbook (GNM_WBC (wbcg));
	Sheet *sheet = wb_control_cur_sheet (GNM_WBC (wbcg));
	int N = workbook_sheet_count (wb), i;
	GPtrArray *os = g_ptr_array_new ();

	dd->objects_signals = os;

	/* FIXME: Properly implement CURRENT_SHEET_REMOVED.  */
	if (what & GNM_DIALOG_DESTROY_CURRENT_SHEET_REMOVED)
		what |= GNM_DIALOG_DESTROY_SHEET_REMOVED;

	if (what & GNM_DIALOG_DESTROY_SHEET_REMOVED) {
		gulong s = g_signal_connect_swapped
			(G_OBJECT (wb),
			 "sheet_deleted",
			 G_CALLBACK (gtk_widget_destroy),
			 dialog);
		g_ptr_array_add (os, wb);
		g_ptr_array_add (os, GSIZE_TO_POINTER (s));
	}

	if (what & GNM_DIALOG_DESTROY_SHEET_ADDED) {
		gulong s = g_signal_connect_swapped
			(G_OBJECT (wb),
			 "sheet_added",
			 G_CALLBACK (gtk_widget_destroy),
			 dialog);
		g_ptr_array_add (os, wb);
		g_ptr_array_add (os, GSIZE_TO_POINTER (s));
	}

	if (what & GNM_DIALOG_DESTROY_SHEETS_REORDERED) {
		gulong s = g_signal_connect_swapped
			(G_OBJECT (wb),
			 "sheet_order_changed",
			 G_CALLBACK (gtk_widget_destroy),
			 dialog);
		g_ptr_array_add (os, wb);
		g_ptr_array_add (os, GSIZE_TO_POINTER (s));
	}

	for (i = 0; i < N; i++) {
		Sheet *this_sheet = workbook_sheet_by_index (wb, i);
		gboolean current = (sheet == this_sheet);

		if ((what & GNM_DIALOG_DESTROY_SHEET_RENAMED) ||
		    (current && (what & GNM_DIALOG_DESTROY_CURRENT_SHEET_RENAMED))) {
			gulong s = g_signal_connect_swapped
				(G_OBJECT (this_sheet),
				 "notify::name",
				 G_CALLBACK (gtk_widget_destroy),
				 dialog);
			g_ptr_array_add (os, this_sheet);
			g_ptr_array_add (os, GSIZE_TO_POINTER (s));
		}
	}

	g_signal_connect (G_OBJECT (dialog),
			  "destroy",
			  G_CALLBACK (cb_gnm_dialog_setup_destroy_handlers),
			  dd);
}


void
gnm_canvas_get_position (GocCanvas *canvas, int *x, int *y, gint64 px, gint64 py)
{
	GtkWidget *cw = GTK_WIDGET (canvas);
	GdkWindow *cbw = gtk_layout_get_bin_window (GTK_LAYOUT (cw));
	int wx, wy;

	gdk_window_get_origin (cbw, &wx, &wy);

	/* we don't need to multiply px and py by the canvas pixels_per_unit
	 * field since all the callers already do that */
	px -= canvas->scroll_x1 * canvas->pixels_per_unit;
	py -= canvas->scroll_y1 * canvas->pixels_per_unit;
	/* let's take care of RTL sheets */
	if (canvas->direction == GOC_DIRECTION_RTL)
		px = goc_canvas_get_width (canvas) - px;

	*x = px + wx;
	*y = py + wy;
}

/*
 * Get the gdk position for canvas coordinates (x,y).  This is suitable
 * for tooltip windows.
 *
 * It is possible that this does not work right for very large coordinates
 * prior to gtk+ 2.18.  See the code and comments in gnm_canvas_get_position.
 */
void
gnm_canvas_get_screen_position (GocCanvas *canvas,
				double x, double y,
				int *ix, int *iy)
{
	GdkWindow *cbw = gtk_layout_get_bin_window (GTK_LAYOUT (canvas));
	int wx, wy;

	gdk_window_get_origin (cbw, &wx, &wy);
	goc_canvas_c2w (canvas, x, y, ix, iy);
	(*ix) += wx;
	(*iy) += wy;
}


gboolean
gnm_check_for_plugins_missing (char const **ids, GtkWindow *parent)
{
	for (; *ids != NULL; ids++) {
		GOPlugin *pi = go_plugins_get_plugin_by_id (*ids);
		if (pi == NULL) {
			GOErrorInfo *error;
			error = go_error_info_new_printf
				(_("The plugin with id %s is required "
				   "but cannot be found."), *ids);
			gnm_go_error_info_dialog_show (parent,
							 error);
			return TRUE;
		} else if (!go_plugin_is_active (pi)) {
			GOErrorInfo *error;
			error = go_error_info_new_printf
				(_("The %s plugin is required "
				   "but is not loaded."),
				 go_plugin_get_name (pi));
			gnm_go_error_info_dialog_show (parent,
							 error);
			return TRUE;
		}
	}
	return FALSE;
}


void
gnm_cell_renderer_text_copy_background_to_cairo (GtkCellRendererText *crt,
						 cairo_t *cr)
{
	GdkRGBA *c = NULL;
	g_object_get (crt, "background-rgba", &c, NULL);
	gdk_cairo_set_source_rgba (cr, c);
	gdk_rgba_free (c);
}

int
gnm_widget_measure_string (GtkWidget *w, const char *s)
{
	GtkStyleContext *ctxt;
	int len;
	PangoFontDescription *desc;
	GtkStateFlags state = GTK_STATE_FLAG_NORMAL;

	ctxt = gtk_widget_get_style_context (w);

	// As-of gtk+ 3.20 we have to set the context state to the state
	// we are querying for.  This ought to work before gtk+ 3.20 too.
	gtk_style_context_save (ctxt);
	gtk_style_context_set_state (ctxt, state);
	gtk_style_context_get (ctxt, state, "font", &desc, NULL);
	gtk_style_context_restore (ctxt);

	len = go_pango_measure_string
		(gtk_widget_get_pango_context (w), desc, s);

	pango_font_description_free (desc);

	return len;
}

static const char *
gnm_ag_translate (const char *s, const char *ctxt)
{
	return ctxt
		? g_dpgettext2 (NULL, ctxt, s)
		: _(s);
}

typedef struct {
	char *name;
	GCallback callback;
	gpointer user;
} TimerBounce;

static void
time_action (GtkAction *a, TimerBounce *b)
{
	const char *name = gtk_action_get_name (a);
	GTimer *timer;
	double elapsed;

	g_printerr ("Executing command %s...\n", name);
	timer = g_timer_new ();
	((void (*) (GtkAction *, gpointer))b->callback)
		(a, b->user);
	elapsed = g_timer_elapsed (timer, NULL);
	g_timer_destroy (timer);

	g_printerr ("Executing command %s...done [%.0fms]\n",
		    name, 1000 * elapsed);
}

void
gnm_action_group_add_actions (GtkActionGroup *group,
			      GnmActionEntry const *actions, size_t n,
			      gpointer user)
{
	unsigned i;
	gboolean debug = gnm_debug_flag("time-actions");

	for (i = 0; i < n; i++) {
		GnmActionEntry const *entry = actions + i;
		const char *name = entry->name;
		const char *label =
			gnm_ag_translate (entry->label, entry->label_context);
		const char *tip =
			gnm_ag_translate (entry->tooltip, NULL);
		GtkAction *a;

		if (entry->toggle) {
			GtkToggleAction *ta =
				gtk_toggle_action_new (name, label, tip, NULL);
			gtk_toggle_action_set_active (ta, entry->is_active);
			a = GTK_ACTION (ta);
		} else {
			a = gtk_action_new (name, label, tip, NULL);
		}

		g_object_set (a,
			      "icon-name", entry->icon,
			      "visible-horizontal", !entry->hide_horizontal,
			      "visible-vertical", !entry->hide_vertical,
			      NULL);

		if (entry->callback == NULL) {
			// Nothing
		} else if (debug) {
			TimerBounce *b = g_new (TimerBounce, 1);
			b->callback = entry->callback;
			b->user = user;
			g_signal_connect (a, "activate",
					  G_CALLBACK(time_action), b);
			g_object_set_data_full (G_OBJECT (a),
						"timer-hook",
						b,
						(GDestroyNotify)g_free);
		} else {
			g_signal_connect (a, "activate",
					  entry->callback, user);
		}

		gtk_action_group_add_action_with_accel (group,
							a,
							entry->accelerator);
		g_object_unref (a);
	}
}

void
gnm_action_group_add_action (GtkActionGroup *group, GtkAction *act)
{
	/*
	 * See the docs for gtk_action_group_add_action as to why we don't
	 * call just that.
	 */
	gtk_action_group_add_action_with_accel (group, act, NULL);
}


static int gnm_debug_css = -1;


void
gnm_css_debug_color (const char *name,
		     const GdkRGBA *color)
{
	if (gnm_debug_css < 0) gnm_debug_css = gnm_debug_flag ("css");

	if (gnm_debug_css) {
		char *ctxt = gdk_rgba_to_string (color);
		g_printerr ("css %s = %s\n", name, ctxt);
		g_free (ctxt);
	}
}

void
gnm_css_debug_int (const char *name, int i)
{
	if (gnm_debug_css < 0) gnm_debug_css = gnm_debug_flag ("css");

	if (gnm_debug_css)
		g_printerr ("css %s = %d\n", name, i);
}


void
gnm_style_context_get_color (GtkStyleContext *context,
			     GtkStateFlags state,
			     GdkRGBA *color)
{
	// As-of gtk+ 3.20 we have to set the context state to the state
	// we are querying for.  This ought to work before gtk+ 3.20 too.
	gtk_style_context_save (context);
	gtk_style_context_set_state (context, state);
	gtk_style_context_get_color (context,
				     gtk_style_context_get_state (context),
				     color);
	gtk_style_context_restore (context);
}

void
gnm_get_link_color (GtkWidget *widget, GdkRGBA *res)
{
#if GTK_CHECK_VERSION(3,12,0)
	GtkStyleContext *ctxt = gtk_widget_get_style_context (widget);
	gnm_style_context_get_color (ctxt, GTK_STATE_FLAG_LINK, res);
#else
	(void)widget;
	gdk_rgba_parse (res, "blue");
#endif
	gnm_css_debug_color ("link.color", res);
}

gboolean
gnm_theme_is_dark (GtkWidget *widget)
{
	GtkStyleContext *context;
	GdkRGBA fg_color;
	double lum;
	gboolean dark;

	context = gtk_widget_get_style_context (widget);
	gnm_style_context_get_color (context, GTK_STATE_FLAG_NORMAL, &fg_color);

	// One of many possible versions.
	lum = 0.299 * fg_color.red + 0.587 * fg_color.green + 0.114 * fg_color.blue;

	// Theme is dark if fg is light.
	dark = lum > 0.5;
	gnm_css_debug_int ("theme.dark", dark);

	return dark;
}


// ----------------------------------------------------------------------------
