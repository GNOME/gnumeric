#ifndef GRAPHIC_TYPE_H
#define GRAPHIC_TYPE_H

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

void graphic_type_boot         (GladeXML *gui, WizardGraphicContext *gc);
void graphic_type_restore_view (WizardGraphicContext *gc);
void graphic_type_show_preview (WizardGraphicContext *gc);
void graphic_type_show_page    (WizardGraphicContext *gc, int n);
void graphic_type_init_preview (WizardGraphicContext *gc);
void graphic_type_set_chart_mode (GNOME_Graph_Chart chart, PlotParameters *par);

#endif
	
