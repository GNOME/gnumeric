#ifndef GO_FORMAT_IMPL_H
#define GO_FORMAT_IMPL_H

#include <goffice/utils/format.h>
#include <goffice/utils/numbers.h>

G_BEGIN_DECLS

typedef struct {
        char const *format;
        char        restriction_type;
        double	    restriction_value;
	GOColor     go_color;

	gboolean    want_am_pm;
	gboolean    has_fraction;
	gboolean    suppress_minus;
	gboolean    elapsed_time;
} StyleFormatEntry;

void fmt_general_int	(GString *result, int val, int col_width);
void fmt_general_float	(GString *result, gnm_float val, double col_width);
void format_number	(GString *result,
			 gnm_float number, int col_width, StyleFormatEntry const *entry,
			 GODateConventions const *date_conv);

G_END_DECLS

#endif /* GO_FORMAT_IMPL_H */
