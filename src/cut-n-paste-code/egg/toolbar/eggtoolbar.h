/* File import from libegg to gnumeric by import-egg.  Do not edit.  */

#ifndef __EGG_TOOLBAR_H__
#define __EGG_TOOLBAR_H__

#include <gtk/gtkcontainer.h>
#include "eggtoolitem.h"

G_BEGIN_DECLS

#define EGG_TYPE_TOOLBAR                  (egg_toolbar_get_type ())
#define EGG_TOOLBAR(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), EGG_TYPE_TOOLBAR, EggToolbar))
#define EGG_TOOLBAR_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), EGG_TYPE_TOOLBAR, EggToolbarClass))
#define EGG_IS_TOOLBAR(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EGG_TYPE_TOOLBAR))
#define EGG_IS_TOOLBAR_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), EGG_TYPE_TOOLBAR))
#define EGG_TOOLBAR_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), EGG_TYPE_TOOLBAR, EggToolbarClass))

typedef struct _EggToolbar           EggToolbar;
typedef struct _EggToolbarClass      EggToolbarClass;

struct _EggToolbar
{
  GtkContainer container;

  gint           num_children;
  GList         *children;
  GtkOrientation orientation;
  GtkToolbarStyle  style;
  /*
  GtkIconSize      icon_size;

  GtkTooltips     *tooltips;
  */
  gint             button_maxw;
  gint             button_maxh;

  /* FIXME: These are private and should be object data eventually */
  gint             total_button_maxw;
  gint             total_button_maxh;
  GList         *items;
  GList *first_non_fitting_item;
  GtkWidget *button, *arrow;
  gboolean show_arrow;
};

struct _EggToolbarClass
{
  GtkContainerClass parent_class;

  void (* orientation_changed) (EggToolbar      *toolbar,
				GtkOrientation   orientation);
  void (* style_changed)       (EggToolbar      *toolbar,
				GtkToolbarStyle  style);
};

GType      egg_toolbar_get_type        (void) G_GNUC_CONST;
GtkWidget* egg_toolbar_new             (void);

/* pos=-1 appends an item */
void egg_toolbar_insert_item (EggToolbar *toolbar, EggToolItem *item, gint pos);

void egg_toolbar_set_orientation (EggToolbar *toolbar, GtkOrientation orientation);
void egg_toolbar_set_style (EggToolbar *toolbar, GtkToolbarStyle style);
void egg_toolbar_set_show_arrow (EggToolbar *toolbar, gboolean show_arrow);

G_END_DECLS

#endif /* __EGG_TOOLBAR_H__ */
