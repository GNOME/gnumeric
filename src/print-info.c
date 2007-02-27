/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * print-info.c: Print information management.  This keeps
 * track of what the print parameters for a sheet are.
 *
 * Authors:
 *	Andreas J. Guelzow (aguelzow@taliesin.ca)
 *	Jody Goldberg (jody@gnome.org)
 *	Miguel de Icaza (miguel@gnu.org)
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "print-info.h"

#include "gutils.h"
#include "ranges.h"
#include "gnm-format.h"
#include "func.h"
#include "sheet.h"
#include "value.h"
#include "workbook.h"
#include "gnumeric-gconf.h"
#include "gnumeric-gconf-priv.h"

#include <goffice/app/go-doc.h>
#include <goffice/utils/go-file.h>
#include <goffice/utils/datetime.h>
#include <goffice/utils/go-glib-extras.h>

#include <glib/gi18n-lib.h>
#include <string.h>
#include <locale.h>
#include <time.h>

#define MAX_SAVED_CUSTOM_HF_FORMATS 9

GList *hf_formats = NULL;
static gint hf_formats_base_num = 0;

PrintHF *
print_hf_new (char const *left_side_format,
	      char const *middle_format,
	      char const *right_side_format)
{
	PrintHF *hf = g_new0 (PrintHF, 1);
	hf->left_format   = g_strdup (left_side_format);
	hf->middle_format = g_strdup (middle_format);
	hf->right_format  = g_strdup (right_side_format);
	return hf;
}

gboolean
print_hf_same (PrintHF const *a, PrintHF const *b)
{
	if (a->left_format != b->left_format) {
		if (a->left_format == NULL ||
		    b->left_format == NULL ||
		    strcmp (b->left_format, a->left_format))
			return FALSE;
	}
	if (a->middle_format != b->middle_format) {
		if (a->middle_format == NULL ||
		    b->middle_format == NULL ||
		    strcmp (b->middle_format, a->middle_format))
			return FALSE;
	}
	if (a->right_format != b->right_format) {
		if (a->right_format == NULL ||
		    b->right_format == NULL ||
		    strcmp (b->right_format, a->right_format))
			return FALSE;
	}

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
	g_free (pi->paper_width);
	g_free (pi->paper_height);
	g_free (pi->gp_config_str);
	g_free (pi);
}

