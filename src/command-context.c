/*
 * command-context.c : Error dispatch utilities.
 *
 * Author:
 * 	Jody Goldberg <jody@gnome.org>
 *
 * (C) 1999-2001 Jody Goldberg
 */
#include <gnumeric-config.h>
#include <glib/gi18n.h>
#include "gnumeric.h"
#include "ranges.h"
#include "command-context.h"

#include <goffice/app/go-cmd-context-impl.h>

#define CC_CLASS(o) (G_TYPE_INSTANCE_GET_INTERFACE ((o), GO_CMD_CONTEXT_TYPE, GnmCmdContextClass))

static GError *
format_message (GQuark id, char const *message)
{
	char const *msg = message ? message : "";
	return g_error_new_literal (id, 0, msg);
}

void
go_cmd_context_error_calc (GOCmdContext *context, char const *msg)
{
	GError *err = format_message (go_error_calc (), msg);
	go_cmd_context_error (context, err);
	g_error_free (err);
}

void
go_cmd_context_error_splits_array (GOCmdContext *context,
				   G_GNUC_UNUSED char const *cmd,
				   GnmRange const *array)
{
	GError *err;

	if (array != NULL)
		err = g_error_new (go_error_array(), 1,
			_("Would split array %s"), range_name (array));
	else
		err = g_error_new (go_error_array(), 0,
			_("Would split an array"));
	go_cmd_context_error (context, err);
}

GQuark
go_error_array (void)
{
	static GQuark quark;
	if (!quark)
		quark = g_quark_from_static_string ("go_error_array");
	return quark;
}

GQuark
go_error_calc (void)
{
	static GQuark quark;
	if (!quark)
		quark = g_quark_from_static_string ("go_error_calc");
	return quark;
}
