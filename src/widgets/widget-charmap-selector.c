/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
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

#define CHARMAP_NAME_KEY "Name of Character Encoding"

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

typedef enum {
     CI_MINOR,
     CI_MAJOR
} CharsetImportance;



typedef struct {
     gchar const *charset_title;
     gchar const *charset_name;
     LanguageGroup const lgroup;
     CharsetImportance const imp;
} CharsetInfo;

typedef struct
{
        char const *group_name;
	LanguageGroup const lgroup;
}
LGroupInfo;


static LGroupInfo const lgroups[] = {
	{N_("Arabic"), LG_ARABIC},
	{N_("Baltic"), LG_BALTIC},
	{N_("Central European"), LG_CENTRAL_EUROPEAN},
	{N_("Chinese"), LG_CHINESE},
	{N_("Cyrillic"), LG_CYRILLIC},
	{N_("Greek"), LG_GREEK},
	{N_("Hebrew"), LG_HEBREW},
	{N_("Indian"), LG_INDIAN},
	{N_("Japanese"), LG_JAPANESE},
	{N_("Korean"), LG_KOREAN},
	{N_("Turkish"), LG_TURKISH},
	{N_("Unicode"), LG_UNICODE},
	{N_("Vietnamese"), LG_VIETNAMESE},
	{N_("Western"), LG_WESTERN},
	{N_("Other"), LG_OTHER},
	{NULL, LG_LAST}
};