static gboolean
load_range (char const *str, GnmRange *r)
{
	return ((str != NULL) &&  range_parse (r, str));
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
	GSList *list;
	PrintInformation *res = g_new0 (PrintInformation, 1);

	res->scaling.type = gnm_app_prefs->print_scale_percentage
		? PRINT_SCALE_PERCENTAGE : PRINT_SCALE_FIT_PAGES;
	res->scaling.percentage.x = res->scaling.percentage.y 
		= gnm_app_prefs->print_scale_percentage_value;
	res->scaling.dim.cols = gnm_app_prefs->print_scale_width;
	res->scaling.dim.rows = gnm_app_prefs->print_scale_height;
	res->margin.top       = gnm_app_prefs->print_margin_top;
	res->margin.bottom    = gnm_app_prefs->print_margin_bottom;
	res->margin.left = res->margin.right = res->margin.header = res->margin.footer = -1.;

	res->repeat_top.use   = load_range (gnm_app_prefs->print_repeat_top,
					    &res->repeat_top.range);
	res->repeat_left.use  = load_range (gnm_app_prefs->print_repeat_left,
					    &res->repeat_left.range);

	res->center_vertically     = gnm_app_prefs->print_center_vertically;
	res->center_horizontally   = gnm_app_prefs->print_center_horizontally;
	res->print_grid_lines      = gnm_app_prefs->print_grid_lines;
	res->print_titles          = gnm_app_prefs->print_titles;
	res->print_black_and_white = gnm_app_prefs->print_black_and_white;
	res->print_as_draft	   = FALSE;
	res->portrait_orientation  = TRUE;

	res->invert_orientation    = FALSE;
	res->print_even_if_only_styles 
		= gnm_app_prefs->print_even_if_only_styles;

	res->print_across_then_down = gnm_app_prefs->print_order_across_then_down;
	res->comment_placement = PRINT_COMMENTS_IN_PLACE;
	res->error_display     = PRINT_ERRORS_AS_DISPLAYED;

	list = (GSList *) gnm_app_prefs->printer_header;
	res->header = list ?
		print_hf_new ((char *)g_slist_nth_data (list, 0),
			      (char *)g_slist_nth_data (list, 1),
			      (char *)g_slist_nth_data (list, 2)) :
		print_hf_new ("", _("&[TAB]"), "");
	list = (GSList *) gnm_app_prefs->printer_footer;
	res->footer = list ?
		print_hf_new ((char *)g_slist_nth_data (list, 0),
			      (char *)g_slist_nth_data (list, 1),
			      (char *)g_slist_nth_data (list, 2)) :
		print_hf_new ("", _("Page &[PAGE]"), "");

	res->n_copies	   = 1;
	res->start_page	   = -1;
	res->gp_config_str = NULL;
	res->paper	   = NULL;
	res->paper_width   = NULL;
	res->paper_height  = NULL;

	return res;
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

		GO_SLIST_PREPEND (left, g_strdup(hf->left_format));
		GO_SLIST_PREPEND (middle, g_strdup(hf->middle_format));
		GO_SLIST_PREPEND (right, g_strdup(hf->right_format));
	}
	GO_SLIST_REVERSE(left);
	GO_SLIST_REVERSE(middle);
	GO_SLIST_REVERSE(right);

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
	GOConfNode *node = go_conf_get_node (gnm_conf_get_root (), PRINTSETUP_GCONF_DIR);

	gnm_gconf_set_print_scale_percentage (pi->scaling.type == PRINT_SCALE_PERCENTAGE);
	gnm_gconf_set_print_scale_percentage_value (pi->scaling.percentage.x);
	go_conf_set_int (node, PRINTSETUP_GCONF_SCALE_WIDTH,  pi->scaling.dim.cols);
	go_conf_set_int (node, PRINTSETUP_GCONF_SCALE_HEIGHT, pi->scaling.dim.rows);

	gnm_gconf_set_print_tb_margins (&pi->margin);

	gnm_gconf_set_print_center_horizontally (pi->center_horizontally);
	gnm_gconf_set_print_center_vertically (pi->center_vertically);
	gnm_gconf_set_print_grid_lines (pi->print_grid_lines);
	gnm_gconf_set_print_titles (pi->print_titles);
	gnm_gconf_set_print_even_if_only_styles (pi->print_even_if_only_styles);
	gnm_gconf_set_print_black_and_white (pi->print_black_and_white);
	gnm_gconf_set_print_order_across_then_down (pi->print_across_then_down);
	
	go_conf_set_string (node, PRINTSETUP_GCONF_REPEAT_TOP,
		pi->repeat_top.use ? range_as_string (&pi->repeat_top.range) : "");
	go_conf_set_string (node, PRINTSETUP_GCONF_REPEAT_LEFT,
		pi->repeat_left.use ? range_as_string (&pi->repeat_left.range) : "");

	save_formats ();

	if (NULL != pi->gp_config_str)
		gnm_gconf_set_printer_config (pi->gp_config_str);
	gnm_gconf_set_printer_header (pi->header->left_format,
				      pi->header->middle_format,
				      pi->header->right_format);
	gnm_gconf_set_printer_footer (pi->footer->left_format,
				      pi->footer->middle_format,
				      pi->footer->right_format);

	go_conf_free_node (node);
}

