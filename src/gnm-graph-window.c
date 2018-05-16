#include <gnumeric-config.h>

#include <gnm-graph-window.h>
#include <application.h>

#include <string.h>

#include <glib/gi18n-lib.h>

#include <goffice/goffice.h>

struct _GnmGraphWindow {
	GtkWindow parent;

	GtkWidget *vbox;

	GtkWidget *toolbar;
	GtkWidget *size_combo;

	GtkWidget *scrolled_window;

	GtkWidget *graph;
	double graph_height;
	double graph_width;

	gboolean is_fullscreen;
};

struct _GnmGraphWindowClass {
	GtkWindowClass parent_class;
};

/* keep in sync with gnm_graph_window_init */
typedef enum {
	CHART_SIZE_FIT = 0,
	CHART_SIZE_FIT_WIDTH,
	CHART_SIZE_FIT_HEIGHT,
	/* separator */
	CHART_SIZE_100 = 4,
	CHART_SIZE_125,
	CHART_SIZE_150,
	CHART_SIZE_200,
	CHART_SIZE_300,
	CHART_SIZE_500
} ChartSize;

G_DEFINE_TYPE (GnmGraphWindow, gnm_graph_window, GTK_TYPE_WINDOW)
#define parent_class gnm_graph_window_parent_class

static void
fullscreen_button_clicked (GtkToolButton  *button,
			   GnmGraphWindow *window)
{
	if (!window->is_fullscreen) {
		gtk_window_fullscreen (GTK_WINDOW (window));
		gtk_tool_button_set_icon_name (button, "view-restore");
	} else {
		gtk_window_unfullscreen (GTK_WINDOW (window));
		gtk_tool_button_set_icon_name (button, "view-fullscreen");
	}

	window->is_fullscreen = !window->is_fullscreen;
}

static void
update_graph_sizing_mode (GnmGraphWindow *window)
{
	int height, width;
	gboolean obey_ratio;
	GOGraphWidgetSizeMode size_mode;
	ChartSize size;

	g_return_if_fail (GO_IS_GRAPH_WIDGET (window->graph));

	obey_ratio = FALSE;

	size = gtk_combo_box_get_active (GTK_COMBO_BOX (window->size_combo));
	switch (size) {
		case CHART_SIZE_FIT:
			size_mode = GO_GRAPH_WIDGET_SIZE_MODE_FIT;
			height = width = -1;
			obey_ratio = TRUE;
			break;

		case CHART_SIZE_FIT_WIDTH:
			size_mode = GO_GRAPH_WIDGET_SIZE_MODE_FIT_WIDTH;
			height = width = -1;
			obey_ratio = TRUE;
			break;

		case CHART_SIZE_FIT_HEIGHT:
			size_mode = GO_GRAPH_WIDGET_SIZE_MODE_FIT_HEIGHT;
			height = width = -1;
			obey_ratio = TRUE;
			break;

		case CHART_SIZE_100:
			size_mode = GO_GRAPH_WIDGET_SIZE_MODE_FIXED_SIZE;
			width =  window->graph_width;
			height = window->graph_height;
			break;

		case CHART_SIZE_125:
			size_mode = GO_GRAPH_WIDGET_SIZE_MODE_FIXED_SIZE;
			width =  window->graph_width * 1.25;
			height = window->graph_height * 1.25;
			break;

		case CHART_SIZE_150:
			size_mode = GO_GRAPH_WIDGET_SIZE_MODE_FIXED_SIZE;
			width =  window->graph_width * 1.50;
			height = window->graph_height * 1.50;
			break;

		case CHART_SIZE_200:
			size_mode = GO_GRAPH_WIDGET_SIZE_MODE_FIXED_SIZE;
			width =  window->graph_width * 2.0;
			height = window->graph_height * 2.0;
			break;

		case CHART_SIZE_300:
			size_mode = GO_GRAPH_WIDGET_SIZE_MODE_FIXED_SIZE;
			width =  window->graph_width * 3.0;
			height = window->graph_height * 3.0;
			break;

		case CHART_SIZE_500:
			size_mode = GO_GRAPH_WIDGET_SIZE_MODE_FIXED_SIZE;
			width = window->graph_width * 5.0;
			height = window->graph_height * 5.0;
			break;

		default:
			g_assert_not_reached ();
			return;
	}

	g_object_set (window->graph,
		      "aspect-ratio", obey_ratio ? window->graph_height / window->graph_width : 0.0,
		      NULL);
	go_graph_widget_set_size_mode (GO_GRAPH_WIDGET (window->graph), size_mode, width, height);
}

static gboolean
size_combo_is_row_separator (GtkTreeModel *model,
			     GtkTreeIter *iter,
			     G_GNUC_UNUSED gpointer data)
{
	gboolean is_sep;
	char *str;

	gtk_tree_model_get (model, iter, 0, &str, -1);
	is_sep = (strcmp (str, "SEPARATOR") == 0);

	g_free (str);

	return is_sep;
}

