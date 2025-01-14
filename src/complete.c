/*
 * complete.c: Our auto completion engine.  This is an abstract class
 * that must be derived to implement its actual functionality.
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org)
 *
 * Theory of operation:
 *
 *    Derived types of GnmComplete provide the search function.
 *
 *    The search function should not take too long to run, and try to
 *    search on each step information on its data repository.  When the
 *    data repository information has been extenuated or if a match has
 *    been found, then the method should return %FALSE and invoke the
 *    notification function that was provided to GnmComplete.
 *
 *
 * (C) 2000-2001 Ximain Inc.
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include <complete.h>

#include <gsf/gsf-impl-utils.h>
#include <stdlib.h>

#define PARENT_TYPE (G_TYPE_OBJECT)
#define ACC(o) (GNM_COMPLETE_CLASS (G_OBJECT_GET_CLASS (o)))

/**
 * gnm_complete_construct:
 * @complete: #GnmComplete
 * @notify: (scope async): #GnmCompleteMatchNotifyFn
 * @notify_closure: user data
 **/
void
gnm_complete_construct (GnmComplete *complete,
			GnmCompleteMatchNotifyFn notify,
			void *notify_closure)
{
	complete->notify = notify;
	complete->notify_closure = notify_closure;
}

static void
complete_finalize (GObject *object)
{
	GObjectClass *parent;
	GnmComplete *complete = GNM_COMPLETE (object);

	if (complete->idle_tag) {
		g_source_remove (complete->idle_tag);
		complete->idle_tag = 0;
	}

	g_free (complete->text);
	complete->text = NULL;

	parent = g_type_class_peek (PARENT_TYPE);
	parent->finalize (object);
}

static gint
complete_idle (gpointer data)
{
	GnmComplete *complete = data;

	g_return_val_if_fail (complete->idle_tag != 0, FALSE);

	if (ACC(complete)->search_iteration (complete))
		return TRUE;

	complete->idle_tag = 0;

	return FALSE;
}

void
gnm_complete_start (GnmComplete *complete, char const *text)
{
	g_return_if_fail (complete != NULL);
	g_return_if_fail (GNM_IS_COMPLETE (complete));
	g_return_if_fail (text != NULL);

	if (complete->text != text) {
		g_free (complete->text);
		complete->text = g_strdup (text);
	}

	if (complete->idle_tag == 0)
		complete->idle_tag = g_idle_add (complete_idle, complete);

	if (ACC(complete)->start_over)
		ACC(complete)->start_over (complete);
}

static gboolean
default_search_iteration (G_GNUC_UNUSED GnmComplete *complete)
{
	return FALSE;
}

static void
complete_class_init (GObjectClass *object_class)
{
	GnmCompleteClass *complete_class = (GnmCompleteClass *) object_class;

	object_class->finalize = complete_finalize;
	complete_class->search_iteration = default_search_iteration;
}

GSF_CLASS (GnmComplete, gnm_complete,
	   complete_class_init, NULL, PARENT_TYPE)
