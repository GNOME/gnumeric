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
#include "wizard.h"
#include "../../graph/Graph.h"

typedef struct {
	GNOME_Graph_ChartType     chart_type;
	GNOME_Graph_PlotMode      plot_mode;
	GNOME_Graph_ColBarMode    col_bar_mode;
	GNOME_Graph_DirMode       direction;
	GNOME_Graph_LineMode      line_mode;
	GNOME_Graph_PieMode       pie_mode;
	GNOME_Graph_PieDimension  pie_dim;
	GNOME_Graph_ScatterPoints scatter_mode;
	GNOME_Graph_ScatterConn   scatter_conn;
	GNOME_Graph_SurfaceMode   surface_mode;
} PlotParameters;

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
		2, 1,
		{
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
		2, 1,
		{
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

static void
graphic_type_selected (GtkCList *clist, gint row, gint column, GdkEvent *event,
		       WizardGraphicContext *gc)
{
	printf ("Row selected: %d\n", row);
}

void
fill_graphic_types (GladeXML *gui, WizardGraphicContext *gc)
{
	GtkCList *clist = GTK_CLIST (glade_xml_get_widget (gui, "graphic-type-clist"));
	int i;

	gtk_clist_set_column_justification (clist, 1, GTK_JUSTIFY_LEFT);
	gtk_signal_connect (GTK_OBJECT (clist), "select_row",
			    GTK_SIGNAL_FUNC (graphic_type_selected), gc);
	
	for (i = 0; graphic_types [i].text; i++){
		char *clist_text [2];

		clist_text [0] = "";
		clist_text [1] = _(graphic_types [i].text);
		gtk_clist_append (clist, clist_text);
	}
	gtk_clist_select_row (clist, 1, 0);
	gtk_clist_thaw (clist);
}



