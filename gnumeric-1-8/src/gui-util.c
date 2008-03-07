/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gnumeric-util.c:  Various GUI utility functions.
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include "gnumeric.h"
#include "libgnumeric.h"
#include "gui-util.h"

#include "gutils.h"
#include "parse-util.h"
#include "style.h"
#include "style-color.h"
#include "value.h"
#include "number-match.h"
#include "gnm-format.h"
#include "application.h"
#include "workbook.h"
#include "libgnumeric.h"
#include "wbc-gtk.h"
#include "widgets/gnumeric-expr-entry.h"

#include <goffice/app/error-info.h>
#include <goffice/gtk/go-combo-color.h>
#include <goffice/gtk/goffice-gtk.h>
#include <glade/glade.h>
#include <gtk/gtk.h>
#include <atk/atkrelation.h>
#include <atk/atkrelationset.h>
#include <gdk/gdkkeysyms.h>

#include <string.h>

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
 * SHOULD BE IN GOFFICE
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
	GdkScreen *screen;

	g_return_val_if_fail (error != NULL, NULL);

	message = (gchar *) error_info_peek_message (error);
	if (message == NULL)
		bf_lim++;

	mtype = GTK_MESSAGE_ERROR;
	if (error_info_peek_severity (error) < GO_ERROR)
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
	insert_error_info (text, error, 0);

	gtk_container_add (GTK_CONTAINER (scrolled_window), GTK_WIDGET (view));
	gtk_widget_show_all (GTK_WIDGET (scrolled_window));
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), scrolled_window, TRUE, TRUE, 0);

	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_CLOSE);
	return dialog;
}

/**
 * gnumeric_error_info_dialog_show
 *
 */
void
gnumeric_error_info_dialog_show (GtkWindow *parent, ErrorInfo *error)
{
	GtkWidget *dialog = gnumeric_error_info_dialog_new (error);
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

	/*
	 * One of these causes a recursive call which will do nothing due to
	 * ->freed.
	 */
	g_object_set_data (G_OBJECT (ctxt->wbcg), ctxt->key, NULL);
	g_object_set_data (G_OBJECT (ctxt->dialog), "KeyedDialog", NULL);
	g_free (ctxt);
}

static gint
cb_keyed_dialog_keypress (GtkWidget *dialog, GdkEventKey *event,
			  G_GNUC_UNUSED gpointer user)
{
	if (event->keyval == GDK_Escape) {
		gtk_object_destroy (GTK_OBJECT (dialog));
		return TRUE;
	}
	return FALSE;
}

#define SAVE_SIZES_SCREEN_KEY "geometry-hash"

static void
cb_save_sizes (GtkWidget *dialog, const char *key)
{
	GdkRectangle *r;
	GdkScreen *screen = gtk_widget_get_screen (dialog);
	GHashTable *h = g_object_get_data (G_OBJECT (screen),
					   SAVE_SIZES_SCREEN_KEY);
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

	r = g_memdup (&dialog->allocation, sizeof (dialog->allocation));
	gdk_window_get_position (dialog->window, &r->x, &r->y);
	g_hash_table_replace (h, g_strdup (key), r);
}

/**
 * gnumeric_keyed_dialog
 *
 * @wbcg    A WBCGtk
 * @dialog  A transient window
 * @key     A key to identify the dialog
 *
 * Make dialog a transient child of wbcg, attaching to wbcg object data to
 * identify the dialog. The object data makes it possible to ensure that
 * only one dialog of a kind can be displayed for a wbcg. Deallocation of
 * the object data is managed here.
 **/
void
gnumeric_keyed_dialog (WBCGtk *wbcg, GtkWindow *dialog, char const *key)
{
	KeyedDialogContext *ctxt;
	GtkWindow *top;
	GdkScreen *screen;
	GHashTable *h;
	GdkRectangle *allocation;

	g_return_if_fail (IS_WBC_GTK (wbcg));
	g_return_if_fail (GTK_IS_WINDOW (dialog));
	g_return_if_fail (key != NULL);

	wbcg_set_transient (wbcg, dialog);

	go_dialog_guess_alternative_button_order (GTK_DIALOG (dialog));

	ctxt = g_new (KeyedDialogContext, 1);
	ctxt->wbcg   = wbcg;
	ctxt->dialog = GTK_WIDGET (dialog);
	ctxt->key  = key;
	ctxt->freed = FALSE;
	g_object_set_data_full (G_OBJECT (wbcg), key, ctxt,
				(GDestroyNotify)cb_free_keyed_dialog_context);
	g_object_set_data_full (G_OBJECT (dialog), "KeyedDialog", ctxt,
				(GDestroyNotify)cb_free_keyed_dialog_context);
	g_signal_connect (G_OBJECT (dialog), "key_press_event",
			  G_CALLBACK (cb_keyed_dialog_keypress), NULL);

	top = GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (dialog)));
	screen = gtk_widget_get_screen (GTK_WIDGET (dialog));
	h = g_object_get_data (G_OBJECT (screen), SAVE_SIZES_SCREEN_KEY);
	allocation = h ? g_hash_table_lookup (h, key) : NULL;

	/* TECHOLOGY PREVIEW -- ZOOM DIALOG ONLY.  */
	if (strcmp (key, "zoom-dialog") == 0) {
		if (allocation) {
#if 0
			g_print ("Restoring %s to %dx%d at (%d,%d)\n",
				 key, allocation->width, allocation->height,
				 allocation->x, allocation->y);
#endif
			gtk_window_move (top, allocation->x, allocation->y);
			gtk_window_set_default_size
				(top, allocation->width, allocation->height);
		}

		g_signal_connect (G_OBJECT (dialog), "unrealize",
				  G_CALLBACK (cb_save_sizes),
				  (gpointer)key);
	}
}

