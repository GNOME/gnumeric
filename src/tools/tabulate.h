#ifndef GNM_TABULATE_H_
#define GNM_TABULATE_H_

#include <gnumeric.h>
#include <numbers.h>

#include <glib-object.h>

#define GNM_TABULATE_TYPE (gnm_tabulate_get_type ())
G_DECLARE_FINAL_TYPE (GnmTabulate, gnm_tabulate, GNM, TABULATE, GObject)

struct _GnmTabulate {
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
