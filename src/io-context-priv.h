#ifndef GNUMERIC_IO_CONTEXT_PRIV_H
#define GNUMERIC_IO_CONTEXT_PRIV_H

#include <gtk/gtkobject.h>
#include "io-context.h"
#include "workbook-control.h"
#include "error-info.h"

#define IO_CONTEXT_CLASS(klass)     (GTK_CHECK_CLASS_CAST ((klass), TYPE_IO_CONTEXT, IOContext))
#define IS_IO_CONTEXT_CLASS(klass)  (GTK_CHECK_CLASS_TYPE ((klass), TYPE_IO_CONTEXT))

typedef enum {
	GNUM_PROGRESS_HELPER_NONE,
	GNUM_PROGRESS_HELPER_FILE,
	GNUM_PROGRESS_HELPER_MEM,
	GNUM_PROGRESS_HELPER_COUNT,
	GNUM_PROGRESS_HELPER_LAST
} GnumProgressHelperType;

typedef struct {
	GnumProgressHelperType helper_type;
	gdouble min_f, max_f;
	union {
		struct {
			FILE *f;
			glong size;
		} file;
		struct {
			gchar *start;
			gint size;
		} mem;
		struct {
			gint total;
		} count;
	} v;
} GnumProgressHelper;

struct _IOContext {
	GtkObject parent;

	WorkbookControl *impl;
	ErrorInfo *error_info;
	gboolean error_occurred;

	gdouble last_progress;
	gdouble last_time;
	GnumProgressHelper helper;
};

struct _IOContextClass {
	GtkObjectClass parent_class;
};

#endif /* GNUMERIC_IO_CONTEXT_PRIV_H */
