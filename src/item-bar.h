#ifndef _GNM_ITEM_BAR_H_
#define _GNM_ITEM_BAR_H_

#include <gnumeric-fwd.h>
#include <glib-object.h>
#include <pango/pango-font.h>

G_BEGIN_DECLS

#define GNM_ITEM_BAR(obj)      (G_TYPE_CHECK_INSTANCE_CAST((obj), gnm_item_bar_get_type (), GnmItemBar))
#define GNM_IS_ITEM_BAR(o)     (G_TYPE_CHECK_INSTANCE_TYPE((o), gnm_item_bar_get_type ()))

GType    gnm_item_bar_get_type	(void);
int      gnm_item_bar_calc_size	(GnmItemBar *ib);
int      gnm_item_bar_group_size(GnmItemBar const *ib, int max_outline);
int      gnm_item_bar_indent	(GnmItemBar const *ib);
PangoFontDescription *item_bar_normal_font (GnmItemBar const *ib);

G_END_DECLS

#endif /* _GNM_ITEM_BAR_H_ */
