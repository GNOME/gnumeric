#ifndef GNUMERIC_TOOLBAR_H
#define GNUMERIC_TOOLBAR_H

#include <gui-gnumeric.h>

#define GNUMERIC_TOOLBAR_TYPE    (gnumeric_toolbar_get_type ())
#define GNUMERIC_TOOLBAR(obj)    (GTK_CHECK_CAST((obj), GNUMERIC_TOOLBAR_TYPE, GnumericToolbar))
#define IS_GNUMERIC_TOOLBAR(obj) (GTK_CHECK_TYPE((obj), GNUMERIC_TOOLBAR_TYPE))

typedef struct _GnumericToolbar GnumericToolbar;
typedef struct _GnumericToolbarPrivate GnumericToolbarPrivate;

struct _GnumericToolbar {
	GtkToolbar   toolbar;
};

typedef struct {
	GtkToolbarClass toolbar_class;
} GnumericToolbarClass;

GtkType    gnumeric_toolbar_get_type   		(void);
GtkWidget *gnumeric_toolbar_new        		(GnomeUIInfo *info,
						 GtkAccelGroup *accel_group,
				       		 void  *data);
GtkWidget *gnumeric_toolbar_get_widget          (GnumericToolbar *toolbar,
						 int pos);

#endif

