/*
 * summary.c:  Summary Information management
 *
 * Author:
 *    Michael Meeks (michael@ximian.com)
 *
 * (C) 1999 Michael Meeks
 */
#include <config.h>
#include <glib.h>
#include <stdio.h>
#include <time.h>
#include "summary.h"
#include "gutils.h"
#include <ctype.h>
#include <string.h>

/*
 *  NOTE:
 *  These strings are related to the fields in src/dialogs/summary.glade
 *  The field names are summary_item_name[i] prefixed with "glade_".
 *
 */
gchar *summary_item_name[] = {
	"codepage",
	"title",
	"subject",
	"author",
	"keywords",
	"comments",
	"template",
	"last_author",
	"revision_number",
	"last_printed",
	"created",
	"last_saved",
	"page_count",
	"word_count",
	"character_count",
	"application",
	"security",
	"category",
	"presentation_format",
	"number_of_bytes",
	"number_of_lines",
	"number_of_paragraphs",
	"number_of_slides",
	"number_of_pages_with_notes",
	"number_of_hidden_slides",
	"number_of_sound_or_video_clips",
	"thumbnail_scaling",
	"manager",
	"company",
	"links_uptodate"
};

static SummaryItem *
summary_item_new (const gchar *name, SummaryItemType t)
{
	SummaryItem *sit = g_new (SummaryItem, 1);
	sit->name = g_strdup (name);
	sit->type = t;
	return sit;
}


SummaryItem *
summary_item_new_int (const gchar *name, gint i)
{
	SummaryItem *sit = summary_item_new (name, SUMMARY_INT);
	sit->v.i = i;
	return sit;
}

SummaryItem *
summary_item_new_boolean (const gchar *name, gboolean i)
{
	SummaryItem *sit = summary_item_new (name, SUMMARY_BOOLEAN);
	sit->v.boolean = i;
	return sit;
}

SummaryItem *
summary_item_new_short (const gchar *name, gshort i)
{
	SummaryItem *sit = summary_item_new (name, SUMMARY_SHORT);
	sit->v.short_i = i;
	return sit;
}

SummaryItem *
summary_item_new_time (const gchar *name, GTimeVal t)
{
	SummaryItem *sit = summary_item_new (name, SUMMARY_TIME);
	sit->v.time = t;
	return sit;
}

SummaryItem *
summary_item_new_string (const gchar *name, const gchar *string)
{
	SummaryItem *sit = summary_item_new (name, SUMMARY_STRING);
	sit->v.txt = g_strdup (string);
	return sit;
}

char *
summary_item_as_text (const SummaryItem *sit)
{
	char   *ch_time;
	time_t  time;

	g_return_val_if_fail (sit != NULL, NULL);

	switch (sit->type) {
	case SUMMARY_STRING:
		if (sit->v.txt)
			return g_strdup (sit->v.txt);
		return g_strdup ("Internal Error");

	case SUMMARY_BOOLEAN:
		if (sit->v.boolean == 0)
			return g_strdup ("False");

		if (sit->v.boolean == 1)
			return g_strdup ("True");

		return "Unrecognized boolean value";

	case SUMMARY_SHORT:
		return g_strdup_printf ("%d", sit->v.short_i);

	case SUMMARY_INT:
		return g_strdup_printf ("%d", sit->v.i);

	case SUMMARY_TIME:
		time = (time_t)sit->v.time.tv_sec;
		ch_time = ctime (&time);
		ch_time[strlen (ch_time) - 1] = '\0';
		return g_strdup (ch_time);

	default:
		return g_strdup ("Unhandled type");
	}
}

void
summary_item_free (SummaryItem *sit)
{
	g_return_if_fail (sit);

	switch (sit->type) {
	case SUMMARY_STRING:
		g_free (sit->v.txt);
		sit->v.txt = NULL;
		break;

	case SUMMARY_BOOLEAN:
	case SUMMARY_SHORT:
	case SUMMARY_INT:
	case SUMMARY_TIME:
		break;
	default:
		g_warning ("unknown / unimplemented summary type");
		break;
	}
	g_free (sit->name);
	g_free (sit);
}

static void
summary_item_dump (SummaryItem *sit)
{
	char *txt;
	g_return_if_fail (sit);
	g_return_if_fail (sit->name);

	printf (" '%s' = ", sit->name);

	txt = summary_item_as_text (sit);
	printf (" %s\n", txt);
	g_free (txt);
}

SummaryInfo *
summary_info_new (void)
{
	SummaryInfo *sin = g_new (SummaryInfo, 1);
	sin->names = g_hash_table_new (&gnumeric_strcase_hash,
				       &gnumeric_strcase_equal);
	return sin;
}

SummaryItem *
summary_info_get (SummaryInfo *sin, char *name)
{
	g_return_val_if_fail (sin != NULL, NULL);
	g_return_val_if_fail (name != NULL, NULL);
	g_return_val_if_fail (sin->names != NULL, NULL);

	return g_hash_table_lookup (sin->names, name);
}

void
summary_info_add (SummaryInfo *sin, SummaryItem *sit)
{
	SummaryItem *tsit;

	g_return_if_fail (sin != NULL);
	g_return_if_fail (sit != NULL);
	g_return_if_fail (sit->name != NULL);
	g_return_if_fail (sin->names != NULL);

	if ((tsit = summary_info_get (sin, sit->name))) {
		g_hash_table_remove (sin->names, sit->name);
		summary_item_free (tsit);
	}

	g_hash_table_insert (sin->names, sit->name, sit);
}

void
summary_info_default (SummaryInfo *sin)
{
	SummaryItem *sit;

	g_return_if_fail (sin != NULL);

	sit = summary_item_new_string (summary_item_name [SUMMARY_I_AUTHOR],
				       g_get_real_name ());
	summary_info_add (sin, sit);

	sit = summary_item_new_string (summary_item_name [SUMMARY_I_APP],
				       g_get_prgname ());
	summary_info_add (sin, sit);
}

static void
free_item (gchar *key, SummaryItem *item, void *dummy)
{
	g_return_if_fail (item);

	summary_item_free (item);
}

void
summary_info_free (SummaryInfo *sin)
{
	g_return_if_fail (sin != NULL);
	g_return_if_fail (sin->names != NULL);

	g_hash_table_foreach (sin->names, (GHFunc)free_item,
			      NULL);
	g_hash_table_destroy (sin->names);
	sin->names = NULL;
	g_free (sin);
}

static void
append_item (gchar *key, SummaryItem *item, GList **l)
{
	*l = g_list_append (*l, item);
}

GList *
summary_info_as_list (SummaryInfo *sin)
{
	GList *l = NULL;

	g_return_val_if_fail (sin != NULL, NULL);
	g_return_val_if_fail (sin->names != NULL, NULL);

	g_hash_table_foreach (sin->names, (GHFunc)append_item,
			      &l);

	return l;
}

static void
dump_item (gchar *key, SummaryItem *item, void *dummy)
{
	summary_item_dump (item);
}

void
summary_info_dump (SummaryInfo *sin)
{
	g_return_if_fail (sin != NULL);
	g_return_if_fail (sin->names != NULL);

	printf ("summary information ...\n");

	g_hash_table_foreach (sin->names, (GHFunc)dump_item,
			      NULL);

	printf ("... end of summary information\n");
}
