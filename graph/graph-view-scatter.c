/*
 * graph-view-scatter.c: Scatter plots.
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org)
 *
 * Copyright 1999, International GNOME Support (http://www.gnome-support.com)
 */
#include <config.h>
#include "Graph.h"
#include "graph.h"
#include "graph-view.h"
#include "graph-view-scatter.h"
#include "graph-view-util.h"
#include "graph-vector.h"

/*
 * Maps the x,y point to a canvas point
 */
static void
plot_point (ViewDrawCtx *ctx, int xi, double x, int series, double y)
{
	Symbol sym;

	sym = symbol_setup (ctx, series);
	symbol_draw (ctx, sym, MAP_X (ctx, x), MAP_Y (ctx, y));
}

/*
 * This assumes the GC has been prepared previously
 */
static void
draw_line (ViewDrawCtx *ctx, double x1, double y1, double x2, double y2)
{
	gdk_draw_line (
		ctx->drawable, ctx->graph_view->fill_gc,
		MAP_X (ctx, x1), MAP_Y (ctx, y1),
		MAP_X (ctx, x2), MAP_Y (ctx, y2));
}

void
graph_view_line_plot (GraphView *graph_view, GdkDrawable *drawable,
		      int x, int y, int width, int height)
{
	GraphVector **vectors = graph_view->graph->layout->vectors;
	GraphVector *x_vector = vectors [0];
	const int vector_count = graph_view->graph->layout->n_series;
	const int x_vals = graph_vector_count (x_vector);
	int vector;
	ViewDrawCtx ctx;

	setup_view_ctx (&ctx, graph_view, drawable, graph_view->fill_gc, x, y, width, height);

	for (vector = 1; vector < vector_count; vector ++){
		double last_x, last_y;
		int xi;
		
		last_y = graph_vector_get_double (vectors [vector], 0);
		last_x = graph_vector_get_double (x_vector, 0);

		for (xi = 1; xi < x_vals; xi++){
			double tx, ty;

			tx = graph_vector_get_double (x_vector, xi);
			ty = graph_vector_get_double (vectors [vector], xi);

			symbol_setup (&ctx, vector);
			draw_line (&ctx, last_x, last_y, tx, ty);

			last_x = tx;
			last_y = ty;
		}
	}
}

void
graph_view_scatter_plot (GraphView *graph_view, GdkDrawable *drawable,
			 int x, int y, int width, int height)
{
	GraphVector **vectors = graph_view->graph->layout->vectors;
	GraphVector *x_vector = vectors [0];
	const int x_vals = graph_vector_count (x_vector);
	const int vector_count = graph_view->graph->layout->n_series;
	int xi;
	ViewDrawCtx ctx;

	setup_view_ctx (&ctx, graph_view, drawable, graph_view->fill_gc, x, y, width, height);
	
	ctx.dim = MIN (ctx.xl, ctx.yl) / 40;

	if (ctx.dim > 8)
		ctx.dim = 8;

	if (ctx.dim < 2)
		ctx.dim = 2;
	
	/*
	 * FIXME:
	 *
	 * We should process this in chunks, as the local vector cache
	 * only holds a limited set of values (ok, 4k-ish)
	 * and we want to avoid trashing by calling the provider
	 * a lot
	 */

	for (xi = 0; xi < x_vals; xi++){
		double const xv = graph_vector_get_double (x_vector, xi);
		int vector;
		
		for (vector = 1; vector < vector_count; vector++){
			double y;

			y = graph_vector_get_double (vectors [vector], xi);

			printf ("%d: %g %g [%g %g]\n", xi, xv, y, ctx.graph->x_size, ctx.graph->y_size);
			plot_point (&ctx, xi, xv, vector, y);
		}
	}

}


