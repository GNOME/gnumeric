/*
 * Graph-view-util.h: utilities used to render in a graph view
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org)
 *
 * Copyright 1999, International GNOME Support (http://www.gnome-support.com)
 */
#include <config.h>
#include "src/portability.h"
#include "graph.h"
#include "graph-view.h"
#include "graph-view-util.h"

Symbol
symbol_setup (ViewDrawCtx *ctx, int series)
{
	/*
	 * FIXME: This can be much improved
	 */
	gdk_gc_set_foreground (
		ctx->graph_view->fill_gc,
		&ctx->graph_view->palette [series % ctx->graph_view->n_palette]);
	
	return (Symbol) series % (SYMBOL_LAST-1);
}

void
symbol_draw (ViewDrawCtx *ctx, Symbol sym, int px, int py)
{
	gboolean fill = FALSE;
	const int dim = ctx->dim;
	const int dim_h = dim / 2;
	
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

 	case SYMBOL_FILLED_CIRCLE:
	case SYMBOL_CIRCLE:
		gdk_draw_arc (
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
		
		gdk_draw_polygon (ctx->drawable, ctx->gc, fill, tpoints, 4);
		break;
	}

	default:
		g_assert_not_reached ();
	}
}

void
setup_view_ctx (ViewDrawCtx *ctx, GraphView *gv, GdkDrawable *d, GdkGC *gc,
		int x, int y, int width, int height)
{
	ctx->x = x;
	ctx->y = y;
	ctx->width = width;
	ctx->height = height;
	ctx->drawable = d;
  	ctx->gc = gc;
	ctx->graph_view = gv;
	ctx->graph = gv->graph;
	ctx->yl = gv->bbox.y1 - gv->bbox.y0;
	ctx->xl = gv->bbox.x1 - gv->bbox.x0;
}

	
