/*
 * command-context.c: Gnumeric specific extensions to GOCmdContext
 *
 * Copyright (C) 1999-2005 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include <command-context.h>
#include <ranges.h>

#include <glib/gi18n-lib.h>

#define CC_CLASS(o) (G_TYPE_INSTANCE_GET_INTERFACE ((o), GO_TYPE_CMD_CONTEXT, GOCmdContextClass))

static GError *
format_message (GQuark id, char const *message)
{
	char const *msg = message ? message : "";
	return g_error_new_literal (id, 0, msg);
}

void
gnm_cmd_context_error_calc (GOCmdContext *context, char const *msg)
{
	GError *err = format_message (gnm_error_calc (), msg);
	go_cmd_context_error (context, err);
	g_error_free (err);
}

void
gnm_cmd_context_error_splits_array (GOCmdContext *context,
				    G_GNUC_UNUSED char const *cmd,
				    GnmRange const *array)
{
	GError *err;

	if (array != NULL)
		err = g_error_new (gnm_error_array(), 1,
			_("Would split array %s"), range_as_string (array));
	else
		err = g_error_new (gnm_error_array(), 0,
			_("Would split an array"));
	go_cmd_context_error (context, err);
	g_error_free (err);
}

void
gnm_cmd_context_error_splits_merge (GOCmdContext *context,
				    GnmRange const *merge)
{
	GError *err =
		g_error_new (gnm_error_array(), 1,
			     _("Would split merge %s"),
			     range_as_string (merge));
	go_cmd_context_error (context, err);
	g_error_free (err);
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
