#ifndef GNM_DATETIME_H
#define GNM_DATETIME_H

#include "gnumeric.h"
#include <goffice/utils/numbers.h>
#include <goffice/utils/datetime.h>

G_BEGIN_DECLS

/* These do not round and produces fractional values, i.e., includes time.  */
gnm_float datetime_value_to_serial_raw (GnmValue const *v, GODateConventions const *conv);

/* These are date-only, no time.  */
gboolean datetime_value_to_g		(GDate *res, GnmValue const *v, GODateConventions const *conv);
int      datetime_value_to_serial	(GnmValue const *v, GODateConventions const *conv);

/* These are time-only assuming a 24h day.  It probably loses completely on */
/* days with summer time ("daylight savings") changes.  */
int datetime_value_to_seconds (GnmValue const *v);

int     annual_year_basis  (GnmValue const *value_date, basis_t basis,
			    GODateConventions const *date_conv);
gnm_float yearfrac (GDate const *from, GDate const *to, basis_t basis);


G_END_DECLS

#endif /* GNM_DATETIME_H */
