/*
 * A charmap selector widget.  
 *
 *  Copyright (C) 2003 Andreas J. Guelzow
 *
 *  based on code by:
 *  Copyright (C) 2000 Marco Pesenti Gritti
 *  from the galeon code base
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

#include <gnumeric-i18n.h>
#include <gnumeric.h>
#include "widget-charmap-selector.h"
#include "gnumeric-optionmenu.h"
#include <gsf/gsf-impl-utils.h>

#define CS(x) CHARMAP_SELECTOR(x)

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
        char const *group_name;
        gint preferred_encoding;
	LanguageGroup lgroup;
}
LGroupInfo;


LGroupInfo lgroups[] = {
	{N_("Arabic"),0, LG_ARABIC},
	{N_("Baltic"),0, LG_BALTIC},
	{N_("Central European"),0, LG_CENTRAL_EUROPEAN},
	{N_("Chinese"),0, LG_CHINESE},
	{N_("Cyrillic"),0, LG_CYRILLIC},
	{N_("Greek"),0, LG_GREEK},
	{N_("Hebrew"),0, LG_HEBREW},
	{N_("Indian"),0, LG_INDIAN},
	{N_("Japanese"),0, LG_JAPANESE},
	{N_("Korean"),0, LG_KOREAN},
	{N_("Turkish"),0, LG_TURKISH},
	{N_("Unicode"),1, LG_UNICODE},
	{N_("Vietnamese"),0, LG_VIETNAMESE},
	{N_("Western"),0, LG_WESTERN},
	{N_("Other"), 0, LG_OTHER},
	{NULL, 0, LG_LAST}
};

CharsetInfo const charset_trans_array[] = { 
	{N_("Arabic (IBM-864)"),                  "IBM864",                LG_ARABIC},
	{N_("Arabic (IBM-864-I)"),                "IBM864i",               LG_ARABIC},
	{N_("Arabic (ISO-8859-6)"),               "ISO-8859-6",            LG_ARABIC},
	{N_("Arabic (ISO-8859-6-E)"),             "ISO-8859-6-E",          LG_ARABIC},
	{N_("Arabic (ISO-8859-6-I)"),             "ISO-8859-6-I",          LG_ARABIC},
	{N_("Arabic (MacArabic)"),                "x-mac-arabic",          LG_ARABIC},
	{N_("Arabic (Windows-1256)"),             "windows-1256",          LG_ARABIC},
	{N_("Armenian (ARMSCII-8)"),              "armscii-8", 	           LG_OTHER},
	{N_("Baltic (ISO-8859-13)"),              "ISO-8859-13",           LG_BALTIC},
	{N_("Baltic (ISO-8859-4)"),               "ISO-8859-4",            LG_BALTIC},
	{N_("Baltic (Windows-1257)"),             "windows-1257",          LG_BALTIC},
	{N_("Celtic (ISO-8859-14)"),              "ISO-8859-14",           LG_OTHER},
	{N_("Central European (IBM-852)"),        "IBM852",                LG_CENTRAL_EUROPEAN},
	{N_("Central European (ISO-8859-2)"),     "ISO-8859-2",	           LG_CENTRAL_EUROPEAN},
	{N_("Central European (MacCE)"),          "x-mac-ce",              LG_CENTRAL_EUROPEAN},
	{N_("Central European (Windows-1250)"),   "windows-1250",          LG_CENTRAL_EUROPEAN},
	{N_("Chinese Simplified (GB18030)"),      "gb18030",               LG_CHINESE},
	{N_("Chinese Simplified (GB2312)"),       "GB2312",                LG_CHINESE},
	{N_("Chinese Simplified (GBK)"),          "x-gbk",                 LG_CHINESE},
	{N_("Chinese Simplified (HZ)"),           "HZ-GB-2312",	           LG_CHINESE},
	{N_("Chinese Simplified (Windows-936)"),  "windows-936",           LG_CHINESE},
	{N_("Chinese Traditional (Big5)"),        "Big5",                  LG_CHINESE},
	{N_("Chinese Traditional (Big5-HKSCS)"),  "Big5-HKSCS",	           LG_CHINESE},
	{N_("Chinese Traditional (EUC-TW)"),      "x-euc-tw",              LG_CHINESE},
	{N_("Croatian (MacCroatian)"),            "x-mac-croatian",        LG_CENTRAL_EUROPEAN},
	{N_("Cyrillic (IBM-855)"),                "IBM855",                LG_CYRILLIC},
	{N_("Cyrillic (ISO-8859-5)"),             "ISO-8859-5",	           LG_CYRILLIC},
	{N_("Cyrillic (ISO-IR-111)"),             "ISO-IR-111",	           LG_CYRILLIC},
	{N_("Cyrillic (KOI8-R)"),                 "KOI8-R",                LG_CYRILLIC},
	{N_("Cyrillic (MacCyrillic)"),            "x-mac-cyrillic",        LG_CYRILLIC},
	{N_("Cyrillic (Windows-1251)"),           "windows-1251",          LG_CYRILLIC},
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
	{N_("Hebrew (IBM-862)"),                  "IBM862",                LG_HEBREW},
	{N_("Hebrew (ISO-8859-8-E)"),             "ISO-8859-8-E",          LG_HEBREW},
	{N_("Hebrew (ISO-8859-8-I)"),             "ISO-8859-8-I",          LG_HEBREW},
	{N_("Hebrew (MacHebrew)"),                "x-mac-hebrew",          LG_HEBREW},
	{N_("Hebrew (Windows-1255)"),             "windows-1255",          LG_HEBREW},
	{N_("Hindi (MacDevanagari)"),             "x-mac-devanagari",      LG_INDIAN},
	{N_("Icelandic (MacIcelandic)"),          "x-mac-icelandic",       LG_OTHER},
	{N_("Japanese (EUC-JP)"),                 "EUC-JP",                LG_JAPANESE},
	{N_("Japanese (ISO-2022-JP)"),            "ISO-2022-JP",           LG_JAPANESE},
	{N_("Japanese (Shift_JIS)"),              "Shift_JIS",             LG_JAPANESE},
	{N_("Korean (EUC-KR)"),                   "EUC-KR",                LG_KOREAN},
	{N_("Korean (ISO-2022-KR)"),              "ISO-2022-KR",           LG_KOREAN},
	{N_("Korean (JOHAB)"),                    "x-johab",               LG_KOREAN},
	{N_("Korean (UHC)"),                      "x-windows-949",         LG_KOREAN},
	{N_("Nordic (ISO-8859-10)"),              "ISO-8859-10",           LG_OTHER},
	{N_("Romanian (MacRomanian)"),            "x-mac-romanian",        LG_OTHER},
	{N_("Romanian (ISO-8859-16)"),            "ISO-8859-16",           LG_OTHER},
	{N_("South European (ISO-8859-3)"),       "ISO-8859-3",            LG_OTHER},
	{N_("Thai (TIS-620)"),                    "TIS-620",               LG_OTHER},
	{N_("Turkisk (IBM-857)"),                 "IBM857",                LG_TURKISH},
	{N_("Turkisk (ISO-8859-9)"),              "ISO-8859-9",            LG_TURKISH},
	{N_("Turkisk (MacTurkish)"),              "x-mac-turkish",         LG_TURKISH},
	{N_("Turkisk (Windows-1254)"),            "windows-1254",          LG_TURKISH},
	{N_("Unicode (UTF-7)"),                   "UTF-7",                 LG_UNICODE},
	{N_("Unicode (UTF-8)"),                   "UTF-8",                 LG_UNICODE},
	{N_("Unicode (UTF-16BE)"),                "UTF-16BE",              LG_UNICODE},
	{N_("Unicode (UTF-16LE)"),                "UTF-16LE",              LG_UNICODE},
	{N_("Unicode (UTF-32BE)"),                "UTF-32BE",              LG_UNICODE},
	{N_("Unicode (UTF-32LE)"),                "UTF-32LE",              LG_UNICODE},
	{N_("User Defined"),                      "x-user-defined",        LG_OTHER},
	{N_("Vietnamese (TCVN)"),                 "x-viet-tcvn5712",       LG_VIETNAMESE},
	{N_("Vietnamese (VISCII)"),               "VISCII",                LG_VIETNAMESE},
	{N_("Vietnamese (VPS)"),                  "x-viet-vps",            LG_VIETNAMESE},
	{N_("Vietnamese (Windows-1258)"),         "windows-1258",          LG_VIETNAMESE},
	{N_("Visual Hebrew(ISO-8859-8)"),         "ISO-8859-8",            LG_HEBREW},
	{N_("Western (IBM-850)"),                 "IBM850",                LG_WESTERN},
	{N_("Western (ISO-8859-1)"),              "ISO-8859-1",            LG_WESTERN},
	{N_("Western (ISO-8859-15)"),             "ISO-8859-15",           LG_WESTERN},
	{N_("Western (MacRoman)"),                "x-mac-roman",           LG_WESTERN},
	{N_("Western (Windows-1252)"),            "windows-1252",          LG_WESTERN},
	/* charsets whithout possibly translatable names */
	{"T61.8bit",                              "T61.8bit",              LG_OTHER},
	{"x-imap4-modified-utf7",                 "x-imap4-modified-utf7", LG_UNICODE},
	{"x-u-escaped",                           "x-u-escaped",           LG_OTHER},
	{NULL,                                    NULL,                    LG_LAST}
};

