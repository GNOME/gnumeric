/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * summary.c:  Summary Information management
 *
 * Author:
 *    Michael Meeks (michael@ximian.com)
 *
 * (C) 1999-2001 Michael Meeks
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "summary.h"

#include "gutils.h"

#include <stdio.h>
#include <time.h>
#include <string.h>

/*
 *  NOTE:
 *  These strings are related to the fields in src/dialogs/summary.glade
 *  The field names are summary_item_name[i] prefixed with "glade_".
 *
 */
const gchar *summary_item_name[] = {
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
summary_item_new (gchar const *name, SummaryItemType t)
{
	SummaryItem *sit = g_new (SummaryItem, 1);
	sit->name = g_strdup (name);
	sit->type = t;
	return sit;
}


SummaryItem *
summary_item_new_int (gchar const *name, gint i)
{
	SummaryItem *sit = summary_item_new (name, SUMMARY_INT);
	sit->v.i = i;
	return sit;
}

SummaryItem *
summary_item_new_boolean (gchar const *name, gboolean i)
{
	SummaryItem *sit = summary_item_new (name, SUMMARY_BOOLEAN);
	sit->v.boolean = i;
	return sit;
}

SummaryItem *
summary_item_new_short (gchar const *name, gshort i)
{
	SummaryItem *sit = summary_item_new (name, SUMMARY_SHORT);
	sit->v.short_i = i;
	return sit;
}

SummaryItem *
summary_item_new_time (gchar const *name, GTimeVal t)
{
	SummaryItem *sit = summary_item_new (name, SUMMARY_TIME);
	sit->v.time = t;
	return sit;
}

SummaryItem *
summary_item_new_string (gchar const *name, gchar const *string, gboolean copy)
{
	SummaryItem *sit = summary_item_new (name, SUMMARY_STRING);
	sit->v.txt = copy ? g_strdup (string) : (char *)string;
	return sit;
}

char *
summary_item_as_text (SummaryItem const *sit)
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

		return g_strdup ("Unrecognized boolean value");

	case SUMMARY_SHORT:
		return g_strdup_printf ("%d", sit->v.short_i);

	case SUMMARY_INT:
		return g_strdup_printf ("%d", sit->v.i);

	case SUMMARY_TIME:
		time = (time_t)sit->v.time.tv_sec;
		ch_time = ctime (&time);
		ch_time[strlen (ch_time) - 1] = '\0';
		return g_strdup (ch_time);

#ifndef DEBUG_SWITCH_ENUM
	default:
		g_assert_not_reached ();
		return g_strdup ("Unhandled type");
#endif
	}
}

void
summary_item_free (SummaryItem *sit)
{
	if (!sit)
		return;

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

#ifndef DEBUG_SWITCH_ENUM
	default:
		g_assert_not_reached ();
		break;
#endif
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
static gboolean
summary_item_eq (SummaryItem const *a, SummaryItem const *b)
{
	if (a->type != b->type || strcmp (a->name, b->name))
		return FALSE;

	switch (a->type) {
	case SUMMARY_STRING:	return strcmp (a->v.txt, b->v.txt) == 0;
	case SUMMARY_BOOLEAN:	return a->v.boolean == b->v.boolean;
	case SUMMARY_SHORT:	return a->v.short_i == b->v.short_i;
	case SUMMARY_INT:	return a->v.i == b->v.i;
	case SUMMARY_TIME:	return a->v.time.tv_sec == b->v.time.tv_sec &&
				       a->v.time.tv_usec == b->v.time.tv_usec;

	default :
		g_warning ("Huh!?");
	}
	return FALSE;
}

SummaryInfo *
summary_info_new (void)
{
	SummaryInfo *sin = g_new (SummaryInfo, 1);
	sin->names = g_hash_table_new (&gnumeric_ascii_strcase_hash,
				       &gnumeric_ascii_strcase_equal);
	sin->modified = FALSE;
	return sin;
}

static SummaryItem *
summary_info_get (SummaryInfo const *sin, char const *name)
{
	g_return_val_if_fail (sin != NULL, NULL);
	g_return_val_if_fail (name != NULL, NULL);
	g_return_val_if_fail (sin->names != NULL, NULL);

	return g_hash_table_lookup (sin->names, name);
}

gboolean
summary_info_add (SummaryInfo *sin, SummaryItem *sit)
{
	SummaryItem *tsit;

	g_return_val_if_fail (sin != NULL, FALSE);
	g_return_val_if_fail (sit != NULL, FALSE);
	g_return_val_if_fail (sit->name != NULL, FALSE);
	g_return_val_if_fail (sin->names != NULL, FALSE);

	/* remove existing items if it is different */
	if ((tsit = summary_info_get (sin, sit->name))) {
		if (summary_item_eq (sit, tsit)) {
			summary_item_free (sit);
			return FALSE;
		}
		g_hash_table_remove (sin->names, sit->name);
		summary_item_free (tsit);
		sin->modified = TRUE;
	}

	/* Storing a blank string removes that tag */
	if (sit->type == SUMMARY_STRING &&
	    (sit->v.txt == NULL || sit->v.txt[0] == '\0'))
		summary_item_free (sit);
	else {
		g_hash_table_insert (sin->names, sit->name, sit);
		sin->modified = TRUE;
	}
	return TRUE;
}

void
summary_info_default (SummaryInfo *sin)
{
	SummaryItem *sit;

	g_return_if_fail (sin != NULL);

	sit = summary_item_new_string (summary_item_name [SUMMARY_I_AUTHOR],
				       g_get_real_name (), TRUE);
	summary_info_add (sin, sit);

	sit = summary_item_new_string (summary_item_name [SUMMARY_I_APP],
				       g_get_prgname (), TRUE);
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
summary_info_as_list (SummaryInfo const *sin)
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

SummaryItem *
summary_item_copy (SummaryItem const *sit)
{
	if (sit == NULL)
		return NULL;

	switch (sit->type) {
	case SUMMARY_STRING:
		return summary_item_new_string (sit->name, sit->v.txt, TRUE);

	case SUMMARY_BOOLEAN:
		return summary_item_new_boolean (sit->name, sit->v.boolean);

	case SUMMARY_SHORT:
		return summary_item_new_short (sit->name, sit->v.short_i);

	case SUMMARY_INT:
		return summary_item_new_int (sit->name, sit->v.i);

	case SUMMARY_TIME:
		return summary_item_new_time (sit->name, sit->v.time);

#ifndef DEBUG_SWITCH_ENUM
	default:
		g_assert_not_reached ();
		return NULL;
#endif
	}
}

SummaryItem *
summary_item_by_name (char const *name, SummaryInfo const *sin)
{
	return summary_item_copy (summary_info_get (sin, name));
}

char *
summary_item_as_text_by_name (char const *name, SummaryInfo const *sin)
{
	SummaryItem *sit = summary_info_get (sin, name);

	if (sit)
		return summary_item_as_text (sit);
	else
		return g_strdup ("");
}
