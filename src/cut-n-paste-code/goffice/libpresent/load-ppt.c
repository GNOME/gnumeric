/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * load_ppt.c - 
 * Copyright (C) 2003, Christopher James Lahey
 *
 * Authors:
 *   Christopher James Lahey <clahey@ximian.com>
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU Library General Public
 * License as published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this file; if not, write to
 * the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 **/

#include <goffice/goffice-config.h>
#include "load-ppt.h"
#include "ppt-types.h"
#include "god-drawing-ms-client-handler-ppt.h"

#include <gsf/gsf-input-stdio.h>
#include <gsf/gsf-input-memory.h>
#include <gsf/gsf-utils.h>
#include <gsf/gsf-infile.h>
#include <gsf/gsf-infile-msole.h>
#include <gsf/gsf-msole-utils.h>
#include <ms-compat/god-drawing-ms.h>
#include <ms-compat/go-ms-parser.h>

#define ERROR(conditional, message) if (!(conditional)) { g_warning ((message)); }

static const GOMSParserRecordType types[] =
{
	/*	{	Unknown,			"Unknown",			FALSE,	FALSE,	NULL,	-1,	-1,	-1	}, */
	{	Document,			"Document",			TRUE,	FALSE,	-1,	-1	},
	{	DocumentAtom,			"DocumentAtom",			FALSE,	FALSE,	-1,	-1	},
	{	EndDocument,			"EndDocument",			FALSE,	FALSE,	-1,	-1	},
	{	Slide,				"Slide",			TRUE,	FALSE,	-1,	-1	},
	{	SlideAtom,			"SlideAtom",			FALSE,	FALSE,	-1,	-1	},
	{	Notes,				"Notes",			TRUE,	FALSE,	-1,	-1	},
	{	NotesAtom,			"NotesAtom",			FALSE,	FALSE,	-1,	-1	},
	{	Environment,			"Environment",			TRUE,	FALSE,	-1,	-1	},
	{	SlidePersistAtom,		"SlidePersistAtom",		FALSE,	TRUE,	20,	20	},
	{	SSlideLayoutAtom,		"SSlideLayoutAtom",		FALSE,	FALSE,	-1,	-1	},
	{	MainMaster,			"MainMaster",			TRUE,	FALSE,	-1,	-1	},
	{	SSSlideInfoAtom,		"SSSlideInfoAtom",		FALSE,	FALSE,	-1,	-1	},
	{	SlideViewInfo,			"SlideViewInfo",		TRUE,	FALSE,	-1,	-1	},
	{	GuideAtom,			"GuideAtom",			FALSE,	FALSE,	-1,	-1	},
	{	ViewInfo,			"ViewInfo",			TRUE,	FALSE,	-1,	-1	},
	{	ViewInfoAtom,			"ViewInfoAtom",			FALSE,	FALSE,	-1,	-1	},
	{	SlideViewInfoAtom,		"SlideViewInfoAtom",		FALSE,	FALSE,	-1,	-1	},
	{	VBAInfo,			"VBAInfo",			TRUE,	FALSE,	-1,	-1	},
	{	VBAInfoAtom,			"VBAInfoAtom",			FALSE,	FALSE,	-1,	-1	},
	{	SSDocInfoAtom,			"SSDocInfoAtom",		FALSE,	FALSE,	-1,	-1	},
	{	Summary,			"Summary",			TRUE,	FALSE,	-1,	-1	},
	{	DocRoutingSlip,			"DocRoutingSlip",		FALSE,	FALSE,	-1,	-1	},
	{	OutlineViewInfo,		"OutlineViewInfo",		TRUE,	FALSE,	-1,	-1	},
	{	SorterViewInfo,			"SorterViewInfo",		TRUE,	FALSE,	-1,	-1	},
	{	ExObjList,			"ExObjList",			TRUE,	FALSE,	-1,	-1	},
	{	ExObjListAtom,			"ExObjListAtom",		FALSE,	FALSE,	-1,	-1	},
	{	PPDrawingGroup,			"PPDrawingGroup",		FALSE,	FALSE,	-1,	-1	}, // Escher
	{	PPDrawing,			"PPDrawing",			FALSE,	FALSE,	-1,	-1	}, // Escher
	{	NamedShows,			"NamedShows",			FALSE,	FALSE,	-1,	-1	}, // don't know if container
	{	NamedShow,			"NamedShow",			TRUE,	FALSE,	-1,	-1	},
	{	NamedShowSlides,		"NamedShowSlides",		FALSE,	FALSE,	-1,	-1	}, // don't know if container
	{	List,				"List",				TRUE,	FALSE,	-1,	-1	},
	{	FontCollection,			"FontCollection",		TRUE,	FALSE,	-1,	-1	},
	{	BookmarkCollection,		"BookmarkCollection",		TRUE,	FALSE,	-1,	-1	},
	{	SoundCollAtom,			"SoundCollAtom",		FALSE,	FALSE,	-1,	-1	},
	{	Sound,				"Sound",			TRUE,	FALSE,	-1,	-1	},
	{	SoundData,			"SoundData",			FALSE,	FALSE,	-1,	-1	},
	{	BookmarkSeedAtom,		"BookmarkSeedAtom",		FALSE,	FALSE,	-1,	-1	},
	{	ColorSchemeAtom,		"ColorSchemeAtom",		FALSE,	FALSE,	-1,	-1	},
	{	ExObjRefAtom,			"ExObjRefAtom",			FALSE,	FALSE,	-1,	-1	},
	{	OEShapeAtom,			"OEShapeAtom",			FALSE,	FALSE,	-1,	-1	},
	{	OEPlaceholderAtom,		"OEPlaceholderAtom",		FALSE,	FALSE,	-1,	-1	},
	{	GPointAtom,			"GPointAtom",			FALSE,	FALSE,	-1,	-1	},
	{	GRatioAtom,			"GRatioAtom",			FALSE,	FALSE,	-1,	-1	},
	{	OutlineTextRefAtom,		"OutlineTextRefAtom",		FALSE,	FALSE,	-1,	-1	},
	{	TextHeaderAtom,			"TextHeaderAtom",		FALSE,	TRUE,	4,	4	},
	{	TextCharsAtom,			"TextCharsAtom",		FALSE,	TRUE,	-1,	-1	},
	{	StyleTextPropAtom,		"StyleTextPropAtom",		FALSE,	FALSE,	-1,	-1	},
	{	BaseTextPropAtom,		"BaseTextPropAtom",		FALSE,	FALSE,	-1,	-1	},
	{	TxMasterStyleAtom,		"TxMasterStyleAtom",		FALSE,	FALSE,	-1,	-1	},
	{	TxCFStyleAtom,			"TxCFStyleAtom",		FALSE,	FALSE,	-1,	-1	},
	{	TxPFStyleAtom,			"TxPFStyleAtom",		FALSE,	FALSE,	-1,	-1	},
	{	TextRulerAtom,			"TextRulerAtom",		FALSE,	FALSE,	-1,	-1	},
	{	TextBookmarkAtom,		"TextBookmarkAtom",		FALSE,	FALSE,	-1,	-1	},
	{	TextBytesAtom,			"TextBytesAtom",		FALSE,	TRUE,	-1,	-1	},
	{	TxSIStyleAtom,			"TxSIStyleAtom",		FALSE,	FALSE,	-1,	-1	},
	{	TextSpecInfoAtom,		"TextSpecInfoAtom",		FALSE,	FALSE,	-1,	-1	},
	{	DefaultRulerAtom,		"DefaultRulerAtom",		FALSE,	FALSE,	-1,	-1	},
	{	FontEntityAtom,			"FontEntityAtom",		FALSE,	FALSE,	-1,	-1	},
	{	FontEmbeddedData,		"FontEmbeddedData",		FALSE,	FALSE,	-1,	-1	},
	{	CString,			"CString",			FALSE,	FALSE,	-1,	-1	},
	{	MetaFile,			"MetaFile",			FALSE,	FALSE,	-1,	-1	},
	{	ExOleObjAtom,			"ExOleObjAtom",			FALSE,	FALSE,	-1,	-1	},
	{	SrKinsoku,			"SrKinsoku",			TRUE,	FALSE,	-1,	-1	},
	{	HandOut,			"HandOut",			TRUE,	FALSE,	-1,	-1	},
	{	ExEmbed,			"ExEmbed",			TRUE,	FALSE,	-1,	-1	},
	{	ExEmbedAtom,			"ExEmbedAtom",			FALSE,	FALSE,	-1,	-1	},
	{	ExLink,				"ExLink",			TRUE,	FALSE,	-1,	-1	},
	{	BookmarkEntityAtom,		"BookmarkEntityAtom",		FALSE,	FALSE,	-1,	-1	},
	{	ExLinkAtom,			"ExLinkAtom",			FALSE,	FALSE,	-1,	-1	},
	{	SrKinsokuAtom,			"SrKinsokuAtom",		FALSE,	FALSE,	-1,	-1	},
	{	ExHyperlinkAtom,		"ExHyperlinkAtom",		FALSE,	FALSE,	-1,	-1	},
	{	ExHyperlink,			"ExHyperlink",			TRUE,	FALSE,	-1,	-1	},
	{	SlideNumberMCAtom,		"SlideNumberMCAtom",		FALSE,	FALSE,	-1,	-1	},
	{	HeadersFooters,			"HeadersFooters",		TRUE,	FALSE,	-1,	-1	},
	{	HeadersFootersAtom,		"HeadersFootersAtom",		FALSE,	FALSE,	-1,	-1	},
	{	TxInteractiveInfoAtom,		"TxInteractiveInfoAtom",	FALSE,	FALSE,	-1,	-1	},
	{	CharFormatAtom,			"CharFormatAtom",		FALSE,	FALSE,	-1,	-1	},
	{	ParaFormatAtom,			"ParaFormatAtom",		FALSE,	FALSE,	-1,	-1	},
	{	RecolorInfoAtom,		"RecolorInfoAtom",		FALSE,	FALSE,	-1,	-1	},
	{	ExQuickTimeMovie,		"ExQuickTimeMovie",		TRUE,	FALSE,	-1,	-1	},
	{	ExQuickTimeMovieData,		"ExQuickTimeMovieData",		FALSE,	FALSE,	-1,	-1	},
	{	ExControl,			"ExControl",			TRUE,	FALSE,	-1,	-1	},
	{	SlideListWithText,		"SlideListWithText",		TRUE,	FALSE,	-1,	-1	},
	{	InteractiveInfo,		"InteractiveInfo",		TRUE,	FALSE,	-1,	-1	},
	{	InteractiveInfoAtom,		"InteractiveInfoAtom",		FALSE,	FALSE,	-1,	-1	},
	{	UserEditAtom,			"UserEditAtom",			FALSE,	FALSE,	-1,	-1	},
	{	CurrentUserAtom,		"CurrentUserAtom",		FALSE,	FALSE,	-1,	-1	},
	{	DateTimeMCAtom,			"DateTimeMCAtom",		FALSE,	FALSE,	-1,	-1	},
	{	GenericDateMCAtom,		"GenericDateMCAtom",		FALSE,	FALSE,	-1,	-1	},
	{	FooterMCAtom,			"FooterMCAtom",			FALSE,	FALSE,	-1,	-1	},
	{	ExControlAtom,			"ExControlAtom",		FALSE,	FALSE,	-1,	-1	},
	{	ExMediaAtom,			"ExMediaAtom",			FALSE,	FALSE,	-1,	-1	},
	{	ExVideo,			"ExVideo",			TRUE,	FALSE,	-1,	-1	},
	{	ExAviMovie,			"ExAviMovie",			TRUE,	FALSE,	-1,	-1	},
	{	ExMCIMovie,			"ExMCIMovie",			TRUE,	FALSE,	-1,	-1	},
	{	ExMIDIAudio,			"ExMIDIAudio",			TRUE,	FALSE,	-1,	-1	},
	{	ExCDAudio,			"ExCDAudio",			TRUE,	FALSE,	-1,	-1	},
	{	ExWAVAudioEmbedded,		"ExWAVAudioEmbedded",		TRUE,	FALSE,	-1,	-1	},
	{	ExWAVAudioLink,			"ExWAVAudioLink",		TRUE,	FALSE,	-1,	-1	},
	{	ExOleObjStg,			"ExOleObjStg",			FALSE,	FALSE,	-1,	-1	},
	{	ExCDAudioAtom,			"ExCDAudioAtom",		FALSE,	FALSE,	-1,	-1	},
	{	ExWAVAudioEmbeddedAtom,		"ExWAVAudioEmbeddedAtom",	FALSE,	FALSE,	-1,	-1	},
	{	AnimationInfoAtom,		"AnimationInfoAtom",		FALSE,	FALSE,	-1,	-1	},
	{	RTFDateTimeMCAtom,		"RTFDateTimeMCAtom",		FALSE,	FALSE,	-1,	-1	},
	{	ProgTags,			"ProgTags",			FALSE,	FALSE,	-1,	-1	}, // don't know if container
	{	ProgStringTag,			"ProgStringTag",		TRUE,	FALSE,	-1,	-1	},
	{	ProgBinaryTag,			"ProgBinaryTag",		TRUE,	FALSE,	-1,	-1	},
	{	BinaryTagData,			"BinaryTagData",		FALSE,	FALSE,	-1,	-1	},
	{	PrintOptions,			"PrintOptions",			FALSE,	FALSE,	-1,	-1	},
	{	PersistPtrFullBlock,		"PersistPtrFullBlock",		FALSE,	FALSE,	-1,	-1	}, // don't know if container
	{	PersistPtrIncrementalBlock,	"PersistPtrIncrementalBlock",	FALSE,	FALSE,	-1,	-1	},
	{	GScalingAtom,			"GScalingAtom",			FALSE,	FALSE,	-1,	-1	},
	{	GRColorAtom,			"GRColorAtom",			FALSE,	FALSE,	-1,	-1	},
};

