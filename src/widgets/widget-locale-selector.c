/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * A locale selector widget.
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

#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include <gnumeric.h>
#include <gutils.h>
#include <string.h>
#include "widget-locale-selector.h"
#include "gnumeric-optionmenu.h"
#include <gsf/gsf-impl-utils.h>
#include <stdlib.h>

#define LS(x) LOCALE_SELECTOR (x)

#define LOCALE_NAME_KEY "Name of Locale"

/* ------------------------------------------------------------------------- */

typedef enum {
	LG_WESTERN_EUROPE,
	LG_EASTERN_EUROPE,
	LG_NORTH_AMERICA,
	LG_SOUTHCENTRAL_AMERICA,
	LG_ASIA,
	LG_MIDDLE_EAST,
	LG_AFRICA,
	LG_AUSTRALIA,
	LG_OTHER,
	LG_LAST
} LocaleGroup;

typedef struct
{
        char const *group_name;
	LocaleGroup const lgroup;
}
LGroupInfo;

static LGroupInfo lgroups[] = {
	{N_("Western Europe"), LG_WESTERN_EUROPE},
	{N_("Eastern Europe"), LG_EASTERN_EUROPE},
	{N_("North America"), LG_NORTH_AMERICA},
	{N_("South & Central America"), LG_SOUTHCENTRAL_AMERICA},
	{N_("Asia"), LG_ASIA},
	{N_("Africa"), LG_AFRICA},
	{N_("Australia"), LG_AUSTRALIA},
	{N_("Middle East"), LG_MIDDLE_EAST},
	{N_("Other"), LG_OTHER},
	{NULL, LG_LAST}
};

static int
lgroups_order (const void *_a, const void *_b)
{
	const LGroupInfo *a = (const LGroupInfo *)_a;
	const LGroupInfo *b = (const LGroupInfo *)_b;

	return g_utf8_collate (_(a->group_name), _(b->group_name));
}

/* ------------------------------------------------------------------------- */

typedef struct {
	gchar const *locale_title;
	gchar const *locale;
	LocaleGroup const lgroup;
	gboolean available;
} LocaleInfo;

