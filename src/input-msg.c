/*
 * input-msg.c: Input Message
 *
 * Copyright (C) 2002 Jody Goldberg (jody@gnome.org)
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
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <input-msg.h>

#include <gsf/gsf-impl-utils.h>

struct _GnmInputMsg {
	GObject obj;
	GOString *title;
	GOString *msg;
};

typedef struct {
	GObjectClass obj;
} GnmInputMsgClass;

static void
gnm_input_msg_finalize (GObject *obj)
{
	GObjectClass *parent_class;
	GnmInputMsg *msg = (GnmInputMsg *)obj;

	go_string_unref (msg->title);
	msg->title = NULL;

	go_string_unref (msg->msg);
	msg->msg = NULL;

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
 * gnm_input_msg_new:
 * @msg: (nullable): A message to show
 * @title: (nullable): A title to show for the message
 *
 * Returns: a ref to new #GnmInputMsg.
 **/
GnmInputMsg *
gnm_input_msg_new (char const *msg, char const *title)
{
	GnmInputMsg *res = g_object_new (GNM_INPUT_MSG_TYPE, NULL);

	if (msg != NULL)
		res->msg = go_string_new (msg);
	if (title != NULL)
		res->title = go_string_new (title);

	return res;
}

gboolean
gnm_input_msg_equal (GnmInputMsg const *a,
		     GnmInputMsg const *b)
{
	g_return_val_if_fail (GNM_IS_INPUT_MSG (a), FALSE);
	g_return_val_if_fail (GNM_IS_INPUT_MSG (b), FALSE);

	return (g_strcmp0 (a->title ? a->title->str : NULL,
			   b->title ? b->title->str : NULL) == 0 &&
		g_strcmp0 (a->msg ? a->msg->str : NULL,
			   b->msg ? b->msg->str : NULL) == 0);
}


/**
 * gnm_input_msg_get_msg:
 * @msg: #GnmInputMsg
 *
 * Returns: (transfer none): The message to show
 **/
char const *
gnm_input_msg_get_msg (GnmInputMsg const *msg)
{
	return (msg->msg != NULL) ? msg->msg->str : "";
}

/**
 * gnm_input_msg_get_title:
 * @msg: #GnmInputMsg
 *
 * Returns: (transfer none): The title of the message to show
 **/
char const  *
gnm_input_msg_get_title (GnmInputMsg const *msg)
{
	return (msg->title != NULL) ? msg->title->str : "";
}
