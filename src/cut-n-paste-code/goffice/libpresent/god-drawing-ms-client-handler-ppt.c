/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- *//* vim: set sw=8: */

/*
 * god-drawing-ms-client-handler-ppt.c: MS Office Graphic Object support
 *
 * Copyright (C) 2000-2004
 *	Jody Goldberg (jody@gnome.org)
 *	Michael Meeks (mmeeks@gnu.org)
 *      Christopher James Lahey <clahey@ximian.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
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
#include "libpresent/god-drawing-ms-client-handler-ppt.h"
#include "ppt-types.h"
#include <utils/go-units.h>
#include <gsf/gsf-impl-utils.h>
#include <gsf/gsf-input.h>
#include <gsf/gsf-utils.h>
#include <string.h>
#include <ms-compat/go-ms-parser.h>

#define CVS_VERSION "$Id$"
#define ERROR_STRING(cond,str) G_STRLOC "\n<" CVS_VERSION ">\n" str " (" #cond ")"
#define ERROR(cond,str) { \
	if (!(cond)) { \
		if (err) \
			g_set_error (err, domain, code, ERROR_STRING(cond,str)); \
		else \
			g_warning (ERROR_STRING(cond,str)); \
		return; \
	} \
}
#define ERROR_RETVAL(cond,str,retval) { \
	if (!(cond)) { \
		if (err) \
			g_set_error (err, domain, code, ERROR_STRING(cond,str)); \
		else \
			g_warning (ERROR_STRING(cond,str)); \
		return retval; \
	} \
}

static GQuark domain;
static gint code;

static GObjectClass *parent_class;

struct GodDrawingMsClientHandlerPptPrivate_ {
	PresentSlide *slide;
};

static const GOMSParserRecordType types[] =
{
	{	TextCharsAtom,			"TextCharsAtom",		FALSE,	TRUE,	-1,	-1	},
	{	OutlineTextRefAtom,		"OutlineTextRefAtom",		FALSE,	TRUE,	-1,	-1	},
};

GodDrawingMsClientHandler *
god_drawing_ms_client_handler_ppt_new (PresentSlide *slide)
{
	GodDrawingMsClientHandler *handler;

	handler = g_object_new (GOD_DRAWING_MS_CLIENT_HANDLER_PPT_TYPE, NULL);

	GOD_DRAWING_MS_CLIENT_HANDLER_PPT(handler)->priv->slide = slide;

	return handler;
}

static void
god_drawing_ms_client_handler_ppt_init (GObject *object)
{
	GodDrawingMsClientHandlerPpt *handler = GOD_DRAWING_MS_CLIENT_HANDLER_PPT (object);
	handler->priv = g_new0 (GodDrawingMsClientHandlerPptPrivate, 1);
}

static void
god_drawing_ms_client_handler_ppt_finalize (GObject *object)
{
	GodDrawingMsClientHandlerPpt *handler = GOD_DRAWING_MS_CLIENT_HANDLER_PPT (object);

	g_free (handler->priv);
	handler->priv = NULL;

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

typedef struct {
	char *text;
	int outline_text_ref;
} TextParseState;

static void
handle_atom (GOMSParserRecord *record, GSList *stack, const guint8 *data, GsfInput *input, GError **err, gpointer user_data)
{
	TextParseState *parse_state = user_data;
	switch (record->opcode) {
	case TextCharsAtom:
		{
			ERROR (stack == NULL, "TextCharsAtom is root only inside ClientTextbox.");
			ERROR (parse_state->text == NULL && parse_state->outline_text_ref == -1, "Only one text per ClientTextbox.");
			
			parse_state->text = g_utf16_to_utf8 ((gunichar2 *) data, record->length / 2, NULL, NULL, NULL);
		}
		break;
	case OutlineTextRefAtom:
		{
			ERROR (stack == NULL, "OutlineTextRefAtom is root only inside ClientTextbox.");
			ERROR (parse_state->text == NULL && parse_state->outline_text_ref == -1, "Only one text per ClientTextbox.");
			
			parse_state->outline_text_ref = GSF_LE_GET_GUINT32 (data);
		}
		break;
	}
}

static GOMSParserCallbacks callbacks = { handle_atom,
					 NULL,
					 NULL };

static GodTextModel *
god_drawing_ms_client_handler_ppt_handle_client_text    (GodDrawingMsClientHandler *handler,
							 const guint8              *data,
							 GsfInput                  *input,
							 gsf_off_t                  length,
							 GError                   **err)
{
	TextParseState parse_state;

	parse_state.text = NULL;
	parse_state.outline_text_ref = -1;
	go_ms_parser_read (input,
			   length,
			   types,
			   (sizeof (types) / sizeof (types[0])),
			   &callbacks,
			   &parse_state,
			   NULL);

	if (parse_state.text) {
		GodTextModel *text_model = god_text_model_new ();
		god_text_model_set_text (text_model, parse_state.text);

		g_free (parse_state.text);
		return text_model;
	} else if (parse_state.outline_text_ref != -1) {
		PresentSlide *slide = GOD_DRAWING_MS_CLIENT_HANDLER_PPT (handler)->priv->slide;
		PresentText *text = NULL;
		int i, text_count;

		if (slide) {
			text_count = present_slide_get_text_count (slide);
			for (i = 0; i < text_count; i++) {
				if (text)
					g_object_unref (text);
				text = present_slide_get_text (slide, i);
				if (present_text_get_text_id (text) == parse_state.outline_text_ref)
					break;
			}

			if (i == text_count) {
				if (text)
					g_object_unref (text);
				return NULL;
			}
			return GOD_TEXT_MODEL (text);
		} else {
			return NULL;
		}
	}
	return NULL;
}

static GodAnchor *
god_drawing_ms_client_handler_ppt_handle_client_anchor    (GodDrawingMsClientHandler *handler,
							   const guint8              *data,
							   GsfInput                  *input,
							   gsf_off_t                  length,
							   GError                   **err)
{
	GodAnchor *anchor;
	GoRect rect;

	ERROR_RETVAL (length == 8, "Incorrect EscherClientAnchor", NULL);

	rect.top = GO_IN_TO_UN ((gint64)GSF_LE_GET_GUINT16 (data)) / 576;
	rect.left = GO_IN_TO_UN ((gint64)GSF_LE_GET_GUINT16 (data + 2)) / 576;
	rect.right = GO_IN_TO_UN ((gint64)GSF_LE_GET_GUINT16 (data + 4)) / 576;
	rect.bottom = GO_IN_TO_UN ((gint64)GSF_LE_GET_GUINT16 (data + 6)) / 576;

	anchor = god_anchor_new ();

	god_anchor_set_rect (anchor, &rect);

	return anchor;
}

#if 0
static GObject *
god_drawing_ms_client_handler_ppt_handle_client_data    (GodDrawingMsClientHandler *handler,
							 const guint8              *data,
							 GsfInput                  *input,
							 gsf_off_t                  length,
							 GError                    **err)
{
	return NULL;
}
#endif

static void
god_drawing_ms_client_handler_ppt_class_init (GodDrawingMsClientHandlerPptClass *class)
{
	GObjectClass *object_class;
	GodDrawingMsClientHandlerClass *handler_class;

	object_class                         = (GObjectClass *) class;
	handler_class                        = (GodDrawingMsClientHandlerClass *) class;

	domain                               = g_quark_from_static_string ("GodDrawingMsClientHandlerPpt");
	code                                 = 1;

	parent_class                         = g_type_class_peek_parent (class);

	object_class->finalize               = god_drawing_ms_client_handler_ppt_finalize;

	handler_class->handle_client_text    = god_drawing_ms_client_handler_ppt_handle_client_text;
	handler_class->handle_client_anchor  = god_drawing_ms_client_handler_ppt_handle_client_anchor;
#if 0
	handler_class->handle_client_data    = god_drawing_ms_client_handler_ppt_handle_client_data;
#endif

	handler_class->client_text_read_data = FALSE;
}

GSF_CLASS (GodDrawingMsClientHandlerPpt, god_drawing_ms_client_handler_ppt,
	   god_drawing_ms_client_handler_ppt_class_init, god_drawing_ms_client_handler_ppt_init,
	   GOD_DRAWING_MS_CLIENT_HANDLER_TYPE)
