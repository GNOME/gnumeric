/*
 * py-interpreter-selector.c: A widget that can be used to select Python
 *                            interpreter from the list of available ones.
 *
 * Author: Zbigniew Chyla (cyba@gnome.pl)
 */

#include <gnumeric-config.h>
#include <string.h>
#include <glib.h>
#include <gtk/gtkoptionmenu.h>
#include <gtk/gtkmenu.h>
#include <gtk/gtkmenuitem.h>
#include <gtk/gtkmenushell.h>
#include <gsf/gsf-impl-utils.h>
#include <gnumeric.h>
#include <gutils.h>
#include <gui-util.h>
#include <module-plugin-defs.h>
#include "gnm-python.h"
#include "gnm-py-interpreter.h"
#include "py-interpreter-selector.h"


struct _GnmPyInterpreterSelector {
	GtkOptionMenu parent;

	GnmPython *py_object;
	GnmPyInterpreter *cur_interpreter;
	GSList *added_interpreters;
};

typedef struct {
	GtkOptionMenuClass parent_class;

	void (*interpreter_changed) (GnmPyInterpreterSelector *sel);
} GnmPyInterpreterSelectorClass;

enum {
	INTERPRETER_CHANGED_SIGNAL,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };
static GObjectClass *parent_class = NULL;


static void cb_destroyed_interpreter (GnmPyInterpreterSelector *sel,
                                      GnmPyInterpreter *interpreter);

static void
cb_selector_item_activated (GObject *item, GnmPyInterpreterSelector *sel)
{
	GnmPyInterpreter *interpreter;
	
	interpreter = g_object_get_data (item, "py-interpreter");
	if (interpreter != sel->cur_interpreter) {
		sel->cur_interpreter = interpreter;
		g_signal_emit (sel, signals[INTERPRETER_CHANGED_SIGNAL], 0);
	}
}

static GtkWidget *
menu_find_item_with_interpreter (GtkWidget *menu,
                                 GnmPyInterpreter *interpreter)
{
	GNM_LIST_FOREACH (GTK_MENU_SHELL (menu)->children, GtkWidget, item,
		if (interpreter == g_object_get_data (G_OBJECT (item), "py-interpreter")) {
			return item;
		}
	);
	g_return_val_if_fail (FALSE, NULL);
}

static void
menu_add_item_with_interpreter (GnmPyInterpreterSelector *sel, GtkWidget *menu,
                                GnmPyInterpreter *interpreter, int pos)
{
	GtkWidget *item;

	item = gtk_menu_item_new_with_label (
		gnm_py_interpreter_get_name (interpreter));
	g_object_set_data (G_OBJECT (item), "py-interpreter", interpreter);
	g_signal_connect (
		item, "activate",
		G_CALLBACK (cb_selector_item_activated),
		G_OBJECT (sel));
	gtk_widget_show (item);
	gtk_menu_insert (GTK_MENU (menu), item, pos);
	GNM_SLIST_PREPEND (sel->added_interpreters, interpreter);
	g_object_weak_ref (
		G_OBJECT (interpreter), (GWeakNotify) cb_destroyed_interpreter, sel);
}

static void
cb_created_interpreter (GObject *obj, GnmPyInterpreter *interpreter,
                        GnmPyInterpreterSelector *sel)
{
	GList *l;
	int i, newpos = -1;
	GtkWidget *menu;

	menu = gtk_option_menu_get_menu (GTK_OPTION_MENU (sel));
	for (l = GTK_MENU_SHELL (menu)->children, i = 0; l != NULL; l = l->next, i++) {
		GnmPyInterpreter *itp;

		itp = g_object_get_data (G_OBJECT (l->data), "py-interpreter");
		if (gnm_py_interpreter_compare (itp, interpreter) > 0) {
			newpos = i;
			break;
		}
	}
	menu_add_item_with_interpreter (sel, menu, interpreter, newpos);
}

static void
cb_destroyed_interpreter (GnmPyInterpreterSelector *sel,
                          GnmPyInterpreter *ex_interpreter)
{
	GtkWidget *menu;

	GNM_SLIST_REMOVE (sel->added_interpreters, ex_interpreter);
	menu = gtk_option_menu_get_menu (GTK_OPTION_MENU (sel));
	gtk_object_destroy (
		GTK_OBJECT (menu_find_item_with_interpreter (menu, ex_interpreter)));
	if (sel->cur_interpreter == ex_interpreter) {
		sel->cur_interpreter = gnm_python_get_default_interpreter (sel->py_object);
		g_signal_emit (sel, signals[INTERPRETER_CHANGED_SIGNAL], 0);
	}
}

static void
gnm_py_interpreter_selector_init (GnmPyInterpreterSelector *sel)
{
	GSList *interpreters;
	GtkWidget *menu;

	sel->py_object = gnm_python_object_get ();
	g_signal_connect (
		sel->py_object, "created_interpreter",
		G_CALLBACK (cb_created_interpreter), sel);
	sel->added_interpreters = NULL;
	sel->cur_interpreter = gnm_python_get_default_interpreter (sel->py_object);

	interpreters = g_slist_copy (gnm_python_get_interpreters (sel->py_object));
	GNM_SLIST_SORT (interpreters, gnm_py_interpreter_compare);
	g_assert (interpreters != NULL);
	menu = gtk_menu_new ();
	GNM_SLIST_FOREACH (interpreters, GnmPyInterpreter, interpreter,
		menu_add_item_with_interpreter (sel, menu, interpreter, -1);
	);
	gtk_option_menu_set_menu (GTK_OPTION_MENU (sel), menu);
	g_slist_free (interpreters);
}

static void
gnm_py_interpreter_selector_finalize (GObject *obj)
{
	GnmPyInterpreterSelector *sel = GNM_PY_INTERPRETER_SELECTOR (obj);

	g_signal_handlers_disconnect_by_func (
		sel->py_object, G_CALLBACK (cb_created_interpreter), sel);
	GNM_SLIST_FOREACH (sel->added_interpreters, GnmPyInterpreter, interpreter,
		g_object_weak_unref (
			G_OBJECT (interpreter), (GWeakNotify) cb_destroyed_interpreter, sel);
	);
	if (sel->py_object != NULL) {
		g_object_unref (sel->py_object);
		sel->py_object = NULL;
	}
	g_slist_free (sel->added_interpreters);
	sel->added_interpreters = NULL;

	parent_class->finalize (obj);
}

static void
gnm_py_interpreter_selector_class_init (GObjectClass *gobject_class)
{
	parent_class = g_type_class_peek_parent (gobject_class);

	gobject_class->finalize = gnm_py_interpreter_selector_finalize;

	signals[INTERPRETER_CHANGED_SIGNAL] =
	g_signal_new (
		"interpreter_changed",
		G_TYPE_FROM_CLASS (gobject_class),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (GnmPyInterpreterSelectorClass, interpreter_changed),
		NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 0);

	/* we registered a static class, don't unload the plugin */
	g_type_plugin_use (G_TYPE_PLUGIN (PLUGIN));
}

GSF_CLASS (
	GnmPyInterpreterSelector, gnm_py_interpreter_selector,
	gnm_py_interpreter_selector_class_init,
	gnm_py_interpreter_selector_init, GTK_TYPE_OPTION_MENU)


GtkWidget *
gnm_py_interpreter_selector_new (void)
{
	return gtk_type_new (GNM_PY_INTERPRETER_SELECTOR_TYPE);
}

GnmPyInterpreter *
gnm_py_interpreter_selector_get_current (GnmPyInterpreterSelector *sel)
{
	return sel->cur_interpreter;
}