static CharsetInfo const charset_trans_array[] = { 
	{N_("Arabic (IBM-864)"),                  "IBM864",                LG_ARABIC, CI_MINOR},
	{N_("Arabic (IBM-864-I)"),                "IBM864i",               LG_ARABIC, CI_MINOR},
	{N_("Arabic (ISO-8859-6)"),               "ISO-8859-6",            LG_ARABIC, CI_MINOR},
	{N_("Arabic (ISO-8859-6-E)"),             "ISO-8859-6-E",          LG_ARABIC, CI_MINOR},

	{N_("Arabic (ISO-8859-6-I)"),             "ISO-8859-6-I",          LG_ARABIC, CI_MINOR},
	{N_("Arabic (MacArabic)"),                "x-mac-arabic",          LG_ARABIC, CI_MINOR},
	{N_("Arabic (Windows-1256)"),             "windows-1256",          LG_ARABIC, CI_MINOR},
	{N_("Armenian (ARMSCII-8)"),              "armscii-8", 	           LG_OTHER, CI_MINOR},
	{N_("Baltic (ISO-8859-13)"),              "ISO-8859-13",           LG_BALTIC, CI_MINOR},
	{N_("Baltic (ISO-8859-4)"),               "ISO-8859-4",            LG_BALTIC, CI_MINOR},
	{N_("Baltic (Windows-1257)"),             "windows-1257",          LG_BALTIC, CI_MINOR},
	{N_("Celtic (ISO-8859-14)"),              "ISO-8859-14",           LG_OTHER, CI_MINOR},
	{N_("Central European (IBM-852)"),        "IBM852",                LG_CENTRAL_EUROPEAN, CI_MINOR},
	{N_("Central European (ISO-8859-2)"),     "ISO-8859-2",	           LG_CENTRAL_EUROPEAN, CI_MINOR},
	{N_("Central European (MacCE)"),          "x-mac-ce",              LG_CENTRAL_EUROPEAN, CI_MINOR},
	{N_("Central European (Windows-1250)"),   "windows-1250",          LG_CENTRAL_EUROPEAN, CI_MINOR},
	{N_("Chinese Simplified (GB18030)"),      "gb18030",               LG_CHINESE, CI_MINOR},
	{N_("Chinese Simplified (GB2312)"),       "GB2312",                LG_CHINESE, CI_MINOR},
	{N_("Chinese Simplified (GBK)"),          "x-gbk",                 LG_CHINESE, CI_MINOR},
	{N_("Chinese Simplified (HZ)"),           "HZ-GB-2312",	           LG_CHINESE, CI_MINOR},
	{N_("Chinese Simplified (Windows-936)"),  "windows-936",           LG_CHINESE, CI_MINOR},
	{N_("Chinese Traditional (Big5)"),        "Big5",                  LG_CHINESE, CI_MINOR},
	{N_("Chinese Traditional (Big5-HKSCS)"),  "Big5-HKSCS",	           LG_CHINESE, CI_MINOR},
	{N_("Chinese Traditional (EUC-TW)"),      "x-euc-tw",              LG_CHINESE, CI_MINOR},
	{N_("Croatian (MacCroatian)"),            "x-mac-croatian",        LG_CENTRAL_EUROPEAN, CI_MINOR},
	{N_("Cyrillic (IBM-855)"),                "IBM855",                LG_CYRILLIC, CI_MINOR},
	{N_("Cyrillic (ISO-8859-5)"),             "ISO-8859-5",	           LG_CYRILLIC, CI_MINOR},
	{N_("Cyrillic (ISO-IR-111)"),             "ISO-IR-111",	           LG_CYRILLIC, CI_MINOR},
	{N_("Cyrillic (KOI8-R)"),                 "KOI8-R",                LG_CYRILLIC, CI_MINOR},
	{N_("Cyrillic (MacCyrillic)"),            "x-mac-cyrillic",        LG_CYRILLIC, CI_MINOR},
	{N_("Cyrillic (Windows-1251)"),           "windows-1251",          LG_CYRILLIC, CI_MINOR},
	{N_("Russian (CP-866)"),                  "IBM866",                LG_CYRILLIC, CI_MINOR},
	{N_("Ukrainian (KOI8-U)"),                "KOI8-U",                LG_CYRILLIC, CI_MINOR},
	{N_("Ukrainian (MacUkrainian)"),          "x-mac-ukrainian",       LG_CYRILLIC, CI_MINOR},
	{N_("English (ASCII)"),                   "ANSI_X3.4-1968",        LG_WESTERN, CI_MAJOR},
	{N_("Farsi (MacFarsi)"),                  "x-mac-farsi",           LG_OTHER, CI_MINOR},
	{N_("Georgian (GEOSTD8)"),                "geostd8",               LG_OTHER, CI_MINOR},
	{N_("Greek (ISO-8859-7)"),                "ISO-8859-7",            LG_GREEK, CI_MINOR},
	{N_("Greek (MacGreek)"),                  "x-mac-greek",           LG_GREEK, CI_MINOR},
	{N_("Greek (Windows-1253)"),              "windows-1253",          LG_GREEK, CI_MINOR},
	{N_("Gujarati (MacGujarati)"),            "x-mac-gujarati",        LG_INDIAN, CI_MINOR},
	{N_("Gurmukhi (MacGurmukhi)"),            "x-mac-gurmukhi",        LG_INDIAN, CI_MINOR},
	{N_("Hebrew (IBM-862)"),                  "IBM862",                LG_HEBREW, CI_MINOR},
	{N_("Hebrew (ISO-8859-8-E)"),             "ISO-8859-8-E",          LG_HEBREW, CI_MINOR},
	{N_("Hebrew (ISO-8859-8-I)"),             "ISO-8859-8-I",          LG_HEBREW, CI_MINOR},
	{N_("Hebrew (MacHebrew)"),                "x-mac-hebrew",          LG_HEBREW, CI_MINOR},
	{N_("Hebrew (Windows-1255)"),             "windows-1255",          LG_HEBREW, CI_MINOR},
	{N_("Hindi (MacDevanagari)"),             "x-mac-devanagari",      LG_INDIAN, CI_MINOR},
	{N_("Icelandic (MacIcelandic)"),          "x-mac-icelandic",       LG_OTHER, CI_MINOR},
	{N_("Japanese (EUC-JP)"),                 "EUC-JP",                LG_JAPANESE, CI_MINOR},
	{N_("Japanese (ISO-2022-JP)"),            "ISO-2022-JP",           LG_JAPANESE, CI_MINOR},
	{N_("Japanese (Shift_JIS)"),              "Shift_JIS",             LG_JAPANESE, CI_MINOR},
	{N_("Korean (EUC-KR)"),                   "EUC-KR",                LG_KOREAN, CI_MINOR},
	{N_("Korean (ISO-2022-KR)"),              "ISO-2022-KR",           LG_KOREAN, CI_MINOR},
	{N_("Korean (JOHAB)"),                    "x-johab",               LG_KOREAN, CI_MINOR},
	{N_("Korean (UHC)"),                      "x-windows-949",         LG_KOREAN, CI_MINOR},
	{N_("Nordic (ISO-8859-10)"),              "ISO-8859-10",           LG_OTHER, CI_MINOR},
	{N_("Romanian (MacRomanian)"),            "x-mac-romanian",        LG_OTHER, CI_MINOR},
	{N_("Romanian (ISO-8859-16)"),            "ISO-8859-16",           LG_OTHER, CI_MINOR},
	{N_("South European (ISO-8859-3)"),       "ISO-8859-3",            LG_OTHER, CI_MINOR},
	{N_("Thai (TIS-620)"),                    "TIS-620",               LG_OTHER, CI_MINOR},
	{N_("Turkish (IBM-857)"),                 "IBM857",                LG_TURKISH, CI_MINOR},
	{N_("Turkish (ISO-8859-9)"),              "ISO-8859-9",            LG_TURKISH, CI_MINOR},
	{N_("Turkish (MacTurkish)"),              "x-mac-turkish",         LG_TURKISH, CI_MINOR},
	{N_("Turkish (Windows-1254)"),            "windows-1254",          LG_TURKISH, CI_MINOR},
	{N_("Unicode (UTF-7)"),                   "UTF-7",                 LG_UNICODE, CI_MINOR},
	{N_("Unicode (UTF-8)"),                   "UTF-8",                 LG_UNICODE, CI_MAJOR},
	/* Test encoding that surely does not exist.  */
	/* {"Unicode (UTF-9)",                    "UTF-9",                 LG_UNICODE}, */
	{N_("Unicode (UTF-16BE)"),                "UTF-16BE",              LG_UNICODE, CI_MINOR},
	{N_("Unicode (UTF-16LE)"),                "UTF-16LE",              LG_UNICODE, CI_MINOR},
	{N_("Unicode (UTF-32BE)"),                "UTF-32BE",              LG_UNICODE, CI_MINOR},
	{N_("Unicode (UTF-32LE)"),                "UTF-32LE",              LG_UNICODE, CI_MINOR},
	{N_("User Defined"),                      "x-user-defined",        LG_OTHER, CI_MINOR},
	{N_("Vietnamese (TCVN)"),                 "x-viet-tcvn5712",       LG_VIETNAMESE, CI_MINOR},
	{N_("Vietnamese (VISCII)"),               "VISCII",                LG_VIETNAMESE, CI_MINOR},
	{N_("Vietnamese (VPS)"),                  "x-viet-vps",            LG_VIETNAMESE, CI_MINOR},
	{N_("Vietnamese (Windows-1258)"),         "windows-1258",          LG_VIETNAMESE, CI_MINOR},
	{N_("Visual Hebrew (ISO-8859-8)"),        "ISO-8859-8",            LG_HEBREW, CI_MINOR},
	{N_("Western (IBM-850)"),                 "IBM850",                LG_WESTERN, CI_MINOR},
	{N_("Western (ISO-8859-1)"),              "ISO-8859-1",            LG_WESTERN, CI_MAJOR},
	{N_("Western (ISO-8859-15)"),             "ISO-8859-15",           LG_WESTERN, CI_MINOR},
	{N_("Western (MacRoman)"),                "x-mac-roman",           LG_WESTERN, CI_MINOR},
	{N_("Western (Windows-1252)"),            "windows-1252",          LG_WESTERN, CI_MINOR},
	/* charsets whithout possibly translatable names */
	{"T61.8bit",                              "T61.8bit",              LG_OTHER, CI_MINOR},
	{"x-imap4-modified-utf7",                 "x-imap4-modified-utf7", LG_UNICODE, CI_MINOR},
	{"x-u-escaped",                           "x-u-escaped",           LG_OTHER, CI_MINOR},
	{NULL,                                    NULL,                    LG_LAST, 0}
};

