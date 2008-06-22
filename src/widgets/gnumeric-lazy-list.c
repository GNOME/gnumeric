#include <gnumeric-config.h>
#include "gnumeric-lazy-list.h"
#include <glib/gi18n-lib.h>
#include <gnm-marshalers.h>

#include <gtk/gtktreemodel.h>

static GObjectClass *lazy_list_parent_class = NULL;

static void
lazy_list_finalize (GObject *object)
{
	GnumericLazyList *ll = GNUMERIC_LAZY_LIST (object);

	g_free (ll->column_headers);

	/* must chain up */
	lazy_list_parent_class->finalize (object);
}

static void
lazy_list_init (GnumericLazyList *ll)
{
	ll->stamp = 42;
	ll->rows = 0;
	ll->cols = 0;
	ll->get_value = NULL;
	ll->column_headers = NULL;
	ll->user_data = NULL;
}

static void
lazy_list_class_init (GnumericLazyListClass *class)
{
	GObjectClass *object_class;

	lazy_list_parent_class = g_type_class_peek_parent (class);
	object_class = (GObjectClass*) class;

	object_class->finalize = lazy_list_finalize;
}


/* Fulfill the GtkTreeModel requirements */
static GtkTreeModelFlags
lazy_list_get_flags (GtkTreeModel *tree_model)
{
	g_return_val_if_fail (GNUMERIC_IS_LAZY_LIST (tree_model), 0);

	return GTK_TREE_MODEL_ITERS_PERSIST | GTK_TREE_MODEL_LIST_ONLY;
}

static gint
lazy_list_get_n_columns (GtkTreeModel *tree_model)
{
	GnumericLazyList *ll = (GnumericLazyList *) tree_model;

	g_return_val_if_fail (GNUMERIC_IS_LAZY_LIST (tree_model), 0);

	return ll->cols;
}

static GType
lazy_list_get_column_type (GtkTreeModel *tree_model,
			   gint          index)
{
	GnumericLazyList *ll = (GnumericLazyList *) tree_model;

	g_return_val_if_fail (GNUMERIC_IS_LAZY_LIST (tree_model), G_TYPE_INVALID);
	g_return_val_if_fail (index >= 0 && index < ll->cols, G_TYPE_INVALID);

	return ll->column_headers[index];
}

static gboolean
lazy_list_get_iter (GtkTreeModel *tree_model,
		    GtkTreeIter  *iter,
		    GtkTreePath  *path)
{
	GnumericLazyList *ll = (GnumericLazyList *) tree_model;
	gint i;

	g_return_val_if_fail (GNUMERIC_IS_LAZY_LIST (tree_model), FALSE);
	g_return_val_if_fail (gtk_tree_path_get_depth (path) > 0, FALSE);

	i = gtk_tree_path_get_indices (path)[0];
	if (i < 0 || i >= ll->rows)
		return FALSE;

	iter->stamp = ll->stamp;
	iter->user_data = GINT_TO_POINTER (i);

	return TRUE;
}

static GtkTreePath *
lazy_list_get_path (G_GNUC_UNUSED GtkTreeModel *tree_model,
		    GtkTreeIter  *iter)
{
	GtkTreePath *retval = gtk_tree_path_new ();
	gtk_tree_path_append_index (retval, GPOINTER_TO_INT (iter->user_data));
	return retval;
}

static void
lazy_list_get_value (GtkTreeModel *tree_model,
		     GtkTreeIter  *iter,
		     gint          column,
		     GValue       *value)
{
	gint row;
	GnumericLazyList *ll = (GnumericLazyList *) tree_model;

	g_return_if_fail (GNUMERIC_IS_LAZY_LIST (tree_model));
	row = GPOINTER_TO_INT (iter->user_data);

	if (ll->get_value)
		ll->get_value (row, column, ll->user_data, value);
	else
		g_value_init (value, ll->column_headers[column]);
}

static gboolean
lazy_list_iter_next (GtkTreeModel  *tree_model,
		     GtkTreeIter   *iter)
{
	gint row;
	GnumericLazyList *ll = (GnumericLazyList *) tree_model;

	g_return_val_if_fail (GNUMERIC_IS_LAZY_LIST (tree_model), FALSE);
	row = GPOINTER_TO_INT (iter->user_data);
	row++;
	iter->user_data = GINT_TO_POINTER (row);

	return (row < ll->rows);
}

static gboolean
lazy_list_iter_children (GtkTreeModel *tree_model,
			 GtkTreeIter  *iter,
			 GtkTreeIter  *parent)
{
	GnumericLazyList *ll = (GnumericLazyList *) tree_model;

	/* this is a list, nodes have no children */
	if (parent)
		return FALSE;

	/* but if parent == NULL we return the list itself as children of the
	 * "root"
	 */

	iter->stamp = ll->stamp;
	iter->user_data = NULL;
	return ll->rows > 0;
}

