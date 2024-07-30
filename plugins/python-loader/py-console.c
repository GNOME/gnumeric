/*
 * py-console.c: Python console.
 *
 * Author: Zbigniew Chyla (cyba@gnome.pl)
 */

#include <gnumeric-config.h>
#include <command-context.h>
#include "gnm-python.h"
#include "python-loader.h"
#include "py-interpreter-selector.h"
#include "py-command-line.h"
#include "py-console.h"

#include <goffice/goffice.h>
#include <goffice/app/module-plugin-defs.h>

#include <gdk/gdkkeysyms.h>
#include <glib/gi18n-lib.h>

#include <stdlib.h>
#include <string.h>
typedef enum {
	FORMAT_COMMAND,
	FORMAT_RESULT,
	FORMAT_MESSAGE,
	FORMAT_STDOUT,
	FORMAT_STDERR,
	FORMAT_LAST
} PrintFormat;

typedef struct {
	GtkTextBuffer *text_buffer;
	GtkTextTag *text_tags[FORMAT_LAST];
	GtkTextView *text_view;
	GtkTextMark *text_end;
	GnmPyInterpreter *cur_interpreter;
	GtkWidget   *win;
} App;

static App *app = NULL;


static gint
cb_delete_app (GtkWidget *caller, GdkEvent *event, gpointer data)
{
	app = NULL;

	return FALSE;
}

/* Close window on Ctrl+W */
static gint
cb_key_event (GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
	if (event->keyval == GDK_KEY_w && (event->state & GDK_CONTROL_MASK)) {
		g_signal_stop_emission_by_name (G_OBJECT (widget),
						"key_press_event");
		gtk_widget_destroy (app->win);
		app = NULL;
		return TRUE;
	} else
		return FALSE;
}

static void
app_text_print (const char *line, PrintFormat format, gboolean newline)
{
	GtkTextIter iter;

	gtk_text_buffer_get_end_iter (app->text_buffer, &iter);
	gtk_text_buffer_insert_with_tags (
		app->text_buffer, &iter, line, -1, app->text_tags[format], NULL);
	if (newline) {
		gtk_text_buffer_insert (app->text_buffer, &iter, "\n", -1);
	}
	gtk_text_view_scroll_mark_onscreen (app->text_view, app->text_end);
}

static void
app_interpreter_changed (GnmPyInterpreterSelector *sel)
{
	g_return_if_fail (app != NULL);

	app->cur_interpreter = gnm_py_interpreter_selector_get_current (sel);
	if (app->cur_interpreter != NULL) {
		char *msg;

		msg = g_strdup_printf (
		      _("*** Interpreter: %s\n"), gnm_py_interpreter_get_name (app->cur_interpreter));
		app_text_print (msg, FORMAT_MESSAGE, FALSE);
		g_free (msg);
	}
}

static void
app_run_string (char *cmd)
{
	char *stdout_str, *stderr_str;

	gnm_py_interpreter_run_string (app->cur_interpreter, cmd,
				       &stdout_str, &stderr_str);
	if (stdout_str != NULL && stdout_str[0] != '\0') {
		app_text_print (stdout_str, FORMAT_STDOUT,
		                stdout_str[strlen (stdout_str) - 1] != '\n');
		g_free (stdout_str);
	}
	if (stderr_str != NULL && stderr_str[0] != '\0') {
		app_text_print (stderr_str, FORMAT_STDERR,
		                stderr_str[strlen (stderr_str) - 1] != '\n');
		g_free (stderr_str);
	}
}

static void
app_cline_entered (GnmPyCommandLine *cline)
{
	char *cmd, *msg;

	g_return_if_fail (app != NULL);

	cmd = g_strstrip (g_strdup (gtk_entry_get_text (GTK_ENTRY (cline))));
	while (*cmd == ' ')
		cmd++;
	if (!strncmp (cmd, "quit", 4)) {
		/* check if the non space character is a left bracket */
		char *cur = cmd + 4;
		while (*cur && g_unichar_isspace (g_utf8_get_char (cur)))
			cur = g_utf8_next_char (cur);
		if (*cur == '(') {
			/* don't close gnumeric, just the console */
			gtk_widget_destroy (app->win);
			app = NULL;
			return;
		}
	}
	msg = g_strdup_printf (">>> %s\n", cmd);
	app_text_print (msg, FORMAT_COMMAND, FALSE);
	g_free (msg);
	if (*cmd)
		app_run_string (cmd);
	g_free (cmd);
	return;
}

static void
cb_clear (GtkButton *button, gpointer data)
{
	gtk_text_buffer_set_text (app->text_buffer, "", -1);
}

