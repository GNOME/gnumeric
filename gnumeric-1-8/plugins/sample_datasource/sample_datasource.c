/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * sample_datasource.c: A prototype for handling external data sources
 *
 * Copyright (C) 2002 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */

#include <gnumeric-config.h>
#include <gnumeric.h>

#include "func.h"
#include "value.h"
#include "workbook.h"
#include "sheet.h"
#include "gnm-i18n.h"
#include <goffice/app/go-plugin.h>
#include <gnm-plugin.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <glib/gstdio.h>

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "gnumeric:atl"

GNM_PLUGIN_MODULE_HEADER;

static int    atl_fd = -1;
static char * atl_filename = NULL;
static FILE  *atl_file = NULL;
static guint  atl_source = 0;
static GHashTable *watched_values = NULL;
static GHashTable *watchers = NULL;

typedef struct {
	char	   *name;
	gnm_float   value;
	gboolean    valid;
	GHashTable *deps;
} WatchedValue;

typedef struct {
	GnmExprFunction const *node;  /* Expression node that calls us */
	GnmDependent *dep;	      /* GnmDependent containing that node */

	WatchedValue	*value;
} Watcher;

static guint
watcher_hash (Watcher const *w)
{
	return (GPOINTER_TO_INT(w->node) << 16) + GPOINTER_TO_INT(w->dep);
}
static gint
watcher_equal (Watcher const *w1, Watcher const *w2)
{
	return w1->node == w2->node && w1->dep == w2->dep;
}

static WatchedValue *
watched_value_fetch (char const *tag)
{
	WatchedValue *val = g_hash_table_lookup (watched_values, tag);
	if (val == NULL) {
		val = g_new (WatchedValue, 1);
		val->name = g_strdup (tag);
		val->value = 0.;
		val->valid = FALSE;
		val->deps = g_hash_table_new (g_direct_hash, g_direct_equal);
		g_hash_table_insert (watched_values, val->name, val);
	}
	return val;
}

/***************************************************************************/

static void
cb_watcher_queue_recalc (gpointer key, gpointer value, gpointer closure)
{
	Watcher const *w = key;
	Sheet *sheet = w->dep->sheet;
	dependent_queue_recalc (w->dep);

	if (sheet && workbook_get_recalcmode (sheet->workbook))
		workbook_recalc (sheet->workbook);
}

static gboolean
cb_atl_input (GIOChannel *gioc, GIOCondition cond, gpointer ignored)
{
	char buf[128];

	/* quick format ticker:value\n
	 * there is no notion of a field for now.
	 */
	while (fgets (buf, sizeof (buf), atl_file) != NULL) {
		char *sym = buf;
		char *value_str = strchr (buf, ':');

		if (value_str != NULL) {
			gnm_float val;
			char *end;
			*value_str++ = '\0';

			val = gnm_strto (value_str, &end);
			if (sym != end && errno == 0) {
				WatchedValue *wv = watched_value_fetch (sym);
				wv->valid = TRUE;
				wv->value = val;

				g_hash_table_foreach (wv->deps,
					cb_watcher_queue_recalc, NULL);
				printf ("'%s' <= %f\n", sym, val);
			}
		}
	}

	return TRUE;
}

G_MODULE_EXPORT void
go_plugin_init (GOPlugin *plugin, GOCmdContext *cc)
{
	GIOChannel *channel = NULL;
	char *filename;

	fprintf (stderr, ">>>>>>>>>>>>>>>>>>>>>>>>>>>> LOAD ATL\n");
	g_return_if_fail (atl_fd < 0);

	filename = g_build_filename (g_get_home_dir (), "atl", NULL);

	/* NOTE : better to use popen here, but this is fine for testing */
#ifdef HAVE_MKFIFO
#warning "If gstdio.h had g_mkfifo, that's what we should use here"
	if (mkfifo (filename, S_IRUSR | S_IWUSR) == 0) {
		atl_filename = filename;
		atl_fd = g_open (atl_filename, O_RDWR|O_NONBLOCK, 0);
	} else
#endif /* HAVE_MKFIFO */
		g_free (filename);

	if (atl_fd >= 0) {
		atl_file = fdopen (atl_fd, "rb");
		channel = g_io_channel_unix_new (atl_fd);
		atl_source  = g_io_add_watch (channel,
			G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL,
			cb_atl_input, NULL);
		g_io_channel_unref (channel);
	}
	watched_values = g_hash_table_new (
		(GHashFunc)  g_str_hash,
		(GEqualFunc) g_str_equal);
	watchers = g_hash_table_new (
		(GHashFunc)  watcher_hash,
		(GEqualFunc) watcher_equal);
}

