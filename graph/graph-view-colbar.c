/*
 * Graphics View: Columns and Bars plotting implementation
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org)
 *
 * (C) 1999, 2000 Helix Code, Inc (http://www.helixcode.com)
 */
#include <config.h>
#include <math.h>
#include <libgnomeui/gnome-canvas.h>
#include "src/portability.h"
#include "graph.h"
#include "graph-view.h"
#include "graph-view-colbar.h"
#include "graph-view-util.h"

static double
colbar_map_point (ViewDrawCtx *ctx, double point)
{
	return (fabs (ctx->graph->low) + point) * ctx->scale;
}

static void
column_draw (ViewDrawCtx *ctx, int item,
	     double x1, double y1, double x2, double y2)
{
	int height, width, px, py;
	
	if (ctx->graph->direction == GNOME_Graph_DIR_BAR){
		py = MIN (x1, x2);
		height = fabs (x2-x1);
		px = MIN (y1, y2);
		width = fabs (y2 - y1);
	} else {
		/* Map y */
		y1 = ctx->yl - y1;
		y2 = ctx->yl - y2;

		/* Massage arguments for Gdk consumption */
		py = MIN (y1, y2);
		height = fabs (y2 - y1);
		px = x1;
		width = x2 - x1;
	}

	px -= ctx->x;
	py -= ctx->y;
	
	gdk_draw_rectangle (
		ctx->drawable, ctx->graph_view->fill_gc, TRUE,
		px + ctx->graph_view->bbox.x0, py + ctx->graph_view->bbox.y0,
		width, height);
	
	gdk_draw_rectangle (
		ctx->drawable, ctx->graph_view->outline_gc, FALSE,
		px + ctx->graph_view->bbox.x0, py + ctx->graph_view->bbox.y0,
		width, height);
}

static void
setup_gc (ViewDrawCtx *ctx, int series, int item)
{
	gdk_gc_set_foreground (ctx->graph_view->fill_gc,
			       &ctx->graph_view->palette [series % ctx->graph_view->n_palette]);
}
	      
static void
graph_view_colbar_draw_nth_clustered (ViewDrawCtx *ctx, int item)
{
	const int n_series = ctx->graph->layout->n_series;
	const int item_base = item * ctx->units_per_slot + ctx->margin;
	const int col_width = (ctx->units_per_slot - (ctx->margin * 2)) / n_series;
	int i;

	for (i = ctx->graph->first; i < n_series; i++){
		GraphVector *vector = ctx->graph->layout->vectors [i];

		setup_gc (ctx, i, item);

		column_draw (
			ctx, item,
			item_base + i * col_width,
			colbar_map_point (ctx, 0.0),
			item_base + (i + 1) * col_width - ctx->inter_item_margin, 
			colbar_map_point (
				ctx,
				graph_vector_get_double (vector, item)));
	}
}

static void
graph_view_colbar_draw_nth_stacked (ViewDrawCtx *ctx, int item)
{
	const int n_series = ctx->graph->layout->n_series;
	int item_base = item * ctx->units_per_slot;
	double last_pos = 0.0;
	double last_neg = 0.0;
	int i;
	
	for (i = ctx->graph->first; i < n_series; i++){
		GraphVector *vector = ctx->graph->layout->vectors [i];
		double v;
		
		v = graph_vector_get_double (vector, item);
		setup_gc (ctx, i, item);
		
		if (v >= 0){
			column_draw (
				ctx, i,
				item_base + ctx->margin,
				colbar_map_point (ctx, last_pos),
				item_base + (ctx->units_per_slot - ctx->margin),
				colbar_map_point (ctx, last_pos + v));
			last_pos += v;
		} else {
			column_draw (
				ctx, i,
				item_base + ctx->margin,
				colbar_map_point (ctx, last_neg),
				item_base + (ctx->units_per_slot - ctx->margin),
				colbar_map_point (ctx, last_neg + v));
			last_neg += v;
		}
	}
}

static void
graph_view_colbar_draw_nth_stacked_full (ViewDrawCtx *ctx, int item)
{
	const int n_series = ctx->graph->layout->n_series;
	int item_base = item * ctx->units_per_slot;
	double last_pos, last_neg;
	double total_pos, total_neg;
	double *values;
	int i;
	
	values = g_alloca0 (sizeof (double) * n_series);
	total_pos = total_neg = 0.0;
	last_pos = last_neg = 0.0;
	
	for (i = ctx->graph->first; i < n_series; i++){
		GraphVector *vector = ctx->graph->layout->vectors [i];
		
		values [i] = graph_vector_get_double (vector, item);
		if (values [i] >= 0)
			total_pos += values [i];
		else
			total_neg += values [i];
	}

	for (i = 0; i < n_series; i++){
		setup_gc (ctx, i, item);

		if (values [i] >= 0){
			double v;
			
			if (total_pos == 0.0)
				v = 0.0;
			else
				v = (values [i] * ctx->graph->high) / total_pos;
			
			column_draw (
				ctx, i,
				item_base,
				colbar_map_point (ctx, last_pos),
				item_base + (ctx->units_per_slot - ctx->margin),
				colbar_map_point (ctx, last_pos + v));
			last_pos += v;
		} else {
			double v;
			
			if (total_neg == 0.0)
				v = 0.0;
			else
				v = (values [i] * ctx->graph->low) / total_neg;
			
			column_draw (
				ctx, i,
				item_base,
				colbar_map_point (ctx, last_neg),
				item_base + (ctx->units_per_slot - ctx->margin),
				colbar_map_point (ctx, last_neg + v));
			last_neg += v;
		}			
	}
}

