#ifndef GNUMERIC_VSCROLLBAR_H
#define GNUMERIC_VSCROLLBAR_H

#include <gdk/gdk.h>
#include <gtk/gtkvscrollbar.h>

#define GNUMERIC_VSCROLLBAR_TYPE    (gnumeric_vscrollbar_get_type ())
#define GNUMERIC_VSCROLLBAR(obj)    (GTK_CHECK_CAST((obj), GNUMERIC_VSCROLLBAR_TYPE, GnumericVScrollbar))
#define IS_GNUMERIC_VSCROLLBAR(obj) (GTK_CHECK_TYPE((obj), GNUMERIC_VSCROLLBAR_TYPE))

typedef struct _GnumericVScrollbar       GnumericVScrollbar;
typedef struct _GnumericVScrollbarClass  GnumericVScrollbarClass;

struct _GnumericVScrollbar
{
	GtkVScrollbar  vscrollbar;

	struct {
		gboolean def;
		gboolean now;
	} live;
};

struct _GnumericVScrollbarClass
{
	GtkVScrollbarClass parent_class;

	void (* offset_changed)   (GnumericVScrollbar *vs, int top, int is_hint);
};


GtkType    gnumeric_vscrollbar_get_type (void);
GtkWidget* gnumeric_vscrollbar_new      (GtkAdjustment *adjustment);


#endif /* GNUMERIC_VSCROLLBAR_H */
