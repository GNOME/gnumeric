/*
 *  Copyright (C) 2000 Marco Pesenti Gritti
 *  Copyright (C) 2003 Andreas J. Guelzow
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef GNUMERIC_CHARSET_H
#define GNUMERIC_CHARSET_H

#include <gtk/gtk.h>

typedef enum {
	LG_ARABIC,
	LG_BALTIC,
	LG_CENTRAL_EUROPEAN,
	LG_CHINESE,
	LG_CYRILLIC,
	LG_GREEK,
	LG_HEBREW,
	LG_INDIAN,
	LG_JAPANESE, 
	LG_KOREAN,
	LG_TURKISH,
	LG_UNICODE,
	LG_VIETNAMESE,
	LG_WESTERN,
	LG_OTHER,
	LG_LAST
} LanguageGroup;

typedef struct {
	gchar const *charset_title;
	gchar const *charset_name;
	LanguageGroup const lgroup;
} CharsetInfo;

typedef struct 
{
	GtkOptionMenu *encoding_classes;
	GtkMenu *encoding_classes_menu;
	unsigned int last_encoding_class;
	GtkOptionMenu *encodings;
	GtkMenu *encodings_menu;
} CharmapChooser;

typedef struct
{
	
        char const *group_name;
	
        gint preferred_encoding;
	
}
LGroupInfo;

/* language groups names */
extern LGroupInfo lgroups[];

/* translated charset titles */
extern CharsetInfo const charset_trans_array[];

/* FIXME */
/* extern const gchar *lang_encode_name[LANG_ENC_NUM];
 * extern const gchar *lang_encode_item[LANG_ENC_NUM];
 */

GtkWidget *make_charmap_chooser (CharmapChooser *chooser);
gchar const *charmap_chooser_get_selected_encoding (CharmapChooser *data);
void charmap_chooser_set_sensitive (CharmapChooser *data, gboolean sensitive);


#endif /* GNUMERIC_CHARSET_H */
