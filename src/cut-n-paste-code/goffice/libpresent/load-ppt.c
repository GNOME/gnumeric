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
#include "utils/go-units.h"
#include "ppt-parsing-helper.h"

#include <gsf/gsf-input-stdio.h>
#include <gsf/gsf-input-memory.h>
#include <gsf/gsf-utils.h>
#include <gsf/gsf-infile.h>
#include <gsf/gsf-infile-msole.h>
#include <gsf/gsf-msole-utils.h>
#include <ms-compat/god-drawing-ms.h>
#include <ms-compat/go-ms-parser.h>

#include <string.h>

#define ERROR(conditional, message) if (!(conditional)) { g_warning ((message)); }

static const GOMSParserRecordType types[] =
{
	/*	{	Unknown,			"Unknown",			FALSE,	FALSE,	NULL,	-1,	-1,	-1	}, */
	{	Document,			"Document",			TRUE,	FALSE,	-1,	-1	},
	{	DocumentAtom,			"DocumentAtom",			FALSE,	TRUE,	-1,	-1	},
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
	{	StyleTextPropAtom,		"StyleTextPropAtom",		FALSE,	TRUE,	-1,	-1	},
	{	BaseTextPropAtom,		"BaseTextPropAtom",		FALSE,	FALSE,	-1,	-1	},
	{	TxMasterStyleAtom,		"TxMasterStyleAtom",		FALSE,	TRUE,	-1,	-1	},
	{	TxCFStyleAtom,			"TxCFStyleAtom",		FALSE,	FALSE,	-1,	-1	},
	{	TxPFStyleAtom,			"TxPFStyleAtom",		FALSE,	FALSE,	-1,	-1	},
	{	TextRulerAtom,			"TextRulerAtom",		FALSE,	FALSE,	-1,	-1	},
	{	TextBookmarkAtom,		"TextBookmarkAtom",		FALSE,	FALSE,	-1,	-1	},
	{	TextBytesAtom,			"TextBytesAtom",		FALSE,	TRUE,	-1,	-1	},
	{	TxSIStyleAtom,			"TxSIStyleAtom",		FALSE,	FALSE,	-1,	-1	},
	{	TextSpecInfoAtom,		"TextSpecInfoAtom",		FALSE,	FALSE,	-1,	-1	},
	{	DefaultRulerAtom,		"DefaultRulerAtom",		FALSE,	FALSE,	-1,	-1	},
	{	FontEntityAtom,			"FontEntityAtom",		FALSE,	TRUE,	-1,	-1	},
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
	GPtrArray *fonts;
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
handle_atom (GOMSParserRecord *record, GSList *stack, const guint8 *data, GsfInput *input, GError **err, gpointer user_data)
{
	ParseUserData *parse_user_data = user_data;
	switch (record->opcode) {
	case FontEntityAtom:
		if (record->inst >= parse_user_data->fonts->len)
			g_ptr_array_set_size (parse_user_data->fonts, record->inst + 1);
		if (g_ptr_array_index (parse_user_data->fonts, record->inst))
			g_free (g_ptr_array_index (parse_user_data->fonts, record->inst));
		g_ptr_array_index (parse_user_data->fonts, record->inst) = g_utf16_to_utf8 ((gunichar2 *) data, record->length / 2, NULL, NULL, NULL);
		break;
	case TxMasterStyleAtom:
		{
			int indentation_levels;
			int indentation_level;
			guint i = 0;
			GodDefaultAttributes *default_attributes;
			gboolean first = TRUE;

			if (stack && STACK_TOP->opcode == Environment)
				return;

			ERROR (stack && STACK_TOP->opcode == MainMaster, "Placement Error");

			default_attributes = god_default_attributes_new ();

			indentation_levels = GSF_LE_GET_GUINT16 (data);
			i += 2;
			if (record->inst >= 5) {
				i += 2;
				first = FALSE;
			}
			for (indentation_level = 0; indentation_level < indentation_levels; indentation_level ++) {
				GList *pango_attributes = NULL;
				GodParagraphAttributes *para_attributes = god_paragraph_attributes_new ();
				guint32 fields;

				/* Paragraph Attributes */
				fields = GSF_LE_GET_GUINT32 (data + i);
				i += 4;

				/*				g_print ("%d: %x\n", indentation_level, fields);*/
				if (fields & 0x000f) {
					i += 2; /* Bullet Flags */
				}
				if (fields & 0x0080) {
					g_object_set (para_attributes, "bullet_character", (guint) (GSF_LE_GET_GUINT16 (data + i)), NULL);
					i += 2; /* Bullet Char */
				}
				if (fields & 0x0010) {
					guint font_index = GSF_LE_GET_GUINT16 (data + i);
					if (font_index < parse_user_data->fonts->len &&
					    g_ptr_array_index (parse_user_data->fonts, font_index)) {
						g_object_set (para_attributes, "bullet_family", g_ptr_array_index (parse_user_data->fonts, font_index), NULL);
					}
					i += 2; /* Bullet Font */
				}
				if (fields & 0x0040) {
					g_object_set (para_attributes, "bullet_size", (double) GSF_LE_GET_GUINT16 (data + i) / 100.0, NULL);
					i += 2; /* Bullet Height */
				}
				if (fields & 0x0020)
					i += 4; /* Bullet Color */
				if (first ? (fields & 0x0f00) : (fields & 0x0800)) {
					g_object_set (para_attributes, "alignment", (guint) ((GSF_LE_GET_GUINT16 (data + i)) & 3), NULL);
					i += 2; /* Justification last 2 bits */
				}
				if (fields & 0x1000)
					i += 2; /* line feed */
				if (fields & 0x2000) {
					double space_before = 0;
					int space;
					space = GSF_LE_GET_GUINT16 (data + i);
					if (space & 0x8000) {
						space = 0x10000 - space;
					}
					space_before = space * (UN_PER_IN / 576.0);
					g_object_set (para_attributes,
						      "space_before", space_before,
						      NULL);
					i += 2; /* upper dist */
				}
				if (fields & 0x4000) {
					double space_after = 0;
					int space;
					space = GSF_LE_GET_GUINT16 (data + i);
					if (space & 0x8000) {
						space = 0x10000 - space;
					}
					space_after = space * (UN_PER_IN / 576.0);
					g_object_set (para_attributes,
						      "space_after", space_after,
						      NULL);
					i += 2; /* upper dist */
				}
				if (first) {
					if (fields & 0x8000) {
						g_object_set (para_attributes, "indent", (double) (GO_IN_TO_UN ((go_unit_t) GSF_LE_GET_GUINT16 (data + i)) / 576), NULL);
						i += 2; /* Text offset */
						if (i > record->length)
							break;
					}
					if (fields & 0x00010000) {
						g_object_set (para_attributes, "bullet_indent", (double) (GO_IN_TO_UN ((go_unit_t) GSF_LE_GET_GUINT16 (data + i)) / 576), NULL);
						i += 2; /* Bullet offset */
						if (i > record->length)
							break;
					}
					if (fields & 0x00020000) {
						i += 2; /* Default tab */
						if (i > record->length)
							break;
					}
					if (fields & 0x00200000) {
						guint tab_count = GSF_LE_GET_GUINT16 (data + i);
						i += 2 + tab_count * 4; /* Tabs */
						if (i > record->length)
							break;
					}
					if (fields & 0x00040000) {
						i += 2; /* Unknown */
						if (i > record->length)
							break;
					}
					if (fields & 0x00080000) {
						i += 2; /* Asian Line Break */
						if (i > record->length)
							break;
					}
					if (fields & 0x00100000) {
						i += 2; /* bidi */
						if (i > record->length)
							break;
					}
				} else {
					if (fields & 0x8000) {
						i += 2; /* Unknown */
						if (i > record->length)
							break;
					}
					if (fields & 0x0100) {
						g_object_set (para_attributes, "indent", (double) (GO_IN_TO_UN ((go_unit_t) GSF_LE_GET_GUINT16 (data + i)) / 576), NULL);
						i += 2; /* Text offset */
						if (i > record->length)
							break;
					}
					if (fields & 0x0200) {
						i += 2; /* Unknown */
						if (i > record->length)
							break;
					}
					if (fields & 0x0400) {
						g_object_set (para_attributes, "bullet_indent", (double) (GO_IN_TO_UN ((go_unit_t) GSF_LE_GET_GUINT16 (data + i)) / 576), NULL);
						i += 2; /* Bullet offset */
						if (i > record->length)
							break;

					}
					if (fields & 0x00010000) {
						i += 2; /* Unknown */
						if (i > record->length)
							break;
					}
					if (fields & 0x000e0000) {
						i += 2; /* Asian Line Break some bits. */
						if (i > record->length)
							break;
					}
					if (fields & 0x00100000) {
						guint tab_count = GSF_LE_GET_GUINT16 (data + i);
						i += 2 + tab_count * 4; /* Tabs */
						if (i > record->length)
							break;
					}
					if (fields & 0x00200000) {
						i += 2;
						if (i > record->length)
							break;
					}
				}

				/* Character Attributes */
				fields = GSF_LE_GET_GUINT32 (data + i);
				i += 4;
				if (fields & 0x0000ffff)
					i += 2; /* Bit Field */
				if (fields & 0x00010000) {
					guint font_index = GSF_LE_GET_GUINT16 (data + i);
					if (font_index < parse_user_data->fonts->len &&
					    g_ptr_array_index (parse_user_data->fonts, font_index)) {
						pango_attributes = g_list_prepend (pango_attributes,
										   pango_attr_family_new (g_ptr_array_index (parse_user_data->fonts, font_index)));
					}
					i += 2;
				}
				if (fields & 0x00200000)
					i += 2; /* Asian or Complex Font */
				if (fields & 0x00400000)
					i += 2; /* Unknown */
				if (fields & 0x00800000)
					i += 2; /* Symbol */
				if (fields & 0x00020000) {
					pango_attributes = g_list_prepend (pango_attributes,
									   pango_attr_size_new (GSF_LE_GET_GUINT16 (data + i) * PANGO_SCALE));
					i += 2;
				}
				if (fields & 0x00040000)
					i += 4; /* Font Color */
				if (fields & 0x00080000)
					i += 2; /* Escapement */
				if (fields & 0x00100000)
					i += 2; /* Unknown */

				god_default_attributes_set_paragraph_attributes_for_indent (default_attributes,
											    indentation_level,
											    para_attributes);
				god_default_attributes_set_pango_attributes_for_indent (default_attributes,
											indentation_level,
											pango_attributes);
				g_list_foreach (pango_attributes, (GFunc) pango_attribute_destroy, NULL);
				g_list_free (pango_attributes);
				g_object_unref (para_attributes);

				first = FALSE;
			}
			present_presentation_set_default_attributes_for_text_type (parse_user_data->presentation,
										   record->inst,
										   default_attributes);
			g_object_unref (default_attributes);
		}
		break;
	case DocumentAtom:
		{
			GodAnchor *anchor;
			GoRect rect;

			ERROR (stack && STACK_TOP->opcode == Document, "Placement Error");

			ERROR (record->length == 40, "Incorrect DocumentAtom");

			rect.top = 0;
			rect.left = 0;
			rect.right = GO_IN_TO_UN ((go_unit_t)GSF_LE_GET_GUINT32 (data)) / 576;
			rect.bottom = GO_IN_TO_UN ((go_unit_t)GSF_LE_GET_GUINT32 (data + 4)) / 576;

			anchor = god_anchor_new ();
			god_anchor_set_rect (anchor, &rect);
			present_presentation_set_extents (parse_user_data->presentation, anchor);

			rect.right = GO_IN_TO_UN ((go_unit_t)GSF_LE_GET_GUINT32 (data + 8)) / 576;
			rect.bottom = GO_IN_TO_UN ((go_unit_t)GSF_LE_GET_GUINT32 (data + 12)) / 576;

			anchor = god_anchor_new ();
			god_anchor_set_rect (anchor, &rect);
			present_presentation_set_notes_extents (parse_user_data->presentation, anchor);
		}
		break;
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
				g_object_set (parse_state->current_text,
					      "presentation", parse_user_data->presentation,
					      NULL);
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
				char *text = g_convert (data, record->length, "utf8", "latin1", NULL, NULL, NULL);
				god_text_model_set_text (GOD_TEXT_MODEL (parse_state->current_text), text);
				g_free (text);
			}
		}
		break;
	case StyleTextPropAtom:
		{
			SlideListWithTextParseState *parse_state;
			ERROR (stack && STACK_TOP->opcode == SlideListWithText, "Placement Error");
			parse_state = stack ? STACK_TOP->parse_state : NULL;
			if (parse_state) {
				ppt_parsing_helper_parse_style_text_prop_atom (data, record->length, GOD_TEXT_MODEL (parse_state->current_text), parse_user_data->fonts);
			}
		}
		break;
	case PPDrawingGroup:
		{
			GodDrawingGroup *drawing_group;
			ERROR (present_presentation_get_drawing_group (parse_user_data->presentation) == NULL, "Multiple Drawing Groups");
			drawing_group = god_drawing_group_read_ms (input, record->length, NULL, NULL);
			ERROR (drawing_group, "DrawingGroup load failed");
			present_presentation_set_drawing_group (parse_user_data->presentation,
								drawing_group);
			g_object_unref (drawing_group);
		}
		break;
	case PPDrawing:
		{
			GodDrawingMsClientHandler *handler;
			GodDrawing *drawing;

			ERROR (stack && (STACK_TOP->opcode == Slide ||
					 STACK_TOP->opcode == MainMaster ||
					 STACK_TOP->opcode == Notes), "Placement Error");

			if (STACK_TOP->opcode == Slide) {
				SlideParseState *parse_state = STACK_TOP->parse_state;

				handler = god_drawing_ms_client_handler_ppt_new (parse_state->slide, parse_user_data->fonts);
			} else {
				handler = god_drawing_ms_client_handler_ppt_new (NULL, parse_user_data->fonts);
			}

			drawing = god_drawing_read_ms (input, record->length, handler, NULL);
			ERROR (drawing, "Drawing load failed");
			god_drawing_set_drawing_group (drawing,
						       present_presentation_get_drawing_group (parse_user_data->presentation));
			g_object_unref (handler);
			if (STACK_TOP->opcode == Slide) {
				SlideParseState *parse_state = STACK_TOP->parse_state;
				present_slide_set_drawing (parse_state->slide,
							   drawing);
			}
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
	user_data.fonts = g_ptr_array_new();

	go_ms_parser_read (input,
			   length,
			   types,
			   (sizeof (types) / sizeof (types[0])),
			   &callbacks,
			   &user_data,
			   NULL);

	g_ptr_array_foreach (user_data.fonts, (GFunc) g_free, NULL);
	g_ptr_array_free (user_data.fonts, TRUE);

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

	input = GSF_INPUT (gsf_input_uncompress (input));

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

	if (presentation) {
		GodDrawingGroup *drawing_group = present_presentation_get_drawing_group (presentation);
		if (drawing_group) {
			stream = gsf_infile_child_by_name (infile, "Pictures");

			if (stream != NULL) {
				god_drawing_group_parse_images (drawing_group,
								stream,
								gsf_input_remaining (stream),
								NULL,
								NULL);
				g_object_unref (G_OBJECT (stream));
			}
		}
		g_object_unref (drawing_group);
	}
	g_object_unref (G_OBJECT (infile));
	g_object_unref (G_OBJECT (input));

	return presentation;
}