static LocaleInfo locale_trans_array[] = {
	/*
	 * The format here is "Country/Language (locale)" or just
	 * "Country (locale)" when there is only one choice or one
	 * very dominant language.
	 *
	 * Note: lots of people get very emotional over this.  Please
	 * err on the safe side, if any.
	 */
	{N_("UNIX Default (C)"),       "C",       LG_OTHER},
	{N_("(af_ZA)"),"af_ZA", LG_OTHER },
	{N_("(am_ET)"),"am_ET", LG_OTHER },
	{N_("(ar_AE)"),"ar_AE", LG_OTHER },
	{N_("(ar_BH)"),"ar_BH", LG_OTHER },
	{N_("(ar_DZ)"),"ar_DZ", LG_OTHER },
	{N_("(ar_EG)"),"ar_EG", LG_OTHER },
	{N_("(ar_IN)"),"ar_IN", LG_OTHER },
	{N_("(ar_IQ)"),"ar_IQ", LG_OTHER },
	{N_("(ar_JO)"),"ar_JO", LG_OTHER },
	{N_("(ar_KW)"),"ar_KW", LG_OTHER },
	{N_("(ar_LB)"),"ar_LB", LG_OTHER },
	{N_("(ar_LY)"),"ar_LY", LG_OTHER },
	{N_("(ar_MA)"),"ar_MA", LG_OTHER },
	{N_("(ar_OM)"),"ar_OM", LG_OTHER },
	{N_("(ar_QA)"),"ar_QA", LG_OTHER },
	{N_("(ar_SA)"),"ar_SA", LG_OTHER },
	{N_("(ar_SD)"),"ar_SD", LG_OTHER },
	{N_("(ar_SY)"),"ar_SY", LG_OTHER },
	{N_("(ar_TN)"),"ar_TN", LG_OTHER },
	{N_("(ar_YE)"),"ar_YE", LG_OTHER },
	{N_("(az_AZ)"),"az_AZ", LG_OTHER },
	{N_("(be_BY)"),"be_BY", LG_OTHER },
	{N_("(bg_BG)"),"bg_BG", LG_OTHER },
	{N_("(bn_BD)"),"bn_BD", LG_OTHER },
	{N_("(bn_IN)"),"bn_IN", LG_OTHER },
	{N_("(br_FR)"),"br_FR", LG_OTHER },
	{N_("(bs_BA)"),"bs_BA", LG_OTHER },
	{N_("Spain, Catalan (ca_ES)"),"ca_ES", LG_WESTERN_EUROPE },
	{N_("(cs_CZ)"),"cs_CZ", LG_OTHER },
	{N_("(cy_GB)"),"cy_GB", LG_OTHER },
	{N_("Denmark (da_DK)"),"da_DK", LG_WESTERN_EUROPE },
	{N_("Austria (de_AT)"),"de_AT", LG_WESTERN_EUROPE },
	{N_("Belgium/German (de_BE)"),"de_BE", LG_WESTERN_EUROPE },
	{N_("Switzerland/German (de_CH)"),"de_CH", LG_WESTERN_EUROPE },
	{N_("Germany (de_DE)"),        "de_DE",   LG_WESTERN_EUROPE},
	{N_("(de_LU)"),"de_LU", LG_WESTERN_EUROPE },
	{N_("(el_GR)"),"el_GR", LG_WESTERN_EUROPE },
	{N_("Australia (en_AU)"),"en_AU", LG_AUSTRALIA },
	{N_("(en_BW)"),"en_BW", LG_OTHER },
	{N_("Canada (en_CA)"),         "en_CA",   LG_NORTH_AMERICA},
	{N_("(en_DK)"),"en_DK", LG_WESTERN_EUROPE },
	{N_("Great Britain (en_GB)"),  "en_GB",   LG_WESTERN_EUROPE},
	{N_("Hong Kong/English (en_HK)"),"en_HK", LG_ASIA },
	{N_("Ireland (en_IE)"),"en_IE", LG_WESTERN_EUROPE },
	{N_("India/English (en_IN)"),"en_IN", LG_ASIA },
	{N_("New Zealand (en_NZ)"),"en_NZ", LG_AUSTRALIA },
	{N_("(en_PH)"),"en_PH", LG_OTHER },
	{N_("(en_SG)"),"en_SG", LG_OTHER },
	{N_("United States (en_US)"),  "en_US",   LG_NORTH_AMERICA},
	{N_("South Africa (en_ZA)"),"en_ZA", LG_AFRICA },
	{N_("(en_ZW)"),"en_ZW", LG_OTHER },
	{N_("(eo_EO)"),"eo_EO", LG_OTHER },
	{N_("(es_AR)"),"es_AR", LG_OTHER },
	{N_("(es_BO)"),"es_BO", LG_OTHER },
	{N_("(es_CL)"),"es_CL", LG_OTHER },
	{N_("(es_CO)"),"es_CO", LG_OTHER },
	{N_("(es_CR)"),"es_CR", LG_OTHER },
	{N_("Dominican Republic (es_DO)"),"es_DO", LG_OTHER },
	{N_("(es_EC)"),"es_EC", LG_OTHER },
	{N_("Spain (es_ES)"),"es_ES", LG_WESTERN_EUROPE },
	{N_("(es_GT)"),"es_GT", LG_OTHER },
	{N_("(es_HN)"),"es_HN", LG_OTHER },
	{N_("Mexico (es_MX)"),"es_MX", LG_OTHER },
	{N_("(es_NI)"),"es_NI", LG_OTHER },
	{N_("(es_PA)"),"es_PA", LG_OTHER },
	{N_("(es_PE)"),"es_PE", LG_OTHER },
	{N_("(es_PR)"),"es_PR", LG_OTHER },
	{N_("(es_PY)"),"es_PY", LG_OTHER },
	{N_("(es_SV)"),"es_SV", LG_OTHER },
	{N_("(es_US)"),"es_US", LG_OTHER },
	{N_("(es_UY)"),"es_UY", LG_OTHER },
	{N_("(es_VE)"),"es_VE", LG_OTHER },
	{N_("(et_EE)"),"et_EE", LG_OTHER },
	{N_("(eu_ES)"),"eu_ES", LG_OTHER },
	{N_("(fa_IR)"),"fa_IR", LG_OTHER },
	{N_("Finland/Finnish (fi_FI)"),"fi_FI", LG_OTHER },
	{N_("(fo_FO)"),"fo_FO", LG_OTHER },
	{N_("Belgium/French (fr_BE)"),"fr_BE", LG_WESTERN_EUROPE },
	{N_("Canada/French (fr_CA)"),"fr_CA", LG_NORTH_AMERICA },
	{N_("Switzerland/French (fr_CH)"),"fr_CH", LG_WESTERN_EUROPE },
	{N_("France (fr_FR)"),"fr_FR", LG_WESTERN_EUROPE },
	{N_("(ga_IE)"),"ga_IE", LG_OTHER },
	{N_("(gd_GB)"),"gd_GB", LG_OTHER },
	{N_("(gl_ES)"),"gl_ES", LG_OTHER },
	{N_("(gv_GB)"),"gv_GB", LG_OTHER },
	{N_("(he_IL)"),"he_IL", LG_OTHER },
	{N_("India/Hindu (hi_IN)"),"hi_IN", LG_ASIA },
	{N_("(hr_HR)"),"hr_HR", LG_OTHER },
	{N_("(hu_HU)"),"hu_HU", LG_OTHER },
	{N_("(hy_AM)"),"hy_AM", LG_OTHER },
	{N_("(i18n)"),"i18n", LG_OTHER },
	{N_("(id_ID)"),"id_ID", LG_OTHER },
	{N_("Iceland (is_IS)"),"is_IS", LG_WESTERN_EUROPE },
	{N_("(iso14651_t1)"),"iso14651_t1", LG_OTHER },
	{N_("Switzerland/Italian (it_CH)"),"it_CH", LG_WESTERN_EUROPE },
	{N_("Italy (it_IT)"),"it_IT", LG_WESTERN_EUROPE },
	{N_("(iw_IL)"),"iw_IL", LG_OTHER },
	{N_("Japan (ja_JP)"),"ja_JP", LG_ASIA },
	{N_("(ka_GE)"),"ka_GE", LG_OTHER },
	{N_("(kl_GL)"),"kl_GL", LG_OTHER },
	{N_("(ko_KR)"),"ko_KR", LG_OTHER },
	{N_("(kw_GB)"),"kw_GB", LG_OTHER },
	{N_("(lt_LT)"),"lt_LT", LG_OTHER },
	{N_("(lug_UG)"),"lug_UG", LG_OTHER },
	{N_("(lv_LV)"),"lv_LV", LG_OTHER },
	{N_("(mi_NZ)"),"mi_NZ", LG_OTHER },
	{N_("(mk_MK)"),"mk_MK", LG_OTHER },
	{N_("(mr_IN)"),"mr_IN", LG_OTHER },
	{N_("(ms_MY)"),"ms_MY", LG_OTHER },
	{N_("(mt_MT)"),"mt_MT", LG_OTHER },
	{N_("Belgium/Flemish (nl_BE)"),"nl_BE", LG_WESTERN_EUROPE },
	{N_("The Netherlands (nl_NL)"),"nl_NL", LG_WESTERN_EUROPE },
	{N_("Norway/Nynorsk (nn_NO)"),"nn_NO", LG_WESTERN_EUROPE },
	{N_("Norway/Bokmal (no_NO)"),  "no_NO",   LG_WESTERN_EUROPE},
	{N_("(oc_FR)"),"oc_FR", LG_OTHER },
	{N_("Poland (pl_PL)"),"pl_PL", LG_EASTERN_EUROPE },
	{N_("POSIX"),"POSIX", LG_OTHER },
	{N_("Brazil (pt_BR)"),"pt_BR", LG_SOUTHCENTRAL_AMERICA },
	{N_("Portugal (pt_PT)"),"pt_PT", LG_WESTERN_EUROPE },
	{N_("(ro_RO)"),"ro_RO", LG_OTHER },
	{N_("Russia (ru_RU)"),"ru_RU", LG_EASTERN_EUROPE },
	{N_("(ru_UA)"),"ru_UA", LG_OTHER },
	{N_("(se_NO)"),"se_NO", LG_OTHER },
	{N_("(sk_SK)"),"sk_SK", LG_OTHER },
	{N_("(sl_SI)"),"sl_SI", LG_OTHER },
	{N_("(sq_AL)"),"sq_AL", LG_OTHER },
	{N_("(sr_YU)"),"sr_YU", LG_OTHER },
	{N_("Finland/Swedish (sv_FI)"),"sv_FI", LG_OTHER },
	{N_("Sweden (sv_SE)"),"sv_SE", LG_OTHER },
	{N_("India/Tamil (ta_IN)"),"ta_IN", LG_ASIA },
	{N_("(te_IN)"),"te_IN", LG_OTHER },
	{N_("(tg_TJ)"),"tg_TJ", LG_OTHER },
	{N_("Thailand (th_TH)"),"th_TH", LG_ASIA },
	{N_("(ti_ER)"),"ti_ER", LG_OTHER },
	{N_("(ti_ET)"),"ti_ET", LG_OTHER },
	{N_("(tl_PH)"),"tl_PH", LG_OTHER },
	{N_("(tr_TR)"),"tr_TR", LG_OTHER },
	{N_("(tt_RU)"),"tt_RU", LG_OTHER },
	{N_("(uk_UA)"),"uk_UA", LG_OTHER },
	{N_("(ur_PK)"),"ur_PK", LG_OTHER },
	{N_("(uz_UZ)"),"uz_UZ", LG_OTHER },
	{N_("(vi_VN)"),"vi_VN", LG_OTHER },
	{N_("(wa_BE)"),"wa_BE", LG_OTHER },
	{N_("(yi_US)"),"yi_US", LG_OTHER },
	{N_("China (zh_CN)"),"zh_CN", LG_ASIA },
	{N_("Hong Kong/Chinese (zh_HK)"),"zh_HK", LG_ASIA },
	{N_("(zh_SG)"),"zh_SG", LG_OTHER },
	{N_("Taiwan (zh_TW)"),"zh_TW", LG_OTHER },
	{NULL,                         NULL,      LG_LAST}
};

