#ifndef _GNM_AUTO_FORMAT_H_
# define _GNM_AUTO_FORMAT_H_

#include <gnumeric.h>

G_BEGIN_DECLS

GOFormat const *gnm_auto_style_format_suggest (GnmExprTop const *texpr,
					   GnmEvalPos const *epos);

G_END_DECLS

#endif /* _GNM_AUTO_FORMAT_H_ */