typedef struct {
	PresentSlide *current_slide;
	int current_slide_text_count;
	PresentText  *current_text;
} SlideListWithTextParseState;

typedef struct {
	PresentSlide *slide;
} SlideParseState;

typedef struct {
	PresentPresentation *presentation;
	int slides_read;
} ParseUserData;

static void
slide_list_with_text_parse_state_finish_text (PresentPresentation *presentation, SlideListWithTextParseState *parse_state)
{
	if (parse_state->current_text) {
		ERROR (parse_state->current_slide, "Parse Error 1");
		present_slide_append_text (parse_state->current_slide, parse_state->current_text);
		g_object_unref (parse_state->current_text);
		parse_state->current_text = NULL;
		ERROR (parse_state->current_slide_text_count > 0, "Parse Error 2");
		parse_state->current_slide_text_count --;
	}
}

static void
slide_list_with_text_parse_state_finish_slide (PresentPresentation *presentation, SlideListWithTextParseState *parse_state)
{
	slide_list_with_text_parse_state_finish_text (presentation, parse_state);
	if (parse_state->current_slide) {
		present_presentation_append_slide (presentation, parse_state->current_slide);
		g_object_unref (parse_state->current_slide);
		parse_state->current_slide = NULL;
	}
}

#undef ERROR
#define ERROR(conditional, message) if (!(conditional)) { g_warning ("Error: %s", (message)); }

