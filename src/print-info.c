/*
 * print-info.c: Print information management.  This keeps
 * track of what the print parameters for a sheet are.
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "print-info.h"

#include "ranges.h"
#include "format.h"
#include "func.h"
#include "datetime.h"
#include "sheet.h"

#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-config.h>
#include <string.h>

static const PrintHF predefined_formats [] = {
	{ "",                 "",                             "" },
	{ "",                 N_("Page &[PAGE]"),             "" },
	{ "",                 N_("Page &[PAGE] of &[PAGES]"), "" },
	{ "",                 N_("&[TAB]"),                   "" },
	{ N_("Page &[PAGE]"), N_("&[TAB]"),                   "" },
	{ N_("Page &[PAGE]"), N_("&[TAB]"),                   N_("&[DATE]") },
	{ "",                 N_("&[DATE]"),                  "" },
	{ N_("&[TAB]"),       N_("Page &[PAGE] of &[PAGES]"), N_("&[DATE]") },
	{ NULL, }
};

GList *hf_formats = NULL;

PrintHF *
print_hf_new (const char *left_side_format,
	      const char *middle_format,
	      const char *right_side_format)
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

gboolean
print_hf_same (const PrintHF *a, const PrintHF *b)
{
	if (strcmp (b->left_format, a->left_format))
		return FALSE;

	if (strcmp (b->middle_format, a->middle_format))
		return FALSE;

	if (strcmp (b->right_format, a->right_format))
		return FALSE;

	return TRUE;
}

PrintHF *
print_hf_register (PrintHF *hf)
{
	GList *l;
	PrintHF *newi;

	g_return_val_if_fail (hf != NULL, NULL);

	for (l = hf_formats; l; l = l->next)
		if (print_hf_same (hf, l->data))
			return l->data;

	newi = print_hf_copy (hf);
	hf_formats = g_list_append (hf_formats, newi);

	return newi;
}


PrintHF *
print_hf_copy (const PrintHF *source)
{
	PrintHF *res;

	res = g_new0 (PrintHF, 1);
	res->left_format = g_strdup (source->left_format);
	res->middle_format = g_strdup (source->middle_format);
	res->right_format = g_strdup (source->right_format);

	return res;
}

void
print_hf_free (PrintHF *print_hf)
{
	g_return_if_fail (print_hf != NULL);

	g_free (print_hf->left_format);
	g_free (print_hf->middle_format);
	g_free (print_hf->right_format);
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

#if 0
static PrintUnit
print_unit_new (UnitName unit, double value)
{
	PrintUnit u;

	u.points = unit_convert (value, unit, UNIT_POINTS);
	u.desired_display = unit;

	return u;
}
#endif

static void
load_margin (const char *str, PrintUnit *p, char *def)
{
	char *pts_units = g_strconcat (str, "_units=centimeter", NULL);
	char *pts = g_strconcat (str, "=", def, NULL);
	char *s;

	p->points = gnome_config_get_float (pts);
	s = gnome_config_get_string (pts_units);
	p->desired_display = unit_name_to_unit (s, FALSE);
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

	format->left_format   = gnome_config_get_string (code_a);
	format->middle_format = gnome_config_get_string (code_b);
	format->right_format  = gnome_config_get_string (code_c);

	g_free (code_a);
	g_free (code_b);
	g_free (code_c);

	return format;
}

static gboolean
load_range (char const *name, Range *r)
{
	gboolean success = FALSE;
	char *str = gnome_config_get_string (name);
	if (str != NULL) {
		success = parse_range (str, r);
		g_free (str);
	}
	return success;
}

static void
load_formats (void)
{
	int format_count;

	/* Fetch header/footer formats */
	gnome_config_push_prefix ("/Gnumeric/Headers_and_Footers/");

	format_count = gnome_config_get_int ("formats=0");
	if (format_count == 0) {
		int i;

		for (i = 0; predefined_formats [i].left_format; i++) {
			PrintHF *format;

			format = print_hf_new (
				predefined_formats [i].left_format[0]?
				_(predefined_formats [i].left_format):"",
				predefined_formats [i].middle_format[0]?
				_(predefined_formats [i].middle_format):"",
				predefined_formats [i].right_format[0]?
				_(predefined_formats [i].right_format):"");

			hf_formats = g_list_prepend (hf_formats, format);
		}
	} else {
		int i;

		for (i = 0; i < format_count; i++) {
			char *str = g_strdup_printf ("FormatHF-%d", i);
			PrintHF *format;

			format = load_hf (str, "", "", "");
			hf_formats = g_list_prepend (hf_formats, format);

			g_free (str);
		}
	}

	hf_formats = g_list_reverse (hf_formats);
	gnome_config_pop_prefix ();
}

