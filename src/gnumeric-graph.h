#ifndef GNUMERIC_GRAPH_H
#define GNUMERIC_GRAPH_H

#include "gnumeric.h"
#include <gtk/gtkwidget.h>
#include <gnome-xml/tree.h>

typedef enum {
	GNM_VECTOR_AUTO	   = 0,
	GNM_VECTOR_SCALAR  = 1,
	GNM_VECTOR_DATE	   = 2,
	GNM_VECTOR_STRING  = 3
} GnmGraphVectorType;

#define GNUMERIC_GRAPH_TYPE	(gnm_graph_get_type ())
#define GNUMERIC_GRAPH(o)	(GTK_CHECK_CAST ((o), GNUMERIC_GRAPH_TYPE, GnmGraph))
#define IS_GNUMERIC_GRAPH(o)	(GTK_CHECK_TYPE ((o), GNUMERIC_GRAPH_TYPE))

GtkType gnm_graph_get_type (void);

GnmGraph	*gnm_graph_new			(Workbook *wb);
GtkWidget	*gnm_graph_type_selector	(GnmGraph *graph);
void		 gnm_graph_clear_vectors	(GnmGraph *graph);
void		 gnm_graph_arrange_vectors	(GnmGraph *graph);
void		 gnm_graph_import_specification	(GnmGraph *graph,
						 xmlDocPtr spec);
GnmGraphVector	*gnm_graph_add_vector	   	(GnmGraph *graph,
						 ExprTree *expr,
						 GnmGraphVectorType type,
						 Sheet *sheet);

extern char const * const gnm_graph_vector_type_name [];

#define GNUMERIC_GRAPH_VECTOR_TYPE	(gnm_graph_vector_get_type ())
#define GNUMERIC_GRAPH_VECTOR(o)	(GTK_CHECK_CAST ((o), GNUMERIC_GRAPH_VECTOR_TYPE, GnmGraphVector))
#define IS_GNUMERIC_GRAPH_VECTOR(o)	(GTK_CHECK_TYPE ((o), GNUMERIC_GRAPH_VECTOR_TYPE))

GtkType	gnm_graph_vector_get_type (void);

#endif /* GNUMERIC_GRAPH_H */
