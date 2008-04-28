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

#include "print.h"
#include "gutils.h"
#include "ranges.h"
#include "gnm-format.h"
#include "func.h"
#include "sheet.h"
#include "value.h"
#include "workbook.h"
#include "workbook-view.h"
#include "gnumeric-gconf.h"
#include "gnumeric-gconf-priv.h"
#include "parse-util.h"

#include <goffice/app/go-doc.h>
#include <goffice/app/go-cmd-context.h>
#include <goffice/utils/go-file.h>
#include <goffice/utils/datetime.h>
#include <goffice/utils/go-glib-extras.h>

#include <glib/gi18n-lib.h>
#include <string.h>
#include <locale.h>
#include <time.h>

#define PDF_SAVER_ID "Gnumeric_pdf:pdf_assistant"

#define MAX_SAVED_CUSTOM_HF_FORMATS 9

GList *hf_formats = NULL;
static gint hf_formats_base_num = 0;

PrintHF *
print_hf_new (char const *left_side_format,
	      char const *middle_format,
	      char const *right_side_format)
{
	PrintHF *hf = g_new0 (PrintHF, 1);
	hf->left_format   = g_strdup (left_side_format ? 
				      left_side_format : "");
	hf->middle_format = g_strdup (middle_format ?
				      middle_format : "");
	hf->right_format  = g_strdup (right_side_format ?
				      right_side_format : "");
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
	if (print_hf == NULL)
		return;

	g_free (print_hf->left_format);
	g_free (print_hf->middle_format);
	g_free (print_hf->right_format);
	g_free (print_hf);
}

void
print_info_free (PrintInformation *pi)
{
	g_return_if_fail (pi != NULL);

	if (NULL != pi->page_breaks.v)
		gnm_page_breaks_free (pi->page_breaks.v);
	if (NULL != pi->page_breaks.h)
		gnm_page_breaks_free (pi->page_breaks.h);

	print_hf_free (pi->header);
	print_hf_free (pi->footer);

	if (pi->page_setup)
		g_object_unref (pi->page_setup);

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
				(left->data ? left->data : "",
				 middle->data ? middle->data : "",
				 right->data ? right->data : "");

			hf_formats = g_list_prepend (hf_formats, format);

			left = left->next;
			middle = middle->next;
			right = right->next;
		}
	}

	hf_formats = g_list_reverse (hf_formats);
}

/**
 * print_info_load_defaults:
 *
 *
 * NOTE: This reads from a globally stored configuration. If a
 *       configuration is stored along with a sheet then that will
 *       override these global defaults.
 */

PrintInformation *
print_info_load_defaults (PrintInformation *res)
{
	GSList *list;

	if (res->page_setup != NULL)
		return res;

	res->page_setup = gtk_page_setup_copy (gnm_gconf_get_page_setup ());

	res->scaling.type = gnm_app_prefs->print_scale_percentage
		? PRINT_SCALE_PERCENTAGE : PRINT_SCALE_FIT_PAGES;
	res->scaling.percentage.x = res->scaling.percentage.y
		= gnm_app_prefs->print_scale_percentage_value;
	res->scaling.dim.cols = gnm_app_prefs->print_scale_width;
	res->scaling.dim.rows = gnm_app_prefs->print_scale_height;
	res->edge_to_below_header = gnm_app_prefs->print_margin_top;
	res->edge_to_above_footer = gnm_app_prefs->print_margin_bottom;
	res->desired_display.top = gnm_app_prefs->desired_display;
	res->desired_display.bottom = gnm_app_prefs->desired_display;
	res->desired_display.left = gnm_app_prefs->desired_display;
	res->desired_display.right = gnm_app_prefs->desired_display;
	res->desired_display.footer = gnm_app_prefs->desired_display;
	res->desired_display.header = gnm_app_prefs->desired_display;

	res->repeat_top.use   = load_range (gnm_app_prefs->print_repeat_top,
					    &res->repeat_top.range);
	res->repeat_left.use  = load_range (gnm_app_prefs->print_repeat_left,
					    &res->repeat_left.range);

	res->center_vertically     = gnm_app_prefs->print_center_vertically;
	res->center_horizontally   = gnm_app_prefs->print_center_horizontally;
	res->print_grid_lines      = gnm_app_prefs->print_grid_lines;
	res->print_titles          = gnm_app_prefs->print_titles;
	res->print_black_and_white = gnm_app_prefs->print_black_and_white;
	res->print_even_if_only_styles
		= gnm_app_prefs->print_even_if_only_styles;

	res->print_across_then_down = gnm_app_prefs->print_order_across_then_down;

	list = (GSList *) gnm_app_prefs->printer_header;
	res->header = list ?
		print_hf_new (g_slist_nth_data (list, 0),
			      g_slist_nth_data (list, 1),
			      g_slist_nth_data (list, 2)) :
		print_hf_new ("", _("&[TAB]"), "");
	list = (GSList *) gnm_app_prefs->printer_footer;
	res->footer = list ?
		print_hf_new (g_slist_nth_data (list, 0),
			      g_slist_nth_data (list, 1),
			      g_slist_nth_data (list, 2)) :
		print_hf_new ("", _("Page &[PAGE]"), "");

	return res;
}