/**
 * print_info_new:
 *
 * Returns a newly allocated PrintInformation buffer
 *
 * NOTE: This reads from a globally stored configuration. If a
 *       configuration is stored along with a sheet then that will
 *       override these global defaults.
 */
PrintInformation *
print_info_new (void)
{
	PrintInformation *pi;
	char *s;

	pi = g_new0 (PrintInformation, 1);

	gnome_config_push_prefix ("/Gnumeric/Print/");

	pi->n_copies = gnome_config_get_int ("num_copies=1");

	pi->orientation = gnome_config_get_bool ("vertical_print=false")
		? PRINT_ORIENT_VERTICAL : PRINT_ORIENT_HORIZONTAL;

	/* Scaling */
	if (gnome_config_get_bool ("do_scale_percent=true"))
		pi->scaling.type = PERCENTAGE;
	else
		pi->scaling.type = SIZE_FIT;
	pi->scaling.percentage = gnome_config_get_float ("scale_percent=100");
	pi->scaling.dim.cols = gnome_config_get_int ("scale_width=1");
	pi->scaling.dim.rows = gnome_config_get_int ("scale_height=1");

	/* Margins */
	s = g_strdup_printf ("%.13g", unit_convert (1.0, UNIT_CENTIMETER, UNIT_POINTS));
	load_margin ("margin_top", &pi->margins.top,       s);
	load_margin ("margin_bottom", &pi->margins.bottom, s);
	load_margin ("margin_left", &pi->margins.left,     s);
	load_margin ("margin_right", &pi->margins.right,   s);
	g_free (s);
	s = g_strdup_printf ("%.13g", unit_convert (0.5, UNIT_CENTIMETER, UNIT_POINTS));
	load_margin ("margin_header", &pi->margins.header, s);
	load_margin ("margin_footer", &pi->margins.footer, s);
	g_free (s);

	pi->header = load_hf ("header", "", _("&[TAB]"), "");
	pi->footer = load_hf ("footer", "", _("Page &[PAGE]"), "");

	s = gnome_config_get_string ("paper=none");
	if (strcmp (s, "none") != 0)
		pi->paper = gnome_print_paper_get_by_name (s);

	if (pi->paper == NULL)
		pi->paper = gnome_print_paper_get_default ();
	g_free (s);

	pi->center_horizontally       = gnome_config_get_bool ("center_horizontally=false");
	pi->center_vertically         = gnome_config_get_bool ("center_vertically=false");
	pi->print_grid_lines          = gnome_config_get_bool ("print_grid_lines=false");
	pi->print_even_if_only_styles = gnome_config_get_bool ("print_even_if_only_styles=false");
	pi->print_black_and_white     = gnome_config_get_bool ("print_black_and_white=false");
	pi->print_titles              = gnome_config_get_bool ("print_titles=false");

	if (gnome_config_get_bool ("order_right=true"))
		pi->print_order = PRINT_ORDER_RIGHT_THEN_DOWN;
	else
		pi->print_order = PRINT_ORDER_DOWN_THEN_RIGHT;

	/* Load the columns/rows to repeat */
	pi->repeat_top.use  = load_range ("repeat_top_range=",
					  &pi->repeat_top.range);
	pi->repeat_left.use = load_range ("repeat_left_range=",
					  &pi->repeat_left.range);

	gnome_config_pop_prefix ();
	gnome_config_sync ();

	return pi;
}