/* TODO : init and cleanup should be given CommandContexts
 * to make things tidier
 */
G_MODULE_EXPORT void
go_plugin_shutdown (GOPlugin *plugin, GOCmdContext *cc)
{
	fprintf (stderr, "UNLOAD ATL >>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");

	if (atl_source) {
		g_source_remove (atl_source);
		atl_source = 0;
	}

	if (atl_filename) {
		g_unlink (atl_filename);
		g_free (atl_filename);
		atl_filename = NULL;
	}

	if (atl_fd >= 0) {
		close (atl_fd);
		atl_fd = -1;
	}

	if (atl_file != NULL) {
		fclose (atl_file);
		atl_file = NULL;
	}

	g_hash_table_destroy (watched_values);
	watched_values = NULL;
	g_hash_table_destroy (watchers);
	watchers = NULL;
}

static GnmValue *
atl_last (GnmFuncEvalInfo *ei, GnmValue const * const argv[])
{
	WatchedValue *val = watched_value_fetch (value_peek_string (argv[0]));

	Watcher key;
	key.node = ei->func_call;
	key.dep = ei->pos->dep;

	g_return_val_if_fail (val != NULL,
		value_new_error_NA (ei->pos));

	/* If caller wants to be notified of updates */
	if (key.node != NULL && key.dep != NULL) {
		Watcher *w = g_hash_table_lookup (watchers, &key);
		if (w == NULL) {
			w = g_new (Watcher, 1);
			key.value = val;
			*w = key;
			g_hash_table_insert (watchers, w, w);
			g_hash_table_insert (w->value->deps, w, w);
		} else if (w->value != val) {
			g_hash_table_remove (w->value->deps, w);
			w->value = val;
			g_hash_table_insert (w->value->deps, w, w);
		}
	}

	if (!val->valid)
		return value_new_error_NA (ei->pos);
	return value_new_float (val->value);
}

static DependentFlags
atl_last_link (GnmFuncEvalInfo *ei)
{
	puts ("link atl_last");
	return DEPENDENT_ALWAYS_UNLINK;
}
static void
atl_last_unlink (GnmFuncEvalInfo *ei)
{
	Watcher key, *w;
	key.node = ei->func_call;
	key.dep = ei->pos->dep;

	w = g_hash_table_lookup (watchers, &key);
	if (w != NULL) {
		if (w->value != NULL)
			g_hash_table_remove (w->value->deps, w);
		g_free (w);
	}
	puts ("unlink atl_last");
}

static GnmFuncHelp const help_atl_last[] = {
	{ GNM_FUNC_HELP_OLD,
        /* xgettext:no-c-format */
	F_("@FUNCTION=atl_last\n"
	   "@SYNTAX=ATL_LAST (tag)\n"
	   "@DESCRIPTION="
	   "ATL_LAST is a sample implemention of a real time data source. "
	   "It takes a string tag and monitors the named pipe /tmp/atl "
	   "for changes to the value of that tag.\n"
	   "\n"
	   "* This is not intended to be generally enabled and is OFF by default.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "ATL_LAST(\"IBM\")"
	   "\n"
	   "@SEEALSO=")
	},
	{ GNM_FUNC_HELP_END }
};

GnmFuncDescriptor const ATL_functions[] = {
	{"atl_last", "s", "tag", help_atl_last, atl_last, NULL, atl_last_link, atl_last_unlink },

	{NULL}
};
