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
	int i;
	GList *slidel, *textl;

	gsf_init ();
	for (i = 1 ; i < argc ; i++) {
		PresentPresentation *presentation;
		g_print( "%s\n",argv[i]);
		presentation = load_ppt (argv[i]);

		for (slidel = presentation->slides; slidel; slidel = slidel->next) {
			PresentSlide *slide = slidel->data;
			printf ("Slide:\n");
			for (textl = slide->texts; textl; textl = textl->next) {
				PresentText *text = textl->data;
				printf ("\tText %d of type %d:\n", text->id, text->type);
				if (text->text) {
					char **texts = g_strsplit (text->text, "\xd", 0);
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


