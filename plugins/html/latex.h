/*
 * latex.h
 *
 * Copyright (C) 1999 Rasca, Berlin
 * EMail: thron@gmx.de
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#ifndef GNUMERIC_PLUGIN_LATEX_H
#define GNUMERIC_PLUGIN_LATEX_H

#include <gnumeric.h>
#include <goffice/goffice.h>

void latex_file_save (GOFileSaver const *fs, GOIOContext *io_context,
		      WorkbookView const *wb_view, GsfOutput *output);

void latex_table_file_save (GOFileSaver const *fs, GOIOContext *io_context,
		            WorkbookView const *wb_view, GsfOutput *output);

void latex_table_visible_file_save (GOFileSaver const *fs,
				    GOIOContext *io_context,
				    WorkbookView const *wb_view,
				    GsfOutput *output);

#endif