static int
locale_order (const void *_a, const void *_b)
{
	const LocaleInfo *a = (const LocaleInfo *)_a;
	const LocaleInfo *b = (const LocaleInfo *)_b;

	if (a->lgroup != b->lgroup)
		return b->lgroup - a->lgroup;

	return g_utf8_collate (_(a->locale_title), _(b->locale_title));
}

/* ------------------------------------------------------------------------- */

/* name -> LocaleInfo* mapping */
static GHashTable *locale_hash;

struct _LocaleSelector {
	GtkHBox box;
	GnumericOptionMenu *locales;
	GtkMenu *locales_menu;
};

typedef struct {
	GtkHBoxClass parent_class;

	gboolean (* locale_changed) (LocaleSelector *ls, char const *new_locale);
} LocaleSelectorClass;


typedef LocaleSelector Ls;
typedef LocaleSelectorClass LsClass;

/* Signals we emit */
enum {
	LOCALE_CHANGED,
	LAST_SIGNAL
};

enum {
	PROP_0
};




static guint ls_signals[LAST_SIGNAL] = { 0 };

static void ls_set_property      (GObject          *object,
				  guint             prop_id,
				  const GValue     *value,
				  GParamSpec       *pspec);

static void ls_get_property      (GObject          *object,
				  guint             prop_id,
				  GValue           *value,
				  GParamSpec       *pspec);