/**
 * print_info_new:
 *
 * Returns a newly allocated PrintInformation buffer
 *
 */
PrintInformation *
print_info_new (gboolean load_defaults)
{
	PrintInformation *res = g_new0 (PrintInformation, 1);

	res->print_as_draft	   = FALSE;
	res->comment_placement = PRINT_COMMENTS_IN_PLACE;
	res->error_display     = PRINT_ERRORS_AS_DISPLAYED;

	res->start_page	   = -1;
	res->n_copies	   = 0;
	res->do_not_print   = FALSE;

	res->page_setup = NULL;
	res->page_breaks.v = NULL;
	res->page_breaks.h = NULL;

	if (load_defaults)
		return print_info_load_defaults (res);
	else
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
print_info_save (PrintInformation *pi)
{
	GOConfNode *node = go_conf_get_node (gnm_conf_get_root (), PRINTSETUP_GCONF_DIR);

	gnm_gconf_set_print_scale_percentage (pi->scaling.type == PRINT_SCALE_PERCENTAGE);
	gnm_gconf_set_print_scale_percentage_value (pi->scaling.percentage.x);
	go_conf_set_int (node, PRINTSETUP_GCONF_SCALE_WIDTH,  pi->scaling.dim.cols);
	go_conf_set_int (node, PRINTSETUP_GCONF_SCALE_HEIGHT, pi->scaling.dim.rows);

	gnm_gconf_set_print_tb_margins (pi->edge_to_below_header,
					pi->edge_to_above_footer,
					pi->desired_display.top);

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

	gnm_gconf_set_printer_header (pi->header->left_format,
				      pi->header->middle_format,
				      pi->header->right_format);
	gnm_gconf_set_printer_footer (pi->footer->left_format,
				      pi->footer->middle_format,
				      pi->footer->right_format);

	gnm_gconf_set_page_setup (pi->page_setup);

	go_conf_free_node (node);
}

GtkUnit
unit_name_to_unit (char const *name)
{
	if (!g_ascii_strcasecmp (name, "cm"))
		return GTK_UNIT_MM;
	if (!g_ascii_strcasecmp (name, "mm"))
		return GTK_UNIT_MM;
	if (!g_ascii_strcasecmp (name, "centimeter"))
		return GTK_UNIT_MM;
	if (!g_ascii_strcasecmp (name, "millimeter"))
		return GTK_UNIT_MM;
	if (!g_ascii_strcasecmp (name, "inch"))
		return GTK_UNIT_INCH;
	if (!g_ascii_strcasecmp (name, "in"))
		return GTK_UNIT_INCH;
	if (!g_ascii_strcasecmp (name, "inches"))
		return GTK_UNIT_INCH;

	return GTK_UNIT_POINTS;
}

char const *
unit_to_unit_name (GtkUnit unit)
{
	switch (unit) {
	case GTK_UNIT_MM:
		return "mm";
	case GTK_UNIT_INCH:
		return "inch";
	default:
		return "points";
	}
}


static void
render_cell (GString *target, HFRenderInfo *info, char const *args)
{
	gboolean use_repeating = FALSE;

	if (args && ((use_repeating = g_str_has_prefix (args, "rep|"))))
		args += 4;

	if (info->sheet) {
		GnmRangeRef ref;
		GnmValue const *val;
		char const *tmp;
		GnmParsePos ppos;

		parse_pos_init (&ppos, info->sheet->workbook, (Sheet *)info->sheet, 0, 0);
		tmp = rangeref_parse 
			(&ref, args, &ppos, sheet_get_conventions (info->sheet));
		if (tmp == NULL || tmp == args) {
			gnm_cellref_init (&ref.a, (Sheet *)(info->sheet), 0, 0, FALSE);
		}

		if (ref.a.row_relative)
			ref.a.row += (use_repeating ? 
				      info->top_repeating.row : info->page_area.start.row);
		if (ref.a.col_relative)
			ref.a.col += (use_repeating ? 
				      info->top_repeating.col : info->page_area.start.col);

		val = sheet_cell_get_value 
			(ref.a.sheet ? ref.a.sheet : (Sheet *)(info->sheet), 
			 ref.a.col, ref.a.row);
		if (val != NULL) {
			char const *value;
			value = value_peek_string (val);
			g_string_append (target, value);
		}
	}
	else {
		if (use_repeating)
			g_string_append (target, "[");
		g_string_append (target, args);
		if (use_repeating)
			g_string_append (target, "]");
	}
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
	{ N_("cell"),  render_cell  , NULL},
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
	range_init_full_sheet (&hfi->page_area);
	hfi->top_repeating.col = 0;
	hfi->top_repeating.row = 0;

	return hfi;
}

void
hf_render_info_destroy (HFRenderInfo *hfi)
{
	g_return_if_fail (hfi != NULL);

	value_release (hfi->date_time);
	g_free (hfi);
}

static void
pdf_write_workbook (GOFileSaver const *fs, IOContext *context,
		    gconstpointer wbv_, GsfOutput *output)
{
	WorkbookView const *wbv = wbv_;
	Workbook const *wb = wb_view_get_workbook (wbv);
	GPtrArray *sheets = g_object_get_data (G_OBJECT (wb), "pdf-sheets");

	if (sheets) {
		int i;

		for (i = 0; i < workbook_sheet_count (wb); i++) {
			Sheet *sheet = workbook_sheet_by_index (wb, i);
			sheet->print_info->do_not_print = TRUE;
		}
		for (i = 0; i < (int)sheets->len; i++) {
			Sheet *sheet = g_ptr_array_index (sheets, i);
			sheet->print_info->do_not_print = FALSE;
		}
	}

	gnm_print_sheet (NULL, wb_view_cur_sheet (wbv), FALSE,
			 PRINT_ALL_SHEETS, output);
}

static void
cb_free_sheets (GPtrArray *a)
{
	g_ptr_array_free (a, TRUE);
}

static gboolean
cb_set_pdf_option (const char *key, const char *value,
		   GError **err, gpointer user)
{
	Workbook *wb = user;

	if (strcmp (key, "sheet") == 0) {
		Sheet *sheet = workbook_sheet_by_name (wb, value);
		GPtrArray *sheets;

		if (!sheet) {
			*err = g_error_new (go_error_invalid (), 0,
					    _("There is no such sheet"));
			return TRUE;
		}

		sheets = g_object_get_data (G_OBJECT (wb), "pdf-sheets");
		if (!sheets) {
			sheets = g_ptr_array_new ();
			g_object_set_data_full (G_OBJECT (wb),
						"pdf-sheets", sheets,
						(GDestroyNotify)cb_free_sheets);
		}
		g_ptr_array_add (sheets, sheet);

		return FALSE;
	}

	if (strcmp (key, "paper") == 0) {
		int i;

		for (i = 0; i < workbook_sheet_count (wb); i++) {
			Sheet *sheet = workbook_sheet_by_index (wb, i);
			if (print_info_set_paper (sheet->print_info, value)) {
				*err = g_error_new (go_error_invalid (), 0,
						    _("Unknown paper size"));

				return TRUE;
			}
		}
		return FALSE;
	}

	if (err)
		*err = g_error_new (go_error_invalid (), 0,
				    _("Invalid option for pdf exporter"));

	return TRUE;
}

static gboolean
pdf_set_export_options (GOFileSaver *fs,
			GODoc *doc,
			const char *options,
			GError **err,
			gpointer user)
{
	return go_parse_key_value (options, err, cb_set_pdf_option, doc);
}

void
print_init (void)
{
	/* Install a pdf saver.  */
	GOFileSaver *saver = go_file_saver_new (
		PDF_SAVER_ID, "pdf",
		_("PDF export"),
		FILE_FL_WRITE_ONLY, pdf_write_workbook);
	g_signal_connect (G_OBJECT (saver), "set-export-options",
			  G_CALLBACK (pdf_set_export_options),
			  NULL);
	go_file_saver_register (saver);
	g_object_unref (saver);

	load_formats ();
}

void
print_shutdown (void)
{
	go_file_saver_unregister (go_file_saver_for_id (PDF_SAVER_ID));

	save_formats ();
	destroy_formats ();
}

PrintInformation *
print_info_dup (PrintInformation *src)
{
	PrintInformation *dst = print_info_new (TRUE);

	print_info_load_defaults (src);

	/* clear the refs in the new obj */
	print_hf_free (dst->header);
	print_hf_free (dst->footer);
	if (dst->page_setup)
		g_object_unref (dst->page_setup);

	*dst = *src; /* bit bash */

	dst->page_breaks.v = gnm_page_breaks_dup (src->page_breaks.v);
	dst->page_breaks.h = gnm_page_breaks_dup (src->page_breaks.h);

	/* setup the refs for new content */
	dst->header	   = print_hf_copy (src->header);
	dst->footer	   = print_hf_copy (src->footer);
	dst->page_setup    = gtk_page_setup_copy (src->page_setup);

	return dst;
}

void
print_info_get_margins (PrintInformation *pi,
			double *top, double *bottom,
			double *left, double *right,
			double *edge_to_below_header,
			double *edge_to_above_footer)
{
	g_return_if_fail (pi != NULL);
	print_info_load_defaults (pi);
	g_return_if_fail (pi->page_setup != NULL);

	if (NULL != top)
		*top = gtk_page_setup_get_top_margin (pi->page_setup, GTK_UNIT_POINTS);
	if (NULL != bottom)
		*bottom = gtk_page_setup_get_bottom_margin (pi->page_setup, GTK_UNIT_POINTS);
	if (NULL != left)
		*left = gtk_page_setup_get_left_margin (pi->page_setup, GTK_UNIT_POINTS);
	if (NULL != right)
		*right = gtk_page_setup_get_right_margin (pi->page_setup, GTK_UNIT_POINTS);
	if (NULL != edge_to_below_header)
		*edge_to_below_header = pi->edge_to_below_header;
	if (NULL != edge_to_above_footer)
		*edge_to_above_footer = pi->edge_to_above_footer;
}

void
print_info_set_margin_header (PrintInformation *pi, double header)
{
	g_return_if_fail (pi != NULL);
	print_info_load_defaults (pi);
	g_return_if_fail (pi->page_setup != NULL);

	gtk_page_setup_set_top_margin (pi->page_setup, header, GTK_UNIT_POINTS);
}

void
print_info_set_margin_footer (PrintInformation *pi, double footer)
{
	g_return_if_fail (pi != NULL);
	print_info_load_defaults (pi);
        g_return_if_fail (pi->page_setup != NULL);

        gtk_page_setup_set_bottom_margin (pi->page_setup, footer, GTK_UNIT_POINTS);
}

void
print_info_set_margin_left (PrintInformation *pi, double left)
{
	g_return_if_fail (pi != NULL);
	print_info_load_defaults (pi);
	g_return_if_fail (pi->page_setup != NULL);

	gtk_page_setup_set_left_margin (pi->page_setup, left, GTK_UNIT_POINTS);
}

void
print_info_set_margin_right (PrintInformation *pi, double right)
{
	g_return_if_fail (pi != NULL);
	print_info_load_defaults (pi);
	g_return_if_fail (pi->page_setup != NULL);

	gtk_page_setup_set_right_margin (pi->page_setup, right, GTK_UNIT_POINTS);
}

void
print_info_set_edge_to_above_footer (PrintInformation *pi, double e_f)
{
	g_return_if_fail (pi != NULL);
	print_info_load_defaults (pi);
	g_return_if_fail (pi->page_setup != NULL);

	pi->edge_to_above_footer = e_f;
}

void
print_info_set_edge_to_below_header (PrintInformation *pi, double e_h)
{
	g_return_if_fail (pi != NULL);
	print_info_load_defaults (pi);
	g_return_if_fail (pi->page_setup != NULL);

	pi->edge_to_below_header = e_h;
}


void
print_info_set_margins (PrintInformation *pi,
			double header, double footer, double left, double right)
{
	g_return_if_fail (pi != NULL);
	print_info_load_defaults (pi);
	g_return_if_fail (pi->page_setup != NULL);

	if (header >= 0)
		gtk_page_setup_set_top_margin (pi->page_setup,
					       header, GTK_UNIT_POINTS);
	if (footer >= 0)
		gtk_page_setup_set_bottom_margin (pi->page_setup,
						  footer, GTK_UNIT_POINTS);
	if (left >= 0)
		gtk_page_setup_set_left_margin (pi->page_setup,
						left, GTK_UNIT_POINTS);
	if (right >= 0)
		gtk_page_setup_set_right_margin (pi->page_setup,
						 right, GTK_UNIT_POINTS);
}


static void
paper_log_func (const gchar   *log_domain,
		GLogLevelFlags log_level,
		const gchar   *message,
		gpointer       user_data)
{
	int *pwarn = user_data;

	if (log_level & G_LOG_LEVEL_WARNING)
		*pwarn = 1;
}

gboolean
page_setup_set_paper (GtkPageSetup *page_setup, char const *paper)
{
	GtkPaperSize* gtk_paper;
	int bad_paper = 0;

	g_return_val_if_fail (page_setup != NULL, TRUE);

/* We are now using the standard paper names given by PWG 5101.1-2002 */
/* We are trying to map some old gnome-print paper names.                  */

/*
 "A4" -> GTK_PAPER_NAME_A4
 "USLetter" -> GTK_PAPER_NAME_LETTER
 "USLegal" -> GTK_PAPER_NAME_LEGAL
 "Executive" -> GTK_PAPER_NAME_EXECUTIVE
 "A3" -> GTK_PAPER_NAME_A3
 "A5" -> GTK_PAPER_NAME_A5
 "B5" -> GTK_PAPER_NAME_B5
 *  */

	if (g_ascii_strcasecmp ("A4", paper) == 0)
		paper = GTK_PAPER_NAME_A4;
	else if (g_ascii_strcasecmp ("A3", paper) == 0)
	        paper = GTK_PAPER_NAME_A3;
	else if (g_ascii_strcasecmp ("A5", paper) == 0)
		paper = GTK_PAPER_NAME_A5;
	else if (g_ascii_strcasecmp ("B5", paper) == 0)
		paper = GTK_PAPER_NAME_B5;
	else if (g_ascii_strcasecmp ("USLetter", paper) == 0 ||
		 g_ascii_strcasecmp ("US-Letter", paper) == 0 ||
		 g_ascii_strcasecmp ("Letter", paper) == 0)
		paper = GTK_PAPER_NAME_LETTER;
	else if (g_ascii_strcasecmp ("USLegal", paper) == 0)
		paper = GTK_PAPER_NAME_LEGAL;
	else if (g_ascii_strncasecmp ("Executive", paper, 9) == 0)
		paper = GTK_PAPER_NAME_EXECUTIVE;

	/* Hack: gtk_paper_size_new warns on bad paper, so shut it up.  */
	/* http://bugzilla.gnome.org/show_bug.cgi?id=493880 */
	{
		const char *domain = "Gtk";
		guint handler = g_log_set_handler (domain, G_LOG_LEVEL_WARNING,
						   paper_log_func, &bad_paper);

		gtk_paper = gtk_paper_size_new (paper);
		g_log_remove_handler (domain, handler);
		if (!gtk_paper)
			bad_paper = 1;
	}

	if (!bad_paper)
		gtk_page_setup_set_paper_size (page_setup, gtk_paper);
	if (gtk_paper)
		gtk_paper_size_free (gtk_paper);

	return bad_paper;
}

gboolean
print_info_set_paper (PrintInformation *pi, char const *paper)
{
	g_return_val_if_fail (pi != NULL, TRUE);

	print_info_load_defaults (pi);
	return page_setup_set_paper (pi->page_setup, paper);
}

char *
page_setup_get_paper (GtkPageSetup *page_setup)
{
	GtkPaperSize* paper;
	char const *name;

	g_return_val_if_fail (page_setup != NULL, g_strdup (GTK_PAPER_NAME_A4));

	paper = gtk_page_setup_get_paper_size (page_setup);

	if (gtk_paper_size_is_custom (paper)) {
		double width = gtk_paper_size_get_width (paper, GTK_UNIT_MM);
		double height = gtk_paper_size_get_height (paper, GTK_UNIT_MM);
		return g_strdup_printf ("custom_Gnm-%.0fx%.0fmm_%.0fx%.0fmm",
					width, height, width, height);
	}

	name =  gtk_paper_size_get_name (gtk_page_setup_get_paper_size (page_setup));

/* Working around gtk bug 426416 */
	if (strncmp (name, "custom", 6) == 0) {
		double width = gtk_paper_size_get_width (paper, GTK_UNIT_MM);
		double height = gtk_paper_size_get_height (paper, GTK_UNIT_MM);
		return g_strdup_printf ("custom_Gnm-%.0fx%.0fmm_%.0fx%.0fmm",
					width, height, width, height);
	}
	return g_strdup (name);
}

char  *
print_info_get_paper (PrintInformation *pi)
{
	g_return_val_if_fail (pi != NULL, g_strdup (GTK_PAPER_NAME_A4));
	print_info_load_defaults (pi);

	return page_setup_get_paper (pi->page_setup);
}

char  const*
print_info_get_paper_display_name (PrintInformation *pi)
{
	GtkPaperSize* paper;

	g_return_val_if_fail (pi != NULL, "ERROR: No printinformation specified");
	print_info_load_defaults (pi);
	g_return_val_if_fail (pi->page_setup != NULL, "ERROR: No pagesetup loaded");

	paper = gtk_page_setup_get_paper_size (pi->page_setup);
	return gtk_paper_size_get_display_name (paper);
}

double
print_info_get_paper_width (PrintInformation *pi, GtkUnit unit)
{
	g_return_val_if_fail (pi != NULL, 0.);
	print_info_load_defaults (pi);

	return gtk_page_setup_get_paper_width (pi->page_setup, unit);
}

double
print_info_get_paper_height (PrintInformation *pi, GtkUnit unit)
{
	g_return_val_if_fail (pi != NULL, 0);
	print_info_load_defaults (pi);

	return gtk_page_setup_get_paper_height (pi->page_setup, unit);
}

GtkPageSetup*
print_info_get_page_setup (PrintInformation *pi)
{
	g_return_val_if_fail (pi != NULL, NULL);
	print_info_load_defaults (pi);

	if (pi->page_setup)
		return g_object_ref (pi->page_setup);
	else
		return NULL;
}

void
print_info_set_page_setup (PrintInformation *pi, GtkPageSetup *page_setup)
{
	double header, footer, left, right;

	g_return_if_fail (pi != NULL);
	print_info_load_defaults (pi);

	if (pi->page_setup) {
		g_object_unref (pi->page_setup);
		print_info_get_margins (pi, &header, &footer, &left, &right,
					NULL, NULL);
		pi->page_setup = page_setup;
		print_info_set_margins (pi, header, footer, left, right);
	} else pi->page_setup = page_setup;
}

GtkPageOrientation
print_info_get_paper_orientation (PrintInformation *pi)
{
	GtkPageOrientation orientation;

	g_return_val_if_fail (pi != NULL, GTK_PAGE_ORIENTATION_PORTRAIT);
	print_info_load_defaults (pi);
	g_return_val_if_fail (pi->page_setup != NULL, GTK_PAGE_ORIENTATION_PORTRAIT);

	orientation = gtk_page_setup_get_orientation (pi->page_setup);
	return orientation;
}

void
print_info_set_paper_orientation (PrintInformation *pi,
				  GtkPageOrientation orientation)
{
	g_return_if_fail (pi != NULL);
	print_info_load_defaults (pi);

	gtk_page_setup_set_orientation (pi->page_setup, orientation);
}

/**
 * print_info_set_breaks :
 * @pi : #PrintInformation
 * @breaks : #GnmPageBreaks
 * @is_col : Do @breaks split between columns or rows ?
 *
 * NOTE : Takes ownership of @breaks.  DO NOT FREE after calling.
 **/
void
print_info_set_breaks (PrintInformation *pi,
		       GnmPageBreaks    *breaks)
{
	GnmPageBreaks **target;

	g_return_if_fail (pi != NULL);

	target = breaks->is_vert ? &pi->page_breaks.v : &pi->page_breaks.h;

	if (*target == breaks) /* just in case something silly happens */
		return;

	if (NULL != *target)
		gnm_page_breaks_free (*target);
	*target = breaks;
}

/********************************************************************
 * Simple data structure to store page breaks defined as a wrapper in case we
 * need something more extensive later. */

/**
 * gnm_page_breaks_new :
 * @len : optional estimate of number of breaks that will be in the collection
 * @is_vert :
 *
 * Allocate a collection of page breaks and provide some sanity checking for
 * @len.
 **/
GnmPageBreaks *
gnm_page_breaks_new (int len, gboolean is_vert)
{
	GnmPageBreaks *res = g_new (GnmPageBreaks, 1);

#warning "We cannot use NULL here."
	if (len < 0 || len > colrow_max (is_vert, NULL))
		len = 0;
	res->is_vert = is_vert;
	res->details = g_array_sized_new (FALSE, FALSE,
		sizeof (GnmPageBreak), len);

	return res;
}

GnmPageBreaks *
gnm_page_breaks_dup (GnmPageBreaks const *src)
{
	if (src != NULL) {
		GnmPageBreaks *dst = gnm_page_breaks_new (src->details->len, src->is_vert);
		GArray       *d_details = dst->details;
		GArray const *s_details = src->details;
		unsigned i;

		/* no need to validate through gnm_page_breaks_append_break, just dup */
		for (i = 0; i < s_details->len ; i++)
			g_array_append_val (d_details,
				g_array_index (s_details, GnmPageBreak, i));

		return dst;
	} else
		return NULL;
}

void
gnm_page_breaks_free (GnmPageBreaks *breaks)
{
	g_array_free (breaks->details, TRUE);
	g_free (breaks);
}

gboolean
gnm_page_breaks_append_break (GnmPageBreaks *breaks,
			      int pos,
			      GnmPageBreakType type)
{
	GnmPageBreak const *prev;
	GnmPageBreak info;

	g_return_val_if_fail (breaks != NULL, FALSE);

	/* Do some simple validation */
	if (pos < 0)
		return FALSE;
	if (breaks->details->len > 0) {
		prev = &g_array_index (breaks->details, GnmPageBreak,
			breaks->details->len-1);
		if (prev->pos >= pos)
			return FALSE;
	}

	info.pos   = pos;
	info.type  = type;
	g_array_append_val (breaks->details, info);

	return TRUE;
}

/**
 * gnm_page_break_type_from_str
 * @str :
 *
 **/
GnmPageBreakType
gnm_page_break_type_from_str (char const *str)
{
	if (0 == g_ascii_strcasecmp (str, "manual"))
		return GNM_PAGE_BREAK_MANUAL;
	if (0 == g_ascii_strcasecmp (str, "auto"))
		return GNM_PAGE_BREAK_AUTO;
	if (0 == g_ascii_strcasecmp (str, "data-slice"))
		return GNM_PAGE_BREAK_DATA_SLICE;
	return GNM_PAGE_BREAK_AUTO;
}
