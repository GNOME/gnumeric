#ifndef GNUMERIC_HSCROLLBAR_H
#define GNUMERIC_HSCROLLBAR_H

#include <gdk/gdk.h>
#include <gtk/gtkhscrollbar.h>

#define GNUMERIC_HSCROLLBAR_TYPE    (gnumeric_hscrollbar_get_type ())
#define GNUMERIC_HSCROLLBAR(obj)    (GTK_CHECK_CAST((obj), GNUMERIC_HSCROLLBAR_TYPE, GnumericHScrollbar))
#define IS_GNUMERIC_HSCROLLBAR(obj) (GTK_CHECK_TYPE((obj), GNUMERIC_HSCROLLBAR_TYPE))

typedef struct _GnumericHScrollbar       GnumericHScrollbar;
typedef struct _GnumericHScrollbarClass  GnumericHScrollbarClass;

struct _GnumericHScrollbar
{
	GtkHScrollbar  hscrollbar;

	struct {
		gboolean def;
		gboolean now;
	} live;
};

struct _GnumericHScrollbarClass
{
	GtkHScrollbarClass parent_class;

	void (* offset_changed)   (GnumericHScrollbar *hs, int left, int is_hint);
};


GtkType    gnumeric_hscrollbar_get_type (void);
GtkWidget* gnumeric_hscrollbar_new      (GtkAdjustment *adjustment);


#endif /* GNUMERIC_HSCROLLBAR_H */