static void
graph_view_colbar_draw_nth (ViewDrawCtx *ctx, int item)
{
	switch (ctx->graph->chart_type){
	case GNOME_Graph_CHART_TYPE_CLUSTERED:
		graph_view_colbar_draw_nth_clustered (ctx, item);
		break;

	case GNOME_Graph_CHART_TYPE_STACKED:
		graph_view_colbar_draw_nth_stacked (ctx, item);
		break;

	case GNOME_Graph_CHART_TYPE_STACKED_FULL:
		graph_view_colbar_draw_nth_stacked_full (ctx, item);
		break;

	default:
		g_error ("Unsupported chart_type for this mode");
	}
}

#define LAD_IS_LAST   1
#define LAD_DRAW_AREA 2

/*
 * Draws a line or an area (depending on the draw_flags) and uses
 * Sym for the end points.
 *
 * The x1, and x2 coordinates are not mapped, they are just adjusted to
 * be withing the drawing region bounding box, and canvas-paint-adjusted.
 *
 * The "y" coordinates are all passed trough the MAP_Y macros for mapping
 * the values to their correct location.
 * 
 * This is used for the lines/areas plotting modes.
 */
static void
la_draw (ViewDrawCtx *ctx, int item, int draw_flags, Symbol sym,
	 double x1,  double x2,
	 double ya1, double yb1,
	 double ya2, double yb2)
{
	ArtIRect *bbox = &ctx->graph_view->bbox;
	double plot_x1, plot_x2;
	double plot_ya1, plot_ya2;
	double plot_yb1, plot_yb2;

	plot_x1  = x1 + bbox->x0 - ctx->x;
	plot_x2  = x2 + bbox->x0 - ctx->x;
	plot_ya1 = MAP_Y (ctx, ya1);
	plot_ya2 = MAP_Y (ctx, ya2);
	plot_yb1 = MAP_Y (ctx, yb1);
	plot_yb2 = MAP_Y (ctx, yb2);

	if (draw_flags & LAD_DRAW_AREA){
		GdkPoint rect_points [5];

		rect_points [0].x = plot_x1;
		rect_points [0].y = plot_ya1;
		rect_points [1].x = plot_x1;
		rect_points [1].y = plot_yb1;
		rect_points [2].x = plot_x2;
		rect_points [2].y = plot_yb2;
		rect_points [3].x = plot_x2;
		rect_points [3].y = plot_ya2;
		rect_points [4] = rect_points [0];
		       
		gdk_draw_polygon (
			ctx->drawable, ctx->graph_view->fill_gc,
			TRUE, rect_points, 4);
	} 

	gdk_draw_line (ctx->drawable, ctx->graph_view->outline_gc,
		       plot_x1, plot_yb1, plot_x2, plot_yb2);

	/*
	 * We only draw the end point symbols for lines, not for
	 * areas
	 */
	if (draw_flags & LAD_DRAW_AREA)
		return;
	
	symbol_draw (ctx, sym, plot_x1, plot_yb1);

	if (draw_flags & LAD_IS_LAST)
		symbol_draw (ctx, sym, plot_x2, plot_yb2);
}

/*
 * Line/Area draw in "clustered" mode (This is actually not the correct name
 * but it belong in the same category that would display "Clustered" in column
 * mode.
 */
static void
graph_view_line_draw_nth_clustered (ViewDrawCtx *ctx, int item, int draw_flags)
{
	const int n_series = ctx->graph->layout->n_series;
	int i;

	for (i = ctx->graph->first; i < n_series; i++){
		GraphVector *vector = ctx->graph->layout->vectors [i];
		Symbol sym;
		
		setup_gc (ctx, i, item);

		sym = symbol_setup (ctx, i);

		la_draw (
			ctx, item, draw_flags, sym,
			item * ctx->units_per_slot,
			(item+1) * ctx->units_per_slot,
			0.0,
			graph_vector_get_double (vector, item),
			0.0,
			graph_vector_get_double (vector, item + 1));
	}
	
}