/**
 * gnumeric_dialog_raise_if_exists
 *
 * @wbcg    A WBCGtk
 * @key     A key to identify the dialog
 *
 * Raise the dialog identified by key if it is registered on the wbcg.
 * Returns TRUE if dialog found, FALSE if not.
 **/
gpointer
gnumeric_dialog_raise_if_exists (WBCGtk *wbcg, char const *key)
{
	KeyedDialogContext *ctxt;

	g_return_val_if_fail (wbcg != NULL, NULL);
	g_return_val_if_fail (key != NULL, NULL);

	/* Ensure we only pop up one copy per workbook */
	ctxt = g_object_get_data (G_OBJECT (wbcg), key);
	if (ctxt && GTK_IS_WINDOW (ctxt->dialog)) {
		gdk_window_raise (ctxt->dialog->window);
		return ctxt->dialog;
	} else
		return NULL;
}

static gboolean
cb_activate_default (GtkWindow *window)
{
	/*
	 * gtk_window_activate_default has a bad habit of trying
	 * to activate the focus widget.
	 */
	return window->default_widget &&
		GTK_WIDGET_IS_SENSITIVE (window->default_widget) &&
		gtk_window_activate_default (window);
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
	if (IS_GNM_EXPR_ENTRY (w))
		w = GTK_WIDGET (gnm_expr_entry_get_entry (GNM_EXPR_ENTRY (w)));

	g_signal_connect_swapped (G_OBJECT (w),
		"activate",
		G_CALLBACK (cb_activate_default), window);
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
gnumeric_glade_group_value (GladeXML *gui, char const * const group[])
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

/**
 * gnumeric_popup_menu :
 * @menu : #GtkMenu
 * @event : #GdkEventButton optionally NULL
 *
 * Bring up a popup and if @event is non-NULL ensure that the popup is on the
 * right screen.
 **/
void
gnumeric_popup_menu (GtkMenu *menu, GdkEventButton *event)
{
	g_return_if_fail (menu != NULL);
	g_return_if_fail (GTK_IS_MENU (menu));

#if GLIB_CHECK_VERSION(2,10,0) && GTK_CHECK_VERSION(2,8,14)
	g_object_ref_sink (menu);
#else
	g_object_ref (menu);
	gtk_object_sink (GTK_OBJECT (menu));
#endif

	if (event)
		gtk_menu_set_screen (menu,
				     gdk_drawable_get_screen (event->window));

	g_signal_connect (G_OBJECT (menu),
		"hide",
		G_CALLBACK (kill_popup_menu), menu);

	/* Do NOT pass the button used to create the menu.
	 * instead pass 0.  Otherwise bringing up a menu with
	 * the right button will disable clicking on the menu with the left.
	 */
	gtk_menu_popup (menu, NULL, NULL, NULL, NULL, 0,
			(event != NULL) ? event->time
			: gtk_get_current_event_time());
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
		x -= req.width / 2;
		y -= req.height + 20;
	} else {
		x -= req.width + 20;
		y -= req.height / 2;
	}

	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;

	gtk_window_move (GTK_WINDOW (gtk_widget_get_toplevel (tip)), x, y);
}

/**
 * gnm_glade_xml_new :
 * @cc : #GOCmdContext
 * @gladefile :
 *
 * Simple utility to open glade files
 **/
GladeXML *
gnm_glade_xml_new (GOCmdContext *cc, char const *gladefile,
		   char const *root, char const *domain)
{
	GladeXML *gui;
	char *f;

	g_return_val_if_fail (gladefile != NULL, NULL);

	if (g_path_is_absolute (gladefile))
		f = g_strdup (gladefile);
	else
		f = g_build_filename (gnm_sys_data_dir (), "glade", gladefile, NULL);

	gui = glade_xml_new (f, root, domain);
	if (gui == NULL && cc != NULL) {
		char *msg = g_strdup_printf (_("Unable to open file '%s'"), f);
		go_cmd_context_error_system (cc, msg);
		g_free (msg);
	}
	g_free (f);

	return gui;
}

