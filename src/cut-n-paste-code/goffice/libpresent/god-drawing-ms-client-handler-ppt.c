/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
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

#include <goffice/goffice-config.h>
#include <goffice/libpresent/god-drawing-ms-client-handler-ppt.h>
#include "ppt-types.h"
#include <goffice/utils/go-units.h>
#include <gsf/gsf-impl-utils.h>
#include <gsf/gsf-input.h>
#include <gsf/gsf-utils.h>
#include <string.h>
#include <goffice/ms-compat/go-ms-parser.h>
#include "ppt-parsing-helper.h"

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
	GPtrArray *fonts;
};

static const GOMSParserRecordType types[] =
{
	{	TextCharsAtom,			"TextCharsAtom",		FALSE,	TRUE,	-1,	-1	},
	{	TextBytesAtom,			"TextBytesAtom",		FALSE,	TRUE,	-1,	-1	},
	{	OutlineTextRefAtom,		"OutlineTextRefAtom",		FALSE,	TRUE,	-1,	-1	},
	{	StyleTextPropAtom,		"StyleTextPropAtom",		FALSE,	TRUE,	-1,	-1	},
};

GodDrawingMsClientHandler *
god_drawing_ms_client_handler_ppt_new (PresentSlide *slide, GPtrArray *fonts)
{
	GodDrawingMsClientHandler *handler;

	handler = g_object_new (GOD_DRAWING_MS_CLIENT_HANDLER_PPT_TYPE, NULL);

	GOD_DRAWING_MS_CLIENT_HANDLER_PPT(handler)->priv->slide = slide;
	GOD_DRAWING_MS_CLIENT_HANDLER_PPT(handler)->priv->fonts = fonts;

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
	GodTextModel *text_model;
	GodDrawingMsClientHandlerPpt *handler;
} TextParseState;

static void
handle_atom (GOMSParserRecord *record, GSList *stack, const guint8 *data, GsfInput *input, GError **err, gpointer user_data)
{
	TextParseState *parse_state = user_data;
	switch (record->opcode) {
	case TextCharsAtom:
		{
			char *text;
			ERROR (stack == NULL, "TextCharsAtom is root only inside ClientTextbox.");
			ERROR (parse_state->text_model == NULL, "Only one text per ClientTextbox.");
			
			text = g_utf16_to_utf8 ((gunichar2 *) data, record->length / 2, NULL, NULL, NULL);
			parse_state->text_model = god_text_model_new ();
			god_text_model_set_text (parse_state->text_model, text);
			g_free (text);
		}
		break;
	case TextBytesAtom:
		{
			char *text;
			ERROR (stack == NULL, "TextBytesAtom is root only inside ClientTextbox.");
			ERROR (parse_state->text_model == NULL, "Only one text per ClientTextbox.");

			text = g_convert (data, record->length, "utf8", "latin1", NULL, NULL, NULL);
			parse_state->text_model = god_text_model_new ();
			god_text_model_set_text (parse_state->text_model, text);
			g_free (text);
		}
		break;
	case OutlineTextRefAtom:
		{
			int outline_text_ref;
			PresentSlide *slide;
			int i, text_count;

			ERROR (stack == NULL, "OutlineTextRefAtom is root only inside ClientTextbox.");
			ERROR (parse_state->text_model == NULL, "Only one text per ClientTextbox.");

			slide = parse_state->handler->priv->slide;

			outline_text_ref = GSF_LE_GET_GUINT32 (data);

			if (slide) {
				text_count = present_slide_get_text_count (slide);
				for (i = 0; i < text_count; i++) {
					PresentText *text = present_slide_get_text (slide, i);
					if (present_text_get_text_id (text) == outline_text_ref) {
						parse_state->text_model = GOD_TEXT_MODEL (text);
						break;
					}
					g_object_unref (text);
				}
			}
		}
		break;
	case StyleTextPropAtom:
		{
			ERROR (stack == NULL, "StyleTextPropAtom is root only inside ClientTextbox.");
			ERROR (parse_state->text_model != NULL, "Must have text before StyleTextPropAtom ClientTextbox.");

			ppt_parsing_helper_parse_style_text_prop_atom (data, record->length, parse_state->text_model, parse_state->handler->priv->fonts);
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

	parse_state.text_model = NULL;
	parse_state.handler = GOD_DRAWING_MS_CLIENT_HANDLER_PPT (handler);

	go_ms_parser_read (input,
			   length,
			   types,
			   (sizeof (types) / sizeof (types[0])),
			   &callbacks,
			   &parse_state,
			   NULL);
	return parse_state.text_model;
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
