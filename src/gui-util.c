/*
 * gnumeric-util.c:  Various GUI utility functions.
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *
 */
#include <config.h>
#include <gnome.h>
#include <string.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "gutils.h"
#include "parse-util.h"
#include "command-context-gui.h"
#include "style.h"
#include "workbook.h"
#include <gal/widgets/e-colors.h>

#ifdef ENABLE_BONOBO
#	include <bonobo.h>
#	include "workbook-private.h"
#endif

void
gnumeric_no_modify_array_notice (Workbook *wb)
{
	gnumeric_notice (wb, GNOME_MESSAGE_BOX_ERROR,
			 _("You cannot change part of an array."));
}

/*
 * TODO:
 * Get rid of trailing newlines /whitespace.
 * Wrap overlong lines.
 */
void
gnumeric_notice (Workbook *wb, const char *type, const char *str)
{
	GtkWidget *dialog;

	dialog = gnome_message_box_new (str, type, GNOME_STOCK_BUTTON_OK, NULL);

	gnumeric_dialog_run (wb, GNOME_DIALOG (dialog));
}

/**
 * gnumeric_wb_dialog_run : A utility routine to handle the
 * application being closed by the window manager while a modal dialog
 * is being displayed.
 */
static gint
gnumeric_wb_dialog_run (Workbook *wb, GnomeDialog *dialog)
{
	gint res;
	GtkObject * const app = GTK_OBJECT (workbook_get_toplevel (wb));

	if (GTK_WINDOW (dialog)->transient_parent !=
	    GTK_WINDOW (workbook_get_toplevel (wb)))
		gnome_dialog_set_parent (GNOME_DIALOG (dialog),
					 GTK_WINDOW (workbook_get_toplevel (wb)));

	gtk_object_ref (app);
	res = gnome_dialog_run (dialog);

	/* If the application was closed close the dialog too */
	if (res < 0 && GTK_OBJECT_DESTROYED (app))
		gnome_dialog_close (dialog);

	/* TODO :
	 * 2) Handle destruction of the dialog
	 * 3) Handle the more interesting case of exiting of the
	 *    main window.
	 */
	gtk_object_unref (app);
	return res;
}

/**
 * gnumeric_dialog_run
 *
 * Pop up a dialog as child of a workbook.
 */
gint
gnumeric_dialog_run (Workbook *wb, GnomeDialog *dialog)
{
	if (wb)
		return gnumeric_wb_dialog_run (wb, dialog);
	else
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
gnumeric_dialog_show (GtkObject *parent, GnomeDialog *dialog,
		      gboolean click_closes, gboolean close_with_parent)
{
	DialogRunInfo *run_info = NULL;

	g_return_if_fail(GNOME_IS_DIALOG(dialog));
	if (parent) {
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

/**
 * gnumeric_set_transient
 * @context            command_context
 * @window             the transient window
 *
 * Make the window a child of the workbook in the command context, if there is
 * one. */
void
gnumeric_set_transient (CommandContext *context, GtkWindow *window)
{
	if (IS_COMMAND_CONTEXT_GUI (context)) {
		CommandContextGui *ccg = COMMAND_CONTEXT_GUI(context);
		gtk_window_set_transient_for
			(window, GTK_WINDOW (workbook_get_toplevel (ccg->wb)));
	}
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

/*
 * Returns TRUE if the GtkEntry supplied is editing an expression
 *    and the current position is at an expression boundary.
 * eg '=sum', or 'bob' are not while '=sum(' is
 */
gboolean
gnumeric_entry_at_subexpr_boundary_p (GtkEntry *entry)
{
	int cursor_pos;

	g_return_val_if_fail (entry != NULL, FALSE);

	cursor_pos = GTK_EDITABLE (entry)->current_pos;

	if (NULL == gnumeric_char_start_expr_p (entry->text_mb) || cursor_pos <= 0)
		return FALSE;

	switch (entry->text [cursor_pos-1]){
	case '=': case '-': case '*': case '/': case '^':
	case '+': case '&': case '(': case '%': case '!':
	case ':': case ',':
		return TRUE;

	default :
		return FALSE;
	};
}

GladeXML *
gnumeric_glade_xml_new (CommandContext *context, char const * gladefile)
{
	GladeXML *gui;
	char *d = gnumeric_sys_glade_dir ();
	char *f = g_concat_dir_and_file (d, gladefile);
	gui = glade_xml_new (f, NULL);

	/* Onlt report errors if the context is non-null */
	if (gui == NULL && context != NULL) {
		char *msg = g_strdup_printf (_("Unable to open file '%s'"), f);
		gnumeric_error_sys_err (context, msg);
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
gnumeric_non_modal_dialog (Workbook *wb, GtkWindow *dialog)
{
	gtk_window_set_transient_for (dialog,
				      GTK_WINDOW (workbook_get_toplevel (wb)));
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
gnumeric_inject_widget_into_bonoboui (Workbook *wb, GtkWidget *widget, char const *path)
{
	BonoboControl *control;

	gtk_widget_show_all (widget);
	control = bonobo_control_new (widget);
	bonobo_ui_component_object_set (
		wb->priv->uic, path,
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
		    elements [i].display_filter != display_filter)
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
		} else
			item = gtk_menu_item_new ();

		if (elements [i].sensitive_filter != 0 &&
		    elements [i].sensitive_filter != sensitive_filter)
			gtk_widget_set_sensitive (GTK_WIDGET (item), FALSE);

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
