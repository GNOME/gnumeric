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
#include "ranges.h"
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

static void
load_margin (const char *str, PrintUnit *p, char *def)
{
	char *pts_units = g_strconcat (str, "_units=centimeter", NULL);
	char *pts = g_strconcat (str, "=", def, NULL);
	char *s;
	
	p->points = gnome_config_get_float (pts);
	s = gnome_config_get_string (pts_units);
	p->desired_display = unit_name_to_unit (s);
	g_free (pts_units);
	g_free (pts);
	g_free (s);
}

static PrintHF *
load_hf (const char *type, const char *a, const char *b, const char *c)
{
	PrintHF *format;
	char *code_a = g_strconcat (type, "_left=", a, NULL);
	char *code_b = g_strconcat (type, "_middle=", b, NULL);
	char *code_c = g_strconcat (type, "_right=", c, NULL);

	format = g_new (PrintHF, 1);

	format->left_format = gnome_config_get_string (code_a);
	format->middle_format = gnome_config_get_string (code_b);
	format->right_format = gnome_config_get_string (code_c);

	g_free (code_a);
	g_free (code_b);
	g_free (code_c);

	return format;
}

static void
init_invalid_range (Value *v)
{
	v->type = VALUE_CELLRANGE;
	v->v.cell_range.cell_a.sheet = NULL;
	v->v.cell_range.cell_b.sheet = NULL;
	v->v.cell_range.cell_a.col = -1;
	v->v.cell_range.cell_a.row = -1;
	v->v.cell_range.cell_b.col = -1;
	v->v.cell_range.cell_b.row = -1;
}

static const Value *
load_range (const char *name)
{
	static Value v;
	char *str;
       
	str = gnome_config_get_string (name);
	if (!str){
		init_invalid_range (&v);
		return &v;
	}
		
	if (!range_parse (NULL, str, &v))
		init_invalid_range (&v);
	g_free (str);
	
	return &v;
}

#define CENTIMETER_IN_POINTS      "28.346457"
#define HALF_CENTIMETER_IN_POINTS "14.1732285"

/**
 * print_info_new:
 *
 * Returns a newly allocated PrintInformation buffer
 */
PrintInformation *
print_info_new (void)
{
	PrintInformation *pi;
	char *s;
	
	pi = g_new0 (PrintInformation, 1);

	gnome_config_push_prefix ("/Gnumeric/Print");
	
	/* Orientation */
	if (gnome_config_get_bool ("vertical_print=false"))
		pi->orientation = PRINT_ORIENT_HORIZONTAL;
	else
		pi->orientation = PRINT_ORIENT_VERTICAL;

	/* Scaling */
	if (gnome_config_get_bool ("do_scale_percent=true"))
		pi->scaling.type = PERCENTAGE;
	else
		pi->scaling.type = SIZE_FIT;
	pi->scaling.percentage = gnome_config_get_float ("scale_percent=100");
	pi->scaling.dim.cols = gnome_config_get_int ("scale_width=1");
	pi->scaling.dim.rows = gnome_config_get_int ("scale_height=1");

	/* Margins */
	load_margin ("margin_top", &pi->margins.top,       CENTIMETER_IN_POINTS);
	load_margin ("margin_bottom", &pi->margins.bottom, CENTIMETER_IN_POINTS);
	load_margin ("margin_left", &pi->margins.left,     CENTIMETER_IN_POINTS);
	load_margin ("margin_right", &pi->margins.right,   CENTIMETER_IN_POINTS);
	load_margin ("margin_header", &pi->margins.header, HALF_CENTIMETER_IN_POINTS);
	load_margin ("margin_footer", &pi->margins.footer, HALF_CENTIMETER_IN_POINTS);

	pi->header = load_hf ("header", "", _("Sheet &[NUM]"), "");
	pi->footer = load_hf ("footer", "", _("Page &[NUM]"), "");

	s = gnome_config_get_string ("paper=none");
	if (strcmp (s, "none") != 0)
		pi->paper = gnome_paper_with_name (s);

	if (pi->paper == NULL)
		pi->paper = gnome_paper_with_name (gnome_paper_name_default ());
	g_free (s);

	pi->center_horizontally   = gnome_config_get_bool ("center_horizontally=false");
	pi->center_vertically     = gnome_config_get_bool ("center_vertically=false");
	pi->print_line_divisions  = gnome_config_get_bool ("print_divisions=false");
	pi->print_black_and_white = gnome_config_get_bool ("print_black_and_white=false");
	pi->print_titles          = gnome_config_get_bool ("print_titles=false");

	if (gnome_config_get_bool ("order_right=true"))
		pi->print_order = PRINT_ORDER_RIGHT_THEN_DOWN;
	else
		pi->print_order = PRINT_ORDER_DOWN_THEN_RIGHT;

	pi->repeat_top_range =  *load_range ("repeat_top_range=");
	pi->repeat_left_range = *load_range ("repeat_bottom_range=");
		
	gnome_config_pop_prefix ();
	gnome_config_sync ();

	return pi;
}

static void
save_margin (const char *prefix, PrintUnit *p)
{
	char *x = g_strconcat (prefix, "_units");
	
	gnome_config_set_float (prefix, p->points);
	gnome_config_set_string (x, unit_name_get_name (p->desired_display));
	g_free (x);
}

static void
save_range (const char *section, Value *v)
{
	if (v->v.cell_range.cell_a.col == -1)
		gnome_config_set_string (section, "");
	else {
		char *s;

		s = value_cellrange_get_as_string (v, FALSE);
		gnome_config_set_string (section, s);
		g_free (s);
	}
}

void
print_info_save (PrintInformation *pi)
{
	gnome_config_push_prefix ("/Gnumeric/Print/");

	gnome_config_set_bool ("vertical_print", pi->orientation == PRINT_ORIENT_VERTICAL);
	gnome_config_set_bool ("do_scale_percent", pi->scaling.type == PERCENTAGE);
	gnome_config_set_float ("scale_percent", pi->scaling.percentage);
	gnome_config_set_int ("scale_width", pi->scaling.dim.cols);
	gnome_config_set_int ("scale_height", pi->scaling.dim.rows);
	gnome_config_set_string ("paper", gnome_paper_name (pi->paper));

	save_margin ("margin_top", &pi->margins.top);
	save_margin ("margin_bottom", &pi->margins.bottom);
	save_margin ("margin_left", &pi->margins.left);
	save_margin ("margin_right", &pi->margins.right);
	save_margin ("margin_header", &pi->margins.header);
	save_margin ("margin_footer", &pi->margins.footer);

	gnome_config_set_bool ("center_horizontally", pi->center_horizontally);
	gnome_config_set_bool ("center_vertically", pi->center_vertically);

	gnome_config_set_bool ("print_divisions", pi->print_line_divisions);
	gnome_config_set_bool ("print_black_and_white", pi->print_black_and_white);
	gnome_config_set_bool ("print_titles", pi->print_titles);
	gnome_config_set_bool ("order_right", pi->print_order);

	save_range ("repeat_top_range", &pi->repeat_top_range);
	save_range ("repeat_left_range", &pi->repeat_left_range);
	
	gnome_config_pop_prefix ();
	gnome_config_sync ();
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
 
UnitName
unit_name_to_unit (const char *s)
{
	int i;

	for (i = 0; units [i].short_name != NULL; i++){
		if (strcmp (s, units [i].full_name) == 0)
			return (UnitName) i;
	}

	return UNIT_POINTS;
}