static void
save_margin (const char *prefix, PrintUnit *p)
{
	char *x = g_strconcat (prefix, "_units", NULL);

	gnome_config_set_float (prefix, p->points);
	gnome_config_set_string (x, unit_name_get_name (p->desired_display, FALSE));
	g_free (x);
}

static void
save_range (const char *section, PrintRepeatRange *repeat)
{
	char const *s = (repeat->use) ? range_name (&repeat->range) : "";
	gnome_config_set_string (section, s);
}

static void
save_hf (const char *type, const char *a, const char *b, const char *c)
{
	char *code_a = g_strconcat (type, "_left=", a, NULL);
	char *code_b = g_strconcat (type, "_middle=", b, NULL);
	char *code_c = g_strconcat (type, "_right=", c, NULL);

	gnome_config_set_string (code_a, a);
	gnome_config_set_string (code_b, b);
	gnome_config_set_string (code_c, c);

	g_free (code_a);
	g_free (code_b);
	g_free (code_c);
}

/*
 *   This can get out of hand, we limit the number of stored
 * formats.
 */
static void
save_formats (void)
{
	int format_count, i;
	GList *l;

	gnome_config_push_prefix ("/Gnumeric/Headers_and_Footers/");

	format_count = g_list_length (hf_formats);
	gnome_config_set_int ("formats", format_count);

	for (i = 0, l = hf_formats; l; l = l->next, i++){
		PrintHF *hf = l->data;
		char *name;

		name = g_strdup_printf ("FormatHF-%d", i);
		save_hf (name, hf->left_format, hf->middle_format, hf->right_format);
		g_free (name);
	}

	gnome_config_pop_prefix ();
}

static void
destroy_formats (void)
{
	while (hf_formats) {
		print_hf_free (hf_formats->data);
		hf_formats = g_list_remove (hf_formats,
					    hf_formats->data);
	}
	hf_formats = NULL;
}

void
print_info_save (PrintInformation *pi)
{
	gnome_config_push_prefix ("/Gnumeric/Print/");

	gnome_config_set_int ("num_copies", pi->n_copies);
	gnome_config_set_bool ("vertical_print", pi->orientation == PRINT_ORIENT_VERTICAL);
	gnome_config_set_bool ("do_scale_percent", pi->scaling.type == PERCENTAGE);
	gnome_config_set_float ("scale_percent", pi->scaling.percentage);
	gnome_config_set_int ("scale_width", pi->scaling.dim.cols);
	gnome_config_set_int ("scale_height", pi->scaling.dim.rows);
	gnome_config_set_string ("paper", pi->paper->name);

	save_margin ("margin_top", &pi->margins.top);
	save_margin ("margin_bottom", &pi->margins.bottom);
	save_margin ("margin_left", &pi->margins.left);
	save_margin ("margin_right", &pi->margins.right);
	save_margin ("margin_header", &pi->margins.header);
	save_margin ("margin_footer", &pi->margins.footer);

	gnome_config_set_bool ("center_horizontally", pi->center_horizontally);
	gnome_config_set_bool ("center_vertically", pi->center_vertically);

	gnome_config_set_bool ("print_grid_lines", pi->print_grid_lines);
	gnome_config_set_bool ("print_even_if_only_styles", pi->print_even_if_only_styles);
	gnome_config_set_bool ("print_black_and_white", pi->print_black_and_white);
	gnome_config_set_bool ("print_titles", pi->print_titles);
	gnome_config_set_bool ("order_right", pi->print_order);

	save_range ("repeat_top_range", &pi->repeat_top);
	save_range ("repeat_left_range", &pi->repeat_left);
	gnome_config_pop_prefix ();

	save_formats ();

	gnome_config_sync ();
}

static struct {
	const char *short_name;
	const char *full_name;
	double factor;
} units [UNIT_LAST + 1] = {
	{ N_("pts"), N_("points"),     1.0 },
	{ N_("mm"),  N_("millimeter"), 72.0 / 25.4 },  /* 1in = 25.4mm, exact */
	{ N_("cm"),  N_("centimeter"), 72.0 / 2.54 },
	{ N_("In"),  N_("inch"),       72.0 },
	{ NULL, NULL, 0.0 }
};

