/*
 * io-context.c : Place holder for an io error context.
 *   It is intended to become a place to handle errors
 *   as well as storing non-fatal warnings.
 *
 * Authors:
 * 	Jody Goldberg <jgoldberg@home.com>
 *	Zbigniew Chyla <cyba@gnome.pl>
 *
 * (C) 2000, 2001 Jody Goldberg
 */
#include <config.h>
#include "io-context.h"
#include "io-context-priv.h"
#include "sheet.h"
#include "workbook.h"
#include "command-context.h"
#include "gnumeric-util.h"

#include <limits.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>

#define PROGRESS_UPDATE_STEP        0.01
#define PROGRESS_UPDATE_PERIOD_SEC  0.20

static void
io_context_init (IOContext *io_context)
{
	io_context->impl = NULL;
	io_context->error_info = NULL;
	io_context->error_occurred = FALSE;

	io_context->progress_ranges = NULL;
	io_context->progress_min = 0.0;
	io_context->progress_max = 1.0;
	io_context->last_progress = -1.0;
	io_context->last_time = 0.0;
	io_context->helper.helper_type = GNUM_PROGRESS_HELPER_NONE;
}

static void
io_context_destroy (GtkObject *obj)
{
	IOContext *io_context;

	g_return_if_fail (IS_IO_CONTEXT (obj));

	io_context = IO_CONTEXT (obj);
	error_info_free (io_context->error_info);
	gnumeric_progress_set (COMMAND_CONTEXT (io_context->impl), 0.0);
	gnumeric_progress_message_set (COMMAND_CONTEXT (io_context->impl), NULL);

	GTK_OBJECT_CLASS (gtk_type_class (GTK_TYPE_OBJECT))->destroy (obj);
}

static void
io_context_class_init (IOContextClass *klass)
{
	GTK_OBJECT_CLASS (klass)->destroy = io_context_destroy;
}

E_MAKE_TYPE (io_context, "GnumIOContext", IOContext, \
             io_context_class_init, io_context_init, \
             GTK_TYPE_OBJECT)

IOContext *
gnumeric_io_context_new (WorkbookControl *wbc)
{
	IOContext *io_context;

	g_return_val_if_fail (IS_WORKBOOK_CONTROL (wbc), NULL);

	io_context = IO_CONTEXT (gtk_type_new (TYPE_IO_CONTEXT));
	io_context->impl = wbc;

	return io_context;
}

void
gnumeric_io_error_system (IOContext *context,
                          gchar const *message)
{
	g_return_if_fail (context != NULL);
	g_return_if_fail (message != NULL);

	gnumeric_io_error_string (context, message);
}

void
gnumeric_io_error_read (IOContext *context,
                        gchar const *message)
{
	g_return_if_fail (context != NULL);
	g_return_if_fail (message != NULL);

	gnumeric_io_error_string (context, message);
}

void
gnumeric_io_error_save (IOContext *context,
                        gchar const *message)
{
	g_return_if_fail (context != NULL);
	g_return_if_fail (message != NULL);

	gnumeric_io_error_string (context, message);
}

void
gnumeric_io_error_unknown (IOContext *context)
{
	g_return_if_fail (context != NULL);
	g_return_if_fail (context->impl != NULL);

	context->error_occurred = TRUE;
}

void
gnumeric_io_error_info_set (IOContext *context, ErrorInfo *error)
{
	g_return_if_fail (context != NULL);
	g_return_if_fail (error != NULL);

	g_return_if_fail (context->error_info == NULL);

	context->error_info = error;
	context->error_occurred = TRUE;
}

void
gnumeric_io_error_string (IOContext *context, const gchar *str)
{
	ErrorInfo *error;

	g_return_if_fail (context != NULL);
	g_return_if_fail (str != NULL);

	error = error_info_new_str (str);
	gnumeric_io_error_info_set (context, error);
}

void
gnumeric_io_error_push (IOContext *context, ErrorInfo *error)
{
	g_return_if_fail (context != NULL);
	g_return_if_fail (error != NULL);

	error_info_add_details (error, context->error_info);
	context->error_info = error;
}

ErrorInfo *
gnumeric_io_error_pop  (IOContext *context)
{
	ErrorInfo *error;

	g_return_val_if_fail (context != NULL, NULL);

	error = context->error_info;
	context->error_info = NULL;

	return error;
}

void
gnumeric_io_error_display (IOContext *context)
{
	g_return_if_fail (context != NULL);

	if (context->error_info != NULL) {
		gnumeric_error_error_info (COMMAND_CONTEXT (context->impl),
		                           context->error_info);
	}
}

