/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef GNUMERIC_GRAPH_H
#define GNUMERIC_GRAPH_H

#ifdef NEW_GRAPHS

#include "gnumeric.h"
#include <goffice/graph/goffice-graph.h>
#include <glib-object.h>

#define GNM_GO_DATA_SCALAR_TYPE	 (gnm_go_data_scalar_get_type ())
#define GNM_GO_DATA_SCALAR(o)	 (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_GO_DATA_SCALAR_TYPE, GnmGODataScalar))
#define IS_GNM_GO_DATA_SCALAR(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_GO_DATA_SCALAR_TYPE))

typedef struct _GnmGODataScalar GnmGODataScalar;
GType	 gnm_go_data_scalar_get_type  (void);
GOData	*gnm_go_data_scalar_new_expr  (Sheet *sheet, GnmExpr const *expr);
void	 gnm_go_data_scalar_set_sheet (GnmGODataScalar *scalar, Sheet *sheet);

#define GNM_GO_DATA_VECTOR_TYPE	 (gnm_go_data_vector_get_type ())
#define GNM_GO_DATA_VECTOR(o)	 (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_GO_DATA_VECTOR_TYPE, GnmGODataVector))
#define IS_GNM_GO_DATA_VECTOR(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_GO_DATA_VECTOR_TYPE))

typedef struct _GnmGODataVector GnmGODataVector;
GType	 gnm_go_data_vector_get_type  (void);
GOData	*gnm_go_data_vector_new_expr  (Sheet *sheet, GnmExpr const *expr);
void	 gnm_go_data_vector_set_sheet (GnmGODataVector *vec, Sheet *sheet);

#endif /* NEW_GRAPHS */
#endif /* GNUMERIC_GRAPH_H */
