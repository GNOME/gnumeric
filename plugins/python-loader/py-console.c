/*
 * py-console.c: Python console.
 *
 * Author: Zbigniew Chyla (cyba@gnome.pl)
 */

#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include <stdlib.h>
#include <gtk/gtk.h>
#include <gnome.h>
#include <gal/util/e-util.h>
#include <bonobo.h>
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
	GtkWidget *win;
	BonoboUIContainer *ui_container;
	BonoboUIComponent *uic;
	GtkWidget *cline;
	GtkTextView *text_view;
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
vb_clear (BonoboUIComponent *uic, gpointer data, const char *cname)
{
	gtk_text_buffer_set_text (app->text_buffer, "", -1);
}

static BonoboUIVerb verbs [] = {
	BONOBO_UI_UNSAFE_VERB ("Clear", vb_clear),
	BONOBO_UI_VERB_END
};

void
show_python_console (WorkbookControlGUI *wbcg)
{
	char *ui_file_name;
	BonoboUINode *ui_node;
	GtkWidget *vbox, *sc_win;
	PangoFontDescription *font_desc;
	GtkWidget *sel;

	if (app != NULL) {
		return;
	}

	app = g_new (App, 1);
	app->win = bonobo_window_new ("python-console", _("Gnumeric Python console"));
	vbox = gtk_vbox_new (FALSE, 0);
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
	font_desc = pango_font_description_from_string ("Fixed");
	gtk_widget_modify_font (GTK_WIDGET (app->text_view), font_desc);
	pango_font_description_free (font_desc);
	gtk_text_view_set_editable (app->text_view, FALSE);
	gtk_text_view_set_wrap_mode (app->text_view, GTK_WRAP_WORD);
	gtk_container_add (GTK_CONTAINER (sc_win), GTK_WIDGET (app->text_view));
	gtk_box_pack_start (GTK_BOX (vbox), sc_win, TRUE, TRUE, 0);
	app->cline = gnm_py_command_line_new ();
	g_signal_connect (
		app->cline, "entered", G_CALLBACK (app_cline_entered), NULL);
	gtk_box_pack_start (GTK_BOX (vbox), app->cline, FALSE, TRUE, 0);
	bonobo_window_set_contents (BONOBO_WINDOW (app->win), vbox);
	gtk_widget_grab_focus (app->cline);
	gtk_widget_set_usize (app->win, 600, 400);
	g_signal_connect (
		app->win, "delete_event", G_CALLBACK (cb_delete_app), NULL);
	sel = gnm_py_interpreter_selector_new ();
	gtk_widget_show (sel);
	app->cur_interpreter =
		gnm_py_interpreter_selector_get_current (GNM_PY_INTERPRETER_SELECTOR (sel));
	g_signal_connect (
		sel, "interpreter_changed", G_CALLBACK (app_interpreter_changed), NULL);

	app->ui_container = bonobo_window_get_ui_container (BONOBO_WINDOW (app->win));
	app->uic = bonobo_ui_component_new_default ();
	bonobo_ui_component_set_container (app->uic, BONOBO_OBJREF (app->ui_container), NULL);
	ui_file_name = g_build_path (
		G_DIR_SEPARATOR_S, gnm_plugin_get_dir_name (PLUGIN),
		"py-console-ui.xml", NULL);
	ui_node = bonobo_ui_node_from_file (ui_file_name);
	bonobo_ui_util_translate_ui (ui_node);
	g_free (ui_file_name);
	bonobo_ui_component_set_tree (app->uic, "/", ui_node, NULL);
	bonobo_ui_node_free (ui_node);
	bonobo_ui_component_object_set (
		app->uic, "/MainToolbar/InterpreterSelector",
		BONOBO_OBJREF (bonobo_control_new (sel)),
		NULL);
	bonobo_ui_component_add_verb_list (app->uic, verbs);

	gtk_widget_show_all (app->win);
}