struct _CharmapSelector {
	GtkHBox box;
	GnumericOptionMenu *encodings;
	GtkMenu *encodings_menu;
	CharmapSelectorTestDirection test;
};

typedef struct {
	GtkHBoxClass parent_class;

	gboolean (* charmap_changed) (CharmapSelector *cs, char const *new_charmap);
} CharmapSelectorClass;


typedef CharmapSelector Cs;
typedef CharmapSelectorClass CsClass;

/* Signals we emit */
enum {
	CHARMAP_CHANGED,
	LAST_SIGNAL
};

enum {
	PROP_0,
	PROP_TEST_DIRECTION
};




static guint cs_signals[LAST_SIGNAL] = { 0 };

static void cs_set_property      (GObject          *object,
				  guint             prop_id,
				  const GValue     *value,
				  GParamSpec       *pspec);

static void cs_get_property      (GObject          *object,
				  guint             prop_id,
				  GValue           *value,
				  GParamSpec       *pspec);


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
	g_return_if_fail (IS_CHARMAP_SELECTOR(cs));
	g_return_if_fail (optionmenu == cs->encodings);

	gtk_signal_emit (GTK_OBJECT (cs),
			 cs_signals[CHARMAP_CHANGED], 
			 charmap_selector_get_encoding (cs));
}