const char *
locale_selector_get_locale_name (G_GNUC_UNUSED LocaleSelector *ls,
				    const char *locale)
{
	LocaleInfo const *ci;

	g_return_val_if_fail (locale != NULL, NULL);

	ci = g_hash_table_lookup (locale_hash, locale);
	return ci ? _(ci->locale_title) : NULL;
}

static char*
get_locale_name (LocaleSelector *ls)
{
	char const *cur_locale;
	char *cur_locale_cp=NULL;
	char const *name;
	char **parts;

	cur_locale = setlocale (LC_ALL, NULL);
	if (cur_locale) {
		parts = g_strsplit (cur_locale,".",2);
		cur_locale_cp = g_strdup (parts[0]);
		g_strfreev (parts);
	}

	name = locale_selector_get_locale_name (ls, cur_locale_cp);
	return name ? g_strdup (name) : cur_locale_cp;
}

static void
locales_changed_cb (GnumericOptionMenu *optionmenu, LocaleSelector *ls)
{
	char * locale;

	g_return_if_fail (IS_LOCALE_SELECTOR (ls));
	g_return_if_fail (optionmenu == ls->locales);

	locale = locale_selector_get_locale (ls);

	g_signal_emit (G_OBJECT (ls),
		       ls_signals[LOCALE_CHANGED],
		       0, locale);
	g_free (locale);
}

