#ifndef GNM_INPUT_MSG_H
#define GNM_INPUT_MSG_H

#include "gnumeric.h"
#include <glib-object.h>

#define GNM_INPUT_MSG_TYPE	(gnm_input_msg_get_type ())
#define GNM_INPUT_MSG(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_INPUT_MSG_TYPE, GnmInputMsg))
#define GNM_IS_INPUT_MSG(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_INPUT_MSG_TYPE))

GType	     gnm_input_msg_get_type (void);
GnmInputMsg *gnm_input_msg_new	    (char const *msg, char const *title);

#endif /* GNM_INPUT_MSG_H */
