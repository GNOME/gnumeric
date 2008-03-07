#ifndef GNM_CELL_COMBO_FOO_VIEW_IMPL_H
#define GNM_CELL_COMBO_FOO_VIEW_IMPL_H

#include "gui-gnumeric.h"
#include <gtk/gtkwidget.h>
#include <gtk/gtktreeview.h>
#include <gtk/gtkliststore.h>

typedef struct _GnmCComboFooView GnmCComboFooView;
typedef struct {
	GTypeInterface base;

	void		(*activate)	(SheetObject *so, GtkWidget *popup,   GtkTreeView *list,
					 WBCGtk *wbcg);
	GtkListStore *	(*fill_model)   (SheetObject *so, GtkTreePath **clip, GtkTreePath **select);
	GtkWidget *	(*create_arrow) (SheetObject *so);
} GnmCComboFooViewIface;

#define GNM_CCOMBO_FOO_VIEW_TYPE	 (gnm_ccombo_foo_view_get_type ())
#define GNM_CCOMBO_FOO_VIEW(o)		 (G_TYPE_CHECK_INSTANCE_CAST((o), GNM_CCOMBO_FOO_VIEW_TYPE, GnmCComboFooView))
#define IS_GNM_CCOMBO_FOO_VIEW(o)	 (G_TYPE_CHECK_INSTANCE_TYPE((o), GNM_CCOMBO_FOO_VIEW_TYPE))
#define GNM_CCOMBO_FOO_VIEW_CLASS(k)	 (G_TYPE_CHECK_CLASS_CAST((k), GNM_CCOMBO_FOO_VIEW_TYPE, GnmCComboFooViewIface))
#define IS_GNM_CCOMBO_FOO_VIEW_CLASS(k)	 (G_TYPE_CHECK_CLASS_TYPE((k), GNM_CCOMBO_FOO_VIEW_TYPE))
#define GNM_CCOMBO_FOO_VIEW_GET_CLASS(o) (G_TYPE_INSTANCE_GET_INTERFACE ((o), GNM_CCOMBO_FOO_VIEW_TYPE, GnmCComboFooViewIface))

GType gnm_ccombo_foo_view_get_type (void);

#endif /* GNM_CELL_COMBO_FOO_VIEW_IMPL_H */
