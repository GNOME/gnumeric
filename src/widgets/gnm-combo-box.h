/*
 * GtkComboBox: A customizable ComboBox.
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org)
 *
 */
#ifndef _GTK_COMBO_BOX_H_
#define _GTK_COMBO_BOX_H_

#include <gtk/gtkhbox.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define GTK_COMBO_BOX(obj)	    GTK_CHECK_CAST (obj, gtk_combo_box_get_type (), GtkComboBox)
#define GTK_COMBO_BOX_CLASS(klass)  GTK_CHECK_CLASS_CAST (klass, gtk_combo_box_get_type (), GtkComboBoxClass)
#define GTK_IS_COMBO_BOX(obj)       GTK_CHECK_TYPE (obj, gtk_combo_box_get_type ())

typedef struct _GtkComboBox	   GtkComboBox;
typedef struct _GtkComboBoxPrivate GtkComboBoxPrivate;
typedef struct _GtkComboBoxClass   GtkComboBoxClass;

struct _GtkComboBox {
	GtkHBox hbox;
	GtkComboBoxPrivate *priv;
};

struct _GtkComboBoxClass {
	GtkHBoxClass parent_class;

	GtkWidget *(*pop_down_widget) (GtkComboBox *cbox);

	/*
	 * invoked when the popup has been hidden, if the signal
	 * returns TRUE, it means it should be killed from the
	 */ 
	gboolean  *(*pop_down_done)   (GtkComboBox *cbox, GtkWidget *);
};

GtkType    gtk_combo_box_get_type  (void);
void       gtk_combo_box_construct (GtkComboBox *combo_box,
				    GtkWidget   *display_widget,
				    GtkWidget   *optional_pop_down_widget);

GtkWidget *gtk_combo_box_new       (GtkWidget *display_widget,
				    GtkWidget *optional_pop_down_widget);
void       gtk_combo_box_popup_hide (GtkComboBox *combo_box);

void       gtk_combo_box_set_display (GtkComboBox *combo_box,
				      GtkWidget *display_widget);

void       gtk_combo_box_set_arrow_relief (GtkComboBox *cc, GtkReliefStyle relief);
void       gtk_combo_box_set_title (GtkComboBox *combo, const gchar *title);

void       gtk_combo_box_set_arrow_sensitive (GtkComboBox *combo,
						  gboolean sensitive);
    
#ifdef __cplusplus
};
#endif /* __cplusplus */

#endif /* _GTK_COMBO_BOX_H_ */
