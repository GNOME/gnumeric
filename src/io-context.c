/*
 * io-context.c : Place holder for an io error context.
 *   It is intended to become a place to handle errors
 *   as well as storing non-fatal warnings.
 *
 * Author:
 * 	Jody Goldberg <jgoldberg@home.com>
 *
 * (C) 2000 Jody Goldberg
 */
#include <config.h>
#include "io-context-priv.h"
#include "command-context.h"
#include "gnumeric-util.h"

IOContext *
gnumeric_io_context_new (WorkbookControl *wbc)
{
	IOContext *context;

	g_return_val_if_fail (wbc != NULL, NULL);

	context = g_new (IOContext, 1);
	context->impl = wbc;
	context->error_info = NULL;

	return context;
}

void
gnumeric_io_context_free (IOContext *context)
{
	g_return_if_fail (context != NULL);

	error_info_free (context->error_info);
	g_free (context);
}

void
gnumeric_io_error_system (IOContext *context,
                          gchar const *message)
{
	g_return_if_fail (context != NULL);
	g_return_if_fail (context->impl != NULL);

	gnumeric_error_system (COMMAND_CONTEXT (context->impl), message);
}

void
gnumeric_io_error_read (IOContext *context,
                        gchar const *message)
{
	g_return_if_fail (context != NULL);
	g_return_if_fail (context->impl != NULL);

	gnumeric_error_read (COMMAND_CONTEXT (context->impl), message);
}

void
gnumeric_io_error_save (IOContext *context,
                        gchar const *message)
{
	g_return_if_fail (context != NULL);
	g_return_if_fail (context->impl != NULL);

	gnumeric_error_save (COMMAND_CONTEXT (context->impl), message);
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
gnumeric_io_progress_set (IOContext *context, gfloat f)
{
	g_return_if_fail (context != NULL);
	g_return_if_fail (context->impl != NULL);

	gnumeric_progress_set (COMMAND_CONTEXT (context->impl), f);
}
