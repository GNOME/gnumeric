/*
 * command-context.c : Error dispatch utilities.
 *
 * Author:
 * 	Jody Goldberg <jody@gnome.org>
 *
 * (C) 1999-2001 Jody Goldberg
 */
#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include "gnumeric.h"
#include "command-context-priv.h"
#include "ranges.h"

#include <gsf/gsf-impl-utils.h>

#define CC_CLASS(o) COMMAND_CONTEXT_CLASS (G_OBJECT_GET_CLASS (o))

static GError *
format_message (GQuark id, char const *message)
{
	char const *msg = message ? message : "";
	return g_error_new (id, 0, msg);
}

void
cmd_context_error (CommandContext *context, GError *err)
{
	g_return_if_fail (IS_COMMAND_CONTEXT (context));
	CC_CLASS (context)->error.error (context, err);
}

void
gnumeric_error_error_info (CommandContext *context, ErrorInfo *error)
{
	g_return_if_fail (IS_COMMAND_CONTEXT (context));
	CC_CLASS (context)->error.error_info (context, error);
}

void
gnumeric_error_system (CommandContext *context, char const *message)
{
	GError *err = format_message (gnm_error_system (), message);
	cmd_context_error (context, err);
	g_error_free (err);
}

void
gnumeric_error_read (CommandContext *context, char const *message)
{
	GError *err = format_message (gnm_error_read (), message);
	cmd_context_error (context, err);
	g_error_free (err);
}

void
gnumeric_error_save (CommandContext *context, char const *message)
{
	GError *err = format_message (gnm_error_write (), message);
	cmd_context_error (context, err);
	g_error_free (err);
}

void
gnumeric_error_invalid (CommandContext *context, char const *msg, char const *val)
{
	GError *err = g_error_new (gnm_error_invalid(), 0, "Invalid %s : '%s'", msg, val);
	cmd_context_error (context, err);
	g_error_free (err);
}

void
gnumeric_error_calc (CommandContext *context, char const *msg)
{
	GError *err = format_message (gnm_error_calc (), msg);
	cmd_context_error (context, err);
	g_error_free (err);
}

void
gnumeric_error_splits_array (CommandContext *context,
			     char const *cmd, Range const *array)
{
	GError *err;

	if (array != NULL)
		err = g_error_new (gnm_error_array(), 1,
			_("Would split array %s"), range_name (array));
	else
		err = g_error_new (gnm_error_array(), 0,
			_("Would split an array"));
	cmd_context_error (context, err);
}

GQuark
gnm_error_system (void)
{
	static GQuark quark;
	if (!quark)
		quark = g_quark_from_static_string ("gnm_error_system");
	return quark;
}
GQuark
gnm_error_read (void)
{
	static GQuark quark;
	if (!quark)
		quark = g_quark_from_static_string ("gnm_error_read");
	return quark;
}
GQuark
gnm_error_write (void)
{
	static GQuark quark;
	if (!quark)
		quark = g_quark_from_static_string ("gnm_error_write");
	return quark;
}
GQuark
gnm_error_array (void)
{
	static GQuark quark;
	if (!quark)
		quark = g_quark_from_static_string ("gnm_error_array");
	return quark;
}

GQuark
gnm_error_calc (void)
{
	static GQuark quark;
	if (!quark)
		quark = g_quark_from_static_string ("gnm_error_calc");
	return quark;
}

GQuark
gnm_error_invalid (void)
{
	static GQuark quark;
	if (!quark)
		quark = g_quark_from_static_string ("gnm_error_invalid");
	return quark;
}

void
cmd_context_progress_set (CommandContext *context, gfloat f)
{
	g_return_if_fail (IS_COMMAND_CONTEXT (context));

	CC_CLASS (context)->progress_set (context, f);
}

void
cmd_context_progress_message_set (CommandContext *context, gchar const *msg)
{
	g_return_if_fail (IS_COMMAND_CONTEXT (context));

	CC_CLASS (context)->progress_message_set (context, msg);
}

char *
cmd_context_get_password (CommandContext *cc, char const *msg)
{
	g_return_val_if_fail (IS_COMMAND_CONTEXT (cc), NULL);

	return CC_CLASS (cc)->get_password (cc, msg);
}

void
cmd_context_set_sensitive (CommandContext *cc, gboolean sensitive)
{
	g_return_if_fail (IS_COMMAND_CONTEXT (cc));

	CC_CLASS (cc)->set_sensitive (cc, sensitive);
}

GSF_CLASS (CommandContext, command_context,
	   NULL, NULL, G_TYPE_OBJECT)
