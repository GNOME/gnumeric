/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_DATETIME_H_
# define _GNM_DATETIME_H_

#include "gnumeric.h"
#include "numbers.h"
#include <goffice/utils/datetime.h>

G_BEGIN_DECLS

/* These do not round and produces fractional values, i.e., includes time.  */
gnm_float datetime_value_to_serial_raw (GnmValue const *v, GODateConventions const *conv);

/* These are date-only, no time.  */
gboolean datetime_value_to_g		(GDate *res, GnmValue const *v, GODateConventions const *conv);
int      datetime_value_to_serial	(GnmValue const *v, GODateConventions const *conv);

int     annual_year_basis  (GnmValue const *value_date, basis_t basis,
			    GODateConventions const *date_conv);
gnm_float yearfrac (GDate const *from, GDate const *to, basis_t basis);

void gnm_date_add_days (GDate *d, int n);
void gnm_date_add_months (GDate *d, int n);
void gnm_date_add_years (GDate *d, int n);

G_END_DECLS

#endif /* _GNM_DATETIME_H_ */
