#ifndef GNUMERIC_GRAPH_H
#define GNUMERIC_GRAPH_H

#include "gnumeric.h"
#include <gtk/gtkwidget.h>
#include <libxml/tree.h>
/* Do not include idl here due to automake irritaion for the non-bonobo case */

typedef enum {
	GNM_VECTOR_SCALAR  = 0,	/* See idl for details */
	GNM_VECTOR_DATE    = 1,
	GNM_VECTOR_STRING  = 2,
	GNM_VECTOR_AUTO	   = 99
} GnmGraphVectorType;

#define GNUMERIC_GRAPH_TYPE	(gnm_graph_get_type ())
#define GNUMERIC_GRAPH(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), GNUMERIC_GRAPH_TYPE, GnmGraph))
#define IS_GNUMERIC_GRAPH(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GNUMERIC_GRAPH_TYPE))

GType gnm_graph_get_type (void);

GObject   *gnm_graph_new		  (void);
void	   gnm_graph_clear_vectors	  (GnmGraph *g);
void	   gnm_graph_arrange_vectors	  (GnmGraph *g);
void	   gnm_graph_range_to_vectors	  (GnmGraph *g, Sheet *sheet,
					   Range const *src, gboolean as_cols);
xmlDoc	  *gnm_graph_get_spec		  (GnmGraph *g, gboolean force_update);
void	   gnm_graph_import_specification (GnmGraph *graph, xmlDoc *spec);
int	   gnm_graph_add_vector	   	  (GnmGraph *graph, GnmExpr const *expr,
					   GnmGraphVectorType type, Sheet *s);

GnmGraphVector *gnm_graph_get_vector	  (GnmGraph *graph, int id);

void	      gnm_graph_remove_plot   (GnmGraph *graph, GnmGraphPlot *plot);
GnmGraphPlot *gnm_graph_add_plot      (GnmGraph *graph);

void	      gnm_graph_plot_set_type (GnmGraphPlot *plot, xmlNode *type);
void	      gnm_graph_plot_remove_series (GnmGraphPlot *plot, GnmGraphSeries *series);
GnmGraphPlot *gnm_graph_plot_add_series    (GnmGraphPlot *plot);
void	      gnm_graph_plot_set_type (GnmGraphPlot *plot, xmlNode *type);
extern char const * const gnm_graph_vector_type_name [];

#define GNUMERIC_GRAPH_VECTOR_TYPE	(gnm_graph_vector_get_type ())
#define GNUMERIC_GRAPH_VECTOR(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), GNUMERIC_GRAPH_VECTOR_TYPE, GnmGraphVector))
#define IS_GNUMERIC_GRAPH_VECTOR(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GNUMERIC_GRAPH_VECTOR_TYPE))

GType	gnm_graph_vector_get_type    (void);
Dependent const *gnm_graph_vector_get_dependent (GnmGraphVector const *v);

/* Series utilities */
xmlNode *gnm_graph_series_get_dimension (xmlNode *series, xmlChar const *element);
xmlNode *gnm_graph_series_add_dimension (xmlNode *series, char const *element);

#endif /* GNUMERIC_GRAPH_H */
