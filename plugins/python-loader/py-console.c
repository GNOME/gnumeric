/*
 * py-console.c: Python console.
 *
 * Author: Zbigniew Chyla (cyba@gnome.pl)
 */

#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include <workbook-control-gui.h>
#include <plugin.h>
#include <module-plugin-defs.h>
#include "gnm-python.h"
#include "python-loader.h"
#include "py-interpreter-selector.h"
#include "py-command-line.h"
#include "py-console.h"


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
	GnmPyInterpreter *cur_interpreter;
} App;

static App *app = NULL;


static gint
cb_delete_app (GtkWidget *caller, GdkEvent *event, gpointer data)
{
	app = NULL;

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
app_cline_entered (GnmPyCommandLine *cline)
{
	const char *cmd;
	char *msg;
	char *stdout_str, *stderr_str;

	g_return_if_fail (app != NULL);

	cmd = gtk_entry_get_text (GTK_ENTRY (cline));
	msg = g_strdup_printf (">>> %s\n", cmd);
	app_text_print (msg, FORMAT_COMMAND, FALSE);
	g_free (msg);
	gnm_py_interpreter_run_string (app->cur_interpreter, cmd, &stdout_str, &stderr_str);
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
cb_clear (GtkButton *button, gpointer data)
{
	gtk_text_buffer_set_text (app->text_buffer, "", -1);
}

void
show_python_console (WorkbookControlGUI *wbcg)
{
	GtkWidget *win, *vbox, *text_view, *sc_win, *hbox, *sel, *cline, *w;
	PangoFontDescription *font_desc;

	if (app != NULL) {
		return;
	}

	app = g_new (App, 1);
	win = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title (GTK_WINDOW (win), _("Gnumeric Python console"));
	vbox = gtk_vbox_new (FALSE, 0);

	hbox = gtk_hbox_new (FALSE, 0);
	sel = gnm_py_interpreter_selector_new ();
	app->cur_interpreter =
		gnm_py_interpreter_selector_get_current (GNM_PY_INTERPRETER_SELECTOR (sel));
	g_signal_connect_object (
		sel, "interpreter_changed", G_CALLBACK (app_interpreter_changed),
		win, 0);
	w = gtk_label_new_with_mnemonic (_("E_xecute in:"));
	gtk_label_set_mnemonic_widget (GTK_LABEL (w), sel);
	gtk_box_pack_start (GTK_BOX (hbox), w, FALSE, TRUE, 4);
	gtk_box_pack_start (GTK_BOX (hbox), sel, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (hbox), gtk_label_new (""), TRUE, TRUE, 0);
	w = gtk_button_new_from_stock (GTK_STOCK_CLEAR);
	g_signal_connect (w, "clicked", G_CALLBACK (cb_clear), NULL);
	gtk_box_pack_start (GTK_BOX (hbox), w, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, TRUE, 2);

	sc_win = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (
		GTK_SCROLLED_WINDOW (sc_win), GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
	text_view = gtk_text_view_new ();
	app->text_buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (text_view));
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
	font_desc = pango_font_description_from_string ("Fixed");
	gtk_widget_modify_font (GTK_WIDGET (text_view), font_desc);
	pango_font_description_free (font_desc);
	gtk_text_view_set_editable (GTK_TEXT_VIEW (text_view), FALSE);
	gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (text_view), GTK_WRAP_WORD);
	gtk_container_add (GTK_CONTAINER (sc_win), text_view);
	gtk_box_pack_start (GTK_BOX (vbox), sc_win, TRUE, TRUE, 0);

	hbox = gtk_hbox_new (FALSE, 0);
	cline = gnm_py_command_line_new ();
	g_signal_connect (
		cline, "entered", G_CALLBACK (app_cline_entered), NULL);
	w = gtk_label_new_with_mnemonic (_("C_ommand:"));
	gtk_label_set_mnemonic_widget (GTK_LABEL (w), cline);
	gtk_box_pack_start (GTK_BOX (hbox), w, FALSE, TRUE, 4);
	gtk_box_pack_start (GTK_BOX (hbox), cline, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, TRUE, 0);

	gtk_container_add (GTK_CONTAINER (win), vbox);
	gtk_widget_set_usize (win, 600, 400);
	g_signal_connect (
		win, "delete_event", G_CALLBACK (cb_delete_app), NULL);

	gtk_widget_show_all (win);
}
