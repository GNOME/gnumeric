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


#include <gnumeric-config.h>
#include <stdio.h>
#include <gsf/gsf-utils.h>
#include <glib.h>

#include <libpresent/load-ppt.h>

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
		}
	}

	gsf_shutdown ();

	return 0;
}