void
gnumeric_io_error_clear (IOContext *context)
{
	g_return_if_fail (context != NULL);

	context->error_occurred = FALSE;
	error_info_free (context->error_info);
	context->error_info = NULL;
}

gboolean
gnumeric_io_error_occurred (IOContext *context)
{
	return context->error_occurred;
}

void
io_progress_update (IOContext *io_context, gdouble f)
{
	g_return_if_fail (IS_IO_CONTEXT (io_context));

	if (io_context->progress_ranges != NULL) {
		f = f * (io_context->progress_max - io_context->progress_min)
		    + io_context->progress_min;
	}
	if (f - io_context->last_progress >= PROGRESS_UPDATE_STEP) {
		struct timeval tv;
		double t;

		(void) gettimeofday (&tv, NULL);
		t = tv.tv_sec + tv.tv_usec / 1000000.0;
		if (t - io_context->last_time >= PROGRESS_UPDATE_PERIOD_SEC) {
			gnumeric_progress_set (COMMAND_CONTEXT (io_context->impl), f);
			io_context->last_time = t;
			io_context->last_progress = f;
		}
	}

	while (gtk_events_pending ())
		gtk_main_iteration_do (FALSE);
}

void
io_progress_message (IOContext *io_context, const gchar *msg)
{
	g_return_if_fail (IS_IO_CONTEXT (io_context));

	gnumeric_progress_message_set (COMMAND_CONTEXT (io_context->impl), msg);
}

void
io_progress_range_push (IOContext *io_context, gdouble min, gdouble max)
{
	ProgressRange *r;
	gdouble new_min, new_max;

	g_return_if_fail (IS_IO_CONTEXT (io_context));

	r = g_new (ProgressRange, 1);
	r->min = min;
	r->max = max;
	io_context->progress_ranges = g_list_append (io_context->progress_ranges, r);

	new_min = min / (io_context->progress_max - io_context->progress_min)
	          + io_context->progress_min;
	new_max = max / (io_context->progress_max - io_context->progress_min)
	          + io_context->progress_min;
	io_context->progress_min = new_min;
	io_context->progress_max = new_max;
}

void
io_progress_range_pop (IOContext *io_context)
{
	GList *l;

	g_return_if_fail (IS_IO_CONTEXT (io_context));
	g_return_if_fail (io_context->progress_ranges != NULL);

	l = g_list_last (io_context->progress_ranges);
	io_context->progress_ranges= g_list_remove_link (io_context->progress_ranges, l);
	g_free (l->data);
	g_list_free_1 (l);

	io_context->progress_min = 0.0;
	io_context->progress_max = 1.0;
	for (l = io_context->progress_ranges; l != NULL; l = l->next) {
		ProgressRange *r = l->data;
		gdouble new_min, new_max;

		new_min = r->min / (io_context->progress_max - io_context->progress_min)
		          + io_context->progress_min;
		new_max = r->max / (io_context->progress_max - io_context->progress_min)
		          + io_context->progress_min;
		io_context->progress_min = new_min;
		io_context->progress_max = new_max;
	}
}

void
file_io_progress_set (IOContext *io_context, const gchar *file_name, FILE *f)
{
	struct stat sbuf;

	g_return_if_fail (IS_IO_CONTEXT (io_context));
	g_return_if_fail (file_name != NULL && f != NULL);

	io_context->helper.helper_type = GNUM_PROGRESS_HELPER_FILE;
	io_context->helper.v.file.f = f;
	if (stat (file_name, &sbuf) == 0) {
		io_context->helper.v.file.size = MAX ((glong) sbuf.st_size, 1);
	} else {
		io_context->helper.v.file.size = LONG_MAX;
	}
}

void
file_io_progress_update (IOContext *io_context)
{
	glong pos;
	gdouble complete;

	g_return_if_fail (IS_IO_CONTEXT (io_context));
	g_return_if_fail (io_context->helper.helper_type = GNUM_PROGRESS_HELPER_FILE);

	pos = ftell (io_context->helper.v.file.f);
	if (pos == -1) {
		pos = io_context->helper.v.file.size;
	}
	complete = 1.0 * pos / io_context->helper.v.file.size;
	io_progress_update (io_context, complete);
}

void
memory_io_progress_set (IOContext *io_context, gpointer mem_start, gint mem_size)
{
	g_return_if_fail (IS_IO_CONTEXT (io_context));
	g_return_if_fail (mem_start != NULL && mem_size >=0);

	io_context->helper.helper_type = GNUM_PROGRESS_HELPER_MEM;
	io_context->helper.v.mem.start = mem_start;
	io_context->helper.v.mem.size = MAX (mem_size, 1);
}

