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
#include "parse-util.h"
#include "command-context-gui.h"
#include "style.h"
#include "color.h"
#include "workbook.h"

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
	GtkObject * const app = GTK_OBJECT (wb->toplevel);

	gnome_dialog_set_parent (GNOME_DIALOG (dialog),
				 GTK_WINDOW (wb->toplevel));

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
gnumeric_dialog_show (GtkWidget *parent, GnomeDialog *dialog,
		      gboolean click_closes, gboolean close_with_parent)
{
	DialogRunInfo *run_info = NULL;
	
	g_return_if_fail(GNOME_IS_DIALOG(dialog));
	if (parent) {
		run_info = g_new0 (DialogRunInfo, 1);
		run_info->parent_toplevel
			= gtk_widget_get_toplevel (GTK_WIDGET (parent));
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
			(window, GTK_WINDOW (ccg->wb->toplevel));
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

void
gtk_radio_button_select (GSList *group, int n)
{
	GSList *l;
	int len = g_slist_length (group);

	l = g_slist_nth (group, len - n - 1);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (l->data), 1);
}

static void
popup_menu_item_activated (GtkWidget *item, void *value)
{
	int *dest = gtk_object_get_user_data (GTK_OBJECT (item));

	*dest = GPOINTER_TO_INT (value);
	gtk_main_quit ();
}

int
run_popup_menu (GdkEvent *event, int button, char **strings)
{
	GtkWidget *menu;
	int i;

	g_return_val_if_fail (event != NULL, -1);
	g_return_val_if_fail (strings != NULL, -1);

	/* Create the popup menu */
	menu = gtk_menu_new ();
	for (i = 0;*strings; strings++, i++){
		GtkWidget *item;

		item = gtk_menu_item_new_with_label (_(*strings));

		gtk_widget_show (item);
		gtk_signal_connect (
			GTK_OBJECT (item), "activate",
			GTK_SIGNAL_FUNC (popup_menu_item_activated), GINT_TO_POINTER (i));

		/* Pass a pointer where we want the result stored */
		gtk_object_set_user_data (GTK_OBJECT (item), &i);

		gtk_menu_append (GTK_MENU (menu), item);
	}

	i = -1;

	/* Configure it: */
	gtk_signal_connect (GTK_OBJECT (menu), "deactivate",
			    GTK_SIGNAL_FUNC (gtk_main_quit), NULL);

	/* popup the menu */
	gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL,
			event->button.button, event->button.time);
	gtk_main ();

	gtk_widget_destroy (menu);

	return i;
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
	gtk_menu_popup (menu, NULL, NULL, 0, NULL, event->button, event->time);
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
			color_alloc_name ("LightYellow",  &rc_style->bg[i]);
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
gnumeric_entry_at_subexpr_boundary_p (GtkWidget const * const w)
{
	GtkEntry *entry;
	int cursor_pos;

	g_return_val_if_fail (w, FALSE);
	entry = GTK_ENTRY (w);
	g_return_val_if_fail (entry, FALSE);

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

