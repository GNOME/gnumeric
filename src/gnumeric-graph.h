#ifndef GNUMERIC_GRAPH_H
#define GNUMERIC_GRAPH_H

#include "gnumeric.h"
#include <gtk/gtkwidget.h>
#include <gnome-xml/tree.h>
#include <idl/GNOME_Gnumeric_Graph.h>

typedef enum {
	GNM_VECTOR_SCALAR  = GNOME_Gnumeric_VECTOR_TYPE_SCALAR,
	GNM_VECTOR_DATE	   = GNOME_Gnumeric_VECTOR_TYPE_DATE,
	GNM_VECTOR_STRING  = GNOME_Gnumeric_VECTOR_TYPE_STRING,
	GNM_VECTOR_AUTO	   = 99
} GnmGraphVectorType;

#define GNUMERIC_GRAPH_TYPE	(gnm_graph_get_type ())
#define GNUMERIC_GRAPH(o)	(GTK_CHECK_CAST ((o), GNUMERIC_GRAPH_TYPE, GnmGraph))
#define IS_GNUMERIC_GRAPH(o)	(GTK_CHECK_TYPE ((o), GNUMERIC_GRAPH_TYPE))

GtkType gnm_graph_get_type (void);

GnmGraph  *gnm_graph_new		  (Workbook *wb);
void	   gnm_graph_clear_vectors	  (GnmGraph *g);
void	   gnm_graph_arrange_vectors	  (GnmGraph *g);
void	   gnm_graph_range_to_vectors	  (GnmGraph *g, Sheet *sheet,
					   Range const *src, gboolean as_cols);
xmlDoc	  *gnm_graph_get_spec		  (GnmGraph *g, gboolean force_update);
void	   gnm_graph_import_specification (GnmGraph *graph, xmlDoc *spec);
int	   gnm_graph_add_vector	   	  (GnmGraph *graph, ExprTree *expr,
					   GnmGraphVectorType type, Sheet *s);
GnmGraphVector *gnm_graph_get_vector	  (GnmGraph *graph, int id);

extern char const * const gnm_graph_vector_type_name [];

#define GNUMERIC_GRAPH_VECTOR_TYPE	(gnm_graph_vector_get_type ())
#define GNUMERIC_GRAPH_VECTOR(o)	(GTK_CHECK_CAST ((o), GNUMERIC_GRAPH_VECTOR_TYPE, GnmGraphVector))
#define IS_GNUMERIC_GRAPH_VECTOR(o)	(GTK_CHECK_TYPE ((o), GNUMERIC_GRAPH_VECTOR_TYPE))

GtkType	gnm_graph_vector_get_type    (void);
Dependent const *gnm_graph_vector_get_dependent (GnmGraphVector const *v);

/* Series utilities */
xmlNode *gnm_graph_series_get_dimension (xmlNode *series, xmlChar const *element);
xmlNode *gnm_graph_series_add_dimension (xmlNode *series, char const *element);

char 	       *gnm_graph_exception	     (CORBA_Environment *ev);
Bonobo_Control  gnm_graph_get_config_control (GnmGraph *g, char const *which);

#endif /* GNUMERIC_GRAPH_H */
