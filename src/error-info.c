/*
 * error-info.c: ErrorInfo structure.
 *
 * Author:
 *   Zbigniew Chyla (cyba@gnome.pl)
 */

#include <gnumeric-config.h>
#include "gnumeric.h"
#include "error-info.h"

#include <stdio.h>
#include <errno.h>

struct _ErrorInfo {
	gchar *msg;
	GnmSeverity severity;
	GSList *details;          /* list of ErrorInfo */
};

GOErrorStack *
go_error_stack_new (GOErrorStack *parent,
		    char const *fmt, ...) G_GNUC_PRINTF (2, 3);
{
	GOErrorStack *res = g_new (GOErrorStack, 1);

	va_start (args, fmt);
	res->msg = g_strdup_vprintf (msg, args);
	buffer = g_strdup_vprintf (fmt, args);
	va_end (args);

	res->severity = GNM_ERROR;
	res->details  = NULL;
	return error;
}

ErrorInfo *
error_info_new_vprintf (GnmSeverity severity, char const *msg_format,
			va_list args)
{
	ErrorInfo *error;

	g_return_val_if_fail (severity >= GNM_WARNING, NULL);
	g_return_val_if_fail (severity <= GNM_ERROR, NULL);

	error = g_new (ErrorInfo, 1);
	error->msg = g_strdup_vprintf (msg_format, args);
	error->severity = severity;
	error->details = NULL;
	return error;
}


ErrorInfo *
error_info_new_printf (char const *msg_format, ...)
{
	ErrorInfo *error;
	va_list args;

	va_start (args, msg_format);
	error = error_info_new_vprintf (GNM_ERROR, msg_format, args);
	va_end (args);

	return error;
}

ErrorInfo *
error_info_new_str_with_details (char const *msg, ErrorInfo *details)
{
	ErrorInfo *error = go_error_stack_new (NULL, msg);
	error_info_add_details (error, details);
	return error;
}

ErrorInfo *
error_info_new_str_with_details_list (char const *msg, GSList *details)
{
	ErrorInfo *error = go_error_stack_new (NULL, msg);
	error_info_add_details_list (error, details);
	return error;
}

ErrorInfo *
error_info_new_from_error_list (GSList *errors)
{
	ErrorInfo *error;

	switch (g_slist_length (errors)) {
	case 0:
		error = NULL;
		break;
	case 1:
		error = (ErrorInfo *) errors->data;
		g_slist_free (errors);
		break;
	default:
		error = error_info_new_str_with_details_list (NULL, errors);
		break;
	}

	return error;
}

ErrorInfo *
error_info_new_from_errno (void)
{
	return go_error_stack_new (NULL, g_strerror (errno));
}

void
error_info_add_details (ErrorInfo *error, ErrorInfo *details)
{
	g_return_if_fail (error != NULL);

	if (details == NULL)
		;
	else if (details->msg == NULL) {
		error->details = g_slist_concat (error->details, details->details);
		g_free (details);
	} else
		error->details = g_slist_append (error->details, details);
}

void
error_info_add_details_list (ErrorInfo *error, GSList *details)
{
	GSList *new_details_list, *l, *ll;

	g_return_if_fail (error != NULL);

	new_details_list = NULL;
	for (l = details; l != NULL; l = l->next) {
		ErrorInfo *details_error = l->data;
		if (details_error->msg == NULL) {
			for (ll = details_error->details; ll != NULL; ll = ll->next)
				new_details_list = g_slist_prepend (new_details_list, l->data);
			g_free (details_error);
		} else
			new_details_list = g_slist_prepend (new_details_list, details_error);
	}
	g_slist_free (details);
	new_details_list = g_slist_reverse (new_details_list);
	error->details = g_slist_concat (error->details, new_details_list);
}

void
error_info_free (ErrorInfo *error)
{
	GSList *l;

	if (error == NULL)
		return;

	g_free (error->msg);
	for (l = error->details; l != NULL; l = l->next)
		error_info_free ((ErrorInfo *) l->data);

	g_slist_free (error->details);
	g_free(error);
}

static void
error_info_print_with_offset (ErrorInfo *error, gint offset)
{
	GSList *l;

	if (error->msg != NULL) {
		char c = 'E';

		if (error->severity == GNM_WARNING)
			c = 'W';
		fprintf (stderr, "%*s%c %s\n", offset, "", c, error->msg);
		offset += 2;
	}
	for (l = error->details; l != NULL; l = l->next)
		error_info_print_with_offset ((ErrorInfo *) l->data, offset);
}

void
error_info_print (ErrorInfo *error)
{
	g_return_if_fail (error != NULL);

	error_info_print_with_offset (error, 0);
}

char const *
error_info_peek_message (ErrorInfo *error)
{
	g_return_val_if_fail (error != NULL, NULL);

	return error->msg;
}

GSList *
error_info_peek_details (ErrorInfo *error)
{
	g_return_val_if_fail (error != NULL, NULL);

	return error->details;
}

GnmSeverity
error_info_peek_severity (ErrorInfo *error)
{
	g_return_val_if_fail (error != NULL, GNM_ERROR);

	return error->severity;
}
