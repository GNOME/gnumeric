/*
 *  Copyright (C) 2000 Marco Pesenti Gritti
 *  Copyright (C) 2002 Andreas J. Guelzow
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

#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include "gnumeric.h"
#include "charset.h"

LGroupInfo lgroups[] = {
	{N_("Arabic"),0},
	{N_("Baltic"),0},
	{N_("Central European"),0},
	{N_("Chinese"),0},
	{N_("Cyrillic"),0},
	{N_("Greek"),0},
	{N_("Hebrew"),0},
	{N_("Indian"),0},
	{N_("Japanese"),0},
	{N_("Korean"),0},
	{N_("Turkish"),0},
	{N_("Unicode"),1},
	{N_("Vietnamese"),0},
	{N_("Western"),0},
	{N_("Other"),0},
	{NULL, 0}
};

CharsetInfo const charset_trans_array[] = { 
	{N_("IBM-864"),                           "IBM864",                LG_ARABIC},
	{N_("IBM-864-I"),                         "IBM864i",               LG_ARABIC},
	{N_("ISO-8859-6"),                        "ISO-8859-6",            LG_ARABIC},
	{N_("ISO-8859-6-E"),                      "ISO-8859-6-E",          LG_ARABIC},
	{N_("ISO-8859-6-I"),                      "ISO-8859-6-I",          LG_ARABIC},
	{N_("MacArabic"),                         "x-mac-arabic",          LG_ARABIC},
	{N_("Windows-1256"),                      "windows-1256",          LG_ARABIC},
	{N_("Armenian (ARMSCII-8)"),              "armscii-8", 	           LG_OTHER},
	{N_("ISO-8859-13"),                       "ISO-8859-13",           LG_BALTIC},
	{N_("ISO-8859-4"),                        "ISO-8859-4",            LG_BALTIC},
	{N_("Windows-1257"),                      "windows-1257",          LG_BALTIC},
	{N_("Celtic (ISO-8859-14)"),              "ISO-8859-14",           LG_OTHER},
	{N_("IBM-852"),                           "IBM852",                LG_CENTRAL_EUROPEAN},
	{N_("ISO-8859-2"),                        "ISO-8859-2",	           LG_CENTRAL_EUROPEAN},
	{N_("MacCE"),                             "x-mac-ce",              LG_CENTRAL_EUROPEAN},
	{N_("Windows-1250"),                      "windows-1250",          LG_CENTRAL_EUROPEAN},
	{N_("Simplified (GB18030)"),              "gb18030",               LG_CHINESE},
	{N_("Simplified (GB2312)"),               "GB2312",                LG_CHINESE},
	{N_("Simplified (GBK)"),                  "x-gbk",                 LG_CHINESE},
	{N_("Simplified (HZ)"),                   "HZ-GB-2312",	           LG_CHINESE},
	{N_("Simplified (Windows-936)"),          "windows-936",           LG_CHINESE},
	{N_("Traditional (Big5)"),                "Big5",                  LG_CHINESE},
	{N_("Traditional (Big5-HKSCS)"),          "Big5-HKSCS",	           LG_CHINESE},
	{N_("Traditional (EUC-TW)"),              "x-euc-tw",              LG_CHINESE},
	{N_("Croatian (MacCroatian)"),            "x-mac-croatian",        LG_CENTRAL_EUROPEAN},
	{N_("IBM-855"),                           "IBM855",                LG_CYRILLIC},
	{N_("ISO-8859-5"),                        "ISO-8859-5",	           LG_CYRILLIC},
	{N_("ISO-IR-111"),                        "ISO-IR-111",	           LG_CYRILLIC},
	{N_("KOI8-R"),                            "KOI8-R",                LG_CYRILLIC},
	{N_("MacCyrillic"),                       "x-mac-cyrillic",        LG_CYRILLIC},
	{N_("Windows-1251"),                      "windows-1251",          LG_CYRILLIC},
	{N_("Russian (CP-866)"),                  "IBM866",                LG_CYRILLIC},
	{N_("Ukrainian (KOI8-U)"),                "KOI8-U",                LG_CYRILLIC},
	{N_("Ukrainian (MacUkrainian)"),          "x-mac-ukrainian",       LG_CYRILLIC},
	{N_("English (US-ASCII)"),                "us-ascii",              LG_WESTERN},
	{N_("Farsi (MacFarsi)"),                  "x-mac-farsi",           LG_OTHER},
	{N_("Georgian (GEOSTD8)"),                "geostd8",               LG_OTHER},
	{N_("Greek (ISO-8859-7)"),                "ISO-8859-7",            LG_GREEK},
	{N_("Greek (MacGreek)"),                  "x-mac-greek",           LG_GREEK},
	{N_("Greek (Windows-1253)"),              "windows-1253",          LG_GREEK},
	{N_("Gujarati (MacGujarati)"),            "x-mac-gujarati",        LG_INDIAN},
	{N_("Gurmukhi (MacGurmukhi)"),            "x-mac-gurmukhi",        LG_INDIAN},
	{N_("IBM-862"),                           "IBM862",                LG_HEBREW},
	{N_("ISO-8859-8-E"),                      "ISO-8859-8-E",          LG_HEBREW},
	{N_("ISO-8859-8-I"),                      "ISO-8859-8-I",          LG_HEBREW},
	{N_("MacHebrew"),                         "x-mac-hebrew",          LG_HEBREW},
	{N_("Windows-1255"),                      "windows-1255",          LG_HEBREW},
	{N_("Hindi (MacDevanagari)"),             "x-mac-devanagari",      LG_INDIAN},
	{N_("Icelandic (MacIcelandic)"),          "x-mac-icelandic",       LG_OTHER},
	{N_("EUC-JP"),                            "EUC-JP",                LG_JAPANESE},
	{N_("ISO-2022-JP"),                       "ISO-2022-JP",           LG_JAPANESE},
	{N_("Shift_JIS"),                         "Shift_JIS",             LG_JAPANESE},
	{N_("EUC-KR"),                            "EUC-KR",                LG_KOREAN},
	{N_("ISO-2022-KR"),                       "ISO-2022-KR",           LG_KOREAN},
	{N_("JOHAB"),                             "x-johab",               LG_KOREAN},
	{N_("UHC"),                               "x-windows-949",         LG_KOREAN},
	{N_("Nordic (ISO-8859-10)"),              "ISO-8859-10",           LG_OTHER},
	{N_("Romanian (MacRomanian)"),            "x-mac-romanian",        LG_OTHER},
	{N_("Romanian (ISO-8859-16)"),            "ISO-8859-16",           LG_OTHER},
	{N_("South European (ISO-8859-3)"),       "ISO-8859-3",            LG_OTHER},
	{N_("Thai (TIS-620)"),                    "TIS-620",               LG_OTHER},
	{N_("IBM-857"),                           "IBM857",                LG_TURKISH},
	{N_("ISO-8859-9"),                        "ISO-8859-9",            LG_TURKISH},
	{N_("MacTurkish"),                        "x-mac-turkish",         LG_TURKISH},
	{N_("Windows-1254"),                      "windows-1254",          LG_TURKISH},
	{N_("UTF-7"),                             "UTF-7",                 LG_UNICODE},
	{N_("UTF-8"),                             "UTF-8",                 LG_UNICODE},
	{N_("UTF-16BE"),                          "UTF-16BE",              LG_UNICODE},
	{N_("UTF-16LE"),                          "UTF-16LE",              LG_UNICODE},
	{N_("UTF-32BE"),                          "UTF-32BE",              LG_UNICODE},
	{N_("UTF-32LE"),                          "UTF-32LE",              LG_UNICODE},
	{N_("User Defined"),                      "x-user-defined",        LG_OTHER},
	{N_("TCVN"),                              "x-viet-tcvn5712",       LG_VIETNAMESE},
	{N_("VISCII"),                            "VISCII",                LG_VIETNAMESE},
	{N_("VPS"),                               "x-viet-vps",            LG_VIETNAMESE},
	{N_("Windows-1258"),                      "windows-1258",          LG_VIETNAMESE},
	{N_("Visual (ISO-8859-8)"),               "ISO-8859-8",            LG_HEBREW},
	{N_("Western (IBM-850)"),                 "IBM850",                LG_WESTERN},
	{N_("Western (ISO-8859-1)"),              "ISO-8859-1",            LG_WESTERN},
	{N_("Western (ISO-8859-15)"),             "ISO-8859-15",           LG_WESTERN},
	{N_("Western (MacRoman)"),                "x-mac-roman",           LG_WESTERN},
	{N_("Western (Windows-1252)"),            "windows-1252",          LG_WESTERN},
	/* charsets whithout posibly translatable names */
	{"T61.8bit",                              "T61.8bit",              LG_OTHER},
	{"x-imap4-modified-utf7",                 "x-imap4-modified-utf7", LG_UNICODE},
	{"x-u-escaped",                           "x-u-escaped",           LG_OTHER},
	{NULL,                                    NULL,                    LG_LAST}
};

