/*
 * Gnumeric, the GNOME spreadsheet.
 *
 * Graphics Wizard bootstap file
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org)
 */
#include <config.h>
#include <gnome.h>
#include "gnumeric.h"
#include <glade/glade.h>
#include "idl/Graph.h"
#include "wizard.h"
#include "graphic-type.h"

typedef struct {
	char *description;
	int   col, row;
	PlotParameters par;
} subtype_info_t;

static subtype_info_t column_subtypes [] = {
	{
		N_("Clustered columns.  Compares values between categories"),
		1, 1,
		{
			TRUE,
			GNOME_Graph_CHART_TYPE_CLUSTERED,
			GNOME_Graph_PLOT_COLBAR,
			GNOME_Graph_COLBAR_FLAT,
			GNOME_Graph_DIR_COL
		}
	},

	{
		N_("Stacked columns.  Compares the values brought by each category"),
		1, 2,
		{
			TRUE,
			GNOME_Graph_CHART_TYPE_STACKED,
			GNOME_Graph_PLOT_COLBAR,
			GNOME_Graph_COLBAR_FLAT,
			GNOME_Graph_DIR_COL
		}

	},

	{
		N_("Stacked columns, 100%.  Compares between the categories the values broght to a total"),
		1, 3,
		{
			TRUE,
			GNOME_Graph_CHART_TYPE_STACKED_FULL,
			GNOME_Graph_PLOT_COLBAR,
			GNOME_Graph_COLBAR_FLAT,
			GNOME_Graph_DIR_COL
		}
	},

	{ NULL }
};

static subtype_info_t bar_subtypes [] = {
	{
		N_("Clustered bars.  Compares values between categories"),
		1, 1,
		{
			TRUE,
			GNOME_Graph_CHART_TYPE_CLUSTERED,
			GNOME_Graph_PLOT_COLBAR,
			GNOME_Graph_COLBAR_FLAT,
			GNOME_Graph_DIR_BAR
		}
	},

	{
		N_("Stacked bars.  Compares the values brought by each category"),
		1, 2,
		{
			TRUE,
			GNOME_Graph_CHART_TYPE_STACKED,
			GNOME_Graph_PLOT_COLBAR,
			GNOME_Graph_COLBAR_FLAT,
			GNOME_Graph_DIR_BAR
		}

	},

	{
		N_("Stacked bars, 100%.  Compares between the categories the values broght to a total"),
		1, 3,
		{
			TRUE, 
			GNOME_Graph_CHART_TYPE_STACKED_FULL,
			GNOME_Graph_PLOT_COLBAR,
			GNOME_Graph_COLBAR_FLAT,
			GNOME_Graph_DIR_BAR
		}
	},

	{ NULL }
};

