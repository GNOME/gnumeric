/*
 * print-info.c: Print information management.  This keeps
 * track of what the print parameters for a sheet are.
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *
 */
#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include "gnumeric.h"
#include "print-info.h"

#include "ranges.h"
#include "format.h"
#include "func.h"
#include "datetime.h"
#include "sheet.h"
#include "value.h"
#include "workbook.h"
#include "gnumeric-gconf.h"

#include <libgnome/gnome-config.h>
#include <string.h>

GList *hf_formats = NULL;

PrintHF *
print_hf_new (char const *left_side_format,
	      char const *middle_format,
	      char const *right_side_format)
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
print_hf_same (PrintHF const *a, PrintHF const *b)
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
print_hf_copy (PrintHF const *source)
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
	gnome_print_config_unref(pi->print_config);

	g_free (pi);
}

static void
load_margin (char const *str, PrintUnit *p, char *def)
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
load_hf (char const *type, char const *a, char const *b, char const *c)
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
	static struct {
		char const *left_format;
		char const *middle_format;
		char const *right_format;
	} const predefined_formats [] = {
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

	pi->print_config = gnome_print_config_default ();

	/* FIXME: The print_config default configuration is probably not right! */
	/* Specifically we should load the default paper size and formats       */

	gnome_config_push_prefix ("/Gnumeric/Print/");

	pi->n_copies = gnome_config_get_int ("num_copies=1");

	pi->orientation = gnome_config_get_bool ("vertical_print=false")
		? PRINT_ORIENT_VERTICAL : PRINT_ORIENT_HORIZONTAL;

	/* Scaling */
	if (gnome_config_get_bool ("do_scale_percent=true"))
		pi->scaling.type = PERCENTAGE;
	else
		pi->scaling.type = SIZE_FIT;
	pi->scaling.percentage.x = pi->scaling.percentage.y = gnome_config_get_float ("scale_percent=100");
	pi->scaling.dim.cols = gnome_config_get_int ("scale_width=1");
	pi->scaling.dim.rows = gnome_config_get_int ("scale_height=1");

	/* Margins */
	s = g_strdup_printf ("%.13g", unit_convert (1.0,
						    gnome_print_unit_get_by_abbreviation ("cm"),
						    gnome_print_unit_get_by_abbreviation ("pt")));
	load_margin ("margin_top", &pi->margins.top,       s);
	load_margin ("margin_bottom", &pi->margins.bottom, s);
	g_free (s);

	pi->header = load_hf ("header", "", _("&[TAB]"), "");
	pi->footer = load_hf ("footer", "", _("Page &[PAGE]"), "");

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
save_margin (char const *prefix, PrintUnit const *p)
{
	char *x = g_strconcat (prefix, "_units", NULL);

	gnome_config_set_float (prefix, p->points);
	gnome_config_set_string (x, p->desired_display->name);
	g_free (x);
}

static void
save_range (char const *section, PrintRepeatRange const *repeat)
{
	char const *s = (repeat->use) ? range_name (&repeat->range) : "";
	gnome_config_set_string (section, s);
}

static void
save_hf (char const *type, char const *a, char const *b, char const *c)
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

	for (i = 0, l = hf_formats; l; l = l->next, i++) {
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
print_info_save (PrintInformation const *pi)
{
	/* FIXME: The print_config  configuration should be saved               */
	/* Specifically we should save the default paper size and formats       */

	gnome_config_push_prefix ("/Gnumeric/Print/");

	gnome_config_set_int ("num_copies", pi->n_copies);
	gnome_config_set_bool ("vertical_print", pi->orientation == PRINT_ORIENT_VERTICAL);
	gnome_config_set_bool ("do_scale_percent", pi->scaling.type == PERCENTAGE);
	gnome_config_set_float ("scale_percent", pi->scaling.percentage.x);
	gnome_config_set_int ("scale_width", pi->scaling.dim.cols);
	gnome_config_set_int ("scale_height", pi->scaling.dim.rows);

	save_margin ("margin_top", &pi->margins.top);
	save_margin ("margin_bottom", &pi->margins.bottom);

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

	gnm_gconf_set_printer_config
		(gnome_print_config_to_string (pi->print_config,
					       0));

	gnome_config_sync ();
}

const GnomePrintUnit *
unit_name_to_unit (const char *name)
{
	GList *units = gnome_print_unit_get_list (GNOME_PRINT_UNITS_ALL);
	GList *l;
	const GnomePrintUnit *res = NULL;

	for (l = units; l; l = l->next) {
		const GnomePrintUnit *u = l->data;
		if (g_ascii_strcasecmp (name, u->name) == 0 ||
		    g_ascii_strcasecmp (name, u->plural) == 0 ||
		    g_ascii_strcasecmp (name, u->abbr) == 0 ||
		    g_ascii_strcasecmp (name, u->abbr_plural) == 0) {
			res = u;
			break;
		}
	}

	g_list_free (units);
	return res;
}

double
unit_convert (double value, const GnomePrintUnit *src, const GnomePrintUnit *dst)
{
	gboolean ok = gnome_print_convert_distance (&value, src, dst);
	g_assert (ok);
	return value;
}

static void
render_tab (GString *target, HFRenderInfo *info, char const *args)
{
	if (info->sheet)
		g_string_append (target, info->sheet->name_unquoted);
	else
		g_string_append (target, _("Sheet 1"));
}

static void
render_page (GString *target, HFRenderInfo *info, char const *args)
{
	g_string_append_printf (target, "%d", info->page);
}

static void
render_pages (GString *target, HFRenderInfo *info, char const *args)
{
	g_string_append_printf (target, "%d", info->pages);
}

static void
render_value_with_format (GString *target, char const *number_format, HFRenderInfo *info)
{
	StyleFormat *format;

	/* TODO : Check this assumption.  Is it a localized format ?? */
	format = style_format_new_XL (number_format, FALSE);
	format_value_gstring (target, format, info->date_time, NULL, -1, NULL);
	style_format_unref (format);
}

static void
render_date (GString *target, HFRenderInfo *info, char const *args)
{
	char const *date_format;

	if (args)
		date_format = args;
	else
		date_format = "dd-mmm-yyyy";

	render_value_with_format (target, date_format, info);
}

static void
render_time (GString *target, HFRenderInfo *info, char const *args)
{
	char const *time_format;

	if (args)
		time_format = args;
	else
		time_format = "hh:mm";
	render_value_with_format (target, time_format, info);
}

static void
render_file (GString *target, HFRenderInfo *info, char const *args)
{
	if (info->sheet != NULL && info->sheet->workbook != NULL)
		g_string_append (target,
			workbook_get_filename (info->sheet->workbook));
}

static struct {
	char const *name;
	void (*render)(GString *target, HFRenderInfo *info, char const *args);
} const render_ops [] = {
	{ N_("tab"),   render_tab   },
	{ N_("page"),  render_page  },
	{ N_("pages"), render_pages },
	{ N_("date"),  render_date  },
	{ N_("time"),  render_time  },
	{ N_("file"),  render_file  },
	{ NULL },
};

/*
 * Renders an opcode.  The opcodes can take an argument by adding trailing ':'
 * to the opcode and then a number format code
 */
static void
render_opcode (GString *target, char /* non-const */ *opcode,
	       HFRenderInfo *info, HFRenderType render_type)
{
	char *args;
	int i;

	for (i = 0; render_ops [i].name; i++) {
		if (render_type == HF_RENDER_TO_ENGLISH) {
			if (g_ascii_strcasecmp (_(render_ops [i].name), opcode) == 0) {
				g_string_append (target, render_ops [i].name);
				continue;
			}
		}

		if (render_type == HF_RENDER_TO_LOCALE) {
			if (g_ascii_strcasecmp (render_ops [i].name, opcode) == 0) {
				g_string_append (target, render_ops [i].name);
				continue;
			}
		}

		/*
		 * opcode then comes from a the user interface
		 */
		args = strchr (opcode, ':');
		if (args) {
			*args = 0;
			args++;
		}

		if ((g_ascii_strcasecmp (render_ops [i].name, opcode) == 0) ||
		    (g_ascii_strcasecmp (_(render_ops [i].name), opcode) == 0)) {
			(*render_ops [i].render)(target, info, args);
		}

	}
}

char *
hf_format_render (char const *format, HFRenderInfo *info, HFRenderType render_type)
{
	GString *result;
	char const *p;

	g_return_val_if_fail (format != NULL, NULL);

	result = g_string_new (NULL);
	for (p = format; *p; p++) {
		if (*p == '&' && p[1] == '[') {
			char const *start;

			p += 2;
			start = p;
			while (*p && (*p != ']'))
				p++;

			if (*p == ']') {
				char *operation = g_strndup (start, p - start);
				render_opcode (result, operation, info, render_type);
				g_free (operation);
			} else
				break;
		} else
			g_string_append_c (result, *p);
	}

	return g_string_free (result, FALSE);
}

HFRenderInfo *
hf_render_info_new (void)
{
	HFRenderInfo *hfi;

	hfi = g_new0 (HFRenderInfo, 1);
	hfi->date_time = value_new_float (
		datetime_timet_to_serial_raw (time (NULL), NULL));

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
print_info_margin_copy (PrintUnit const *src_print_unit, PrintUnit *dst_print_unit)
{
	dst_print_unit->points = src_print_unit->points;
	dst_print_unit->desired_display = src_print_unit->desired_display;
}

PrintInformation *
print_info_dup (PrintInformation const *src_pi)
{
	PrintInformation *dst_pi;

	dst_pi = print_info_new ();

	gnome_print_config_unref (dst_pi->print_config);
	dst_pi->print_config       = gnome_print_config_dup (src_pi->print_config);

	dst_pi->orientation        = src_pi->orientation;

	/* Print Scaling */
	dst_pi->scaling.type       = src_pi->scaling.type;
	dst_pi->scaling.percentage = src_pi->scaling.percentage;
	dst_pi->scaling.dim.cols   = src_pi->scaling.dim.cols;
	dst_pi->scaling.dim.rows   = src_pi->scaling.dim.rows;

	/* Margins (note that the others are copied as part of print_config) */
	print_info_margin_copy (&src_pi->margins.top,    &dst_pi->margins.top);
	print_info_margin_copy (&src_pi->margins.bottom, &dst_pi->margins.bottom);

	/* Booleans */
	dst_pi->center_vertically	  = src_pi->center_vertically;
	dst_pi->center_horizontally	  = src_pi->center_horizontally;
	dst_pi->print_grid_lines	  = src_pi->print_grid_lines;
	dst_pi->print_even_if_only_styles = src_pi->print_even_if_only_styles;
	dst_pi->print_black_and_white	  = src_pi->print_black_and_white;
	dst_pi->print_as_draft		  = src_pi->print_as_draft;
	dst_pi->print_comments		  = src_pi->print_comments;
	dst_pi->print_titles		  = src_pi->print_titles;
	dst_pi->print_order		  = src_pi->print_order;

	/* Headers & Footers */
	print_hf_free (dst_pi->header);
	dst_pi->header = print_hf_copy (src_pi->header);
	print_hf_free (dst_pi->footer);
	dst_pi->footer = print_hf_copy (src_pi->footer);

	/* Repeat Range */
	dst_pi->repeat_top  = src_pi->repeat_top;
	dst_pi->repeat_left = src_pi->repeat_left;

	return dst_pi;
}

gboolean
print_info_get_margins (PrintInformation const *pi,
			double *top, double *bottom, double *left, double *right)
{
	gboolean res_top, res_bottom, res_left, res_right;

	g_return_val_if_fail (pi->print_config != NULL, FALSE);


	res_top = gnome_print_config_get_length (pi->print_config,
						 GNOME_PRINT_KEY_PAGE_MARGIN_TOP,
						 top, NULL);
	res_bottom = gnome_print_config_get_length (pi->print_config,
						    GNOME_PRINT_KEY_PAGE_MARGIN_BOTTOM,
						    bottom, NULL);
	res_left = gnome_print_config_get_length (pi->print_config,
						  GNOME_PRINT_KEY_PAGE_MARGIN_LEFT,
						  left, NULL);
	res_right = gnome_print_config_get_length (pi->print_config,
						   GNOME_PRINT_KEY_PAGE_MARGIN_RIGHT,
						   right, NULL);
	return res_top && res_bottom && res_left && res_right;
}

void
print_info_set_margin_header   (PrintInformation *pi, double top)
{
	g_return_if_fail (pi->print_config != NULL);

	gnome_print_config_set_length (pi->print_config,
				       GNOME_PRINT_KEY_PAGE_MARGIN_TOP,
				       top, GNOME_PRINT_PS_UNIT);
}

void
print_info_set_margin_footer   (PrintInformation *pi, double bottom)
{
	g_return_if_fail (pi->print_config != NULL);

	gnome_print_config_set_length (pi->print_config,
				       GNOME_PRINT_KEY_PAGE_MARGIN_BOTTOM,
				       bottom, GNOME_PRINT_PS_UNIT);
}

void
print_info_set_margin_left     (PrintInformation *pi, double left)
{
	g_return_if_fail (pi->print_config != NULL);

	gnome_print_config_set_length (pi->print_config,
				       GNOME_PRINT_KEY_PAGE_MARGIN_LEFT,
				       left, GNOME_PRINT_PS_UNIT);
}

void
print_info_set_margin_right    (PrintInformation *pi, double right)
{
	g_return_if_fail (pi->print_config != NULL);

	gnome_print_config_set_length (pi->print_config,
				       GNOME_PRINT_KEY_PAGE_MARGIN_RIGHT,
				       right, GNOME_PRINT_PS_UNIT);
}

void
print_info_set_margins (PrintInformation *pi,
			double top, double bottom, double left, double right)
{
	g_return_if_fail (pi->print_config != NULL);

	print_info_set_margin_header (pi, top);
	print_info_set_margin_footer (pi, bottom);
	print_info_set_margin_left (pi, left);
	print_info_set_margin_right (pi, right);
}
