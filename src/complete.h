#ifndef GNUMERIC_COMPLETE_H
#define GNUMERIC_COMPLETE_H

#include <gtk/gtkobject.h>

#define COMPLETE_TYPE        (complete_get_type ())
#define COMPLETE(o)          (GTK_CHECK_CAST ((o), COMPLETE_TYPE, Complete))
#define COMPLETE_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), COMPLETE_TYPE, CompleteClass))
#define IS_COMPLETE(o)       (GTK_CHECK_TYPE ((o), COMPLETE_TYPE))
#define IS_COMPLETE_CLASS(k) (GTK_CHECK_CLASS_TYPE ((k), COMPLETE_TYPE))

typedef void (*CompleteMatchNotifyFn) (const char *text, void *closure);

typedef struct {
	GtkObject parent;

	CompleteMatchNotifyFn notify;
	void *notify_closure;

	char *text;

	guint idle_tag;
} Complete;

typedef struct {
	GtkObjectClass parent_class;

	void     (*start_over)       (Complete *complete);
	gboolean (*search_iteration) (Complete *complete);
} CompleteClass;

void    complete_construct (Complete *complete,
			    CompleteMatchNotifyFn notify,
			    void *notify_closure);
void    complete_start     (Complete *complete, const char *text);
GtkType complete_get_type  (void);

#endif /* GNUMERIC_COMPLETE_H */
