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

typedef struct {
	GraphView   *graph_view;
	Graph       *graph;
	GdkDrawable *drawable;
	int          x, y;
	int          width, height;
	int          xl, yl;

	/* Dimensions of our symbols, a rough idea of the size */
	int          dim;
} ScatterDrawCtx;

typedef enum {
	SYMBOL_POINT,
	SYMBOL_CROSS_1,
	SYMBOL_CROSS_2,
	SYMBOL_CIRCLE,
	SYMBOL_FILLED_CIRCLE,
	SYMBOL_SQUARE,
	SYMBOL_FILLED_SQUARE,
	SYMBOL_TRIANGLE,
	SYMBOL_FILLED_TRIANGLE,
	SYMBOL_LAST
} Symbols;

static Symbol
setup_symbol (ScatterDrawCtx *ctx, int series)
{
	/*
	 * FIXME: This can be much improved
	 */
	gdk_gc_set_foreground (ctx->graph_view->fill_gc,
			       &ctx->graph_view->palette [series % ctx->graph_view->n_palette]);
	
	return (Symbol) series % (SYMBOL_LAST-1);
}

static
put_symbol (ScatterDrawCtx *ctx, Symbol sym, int px, int py)
{
	gboolean fill = FALSE;
	const int dim = ctx->dim;
	const int dim_h = dim / 2;
	
	px -= ctx->x;
	py -= ctx->y;

	switch (sym){
	case SYMBOL_FILLED_SQUARE:
	case SYMBOL_FILLED_TRIANGLE:
	case SYMBOL_FILLED_CIRCLE:		
		fill = TRUE;
	default:
	}
	
	switch (sym){
	case SYMBOL_POINT:
		gdk_draw_point (ctx->drawable, ctx->gc, px, py);
		break;

	case SYMBOL_CROSS_1:
		gdk_draw_line (
			ctx->drawable, ctx->gc,
			px - dim_h, py - dim_h,
			px + dim_h, py + dim_h);
		
		gdk_draw_line (
			ctx->drawable, ctx->gc,
			px + dim_h, py - dim_h,
			px - dim_h, py + dim_h);
		break;

	case SYMBOL_CROSS_2:
		gdk_draw_line (
			ctx->drawable, ctx->gc,
			px, py - dim_h,
			px, py + dim_h);
		
		gdk_draw_line (
			ctx->drawable, ctx->gc,
			px + dim_h, py,
			px - dim_h, py);
		break;

	case SYMBOL_CROSS_CIRCLE:
	case SYMBOL_CROSS_FILLED_CIRCLE:
		gdk_draw_ellipse (
			ctx->drawable, ctx->gc, fill,
			px - dim_h, py - dim_h,
			dim, dim, 0, 360 * 64);
		break;
		
	case SYMBOL_SQUARE:
	case SYMBOL_FILLED_SQUARE:
		gdk_draw_rectangle (
			ctx->drawable, ctx->gc, fill,
			px - dim_h, py - dim_h, dim, dim);
		break;
		
	case SYMBOL_TRIANGLE:
	case SYMBOL_FILLED_TRIANGLE: {
		GdkPoint *tpoints = g_new (GdkPoint, 4);

		tpoints [0].x = px - dim/2;
		tpoints [0].y = py + dim/2;
		
		tpoints [1].x = px;
		tpoints [1].y = py - dim/2;

		tpoints [2].x = px + dim/2;
		tpoints [2].y = tpoints [0].y;

		tpoints [3] = tpoints [0];
		
		gdk_draw_polygon (ctx->drawable, ctx->gc, fill, t_points, 4);
		break;
	}

	default:
		g_assert_not_reached ();
	}
}

/*
 * Maps the x,y point to a canvas point
 */
static void
plot_point (ScatterDrawCtx *ctx, int xi, double x, int series, double y)
{
	
}

void
graph_view_scatter_plot (GraphView *graph_view, GdkDrawable *drawable,
			 int x, int y, int width, int height)
{
	ScatterDrawCtx ctx;
	GraphVector **vectors = graph->layout->vectors;
	GraphVector *x_vector = vectors [0];
	int x_vals = graph_vector_count (x_vector);
	int vector_count = graph_view->graph->layout->n_series;
	int xi;
	
	ctx.x = x;
	ctx.y = y;
	ctx.width = width;
	ctx.height = height;
v	ctx.drawable = drawable;
	ctx.graph_view = graph_view;
	ctx.graph = graph_view->graph;
	ctx.yl = graph_view->bbox.y1 - graph_view->bbox.y0;
	ctx.xl = graph_view->bbox.x1 - graph_view->bbox.x0;

	ctx.dim = MIN (ctx.xl, ctx.yl) / 50;
	
	for (xi = 0; xi < x_vals; xi++){
		int vector;

		xv = graph_vector_get_double (x_vector, i);
		
		for (vector = 1; vector < vector_count; vector++){
			double y;

			y = graph_vector_get_double (vectors [vector], xi);
			plot_point (ctx, xi, xv, vector, y);
		}
	}
}


