/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * print-info.c: Print information management.  This keeps
 * track of what the print parameters for a sheet are.
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *
 */
#include <gnumeric-config.h>
#include <glib/gi18n.h>
#include "gnumeric.h"
#include "print-info.h"

#include "gutils.h"
#include "ranges.h"
#include "format.h"
#include "func.h"
#include "datetime.h"
#include "sheet.h"
#include "value.h"
#include "workbook.h"
#include "gnumeric-gconf.h"
#include "gnumeric-gconf-priv.h"
#include <goffice/utils/go-file.h>

#include <string.h>
#include <locale.h>

#define MAX_SAVED_CUSTOM_HF_FORMATS 9

GList *hf_formats = NULL;
gint   hf_formats_base_num = 0;

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

	g_free (pi->paper);
	g_free (pi->gp_config_str);
	g_free (pi);
}

static gboolean
load_range (char const *str, GnmRange *r)
{
	return ((str != NULL) &&  parse_range (str, r));
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

	/* Fetch header/footer formats */
	{
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
			hf_formats_base_num++;
		}
	} 

	/* Now append the custom formats */
	{
		GSList const *left;
		GSList const *middle;
		GSList const *right;
		
		left = gnm_app_prefs->printer_header_formats_left;
		middle = gnm_app_prefs->printer_header_formats_middle;
		right = gnm_app_prefs->printer_header_formats_right;

		while (left != NULL && middle != NULL && right != NULL)
		{
			PrintHF *format;

			format = print_hf_new 
				(left->data ? (char *)(left->data) : "",
				 middle->data ? (char *)(middle->data) : "",
				 right->data ? (char *)(right->data) : "");

			hf_formats = g_list_prepend (hf_formats, format);
			
			left = left->next;
			middle = middle->next;
			right = right->next;
		}
	}

	hf_formats = g_list_reverse (hf_formats);
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
	GSList *list;

	pi = g_new0 (PrintInformation, 1);

	/* Scaling */
	if (gnm_app_prefs->print_scale_percentage)
		pi->scaling.type = PERCENTAGE;
	else
		pi->scaling.type = SIZE_FIT;
	pi->scaling.percentage.x 
		= pi->scaling.percentage.y 
		= gnm_app_prefs->print_scale_percentage_value;
	pi->scaling.dim.cols = gnm_app_prefs->print_scale_width;
	pi->scaling.dim.rows = gnm_app_prefs->print_scale_height;

	pi->center_horizontally       
		= gnm_app_prefs->print_center_horizontally;
	pi->center_vertically     = gnm_app_prefs->print_center_vertically;
	pi->print_grid_lines      = gnm_app_prefs->print_grid_lines;
	pi->print_even_if_only_styles 
		= gnm_app_prefs->print_even_if_only_styles;
	pi->print_black_and_white = gnm_app_prefs->print_black_and_white;
	pi->print_titles          = gnm_app_prefs->print_titles;

	if (gnm_app_prefs->print_order_right_then_down)
		pi->print_order = PRINT_ORDER_RIGHT_THEN_DOWN;
	else
		pi->print_order = PRINT_ORDER_DOWN_THEN_RIGHT;

	pi->margins = gnm_app_prefs->print_tb_margins;

	list = (GSList *) gnm_app_prefs->printer_header;
	pi->header = list ?
		print_hf_new ((char *)g_slist_nth_data (list, 0),
			      (char *)g_slist_nth_data (list, 1),
			      (char *)g_slist_nth_data (list, 2)) :
		print_hf_new ("", _("&[TAB]"), "");
	list = (GSList *) gnm_app_prefs->printer_footer;
	pi->footer = list ?
		print_hf_new ((char *)g_slist_nth_data (list, 0),
			      (char *)g_slist_nth_data (list, 1),
			      (char *)g_slist_nth_data (list, 2)) :
		print_hf_new ("", _("Page &[PAGE]"), "");

	/* Load the columns/rows to repeat */
	pi->repeat_top.use  = load_range (gnm_app_prefs->print_repeat_top,
					  &pi->repeat_top.range);
	pi->repeat_left.use = load_range (gnm_app_prefs->print_repeat_left,
					  &pi->repeat_left.range);

	pi->orientation	  = PRINT_ORIENT_VERTICAL;
	pi->n_copies	  = 1;
	pi->gp_config_str = NULL;
	pi->paper	  = NULL;
	return pi;
}

