#ifndef GNM_TABULATE_H_
#define GNM_TABULATE_H_

#include <gnumeric.h>
#include <numbers.h>

#include <glib-object.h>

GType gnm_tabulate_get_type (void);
#define GNM_TABULATE_TYPE (gnm_tabulate_get_type ())
#define GNM_TABULATE(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_TABULATE_TYPE, GnmTabulate))
#define GNM_IS_TABULATE(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_TABULATE_TYPE))

struct GnmTabulate_ {
	GObject parent;
	GnmCell   *target;
	int dims;
	GnmCell  **cells;
	gnm_float *minima;
	gnm_float *maxima;
	gnm_float *steps;
	gboolean with_coordinates;
};

GnmTabulate *gnm_tabulate_new (int dims);

GSList *gnm_tabulate (GnmTabulate *tab, WorkbookControl *wbc);

#endif
