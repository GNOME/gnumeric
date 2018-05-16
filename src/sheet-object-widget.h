#ifndef _GNM_SHEET_OBJECT_WIDGET_H_
# define _GNM_SHEET_OBJECT_WIDGET_H_

#include <sheet-object.h>
#include <pango/pango-attributes.h>

G_BEGIN_DECLS

/* ------------------------------------------------------------------------ */

void sheet_object_widget_register (void);

GType sheet_object_widget_get_type (void);
#define GNM_SOW_TYPE     (sheet_object_widget_get_type ())
#define GNM_SOW(obj)     (G_TYPE_CHECK_INSTANCE_CAST((obj), GNM_SOW_TYPE, SheetObjectWidget))
#define GNM_IS_SOW(o)    (G_TYPE_CHECK_INSTANCE_TYPE((o), GNM_SOW_TYPE))

GType sheet_widget_adjustment_get_type (void);
#define GNM_SOW_ADJUSTMENT_TYPE   (sheet_widget_adjustment_get_type())
#define GNM_SOW_ADJUSTMENT(obj)	(G_TYPE_CHECK_INSTANCE_CAST ((obj), GNM_SOW_ADJUSTMENT_TYPE, SheetWidgetAdjustment))
#define GNM_IS_SOW_ADJUSTMENT(o) (G_TYPE_CHECK_INSTANCE_TYPE((o), GNM_SOW_ADJUSTMENT_TYPE))

GType sheet_widget_frame_get_type	 (void); /* convert to non-widget */
#define GNM_SOW_FRAME_TYPE (sheet_widget_frame_get_type ())
#define GNM_IS_SOW_FRAME(o) (G_TYPE_CHECK_INSTANCE_TYPE((o), GNM_SOW_FRAME_TYPE))

GType sheet_widget_button_get_type	 (void);
#define GNM_SOW_BUTTON_TYPE (sheet_widget_button_get_type ())
#define GNM_IS_SOW_BUTTON(o) (G_TYPE_CHECK_INSTANCE_TYPE((o), GNM_SOW_BUTTON_TYPE))

GType sheet_widget_checkbox_get_type	 (void);
#define GNM_SOW_CHECKBOX_TYPE (sheet_widget_checkbox_get_type ())
#define GNM_IS_SOW_CHECKBOX(o) (G_TYPE_CHECK_INSTANCE_TYPE((o), GNM_SOW_CHECKBOX_TYPE))

GType sheet_widget_toggle_button_get_type(void);
#define GNM_SOW_TOGGLE_BUTTON_TYPE (sheet_widget_toggle_button_get_type ())
#define GNM_IS_SOW_TOGGLE_BUTTON(o) (G_TYPE_CHECK_INSTANCE_TYPE((o), GNM_SOW_TOGGLE_BUTTON_TYPE))

GType sheet_widget_radio_button_get_type (void);
#define GNM_SOW_RADIO_BUTTON_TYPE (sheet_widget_radio_button_get_type ())
#define GNM_IS_SOW_RADIO_BUTTON(o) (G_TYPE_CHECK_INSTANCE_TYPE((o), GNM_SOW_RADIO_BUTTON_TYPE))

/*Descendents of the list_base sheet widget object*/
GType sheet_widget_list_get_type	 (void);
#define GNM_SOW_LIST_TYPE (sheet_widget_list_get_type ())
#define GNM_IS_SOW_LIST(o) (G_TYPE_CHECK_INSTANCE_TYPE((o), GNM_SOW_LIST_TYPE))

GType sheet_widget_combo_get_type	 (void);
#define GNM_SOW_COMBO_TYPE (sheet_widget_combo_get_type ())
#define GNM_IS_SOW_COMBO(o) (G_TYPE_CHECK_INSTANCE_TYPE((o), GNM_SOW_COMBO_TYPE))

/*Descendents of the adjustment sheet widget object*/
GType sheet_widget_scrollbar_get_type	 (void);
#define GNM_SOW_SCROLLBAR_TYPE (sheet_widget_scrollbar_get_type ())
#define GNM_IS_SOW_SCROLLBAR(o) (G_TYPE_CHECK_INSTANCE_TYPE((o), GNM_SOW_SCROLLBAR_TYPE))

GType sheet_widget_slider_get_type       (void);
#define GNM_SOW_SLIDER_TYPE (sheet_widget_slider_get_type ())
#define GNM_IS_SOW_SLIDER(o) (G_TYPE_CHECK_INSTANCE_TYPE((o), GNM_SOW_SLIDER_TYPE))

GType sheet_widget_spinbutton_get_type   (void);
#define GNM_SOW_SPIN_BUTTON_TYPE (sheet_widget_spinbutton_get_type ())
#define GNM_IS_SOW_SPINBUTTON(o) (G_TYPE_CHECK_INSTANCE_TYPE((o), GNM_SOW_SPIN_BUTTON_TYPE))

/* ------------------------------------------------------------------------ */

void sheet_widget_adjustment_set_details (SheetObject *so,
					  GnmExprTop const *result_link,
					  int value, int min, int max,
					  int inc, int page);
GnmExprTop const *sheet_widget_adjustment_get_link (SheetObject *so);
void sheet_widget_adjustment_set_link	 (SheetObject *so,
					  GnmExprTop const *result_link);
GtkAdjustment *sheet_widget_adjustment_get_adjustment (SheetObject *so);
gboolean sheet_widget_adjustment_get_horizontal (SheetObject *so);
void sheet_widget_adjustment_set_horizontal (SheetObject *so, gboolean horizontal);

void sheet_widget_checkbox_set_link	 (SheetObject *so,
					  GnmExprTop const *result_link);
GnmExprTop const *sheet_widget_checkbox_get_link (SheetObject *so);
void sheet_widget_checkbox_set_label	 (SheetObject *so, char const *str);
GnmExprTop const *sheet_widget_radio_button_get_link (SheetObject *so);
void sheet_widget_radio_button_set_link	 (SheetObject *so,
					  GnmExprTop const *result_link);
void sheet_widget_radio_button_set_label (SheetObject *so, char const *str);
void sheet_widget_radio_button_set_value (SheetObject *so, GnmValue const *val);
GnmValue const *sheet_widget_radio_button_get_value (SheetObject *so);
GnmExprTop const *sheet_widget_button_get_link (SheetObject *so);
void sheet_widget_button_set_link	 (SheetObject *so,
					  GnmExprTop const *result_link);
void sheet_widget_button_set_label	 (SheetObject *so, char const *str);
void sheet_widget_button_set_markup      (SheetObject *so, PangoAttrList *markup);
void sheet_widget_frame_set_label        (SheetObject *so, char const *str);

void  sheet_widget_list_base_set_links	 (SheetObject *so,
					  GnmExprTop const *result_link,
					  GnmExprTop const *content);
void  sheet_widget_list_base_set_result_type (SheetObject *so, gboolean as_index);
GnmExprTop const *sheet_widget_list_base_get_result_link (SheetObject const *so);
gboolean sheet_widget_list_base_result_type_is_index (SheetObject const *so);
GnmExprTop const *sheet_widget_list_base_get_content_link (SheetObject const *so);
GtkAdjustment *sheet_widget_list_base_get_adjustment (SheetObject *so);

G_END_DECLS

#endif /* _GNM_SHEET_OBJECT_WIDGET_H_ */