static void
popup_item_activate (GtkWidget *item, gpointer *user_data)
{
	GnumericPopupMenuElement const *elem =
		g_object_get_data (G_OBJECT (item), "descriptor");
	GnumericPopupMenuHandler handler =
		g_object_get_data (G_OBJECT (item), "handler");

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
			g_object_set_data (
				G_OBJECT (item), "descriptor", (gpointer)(element));
			g_object_set_data (
				G_OBJECT (item), "handler", (gpointer)handler);
		}

		gtk_widget_show (item);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
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
 * go_combo_color_get_style_color :
 *
 * A utility wrapper to map between gal's colour combo and gnumeric's StyleColors.
 */
GnmColor *
go_combo_color_get_style_color (GtkWidget *go_combo_color)
{
	GnmColor *sc = NULL;
	guint16   r, g, b;
	GOColor color = go_combo_color_get_color (GO_COMBO_COLOR (go_combo_color), NULL);
	if (UINT_RGBA_A (color) >= 0x80) {
		r  = UINT_RGBA_R (color); r |= (r << 8);
		g  = UINT_RGBA_G (color); g |= (g << 8);
		b  = UINT_RGBA_B (color); b |= (b << 8);
		sc = style_color_new (r, g, b);
	}
	return sc;
}

void
gnumeric_init_help_button (GtkWidget *w, char const *link)
{
	go_gtk_help_button_init (w, gnm_sys_data_dir (), "gnumeric", link);
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
	gtk_editable_select_region (GTK_EDITABLE (entry), 0, entry->text_length);
}

gboolean
entry_to_float_with_format_default (GtkEntry *entry, gnm_float *the_float, gboolean update,
				    GOFormat *format, gnm_float num)
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
			    GOFormat *format)
{
	GnmValue *value = format_match_number (gtk_entry_get_text (entry), format, NULL);

	*the_float = 0.0;
	if (!value)
		return TRUE;

	*the_float = value_get_as_float (value);
	if (update) {
		char *tmp = format_value (format, value, NULL, 16, NULL);
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
		char *tmp = format_value (NULL, value, NULL, 16, NULL);
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
	char *text = format_value (NULL, val, NULL, 16, NULL);
	value_release(val);
	if (text != NULL) {
		gtk_entry_set_text (entry, text);
		g_free (text);
	}
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
	GnmValue *val  = value_new_int (the_int);
	char *text = format_value (NULL, val, NULL, 16, NULL);
	value_release(val);
	if (text != NULL) {
		gtk_entry_set_text (entry, text);
		g_free (text);
	}
}

GtkWidget *
gnumeric_load_image (char const *filename)
{
	char *path = g_build_filename (gnm_icon_dir (), filename, NULL);
	GtkWidget *image = gtk_image_new_from_file (path);
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
	char *path = g_build_filename (gnm_icon_dir (), filename, NULL);
	GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file (path, NULL);
	g_free (path);
	return pixbuf;
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
	gdk_window_set_cursor (w->window, cursor);
}

void
gnm_widget_set_cursor_type (GtkWidget *w, GdkCursorType ct)
{
	GdkDisplay *display = gdk_drawable_get_display (w->window);
	GdkCursor *cursor = gdk_cursor_new_for_display (display, ct);
	gnm_widget_set_cursor (w, cursor);
	gdk_cursor_unref (cursor);
}

/* ------------------------------------------------------------------------- */

/**
 * gnumeric_message_dialog_new :
 *
 * A convenience fonction to build HIG compliant message dialogs.
 *
 *   parent : transient parent, or NULL for none.
 *   flags
 *   type : type of dialog
 *   primary_message : message displayed in bold
 *   secondary_message : message displayed below
 *
 *   return : a GtkDialog, without buttons.
 **/

GtkWidget *
gnumeric_message_dialog_new (GtkWindow * parent,
			     GtkDialogFlags flags,
			     GtkMessageType type,
			     gchar const * primary_message,
			     gchar const * secondary_message)
{
	GtkWidget * dialog;
	GtkWidget * label;
	GtkWidget * image;
	GtkWidget * hbox;
	gchar * message;
	const gchar *stock_id = NULL;
	GtkStockItem item;

	dialog = gtk_dialog_new_with_buttons ("", parent, flags, NULL);

	if (dialog) {
		image = gtk_image_new ();

		switch (type) {
		case GTK_MESSAGE_INFO:
			stock_id = GTK_STOCK_DIALOG_INFO;
			break;

		case GTK_MESSAGE_QUESTION:
			stock_id = GTK_STOCK_DIALOG_QUESTION;
			break;

		case GTK_MESSAGE_WARNING:
			stock_id = GTK_STOCK_DIALOG_WARNING;
			break;

		case GTK_MESSAGE_ERROR:
			stock_id = GTK_STOCK_DIALOG_ERROR;
			break;

		default:
			g_warning ("Unknown GtkMessageType %d", type);
			break;
		}

		if (stock_id == NULL)
			stock_id = GTK_STOCK_DIALOG_INFO;

		if (gtk_stock_lookup (stock_id, &item)) {
			gtk_image_set_from_stock (GTK_IMAGE (image), stock_id,
						  GTK_ICON_SIZE_DIALOG);

			gtk_window_set_title (GTK_WINDOW (dialog), item.label);
		} else
			g_warning ("Stock dialog ID doesn't exist?");

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
			message = g_strdup_printf (secondary_message);
		}
		label = gtk_label_new (message);
		g_free (message);

		hbox = gtk_hbox_new (FALSE, 0);
		gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, TRUE, 0);
		gtk_box_pack_start_defaults (GTK_BOX (hbox),
					     label);
		gtk_box_pack_start_defaults (GTK_BOX (GTK_DIALOG (dialog)->vbox),
					     hbox);

		gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
		gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
		gtk_misc_set_alignment (GTK_MISC (label), 0.0 , 0.0);
		gtk_box_set_spacing (GTK_BOX (hbox), 12);
		gtk_container_set_border_width (GTK_CONTAINER (hbox), 6);
		gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->vbox), 12);
		gtk_container_set_border_width (GTK_CONTAINER (dialog), 6);
		gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);
		gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
		gtk_widget_show_all (GTK_WIDGET (GTK_DIALOG (dialog)->vbox));
	}

	return dialog;
}