static void set_encodings_menu (CharmapChooser *data)
{
        unsigned int lgroup;
	GtkWidget *item;
	CharsetInfo const *charset_trans;
	GtkMenu *menu;

	g_return_if_fail (data != NULL);
	g_return_if_fail (data->encoding_classes != NULL);
	g_return_if_fail (data->encodings != NULL);

	lgroup = gtk_option_menu_get_history (data->encoding_classes);
	if (data->last_encoding_class == lgroup) return;
	data->last_encoding_class = lgroup;
	menu = data->encodings_menu;
	if (menu) gtk_option_menu_remove_menu (data->encodings);
	menu = GTK_MENU (gtk_menu_new ());
	gtk_option_menu_set_menu (data->encodings, GTK_WIDGET (menu));
	data->encodings_menu = menu;
	charset_trans = charset_trans_array;
	
	if (lgroup < LG_LAST) {
		while (charset_trans->lgroup != LG_LAST) {
			if (charset_trans->lgroup == lgroup) {
				item = gtk_menu_item_new_with_label (_(charset_trans->charset_title));
				gtk_widget_show (item);
				gtk_menu_append (menu, item);
			}
			charset_trans++;
		}
		gtk_option_menu_set_history (data->encodings, 
					     lgroups[lgroup].preferred_encoding);
	} else {
		char const *locale_encoding;
		
		g_get_charset (&locale_encoding);
		while (charset_trans->lgroup != LG_LAST) {
			if (0 == g_ascii_strcasecmp(charset_trans->charset_name, locale_encoding)) {
				item = gtk_menu_item_new_with_label (_(charset_trans->charset_title));
				gtk_widget_show (item);
				gtk_menu_append (menu, item);
				break;
			}
			charset_trans++;
		}
		gtk_option_menu_set_history (data->encodings, 0);
	}
}

