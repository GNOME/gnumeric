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
char *go_dirname_from_uri (const char *uri, gboolean brief);

GsfInput *go_file_open (char const *uri, GError **err);
GsfOutput *go_file_create (char const *uri, GError **err);

GSList *go_file_split_uris (const char *data);

gchar *go_url_decode (gchar const *text);
gchar *go_url_encode (gchar const *text);

/*****************************************************************************/

#include <glib-object.h>
#include <goffice/app/goffice-app.h>

typedef struct {
	GObject	base;
} GOInOut;
typedef struct {
	GObjectClass base;
} GOInOutClass;

typedef struct {
	char *short_name;		/* suitable for a combo */
	char *long_description;
	GSList *suffixes;
	GSList *mime_types;
	gboolean needs_encoding;

	char *type_name;
} GOImporterDesc;

typedef struct {
	GOInOut	 	 base;

	GsfInput	*input;		/* These are set as construction properties */
	char		*encoding;
} GOImporter;
typedef struct {
	GOInOutClass	 base;

	GOImporterDesc	const *desc;
	gboolean 	(*Probe)  (GOImporter *imp);
	void	 	(*Import) (GOImporter *imp, GODoc *doc);
} GOImporterClass;

#define GO_IMPORTER_TYPE	(go_importer_get_type ())
#define GO_IMPORTER(o)		(G_TYPE_CHECK_INSTANCE_CAST((o), GO_IMPORTER_TYPE, GOImporter))
#define IS_GO_IMPORTER(o)	(G_TYPE_CHECK_INSTANCE_TYPE((o), GO_IMPORTER_TYPE))

GType go_importer_get_type (void);
GOImporterDesc const *go_importer_get_desc  (GOImporter const *imp);
gboolean	      go_importer_can_probe (GOImporter const *imp);
gboolean	      go_importer_probe     (GOImporter *imp);
void		      go_importer_read	    (GOImporter *imp, GODoc *doc);

void		      go_importer_warn	    (GOImporter *imp, char const *type,
					     char const *msg, ...) G_GNUC_PRINTF (3, 4);
void		      go_importer_fail	    (GOImporter *imp,
					     char const *msg, ...) G_GNUC_PRINTF (2, 3);

G_END_DECLS

#endif /* GO_FILE_H */
