#ifndef GRAPH_VIEW_SCATTER_H_
#define GRAPH_VIEW_SCATTER_H_

void graph_view_scatter_plot (GraphView *graph_view, GdkDrawable *drawable,
			      int x, int y, int width, int height);

void graph_view_line_plot    (GraphView *graph_view, GdkDrawable *drawable,
			      int x, int y, int width, int height);

#endif /* GRAPH_VIEW_SCATTER_H_ */

