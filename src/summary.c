/*
 * summary.c:  Summary Information management
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 *
 * (C) 1999 Michael Meeks
 */
#include <config.h>
#include <ctype.h>
#include <glib.h>
#include "summary.h"

gchar *summary_item_name[] = {
	"Title",
	"Subject",
	"Author",
	"Keywords",
	"Comments",
	"Saving App"
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
summary_item_new_int    (const gchar *name, gint i)
{
	SummaryItem *sit = summary_item_new (name, SUMMARY_INT);
	sit->v.i = i;
	return sit;
}

SummaryItem *
summary_item_new_time   (const gchar *name, GTimeVal t)
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

void
summary_item_free (SummaryItem *sit)
{
	g_return_if_fail (sit);

	switch (sit->type)
	{
	case SUMMARY_STRING:
		g_free (sit->v.txt);
		sit->v.txt = NULL;
		break;
	case SUMMARY_INT:
	case SUMMARY_TIME:
		break;
	default:
		g_warning ("unknown / unimplemented summary type");
		break;
	}
	g_free (sit);
}

void
summary_item_dump (SummaryItem *sit)
{
	g_return_if_fail (sit);
	g_return_if_fail (sit->name);

	printf (" '%s' = ", sit->name);
	switch (sit->type)
	{
	case SUMMARY_STRING:
		printf (" '%s'\n", sit->v.txt);
		break;
	case SUMMARY_INT:
		printf (" %d\n", sit->v.i);
		break;
	case SUMMARY_TIME:
		printf (" Unimplemented\n");
		break;
	default:
		g_warning ("unknown / unimplemented summary type");
		break;
	}
}

static gint
g_str_case_equal (gconstpointer v, gconstpointer v2)
{
  return g_strcasecmp ((const gchar*) v, (const gchar*)v2) == 0;
}

/* a char* hash function from ASU */
static guint
g_str_case_hash (gconstpointer v)
{
  const char *s = (char*)v;
  const char *p;
  guint h=0, g;

  for(p = s; *p != '\0'; p += 1) {
    h = ( h << 4 ) + tolower (*p);
    if ( ( g = h & 0xf0000000 ) ) {
      h = h ^ (g >> 24);
      h = h ^ g;
    }
  }

  return h /* % M */;
}


SummaryInfo *
summary_info_new (void)
{
	SummaryInfo *sin = g_new (SummaryInfo, 1);
	sin->names = g_hash_table_new (g_str_case_hash,
				       g_str_case_equal);
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
	g_return_if_fail (sin != NULL);
	g_return_if_fail (sit != NULL);
	g_return_if_fail (sit->name != NULL);
	g_return_if_fail (sin->names != NULL);

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
