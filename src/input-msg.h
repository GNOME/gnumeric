#ifndef _GNM_INPUT_MSG_H_
# define _GNM_INPUT_MSG_H_

#include <gnumeric.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define GNM_INPUT_MSG_TYPE	(gnm_input_msg_get_type ())
#define GNM_INPUT_MSG(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_INPUT_MSG_TYPE, GnmInputMsg))
#define GNM_IS_INPUT_MSG(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_INPUT_MSG_TYPE))

GType	     gnm_input_msg_get_type  (void);
GnmInputMsg *gnm_input_msg_new	     (char const *msg, char const *title);
char const  *gnm_input_msg_get_msg   (GnmInputMsg const *msg);
char const  *gnm_input_msg_get_title (GnmInputMsg const *msg);

gboolean     gnm_input_msg_equal     (GnmInputMsg const *a,
				      GnmInputMsg const *b);

G_END_DECLS

#endif /* _GNM_INPUT_MSG_H_ */