static subtype_info_t lines_subtypes [] = {
	{
		N_("Lines.  FIXME: Add nice explanation"),
		1, 1,
		{
			TRUE,
			GNOME_Graph_CHART_TYPE_CLUSTERED,
			GNOME_Graph_PLOT_LINES,
			GNOME_Graph_COLBAR_FLAT,
			GNOME_Graph_DIR_COL,
			GNOME_Graph_LINE_PLAIN, 
		}
	},

	{
		N_("Stacked Lines.  FIXME: Add nice explanation"),
		1, 2,
		{
			TRUE,
			GNOME_Graph_CHART_TYPE_STACKED,
			GNOME_Graph_PLOT_LINES,
			GNOME_Graph_COLBAR_FLAT,
			GNOME_Graph_DIR_COL,
			GNOME_Graph_LINE_PLAIN
		}

	},

	{
		N_("Stacked lines, 100%.  FIXME: Add nice explanation"),
		1, 3,
		{
			TRUE,
			GNOME_Graph_CHART_TYPE_STACKED_FULL,
			GNOME_Graph_PLOT_LINES,
			GNOME_Graph_COLBAR_FLAT,
			GNOME_Graph_DIR_COL,
			GNOME_Graph_LINE_PLAIN
		}

	},
	{
		N_("Lines with markers.  FIXME: Add nice explanation"),
		2, 1,
		{
			TRUE,
			GNOME_Graph_CHART_TYPE_CLUSTERED,
			GNOME_Graph_PLOT_LINES,
			GNOME_Graph_COLBAR_FLAT,
			GNOME_Graph_DIR_COL,
			GNOME_Graph_LINE_MARKERS, 
		}
	},

	{
		N_("Stacked lines with markers.  FIXME: Add nice explanation"),
		2, 2,
		{
			TRUE,
			GNOME_Graph_CHART_TYPE_STACKED,
			GNOME_Graph_PLOT_LINES,
			GNOME_Graph_COLBAR_FLAT,
			GNOME_Graph_DIR_COL,
			GNOME_Graph_LINE_MARKERS
		}
	},

	{
		N_("Stacked lines with markers, 100%.  FIXME: Add nice explanation"),
		2, 3,
		{
			TRUE,
			GNOME_Graph_CHART_TYPE_STACKED_FULL,
			GNOME_Graph_PLOT_LINES,
			GNOME_Graph_COLBAR_FLAT,
			GNOME_Graph_DIR_COL,
			GNOME_Graph_LINE_MARKERS
		}
	},

	{ NULL }
};

static subtype_info_t scatter_subtypes [] = {
	{
		N_("Scatter points"),
		1, 1,
		{
			FALSE,
			GNOME_Graph_CHART_TYPE_SCATTER,
			0, /* plot_mode ignored */
			0, /* col_bar_mode ignored */
			0, /* direction ignored */
			GNOME_Graph_LINE_PLAIN,
			0, /* pie_mode ignored */
			0, /* pie_dim ignored */
			GNOME_Graph_SCATTER_POINTS,
			GNOME_Graph_SCATTER_CONN_NONE,
		},
	},

	{
		N_("Scatter points connected with lines"),
		3, 1,
		{
			FALSE,
			GNOME_Graph_CHART_TYPE_SCATTER,
			0, /* plot_mode ignored */
			0, /* col_bar_mode ignored */
			0, /* direction ignored */
			GNOME_Graph_LINE_PLAIN,
			0, /* pie_mode ignored */
			0, /* pie_dim ignored */
			GNOME_Graph_SCATTER_POINTS,
			GNOME_Graph_SCATTER_CONN_LINES,
		},
	},

	{
		N_("Scatter data connected with lines"),
		3, 2,
		{
			FALSE,
			GNOME_Graph_CHART_TYPE_SCATTER,
			0, /* plot_mode ignored */
			0, /* col_bar_mode ignored */
			0, /* direction ignored */
			GNOME_Graph_LINE_PLAIN,
			0, /* pie_mode ignored */
			0, /* pie_dim ignored */
			GNOME_Graph_SCATTER_NONE,
			GNOME_Graph_SCATTER_CONN_LINES,
		},
	},
	
	{ NULL }
};

static subtype_info_t area_subtypes [] = {
	{
		N_("Area 1: FIXME add nice description"),
		1, 1,
		{
			TRUE,
			GNOME_Graph_CHART_TYPE_CLUSTERED,
			GNOME_Graph_PLOT_AREA,
			GNOME_Graph_COLBAR_FLAT,
			GNOME_Graph_DIR_COL
		}
	},

	{
		N_("Stacked area.  FIXME: add nic description"),
		1, 2,
		{
			TRUE,
			GNOME_Graph_CHART_TYPE_STACKED,
			GNOME_Graph_PLOT_AREA,
			GNOME_Graph_COLBAR_FLAT,
			GNOME_Graph_DIR_COL
		}

	},

	{
		N_("Stacked Area, 100%.  FIXME: add nice description"),
		1, 3,
		{
			TRUE,
			GNOME_Graph_CHART_TYPE_STACKED_FULL,
			GNOME_Graph_PLOT_AREA,
			GNOME_Graph_COLBAR_FLAT,
			GNOME_Graph_DIR_COL
		}
	},
	
	{ NULL }
};

