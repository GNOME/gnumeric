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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */
#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include "gnumeric.h"
#include "input-msg.h"
#include "str.h"

#include <gsf/gsf-impl-utils.h>

struct _GnmInputMsg {
	GObject obj;
	String          *title;
	String          *msg;
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
		string_unref (msg->title);
		msg->title = NULL;
	}
	if (msg->msg != NULL) {
		string_unref (msg->msg);
		msg->msg = NULL;
	}

	parent_class = g_type_class_peek (G_TYPE_OBJECT);
	if (parent_class && parent_class->finalize)
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

GnmInputMsg *
gnm_input_msg_new (char const *msg, char const *title)
{
	GnmInputMsg *res = g_object_new (GNM_INPUT_MSG_TYPE, NULL);

	if (msg != NULL)
		res->msg = string_get (msg);
	if (title != NULL)
		res->title = string_get (title);

	return res;
}