#define STACK_TOP GO_MS_PARSER_STACK_TOP(stack)
#define STACK_SECOND GO_MS_PARSER_STACK_SECOND(stack)

static void
dump_shape (GodShape *shape, int depth)
{
	GodAnchor *anchor;
	int i, count;
	const char *text;
	if (shape == NULL)
		return;

	for (i = 0; i < depth; i++) {
		g_print ("\t");
	}
	anchor = god_shape_get_anchor(shape);
	if (anchor) {
		GoRect rect;
		god_anchor_get_rect (anchor,
				     &rect);
		g_print ("%f, %f - %f, %f",
			 GO_UN_TO_IN ((double)rect.top),
			 GO_UN_TO_IN ((double)rect.left),
			 GO_UN_TO_IN ((double)rect.bottom),
			 GO_UN_TO_IN ((double)rect.right));
	}
	text = god_shape_get_text (shape);
	if (text) {
		g_print (" %s", text);
	}
	g_print ("\n");
	count = god_shape_get_child_count (shape);
	for (i = 0; i < count; i++) {
		GodShape *child;
		child = god_shape_get_child (shape, i);
		dump_shape (child, depth + 1);
		g_object_unref (child);
	}
}

static void
dump_drawing (GodDrawing *drawing)
{
	GodShape *shape;
	if (drawing == NULL)
		return;
	shape = god_drawing_get_root_shape (drawing);
	dump_shape (shape, 0);
	if (shape)
		g_object_unref (shape);
}

