/*
 * Graphics View: Columns and Bars plotting implementation
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org)
 *
 * (C) 1999 International GNOME Support (http://www.gnome-support.com).
 */
#include <config.h>
#include <math.h>
#include <libgnomeui/gnome-canvas.h>
#include "src/portability.h"
#include "graph.h"
#include "graph-view.h"
#include "graph-view-colbar.h"

typedef struct {
	GraphView   *graph_view;
	Graph       *graph;
	GdkDrawable *drawable;
	int          x, y;
	int          width, height;
	int          xl, yl;
	double       units_per_slot;
	int          margin;
	int          inter_item_margin;
	double       scale;
} ColbarDrawCtx;

static double
colbar_map_point (ColbarDrawCtx *ctx, double point)
{
	return (fabs (ctx->graph->low) + point) * ctx->scale;
}

static void
column_draw (ColbarDrawCtx *ctx, int item,
	     double x1, double y1, double x2, double y2)
{
	int height, width, px, py;
	
	/*
	 * Adjust for pixmap offset
	 */
	x1 -= ctx->x;
	x2 -= ctx->x;
	y1 -= ctx->y;
	y2 -= ctx->y;

	printf ("%d: (%g, %g) (%g, %g) ... ", item, x1, y1, x2, y2);
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

	printf ("%d,%d,%d,%d\n", px, py, width, height);
	gdk_draw_rectangle (
		ctx->drawable, ctx->graph_view->fill_gc, TRUE,
		px, py, width, height);
	
	gdk_draw_rectangle (
		ctx->drawable, ctx->graph_view->outline_gc, FALSE,
		px, py, width, height);
}

static void
setup_gc (ColbarDrawCtx *ctx, int series, int item)
{
	gdk_gc_set_foreground (ctx->graph_view->fill_gc,
			       &ctx->graph_view->palette [series % ctx->graph_view->n_palette]);
}
	      
static void
graph_view_colbar_draw_nth_clustered (ColbarDrawCtx *ctx, int item)
{
	const int n_series = ctx->graph->layout->n_series;
	const int item_base = item * ctx->units_per_slot + ctx->margin;
	const int col_width = (ctx->units_per_slot - (ctx->margin * 2)) / n_series;
	int i;

	for (i = 0; i < n_series; i++){
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
graph_view_colbar_draw_nth_stacked (ColbarDrawCtx *ctx, int item)
{
	const int n_series = ctx->graph->layout->n_series;
	int item_base = item * ctx->units_per_slot;
	double last_pos = 0.0;
	double last_neg = 0.0;
	int i;
	
	for (i = 0; i < n_series; i++){
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
graph_view_colbar_draw_nth_stacked_full (ColbarDrawCtx *ctx, int item)
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
	
	for (i = 0; i < n_series; i++){
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
graph_view_colbar_draw_nth (ColbarDrawCtx *ctx, int item)
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
		g_error ("This mode does not support Column/Bar plotting");
	}
}

void
graph_view_colbar_draw (GraphView *graph_view, GdkDrawable *drawable, int x, int y, int width, int height)
{
	ColbarDrawCtx ctx;
	int first, last, i;

	printf ("Divisions: %d\n", graph_view->graph->divisions);
	if (graph_view->graph->divisions == 0)
		return;
	
	ctx.x = x;
	ctx.y = y;
	ctx.width = width;
	ctx.height = height;
	ctx.drawable = drawable;
	ctx.graph_view = graph_view;
	ctx.graph = graph_view->graph;
	ctx.yl = graph_view->bbox.y1 - graph_view->bbox.y0;
	ctx.xl = graph_view->bbox.x1 - graph_view->bbox.x0;

	if (graph_view->graph->direction == GNOME_Graph_DIR_BAR){
		ctx.units_per_slot = ctx.yl / graph_view->graph->divisions;
		first = y / ctx.units_per_slot;
		last = (y + width) / ctx.units_per_slot;
		ctx.scale = ctx.xl / ctx.graph->y_size;
	} else {
		ctx.units_per_slot = ctx.xl / graph_view->graph->divisions;
		first = x / ctx.units_per_slot;
		last = (x + width) / ctx.units_per_slot;
		ctx.scale = ctx.yl / ctx.graph->y_size;
	}
	
	if (ctx.units_per_slot == 0)
		return;

	ctx.margin = ctx.units_per_slot / 20;
	ctx.inter_item_margin = 0;
		
	for (i = first; i <= last; i++)
		graph_view_colbar_draw_nth (&ctx, i);
}

