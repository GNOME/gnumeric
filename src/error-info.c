/*
 * error-info.c: ErrorInfo structure.
 *
 * Author:
 *   Zbigniew Chyla (cyba@gnome.pl)
 */

#include <config.h>
#include <glib.h>
#include <stdio.h>
#include <errno.h>
#include "error-info.h"

struct _ErrorInfo {
	gchar *msg;
	GList *details;          /* list of ErrorInfo */
};

ErrorInfo *
error_info_new_str (const gchar *msg)
{
	ErrorInfo *error;

	error = g_new (ErrorInfo, 1);
	error->msg = g_strdup (msg);
	error->details = NULL;

	return error;
}

ErrorInfo *
error_info_new_vprintf (const gchar *msg_format, va_list args)
{
	ErrorInfo *error;

	error = g_new (ErrorInfo, 1);
	error->msg = g_strdup_vprintf (msg_format, args);
	error->details = NULL;

	return error;
}


ErrorInfo *
error_info_new_printf (const gchar *msg_format, ...)
{
	ErrorInfo *error;
	va_list args;

	va_start (args, msg_format);
	error = error_info_new_vprintf (msg_format, args);
	va_end (args);

	return error;
}

ErrorInfo *
error_info_new_str_with_details (const gchar *msg, ErrorInfo *details)
{
	ErrorInfo *error;

	error = error_info_new_str (msg);
	error_info_add_details (error, details);

	return error;
}

ErrorInfo *
error_info_new_str_with_details_list (const gchar *msg, GList *details)
{
	ErrorInfo *error;

	error = error_info_new_str (msg);
	error_info_add_details_list (error, details);

	return error;
}

ErrorInfo *
error_info_new_from_error_list (GList *errors)
{
	ErrorInfo *error;

	switch (g_list_length (errors)) {
	case 0:
		error = error_info_new_str (NULL);
		break;
	case 1:
		error = (ErrorInfo *) errors->data;
		g_list_free (errors);
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
	return error_info_new_str (g_strerror (errno));
}

void
error_info_add_details (ErrorInfo *error, ErrorInfo *details)
{
	g_return_if_fail (error != NULL);

	if (details == NULL) {
		;
	} else if (details->msg == NULL) {
		error->details = g_list_concat (error->details, details->details);
		g_free (details);
	} else {
		error->details = g_list_append (error->details, details);
	}
}

void
error_info_add_details_list (ErrorInfo *error, GList *details)
{
	GList *new_details_list, *l;

	g_return_if_fail (error != NULL);

	new_details_list = NULL;
	for (l = details; l != NULL; l = l->next) {
		ErrorInfo *details_error;

		details_error = (ErrorInfo *) l->data;
		if (details_error->msg == NULL) {
			GList *ll;

			for (ll = details_error->details; ll != NULL; ll = ll->next) {
				new_details_list = g_list_prepend (new_details_list, (ErrorInfo *) l->data);
			}
			g_free (details_error);
		} else {
			new_details_list = g_list_prepend (new_details_list, details_error);
		}
	}
	g_list_free (details);
	new_details_list = g_list_reverse (new_details_list);
	error->details = g_list_concat (error->details, new_details_list);
}

void
error_info_free (ErrorInfo *error)
{
	GList *l;

	if (error == NULL) {
		return;
	}

	g_free (error->msg);
	for (l = error->details; l != NULL; l = l->next) {
		error_info_free ((ErrorInfo *) l->data);
	}
	g_list_free (error->details);
	g_free(error);
}

static void
error_info_print_with_offset (ErrorInfo *error, gint offset)
{
	GList *l;

	if (error->msg != NULL) {
		printf ("%*s%s\n", offset, "", error->msg);
		offset += 2;
	}
	for (l = error->details; l != NULL; l = l->next) {
		error_info_print_with_offset ((ErrorInfo *) l->data, offset);
	}
}

void
error_info_print (ErrorInfo *error)
{
	g_return_if_fail (error != NULL);

	error_info_print_with_offset (error, 0);
}

const gchar *
error_info_peek_message (ErrorInfo *error)
{
	g_return_val_if_fail (error != NULL, NULL);

	return error->msg;
}

GList *
error_info_peek_details (ErrorInfo *error)
{
	g_return_val_if_fail (error != NULL, NULL);

	return error->details;
}