static void
handle_atom (GOMSParserRecord *record, GSList *stack, const guint8 *data, GsfInput *input, GError **err, gpointer user_data)
{
	ParseUserData *parse_user_data = user_data;
	switch (record->opcode) {
	case SlidePersistAtom:
		{
			SlideListWithTextParseState *parse_state;
			ERROR (stack && STACK_TOP->opcode == SlideListWithText, "Placement Error");

			parse_state = stack ? STACK_TOP->parse_state : NULL;
			if (parse_state) {
				slide_list_with_text_parse_state_finish_slide (parse_user_data->presentation, parse_state);
				parse_state->current_slide = present_slide_new();
				parse_state->current_slide_text_count = GSF_LE_GET_GUINT32 (data + 8);
			}
		}
		break;
	case TextHeaderAtom:
		{
			SlideListWithTextParseState *parse_state;
			ERROR (stack && STACK_TOP->opcode == SlideListWithText, "Placement Error");

			parse_state = stack ? STACK_TOP->parse_state : NULL;
			if (parse_state) {
				slide_list_with_text_parse_state_finish_text (parse_user_data->presentation, parse_state);
				parse_state->current_text = PRESENT_TEXT (present_text_new (record->inst, GSF_LE_GET_GUINT32(data)));
			}
		}
		break;
	case TextCharsAtom:
		{
			SlideListWithTextParseState *parse_state;
			ERROR (stack && STACK_TOP->opcode == SlideListWithText, "Placement Error");

			parse_state = stack ? STACK_TOP->parse_state : NULL;
			if (parse_state) {
				char *text = g_utf16_to_utf8 ((gunichar2 *) data, record->length / 2, NULL, NULL, NULL);
				god_text_model_set_text (GOD_TEXT_MODEL (parse_state->current_text), text);
				g_free (text);
			}
		}
		break;
	case TextBytesAtom:
		{
			SlideListWithTextParseState *parse_state;
			ERROR (stack && STACK_TOP->opcode == SlideListWithText, "Placement Error");

			parse_state = stack ? STACK_TOP->parse_state : NULL;
			if (parse_state) {
				char *text = g_strndup (data, record->length);
				god_text_model_set_text (GOD_TEXT_MODEL (parse_state->current_text), text);
				g_free (text);
			}
		}
		break;
#if 0
	case PPDrawingGroup:
		{
			drawing_group = god_drawing_group_read_ms (stream, record->length, handler, NULL);
		}
		break;
#endif
	case PPDrawing:
		{
			GodDrawingMsClientHandler *handler;
			ERROR (stack && (STACK_TOP->opcode == Slide ||
					 STACK_TOP->opcode == MainMaster ||
					 STACK_TOP->opcode == Notes), "Placement Error");

			if (STACK_TOP->opcode == Slide) {
				SlideParseState *parse_state = STACK_TOP->parse_state;

				handler = god_drawing_ms_client_handler_ppt_new (parse_state->slide);
			} else {
				handler = god_drawing_ms_client_handler_ppt_new (NULL);
			}
			GodDrawing *drawing = god_drawing_read_ms (input, record->length, handler, NULL);
			g_print ("Drawing read %p\n", drawing);
			dump_drawing (drawing);
			g_object_unref (handler);
			if (drawing)
				g_object_unref (drawing);
		}
		break;
	}
}

