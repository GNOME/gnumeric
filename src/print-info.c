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
	pi->margins.top    = METERS_TO_POINTS (0.01);
	pi->margins.bottom = METERS_TO_POINTS (0.01);
	pi->margins.left   = METERS_TO_POINTS (0.01);
	pi->margins.right  = METERS_TO_POINTS (0.01);
	pi->margins.header = METERS_TO_POINTS (0.005);
	pi->margins.footer = METERS_TO_POINTS (0.005);

	pi->header = print_hf_new (NULL, _("Sheet &[NUM]"), NULL);
	pi->footer = print_hf_new (NULL, _("Page &[NUM]"), NULL);

	pi->paper = gnome_paper_with_name (gnome_paper_name_default ());
	
	return pi;
}