void
show_python_console (GnmAction const *action, WorkbookControl *wbc)
{
	GtkWidget *vbox, *sc_win, *hbox, *sel, *cline, *w;
	GtkTextIter enditer;
	PangoFontDescription *font_desc;
	GOErrorInfo *err = NULL;

	if (app != NULL) {
		gtk_window_present (GTK_WINDOW (app->win));
		return;
	}

	sel = gnm_py_interpreter_selector_new (&err);
	if (err != NULL) {
		go_cmd_context_error_info (GO_CMD_CONTEXT (wbc), err);
		go_error_info_free (err);
		return;
	}
	app = g_new (App, 1);
	app->win = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title (GTK_WINDOW (app->win), _("Gnumeric Python console"));
	app->cur_interpreter =
		gnm_py_interpreter_selector_get_current (GNM_PY_INTERPRETER_SELECTOR (sel));
	g_signal_connect_object (
		G_OBJECT (sel), "interpreter_changed",
		G_CALLBACK (app_interpreter_changed), app->win, 0);
	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

	hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	w = gtk_label_new_with_mnemonic (_("E_xecute in:"));
	gtk_label_set_mnemonic_widget (GTK_LABEL (w), sel);
	gtk_box_pack_start (GTK_BOX (hbox), w, FALSE, TRUE, 4);
	gtk_box_pack_start (GTK_BOX (hbox), sel, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (hbox), gtk_label_new (""), TRUE, TRUE, 0);
	w = gtk_button_new_from_stock (GTK_STOCK_CLEAR);
	g_signal_connect (G_OBJECT (w), "clicked",
			  G_CALLBACK (cb_clear), NULL);
	gtk_box_pack_start (GTK_BOX (hbox), w, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, TRUE, 2);

	sc_win = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (
		GTK_SCROLLED_WINDOW (sc_win), GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
	app->text_view = GTK_TEXT_VIEW (gtk_text_view_new ());
	app->text_buffer = gtk_text_view_get_buffer (app->text_view);
	app->text_tags[FORMAT_COMMAND] = gtk_text_buffer_create_tag (
		app->text_buffer, NULL, "foreground", "black", NULL);
	app->text_tags[FORMAT_RESULT] = gtk_text_buffer_create_tag (
		app->text_buffer, NULL, "foreground", "black", NULL);
	app->text_tags[FORMAT_MESSAGE] = gtk_text_buffer_create_tag (
		app->text_buffer, NULL, "foreground", "green", NULL);
	app->text_tags[FORMAT_STDOUT] = gtk_text_buffer_create_tag (
		app->text_buffer, NULL, "foreground", "blue", NULL);
	app->text_tags[FORMAT_STDERR] = gtk_text_buffer_create_tag (
		app->text_buffer, NULL, "foreground", "red", NULL);
	gtk_text_buffer_get_iter_at_offset (app->text_buffer, &enditer, -1);
	app->text_end = gtk_text_buffer_create_mark (app->text_buffer,
						     "text_end",
						     &enditer,
						     FALSE);
	font_desc = pango_font_description_from_string ("Fixed");
	gtk_widget_override_font (GTK_WIDGET (app->text_view), font_desc);
	pango_font_description_free (font_desc);
	gtk_text_view_set_editable (GTK_TEXT_VIEW (app->text_view), FALSE);
	gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (app->text_view),
				     GTK_WRAP_WORD);
	gtk_container_add (GTK_CONTAINER (sc_win),
			   GTK_WIDGET (app->text_view));
	gtk_box_pack_start (GTK_BOX (vbox), sc_win, TRUE, TRUE, 0);

	hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	cline = gnm_py_command_line_new ();
	g_signal_connect (G_OBJECT (cline), "entered",
			  G_CALLBACK (app_cline_entered), NULL);
	w = gtk_label_new_with_mnemonic (_("C_ommand:"));
	gtk_label_set_mnemonic_widget (GTK_LABEL (w), cline);
	gtk_box_pack_start (GTK_BOX (hbox), w, FALSE, TRUE, 4);
	gtk_box_pack_start (GTK_BOX (hbox), cline, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, TRUE, 0);

	gtk_container_add (GTK_CONTAINER (app->win), vbox);
	gtk_widget_grab_focus (cline);
	gtk_window_set_default_size (GTK_WINDOW (app->win), 600, 400);
	g_signal_connect (G_OBJECT (app->win), "delete_event",
			  G_CALLBACK (cb_delete_app), NULL);
	g_signal_connect (G_OBJECT (app->win), "key_press_event",
			  G_CALLBACK (cb_key_event), NULL);

	gtk_widget_show_all (app->win);
}