static void
start_container (GSList *stack, GsfInput *input, GError **err, gpointer user_data)
{
	ParseUserData *parse_user_data = user_data;
	switch (STACK_TOP->opcode) {
	case SlideListWithText:
		if (STACK_TOP->inst == 0) {
			SlideListWithTextParseState *parse_state = g_new0 (SlideListWithTextParseState, 1);
			STACK_TOP->parse_state = parse_state;
		}
		break;
	case Slide:
		{
			SlideParseState *parse_state = g_new0 (SlideParseState, 1);
			parse_state->slide = present_presentation_get_slide (parse_user_data->presentation,
									     parse_user_data->slides_read ++);
			STACK_TOP->parse_state = parse_state;
		}
		break;
	}
}


static void
end_container (GSList *stack, GsfInput *input, GError **err, gpointer user_data)
{
	ParseUserData *parse_user_data = user_data;
	switch (STACK_TOP->opcode) {
	case SlideListWithText:
		{
			SlideListWithTextParseState *parse_state;
			parse_state = STACK_TOP->parse_state;
			if (parse_state) {
				slide_list_with_text_parse_state_finish_slide (parse_user_data->presentation, parse_state);
				g_free (parse_state);
			}
		}
		break;
	default:
		break;
	}
}


static GOMSParserCallbacks callbacks = { handle_atom,
					 start_container,
					 end_container };


