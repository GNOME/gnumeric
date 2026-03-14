#ifndef GNM_COMPLETE_H_
#define GNM_COMPLETE_H_

#include <glib-object.h>
#include <gnumeric-fwd.h>

G_BEGIN_DECLS

#define GNM_COMPLETE_TYPE        (gnm_complete_get_type ())
#define GNM_COMPLETE(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_COMPLETE_TYPE, GnmComplete))
#define GNM_COMPLETE_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST ((k), GNM_COMPLETE_TYPE, GnmCompleteClass))
#define GNM_IS_COMPLETE(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_COMPLETE_TYPE))

struct GnmComplete_ {
	GObject parent;

	char *text;

	guint idle_tag;
};

typedef struct {
	GObjectClass parent_class;

	void     (*start_over)       (GnmComplete *complete);
	gboolean (*search_iteration) (GnmComplete *complete);
} GnmCompleteClass;

void  gnm_complete_start     (GnmComplete *complete, char const *text);
GType gnm_complete_get_type  (void);

G_END_DECLS

#endif /* GNM_COMPLETE_H_ */