static struct {
	char *text;
	char *icon_name;
	subtype_info_t *subtypes;
} graphic_types [] = {
	{ N_("Column"),  "chart_column",  column_subtypes  },
	{ N_("Bar"),     "chart_bar",     bar_subtypes     },
	{ N_("Lines"),   "chart_line",    lines_subtypes   },
	{ N_("Scatter"), "chart_scatter", scatter_subtypes },
	{ N_("Areas"),   "chart_area",    area_subtypes    },
	{ NULL, NULL, NULL }
};

void
graphic_type_show_page (WizardGraphicContext *gc, int n)
{
	gtk_notebook_set_page (gc->graphic_types_notebook, n);
	graphic_type_set_chart_mode (gc->chart, &graphic_types [n].subtypes [0].par);
}

void
graphic_type_show_preview (WizardGraphicContext *gc)
{
	gc->last_graphic_type_page = gtk_notebook_get_current_page (gc->graphic_types_notebook);
	gtk_notebook_set_page (gc->graphic_types_notebook, 5);
}

void
graphic_type_restore_view (WizardGraphicContext *gc)
{
	gtk_notebook_set_page (gc->graphic_types_notebook, gc->last_graphic_type_page);
	gc->last_graphic_type_page = -1;
}

static void
graphic_type_selected (GtkCList *clist, gint row, gint column, GdkEvent *event,
		       WizardGraphicContext *gc)
{
	graphic_type_show_page (gc, row);
}

void
graphic_type_set_chart_mode (GNOME_Graph_Chart chart, PlotParameters *par)
{
	CORBA_Environment ev;

	CORBA_exception_init (&ev);

	GNOME_Graph_Chart__set_chart_type   (chart, par->chart_type, &ev);
	GNOME_Graph_Chart__set_plot_mode    (chart, par->plot_mode, &ev);
	GNOME_Graph_Chart__set_col_bar_mode (chart, par->col_bar_mode, &ev);
	GNOME_Graph_Chart__set_direction    (chart, par->direction, &ev);
	GNOME_Graph_Chart__set_line_mode    (chart, par->line_mode, &ev);
	GNOME_Graph_Chart__set_pie_mode     (chart, par->pie_mode, &ev);
	GNOME_Graph_Chart__set_pie_dim      (chart, par->pie_dim, &ev);
	GNOME_Graph_Chart__set_scatter_mode (chart, par->scatter_mode, &ev);
	GNOME_Graph_Chart__set_scatter_conn (chart, par->scatter_conn, &ev);
	GNOME_Graph_Chart__set_surface_mode (chart, par->surface_mode, &ev);
	GNOME_Graph_Chart__set_with_labels  (chart, par->with_labels, &ev);
	CORBA_exception_free (&ev);
}

static void
graph_type_button_clicked (GtkWidget *widget, subtype_info_t *type)
{
	WizardGraphicContext *gc = gtk_object_get_user_data (GTK_OBJECT (widget));
	GtkLabel *label = GTK_LABEL (glade_xml_get_widget (gc->gui, "plot-description"));

	gtk_label_set_text (label, type->description);
	graphic_type_set_chart_mode (gc->chart, &type->par);
}

static void
show_sample_pressed (GtkWidget *button, WizardGraphicContext *gc)
{
	graphic_type_show_preview (gc);
}

static void
show_sample_released (GtkWidget *button, WizardGraphicContext *gc)
{
	graphic_type_restore_view (gc);
}

