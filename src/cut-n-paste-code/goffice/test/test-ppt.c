/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * testppt.c - 
 * Copyright (C) 2002, Ximian, Inc.
 *
 * Authors:
 *    <clahey@ximian.com>
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
#include <stdio.h>
#include <gsf/gsf-utils.h>
#include <glib.h>

#include <goffice/libpresent/load-ppt.h>

static void
dump_text (const char *text)
{
	char **texts = g_strsplit (text, "\xd", 0);
	int i;

	for (i = 0; texts[i]; i++) {
		printf ("\t\t%s\n", texts[i]);
	}

	g_strfreev (texts);
}

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
	g_print ("\n");

	text = god_shape_get_text (shape);
	if (text) {
		dump_text (text);
	}
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
	if (shape) {
		g_print ("Patriarch:\n");
		dump_shape (shape, 0);
			g_object_unref (shape);
	}
	shape = god_drawing_get_background (drawing);
	if (shape) {
		g_print ("Background:\n");
		dump_shape (shape, 0);
		g_object_unref (shape);
	}
}

int
main (int argc, char *argv[])
{
	int k;

	gsf_init ();
	for (k = 1 ; k < argc ; k++) {
		PresentPresentation *presentation;
		int i, slide_count;
		g_print( "%s\n",argv[k]);
		presentation = load_ppt (argv[k]);

		slide_count = present_presentation_get_slide_count (presentation);

		for (i = 0; i < slide_count; i++) {
			PresentSlide *slide = present_presentation_get_slide (presentation, i);

			dump_drawing (present_slide_get_drawing(slide));
#if 0
			int i, text_count;
			text_count = present_slide_get_text_count (slide);

			for (i = 0; i < text_count; i++) {
				PresentText *text = present_slide_get_text (slide, i);
				char *text_data;
				printf ("\tText %d of type %d:\n", present_text_get_text_id(text), present_text_get_text_type(text));
				text_data = god_text_model_get_text (GOD_TEXT_MODEL (text));
				if (text_data) {
					char **texts = g_strsplit (text_data, "\xd", 0);
					int j;

					for (j = 0; texts[j]; j++) {
						printf ("\t\t%s\n", texts[j]);
					}
				}
			}
#endif
		}
	}

	gsf_shutdown ();

	return 0;
}