static void 
set_menu_to_default (CharmapSelector *cs, gint item)
{
	GSList sel = { GINT_TO_POINTER (item - 1), NULL};
	
	g_return_if_fail (cs != NULL && IS_CHARMAP_SELECTOR(cs));
	
	gnumeric_option_menu_set_history (cs->encodings, &sel);
}

static gboolean
cs_mnemonic_activate (GtkWidget *w, gboolean group_cycling)
{
	CharmapSelector *cs = CHARMAP_SELECTOR (w);
	gtk_widget_grab_focus (GTK_WIDGET (cs->encodings));
	return TRUE;
}

static void
cs_emphasize_label (GtkLabel *label)
{
	char *text = g_strconcat ("<b>", gtk_label_get_label (label), "</b>", NULL);
	
	gtk_label_set_use_underline (label, FALSE);
	gtk_label_set_use_markup (label, TRUE);
	gtk_label_set_label (label, text);
	g_free (text);
}

static void
cs_init (CharmapSelector *cs)
{
	cs->test = CHARMAP_SELECTOR_TO_UTF8;

	cs->encodings = GNUMERIC_OPTION_MENU(gnumeric_option_menu_new());
	
	g_signal_connect (G_OBJECT (cs->encodings), "changed",
                          G_CALLBACK (encodings_changed_cb), cs);
        gtk_box_pack_start (GTK_BOX(cs), GTK_WIDGET(cs->encodings),
                            TRUE, TRUE, 0);
}


static void
cs_build_menu (CharmapSelector *cs)
{
        GtkWidget *item;
	GtkMenu *menu;
	LGroupInfo const *lgroup = lgroups;
	gint lg_cnt = 0;
	
        menu = GTK_MENU (gtk_menu_new ());

	while (lgroup->group_name) {
		CharsetInfo const *charset_trans;
		GtkMenu *submenu;
		gint cnt = 0;
		
		item = gtk_menu_item_new_with_label (_(lgroup->group_name));
		
		submenu = GTK_MENU (gtk_menu_new ());
		charset_trans = charset_trans_array;
		
		while (charset_trans->lgroup != LG_LAST) {
			GtkWidget *subitem;
			if (charset_trans->lgroup == lgroup->lgroup) {
				/* Is it supported?  */
				GIConv ic = (cs->test == CHARMAP_SELECTOR_TO_UTF8) ?
					g_iconv_open (charset_trans->charset_name, "UTF-8") :
					g_iconv_open ("UTF-8", charset_trans->charset_name);
				if (ic != (GIConv)-1) {
					g_iconv_close (ic);
					subitem = gtk_check_menu_item_new_with_label 
						(_(charset_trans->charset_title));
					gtk_widget_show (subitem);
					gtk_menu_append (submenu, subitem);
					if (charset_trans->imp == CI_MAJOR)
						cs_emphasize_label (GTK_LABEL(gtk_bin_get_child (GTK_BIN(subitem))));
					g_object_set_data (G_OBJECT(subitem), CHARMAP_NAME_KEY,
							   (gpointer)charset_trans->charset_name);
					cnt++;
				}
			}
			charset_trans++;
		}
		if (cnt > 0) {
			gtk_menu_item_set_submenu (GTK_MENU_ITEM(item), GTK_WIDGET(submenu));
			gtk_widget_show (item);
			gtk_menu_append (menu, item);
			lg_cnt++;
		} else {
			gtk_widget_destroy (item);
		}
                lgroup++;
        }
	item = gtk_separator_menu_item_new ();
	gtk_widget_show (item);
	gtk_menu_append (menu, item);
	lg_cnt++;
	
	{
		char *locale_encoding_menu_title = g_strconcat (_("Locale: "), 
							      get_locale_encoding_name (), 
							      NULL);
		item = gtk_check_menu_item_new_with_label (locale_encoding_menu_title);
		g_free (locale_encoding_menu_title);
		gtk_widget_show (item);
		gtk_menu_append (menu, item);
		lg_cnt++;
		cs_emphasize_label (GTK_LABEL(gtk_bin_get_child (GTK_BIN(item))));
	}
	
	gnumeric_option_menu_set_menu (cs->encodings, GTK_WIDGET (menu));
	cs->encodings_menu = menu;
	set_menu_to_default (cs, lg_cnt);
}