void
graphic_type_boot (GladeXML *gui, WizardGraphicContext *gc)
{
	GtkCList *clist = GTK_CLIST (glade_xml_get_widget (gui, "graphic-type-clist"));
	GtkTable *table = GTK_TABLE (glade_xml_get_widget (gui, "graphic-selector-table"));
	GtkObject *show_sample = GTK_OBJECT(glade_xml_get_widget (gui, "show-sample"));
	int i;

	gc->graphic_types_notebook = GTK_NOTEBOOK (gtk_notebook_new ());
	gtk_widget_show (GTK_WIDGET (gc->graphic_types_notebook));
	gtk_table_attach (table, GTK_WIDGET (gc->graphic_types_notebook),
			  1, 2, 1, 3,
			  GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL,
			  0, 0);
	gtk_notebook_set_show_tabs (gc->graphic_types_notebook, FALSE);
	gtk_container_set_border_width (GTK_CONTAINER (gc->graphic_types_notebook), 0);
	gtk_notebook_set_show_border (gc->graphic_types_notebook, FALSE);
	
	gtk_clist_set_column_width (clist, 0, 16);
	gtk_clist_set_column_justification (clist, 1, GTK_JUSTIFY_LEFT);
	gtk_signal_connect (GTK_OBJECT (clist), "select_row",
			    GTK_SIGNAL_FUNC (graphic_type_selected), gc);
	
	for (i = 0; graphic_types [i].text; i++){
		GtkWidget *display_table;
		subtype_info_t *type_info;
		char *clist_text [2];
		int j;

		clist_text [0] = "";
		clist_text [1] = _(graphic_types [i].text);
		gtk_clist_append (clist, clist_text);

		type_info = graphic_types [i].subtypes;

		display_table = gtk_table_new (0, 0, 0);
		gtk_widget_show (display_table);
		gtk_notebook_append_page (gc->graphic_types_notebook, display_table, NULL);
			
		for (j = 0; type_info [j].description != NULL; j++){
			GtkWidget *happy_button, *pix;
			char *button_name;
			
			happy_button = gtk_button_new ();
			gtk_widget_show (happy_button);
			gtk_button_set_relief (GTK_BUTTON (happy_button), GTK_RELIEF_NONE);

			button_name = g_strdup_printf (
				"%s/%s_%d_%d.png",
				GNUMERIC_ICONSDIR, graphic_types [i].icon_name,
				type_info [j].col, type_info [j].row);
			pix = gnome_pixmap_new_from_file (button_name);
			if (pix){
				gtk_container_add (GTK_CONTAINER (happy_button), pix);
				gtk_widget_show (pix);
			}
			g_free (button_name);

			gtk_table_attach (
				GTK_TABLE (display_table), happy_button,
				type_info [j].row, type_info [j].row+1,
				type_info [j].col, type_info [j].col+1,
				0, 0, 4, 4);

			gtk_signal_connect (
				GTK_OBJECT (happy_button), "clicked",
				GTK_SIGNAL_FUNC(graph_type_button_clicked), &type_info [j]);
			gtk_object_set_user_data (GTK_OBJECT (happy_button), gc);

			if (i == 0 && j == 0)
				gtk_button_clicked (GTK_BUTTON (happy_button));
		}
		
	}

	{
		BonoboViewFrame *frame;
		GtkWidget *view;
		
		frame = graphic_context_new_chart_view_frame (gc);
		view = bonobo_view_frame_get_wrapper (frame);
		gtk_widget_show (view);
		gtk_notebook_append_page (gc->graphic_types_notebook, view, NULL);
	}

	gtk_clist_select_row (clist, 0, 1);
	gtk_clist_thaw (clist);

	/*
	 * Connect the "Show Preview" button
	 */

	gtk_signal_connect (
		show_sample, "pressed", GTK_SIGNAL_FUNC (show_sample_pressed), gc);
	gtk_signal_connect (
		show_sample, "released", GTK_SIGNAL_FUNC (show_sample_released), gc);
}

/*
 * Set a chart mode default
 */
void
graphic_type_init_preview (WizardGraphicContext *gc)
{
	graphic_type_set_chart_mode (gc->chart, &column_subtypes [0].par);
}
