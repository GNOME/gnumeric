/*
 * go-file.h : 
 *
 * Copyright (C) 2004 Morten Welinder (terra@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
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
#ifndef GO_FILE_H
#define GO_FILE_H

#include <glib.h>
#include <gsf/gsf.h>

G_BEGIN_DECLS

char *go_filename_from_uri (const char *uri);
char *go_filename_to_uri (const char *filename);
char *go_shell_arg_to_uri (const char *arg);
char *go_basename_from_uri (const char *uri);

GsfInput *go_file_open (char const *uri, GError **err);

G_END_DECLS

#endif /* GO_FILE_H */
