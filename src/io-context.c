/*
 * io-context.c : Place holder for an io error context.
 *   It is intended to become a place to handle errors
 *   as well as storing non-fatal warnings.
 *
 * Authors:
 * 	Jody Goldberg <jgoldberg@home.com>
 *  Zbigniew Chyla <cyba@gnome.pl>
 *
 * (C) 2000 Jody Goldberg
 */
#include <config.h>
#include <limits.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>
#include "gnumeric.h"
#include "command-context.h"
#include "gnumeric-util.h"
#include "io-context.h"
#include "io-context-priv.h"

#define PROGRESS_UPDATE_STEP        0.01
#define PROGRESS_UPDATE_PERIOD_SEC  0.20


static void
io_context_init (IOContext *io_context)
{
	io_context->impl = NULL;
	io_context->error_info = NULL;
	io_context->error_occurred = FALSE;
	io_context->last_progress = 0.0;
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
	g_return_if_fail (context->impl != NULL);

	context->error_occurred = TRUE;
	gnumeric_error_system (COMMAND_CONTEXT (context->impl), message);
}

void
gnumeric_io_error_read (IOContext *context,
                        gchar const *message)
{
	g_return_if_fail (context != NULL);
	g_return_if_fail (context->impl != NULL);

	context->error_occurred = TRUE;
	gnumeric_error_read (COMMAND_CONTEXT (context->impl), message);
}

void
gnumeric_io_error_save (IOContext *context,
                        gchar const *message)
{
	g_return_if_fail (context != NULL);
	g_return_if_fail (context->impl != NULL);

	context->error_occurred = TRUE;
	gnumeric_error_save (COMMAND_CONTEXT (context->impl), message);
}

void
gnumeric_io_error_unknown (IOContext *context)
{
	g_return_if_fail (context != NULL);
	g_return_if_fail (context->impl != NULL);

	context->error_occurred = TRUE;
}

void
gnumeric_io_error_info_set  (IOContext *context, ErrorInfo *error)
{
	g_return_if_fail (context != NULL);
	g_return_if_fail (error != NULL);

	g_return_if_fail (context->error_info == NULL);

	context->error_info = error;
}

void
gnumeric_io_error_info_push (IOContext *context, ErrorInfo *error)
{
	g_return_if_fail (context != NULL);
	g_return_if_fail (error != NULL);

	error_info_add_details (error, context->error_info);
	context->error_info = error;
}

ErrorInfo *
gnumeric_io_error_info_pop  (IOContext *context)
{
	ErrorInfo *error;

	g_return_val_if_fail (context != NULL, NULL);

	error = context->error_info;
	context->error_info = NULL;

	return error;
}

void
gnumeric_io_error_info_clear (IOContext *context)
{
	g_return_if_fail (context != NULL);

	error_info_free (context->error_info);
	context->error_info = NULL;
}

void
gnumeric_io_error_info_display (IOContext *context)
{
	g_return_if_fail (context != NULL);

	gnumeric_error_info_dialog_show (WORKBOOK_CONTROL_GUI (context->impl),
	                                 context->error_info);
}

gboolean
gnumeric_io_has_error_info (IOContext *context)
{
	g_return_val_if_fail (context != NULL, FALSE);

	return context->error_info != NULL;
}

void
gnumeric_io_clear_error (IOContext *context)
{
	g_return_if_fail (context != NULL);

	context->error_occurred = FALSE;
	gnumeric_io_error_info_clear (context);
}

gboolean
gnumeric_io_error_occurred (IOContext *context)
{
	return context->error_occurred ||
	       gnumeric_io_has_error_info (context);
}

void
io_progress_update (IOContext *io_context, gdouble f)
{
	g_return_if_fail (IS_IO_CONTEXT (io_context));

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
file_io_progress_set (IOContext *io_context, const gchar *file_name,
                      FILE *f, gdouble min_f, gdouble max_f)
{
	struct stat sbuf;

	g_return_if_fail (IS_IO_CONTEXT (io_context));
	g_return_if_fail (file_name != NULL && f != NULL);
	g_return_if_fail (min_f <= max_f);

	io_context->helper.helper_type = GNUM_PROGRESS_HELPER_FILE;
	io_context->helper.v.file.f = f;
	if (stat (file_name, &sbuf) == 0) {
		io_context->helper.v.file.size = MAX ((glong) sbuf.st_size, 1);
	} else {
		io_context->helper.v.file.size = LONG_MAX;
	}
	io_context->helper.min_f = min_f;
	io_context->helper.max_f = max_f;
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
	complete = (io_context->helper.max_f - io_context->helper.min_f)
	           * pos / io_context->helper.v.file.size;
	io_progress_update (io_context, io_context->helper.min_f + complete);
}

void
memory_io_progress_set (IOContext *io_context, gpointer mem_start,
                        gint mem_size, gdouble min_f, gdouble max_f)
{
	g_return_if_fail (IS_IO_CONTEXT (io_context));
	g_return_if_fail (mem_start != NULL && mem_size >=0);
	g_return_if_fail (min_f <= max_f);

	io_context->helper.helper_type = GNUM_PROGRESS_HELPER_MEM;
	io_context->helper.v.mem.start = mem_start;
	io_context->helper.v.mem.size = MAX (mem_size, 1);
	io_context->helper.min_f = min_f;
	io_context->helper.max_f = max_f;
}

void
memory_io_progress_update (IOContext *io_context, void *mem_current)
{
	gchar *cur = mem_current;
	gdouble complete;

	g_return_if_fail (IS_IO_CONTEXT (io_context));
	g_return_if_fail (io_context->helper.helper_type = GNUM_PROGRESS_HELPER_MEM);

	complete = (io_context->helper.max_f - io_context->helper.min_f)
	           * (cur - io_context->helper.v.mem.start) / io_context->helper.v.mem.size;
	io_progress_update (io_context, io_context->helper.min_f + complete);
}

void
count_io_progress_set (IOContext *io_context, gint total,
                       gdouble min_f, gdouble max_f)
{
	g_return_if_fail (IS_IO_CONTEXT (io_context));
	g_return_if_fail (total >=0);
	g_return_if_fail (min_f <= max_f);

	io_context->helper.helper_type = GNUM_PROGRESS_HELPER_COUNT;
	io_context->helper.v.count.total = MAX (total, 1);
	io_context->helper.min_f = min_f;
	io_context->helper.max_f = max_f;
}

void
count_io_progress_update (IOContext *io_context, gint count)
{
	gdouble complete;

	g_return_if_fail (IS_IO_CONTEXT (io_context));
	g_return_if_fail (io_context->helper.helper_type = GNUM_PROGRESS_HELPER_COUNT);

	complete = (io_context->helper.max_f - io_context->helper.min_f)
	           * count / io_context->helper.v.count.total;
	io_progress_update (io_context, io_context->helper.min_f + complete);
}

void
io_progress_unset (IOContext *io_context)
{
	g_return_if_fail (IS_IO_CONTEXT (io_context));

	io_context->helper.helper_type = GNUM_PROGRESS_HELPER_NONE;
}