static void
graph_view_line_draw_nth_stacked (ViewDrawCtx *ctx, int item, int draw_flags)
{
	const int n_series = ctx->graph->layout->n_series;
	const int col_width = (ctx->units_per_slot - (ctx->margin * 2)) / n_series;
	double *series_values = g_new (double, n_series);
	int i;
	double a, b;
	double last_a_pos = 0, last_a_neg = 0;
	double last_b_pos = 0, last_b_neg = 0;
	double a1, b1, a2, b2;

	for (i = ctx->graph->first; i < n_series; i++){
		GraphVector *vector = ctx->graph->layout->vectors [i];
		Symbol sym;
		
		setup_gc (ctx, i, item);

		sym = symbol_setup (ctx, i);
		
		/*
		 * Toda la rutina está mal.  Creo que lo que debe de hacer
		 * es ver plotear con positivos negativos como las barras,
		 * de otra manera se vuelve muy estúpido el modo stacked
		 */
		a = graph_vector_get_double (vector, item);
		b = graph_vector_get_double (vector, item);

		if (a > 0){
			a1 = last_a_pos;
			a2 = last_a_pos + a;
			last_a_pos = a2;
		} else {
			a1 = last_a_neg;
			a2 = last_a_neg + a;
			last_a_neg = a2;
		}

		if (b > 0){
			b1 = last_b_pos;
			b2 = last_b_pos + b;
			last_b_pos = b2;
		} else {
			b1 = last_b_neg;
			b2 = last_b_neg + b;
			last_b_neg = b2;
		}
		la_draw (
			ctx, item, draw_flags, sym,
			item * ctx->units_per_slot,
			(item+1) * ctx->units_per_slot,
			a1, b1, 
			a2, b2);
	}
}

static void
graph_view_line_draw_nth_stacked_full (ViewDrawCtx *ctx, int item, int draw_flags)
{
	const int n_series = ctx->graph->layout->n_series;
	const int col_width = (ctx->units_per_slot - (ctx->margin * 2)) / n_series;
	double *series_values = g_new (double, n_series);
	int i;

	
}

/*
 * graph_view_draw_area:
 *
 * Draws the itemth object in either line or area mode, using the
 * @draw_flags.
 */
static void
graph_view_draw_area (ViewDrawCtx *ctx, int first, int last, int draw_flags)
{
	int item;
	
	/*
	 * Notice that we only go from first..last-1 as the
	 * lines/areas plotting modes use 2 data points to plot.
	 */
	for (item = first; item < last; item++){
		if ((item + 1) == last)
			draw_flags |= LAD_IS_LAST;
		
		switch (ctx->graph->chart_type){
		case GNOME_Graph_CHART_TYPE_CLUSTERED:
			graph_view_line_draw_nth_clustered (ctx, item, draw_flags);
			break;
		
		case GNOME_Graph_CHART_TYPE_STACKED:
			graph_view_line_draw_nth_stacked (ctx, item, draw_flags);
			break;
			
		case GNOME_Graph_CHART_TYPE_STACKED_FULL:
			graph_view_line_draw_nth_stacked_full (ctx, item, draw_flags);
			break;
			
		default:
			g_error ("This mode does not support Column/Bar plotting");
		}
	}
}

void
graph_view_colbar_draw (GraphView *graph_view, GdkDrawable *drawable,
			int x, int y, int width, int height)
{
	ViewDrawCtx ctx;
	int first, last, i, slots;
	gboolean is_bar;

	if (graph_view->graph->divisions == 0)
		return;

	setup_view_ctx (&ctx, graph_view, drawable, graph_view->fill_gc, x, y, width, height);
	
	is_bar = ctx.graph->direction == GNOME_Graph_DIR_BAR;

	if (ctx.graph->plot_mode != GNOME_Graph_PLOT_COLBAR)
		is_bar = FALSE;

	if (ctx.graph->plot_mode == GNOME_Graph_PLOT_LINES ||
	    ctx.graph->plot_mode == GNOME_Graph_PLOT_AREA){
		slots = ctx.graph->divisions - 1;
		if (slots == 0)
			slots = 1;
	} else
		slots = ctx.graph->divisions;
	
	if (is_bar){
		ctx.units_per_slot = ctx.yl / slots;
		/* first = y / ctx.units_per_slot;          */
		/* last = (y + width) / ctx.units_per_slot; */
		ctx.scale = ctx.xl / ctx.graph->y_size;
	} else {
		ctx.units_per_slot = ctx.xl / slots;

		/* first = x / ctx.units_per_slot;          */
		/* last = (x + width) / ctx.units_per_slot; */
		ctx.scale = ctx.yl / ctx.graph->y_size;
	}
	
	if (ctx.units_per_slot == 0)
		return;

	ctx.margin = ctx.units_per_slot / 20;
	ctx.inter_item_margin = 0;

	first = 0;
	last = ctx.graph->divisions - 1;
	
	switch (ctx.graph->plot_mode){
	case GNOME_Graph_PLOT_COLBAR:
		for (i = first; i <= last; i++)
			graph_view_colbar_draw_nth (&ctx, i);
		break;

	case GNOME_Graph_PLOT_AREA:
	case GNOME_Graph_PLOT_LINES: {
		int draw_flags;

		if (ctx.graph->plot_mode == GNOME_Graph_PLOT_AREA)
			draw_flags = LAD_DRAW_AREA;
		else
			draw_flags = 0;

		graph_view_draw_area (&ctx, first, last, draw_flags);
		break;
	} 
	
	} /* switch */
}


