#ifndef GNUMERIC_IO_CONTEXT_PRIV_H
#define GNUMERIC_IO_CONTEXT_PRIV_H

#include "gnumeric.h"
#include "io-context.h"
#include "error-info.h"
#include "command-context-priv.h"
#include <stdio.h>

#define IO_CONTEXT_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), TYPE_IO_CONTEXT, IOContext))
#define IS_IO_CONTEXT_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), TYPE_IO_CONTEXT))

typedef enum {
	GNUM_PROGRESS_HELPER_NONE,
	GNUM_PROGRESS_HELPER_FILE,
	GNUM_PROGRESS_HELPER_MEM,
	GNUM_PROGRESS_HELPER_COUNT,
	GNUM_PROGRESS_HELPER_VALUE,
	GNUM_PROGRESS_HELPER_WORKBOOK,
	GNUM_PROGRESS_HELPER_LAST
} GnumProgressHelperType;

typedef struct {
	GnumProgressHelperType helper_type;
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
			gint total, last, current;
			gint step;
		} count;
		struct {
			gint total, last;
			gint step;
		} value;
		struct {
			gint n_elements, last, current;
			gint step;
		} workbook;
	} v;
} GnumProgressHelper;

typedef struct {
	gfloat min, max;
} ProgressRange;

struct _IOContext {
	CommandContext parent;

	CommandContext *impl;
	ErrorInfo *info;
	gboolean error_occurred;
	gboolean warning_occurred;

	GList *progress_ranges;
	gfloat progress_min, progress_max;
	gdouble last_progress;
	gdouble last_time;
	GnumProgressHelper helper;
};

struct _IOContextClass {
	CommandContextClass parent_class;
};

#endif /* GNUMERIC_IO_CONTEXT_PRIV_H */
