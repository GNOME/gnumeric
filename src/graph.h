#ifndef _GNM_GRAPH_H_
# define _GNM_GRAPH_H_

#include <gnumeric.h>
#include <sheet-object.h>
#include <goffice/goffice.h>
#include <glib-object.h>

G_BEGIN_DECLS

void	 gnm_go_data_set_sheet (GOData *dat, Sheet *sheet);
Sheet   *gnm_go_data_get_sheet (GOData const *dat);
GnmExprTop const *gnm_go_data_get_expr (GOData const *dat);
void	 gnm_go_data_foreach_dep (GOData *dat, SheetObject *so,
				  SheetObjectForeachDepFunc func, gpointer user);

#define GNM_GO_DATA_SCALAR_TYPE	 (gnm_go_data_scalar_get_type ())
#define GNM_GO_DATA_SCALAR(o)	 (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_GO_DATA_SCALAR_TYPE, GnmGODataScalar))
#define GNM_IS_GO_DATA_SCALAR(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_GO_DATA_SCALAR_TYPE))

typedef struct _GnmGODataScalar GnmGODataScalar;
GType	 gnm_go_data_scalar_get_type  (void);
GOData	*gnm_go_data_scalar_new_expr  (Sheet *sheet, GnmExprTop const *texpr);

#define GNM_GO_DATA_VECTOR_TYPE	 (gnm_go_data_vector_get_type ())
#define GNM_GO_DATA_VECTOR(o)	 (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_GO_DATA_VECTOR_TYPE, GnmGODataVector))
#define GNM_IS_GO_DATA_VECTOR(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_GO_DATA_VECTOR_TYPE))

typedef struct _GnmGODataVector GnmGODataVector;
GType	 gnm_go_data_vector_get_type  (void);
GOData	*gnm_go_data_vector_new_expr  (Sheet *sheet, GnmExprTop const *texpr);

#define GNM_GO_DATA_MATRIX_TYPE	 (gnm_go_data_matrix_get_type ())
#define GNM_GO_DATA_MATRIX(o)	 (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_GO_DATA_MATRIX_TYPE, GnmGODataMatrix))
#define GNM_IS_GO_DATA_MATRIX(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_GO_DATA_MATRIX_TYPE))

typedef struct _GnmGODataMatrix GnmGODataMatrix;
GType	 gnm_go_data_matrix_get_type  (void);
GOData	*gnm_go_data_matrix_new_expr  (Sheet *sheet, GnmExprTop const *texpr);

/* closure for data allocation */
typedef struct {
	int colrowmode; /* 0 = auto; 1 = columns; 2 = rows */
	gboolean share_x, new_sheet;
	GObject *obj;
	GogDataAllocator *dalloc;
	GnmSOAnchorMode anchor_mode;
} GnmGraphDataClosure;

G_END_DECLS

#endif /* _GNM_GRAPH_H_ */
