/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * ppt-parsing-helper.c - 
 * Copyright (C) 2004, Christopher James Lahey
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
#include "ppt-parsing-helper.h"

#include <gsf/gsf-utils.h>
#include <goffice/utils/go-units.h>

#include <string.h>

enum {
	TEXT_FIELD_PROPERTY_EXISTS_BOLD              = 0x00000001,
	TEXT_FIELD_PROPERTY_EXISTS_ITALIC            = 0x00000002,
	TEXT_FIELD_PROPERTY_EXISTS_UNDERLINE         = 0x00000004,
	TEXT_FIELD_PROPERTY_EXISTS_SHADOW            = 0x00000010,
	TEXT_FIELD_PROPERTY_EXISTS_STRIKEOUT         = 0x00000100,
	TEXT_FIELD_PROPERTY_EXISTS_RELIEF            = 0x00000200,
	TEXT_FIELD_PROPERTY_EXISTS_RESET_NUMBERING   = 0x00000400,
	TEXT_FIELD_PROPERTY_EXISTS_ENABLE_NUMBERING1 = 0x00000800,
	TEXT_FIELD_PROPERTY_EXISTS_ENABLE_NUMBERING2 = 0x00001000,
	TEXT_FIELD_PROPERTY_EXISTS_FLAGS             = 0x0000ffff,
	TEXT_FIELD_PROPERTY_EXISTS_FONT              = 0x00010000,
	TEXT_FIELD_PROPERTY_EXISTS_FONT_SIZE         = 0x00020000,
	TEXT_FIELD_PROPERTY_EXISTS_COLOR             = 0x00040000,
	TEXT_FIELD_PROPERTY_EXISTS_OFFSET            = 0x00080000,
	TEXT_FIELD_PROPERTY_EXISTS_UNKNOWN1          = 0x00100000,
	TEXT_FIELD_PROPERTY_EXISTS_ASIAN_OR_COMPLEX  = 0x00200000,
	TEXT_FIELD_PROPERTY_EXISTS_UNKNOWN2          = 0x00400000,
	TEXT_FIELD_PROPERTY_EXISTS_SYMBOL            = 0x00800000,
	TEXT_FIELD_PROPERTY_EXISTS_UNKNOWN3          = 0x01000000,
	TEXT_FIELD_PROPERTY_EXISTS_UNKNOWN4          = 0x02000000,
	TEXT_FIELD_PROPERTY_EXISTS_UNKNOWN5          = 0x04000000,
	TEXT_FIELD_PROPERTY_EXISTS_UNKNOWN6          = 0x08000000,
	TEXT_FIELD_PROPERTY_EXISTS_UNKNOWN7          = 0x10000000,
	TEXT_FIELD_PROPERTY_EXISTS_UNKNOWN8          = 0x20000000,
	TEXT_FIELD_PROPERTY_EXISTS_UNKNOWN9          = 0x40000000,
	TEXT_FIELD_PROPERTY_EXISTS_UNKNOWN10         = 0x80000000,
} TextFieldPropExists;

enum {
	TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_BULLET_FLAGS     = 0x0000000f,
	TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_BULLET_CHARACTER = 0x00000080,
	TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_BULLET_FAMILY    = 0x00000010,
	TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_BULLET_SIZE      = 0x00000040,
	TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_BULLET_COLOR     = 0x00000020,
	TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_ALIGNMENT        = 0x00000800,
	TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_UNKNOWN_1        = 0x00000400,
	TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_UNKNOWN_2        = 0x00000200,
	TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_UNKNOWN_3        = 0x00000100,
	TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_LINE_FEED        = 0x00001000,
	TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_SPACING_ABOVE    = 0x00002000,
	TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_SPACING_BELOW    = 0x00004000,
	TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_UNKNOWN_4        = 0x00008000,
	TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_UNKNOWN_5        = 0x00010000,
	TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_ASIAN_UNKNOWN    = 0x000e0000,
	TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_BIDI             = 0x00200000,
} TextFieldParagraphPropExists;

enum {
	PARAGRAPH_ALIGNMENT_LEFT = 0,
	PARAGRAPH_ALIGNMENT_CENTER = 1,
	PARAGRAPH_ALIGNMENT_RIGHT = 2,
	PARAGRAPH_ALIGNMENT_JUSTIFY = 3,
} ParagraphAlignment;

