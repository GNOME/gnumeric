/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * io-context.c : Place holder for an io error context.
 *   It is intended to become a place to handle errors
 *   as well as storing non-fatal warnings.
 *
 * Authors:
 *	Jody Goldberg <jody@gnome.org>
 *	Zbigniew Chyla <cyba@gnome.pl>
 *
 * (C) 2000-2002 Jody Goldberg
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "io-context-priv.h"

#include "sheet.h"
#include "workbook.h"
#include "command-context.h"
#include "gui-util.h"

#include <gsf/gsf-impl-utils.h>
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
	io_context->info = NULL;
	io_context->error_occurred = FALSE;
	io_context->warning_occurred = FALSE;

	io_context->progress_ranges = NULL;
	io_context->progress_min = 0.0;
	io_context->progress_max = 1.0;
	io_context->last_progress = -1.0;
	io_context->last_time = 0.0;
	io_context->helper.helper_type = GNUM_PROGRESS_HELPER_NONE;
}

static void
ioc_finalize (GObject *obj)
{
	IOContext *io_context;

	g_return_if_fail (IS_IO_CONTEXT (obj));

	io_context = IO_CONTEXT (obj);
	error_info_free (io_context->info);
	if (io_context->impl) {
		cmd_context_progress_set (io_context->impl, 0.0);
		cmd_context_progress_message_set (io_context->impl, NULL);
		g_object_unref (G_OBJECT (io_context->impl));
	}

	G_OBJECT_CLASS (g_type_class_peek (COMMAND_CONTEXT_TYPE))->finalize (obj);
}

static char *
ioc_get_password (CommandContext *cc, char const *msg)
{
	IOContext *ioc = (IOContext *)cc;
	return cmd_context_get_password (ioc->impl, msg);
}

static void
ioc_set_sensitive (CommandContext *cc, gboolean sensitive)
{
	(void)cc; (void)sensitive;
}

static void
ioc_error_error (CommandContext *cc, GError *err)
{
	gnumeric_io_error_string (IO_CONTEXT (cc), err->message);
}

static void
ioc_error_error_info (__attribute__((unused)) CommandContext *ctxt,
		      ErrorInfo *error)
{
	/* TODO what goes here */
	error_info_print (error);
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

static void
io_context_class_init (IOContextClass *klass)
{
	CommandContextClass *cc_class = COMMAND_CONTEXT_CLASS (klass);

	G_OBJECT_CLASS (klass)->finalize = ioc_finalize;

	cc_class->get_password	   = ioc_get_password;
	cc_class->set_sensitive	   = ioc_set_sensitive;
	cc_class->error.error      = ioc_error_error;
	cc_class->error.error_info = ioc_error_error_info;
}

GSF_CLASS (IOContext, io_context,
	   io_context_class_init, io_context_init,
	   COMMAND_CONTEXT_TYPE)

IOContext *
gnumeric_io_context_new (CommandContext *cc)
{
	IOContext *io_context;

	g_return_val_if_fail (IS_COMMAND_CONTEXT (cc), NULL);

	io_context = g_object_new (TYPE_IO_CONTEXT, NULL);
	/* The cc is optional for subclasses, but mandatory in this class. */
	io_context->impl = cc;
	g_object_ref (G_OBJECT (io_context->impl));

	return io_context;
}

void
gnumeric_io_error_unknown (IOContext *context)
{
	g_return_if_fail (context != NULL);

	context->error_occurred = TRUE;
}

void
gnumeric_io_error_info_set (IOContext *context, ErrorInfo *error)
{
	g_return_if_fail (context != NULL);
	g_return_if_fail (error != NULL);

	g_return_if_fail (context->info == NULL);

	context->info = error;
	context->error_occurred = TRUE;
}

void
gnumeric_io_error_push (IOContext *context, ErrorInfo *error)
{
	g_return_if_fail (context != NULL);
	g_return_if_fail (error != NULL);

	error_info_add_details (error, context->info);
	context->info = error;
}

void
gnumeric_io_error_display (IOContext *context)
{
	CommandContext *cc;
	
	g_return_if_fail (context != NULL);

	if (context->info != NULL) {
		if (context->impl)
			cc = context->impl;
		else
			cc = COMMAND_CONTEXT (context);
		gnumeric_error_error_info (cc, context->info);
	}
}

/* TODO: Rename to gnumeric_io_info_clear */
void
gnumeric_io_error_clear (IOContext *context)
{
	g_return_if_fail (context != NULL);

	context->error_occurred = FALSE;
	context->warning_occurred = FALSE;
	error_info_free (context->info);
	context->info = NULL;
}

gboolean
gnumeric_io_error_occurred (IOContext *context)
{
	return context->error_occurred;
}

gboolean
gnumeric_io_warning_occurred (IOContext *context)
{
	return context->warning_occurred;
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
			CommandContext *cc;

			if (io_context->impl)
				cc = io_context->impl;
			else
				cc = COMMAND_CONTEXT (io_context);
			cmd_context_progress_set (cc, f);
			io_context->last_time = t;
			io_context->last_progress = f;
		}
	}

	/* FIXME : abstract this into the workbook control */
	while (gtk_events_pending ())
		gtk_main_iteration_do (FALSE);
}

void
io_progress_message (IOContext *io_context, const gchar *msg)
{
	CommandContext *cc;
	
	g_return_if_fail (IS_IO_CONTEXT (io_context));

	if (io_context->impl)
		cc = io_context->impl;
	else
		cc = COMMAND_CONTEXT (io_context);
	cmd_context_progress_message_set (cc, msg);
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
value_io_progress_set (IOContext *io_context, gint total, gint step)
{
	g_return_if_fail (IS_IO_CONTEXT (io_context));
	g_return_if_fail (total >= 0);

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
	g_return_if_fail (io_context->helper.helper_type == GNUM_PROGRESS_HELPER_VALUE);

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
	g_return_if_fail (total >= 0);

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
	g_return_if_fail (io_context->helper.helper_type == GNUM_PROGRESS_HELPER_COUNT);

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
workbook_io_progress_set (IOContext *io_context, Workbook *wb, gint step)
{
	gint n = 0;
	GList *sheets, *l;

	g_return_if_fail (IS_IO_CONTEXT (io_context));
	g_return_if_fail (IS_WORKBOOK (wb));

	sheets = workbook_sheets (wb);
	for (l = sheets; l != NULL; l = l->next) {
		Sheet *sheet = l->data;
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
	g_return_if_fail (io_context->helper.helper_type == GNUM_PROGRESS_HELPER_WORKBOOK);

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

void
gnm_io_warning (__attribute__((unused)) IOContext *context,
		char const *fmt, ...)
{
	ErrorInfo *condition;
	va_list args;

	va_start (args, fmt);
	context->info = error_info_new_vprintf (GNM_WARNING, fmt, args);
	va_end (args);

	context->warning_occurred = TRUE;
}

void
gnm_io_warning_unknown_font (IOContext *context,
			     __attribute__((unused)) char const *font_name)
{
	g_return_if_fail (IS_IO_CONTEXT (context));
}

void
gnm_io_warning_unknown_function	(IOContext *context,
				 __attribute__((unused)) char const *funct_name)
{
	g_return_if_fail (IS_IO_CONTEXT (context));
}

void
gnm_io_warning_unsupported_feature (IOContext *context, char const *feature)
{
	g_return_if_fail (IS_IO_CONTEXT (context));
	g_warning ("%s : are not supported yet", feature);
}