static void
set_menu_to_default (LocaleSelector *ls, gint item)
{
	GSList sel = { GINT_TO_POINTER (item - 1), NULL};

	g_return_if_fail (ls != NULL && IS_LOCALE_SELECTOR (ls));

	gnumeric_option_menu_set_history (ls->locales, &sel);
}

static gboolean
ls_mnemonic_activate (GtkWidget *w, gboolean group_cycling)
{
	LocaleSelector *ls = LOCALE_SELECTOR (w);
	gtk_widget_grab_focus (GTK_WIDGET (ls->locales));
	return TRUE;
}


static void
ls_build_menu (LocaleSelector *ls)
{
        GtkWidget *item;
	GtkMenu *menu;
	LGroupInfo const *lgroup = lgroups;
	gint lg_cnt = 0;

        menu = GTK_MENU (gtk_menu_new ());

	while (lgroup->group_name) {
		LocaleInfo const *locale_trans;
		GtkMenu *submenu;
		gint cnt = 0;

		item = gtk_menu_item_new_with_label (_(lgroup->group_name));

		submenu = GTK_MENU (gtk_menu_new ());
		locale_trans = locale_trans_array;

		while (locale_trans->lgroup != LG_LAST) {
			GtkWidget *subitem;
			if (locale_trans->lgroup == lgroup->lgroup && locale_trans->available) {
					subitem = gtk_check_menu_item_new_with_label
						(_(locale_trans->locale_title));
					gtk_widget_show (subitem);
					gtk_menu_shell_append (GTK_MENU_SHELL (submenu),  subitem);
					g_object_set_data (G_OBJECT (subitem), LOCALE_NAME_KEY,
							   (gpointer)(locale_trans->locale));
					cnt++;
			}
			locale_trans++;
		}
		if (cnt > 0) {
			gtk_menu_item_set_submenu (GTK_MENU_ITEM(item), GTK_WIDGET (submenu));
			gtk_widget_show (item);
			gtk_menu_shell_append (GTK_MENU_SHELL (menu),  item);
			lg_cnt++;
		} else {
			g_object_unref (item);
		}
                lgroup++;
        }
	item = gtk_separator_menu_item_new ();
	gtk_widget_show (item);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu),  item);
	lg_cnt++;

	{
		char *locale_name = get_locale_name (ls);
		char *locale_menu_title = g_strconcat (_("Current Locale: "),
						       locale_name, NULL);
		g_free (locale_name);
		item = gtk_check_menu_item_new_with_label (locale_menu_title);
		g_free (locale_menu_title);
		gtk_widget_show (item);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu),  item);
		lg_cnt++;
	}

	gnumeric_option_menu_set_menu (ls->locales, GTK_WIDGET (menu));
	ls->locales_menu = menu;
	set_menu_to_default (ls, lg_cnt);
}

static void
ls_init (LocaleSelector *ls)
{
	ls->locales = GNUMERIC_OPTION_MENU(gnumeric_option_menu_new());
	ls_build_menu (ls);

	g_signal_connect (G_OBJECT (ls->locales), "changed",
                          G_CALLBACK (locales_changed_cb), ls);
        gtk_box_pack_start (GTK_BOX(ls), GTK_WIDGET (ls->locales),
                            TRUE, TRUE, 0);
}