static void
gnm_graph_window_init (GnmGraphWindow *window)
{
	GtkToolItem *item;
	unsigned int i;

	/* these indexes match the ChartSize enum */
	static char const * chart_sizes[] = {
		N_("Fit"),
		N_("Fit Width"),
		N_("Fit Height"),
		"SEPARATOR",
		N_("100%"),
		N_("125%"),
		N_("150%"),
		N_("200%"),
		N_("300%"),
		N_("500%")
	};

	window->vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_show (GTK_WIDGET (window->vbox));
	gtk_container_add (GTK_CONTAINER (window), window->vbox);

	window->toolbar = gtk_toolbar_new ();
	gtk_widget_show (GTK_WIDGET (window->toolbar));
	gtk_box_pack_start (GTK_BOX (window->vbox), window->toolbar, FALSE, FALSE, 0);

	window->scrolled_window = gtk_scrolled_window_new (NULL, NULL);
	gtk_widget_show (GTK_WIDGET (window->scrolled_window));
	gtk_container_add (GTK_CONTAINER (window->vbox), window->scrolled_window);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (window->scrolled_window),
					GTK_POLICY_NEVER, GTK_POLICY_NEVER);

	item = gtk_tool_item_new ();
	gtk_widget_show (GTK_WIDGET (item));
	gtk_toolbar_insert (GTK_TOOLBAR (window->toolbar), item, -1);

	window->size_combo = gtk_combo_box_text_new ();
	for (i = 0; i < G_N_ELEMENTS (chart_sizes); i++)
		gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (window->size_combo), _(chart_sizes[i]));
	gtk_widget_set_sensitive (window->size_combo, FALSE);
	gtk_widget_show (window->size_combo);
	gtk_combo_box_set_active (GTK_COMBO_BOX (window->size_combo), CHART_SIZE_FIT);
	gtk_combo_box_set_row_separator_func (GTK_COMBO_BOX (window->size_combo),
					      size_combo_is_row_separator, NULL, NULL);
	gtk_container_add (GTK_CONTAINER (item), window->size_combo);
	g_signal_connect_swapped (window->size_combo, "changed",
				  G_CALLBACK (update_graph_sizing_mode), window);

	item = g_object_new (GTK_TYPE_TOOL_BUTTON,
			     "icon-name", "view-fullscreen",
			     NULL);
	gtk_widget_show (GTK_WIDGET (item));

	gtk_toolbar_insert (GTK_TOOLBAR (window->toolbar), item, -1);
	g_signal_connect (item, "clicked",
			  G_CALLBACK (fullscreen_button_clicked), window);

	gtk_window_set_title (GTK_WINDOW (window), "Chart Viewer");
}

static void
gnm_graph_window_class_init (GnmGraphWindowClass *class)
{
}

static void
gnm_graph_window_set_graph (GnmGraphWindow *window,
			    GogGraph       *graph,
			    gdouble         graph_width,
			    gdouble         graph_height)
{
	GtkRequisition toolbar_requisition;
	GogGraph *old_graph =
		window->graph != NULL ?
		go_graph_widget_get_graph (GO_GRAPH_WIDGET (window->graph)) :
		NULL;

	if (graph == old_graph)
		return;

	if (old_graph != NULL) {
		gtk_container_remove (GTK_CONTAINER (window->scrolled_window), window->graph);
		g_object_unref (window->graph);
		window->graph = NULL;
	}

	if (graph != NULL) {
		graph = gog_graph_dup (graph);
		window->graph = g_object_new (GO_TYPE_GRAPH_WIDGET,
					      "graph", graph,
					      "hres", gnm_app_display_dpi_get (TRUE),
					      "vres", gnm_app_display_dpi_get (FALSE),
					      NULL);
		g_object_unref (graph);
		gtk_widget_show (window->graph);
		gtk_container_add (GTK_CONTAINER (window->scrolled_window), window->graph);
		g_object_set (G_OBJECT (window->graph), "expand", TRUE, NULL);

		gtk_widget_get_preferred_size (window->toolbar, &toolbar_requisition, NULL);
		gtk_window_set_default_size (GTK_WINDOW (window),
					     (int) graph_width,
					     (int) graph_height + toolbar_requisition.height);

		window->graph_width = graph_width;
		window->graph_height = graph_height;

		/* ensure that the aspect ratio is updated */
		gtk_widget_set_sensitive (window->size_combo, TRUE);
		g_signal_emit_by_name (window->size_combo, "changed");
	}
}

GtkWidget *
gnm_graph_window_new (GogGraph *graph,
		      gdouble   graph_width,
		      gdouble   graph_height)
{
	GtkWidget *ret;

	g_return_val_if_fail (GOG_IS_GRAPH (graph), NULL);

	ret = g_object_new (gnm_graph_window_get_type (), NULL);
	gnm_graph_window_set_graph (GNM_GRAPH_WINDOW (ret), graph, graph_width, graph_height);

	return ret;
}
