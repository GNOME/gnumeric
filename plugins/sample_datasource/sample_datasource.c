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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */
#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include <gnumeric.h>

#include "func.h"
#include "plugin.h"
#include "value.h"
#include "plugin-util.h"
#include "module-plugin-defs.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "gnumeric:atl"

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

static int    atl_fd = -1;
static FILE  *atl_file = NULL;
static guint  atl_source = 0;
static GHashTable *watched_values = NULL;
static GHashTable *watchers = NULL;

typedef struct {
	char	   *name;
	float	    value;
	gboolean    valid;
	GHashTable *deps;
} WatchedValue;

typedef struct {
	GnmExprFunction const *node;  /* Expression node that calls us */
	Dependent *dep;		   /* Dependent containing that node */

	WatchedValue	*value;
} Watcher;

static guint
watcher_hash (Watcher const *w)
{
	return ((int)w->node << 16) + (int)w->dep;
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
/* quick and dirty data source that reads from a named pipe
 */
#define PIPE_FILE	"/tmp/atl"

static void
cb_watcher_queue_recalc (gpointer key, gpointer value, gpointer closure)
{
	Watcher const *w = key;
	cb_dependent_queue_recalc (w->dep, NULL);
}

static gboolean
cb_atl_input (GIOChannel *gioc, GIOCondition cond, gpointer ignored)
{
	guchar buf[128];

	/* quick format ticker:value\n
	 * there is no notion of a field for now.
	 */
	while (fgets (buf, sizeof (buf), atl_file) != NULL) {
		char *sym = buf;
		char *value_str = strchr (buf, ':');

		if (value_str != NULL) {
			float val;
			char *end;
			*value_str++ = '\0';

			/* pre clear incase something left a mess */
			errno = 0;
			val = strtod (value_str, &end);
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

void
plugin_init (void)
{
	GIOChannel *channel = NULL;

	fprintf (stderr, ">>>>>>>>>>>>>>>>>>>>>>>>>>>> LOAD ATL\n");
	g_return_if_fail (atl_fd < 0);

	/* NOTE : better to use popen here, but this is fine for testing */
	mkfifo (PIPE_FILE, S_IRUSR | S_IWUSR);
	atl_fd = open (PIPE_FILE, O_RDWR|O_NONBLOCK);
	if (atl_fd >= 0) {
		atl_file = fdopen (atl_fd, "r");
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
void
plugin_cleanup (void)
{
	fprintf (stderr, "UNLOAD ATL >>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");
	g_return_if_fail (atl_fd >= 0);

	if (atl_source) {
		g_source_remove (atl_source);
		atl_source = 0;
	}
	if (atl_fd >= 0) {
		close (atl_fd);
		atl_fd = -1;
	}
	if (atl_file != NULL) {
		fclose (atl_file);
		atl_file = NULL;
	}
	unlink (PIPE_FILE);

	g_hash_table_destroy (watched_values);
	watched_values = NULL;
	g_hash_table_destroy (watchers);
	watchers = NULL;
}

static Value *
atl_last (FunctionEvalInfo *ei, Value *argv[])
{
	WatchedValue *val = watched_value_fetch (value_peek_string (argv[0]));

	Watcher key;
	key.node = ei->func_call;
	key.dep = ei->pos->dep;

	g_return_val_if_fail (val != NULL,
		value_new_error (ei->pos, gnumeric_err_NA));

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
		return value_new_error (ei->pos, gnumeric_err_NA);
	return value_new_float (val->value);
}

static DependentFlags
atl_last_link (FunctionEvalInfo *ei)
{
	puts ("link atl_last");
	return DEPENDENT_ALWAYS_UNLINK;
}
static void
atl_last_unlink (FunctionEvalInfo *ei)
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

static const char *help_atl_last = {
        /* xgettext:no-c-format */
	N_("@FUNCTION=atl_last\n"
	   "@SYNTAX=atl_last(tag)\n"
	   "@DESCRIPTION="
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

ModulePluginFunctionInfo ATL_functions[] = {
	{"atl_last", "s", "tag", &help_atl_last, atl_last, NULL, atl_last_link, atl_last_unlink },

	{NULL}
};