struct _CharmapSelector {
	GtkHBox box;
	GnumericOptionMenu *encodings;
	GtkMenu *encodings_menu;
};

typedef struct {
	GtkHBoxClass parent_class;

	gboolean (* charmap_changed) (CharmapSelector *cs, char const *new_charmap);
} CharmapSelectorClass;


typedef CharmapSelector Cs;
typedef CharmapSelectorClass CsClass;

static GtkHBoxClass *cs_parent_class;

/* Signals we emit */
enum {
	CHARMAP_CHANGED,
	LAST_SIGNAL
};

static guint cs_signals[LAST_SIGNAL] = { 0 };

static char const *
get_locale_encoding_name (void) 
{
		char const *locale_encoding;
		CharsetInfo const *charset_trans = charset_trans_array;
		
		g_get_charset (&locale_encoding);
		while (charset_trans->lgroup != LG_LAST) {
			if (0 == g_ascii_strcasecmp
			    (charset_trans->charset_name, locale_encoding))
				return (_(charset_trans->charset_title));
			charset_trans++;
		}
		return locale_encoding;
}

static void
encodings_changed_cb (GnumericOptionMenu *optionmenu, CharmapSelector *cs)
{
	GSList * selection;
	
	g_return_if_fail (IS_CHARMAP_SELECTOR(cs));
	g_return_if_fail (optionmenu == cs->encodings);

 	selection = gnumeric_option_menu_get_history (cs->encodings);

 	if (GPOINTER_TO_INT (selection->data)  < LG_LAST && selection->next)
 		lgroups[GPOINTER_TO_INT (selection->data)].preferred_encoding = GPOINTER_TO_INT (selection->next->data);

	gtk_signal_emit (GTK_OBJECT (cs),
			 cs_signals[CHARMAP_CHANGED], 
			 charmap_selector_get_encoding (cs));
}

