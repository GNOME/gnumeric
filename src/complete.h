#ifndef GNUMERIC_COMPLETE_H
#define GNUMERIC_COMPLETE_H

#include <glib-object.h>

#define COMPLETE_TYPE        (complete_get_type ())
#define COMPLETE(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), COMPLETE_TYPE, Complete))
#define COMPLETE_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST ((k), COMPLETE_TYPE, CompleteClass))
#define IS_COMPLETE(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), COMPLETE_TYPE))
#define IS_COMPLETE_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), COMPLETE_TYPE))

typedef void (*CompleteMatchNotifyFn) (char const *text, void *closure);

typedef struct {
	GObject parent;

	CompleteMatchNotifyFn notify;
	void *notify_closure;

	char *text;

	guint idle_tag;
} Complete;

typedef struct {
	GObjectClass parent_class;

	void     (*start_over)       (Complete *complete);
	gboolean (*search_iteration) (Complete *complete);
} CompleteClass;

void  complete_construct (Complete *complete,
			  CompleteMatchNotifyFn notify,
			  void *notify_closure);
void  complete_start     (Complete *complete, char const *text);
GType complete_get_type  (void);

#endif /* GNUMERIC_COMPLETE_H */
