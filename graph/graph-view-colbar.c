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
	double       size;	/* |low| + |high| */
} ColbarDrawCtx;

static double
colbar_map_point (ColbarDrawCtx *ctx, double point)
{
	return (fabs (ctx->graph->low) + point) / ctx->scale;
}


FIXME

static void
graph_view_colbar_draw_nth_clustered (ColbarDrawCtx *ctx, int item)
{
	const int n_series = ctx->graph->n_series;
	int item_base = item * ctx->units_per_slot;
	int col_width = ctx->units_per_slot / n_series;
	int i;
		
	for (i = 0; i < n_series; i++){
		GraphVector *vector = ctx->graph->vectors [i];
		
		column_draw (
			ctx, 0,
			item_base, 0,
			item_base + col_width - 1,
			graph_vector_get_double (vector, item));
	}
}

static void
graph_view_colbar_draw_nth_stacked (ColbarDrawCtx *ctx, int item)
{
	double last_pos = 0.0;
	double last_neg = 0.0;
	int i;
	
	for (i = 0; i < n_series; i++){
		GraphVector *vector = ctx->graph->vectors [i];
		double v;
		
		v = graph_vector_get_double (vector, item);
		if (v >= 0){
			column_draw (
				ctx, i,
				item_base, last_pos,
				item_base + ctx->units_per_slot - 1,
				last_pos + v);
			last_pos += v;
		} else {
			column_draw (
				ctx, i,
				item_base, last_neg,
				item_base + ctx->units_per_slot - 1,
				last_neg + v);
			last_neg += v;
		}
	}
}

static void
graph_view_colbar_draw_nth_stacked_full (ColbarDrawCtx *ctx, int item)
{
	double last_pos, last_neg;
	double total_pos, total_neg;
	double *values;
	int i;
	
	values = g_alloca0 (sizeof (double) * n_series);
	total_pos = total_neg = 0.0;
	last_pos = last_neg = 0.0;
	
	for (i = 0; i < n_series; i++){
		GraphVector *vector = ctx->graph->vectors [i];
		
		values [i] = graph_vector_get_double (vector, item);
		if (values [i] >= 0)
			total_pos += values [i];
		else
			total_neg += values [i];
	}

	for (i = 0; i < n_series; i++){
		double v;

		if (values [i] >= 0){
			if (total_pos == 0.0)
				v = 0.0;
			else
				v = (values [i] * graph->high) / total_pos;
			
			column_draw (
				ctx, i,
				item_base, last_pos,
				item_base + ctx->units_per_slot - 1,
				last_pos + v);
			last_pos += v;
		} else {
			if (total_neg == 0.0)
				v = 0.0;
			else
				v = (values [i] * graph->low) / total_neg;
			
			column_draw (
				ctx, i,
				item_base, last_neg,
				item_base + ctx->units_per_slot - 1,
				last_neg + v);
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

	ctx.x = x;
	ctx.y = y;
	ctx.width = width;
	ctx.height = height;
	ctx.drawable = drawable;
	ctx.graph_view = graph_view;
	ctx.graph = graph_view->graph;
	ctx.yl = graph_view->bbox.y1 - graph_view->bbox.y0;
	ctx.xl = graph_view->bbox.x1 - graph_view->bbox.x0;
	ctx.size = fabs (ctx.graph->low) + fabs (ctx.graph->high);
	
	if (graph_view->graph->direction == GNOME_GRAPH_DIR_BAR){
		ctx.units_per_slot = ctx.yl / graph_view->graph->divisions;
		first = y / units_per_slot;
		last = (y + width) / units_per_slot;
		ctx.scale = xl / ctx->size;
	} else {
		ctx.units_per_slot = xl / graph_view->graph->divisions;
		first = x / units_per_slot;
		last = (x + width) / units_per_slot;
		ctx.scale = yl / ctx->size;
	}

	for (i = first; i <= last; i++){
		int base_x = i * units_per_slot;
		
		graph_view_colbar_draw_nth (&ctx, i);
	};
}

