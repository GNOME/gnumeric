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

GnmGraph	*gnm_graph_new			(Workbook *wb);
GtkWidget	*gnm_graph_type_selector	(GnmGraph *graph);
void		 gnm_graph_clear_vectors	(GnmGraph *graph);
void		 gnm_graph_arrange_vectors	(GnmGraph *graph);
void		 gnm_graph_range_to_vectors	(GnmGraph *graph,
						 Sheet *sheet,
						 Range const *src,
						 gboolean as_cols);
xmlDoc *	 gnm_graph_get_spec		(GnmGraph *graph);
void		 gnm_graph_import_specification	(GnmGraph *graph,
						 xmlDoc *spec);
int		gnm_graph_add_vector	   	(GnmGraph *graph,
						 ExprTree *expr,
						 GnmGraphVectorType type,
						 Sheet *sheet);

extern char const * const gnm_graph_vector_type_name [];

#define GNUMERIC_GRAPH_VECTOR_TYPE	(gnm_graph_vector_get_type ())
#define GNUMERIC_GRAPH_VECTOR(o)	(GTK_CHECK_CAST ((o), GNUMERIC_GRAPH_VECTOR_TYPE, GnmGraphVector))
#define IS_GNUMERIC_GRAPH_VECTOR(o)	(GTK_CHECK_TYPE ((o), GNUMERIC_GRAPH_VECTOR_TYPE))

GtkType	gnm_graph_vector_get_type (void);

#endif /* GNUMERIC_GRAPH_H */
