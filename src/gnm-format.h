#ifndef GNM_FORMAT_H
#define GNM_FORMAT_H

#include "gnumeric.h"
#include <goffice/utils/format.h>

char  *format_value	    (GOFormat const *format,
			     GnmValue const *value, GOColor *go_color,
			     double col_width, GODateConventions const *date_conv);
void   format_value_gstring (GString *result,
			     GOFormat const *format,
			     GnmValue const *value, GOColor *go_color,
			     double col_width, GODateConventions const *date_conv);

#endif /* GNM_FORMAT_H */