static void 
set_menu_to_default (CharmapSelector *cs)
{
	GSList sel = { GINT_TO_POINTER (LG_LAST + 1), NULL};
	
	g_return_if_fail (IS_CHARMAP_SELECTOR(cs));
	
	gnumeric_option_menu_set_history (cs->encodings, &sel);
}


static void
cs_init (CharmapSelector *cs)
{
        GtkWidget *item;
	GtkMenu *menu;
	LGroupInfo *lgroup = lgroups;

	cs->encodings = GNUMERIC_OPTION_MENU(gnumeric_option_menu_new());
        menu = GTK_MENU (gtk_menu_new ());

	while (lgroup->group_name) {
		CharsetInfo const *charset_trans;
		GtkMenu *submenu;
		
		item = gtk_menu_item_new_with_label (_(lgroup->group_name));
		
		submenu = GTK_MENU (gtk_menu_new ());
		charset_trans = charset_trans_array;

		while (charset_trans->lgroup != LG_LAST) {
			GtkWidget *subitem;
			if (charset_trans->lgroup == lgroup->lgroup) {
				subitem = gtk_menu_item_new_with_label 
					(_(charset_trans->charset_title));
				gtk_widget_show (subitem);
				gtk_menu_append (submenu, subitem);
			}
			charset_trans++;
		}

		gtk_menu_item_set_submenu (GTK_MENU_ITEM(item), GTK_WIDGET(submenu));
                gtk_widget_show (item);
		gtk_menu_append (menu, item);
                lgroup++;
        }
	item = gtk_separator_menu_item_new ();
	gtk_widget_show (item);
	gtk_menu_append (menu, item);
	
	{
		char *locale_encoding_menu_title = g_strconcat (_("Locale: "), 
							      get_locale_encoding_name (), 
							      NULL);
		
		item = gtk_menu_item_new_with_label (locale_encoding_menu_title);
		g_free (locale_encoding_menu_title);
		gtk_widget_show (item);
		gtk_menu_append (menu, item);
	}
	
	gnumeric_option_menu_set_menu (cs->encodings, GTK_WIDGET (menu));
	cs->encodings_menu = menu;

	g_signal_connect (G_OBJECT (cs->encodings), "changed", 
			  G_CALLBACK (encodings_changed_cb), cs);

	set_menu_to_default (cs);

	gtk_box_pack_start (GTK_BOX(cs), GTK_WIDGET(cs->encodings), 
			    TRUE, TRUE, 0);
}

