/*
 * print-info.c: Print information management.  This keeps
 * track of what the print parameters for a sheet are.
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *
 */
#include <config.h>
#include <gnome.h>
#include <stdio.h>
#include "gnumeric.h"
#include "print-info.h"

PrintHF *
print_hf_new (char *left_side_format, char *middle_format, char *right_side_format)
{
	PrintHF *format;

	format = g_new0 (PrintHF, 1);

	if (left_side_format)
		format->left_format = g_strdup (left_side_format);

	if (middle_format)
		format->middle_format = g_strdup (middle_format);

	if (right_side_format)
		format->right_format = g_strdup (right_side_format);

	return format;
}

void
print_hf_free (PrintHF *print_hf)
{
	g_return_if_fail (print_hf != NULL);
	
	g_free (print_hf);
}

void
print_info_free (PrintInformation *pi)
{
	g_return_if_fail (pi != NULL);

	print_hf_free (pi->header);
	print_hf_free (pi->footer);
	
	g_free (pi);
}

static PrintUnit
print_unit_new (UnitName unit, double value)
{
	PrintUnit u;

	u.points = unit_convert (value, unit, UNIT_POINTS);
	u.desired_display = unit;

	return u;
}

/**
 * print_info_new:
 *
 * Returns a newly allocated PrintInformation buffer
 */
PrintInformation *
print_info_new (void)
{
	PrintInformation *pi;

	pi = g_new0 (PrintInformation, 1);

	/* Orientation */
	pi->orientation = PRINT_ORIENT_VERTICAL;

	/* Scaling */
	pi->scaling.type = PERCENTAGE;
	pi->scaling.percentage = 100.0;

	/* Margins */
	pi->margins.top    = print_unit_new (UNIT_CENTIMETER, 1);
	pi->margins.bottom = print_unit_new (UNIT_CENTIMETER, 1);
	pi->margins.left   = print_unit_new (UNIT_CENTIMETER, 1);
	pi->margins.right  = print_unit_new (UNIT_CENTIMETER, 1);
	pi->margins.header = print_unit_new (UNIT_CENTIMETER, 0.5);
	pi->margins.footer = print_unit_new (UNIT_CENTIMETER, 0.5);

	pi->header = print_hf_new (NULL, _("Sheet &[NUM]"), NULL);
	pi->footer = print_hf_new (NULL, _("Page &[NUM]"), NULL);

	pi->paper = gnome_paper_with_name (gnome_paper_name_default ());

	return pi;
}

static struct {
	char   *short_name;
	char   *full_name;
	double factor;
} units [] = {
	{ N_("pts"), N_("points"),     1.0 },
	{ N_("mm"),  N_("millimeter"), 2.8346457 },
	{ N_("cm"),  N_("centimeter"), 28.346457 },
	{ N_("In"),  N_("inch"),       72.0 },
	{ NULL, NULL, 0.0 }
};

const char *
unit_name_get_short_name (UnitName name)
{
	g_assert (name >= UNIT_POINTS && name < UNIT_LAST);

	return _(units [name].short_name);
}

const char *
unit_name_get_name (UnitName name)
{
	g_assert (name >= UNIT_POINTS && name < UNIT_LAST);

	return _(units [name].full_name);
}

double
print_unit_get_prefered (PrintUnit *unit)
{
	g_assert (unit != NULL);
	g_assert (unit->desired_display >= UNIT_POINTS && unit->desired_display < UNIT_LAST);

	return units [unit->desired_display].factor * unit->points;
}

double
unit_convert (double value, UnitName source, UnitName target)
{
	g_assert (source >= UNIT_POINTS && source < UNIT_LAST);
	g_assert (target >= UNIT_POINTS && target < UNIT_LAST);

	if (source == target)
		return value;

	return (units [source].factor * value) / units [target].factor;
}
 