static void
encoding_class_changed_cb (GtkOptionMenu *optionmenu,
			   CharmapChooser *data)
{
	g_return_if_fail (optionmenu == data->encoding_classes);

	set_encodings_menu (data);
}

static void 
set_menu_to_default (CharmapChooser *data)
{
	gtk_option_menu_set_history (data->encoding_classes, LG_LAST + 1);
	set_encodings_menu (data);
	gtk_option_menu_set_history (data->encodings, 0);	
}


GtkWidget *
make_charmap_chooser (CharmapChooser *data)
{
	g_return_val_if_fail (data != NULL, NULL);
	
        GtkWidget *item;
	GtkMenu *menu;
	LGroupInfo *lgroup = lgroups;
	GtkWidget *box;


	data->encoding_classes = GTK_OPTION_MENU(gtk_option_menu_new());

        menu = GTK_MENU (gtk_menu_new ());

	while (lgroup->group_name) {
		item = gtk_menu_item_new_with_label (_(lgroup->group_name));
                gtk_widget_show (item);
		gtk_menu_append (menu, item);
                lgroup++;
        }
	item = gtk_separator_menu_item_new ();
	gtk_widget_show (item);
	gtk_menu_append (menu, item);
	item = gtk_menu_item_new_with_label (_("Locale"));
	gtk_widget_show (item);
	gtk_menu_append (menu, item);

	gtk_option_menu_set_menu (data->encoding_classes, GTK_WIDGET (menu));
	data->encoding_classes_menu = menu;

	data->encodings = GTK_OPTION_MENU(gtk_option_menu_new());
	data->encodings_menu = NULL;
	data->last_encoding_class = LG_LAST;
	
	g_signal_connect (G_OBJECT (data->encoding_classes), "changed", 
			  G_CALLBACK (encoding_class_changed_cb), data);

	set_menu_to_default (data);

	box = gtk_hbox_new(FALSE, 1);
	gtk_box_pack_start (GTK_BOX(box), GTK_WIDGET(data->encoding_classes), TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX(box), GTK_WIDGET(data->encodings), TRUE, TRUE, 0);

	return (box);
}

gchar const *
charmap_chooser_get_selected_encoding (CharmapChooser *data)
{
	gint selection, cnt = 0;
	unsigned int lgroup;
	CharsetInfo const *charset_trans;
	char const *locale_encoding;
	
	g_get_charset (&locale_encoding);

	g_return_val_if_fail (data != NULL, locale_encoding);
	
	selection = gtk_option_menu_get_history (data->encodings);
	lgroup = gtk_option_menu_get_history (data->encoding_classes);
	if (lgroup >= LG_LAST) 
		return locale_encoding;
	
	charset_trans = charset_trans_array;
	while (charset_trans->lgroup != LG_LAST) {
                if (charset_trans->lgroup == lgroup) {
			if (cnt == selection)
				return charset_trans->charset_name;
			cnt++;
                }
                charset_trans++;
        }
	g_warning ("Locale not found, using %s", locale_encoding);
	return locale_encoding;
}

void        
charmap_chooser_set_sensitive (CharmapChooser *data, gboolean sensitive)
{
	g_return_if_fail (data != NULL);
	
	gtk_widget_set_sensitive (GTK_WIDGET(data->encoding_classes), sensitive);
	gtk_widget_set_sensitive (GTK_WIDGET(data->encodings), sensitive);
}