static void
cs_destroy (GtkObject *object)
{
/* 	CharmapSelector *cs = CHARMAP_SELECTOR (object); */

	((GtkObjectClass *)cs_parent_class)->destroy (object);
}

static void
cs_class_init (GtkObjectClass *klass)
{
	klass->destroy = cs_destroy;

	cs_parent_class = g_type_class_peek (gtk_hbox_get_type ());

	cs_signals[CHARMAP_CHANGED] =
		gtk_signal_new (
			"charmap_changed",
			GTK_RUN_LAST,
			GTK_CLASS_TYPE (klass),
			GTK_SIGNAL_OFFSET (CharmapSelectorClass, 
					   charmap_changed),
			gtk_marshal_NONE__POINTER,
			GTK_TYPE_NONE, 1, GTK_TYPE_POINTER);
}

GSF_CLASS (CharmapSelector, charmap_selector,
	   cs_class_init, cs_init, GTK_TYPE_HBOX)

GtkWidget *
charmap_selector_new (void)
{
	GtkWidget *w;

	w = gtk_type_new (CHARMAP_SELECTOR_TYPE);
	return w;
}


gchar const *
charmap_selector_get_encoding     (CharmapSelector *cs)
{
	GSList *selection;
	gint cnt = 0;
	unsigned int lgroup;
	CharsetInfo const *charset_trans;
	char const *locale_encoding;
	
	g_get_charset (&locale_encoding);

 	g_return_val_if_fail (IS_CHARMAP_SELECTOR(cs), locale_encoding);
	
 	selection = gnumeric_option_menu_get_history (cs->encodings);
 	lgroup = GPOINTER_TO_INT (selection->data);
	
	if (lgroup >= LG_LAST || !(selection->next))
 		return locale_encoding;
	
 	charset_trans = charset_trans_array;
 	while (charset_trans->lgroup != LG_LAST) {
		if (charset_trans->lgroup == lgroup) {
 			if (cnt == GPOINTER_TO_INT (selection->next->data))
 				return charset_trans->charset_name;
			cnt++;
		}
		charset_trans++;
	}
 	g_warning ("Locale not found, using %s", locale_encoding);
	return locale_encoding;
}

void 
charmap_selector_set_sensitive (CharmapSelector *cs, gboolean sensitive)
{
	g_return_if_fail (IS_CHARMAP_SELECTOR(cs));
	
	gtk_widget_set_sensitive (GTK_WIDGET(cs->encodings), sensitive);
}








