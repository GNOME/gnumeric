#ifndef GNM_CELL_COMBO_VIEW_IMPL_H
#define GNM_CELL_COMBO_VIEW_IMPL_H

#include <gnumeric-fwd.h>
#include <sheet-object-impl.h>

typedef SheetObjectView GnmCComboView;
typedef struct {
	SheetObjectViewClass base;

	gboolean	(*activate)	(SheetObject *so, GtkTreeView *list,
					 WBCGtk *wbcg, gboolean button);
	GtkWidget *	(*create_list)  (SheetObject *so, GtkTreePath **clip, GtkTreePath **select,
					 gboolean *add_buttons);
	GtkWidget *	(*create_arrow) (SheetObject *so);
} GnmCComboViewClass;

#define GNM_CCOMBO_VIEW_TYPE	 (gnm_ccombo_view_get_type ())
#define GNM_CCOMBO_VIEW(o)		 (G_TYPE_CHECK_INSTANCE_CAST((o), GNM_CCOMBO_VIEW_TYPE, GnmCComboView))
#define GNM_IS_CCOMBO_VIEW(o)	 (G_TYPE_CHECK_INSTANCE_TYPE((o), GNM_CCOMBO_VIEW_TYPE))
#define GNM_CCOMBO_VIEW_CLASS(k)	 (G_TYPE_CHECK_CLASS_CAST((k), GNM_CCOMBO_CComboViewClass))
#define GNM_CCOMBO_VIEW_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GNM_CCOMBO_VIEW_TYPE, GnmCComboViewClass))

GType gnm_ccombo_view_get_type (void);

#endif /* GNM_CELL_COMBO_VIEW_IMPL_H */