static gboolean
lazy_list_iter_has_child (G_GNUC_UNUSED GtkTreeModel *tree_model,
			  G_GNUC_UNUSED GtkTreeIter  *iter)
{
	return FALSE;
}

static gint
lazy_list_iter_n_children (GtkTreeModel *tree_model,
			   GtkTreeIter  *iter)
{
	g_return_val_if_fail (GNUMERIC_IS_LAZY_LIST (tree_model), -1);
	if (iter == NULL)
		return GNUMERIC_LAZY_LIST (tree_model)->rows;

	return 0;
}

static gboolean
lazy_list_iter_nth_child (GtkTreeModel *tree_model,
			  GtkTreeIter  *iter,
			  GtkTreeIter  *parent,
			  gint          n)
{
	GnumericLazyList *ll = (GnumericLazyList *) tree_model;

	g_return_val_if_fail (GNUMERIC_IS_LAZY_LIST (tree_model), FALSE);

	if (parent)
		return FALSE;

	iter->stamp = ll->stamp;
	iter->user_data = GINT_TO_POINTER (n);

	return (n >= 0 && n < ll->rows);
}

static gboolean
lazy_list_iter_parent (G_GNUC_UNUSED GtkTreeModel *tree_model,
		       G_GNUC_UNUSED GtkTreeIter  *iter,
		       G_GNUC_UNUSED GtkTreeIter  *child)
{
	return FALSE;
}

static void
lazy_list_tree_model_init (GtkTreeModelIface *iface)
{
	iface->get_flags = lazy_list_get_flags;
	iface->get_n_columns = lazy_list_get_n_columns;
	iface->get_column_type = lazy_list_get_column_type;
	iface->get_iter = lazy_list_get_iter;
	iface->get_path = lazy_list_get_path;
	iface->get_value = lazy_list_get_value;
	iface->iter_next = lazy_list_iter_next;
	iface->iter_children = lazy_list_iter_children;
	iface->iter_has_child = lazy_list_iter_has_child;
	iface->iter_n_children = lazy_list_iter_n_children;
	iface->iter_nth_child = lazy_list_iter_nth_child;
	iface->iter_parent = lazy_list_iter_parent;
}

GType
gnumeric_lazy_list_get_type (void)
{
	static GType lazy_list_type = 0;

	if (!lazy_list_type) {
		static const GTypeInfo lazy_list_info =
			{
				sizeof (GnumericLazyListClass),
				NULL,		/* base_init */
				NULL,		/* base_finalize */
				(GClassInitFunc) lazy_list_class_init,
				NULL,		/* class_finalize */
				NULL,		/* class_data */
				sizeof (GnumericLazyList),
				0,
				(GInstanceInitFunc) lazy_list_init,
			};

		static const GInterfaceInfo tree_model_info =
			{
				(GInterfaceInitFunc) lazy_list_tree_model_init,
				NULL,
				NULL
			};

		lazy_list_type = g_type_register_static (G_TYPE_OBJECT, "GnumericLazyList", &lazy_list_info, 0);
		g_type_add_interface_static (lazy_list_type,
					     GTK_TYPE_TREE_MODEL,
					     &tree_model_info);
	}

	return lazy_list_type;
}

GnumericLazyList *
gnumeric_lazy_list_new (GnumericLazyListValueGetFunc get_value,
			gpointer user_data,
			gint n_rows,
			gint n_columns,
			...)
{
	GnumericLazyList *retval;
	va_list args;
	gint i;

	g_return_val_if_fail (n_rows >= 0, NULL);
	g_return_val_if_fail (n_columns >= 0, NULL);

	retval = GNUMERIC_LAZY_LIST (g_object_new (gnumeric_lazy_list_get_type (), NULL));
	retval->get_value = get_value;
	retval->user_data = user_data;
	retval->rows = n_rows;
	retval->cols = n_columns;
	retval->column_headers = g_new (GType, n_columns);

	va_start (args, n_columns);
	for (i = 0; i < n_columns; i++)
		retval->column_headers[i] = va_arg (args, GType);
	va_end (args);

	return retval;
}

void
gnumeric_lazy_list_add_column (GnumericLazyList *ll, int count, GType typ)
{
	int i;

	g_return_if_fail (GNUMERIC_IS_LAZY_LIST (ll));
	g_return_if_fail (count >= 0);

	ll->column_headers = g_renew (GType, ll->column_headers,
				      ll->cols + count);
	for (i = 0; i < count; i++)
		ll->column_headers[ll->cols++] = typ;
}
