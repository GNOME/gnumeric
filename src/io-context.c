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

void
gnumeric_io_error_system (IOContext *context,
			  char const *message)
{
	g_return_if_fail (context != NULL);
	g_return_if_fail (context->impl != NULL);

	gnumeric_error_system (COMMAND_CONTEXT (context->impl), message);
}

void
gnumeric_io_error_read (IOContext *context,
			char const *message)
{
	g_return_if_fail (context != NULL);
	g_return_if_fail (context->impl != NULL);

	gnumeric_error_read (COMMAND_CONTEXT (context->impl), message);
}

void
gnumeric_io_error_save (IOContext *context,
			char const *message)
{
	g_return_if_fail (context != NULL);
	g_return_if_fail (context->impl != NULL);

	gnumeric_error_save (COMMAND_CONTEXT (context->impl), message);
}

void
gnumeric_io_progress_set (IOContext *context, gfloat f)
{
	g_return_if_fail (context != NULL);
	g_return_if_fail (context->impl != NULL);
	
	gnumeric_progress_set (COMMAND_CONTEXT (context->impl), f);
}