void
ppt_parsing_helper_parse_style_text_prop_atom (const char *data, int length, GodTextModel *model, GPtrArray *fonts)
{
	int indent_type = 0;
	int i = 0;
	int position = 0;
	const char *text = god_text_model_get_text (model);
	int text_len = strlen (text);
	GodParagraphAttributes *para_attr;
	while (position < text_len) {
		int sublen = 0;
		int end;
		guint fields;
		int section_length = GSF_LE_GET_GUINT32 (data + i);
		para_attr = god_paragraph_attributes_new ();
		sublen += 4;
		indent_type = GSF_LE_GET_GUINT16 (data + i + sublen);
		sublen += 2;
		fields = GSF_LE_GET_GUINT32 (data + i + sublen);
		sublen += 4;
		if (fields & TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_BULLET_FLAGS) {
			sublen += 2;
		}
		if (fields & TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_BULLET_CHARACTER) {
			sublen += 2;
		}
		if (fields & TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_BULLET_FAMILY) {
			sublen += 2;
		}
		if (fields & TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_BULLET_SIZE) {
			sublen += 2;
#if 0 /* From OOo */
			if ( ! ( ( nMask & ( 1 << PPT_ParaAttr_BuHardHeight ) )
				 && ( nBulFlg && ( 1 << PPT_ParaAttr_BuHardHeight ) ) ) )
				aSet.mnAttrSet ^= 0x40;
#endif
		}
		if (fields & TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_BULLET_COLOR) {
			sublen += 4;
		}
		if (fields & TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_ALIGNMENT) {
			g_object_set (para_attr, "alignment", (guint) GSF_LE_GET_GUINT16 (data + i + sublen), NULL);
			sublen += 2;
		}
		if (fields & TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_UNKNOWN_1) {
			sublen += 2;
		}
		if (fields & TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_UNKNOWN_2) {
			sublen += 2;
		}
		if (fields & TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_UNKNOWN_3) {
			sublen += 2;
		}
		if (fields & TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_LINE_FEED) {
			sublen += 2;
		}

		if (fields & TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_SPACING_ABOVE) {
			int space;
			double space_before;
			space = GSF_LE_GET_GUINT16 (data + i + sublen);
			if (space & 0x8000) {
				space = 0x10000 - space;
			}
			space_before = space * (UN_PER_IN / 576.0);
			g_object_set (para_attr,
				      "space_before", space_before,
				      NULL);
			sublen += 2;
		}
		if (fields & TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_SPACING_BELOW) {
			int space;
			double space_after;
			space = GSF_LE_GET_GUINT16 (data + i + sublen);
			if (space & 0x8000) {
				space = 0x10000 - space;
			}
			space_after = space * (UN_PER_IN / 576.0);
			g_object_set (para_attr,
				      "space_after", space_after,
				      NULL);
			sublen += 2;
		}
		if (fields & TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_UNKNOWN_4)
			sublen += 2;
		if (fields & TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_UNKNOWN_5)
			sublen += 2;
		if (fields & TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_ASIAN_UNKNOWN)
			sublen += 2;
		if (fields & TEXT_FIELD_PARAGRAPH_PROPERTY_EXISTS_BIDI)
			sublen += 2;
		end = position;
		while (section_length && end < text_len) {
			section_length --;
			end += g_utf8_skip[(guchar) text[end]];
		}
		god_text_model_set_paragraph_attributes (model,
							 position, 
							 end,
							 para_attr);
		god_text_model_set_indent (model,
					   position,
					   end,
					   indent_type);
		g_object_unref (para_attr);
		i += sublen;
		position = end;
	}

	position = 0;
	while (position < text_len) {
		int sublen = 0;
		int end;
		GList *attrs = NULL, *iterator;
		guint fields;
		int section_length = GSF_LE_GET_GUINT32 (data + i);
		sublen += 4;
		fields = GSF_LE_GET_GUINT32 (data + i + sublen);
		sublen += 4;
		if (fields & TEXT_FIELD_PROPERTY_EXISTS_FLAGS) {
			guint text_fields = GSF_LE_GET_GUINT16 (data + i + sublen);
			if (fields & TEXT_FIELD_PROPERTY_EXISTS_BOLD)
				attrs = g_list_append (attrs, pango_attr_weight_new
						       (text_fields & TEXT_FIELD_PROPERTY_EXISTS_BOLD ?
							PANGO_WEIGHT_BOLD :
							PANGO_WEIGHT_NORMAL));
			if (fields & TEXT_FIELD_PROPERTY_EXISTS_ITALIC)
				attrs = g_list_append (attrs, pango_attr_style_new
						       (text_fields & TEXT_FIELD_PROPERTY_EXISTS_ITALIC ?
							PANGO_STYLE_ITALIC :
							PANGO_STYLE_NORMAL));
			if (fields & TEXT_FIELD_PROPERTY_EXISTS_UNDERLINE)
				attrs = g_list_append (attrs, pango_attr_underline_new
						       (text_fields & TEXT_FIELD_PROPERTY_EXISTS_UNDERLINE ?
							PANGO_UNDERLINE_SINGLE :
							PANGO_UNDERLINE_NONE));
#if 0
			if (text_fields & 0x10)
				printf ("shadow\n");
			if (text_fields & 0x200)
				printf ("relief\n");
#endif
			sublen += 2;
		}
		if (fields & TEXT_FIELD_PROPERTY_EXISTS_FONT) {
			guint font_index = GSF_LE_GET_GUINT16 (data + i + sublen);
			if (font_index < fonts->len &&
			    g_ptr_array_index (fonts, font_index)) {
				attrs = g_list_append (attrs, pango_attr_family_new
						       (g_ptr_array_index (fonts, font_index)));
			}
			sublen += 2;
		}
		if (fields & TEXT_FIELD_PROPERTY_EXISTS_ASIAN_OR_COMPLEX)
			sublen += 2;
		if (fields & TEXT_FIELD_PROPERTY_EXISTS_UNKNOWN2)
			sublen += 2;
		if (fields & TEXT_FIELD_PROPERTY_EXISTS_SYMBOL)
			sublen += 2;
		if (fields & TEXT_FIELD_PROPERTY_EXISTS_FONT_SIZE) {
			attrs = g_list_append (attrs, pango_attr_size_new
					       (GSF_LE_GET_GUINT16 (data + i + sublen) * PANGO_SCALE));
			sublen += 2;
		}
		if (fields & (TEXT_FIELD_PROPERTY_EXISTS_COLOR)) {
#if 0
			printf ("color: #");
			printf ("%02X", data[i + sublen++]);
			printf ("%02X", data[i + sublen++]);
			printf ("%02X", data[i + sublen++]);
			printf ("\n");
			sublen ++;
#endif
			sublen += 4;
		}
		if (fields & TEXT_FIELD_PROPERTY_EXISTS_OFFSET) {
#if 0
			int offset = GSF_LE_GET_GUINT16 (data + i + sublen - 2);
			if (offset & 0x8000)
				offset -= 0x10000;
			printf ("offset: %d\n", offset);
#endif
			sublen += 2;
		}
		if (fields & TEXT_FIELD_PROPERTY_EXISTS_UNKNOWN1)
			sublen += 2;
		if (fields & TEXT_FIELD_PROPERTY_EXISTS_UNKNOWN3)
			sublen += 2;
		if (fields & TEXT_FIELD_PROPERTY_EXISTS_UNKNOWN4)
			sublen += 2;
		if (fields & TEXT_FIELD_PROPERTY_EXISTS_UNKNOWN5)
			sublen += 2;
		if (fields & TEXT_FIELD_PROPERTY_EXISTS_UNKNOWN6)
			sublen += 2;
		if (fields & TEXT_FIELD_PROPERTY_EXISTS_UNKNOWN7)
			sublen += 2;
		if (fields & TEXT_FIELD_PROPERTY_EXISTS_UNKNOWN8)
			sublen += 2;
		if (fields & TEXT_FIELD_PROPERTY_EXISTS_UNKNOWN9)
			sublen += 2;
		if (fields & TEXT_FIELD_PROPERTY_EXISTS_UNKNOWN10)
			sublen += 2;
		end = position;
		while (section_length && end < text_len) {
			section_length --;
			end += g_utf8_skip[(guchar) text[end]];
		}
		god_text_model_set_pango_attributes (model,
						     position,
						     end,
						     attrs);
		for (iterator = attrs; iterator; iterator = iterator->next)
			pango_attribute_destroy (iterator->data);
		g_list_free (attrs);
		position = end;
		i += sublen;
	}
}
