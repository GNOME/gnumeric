#ifndef _GNM_DATETIME_H_
# define _GNM_DATETIME_H_

#include <gnumeric.h>
#include <numbers.h>
#include <goffice/goffice.h>

G_BEGIN_DECLS

gboolean gnm_datetime_allow_negative (void);

gnm_float datetime_value_to_serial_raw (GnmValue const *v, GODateConventions const *conv);
int	  datetime_value_to_seconds    (GnmValue const *v, GODateConventions const *conv);

/* These are date-only, no time.  */
gboolean datetime_value_to_g		(GDate *res, GnmValue const *v, GODateConventions const *conv);
int      datetime_value_to_serial	(GnmValue const *v, GODateConventions const *conv);

int     annual_year_basis  (GnmValue const *value_date, GOBasisType basis,
			    GODateConventions const *date_conv);
gnm_float yearfrac (GDate const *from, GDate const *to, GOBasisType basis);

void gnm_date_add_days (GDate *d, int n);
void gnm_date_add_months (GDate *d, int n);
void gnm_date_add_years (GDate *d, int n);

#define GNM_DATE_BASIS_HELP							\
	{ GNM_FUNC_HELP_NOTE, F_("If @{basis} is 0, then the US 30/360 method is used.") }, \
	{ GNM_FUNC_HELP_NOTE, F_("If @{basis} is 1, then actual number of days is used.") }, \
	{ GNM_FUNC_HELP_NOTE, F_("If @{basis} is 2, then actual number of days is used within a month, but years are considered only 360 days.") }, \
	{ GNM_FUNC_HELP_NOTE, F_("If @{basis} is 3, then actual number of days is used within a month, but years are always considered 365 days.") }, \
	{ GNM_FUNC_HELP_NOTE, F_("If @{basis} is 4, then the European 30/360 method is used.") },


G_END_DECLS

#endif /* _GNM_DATETIME_H_ */