static PresentPresentation *
parse_stream (GsfInput *input, guint length)
{
	ParseUserData user_data;

	user_data.presentation = present_presentation_new ();
	user_data.slides_read = 0;

	go_ms_parser_read (input,
			   length,
			   types,
			   (sizeof (types) / sizeof (types[0])),
			   &callbacks,
			   &user_data,
			   NULL);

	return user_data.presentation;
}

PresentPresentation *
load_ppt (char *input_file)
{
	GsfInput  *input, *stream;
	GsfInfile *infile;
	GError    *err = NULL;
	PresentPresentation *presentation = NULL;

	input = GSF_INPUT (gsf_input_mmap_new (input_file, &err));
	if (input == NULL) {
		g_return_val_if_fail (err != NULL, NULL);
		g_warning ("'%s' error: %s", input_file, err->message);
		g_error_free (err);
		return NULL;
	}

	input = gsf_input_uncompress (input);

	infile = GSF_INFILE (gsf_infile_msole_new (input, &err));
	if (infile == NULL) {

		g_return_val_if_fail (err != NULL, NULL);

		g_warning ("'%s' Not an OLE file: %s", input_file, err->message);
		g_error_free (err);
		g_object_unref (G_OBJECT (input));
		return NULL;
	}

	stream = gsf_infile_child_by_name (infile, "PowerPoint Document");

	if (stream != NULL) {
		presentation = parse_stream (stream, gsf_input_remaining (stream));
		g_object_unref (G_OBJECT (stream));
	}
	g_object_unref (G_OBJECT (infile));
	g_object_unref (G_OBJECT (input));

	return presentation;
}
