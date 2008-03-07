/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * input-msg.c: Input Message
 *
 * Copyright (C) 2002 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
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
#include <glib/gi18n-lib.h>
#include "gnumeric.h"
#include "input-msg.h"
#include "str.h"

#include <gsf/gsf-impl-utils.h>

struct _GnmInputMsg {
	GObject obj;
	GnmString *title;
	GnmString *msg;
};

typedef struct {
	GObjectClass obj;
} GnmInputMsgClass;

static void
gnm_input_msg_finalize (GObject *obj)
{
	GObjectClass *parent_class;
	GnmInputMsg *msg = (GnmInputMsg *)obj;

	if (msg->title != NULL) {
		gnm_string_unref (msg->title);
		msg->title = NULL;
	}
	if (msg->msg != NULL) {
		gnm_string_unref (msg->msg);
		msg->msg = NULL;
	}

	parent_class = g_type_class_peek (G_TYPE_OBJECT);
	parent_class->finalize (obj);
}

static void
gnm_input_msg_class_init (GObjectClass *object_class)
{
	object_class->finalize = gnm_input_msg_finalize;
}
static void
gnm_input_msg_init (GObject *obj)
{
	GnmInputMsg *msg = (GnmInputMsg * )obj;
	msg->title = NULL;
	msg->msg   = NULL;
}

GSF_CLASS (GnmInputMsg, gnm_input_msg,
	   gnm_input_msg_class_init, gnm_input_msg_init, G_TYPE_OBJECT)

/**
 * gnm_input_msg_new :
 * @msg :
 * @title :
 *
 * Returns: a ref to new #GnmInputMsg.
 **/
GnmInputMsg *
gnm_input_msg_new (char const *msg, char const *title)
{
	GnmInputMsg *res = g_object_new (GNM_INPUT_MSG_TYPE, NULL);

	if (msg != NULL)
		res->msg = gnm_string_get (msg);
	if (title != NULL)
		res->title = gnm_string_get (title);

	return res;
}

char const *
gnm_input_msg_get_msg (GnmInputMsg const *imsg)
{
	return (imsg->msg != NULL) ? imsg->msg->str : "";
}

char const  *
gnm_input_msg_get_title (GnmInputMsg const *imsg)
{
	return (imsg->title != NULL) ? imsg->title->str : "";
}