/*
 * unit_name_get_short_name :
 * @unit : The unit.
 * @translated : Should the name be translated
 *
 * Returns the optionally translated short name of the @unit.
 */
const char *
unit_name_get_short_name (UnitName name, gboolean translated)
{
	g_assert (name >= UNIT_POINTS && name < UNIT_LAST);

	return translated
		? _(units [name].short_name)
		:   units [name].short_name;
}

/*
 * unit_name_get_name :
 * @unit : The unit.
 * @translated : Should the name be translated
 *
 * Returns the optionally translated standard name of the @unit.
 */
const char *
unit_name_get_name (UnitName name, gboolean translated)
{
	g_assert (name >= UNIT_POINTS && name < UNIT_LAST);

	return translated
		? _(units [name].full_name)
		:   units [name].full_name;
}

/*
 * unit_name_to_unit :
 * @str : A string with a unit name.
 * @translated : Is @str localized.
 *
 * Returns the unit associated with the possiblely translated @str.
 */
UnitName
unit_name_to_unit (const char *s, gboolean translated)
{
	int i;

	for (i = 0; units [i].full_name != NULL; i++){
		if (translated) {
			if (strcmp (s, _(units [i].full_name)) == 0)
				return (UnitName) i;
		} else {
			if (strcmp (s, units [i].full_name) == 0)
				return (UnitName) i;
		}
	}

	return UNIT_POINTS;
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

static void
render_tab (GString *target, HFRenderInfo *info, const char *args)
{
	if (info->sheet)
		g_string_append (target, info->sheet->name_unquoted);
	else
		g_string_append (target, _("Sheet 1"));
}

static void
render_page (GString *target, HFRenderInfo *info, const char *args)
{
	g_string_sprintfa (target, "%d", info->page);
}

static void
render_pages (GString *target, HFRenderInfo *info, const char *args)
{
	g_string_sprintfa (target, "%d", info->pages);
}

static void
render_value_with_format (GString *target, const char *number_format, HFRenderInfo *info)
{
	StyleFormat *format;
	char *text;

	/* TODO : Check this assumption.  Is it a localized format ?? */
	format = style_format_new_XL (number_format, FALSE);

	text = format_value (format, info->date_time, NULL, -1);

	/* Just in case someone tries to format it as text */
	g_return_if_fail (text != NULL);

	g_string_append (target, text);
	g_free (text);
	style_format_unref (format);
}

static void
render_date (GString *target, HFRenderInfo *info, const char *args)
{
	const char *date_format;

	if (args)
		date_format = args;
	else
		date_format = "dd-mmm-yyyy";

	render_value_with_format (target, date_format, info);
}

static void
render_time (GString *target, HFRenderInfo *info, const char *args)
{
	const char *time_format;

	if (args)
		time_format = args;
	else
		time_format = "hh:mm";
	render_value_with_format (target, time_format, info);
}

static const struct {
	const char *name;
	void (*render)(GString *target, HFRenderInfo *info, const char *args);
} render_ops [] = {
	{ N_("tab"),   render_tab   },
	{ N_("page"),  render_page  },
	{ N_("pages"), render_pages },
	{ N_("date"),  render_date  },
	{ N_("time"),  render_time  },
	{ NULL },
};

/*
 * Renders an opcode.  The opcodes can take an argument by adding trailing ':'
 * to the opcode and then a number format code
 */
static void
render_opcode (GString *target, const char *opcode, HFRenderInfo *info, HFRenderType render_type)
{
	char *args;
	int i;

	for (i = 0; render_ops [i].name; i++){
		if (render_type == HF_RENDER_TO_ENGLISH){
			if (g_strcasecmp (_(render_ops [i].name), opcode) == 0){
				g_string_append (target, render_ops [i].name);
				continue;
			}
		}

		if (render_type == HF_RENDER_TO_LOCALE){
			if (g_strcasecmp (render_ops [i].name, opcode) == 0){
				g_string_append (target, render_ops [i].name);
				continue;
			}
		}

		/*
		 * opcode then comes from a the user interface
		 */
		args = strchr (opcode, ':');
		if (args){
			*args = 0;
			args++;
		}

		if ((g_strcasecmp (render_ops [i].name, opcode) == 0) ||
		    (g_strcasecmp (_(render_ops [i].name), opcode) == 0)){
			(*render_ops [i].render)(target, info, args);
		}

	}
}

char *
hf_format_render (const char *format, HFRenderInfo *info, HFRenderType render_type)
{
	GString *result;
	const char *p;
	char *str;

	g_return_val_if_fail (format != NULL, NULL);

	result = g_string_new ("");
	for (p = format; *p; p++){
		if (*p == '&' && *(p+1) == '['){
			const char *start;

			p += 2;
			start = p;
			while (*p && (*p != ']'))
				p++;

			if (*p == ']'){
				char *operation = g_malloc (p - start + 1);

				strncpy (operation, start, p - start);
				operation [p-start] = 0;
				render_opcode (result, operation, info, render_type);
				g_free (operation);
			} else
				break;
		} else
			g_string_append_c (result, *p);
	}

	str = result->str;
	g_string_free (result, FALSE);

	return str;
}

HFRenderInfo *
hf_render_info_new (void)
{
	HFRenderInfo *hfi;

	hfi = g_new0 (HFRenderInfo, 1);
	hfi->date_time = value_new_float (datetime_timet_to_serial_raw (time (NULL)));

	return hfi;
}

void
hf_render_info_destroy (HFRenderInfo *hfi)
{
	g_return_if_fail (hfi != NULL);

	value_release (hfi->date_time);
	g_free (hfi);
}

void
print_init (void)
{
	load_formats ();
}

void
print_shutdown (void)
{
	save_formats ();
	destroy_formats ();
}


static void
print_info_margin_copy (PrintUnit *src_print_unit, PrintUnit *dst_print_unit)
{
	dst_print_unit->points = src_print_unit->points;
	dst_print_unit->desired_display = dst_print_unit->desired_display;
}

PrintInformation *
print_info_copy (PrintInformation *src_pi)
{
	PrintInformation *dst_pi;

	dst_pi = print_info_new();

	dst_pi->orientation        = src_pi->orientation;

	/* Print Scaling */
	dst_pi->scaling.type       = src_pi->scaling.type;
	dst_pi->scaling.percentage = src_pi->scaling.percentage;
	dst_pi->scaling.dim.cols   = src_pi->scaling.dim.cols;
	dst_pi->scaling.dim.rows   = src_pi->scaling.dim.rows;

	/* Margins */
	print_info_margin_copy (&src_pi->margins.top,    &dst_pi->margins.top);
	print_info_margin_copy (&src_pi->margins.bottom, &dst_pi->margins.bottom);
	print_info_margin_copy (&src_pi->margins.left,   &dst_pi->margins.left);
	print_info_margin_copy (&src_pi->margins.right,  &dst_pi->margins.right);
	print_info_margin_copy (&src_pi->margins.header, &dst_pi->margins.header);
	print_info_margin_copy (&src_pi->margins.footer, &dst_pi->margins.footer);

	/* Booleans */
	dst_pi->center_vertically	  = src_pi->center_vertically;
	dst_pi->center_horizontally	  = src_pi->center_horizontally;
	dst_pi->print_grid_lines	  = src_pi->print_grid_lines;
	dst_pi->print_even_if_only_styles = src_pi->print_even_if_only_styles;
	dst_pi->print_black_and_white	  = src_pi->print_black_and_white;
	dst_pi->print_as_draft		  = src_pi->print_as_draft;
	dst_pi->print_titles		  = src_pi->print_titles;
	dst_pi->print_order		  = src_pi->print_order;

	/* Headers & Footers */
	print_hf_free (dst_pi->header);
	dst_pi->header = print_hf_copy (src_pi->header);
	print_hf_free (dst_pi->footer);
	dst_pi->footer = print_hf_copy (src_pi->footer);

	/* Paper */
	dst_pi->paper = gnome_print_paper_get_by_name (src_pi->paper->name);

	/* Repeat Range */
	dst_pi->repeat_top  = src_pi->repeat_top;
	dst_pi->repeat_left = src_pi->repeat_left;

	return dst_pi;
}
