/*
 * py-command-line.c: Simple wrapper around GtkEntry with history support
 *
 * Author: Zbigniew Chyla (cyba@gnome.pl)
 */

#include <gnumeric-config.h>
#include "py-command-line.h"
#include "gnm-python.h"
#include <gnumeric.h>
#include <gutils.h>
#include <goffice/goffice.h>
#include <goffice/app/module-plugin-defs.h>
#include <gsf/gsf-impl-utils.h>
#include <glib-object.h>
#include <gdk/gdkkeysyms.h>
#include <string.h>

#define MAX_HISTORY_SIZE  100

struct _GnmPyCommandLine {
	GtkEntry parent;

	GList *history, *history_tail, *history_cur;
	gboolean editing;
	int history_size;
};

typedef struct {
	GtkEntryClass parent_class;

	void (*entered) (GnmPyCommandLine *cline);
} GnmPyCommandLineClass;

enum {
	ENTERED_SIGNAL,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };
static GObjectClass *parent_class = NULL;

static void
gnm_py_command_line_changed (GnmPyCommandLine *cline)
{
	cline->editing = TRUE;
}

static gint
gnm_py_command_line_keypress (GnmPyCommandLine *cline, GdkEventKey *event, gpointer user_data)
{
	switch (event->keyval) {
	case GDK_KEY_Return: {
		const char *text;

		text = gtk_entry_get_text (GTK_ENTRY (cline));
		if (cline->history_tail == NULL) {
			cline->history = g_list_append (NULL, g_strdup (text));
			cline->history_tail = cline->history;
		} else if (text[0] != '\0' && strcmp (text, cline->history_tail->data) != 0) {
			cline->history_tail =
				g_list_append (cline->history_tail,
					       g_strdup (text))->next;
		}
		if (cline->history_size == MAX_HISTORY_SIZE) {
			g_free (cline->history->data);
			cline->history = g_list_delete_link (cline->history, cline->history);
		} else {
			cline->history_size++;
		}
		g_signal_emit (cline, signals[ENTERED_SIGNAL], 0);
		gtk_entry_set_text (GTK_ENTRY (cline), "");
		cline->editing = TRUE;
		break;
	}
	case GDK_KEY_Up:
		if (cline->editing) {
			if (cline->history_tail != NULL) {
				cline->history_cur = cline->history_tail;
				gtk_entry_set_text (GTK_ENTRY (cline), cline->history_cur->data);
				gtk_editable_set_position (
					GTK_EDITABLE (cline), strlen (cline->history_cur->data));
				cline->editing = FALSE;
			}
		} else {
			if (cline->history_cur->prev != NULL) {
				cline->history_cur = cline->history_cur->prev;
				gtk_entry_set_text (GTK_ENTRY (cline), cline->history_cur->data);
				gtk_editable_set_position (
					GTK_EDITABLE (cline), strlen (cline->history_cur->data));
				cline->editing = FALSE;
			}
		}
		break;
	case GDK_KEY_Down:
		if (!cline->editing) {
			if (cline->history_cur->next != NULL) {
				cline->history_cur = cline->history_cur->next;
				gtk_entry_set_text (GTK_ENTRY (cline), cline->history_cur->data);
				gtk_editable_set_position (
					GTK_EDITABLE (cline), strlen (cline->history_cur->data));
				cline->editing = FALSE;
			} else {
				gtk_entry_set_text (GTK_ENTRY (cline), "");
				cline->editing = TRUE;
			}
		}
		break;
	default:
		return FALSE;
	}

	g_signal_stop_emission_by_name (cline, "key_press_event");
	return TRUE;
}

static void
gnm_py_command_line_init (GnmPyCommandLine *cline)
{
	g_signal_connect (
		cline, "key_press_event",
		G_CALLBACK (gnm_py_command_line_keypress), NULL);
	g_signal_connect (
		cline, "changed",
		G_CALLBACK (gnm_py_command_line_changed), NULL);
	cline->history = cline->history_tail = NULL;
	cline->history_cur = NULL;
	cline->editing = TRUE;
	cline->history_size = 0;
}

static void
gnm_py_command_line_finalize (GObject *obj)
{
	GnmPyCommandLine *cline = GNM_PY_COMMAND_LINE (obj);

	g_list_free_full (cline->history, g_free);
	cline->history = NULL;

	parent_class->finalize (obj);
}

static void
gnm_py_command_line_class_init (GObjectClass *gobject_class)
{
	parent_class = g_type_class_peek_parent (gobject_class);

	gobject_class->finalize = gnm_py_command_line_finalize;

	signals[ENTERED_SIGNAL] =
	g_signal_new (
		"entered",
		G_TYPE_FROM_CLASS (gobject_class),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (GnmPyCommandLineClass, entered),
		NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 0);
}

GtkWidget *
gnm_py_command_line_new (void)
{
	return g_object_new (GNM_PY_COMMAND_LINE_TYPE, NULL);
}

GSF_DYNAMIC_CLASS (GnmPyCommandLine, gnm_py_command_line,
	gnm_py_command_line_class_init, gnm_py_command_line_init,
	GTK_TYPE_ENTRY)