/*
 *   This can get out of hand, we should limit the number of stored
 * formats.
 */
static void
save_formats (void)
{
	int base = hf_formats_base_num;
	GList *l;
	GSList *left = NULL;
	GSList *middle = NULL;
	GSList *right = NULL;
	int start;

	start = g_list_length (hf_formats) - MAX_SAVED_CUSTOM_HF_FORMATS;
	if (start > base)
		base = start;

	for (l = hf_formats; l; l = l->next) {
		PrintHF *hf = l->data;

		if (base-- > 0)
			continue;

		GNM_SLIST_PREPEND (left, g_strdup(hf->left_format));
		GNM_SLIST_PREPEND (middle, g_strdup(hf->middle_format));
		GNM_SLIST_PREPEND (right, g_strdup(hf->right_format));
	}
	GNM_SLIST_REVERSE(left);
	GNM_SLIST_REVERSE(middle);
	GNM_SLIST_REVERSE(right);

	gnm_gconf_set_print_header_formats (left, middle, right);
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
	gnm_gconf_set_print_scale_percentage (pi->scaling.type == PERCENTAGE);
	gnm_gconf_set_print_scale_percentage_value (pi->scaling.percentage.x);
	go_conf_set_int (PRINTSETUP_GCONF_SCALE_WIDTH,  pi->scaling.dim.cols);
	go_conf_set_int (PRINTSETUP_GCONF_SCALE_HEIGHT, pi->scaling.dim.rows);

	gnm_gconf_set_print_tb_margins (&pi->margins);

	gnm_gconf_set_print_center_horizontally (pi->center_horizontally);
	gnm_gconf_set_print_center_vertically (pi->center_vertically);
	gnm_gconf_set_print_grid_lines (pi->print_grid_lines);
	gnm_gconf_set_print_even_if_only_styles (pi->print_even_if_only_styles);
	gnm_gconf_set_print_black_and_white (pi->print_black_and_white);
	gnm_gconf_set_print_titles (pi->print_titles);
	gnm_gconf_set_print_order_right_then_down (pi->print_order);
	
	go_conf_set_string (PRINTSETUP_GCONF_REPEAT_TOP,
		pi->repeat_top.use ? range_name (&pi->repeat_top.range) : "");
	go_conf_set_string (PRINTSETUP_GCONF_REPEAT_LEFT,
		pi->repeat_left.use ? range_name (&pi->repeat_left.range) : "");

	save_formats ();

	if (NULL != pi->gp_config_str)
		gnm_gconf_set_printer_config (pi->gp_config_str);
	gnm_gconf_set_printer_header (pi->header->left_format,
				      pi->header->middle_format,
				      pi->header->right_format);
	gnm_gconf_set_printer_footer (pi->footer->left_format,
				      pi->footer->middle_format,
				      pi->footer->right_format);

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
		g_string_append (target, _("Sheet"));
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
	GnmFormat *format;

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
	if (info->sheet != NULL && info->sheet->workbook != NULL) {
		char *name = go_basename_from_uri (workbook_get_uri (info->sheet->workbook));
		g_string_append (target, name);
		g_free (name);
	} else 
		g_string_append (target, _("File Name"));
}

static void
render_path (GString *target, HFRenderInfo *info, char const *args)
{
	if (info->sheet != NULL && info->sheet->workbook != NULL) {
#warning "FIXME: we should have go_dirname_from_uri"
		char *path = g_path_get_dirname (workbook_get_uri (info->sheet->workbook));
		g_string_append (target, path);
		g_free (path);
	} else 
		g_string_append (target, _("Path "));
}

