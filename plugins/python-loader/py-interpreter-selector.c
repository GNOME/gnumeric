/*
 * py-interpreter-selector.c: A widget that can be used to select Python
 *                            interpreter from the list of available ones.
 *
 * Author: Zbigniew Chyla (cyba@gnome.pl)
 */

#include <gnumeric-config.h>
#include <goffice/goffice.h>

#include <gsf/gsf-impl-utils.h>
#include <gnumeric.h>
#include <gui-util.h>
#include <goffice/app/module-plugin-defs.h>
#include "gnm-python.h"
#include "gnm-py-interpreter.h"
#include "py-interpreter-selector.h"

#include <string.h>
#include <glib.h>

struct _GnmPyInterpreterSelector {
	GtkComboBox parent;

	GnmPython *py_object;
	GnmPyInterpreter *cur_interpreter;
	GSList *added_interpreters;
};

typedef struct {
	GtkComboBoxClass parent_class;

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
cb_selector_changed (GnmPyInterpreterSelector *sel)
{
	GnmPyInterpreter *interpreter = NULL;
	GtkTreeIter iter;
	GtkTreePath *path = gtk_tree_path_new_from_indices (
						gtk_combo_box_get_active (GTK_COMBO_BOX (sel)), -1);
	GtkTreeModel *model = gtk_combo_box_get_model (GTK_COMBO_BOX (sel));

	if (gtk_tree_model_get_iter (model, &iter, path))
		gtk_tree_model_get (model, &iter, 1, &interpreter, -1);
	else
		g_warning ("Did not get a valid iterator");
	gtk_tree_path_free (path);
	if (interpreter && interpreter != sel->cur_interpreter) {
		sel->cur_interpreter = interpreter;
		g_signal_emit (sel, signals[INTERPRETER_CHANGED_SIGNAL], 0);
	}
}

static GtkTreePath *
find_item_with_interpreter (GnmPyInterpreterSelector *sel,
                                 GnmPyInterpreter *interpreter)
{
	GtkTreeModel *model = gtk_combo_box_get_model (GTK_COMBO_BOX (sel));
	GtkTreeIter iter;
	GnmPyInterpreter *i;

	if (gtk_tree_model_get_iter_first (model, &iter)) {
		do {
			gtk_tree_model_get (model, &iter, 1, &i, -1);
			if (i == interpreter)
				return gtk_tree_model_get_path (model, &iter);
		} while (gtk_tree_model_iter_next (model, &iter));
	}
	g_warning ("assertion '%s' failed", "interpreter != NULL");

	return NULL;
}

static void
menu_add_item_with_interpreter (GnmPyInterpreterSelector *sel,
                                GnmPyInterpreter *interpreter, int pos)
{
	GtkListStore *store = GTK_LIST_STORE (gtk_combo_box_get_model (GTK_COMBO_BOX (sel)));
	GtkTreeIter iter;

	if (pos < 0) {
		gtk_list_store_append (store, &iter);
	} else {
		gtk_list_store_insert (store, &iter, pos);
	}
	gtk_list_store_set (store, &iter, 0, gnm_py_interpreter_get_name (interpreter),
				1, interpreter, -1);
	GO_SLIST_PREPEND (sel->added_interpreters, interpreter);
	g_object_weak_ref (
		G_OBJECT (interpreter), (GWeakNotify) cb_destroyed_interpreter, sel);
}

static void
cb_created_interpreter (GObject *obj, GnmPyInterpreter *interpreter,
                        GnmPyInterpreterSelector *sel)
{
	int i = 0, newpos = -1;
	GtkTreeModel *model = gtk_combo_box_get_model (GTK_COMBO_BOX (sel));
	GtkTreeIter iter;
	GnmPyInterpreter *itp;

	if (gtk_tree_model_get_iter_first (model, &iter)) {
		do {
			gtk_tree_model_get (model, &iter, 1, &itp, -1);
			if (gnm_py_interpreter_compare (itp, interpreter) > 0) {
				newpos = i;
				break;
			}
			i++;
		} while (gtk_tree_model_iter_next (model, &iter));
	}
	menu_add_item_with_interpreter (sel, interpreter, newpos);
}

static void
cb_destroyed_interpreter (GnmPyInterpreterSelector *sel,
                          GnmPyInterpreter *ex_interpreter)
{
	GtkTreeIter iter;
	GtkTreeModel *model = gtk_combo_box_get_model (GTK_COMBO_BOX (sel));
	GtkTreePath *path = find_item_with_interpreter (sel, ex_interpreter);
	g_return_if_fail (path != NULL);

	GO_SLIST_REMOVE (sel->added_interpreters, ex_interpreter);
	if (gtk_tree_model_get_iter (model, &iter, path))
		gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
	else
		g_warning ("Did not get a valid iterator");
	gtk_tree_path_free (path);
	if (sel->cur_interpreter == ex_interpreter) {
		sel->cur_interpreter = gnm_python_get_default_interpreter (sel->py_object);
		path = find_item_with_interpreter (sel, sel->cur_interpreter);
		if (path) {
			gtk_combo_box_set_active (GTK_COMBO_BOX (sel), *gtk_tree_path_get_indices (path));
			gtk_tree_path_free (path);
		}
		g_signal_emit (sel, signals[INTERPRETER_CHANGED_SIGNAL], 0);
	}
}

static void
gnm_py_interpreter_selector_init (GnmPyInterpreterSelector *sel)
{
	GtkCellRenderer* renderer;
	GtkListStore *store;
	sel->py_object = NULL;
	sel->cur_interpreter = NULL;
	sel->added_interpreters = NULL;

	store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_POINTER);
	gtk_combo_box_set_model (GTK_COMBO_BOX (sel), GTK_TREE_MODEL (store));
	renderer = gtk_cell_renderer_text_new ();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (sel),
								renderer,
								FALSE);
	gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (sel), renderer,
									"text", 0,
									NULL);
}