static void
ls_class_init (GtkWidgetClass *widget_klass)
{
	LocaleInfo *ci;
	char *oldlocale;

	GObjectClass *gobject_class = G_OBJECT_CLASS (widget_klass);
	widget_klass->mnemonic_activate = ls_mnemonic_activate;

	gobject_class->set_property = ls_set_property;
	gobject_class->get_property = ls_get_property;

	ls_signals[LOCALE_CHANGED] =
		g_signal_new ("locale_changed",
			      LOCALE_SELECTOR_TYPE,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (LocaleSelectorClass, locale_changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);

	qsort (lgroups, G_N_ELEMENTS (lgroups) - 2, sizeof (lgroups[0]),
	       lgroups_order);
	qsort (locale_trans_array, G_N_ELEMENTS (locale_trans_array) - 1,
	       sizeof (locale_trans_array[0]), locale_order);

	locale_hash =
		g_hash_table_new_full (gnumeric_ascii_strcase_hash,
				       gnumeric_ascii_strcase_equal,
				       (GDestroyNotify)g_free,
				       NULL);

	oldlocale = g_strdup (setlocale (LC_ALL, NULL));
	for (ci = locale_trans_array; ci->locale_title; ci++) {
		ci->available = (setlocale (LC_ALL, ci->locale) != NULL);
		g_hash_table_insert (locale_hash, (char *)ci->locale, ci);
	}
	setlocale (LC_ALL, oldlocale);
	g_free (oldlocale);
}

GSF_CLASS (LocaleSelector, locale_selector,
	   ls_class_init, ls_init, GTK_TYPE_HBOX)

GtkWidget *
locale_selector_new (void)
{
	return g_object_new (LOCALE_SELECTOR_TYPE, NULL);
}

gchar *
locale_selector_get_locale (LocaleSelector *ls)
{
	GtkMenuItem *selection;
	char const *cur_locale;
	char const *locale;

	char *cur_locale_cp = NULL;
	char **parts;

	cur_locale = setlocale (LC_ALL, NULL);
	if (cur_locale) {
		parts = g_strsplit (cur_locale,".",2);
		cur_locale_cp = g_strdup (parts[0]);
		g_strfreev (parts);
	}

 	g_return_val_if_fail (IS_LOCALE_SELECTOR (ls), cur_locale_cp);

 	selection = GTK_MENU_ITEM (gnumeric_option_menu_get_history (ls->locales));
	locale = (char const *) g_object_get_data (G_OBJECT (selection),
						     LOCALE_NAME_KEY);
	return locale ? g_strdup (locale) : cur_locale_cp;
}

struct cb_find_entry {
	const char *enc;
	gboolean found;
	int i;
	GSList *path;
};

static void
cb_find_entry (GtkMenuItem *w, struct cb_find_entry *cl)
{
	GtkWidget *sub;

	if (cl->found)
		return;

	sub = gtk_menu_item_get_submenu (w);
	if (sub) {
		GSList *tmp = cl->path = g_slist_prepend (cl->path, GINT_TO_POINTER (cl->i));
		cl->i = 0;

		gtk_container_foreach (GTK_CONTAINER (sub), (GtkCallback)cb_find_entry, cl);
		if (cl->found)
			return;
		
		cl->i = GPOINTER_TO_INT (cl->path->data);
		cl->path = cl->path->next;
		g_slist_free_1 (tmp);
	} else {
		const char *this_enc =
			g_object_get_data (G_OBJECT (w), LOCALE_NAME_KEY);
		if (this_enc && strcmp (this_enc, cl->enc) == 0) {
			cl->found = TRUE;
			cl->path = g_slist_prepend (cl->path, GINT_TO_POINTER (cl->i));
			cl->path = g_slist_reverse (cl->path);
			return;
		}
	}
	cl->i++;
}

gboolean
locale_selector_set_locale (LocaleSelector *ls, const char *enc)
{
	struct cb_find_entry cl;
	LocaleInfo const *ci;

	g_return_val_if_fail (IS_LOCALE_SELECTOR (ls), FALSE);
	g_return_val_if_fail (enc != NULL, FALSE);

	ci = g_hash_table_lookup (locale_hash, enc);
	if (!ci)
		return FALSE;

	enc = ci->locale;
	if (!enc)
		return FALSE;

	cl.enc = enc;
	cl.found = FALSE;
	cl.i = 0;
	cl.path = NULL;

	gtk_container_foreach (GTK_CONTAINER (ls->locales_menu),
			       (GtkCallback)cb_find_entry,
			       &cl);
	if (!cl.found)
		return FALSE;

	gnumeric_option_menu_set_history (ls->locales, cl.path);
	g_slist_free (cl.path);

	return TRUE;
}


void
locale_selector_set_sensitive (LocaleSelector *ls, gboolean sensitive)
{
	g_return_if_fail (IS_LOCALE_SELECTOR (ls));

	gtk_widget_set_sensitive (GTK_WIDGET (ls->locales), sensitive);
}

static void
ls_set_property (GObject      *object,
		 guint         prop_id,
		 const GValue *value,
		 GParamSpec   *pspec)
{
	LocaleSelector *ls;
	ls = LOCALE_SELECTOR (object);

	switch (prop_id)
	{
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}


static void
ls_get_property (GObject     *object,
		 guint        prop_id,
		 GValue      *value,
		 GParamSpec  *pspec)
{
	LocaleSelector *ls;

	ls = LOCALE_SELECTOR (object);

	switch (prop_id)
	{
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}