static struct {
	char const *name;
	void (*render)(GString *target, HFRenderInfo *info, char const *args);
	char *name_trans;
} render_ops [] = {
	{ N_("tab"),   render_tab   , NULL},
	{ N_("page"),  render_page  , NULL},
	{ N_("pages"), render_pages , NULL},
	{ N_("date"),  render_date  , NULL},
	{ N_("time"),  render_time  , NULL},
	{ N_("file"),  render_file  , NULL},
	{ N_("path"),  render_path  , NULL},
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
	char *opcode_trans;
	int i;

	args = g_utf8_strchr (opcode, -1, ':');
	if (args) {
		*args = 0;
		args++;
	}
	opcode_trans = g_utf8_casefold (opcode, -1);

	for (i = 0; render_ops [i].name; i++) {
		if (render_ops [i].name_trans == NULL) {
			render_ops [i].name_trans = g_utf8_casefold (_(render_ops [i].name), -1);
		}
		
		if ((g_ascii_strcasecmp (render_ops [i].name, opcode) == 0) ||
		    (g_utf8_collate (render_ops [i].name_trans, opcode_trans) == 0)) {
			(*render_ops [i].render)(target, info, args);
		}
	}
	g_free (opcode_trans);
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

	/* Print Scaling */
	dst_pi->scaling.type       = src_pi->scaling.type;
	dst_pi->scaling.percentage = src_pi->scaling.percentage;
	dst_pi->scaling.dim.cols   = src_pi->scaling.dim.cols;
	dst_pi->scaling.dim.rows   = src_pi->scaling.dim.rows;

	/* Margins (note that the others are copied as part of print_config) */
	print_info_margin_copy (&src_pi->margins.top,    &dst_pi->margins.top);
	print_info_margin_copy (&src_pi->margins.bottom, &dst_pi->margins.bottom);
	dst_pi->margins.left	= src_pi->margins.left;
	dst_pi->margins.right	= src_pi->margins.right;
	dst_pi->margins.header	= src_pi->margins.header;
	dst_pi->margins.footer	= src_pi->margins.footer;

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

	dst_pi->orientation = src_pi->orientation;
	dst_pi->n_copies    = src_pi->n_copies;
	g_free (dst_pi->gp_config_str);
	dst_pi->gp_config_str = g_strdup (src_pi->gp_config_str);
	g_free (dst_pi->paper);
	dst_pi->paper = g_strdup (src_pi->paper);

	return dst_pi;
}

void
print_info_get_margins (PrintInformation const *pi,
			double *top, double *bottom, double *left, double *right)
{
	g_return_if_fail (pi != NULL);

	if (NULL != top)
		*top = pi->margins.header;
	if (NULL != bottom)
		*bottom = pi->margins.footer;
	if (NULL != left)
		*left = pi->margins.left;
	if (NULL != right)
		*right = pi->margins.right;
}

void
print_info_set_margin_header (PrintInformation *pi, double header)
{
	g_return_if_fail (pi != NULL);
	pi->margins.header = header;
}
void
print_info_set_margin_footer (PrintInformation *pi, double footer)
{
	g_return_if_fail (pi != NULL);
	pi->margins.footer = footer;
}
void
print_info_set_margin_left (PrintInformation *pi, double left)
{
	g_return_if_fail (pi != NULL);
	pi->margins.left = left;
}
void
print_info_set_margin_right (PrintInformation *pi, double right)
{
	g_return_if_fail (pi != NULL);
	pi->margins.right = right;
}

void
print_info_set_margins (PrintInformation *pi,
			double top, double bottom, double left, double right)
{
	print_info_set_margin_header (pi, top);
	print_info_set_margin_footer (pi, bottom);
	print_info_set_margin_left (pi, left);
	print_info_set_margin_right (pi, right);
}

void        
print_info_set_n_copies (PrintInformation *pi, int copies)
{
	g_return_if_fail (pi != NULL);
	pi->n_copies = copies;
}
guint        
print_info_get_n_copies  (PrintInformation const *pi)
{
	g_return_val_if_fail (pi != NULL, 1);
	return pi->n_copies;
}
void
print_info_set_paper (PrintInformation *pi, char const *paper)
{
	g_return_if_fail (pi != NULL);
	g_free (pi->paper);
	pi->paper = g_strdup (paper);
}
char const *
print_info_get_paper (PrintInformation const *pi)
{
	g_return_val_if_fail (pi != NULL, "A4");
	return pi->paper;
}

void        
print_info_set_orientation (PrintInformation *pi, PrintOrientation orient)
{
	g_return_if_fail (pi != NULL);
	pi->orientation = orient;
}
PrintOrientation
print_info_get_orientation (PrintInformation const *pi)
{
	g_return_val_if_fail (pi != NULL, PRINT_ORIENT_VERTICAL);
	return pi->orientation;
}