void
memory_io_progress_update (IOContext *io_context, void *mem_current)
{
	gchar *cur = mem_current;
	gdouble complete;

	g_return_if_fail (IS_IO_CONTEXT (io_context));
	g_return_if_fail (io_context->helper.helper_type = GNUM_PROGRESS_HELPER_MEM);

	complete = 1.0 * (cur - io_context->helper.v.mem.start)
	           / io_context->helper.v.mem.size;
	io_progress_update (io_context, complete);
}

void
value_io_progress_set (IOContext *io_context, gint total, gint step)
{
	g_return_if_fail (IS_IO_CONTEXT (io_context));
	g_return_if_fail (total >=0);

	io_context->helper.helper_type = GNUM_PROGRESS_HELPER_VALUE;
	io_context->helper.v.value.total = MAX (total, 1);
	io_context->helper.v.value.last = -step;
	io_context->helper.v.value.step = step;
}

void
value_io_progress_update (IOContext *io_context, gint value)
{
	gdouble complete;

	g_return_if_fail (IS_IO_CONTEXT (io_context));
	g_return_if_fail (io_context->helper.helper_type = GNUM_PROGRESS_HELPER_COUNT);

	if (value - io_context->helper.v.value.last < io_context->helper.v.value.step) {
		return;
	}
	io_context->helper.v.value.last = value;

	complete = 1.0 * value / io_context->helper.v.value.total;
	io_progress_update (io_context, complete);
}

void
count_io_progress_set (IOContext *io_context, gint total, gint step)
{
	g_return_if_fail (IS_IO_CONTEXT (io_context));
	g_return_if_fail (total >=0);

	io_context->helper.helper_type = GNUM_PROGRESS_HELPER_COUNT;
	io_context->helper.v.count.total = MAX (total, 1);
	io_context->helper.v.count.last = -step;
	io_context->helper.v.count.current = 0;
	io_context->helper.v.count.step = step;
}

void
count_io_progress_update (IOContext *io_context, gint inc)
{
	gdouble complete;

	g_return_if_fail (IS_IO_CONTEXT (io_context));
	g_return_if_fail (io_context->helper.helper_type = GNUM_PROGRESS_HELPER_COUNT);

	io_context->helper.v.count.current += inc;
	if (io_context->helper.v.count.current - io_context->helper.v.count.last
	    < io_context->helper.v.count.step) {
		return;
	}
	io_context->helper.v.count.last = io_context->helper.v.count.current;

	complete = 1.0 * io_context->helper.v.count.current
	           / io_context->helper.v.count.total;
	io_progress_update (io_context, complete);
}

void
workbook_io_progress_set (IOContext *io_context, Workbook *wb,
                          WbProgressElements elements, gint step)
{
	gint n = 0;
	GList *sheets, *l;

	g_return_if_fail (IS_IO_CONTEXT (io_context));
	g_return_if_fail (IS_WORKBOOK (wb));
	g_return_if_fail (elements <= WB_PROGRESS_ALL);

	sheets = workbook_sheets (wb);
	for (l = sheets; l != NULL; l = l->next) {
		Sheet *sheet = l->data;

		if ((elements & WB_PROGRESS_CELLS) != 0)
			n += g_hash_table_size (sheet->cell_hash);
	}
	g_list_free (sheets);

	io_context->helper.helper_type = GNUM_PROGRESS_HELPER_WORKBOOK;
	io_context->helper.v.workbook.n_elements = MAX (n, 1);
	io_context->helper.v.workbook.last = -step;
	io_context->helper.v.workbook.current = 0;
	io_context->helper.v.workbook.step = step;
}

void
workbook_io_progress_update (IOContext *io_context, gint inc)
{
	gdouble complete;

	g_return_if_fail (IS_IO_CONTEXT (io_context));
	g_return_if_fail (io_context->helper.helper_type = GNUM_PROGRESS_HELPER_WORKBOOK);

	io_context->helper.v.workbook.current += inc;
	if (io_context->helper.v.workbook.current - io_context->helper.v.workbook.last
	    < io_context->helper.v.workbook.step) {
		return;
	}
	io_context->helper.v.workbook.last = io_context->helper.v.workbook.current;

	complete = 1.0 * io_context->helper.v.workbook.current
	           / io_context->helper.v.workbook.n_elements;
	io_progress_update (io_context, complete);
}

void
io_progress_unset (IOContext *io_context)
{
	g_return_if_fail (IS_IO_CONTEXT (io_context));

	io_context->helper.helper_type = GNUM_PROGRESS_HELPER_NONE;
}