static void
gnm_py_interpreter_selector_finalize (GObject *obj)
{
	GnmPyInterpreterSelector *sel = GNM_PY_INTERPRETER_SELECTOR (obj);

	if (sel->py_object != NULL)
		g_signal_handlers_disconnect_by_func (
			sel->py_object, G_CALLBACK (cb_created_interpreter),
			sel);
	GO_SLIST_FOREACH (sel->added_interpreters, GnmPyInterpreter, interpreter,
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
}

GtkWidget *
gnm_py_interpreter_selector_new (GOErrorInfo **err)
{
	GSList *interpreters;
	GtkTreePath *path;
	GObject *obj = g_object_new (GNM_PY_INTERPRETER_SELECTOR_TYPE, NULL);
	GnmPyInterpreterSelector *sel = GNM_PY_INTERPRETER_SELECTOR (obj);

	GO_INIT_RET_ERROR_INFO (err);
	sel->py_object = gnm_python_object_get (err);
	if (sel->py_object == NULL) {
		g_object_ref_sink (obj);
		g_object_unref (obj);
		return NULL;
	}
	g_signal_connect (
		sel->py_object, "created_interpreter",
		G_CALLBACK (cb_created_interpreter), sel);
	sel->added_interpreters = NULL;
	sel->cur_interpreter = gnm_python_get_default_interpreter (sel->py_object);

	interpreters = g_slist_copy (gnm_python_get_interpreters (sel->py_object));
	GO_SLIST_SORT (interpreters, gnm_py_interpreter_compare);
	g_assert (interpreters != NULL);
	GO_SLIST_FOREACH (interpreters, GnmPyInterpreter, interpreter,
		menu_add_item_with_interpreter (sel, interpreter, -1);
	);
	path = find_item_with_interpreter (sel, sel->cur_interpreter);
	if (path) {
		gtk_combo_box_set_active (GTK_COMBO_BOX (sel), *gtk_tree_path_get_indices (path));
		gtk_tree_path_free (path);
	}
	g_signal_connect (sel, "changed", G_CALLBACK (cb_selector_changed), NULL);
	g_slist_free (interpreters);
	return GTK_WIDGET (sel);
}

GnmPyInterpreter *
gnm_py_interpreter_selector_get_current (GnmPyInterpreterSelector *sel)
{
	return sel->cur_interpreter;
}

GSF_DYNAMIC_CLASS (GnmPyInterpreterSelector, gnm_py_interpreter_selector,
	gnm_py_interpreter_selector_class_init,
	gnm_py_interpreter_selector_init, GTK_TYPE_COMBO_BOX)