gboolean
gnm_tree_model_iter_prev (GtkTreeModel *model, GtkTreeIter* iter)
{
	GtkTreePath *path = gtk_tree_model_get_path (model, iter);

	if (gtk_tree_path_prev (path) &&
	    gtk_tree_model_get_iter (model, iter, path)) {
		gtk_tree_path_free (path);
		return TRUE;
	}
	gtk_tree_path_free (path);
	return FALSE;
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
		guint s = GPOINTER_TO_UINT (g_ptr_array_index (os, i + 1));
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
	Workbook *wb = wb_control_get_workbook (WORKBOOK_CONTROL (wbcg));
	Sheet *sheet = wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg));
	int N = workbook_sheet_count (wb), i;
	GPtrArray *os = g_ptr_array_new ();

	dd->objects_signals = os;

	/* FIXME: Properly implement CURRENT_SHEET_REMOVED.  */
	if (what & GNM_DIALOG_DESTROY_CURRENT_SHEET_REMOVED)
		what |= GNM_DIALOG_DESTROY_SHEET_REMOVED;

	if (what & GNM_DIALOG_DESTROY_SHEET_REMOVED) {
		guint s = g_signal_connect_swapped
			(G_OBJECT (wb),
			 "sheet_deleted",
			 G_CALLBACK (gtk_widget_destroy),
			 dialog);
		g_ptr_array_add (os, wb);
		g_ptr_array_add (os, GUINT_TO_POINTER (s));
	}

	if (what & GNM_DIALOG_DESTROY_SHEET_ADDED) {
		guint s = g_signal_connect_swapped
			(G_OBJECT (wb),
			 "sheet_added",
			 G_CALLBACK (gtk_widget_destroy),
			 dialog);
		g_ptr_array_add (os, wb);
		g_ptr_array_add (os, GUINT_TO_POINTER (s));
	}

	if (what & GNM_DIALOG_DESTROY_SHEETS_REORDERED) {
		guint s = g_signal_connect_swapped
			(G_OBJECT (wb),
			 "sheet_order_changed",
			 G_CALLBACK (gtk_widget_destroy),
			 dialog);
		g_ptr_array_add (os, wb);
		g_ptr_array_add (os, GUINT_TO_POINTER (s));
	}

	for (i = 0; i < N; i++) {
		Sheet *this_sheet = workbook_sheet_by_index (wb, i);
		gboolean current = (sheet == this_sheet);

		if ((what & GNM_DIALOG_DESTROY_SHEET_RENAMED) ||
		    (current && (what & GNM_DIALOG_DESTROY_CURRENT_SHEET_RENAMED))) {
			guint s = g_signal_connect_swapped
				(G_OBJECT (this_sheet),
				 "notify::name",
				 G_CALLBACK (gtk_widget_destroy),
				 dialog);
			g_ptr_array_add (os, this_sheet);
			g_ptr_array_add (os, GUINT_TO_POINTER (s));
		}
	}

	g_signal_connect (G_OBJECT (dialog),
			  "destroy",
			  G_CALLBACK (cb_gnm_dialog_setup_destroy_handlers),
			  dd);
}