static void
cs_class_init (GtkWidgetClass *widget_klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (widget_klass);
	widget_klass->mnemonic_activate = cs_mnemonic_activate;

	gobject_class->set_property = cs_set_property;
	gobject_class->get_property = cs_get_property;

	cs_signals[CHARMAP_CHANGED] =
		gtk_signal_new (
			"charmap_changed",
			GTK_RUN_LAST,
			GTK_CLASS_TYPE (widget_klass),
			GTK_SIGNAL_OFFSET (CharmapSelectorClass, 
					   charmap_changed),
			gtk_marshal_NONE__POINTER,
			GTK_TYPE_NONE, 1, GTK_TYPE_POINTER);

	g_object_class_install_property (gobject_class,
					 PROP_TEST_DIRECTION,
					 g_param_spec_uint ("TestDirection",
							    _("Conversion Direction"),
							    _("This value determines which iconv test to perform."),
							    (guint)CHARMAP_SELECTOR_TO_UTF8,
							    (guint)CHARMAP_SELECTOR_FROM_UTF8,
							    (guint)CHARMAP_SELECTOR_TO_UTF8,
							    G_PARAM_READWRITE));
}

GSF_CLASS (CharmapSelector, charmap_selector,
	   cs_class_init, cs_init, GTK_TYPE_HBOX)

GtkWidget *
charmap_selector_new (CharmapSelectorTestDirection test)
{
	return g_object_new (CHARMAP_SELECTOR_TYPE, "TestDirection", test, NULL);
}

gchar const *
charmap_selector_get_encoding (CharmapSelector *cs)
{
	GtkMenuItem *selection;
	char const *locale_encoding;
	char const *encoding;
	
	g_get_charset (&locale_encoding);

 	g_return_val_if_fail (IS_CHARMAP_SELECTOR(cs), locale_encoding);
	
 	selection = GTK_MENU_ITEM(gnumeric_option_menu_get_history (cs->encodings));
	encoding = (char const *) g_object_get_data (G_OBJECT(selection), 
						     CHARMAP_NAME_KEY);
	return encoding ? encoding : locale_encoding;
}

void 
charmap_selector_set_sensitive (CharmapSelector *cs, gboolean sensitive)
{
	g_return_if_fail (IS_CHARMAP_SELECTOR(cs));
	
	gtk_widget_set_sensitive (GTK_WIDGET(cs->encodings), sensitive);
}

static void
cs_set_property (GObject      *object,
		 guint         prop_id,
		 const GValue *value,
		 GParamSpec   *pspec)
{
	CharmapSelector *cs;
	cs = CHARMAP_SELECTOR (object);
  
	switch (prop_id)
	{
	case PROP_TEST_DIRECTION:
		cs->test = g_value_get_uint (value);
		cs_build_menu (cs);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}


static void 
cs_get_property (GObject     *object,
		 guint        prop_id,
		 GValue      *value,
		 GParamSpec  *pspec)
{
	CharmapSelector *cs;
  
	cs = CHARMAP_SELECTOR (object);
  
	switch (prop_id)
	{
	case PROP_TEST_DIRECTION:
		g_value_set_uint (value, (guint)cs->test);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}


