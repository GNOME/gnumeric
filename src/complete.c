/*
 * complete.c: Our auto completion engine.  This is an abstract class
 * that must be derived to implement its actual functionality.
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org)
 *
 * Theory of operation:
 *
 *    Derived types of Complete provide the search function.
 *
 *    The search function should not take too long to run, and try to
 *    search on each step information on its data repository.  When the
 *    data repository information has been extenuated or if a match has
 *    been found, then the method should return FALSE and invoke the
 *    notification function that was provided to Complete.
 *
 *
 * (C) 2000-2001 Ximain Inc.
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "complete.h"

#include <gal/util/e-util.h>
#include <gtk/gtkmain.h>
#include <stdio.h>
#include <stdlib.h>

#define PARENT_TYPE (gtk_object_get_type ())
#define ACC(o) (COMPLETE_CLASS (GTK_OBJECT (o)->klass))

static GtkObjectClass *parent_class;

void
complete_construct (Complete *complete,
		    CompleteMatchNotifyFn notify,
		    void *notify_closure)
{
	complete->notify = notify;
	complete->notify_closure = notify_closure;
}

static void
complete_destroy (GtkObject *object)
{
	Complete *complete = COMPLETE (object);

	if (complete->idle_tag) {
		gtk_idle_remove (complete->idle_tag);
		complete->idle_tag = 0;
	}

	if (complete->text) {
		g_free (complete->text);
		complete->text = 0;
	}

	if (parent_class->destroy)
		(parent_class->destroy) (object);
}

static gint
complete_idle (gpointer data)
{
	Complete *complete = data;

	if (complete->idle_tag == 0){
		abort ();
	}

	if (ACC(complete)->search_iteration (complete))
		return TRUE;

	complete->idle_tag = 0;

	return FALSE;
}

void
complete_start (Complete *complete, const char *text)
{
	g_return_if_fail (complete != NULL);
	g_return_if_fail (IS_COMPLETE (complete));
	g_return_if_fail (text != NULL);

	if (complete->text != text) {
		if (complete->text)
			g_free (complete->text);
		complete->text = g_strdup (text);
	}

	if (complete->idle_tag == 0)
		complete->idle_tag = gtk_idle_add (complete_idle, complete);

	if (ACC(complete)->start_over)
		ACC(complete)->start_over (complete);
}

static gboolean
default_search_iteration (Complete *complete)
{
	return FALSE;
}

static void
complete_class_init (GtkObjectClass *object_class)
{
	CompleteClass *complete_class = (CompleteClass *) object_class;

	object_class->destroy = complete_destroy;
	complete_class->search_iteration = default_search_iteration;

	parent_class = gtk_type_class (PARENT_TYPE);
}

E_MAKE_TYPE(complete, "Complete", Complete, &complete_class_init, NULL, PARENT_TYPE);