GnomePrintConfig *
print_info_make_config (PrintInformation const *pi)
{
	GnomePrintConfig *res = (NULL != pi->gp_config_str)
		? gnome_print_config_from_string (pi->gp_config_str, 0)
		: ((NULL != gnm_app_prefs->printer_config)
			? gnome_print_config_from_string (gnm_app_prefs->printer_config, 0)
			: gnome_print_config_default ());

	if (NULL != pi->paper)
		gnome_print_config_set (res, GNOME_PRINT_KEY_PAPER_SIZE, pi->paper);
	gnome_print_config_set_length (res, GNOME_PRINT_KEY_PAGE_MARGIN_TOP,
		pi->margins.header, GNOME_PRINT_PS_UNIT);
	gnome_print_config_set_length (res, GNOME_PRINT_KEY_PAGE_MARGIN_BOTTOM,
		pi->margins.footer, GNOME_PRINT_PS_UNIT);
	gnome_print_config_set_length (res, GNOME_PRINT_KEY_PAGE_MARGIN_LEFT,
		pi->margins.left, GNOME_PRINT_PS_UNIT);
	gnome_print_config_set_length (res, GNOME_PRINT_KEY_PAGE_MARGIN_RIGHT,
		pi->margins.right, GNOME_PRINT_PS_UNIT);
	gnome_print_config_set_int (res, GNOME_PRINT_KEY_NUM_COPIES, pi->n_copies);
	switch (pi->orientation) {
	case PRINT_ORIENT_VERTICAL:
		gnome_print_config_set (res, GNOME_PRINT_KEY_ORIENTATION, "R0");
		break;		 
	case PRINT_ORIENT_HORIZONTAL:
		gnome_print_config_set (res, GNOME_PRINT_KEY_ORIENTATION, "R90");
		break;		 
	case PRINT_ORIENT_HORIZONTAL_UPSIDE_DOWN:
		gnome_print_config_set (res, GNOME_PRINT_KEY_ORIENTATION, "R270");
		break;		 
	case PRINT_ORIENT_VERTICAL_UPSIDE_DOWN:
		gnome_print_config_set (res, GNOME_PRINT_KEY_ORIENTATION, "R180");
		break;		 
	}

	return res;
}

void
print_info_load_config (PrintInformation *pi, GnomePrintConfig *config)
{
	guchar *str = NULL;
	double d_tmp;
	int tmp;

	g_return_if_fail (pi != NULL);
	g_return_if_fail (config != NULL);

	g_free (pi->gp_config_str);
	pi->gp_config_str = gnome_print_config_to_string (config, 0);

	if (gnome_print_config_get_length (config, GNOME_PRINT_KEY_PAGE_MARGIN_TOP, &d_tmp, NULL))
		pi->margins.header = d_tmp;
	if (gnome_print_config_get_length (config, GNOME_PRINT_KEY_PAGE_MARGIN_BOTTOM, &d_tmp, NULL))
		pi->margins.footer = d_tmp;
	if (gnome_print_config_get_length (config, GNOME_PRINT_KEY_PAGE_MARGIN_LEFT, &d_tmp, NULL))
		pi->margins.left = d_tmp;
	if (gnome_print_config_get_length (config, GNOME_PRINT_KEY_PAGE_MARGIN_RIGHT, &d_tmp, NULL))
		pi->margins.right = d_tmp;
	if (gnome_print_config_get_int (config, GNOME_PRINT_KEY_NUM_COPIES, &tmp))
		pi->n_copies = tmp;
	else
		pi->n_copies = 1;

	g_free (pi->paper);
	pi->paper = gnome_print_config_get (config, GNOME_PRINT_KEY_PAPER_SIZE);

	str = gnome_print_config_get (config, GNOME_PRINT_KEY_ORIENTATION);
	if (str != NULL) {
		if (strcmp (str, "R0") == 0)
			pi->orientation = PRINT_ORIENT_VERTICAL;
		else if (strcmp (str, "R90") == 0)
			pi->orientation = PRINT_ORIENT_HORIZONTAL;
		else if (strcmp (str, "R180") == 0)
			pi->orientation = PRINT_ORIENT_VERTICAL_UPSIDE_DOWN;
		else if (strcmp (str, "R270") == 0)
			pi->orientation = PRINT_ORIENT_HORIZONTAL_UPSIDE_DOWN;
		g_free (str);
	}
}