#ifdef WITH_GNOME_PRINT
const GnomePrintUnit *
unit_name_to_unit (char const *name)
{
	GList *units = gnome_print_unit_get_list (GNOME_PRINT_UNITS_ALL);
	GList *l;
	GnomePrintUnit const *res = NULL;

	for (l = units; l; l = l->next) {
		GnomePrintUnit const *u = l->data;
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
unit_convert (double value, GnomePrintUnit const *src, GnomePrintUnit const *dst)
{
	gboolean ok = gnome_print_convert_distance (&value, src, dst);
	g_assert (ok);
	return value;
}
#endif /* WITH_GNOME_PRINT */

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
	GOFormat *format;

	/* TODO : Check this assumption.  Is it a localized format ?? */
	format = go_format_new_from_XL (number_format);
	format_value_gstring (target, format, info->date_time, NULL, -1, NULL);
	go_format_unref (format);
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
		char *name = go_basename_from_uri (
			go_doc_get_uri (GO_DOC (info->sheet->workbook)));
		g_string_append (target, name);
		g_free (name);
	} else 
		g_string_append (target, _("File Name"));
}

static void
render_path (GString *target, HFRenderInfo *info, char const *args)
{
	if (info->sheet != NULL && info->sheet->workbook != NULL) {
		char *path = go_dirname_from_uri (
			go_doc_get_uri (GO_DOC (info->sheet->workbook)), TRUE);
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

	if (!format)
		return NULL;

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

PrintInformation *
print_info_dup (PrintInformation const *src)
{
	PrintInformation *dst = print_info_new ();

	/* clear the refs in the new obj */
	g_free (dst->gp_config_str);
	g_free (dst->paper);
	g_free (dst->paper_width);
	g_free (dst->paper_height);
	print_hf_free (dst->header);
	print_hf_free (dst->footer);

	*dst = *src; /* bit bash */

	/* setup the refs for new content */
	dst->gp_config_str = g_strdup (src->gp_config_str);
	dst->paper	   = g_strdup (src->paper);
	dst->paper_width   = g_strdup (src->paper_width);
	dst->paper_height  = g_strdup (src->paper_height);
	dst->header	   = print_hf_copy (src->header);
	dst->footer	   = print_hf_copy (src->footer);

	return dst;
}

void
print_info_get_margins (PrintInformation const *pi,
			double *top, double *bottom, double *left, double *right)
{
	g_return_if_fail (pi != NULL);

	if (NULL != top)
		*top = (pi->margin.header > 0.) ? pi->margin.header : 0.;
	if (NULL != bottom)
		*bottom = (pi->margin.footer > 0.) ? pi->margin.footer : 0.;
	if (NULL != left)
		*left = (pi->margin.left > 0.) ? pi->margin.left : 0.;
	if (NULL != right)
		*right = (pi->margin.right > 0.) ? pi->margin.right : 0.;
}

void
print_info_set_margin_header (PrintInformation *pi, double header)
{
	g_return_if_fail (pi != NULL);
	pi->margin.header = header;
}
void
print_info_set_margin_footer (PrintInformation *pi, double footer)
{
	g_return_if_fail (pi != NULL);
	pi->margin.footer = footer;
}
void
print_info_set_margin_left (PrintInformation *pi, double left)
{
	g_return_if_fail (pi != NULL);
	pi->margin.left = left;
}
void
print_info_set_margin_right (PrintInformation *pi, double right)
{
	g_return_if_fail (pi != NULL);
	pi->margin.right = right;
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
print_info_set_paper (PrintInformation *pi, char const *paper)
{
	g_return_if_fail (pi != NULL);
	g_free (pi->paper);
	pi->paper = g_strdup (paper);
}
void
print_info_set_paper_width (PrintInformation *pi, char const *paper_width)
{
	g_return_if_fail (pi != NULL);
	g_free (pi->paper_width);
	pi->paper_width = g_strdup (paper_width);
}
void
print_info_set_paper_height (PrintInformation *pi, char const *paper_height)
{
	g_return_if_fail (pi != NULL);
	g_free (pi->paper_height);
	pi->paper_height = g_strdup (paper_height);
}
char const *
print_info_get_paper (PrintInformation const *pi)
{
	g_return_val_if_fail (pi != NULL, "A4");
	return pi->paper;
}
char const *
print_info_get_paper_width (PrintInformation const *pi)
{
	g_return_val_if_fail (pi != NULL, "210mm");
	return pi->paper_width;
}
char const *
print_info_get_paper_height (PrintInformation const *pi)
{
	g_return_val_if_fail (pi != NULL, "297mm");
	return pi->paper_height;
}

#ifdef WITH_GNOME_PRINT
GnomePrintConfig *
print_info_make_config (PrintInformation const *pi)
{
	GnomePrintConfig *res = (NULL != pi->gp_config_str)
		? gnome_print_config_from_string (pi->gp_config_str, 0)
		: ((NULL != gnm_app_prefs->printer_config)
			? gnome_print_config_from_string (gnm_app_prefs->printer_config, 0)
			: gnome_print_config_default ());

	if (NULL != pi->paper) {
		gnome_print_config_set (res, GNOME_PRINT_KEY_PAPER_SIZE, pi->paper);
	} else {
		if ((NULL != pi->paper_width) && (NULL != pi->paper_height)) {
			gnome_print_config_set (res, GNOME_PRINT_KEY_PAPER_SIZE, "Custom");
			gnome_print_config_set (res, GNOME_PRINT_KEY_PAPER_WIDTH, pi->paper_width);
			gnome_print_config_set (res, GNOME_PRINT_KEY_PAPER_HEIGHT, pi->paper_height);
		}
	}
	if (pi->margin.header >= 0)
		gnome_print_config_set_length (res, GNOME_PRINT_KEY_PAGE_MARGIN_TOP,
			pi->margin.header, GNOME_PRINT_PS_UNIT);
	if (pi->margin.footer >= 0)
		gnome_print_config_set_length (res, GNOME_PRINT_KEY_PAGE_MARGIN_BOTTOM,
			pi->margin.footer, GNOME_PRINT_PS_UNIT);
	if (pi->margin.left >= 0)
		gnome_print_config_set_length (res, GNOME_PRINT_KEY_PAGE_MARGIN_LEFT,
			pi->margin.left, GNOME_PRINT_PS_UNIT);
	if (pi->margin.right >= 0)
		gnome_print_config_set_length (res, GNOME_PRINT_KEY_PAGE_MARGIN_RIGHT,
			pi->margin.right, GNOME_PRINT_PS_UNIT);

	gnome_print_config_set_int (res, GNOME_PRINT_KEY_NUM_COPIES, pi->n_copies);
	gnome_print_config_set (res, GNOME_PRINT_KEY_ORIENTATION, pi->portrait_orientation
		? (pi->invert_orientation ? "R180" : "R0")
		: (pi->invert_orientation ? "R180" : "R90"));

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
		pi->margin.header = d_tmp;
	if (gnome_print_config_get_length (config, GNOME_PRINT_KEY_PAGE_MARGIN_BOTTOM, &d_tmp, NULL))
		pi->margin.footer = d_tmp;
	if (gnome_print_config_get_length (config, GNOME_PRINT_KEY_PAGE_MARGIN_LEFT, &d_tmp, NULL))
		pi->margin.left = d_tmp;
	if (gnome_print_config_get_length (config, GNOME_PRINT_KEY_PAGE_MARGIN_RIGHT, &d_tmp, NULL))
		pi->margin.right = d_tmp;

	if (gnome_print_config_get_int (config, GNOME_PRINT_KEY_NUM_COPIES, &tmp))
		pi->n_copies = tmp;
	else
		pi->n_copies = 1;

	g_free (pi->paper);
	pi->paper = gnome_print_config_get (config, GNOME_PRINT_KEY_PAPER_SIZE);

	str = gnome_print_config_get (config, GNOME_PRINT_KEY_ORIENTATION);
	if (str != NULL) {
		if (strcmp (str, "R0") == 0) {
			pi->portrait_orientation = TRUE;
			pi->invert_orientation   = FALSE;
		} else if (strcmp (str, "R180") == 0) {
			pi->portrait_orientation = TRUE;
			pi->invert_orientation   = TRUE;
		} else if (strcmp (str, "R90") == 0) {
			pi->portrait_orientation = FALSE;
			pi->invert_orientation   = FALSE;
		} else if (strcmp (str, "R270") == 0) {
			pi->portrait_orientation = FALSE;
			pi->invert_orientation   = TRUE;
		}
		g_free (str);
	}
}
#endif /* WITH_GNOME_PRINT */
