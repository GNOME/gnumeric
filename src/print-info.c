/*
 * print-info.c: Print information management.  This keeps
 * track of what the print parameters for a sheet are.
 *
 * Authors:
 *	Andreas J. Guelzow (aguelzow@taliesin.ca)
 *	Jody Goldberg (jody@gnome.org)
 *	Miguel de Icaza (miguel@gnu.org)
 * Copyright (C) 2007-2009 Morten Welinder (terra@gnome.org)
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include <print-info.h>

#include <print.h>
#include <gutils.h>
#include <ranges.h>
#include <gnm-format.h>
#include <func.h>
#include <sheet.h>
#include <value.h>
#include <workbook.h>
#include <workbook-view.h>
#include <gnumeric-conf.h>
#include <parse-util.h>
#include <sheet-object.h>
#include <sheet-object-graph.h>

#include <goffice/goffice.h>
#include <gsf/gsf-doc-meta-data.h>
#include <gsf/gsf-meta-names.h>

#include <glib/gi18n-lib.h>
#include <string.h>
#include <locale.h>
#include <time.h>

#define PDF_SAVER_ID "Gnumeric_pdf:pdf_assistant"

#define MAX_SAVED_CUSTOM_HF_FORMATS 9

GList *gnm_print_hf_formats = NULL;
static gint hf_formats_base_num = 0;

GType
gnm_print_comment_placement_get_type (void)
{
	static GType etype = 0;
	if (etype == 0) {
		static GEnumValue const values[] = {
			{ GNM_PRINT_COMMENTS_NONE,
			  "GNM_PRINT_COMMENTS_NONE",
			  "none"},
			{ GNM_PRINT_COMMENTS_IN_PLACE,
			  "GNM_PRINT_COMMENTS_IN_PLACE",
			  "in-place"},
			{ GNM_PRINT_COMMENTS_AT_END,
			  "GNM_PRINT_COMMENTS_AT_END",
			  "at-end"},
			{ 0, NULL, NULL }
		};
		etype = g_enum_register_static ("GnmPrintCommentPlacementType",
						values);
	}
	return etype;
}

GType
gnm_print_errors_get_type (void)
{
	static GType etype = 0;
	if (etype == 0) {
		static GEnumValue const values[] = {
			{ GNM_PRINT_ERRORS_AS_DISPLAYED,
			  "GNM_PRINT_ERRORS_AS_DISPLAYED", "as-displayed"},
			{ GNM_PRINT_ERRORS_AS_BLANK,
			  "GNM_PRINT_ERRORS_AS_BLANK", "as-blank"},
			{ GNM_PRINT_ERRORS_AS_DASHES,
			  "GNM_PRINT_ERRORS_AS_DASHES", "as-dashes"},
			{ GNM_PRINT_ERRORS_AS_NA,
			  "GNM_PRINT_ERRORS_AS_NA", "as-na"},
			{ 0, NULL, NULL }
		};
		etype = g_enum_register_static ("GnmPrintErrorsType",
						values);
	}
	return etype;
}


GnmPrintHF *
gnm_print_hf_new (char const *left_side_format,
		  char const *middle_format,
		  char const *right_side_format)
{
	GnmPrintHF *hf = g_new0 (GnmPrintHF, 1);
	hf->left_format   = g_strdup (left_side_format ?
				      left_side_format : "");
	hf->middle_format = g_strdup (middle_format ?
				      middle_format : "");
	hf->right_format  = g_strdup (right_side_format ?
				      right_side_format : "");
	return hf;
}

gboolean
gnm_print_hf_same (GnmPrintHF const *a, GnmPrintHF const *b)
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

GnmPrintHF *
gnm_print_hf_register (GnmPrintHF *hf)
{
	GList *l;
	GnmPrintHF *newi;

	g_return_val_if_fail (hf != NULL, NULL);

	for (l = gnm_print_hf_formats; l; l = l->next)
		if (gnm_print_hf_same (hf, l->data))
			return l->data;

	newi = gnm_print_hf_copy (hf);
	gnm_print_hf_formats = g_list_append (gnm_print_hf_formats, newi);

	return newi;
}


GnmPrintHF *
gnm_print_hf_copy (GnmPrintHF const *source)
{
	GnmPrintHF *res;

	res = g_new0 (GnmPrintHF, 1);
	res->left_format = g_strdup (source->left_format);
	res->middle_format = g_strdup (source->middle_format);
	res->right_format = g_strdup (source->right_format);

	return res;
}

void
gnm_print_hf_free (GnmPrintHF *print_hf)
{
	if (print_hf == NULL)
		return;

	g_free (print_hf->left_format);
	g_free (print_hf->middle_format);
	g_free (print_hf->right_format);
	g_free (print_hf);
}

GType
gnm_print_hf_get_type (void)
{
	static GType t = 0;

	if (t == 0) {
		t = g_boxed_type_register_static ("GnmPrintHF",
			 (GBoxedCopyFunc)gnm_print_hf_copy,
			 (GBoxedFreeFunc)gnm_print_hf_free);
	}
	return t;
}

void
gnm_print_info_free (GnmPrintInformation *pi)
{
	g_return_if_fail (pi != NULL);

	if (NULL != pi->page_breaks.v)
		gnm_page_breaks_free (pi->page_breaks.v);
	if (NULL != pi->page_breaks.h)
		gnm_page_breaks_free (pi->page_breaks.h);

	g_free (pi->repeat_top);
	g_free (pi->repeat_left);
	gnm_print_hf_free (pi->header);
	gnm_print_hf_free (pi->footer);

	g_free (pi->printtofile_uri);
	if (pi->page_setup)
		g_object_unref (pi->page_setup);

	g_free (pi);
}

GType
gnm_print_information_get_type (void)
{
	static GType t = 0;

	if (t == 0) {
		t = g_boxed_type_register_static ("GnmPrintInformation",
			 (GBoxedCopyFunc)gnm_print_info_dup,
			 (GBoxedFreeFunc)gnm_print_info_free);
	}
	return t;
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
		{ NULL, NULL, NULL }
	};

	/* Fetch header/footer formats */
	{
		int i;

		for (i = 0; predefined_formats [i].left_format; i++) {
			GnmPrintHF *format;

			format = gnm_print_hf_new (
				predefined_formats [i].left_format[0]?
				_(predefined_formats [i].left_format):"",
				predefined_formats [i].middle_format[0]?
				_(predefined_formats [i].middle_format):"",
				predefined_formats [i].right_format[0]?
				_(predefined_formats [i].right_format):"");

			gnm_print_hf_formats = g_list_prepend (gnm_print_hf_formats, format);
			hf_formats_base_num++;
		}
	}

	/* Now append the custom formats */
	{
		GSList const *left;
		GSList const *middle;
		GSList const *right;

		left = gnm_conf_get_printsetup_hf_left ();
		middle = gnm_conf_get_printsetup_hf_middle ();
		right = gnm_conf_get_printsetup_hf_right ();

		while (left != NULL && middle != NULL && right != NULL)
		{
			GnmPrintHF *format;

			format = gnm_print_hf_new
				(left->data ? left->data : "",
				 middle->data ? middle->data : "",
				 right->data ? right->data : "");

			gnm_print_hf_formats = g_list_prepend (gnm_print_hf_formats, format);

			left = left->next;
			middle = middle->next;
			right = right->next;
		}
	}

	gnm_print_hf_formats = g_list_reverse (gnm_print_hf_formats);
}

/**
 * gnm_print_info_load_defaults:
 * @pi: #GnmPrintInformation
 *
 * NOTE: This reads from a globally stored configuration. If a
 *       configuration is stored along with a sheet then that will
 *       override these global defaults.
 */
void
gnm_print_info_load_defaults (GnmPrintInformation *res)
{
	GSList *list;
	GtkPrintSettings *settings;

	if (res->page_setup != NULL)
		return;

	res->page_setup = gnm_conf_get_page_setup ();

	res->scaling.type = gnm_conf_get_printsetup_scale_percentage ()
		? PRINT_SCALE_PERCENTAGE
		: PRINT_SCALE_FIT_PAGES;
	res->scaling.percentage.x = res->scaling.percentage.y
		= gnm_conf_get_printsetup_scale_percentage_value ();
	res->scaling.dim.cols = gnm_conf_get_printsetup_scale_width ();
	res->scaling.dim.rows = gnm_conf_get_printsetup_scale_height ();
	res->edge_to_below_header = gnm_conf_get_printsetup_margin_top ();
	res->edge_to_above_footer = gnm_conf_get_printsetup_margin_bottom ();
	res->desired_display.top = gnm_conf_get_printsetup_preferred_unit ();
	res->desired_display.bottom = gnm_conf_get_printsetup_preferred_unit ();
	res->desired_display.left = gnm_conf_get_printsetup_preferred_unit ();
	res->desired_display.right = gnm_conf_get_printsetup_preferred_unit ();
	res->desired_display.footer = gnm_conf_get_printsetup_preferred_unit ();
	res->desired_display.header = gnm_conf_get_printsetup_preferred_unit ();
	res->repeat_top = g_strdup (gnm_conf_get_printsetup_repeat_top ());
	res->repeat_left = g_strdup (gnm_conf_get_printsetup_repeat_left ());
	res->center_vertically = gnm_conf_get_printsetup_center_vertically ();
	res->center_horizontally = gnm_conf_get_printsetup_center_horizontally ();
	res->print_grid_lines = gnm_conf_get_printsetup_print_grid_lines ();
	res->print_titles = gnm_conf_get_printsetup_print_titles ();
	res->print_black_and_white = gnm_conf_get_printsetup_print_black_n_white ();
	res->print_even_if_only_styles = gnm_conf_get_printsetup_print_even_if_only_styles ();
	res->print_across_then_down = gnm_conf_get_printsetup_across_then_down ();

	list = gnm_conf_get_printsetup_header ();
	res->header = list ?
		gnm_print_hf_new (g_slist_nth_data (list, 0),
			      g_slist_nth_data (list, 1),
			      g_slist_nth_data (list, 2)) :
		gnm_print_hf_new ("", _("&[TAB]"), "");

	list = gnm_conf_get_printsetup_footer ();
	res->footer = list ?
		gnm_print_hf_new (g_slist_nth_data (list, 0),
			      g_slist_nth_data (list, 1),
			      g_slist_nth_data (list, 2)) :
		gnm_print_hf_new ("", _("Page &[PAGE]"), "");

	settings = gnm_conf_get_print_settings ();
	print_info_set_from_settings (res, settings);
	g_object_unref (settings);
}

/**
 * gnm_print_information_new:
 *
 * Returns a newly allocated GnmPrintInformation buffer
 *
 */
GnmPrintInformation *
gnm_print_information_new (gboolean load_defaults)
{
	GnmPrintInformation *res = g_new0 (GnmPrintInformation, 1);

	res->print_as_draft = FALSE;
	res->comment_placement = GNM_PRINT_COMMENTS_IN_PLACE;
	res->error_display = GNM_PRINT_ERRORS_AS_DISPLAYED;

	res->start_page	   = -1;
	res->n_copies	   = 0;
	res->do_not_print   = FALSE;

	res->page_setup = NULL;
	res->page_breaks.v = NULL;
	res->page_breaks.h = NULL;

	res->printtofile_uri = NULL;
	res->print_range = GNM_PRINT_ACTIVE_SHEET;

	if (load_defaults)
		gnm_print_info_load_defaults (res);
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

	start = g_list_length (gnm_print_hf_formats) - MAX_SAVED_CUSTOM_HF_FORMATS;
	if (start > base)
		base = start;

	for (l = gnm_print_hf_formats; l; l = l->next) {
		GnmPrintHF *hf = l->data;

		if (base-- > 0)
			continue;

		GO_SLIST_PREPEND (left, g_strdup(hf->left_format));
		GO_SLIST_PREPEND (middle, g_strdup(hf->middle_format));
		GO_SLIST_PREPEND (right, g_strdup(hf->right_format));
	}
	GO_SLIST_REVERSE(left);
	gnm_conf_set_printsetup_hf_left (left);
	g_slist_free_full (left, g_free);

	GO_SLIST_REVERSE(middle);
	gnm_conf_set_printsetup_hf_middle (middle);
	g_slist_free_full (middle, g_free);

	GO_SLIST_REVERSE(right);
	gnm_conf_set_printsetup_hf_right (right);
	g_slist_free_full (right, g_free);
}

static void
destroy_formats (void)
{
	g_list_free_full (gnm_print_hf_formats, (GDestroyNotify)gnm_print_hf_free);
	gnm_print_hf_formats = NULL;
}

static GSList *
make_triple (const GnmPrintHF *hf)
{
	GSList *l = NULL;

	GO_SLIST_PREPEND (l, hf->left_format ? hf->left_format : NULL);
	GO_SLIST_PREPEND (l, hf->middle_format ? hf->middle_format : NULL);
	GO_SLIST_PREPEND (l, hf->right_format ? hf->right_format : NULL);

	return l;
}

void
gnm_print_info_save (GnmPrintInformation *pi)
{
	GSList *l;

	gnm_conf_set_printsetup_scale_percentage (pi->scaling.type == PRINT_SCALE_PERCENTAGE);
	gnm_conf_set_printsetup_scale_percentage_value (pi->scaling.percentage.x);
	gnm_conf_set_printsetup_scale_width (pi->scaling.dim.cols);
	gnm_conf_set_printsetup_scale_height (pi->scaling.dim.rows);

	gnm_conf_set_printsetup_margin_top (pi->edge_to_below_header);
	gnm_conf_set_printsetup_margin_bottom (pi->edge_to_above_footer);
	gnm_conf_set_printsetup_preferred_unit (pi->desired_display.top);

	gnm_conf_set_printsetup_center_horizontally (pi->center_horizontally);
	gnm_conf_set_printsetup_center_vertically (pi->center_vertically);
	gnm_conf_set_printsetup_print_grid_lines (pi->print_grid_lines);
	gnm_conf_set_printsetup_print_titles (pi->print_titles);
	gnm_conf_set_printsetup_print_even_if_only_styles (pi->print_even_if_only_styles);
	gnm_conf_set_printsetup_print_black_n_white (pi->print_black_and_white);
	gnm_conf_set_printsetup_across_then_down (pi->print_across_then_down);

	gnm_conf_set_printsetup_repeat_top (pi->repeat_top);
	gnm_conf_set_printsetup_repeat_left (pi->repeat_left);

	save_formats ();

	l = make_triple (pi->header);
	gnm_conf_set_printsetup_header (l);
	g_slist_free (l);

	l = make_triple (pi->footer);
	gnm_conf_set_printsetup_footer (l);
	g_slist_free (l);

	gnm_conf_set_page_setup (pi->page_setup);
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
render_cell (GString *target, GnmPrintHFRenderInfo *info, char const *args)
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
render_tab (GString *target, GnmPrintHFRenderInfo *info, G_GNUC_UNUSED char const *args)
{
	if (info->sheet)
		g_string_append (target, info->sheet->name_unquoted);
	else
		g_string_append (target, _("Sheet"));
}

static void
render_page (GString *target, GnmPrintHFRenderInfo *info, G_GNUC_UNUSED char const *args)
{
	g_string_append_printf (target, "%d", info->page);
}

static void
render_pages (GString *target, GnmPrintHFRenderInfo *info, G_GNUC_UNUSED char const *args)
{
	g_string_append_printf (target, "%d", info->pages);
}

static void
render_timestamp_with_format (GString *target, char const *number_format, GnmPrintHFRenderInfo *info)
{
	GOFormat *format;

	/* TODO : Check this assumption.  Is it a localized format? */
	format = go_format_new_from_XL (number_format);
	format_value_gstring (target, format, info->date_time,
			      -1, info->date_conv);
	go_format_unref (format);
}

static void
render_date (GString *target, GnmPrintHFRenderInfo *info, char const *args)
{
	char const *date_format;

	if (args)
		date_format = args;
	else
		date_format = "dd-mmm-yyyy";

	render_timestamp_with_format (target, date_format, info);
}

static void
render_time (GString *target, GnmPrintHFRenderInfo *info, char const *args)
{
	char const *time_format;

	if (args)
		time_format = args;
	else
		time_format = "hh:mm";
	render_timestamp_with_format (target, time_format, info);
}

static void
render_file (GString *target, GnmPrintHFRenderInfo *info, G_GNUC_UNUSED char const *args)
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
render_path (GString *target, GnmPrintHFRenderInfo *info, G_GNUC_UNUSED char const *args)
{
	if (info->sheet != NULL && info->sheet->workbook != NULL) {
		char *path = go_dirname_from_uri (
			go_doc_get_uri (GO_DOC (info->sheet->workbook)), TRUE);
		g_string_append (target, path);
		g_free (path);
	} else
		g_string_append (target, _("Path "));
}

static void
render_title (GString *target, GnmPrintHFRenderInfo *info, G_GNUC_UNUSED char const *args)
{
	if (info->sheet != NULL && info->sheet->workbook != NULL) {
		GsfDocProp *prop;
		prop = gsf_doc_meta_data_lookup
			(go_doc_get_meta_data (GO_DOC (info->sheet->workbook)), GSF_META_NAME_TITLE);
		if (prop != NULL) {
			GValue *prop_value = (GValue *) gsf_doc_prop_get_val (prop);
			if (prop_value != NULL)
				g_string_append (target, g_value_get_string (prop_value));
		}
	} else
		g_string_append (target, _("Title"));
}

static struct {
	char const *name;
	void (*render)(GString *target, GnmPrintHFRenderInfo *info, char const *args);
	char *name_trans;
} render_ops [] = {
	{ N_("TAB"),   render_tab   , NULL},
	{ N_("PAGE"),  render_page  , NULL},
	{ N_("PAGES"), render_pages , NULL},
	{ N_("DATE"),  render_date  , NULL},
	{ N_("TIME"),  render_time  , NULL},
	{ N_("FILE"),  render_file  , NULL},
	{ N_("PATH"),  render_path  , NULL},
	{ N_("CELL"),  render_cell  , NULL},
	{ N_("TITLE"), render_title  , NULL},
	{ NULL , NULL, NULL},
};
/*
 * Renders an opcode.  The opcodes can take an argument by adding trailing ':'
 * to the opcode and then a number format code
 */
static void
render_opcode (GString *target, char /* non-const */ *opcode,
	       GnmPrintHFRenderInfo *info,
	       G_GNUC_UNUSED GnmPrintHFRenderType render_type)
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

#if 0
3.3.1.36 evenHeader (Even Page Header)

          &P              Current page number
          &N              Page count
          &D              Current date
          &T              Current time
          &A              Sheet name (BIFF5-BIFF8)
          &F              File name without path
          &Z              File path without file name (BIFF8X)
          &G              Picture (BIFF8X)
          &B              Bold on/off (BIFF2-BIFF4)
          &I              Italic on/off (BIFF2-BIFF4)
          &U              Underlining on/off
          &E              Double underlining on/off (BIFF5-BIFF8)
          &S              Strikeout on/off
          &X              Superscript on/off (BIFF5-BIFF8)
          &Y              Subscript on/off (BIFF5-BIFF8)
     &"<fontname>"        Set new font <fontname>
&"<fontname>,<fontstyle>" Set new font with specified style <fontstyle>. The style <fontstyle> is in most cases
                          one of "Regular", "Bold", "Italic", or "Bold Italic". But this setting is dependent on the
                          used font, it may differ (localised style names, or "Standard", "Oblique", ...). (BIFF5-
                          BIFF8)
     &<fontheight>        Set font height in points (<fontheight> is a decimal value). If this command is followed
                          by a plain number to be printed in the header, it will be separated from the font height
                          with a space character.
#endif

char *
gnm_print_hf_format_render (char const *format, GnmPrintHFRenderInfo *info, GnmPrintHFRenderType render_type)
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

GnmPrintHFRenderInfo *
gnm_print_hf_render_info_new (void)
{
	GnmPrintHFRenderInfo *hfi;

	hfi = g_new0 (GnmPrintHFRenderInfo, 1);
	hfi->date_conv = go_date_conv_from_str ("Lotus:1900");
	hfi->date_time = value_new_float
		(go_date_timet_to_serial_raw (time (NULL), hfi->date_conv));
	/* It doesn't appear like the end is accessed.  */
	range_init (&hfi->page_area, 0, 0, G_MAXINT / 2, G_MAXINT / 2);
	hfi->top_repeating.col = 0;
	hfi->top_repeating.row = 0;

	return hfi;
}

void
gnm_print_hf_render_info_destroy (GnmPrintHFRenderInfo *hfi)
{
	g_return_if_fail (hfi != NULL);

	value_release (hfi->date_time);
	g_free (hfi);
}

static GnmPrintHFRenderInfo *
hf_render_info_copy (GnmPrintHFRenderInfo *hfi)
{
	GnmPrintHFRenderInfo *res;
	g_return_val_if_fail (hfi != NULL, NULL);

	res = g_new (GnmPrintHFRenderInfo, 1);
	res->sheet = hfi->sheet;
	res->page = hfi->page;
	res->pages = hfi->pages;
	res->date_time = value_dup (hfi->date_time);
	res->date_conv = hfi->date_conv;
	res->page_area = hfi->page_area;
	res->top_repeating = hfi->top_repeating;
	return res;
}

GType
gnm_print_hf_render_info_get_type (void)
{
	static GType t = 0;

	if (t == 0) {
		t = g_boxed_type_register_static ("GnmPrintHFRenderInfo",
			 (GBoxedCopyFunc)hf_render_info_copy,
			 (GBoxedFreeFunc)gnm_print_hf_render_info_destroy);
	}
	return t;
}

static void
pdf_write_workbook (G_GNUC_UNUSED GOFileSaver const *fs,
		    G_GNUC_UNUSED GOIOContext *context,
		    WorkbookView const *wbv, GsfOutput *output)
{
	Workbook const *wb = wb_view_get_workbook (wbv);
	GPtrArray *sheets = gnm_file_saver_get_sheets (fs, wbv, FALSE);

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

	if (sheets)
		g_ptr_array_unref (sheets);

	gnm_print_sheet (NULL, wb_view_cur_sheet (wbv), FALSE,
			 GNM_PRINT_ALL_SHEETS, output);
}

static void
pdf_export (GOFileSaver const *fs, GOIOContext *context,
	    GoView const *view, GsfOutput *output)
{
	WorkbookView const *wbv = GNM_WORKBOOK_VIEW (view);
	Workbook const *wb = wb_view_get_workbook (wbv);
	GPtrArray *objects = g_object_get_data (G_OBJECT (wb), "pdf-objects");

	if (objects && objects->len > 0) {
		gpointer object_fit = g_object_get_data (G_OBJECT (wb), "pdf-object-fit");
		if (object_fit != NULL && GPOINTER_TO_INT (object_fit) == 1
		    && GNM_IS_SO_GRAPH (g_ptr_array_index (objects, 0))) {
			GError *err = NULL;
			sheet_object_write_image (g_ptr_array_index (objects, 0), "pdf", 150., output, &err);
			if (err != NULL) {
				go_io_error_push (context, go_error_info_new_str (err->message));
				g_error_free (err);
			}
		} else
			gnm_print_so (NULL, objects, output);
	} else
		pdf_write_workbook (fs, context, wbv, output);
}

struct cb_set_pdf_option {
	GOFileSaver *fs;
	Workbook const *wb;
};

static gboolean
cb_set_pdf_option (const char *key, const char *value,
		   GError **err, gpointer user_)
{
	struct cb_set_pdf_option *user = user_;
	Workbook const *wb = user->wb;

	if (strcmp (key, "object") == 0) {
		GPtrArray *objects = g_object_get_data (G_OBJECT (wb), "pdf-objects");
		GPtrArray *sheets = workbook_sheets (wb);
		gboolean object_seen = FALSE;
		unsigned ui;

		if (!objects) {
			objects = g_ptr_array_new ();
			g_object_set_data_full (G_OBJECT (wb),
						"pdf-objects", objects,
						(GDestroyNotify)g_ptr_array_unref);
		}

		for (ui = 0; ui < sheets->len; ui++) {
			Sheet *sheet = g_ptr_array_index (sheets, ui);
			GSList *sobjects = sheet->sheet_objects;
			for (; sobjects != NULL; sobjects = sobjects->next) {
				SheetObject *so = sobjects->data;
				gchar *name = NULL;
				g_object_get (so, "name", &name, NULL);
				if (strcmp (name, value) == 0) {
					g_ptr_array_add (objects, so);
					object_seen = TRUE;
				}
			}
		}

		g_ptr_array_unref (sheets);

		if (!object_seen) {
			*err = g_error_new (go_error_invalid (), 0,
					    _("There is no object with name "
					      "\'%s\'"), value);
			return TRUE;
		}

		return FALSE;
	}

	if (strcmp (key, "paper") == 0) {
		int i;
		if (strcmp (value, "fit") == 0) {
			g_object_set_data (G_OBJECT (wb),
					   "pdf-object-fit", GINT_TO_POINTER (1));
		} else for (i = 0; i < workbook_sheet_count (wb); i++) {
			Sheet *sheet = workbook_sheet_by_index (wb, i);
			if (print_info_set_paper (sheet->print_info, value)) {
				*err = g_error_new (go_error_invalid (), 0,
						    _("Unknown paper size"));

				return TRUE;
			}
		}
		return FALSE;
	}

	return gnm_file_saver_common_export_option (user->fs, wb,
						    key, value, err);
}

static gboolean
pdf_set_export_options (GOFileSaver *fs,
			GODoc *doc,
			const char *options,
			GError **err,
			G_GNUC_UNUSED gpointer user)
{
	struct cb_set_pdf_option data;
	data.fs = fs;
	data.wb = WORKBOOK (doc);
	return go_parse_key_value (options, err, cb_set_pdf_option, &data);
}

/**
 * print_init: (skip)
 */
void
print_init (void)
{
	/* Install a pdf saver.  */
	GOFileSaver *saver = go_file_saver_new (
		PDF_SAVER_ID, "pdf",
		_("PDF export"),
		GO_FILE_FL_WRITE_ONLY, pdf_export);
	g_object_set (G_OBJECT (saver), "sheet-selection", TRUE, NULL);
	g_signal_connect (G_OBJECT (saver), "set-export-options",
			  G_CALLBACK (pdf_set_export_options),
			  NULL);
	go_file_saver_register (saver);
	g_object_unref (saver);

	load_formats ();
}

/**
 * print_shutdown: (skip)
 */
void
print_shutdown (void)
{
	go_file_saver_unregister (go_file_saver_for_id (PDF_SAVER_ID));

	save_formats ();
	destroy_formats ();
}

#define COPY(field) dst->field = src->field

GnmPrintInformation *
gnm_print_info_dup (GnmPrintInformation const *src)
{
	GnmPrintInformation *dst = gnm_print_information_new (TRUE);

	gnm_print_info_load_defaults ((GnmPrintInformation *)src);

	COPY(scaling);
	COPY(edge_to_below_header);
	COPY(edge_to_above_footer);
        COPY(desired_display);

	g_free (dst->repeat_top);
	dst->repeat_top = g_strdup (src->repeat_top);

	g_free (dst->repeat_left);
	dst->repeat_left = g_strdup (src->repeat_left);

	COPY(print_across_then_down);
	COPY(center_vertically);
	COPY(center_horizontally);
	COPY(print_grid_lines);
	COPY(print_titles);
	COPY(print_black_and_white);
	COPY(print_as_draft);
	COPY(print_even_if_only_styles);
	COPY(do_not_print);
	COPY(comment_placement);
	COPY(error_display);

	gnm_page_breaks_free (dst->page_breaks.h);
	dst->page_breaks.h = gnm_page_breaks_dup (src->page_breaks.h);

	gnm_page_breaks_free (dst->page_breaks.v);
	dst->page_breaks.v = gnm_page_breaks_dup (src->page_breaks.v);

	gnm_print_hf_free (dst->header);
	dst->header = gnm_print_hf_copy (src->header);

	gnm_print_hf_free (dst->footer);
	dst->footer = gnm_print_hf_copy (src->footer);

	COPY(start_page);
        COPY(n_copies);

	g_free (dst->printtofile_uri);
	dst->printtofile_uri = g_strdup (src->printtofile_uri);

	if (dst->page_setup)
		g_object_unref (dst->page_setup);
	dst->page_setup = gtk_page_setup_copy (src->page_setup);

	return dst;
}

#undef COPY

/**
 * print_info_get_margins:
 * @pi: #GnmPrintInformation
 * @top: (out) (optional): top margin.
 * @bottom: (out) (optional): bottom margin.
 * @left: (out) (optional): left margin.
 * @right: (out) (optional): right margin.
 * @edge_to_below_header: (out) (optional):  margin.
 * @edge_to_above_footer: (out) (optional):  margin.
 */
void
print_info_get_margins (GnmPrintInformation *pi,
			double *top, double *bottom,
			double *left, double *right,
			double *edge_to_below_header,
			double *edge_to_above_footer)
{
	g_return_if_fail (pi != NULL);
	gnm_print_info_load_defaults (pi);
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
print_info_set_margin_header (GnmPrintInformation *pi, double header)
{
	g_return_if_fail (pi != NULL);
	gnm_print_info_load_defaults (pi);
	g_return_if_fail (pi->page_setup != NULL);

	gtk_page_setup_set_top_margin (pi->page_setup, header, GTK_UNIT_POINTS);
}

void
print_info_set_margin_footer (GnmPrintInformation *pi, double footer)
{
	g_return_if_fail (pi != NULL);
	gnm_print_info_load_defaults (pi);
        g_return_if_fail (pi->page_setup != NULL);

        gtk_page_setup_set_bottom_margin (pi->page_setup, footer, GTK_UNIT_POINTS);
}

void
print_info_set_margin_left (GnmPrintInformation *pi, double left)
{
	g_return_if_fail (pi != NULL);
	gnm_print_info_load_defaults (pi);
	g_return_if_fail (pi->page_setup != NULL);

	gtk_page_setup_set_left_margin (pi->page_setup, left, GTK_UNIT_POINTS);
}

void
print_info_set_margin_right (GnmPrintInformation *pi, double right)
{
	g_return_if_fail (pi != NULL);
	gnm_print_info_load_defaults (pi);
	g_return_if_fail (pi->page_setup != NULL);

	gtk_page_setup_set_right_margin (pi->page_setup, right, GTK_UNIT_POINTS);
}

void
print_info_set_edge_to_above_footer (GnmPrintInformation *pi, double e_f)
{
	g_return_if_fail (pi != NULL);
	gnm_print_info_load_defaults (pi);
	g_return_if_fail (pi->page_setup != NULL);

	pi->edge_to_above_footer = e_f;
}

void
print_info_set_edge_to_below_header (GnmPrintInformation *pi, double e_h)
{
	g_return_if_fail (pi != NULL);
	gnm_print_info_load_defaults (pi);
	g_return_if_fail (pi->page_setup != NULL);

	pi->edge_to_below_header = e_h;
}


void
print_info_set_margins (GnmPrintInformation *pi,
			double header, double footer, double left, double right)
{
	g_return_if_fail (pi != NULL);
	gnm_print_info_load_defaults (pi);
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

static gboolean
known_bad_paper (const char *paper)
{
	if (strcmp (paper, "") == 0)
		return TRUE;
	return FALSE;
}


static void
paper_log_func (G_GNUC_UNUSED const gchar   *log_domain,
		GLogLevelFlags log_level,
		G_GNUC_UNUSED const gchar   *message,
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
 /* GTK behaves stupid on some string it should recognize:*/
	else if (g_str_has_prefix (paper, "iso_a3_"))
		paper = GTK_PAPER_NAME_A3;
	else if (g_str_has_prefix (paper, "iso_a4_"))
		paper = GTK_PAPER_NAME_A4;
	else if (g_str_has_prefix (paper, "iso_a5_"))
		paper = GTK_PAPER_NAME_A5;
	else if (g_str_has_prefix (paper, "iso_b5_"))
		paper = GTK_PAPER_NAME_B5;
	else if (g_str_has_prefix (paper, "na_letter_"))
		paper = GTK_PAPER_NAME_LETTER;
	else if (g_str_has_prefix (paper, "na_legal_"))
		paper = GTK_PAPER_NAME_LEGAL;
	else if (g_str_has_prefix (paper, "na_executive_"))
		paper = GTK_PAPER_NAME_EXECUTIVE;

	/* Hack: gtk_paper_size_new warns on bad paper, so shut it up.  */
	/* http://bugzilla.gnome.org/show_bug.cgi?id=493880 */
	if (known_bad_paper (paper)) {
		gtk_paper = NULL;
		bad_paper = 1;
	} else {
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
print_info_set_paper (GnmPrintInformation *pi, char const *paper)
{
	g_return_val_if_fail (pi != NULL, TRUE);

	gnm_print_info_load_defaults (pi);
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

char *
print_info_get_paper (GnmPrintInformation *pi)
{
	g_return_val_if_fail (pi != NULL, g_strdup (GTK_PAPER_NAME_A4));
	gnm_print_info_load_defaults (pi);

	return page_setup_get_paper (pi->page_setup);
}

GtkPaperSize *
print_info_get_paper_size (GnmPrintInformation *pi)
{
	g_return_val_if_fail (pi != NULL, NULL);
	gnm_print_info_load_defaults (pi);

	return gtk_page_setup_get_paper_size (pi->page_setup);
}

/**
 * print_info_get_paper_display_name:
 * @pi: #GnmPrintInformation
 *
 * Returns: (transfer none): the name of the selected paper type
 */
char const *
print_info_get_paper_display_name (GnmPrintInformation *pi)
{
	GtkPaperSize* paper;

	g_return_val_if_fail (pi != NULL, "ERROR: No printinformation specified");
	gnm_print_info_load_defaults (pi);
	g_return_val_if_fail (pi->page_setup != NULL, "ERROR: No pagesetup loaded");

	paper = gtk_page_setup_get_paper_size (pi->page_setup);
	return gtk_paper_size_get_display_name (paper);
}

double
print_info_get_paper_width (GnmPrintInformation *pi, GtkUnit unit)
{
	g_return_val_if_fail (pi != NULL, 0.);
	gnm_print_info_load_defaults (pi);

	return gtk_page_setup_get_paper_width (pi->page_setup, unit);
}

double
print_info_get_paper_height (GnmPrintInformation *pi, GtkUnit unit)
{
	g_return_val_if_fail (pi != NULL, 0);
	gnm_print_info_load_defaults (pi);

	return gtk_page_setup_get_paper_height (pi->page_setup, unit);
}

/**
 * gnm_print_info_get_page_setup:
 * @pi: #GnmPrintInformation
 *
 * Returns: (transfer none): the page setup.
 **/
GtkPageSetup *
gnm_print_info_get_page_setup (GnmPrintInformation *pi)
{
	g_return_val_if_fail (pi != NULL, NULL);
	gnm_print_info_load_defaults (pi);

	return pi->page_setup;
}

/**
 * gnm_print_info_set_page_setup:
 * @pi: #GnmPrintInformation
 * @page_setup: (transfer full): #GtkPageSetup
 *
 * Absorb a ref to @page_setup.
 *
 * WHY WHY WHY
 * 1) The life cycle in here is a tad odd, the load_defaults does nothing for the
 * case of an existing page_setup, and seems like it should be ignored for the
 * case of a new one.
 *
 * 2) Why not copy the page_setup in here and make the arg const ?
 **/
void
gnm_print_info_set_page_setup (GnmPrintInformation *pi,
			       GtkPageSetup *page_setup)
{
	g_return_if_fail (pi != NULL);

	gnm_print_info_load_defaults (pi);

	if (pi->page_setup) {
		double header, footer, left, right;
		print_info_get_margins (pi,
			&header, &footer, &left, &right, NULL, NULL);
		g_object_unref (pi->page_setup);
		pi->page_setup = page_setup;
		print_info_set_margins (pi, header, footer, left, right);
	} else
		pi->page_setup = page_setup;
}

GtkPageOrientation
print_info_get_paper_orientation (GnmPrintInformation *pi)
{
	GtkPageOrientation orientation;

	g_return_val_if_fail (pi != NULL, GTK_PAGE_ORIENTATION_PORTRAIT);
	gnm_print_info_load_defaults (pi);
	g_return_val_if_fail (pi->page_setup != NULL, GTK_PAGE_ORIENTATION_PORTRAIT);

	orientation = gtk_page_setup_get_orientation (pi->page_setup);
	return orientation;
}

void
print_info_set_paper_orientation (GnmPrintInformation *pi,
				  GtkPageOrientation orientation)
{
	g_return_if_fail (pi != NULL);
	gnm_print_info_load_defaults (pi);

	gtk_page_setup_set_orientation (pi->page_setup, orientation);
}

/**
 * print_info_set_breaks:
 * @pi: #GnmPrintInformation
 * @breaks: (transfer full): #GnmPageBreaks
 **/
void
print_info_set_breaks (GnmPrintInformation *pi,
		       GnmPageBreaks    *breaks)
{
	GnmPageBreaks **target;

	g_return_if_fail (pi != NULL);

	target = breaks->is_vert ? &pi->page_breaks.v : &pi->page_breaks.h;

	if (*target == breaks) /* just in case something silly happens */
		return;

	gnm_page_breaks_free (*target);
	*target = breaks;
}

gboolean
print_info_has_manual_breaks (GnmPrintInformation *pi)
{
	if (gnm_page_breaks_get_next_manual_break (pi->page_breaks.v, 0) > -1)
		return TRUE;
	return (gnm_page_breaks_get_next_manual_break (pi->page_breaks.h, 0) > -1);
}

/********************************************************************
 * Simple data structure to store page breaks defined as a wrapper in case we
 * need something more extensive later. */

/**
 * gnm_page_breaks_new:
 * @is_vert:
 *
 * Allocate a collection of page breaks.
 **/
GnmPageBreaks *
gnm_page_breaks_new (gboolean is_vert)
{
	GnmPageBreaks *res = g_new (GnmPageBreaks, 1);

	res->is_vert = is_vert;
	res->details = g_array_new (FALSE, FALSE, sizeof (GnmPageBreak));

	return res;
}

/**
 * gnm_page_breaks_dup:
 * @src: (transfer none) (nullable): #GnmPageBreak
 *
 * Returns: (transfer full) (nullable): A duplicate #GnmPageBreak.
 */
GnmPageBreaks *
gnm_page_breaks_dup (GnmPageBreaks const *src)
{
	if (src != NULL) {
		GnmPageBreaks *dst = gnm_page_breaks_new (src->is_vert);
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

/**
 * gnm_page_breaks_free:
 * @breaks: (transfer none) (nullable): #GnmPageBreak
 */
void
gnm_page_breaks_free (GnmPageBreaks *breaks)
{
	if (breaks) {
		g_array_free (breaks->details, TRUE);
		g_free (breaks);
	}
}

GType
gnm_page_breaks_get_type (void)
{
	static GType t = 0;

	if (t == 0) {
		t = g_boxed_type_register_static ("GnmPageBreaks",
			 (GBoxedCopyFunc)gnm_page_breaks_dup,
			 (GBoxedFreeFunc)gnm_page_breaks_free);
	}
	return t;
}

/**
 * gnm_page_breaks_dup_non_auto_breaks:
 * @src: (transfer none) (nullable): #GnmPageBreak
 *
 * Returns: (transfer full) (nullable): A duplicate #GnmPageBreak, but
 * containing only non-auto page breaks.
 */
GnmPageBreaks *
gnm_page_breaks_dup_non_auto_breaks (GnmPageBreaks const *src)
{
	if (src != NULL) {
		GnmPageBreaks *dst = gnm_page_breaks_new (src->is_vert);
		GArray       *d_details = dst->details;
		GArray const *s_details = src->details;
		GnmPageBreak *pbreak;
		unsigned i;

		/* no need to validate through gnm_page_breaks_append_break, just dup */
		for (i = 0; i < s_details->len ; i++) {
			pbreak = &g_array_index (s_details, GnmPageBreak, i);
			if (pbreak->type != GNM_PAGE_BREAK_AUTO)
				g_array_append_val (d_details, *pbreak);
		}
		return dst;
	} else
		return NULL;
}

gboolean
gnm_page_breaks_append_break (GnmPageBreaks *breaks,
			      int pos,
			      GnmPageBreakType type)
{
	GnmPageBreak const *prev;
	GnmPageBreak info;

	g_return_val_if_fail (breaks != NULL, FALSE);

	if (type == GNM_PAGE_BREAK_NONE)
		return TRUE;

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

GnmPageBreakType
gnm_page_breaks_get_break (GnmPageBreaks *breaks,
			   int pos)
{
	int i;

	if (breaks == NULL)
		return GNM_PAGE_BREAK_NONE;

	for (i = breaks->details->len - 1; i >= 0; i--) {
		GnmPageBreak *pbreak;
		pbreak = &g_array_index (breaks->details, GnmPageBreak, i);
		if (pbreak->pos < pos)
			return GNM_PAGE_BREAK_NONE;
		if (pbreak->pos == pos)
			return (pbreak->type);
	}
	return GNM_PAGE_BREAK_NONE;
}

int
gnm_page_breaks_get_next_manual_break (GnmPageBreaks *breaks,
				       int pos)
{
	guint i;

	if (breaks == NULL)
		return -1;

	for (i = 0; i < breaks->details->len; i++) {
		GnmPageBreak const *pbreak;
		pbreak = &g_array_index (breaks->details, GnmPageBreak, i);
		if (pbreak->pos > pos
		    && pbreak->type != GNM_PAGE_BREAK_AUTO)
			return pbreak->pos;
	}
	return -1;
}

int
gnm_page_breaks_get_next_break (GnmPageBreaks *breaks, int pos)
{
	guint i;

	if (breaks == NULL)
		return -1;

	for (i = 0; i < breaks->details->len; i++) {
		GnmPageBreak const *pbreak;
		pbreak = &g_array_index (breaks->details, GnmPageBreak, i);
		if (pbreak->pos > pos)
			return pbreak->pos;
	}
	return -1;
}

gboolean
gnm_page_breaks_set_break (GnmPageBreaks *breaks,
			   int pos,
			   GnmPageBreakType type)
{
	GnmPageBreak *pbreak;
	GnmPageBreak info;
	guint i;
	int before = -1;

	g_return_val_if_fail (breaks != NULL, FALSE);

	/* Do some simple validation */
	if (pos < 0)
		return FALSE;

	if (breaks->details->len == 0 && type != GNM_PAGE_BREAK_NONE)
		return gnm_page_breaks_append_break (breaks, pos, type);

	for (i = 0; i < breaks->details->len; i++) {
		pbreak = &g_array_index (breaks->details, GnmPageBreak, i);
		if (pbreak->pos == pos) {
			if (type == GNM_PAGE_BREAK_NONE) {
				g_array_remove_index (breaks->details, i);
			} else {
				pbreak->type = type;
			}
			return TRUE;
		} else if (pbreak->pos < pos)
			before = (int) i;
	}

	if (type == GNM_PAGE_BREAK_NONE)
		return TRUE;

	info.pos   = pos;
	info.type  = type;
	if ((before + 1) > (int) breaks->details->len)
		g_array_append_val (breaks->details, info);
	else
		g_array_insert_val (breaks->details, (before + 1), info);

	return TRUE;
}

/**
 * gnm_page_break_type_from_str:
 * @str:
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
	if (0 == g_ascii_strcasecmp (str, "none"))
		return GNM_PAGE_BREAK_NONE;
	return GNM_PAGE_BREAK_NONE;
}


/**
 * gnm_page_breaks_clean:
 * @breaks: (nullable): #GnmPageBreakType
 *
 * Remove all auto page breaks
 **/
void
gnm_page_breaks_clean (GnmPageBreaks *breaks)
{
	guint i;

	if (breaks == NULL)
		return;

	for (i = 0; i < breaks->details->len; i++) {
		GnmPageBreak *pbreak = &g_array_index (breaks->details,
						       GnmPageBreak, i);
		if (pbreak->type == GNM_PAGE_BREAK_AUTO) {
			g_array_remove_index (breaks->details, i);
			i--;
		}
	}
}

void
print_info_set_printtofile_uri (GnmPrintInformation *pi,
				gchar const *uri)
{
	g_free (pi->printtofile_uri);
	pi->printtofile_uri = g_strdup (uri);
}

void
print_info_set_printtofile_from_settings (GnmPrintInformation *pi,
					  GtkPrintSettings* settings,
					  gchar const *default_uri)
{
	char const *uri = gtk_print_settings_get
		(settings,
		 GTK_PRINT_SETTINGS_OUTPUT_URI);
	if (strcmp (uri, default_uri) == 0)
		print_info_set_printtofile_uri (pi, NULL);
	else
		print_info_set_printtofile_uri (pi, uri);
}

void
print_info_set_from_settings (GnmPrintInformation *pi,
			      GtkPrintSettings* settings)
{
	pi->print_range = gtk_print_settings_get_int_with_default
		(settings,
		 GNUMERIC_PRINT_SETTING_PRINTRANGE_KEY,
		 GNM_PRINT_ACTIVE_SHEET);
}

PrintRange
print_info_get_printrange (GnmPrintInformation *pi)
{
	gnm_print_info_load_defaults (pi);
	return pi->print_range;
}

void
print_info_set_printrange (GnmPrintInformation *pi, PrintRange pr)
{
	if (pr >= GNM_PRINT_ACTIVE_SHEET
	    && pr <= GNM_PRINT_SHEET_SELECTION_IGNORE_PRINTAREA)
		pi->print_range = pr;
	else
		pi->print_range = GNM_PRINT_ACTIVE_SHEET;
}

/**
 * print_info_get_printtofile_uri:
 * @pi: @GnmPrintInformation
 *
 * Returns: (transfer none): The uri used for print-to-file.
 */
char const *
print_info_get_printtofile_uri (GnmPrintInformation *pi)
{
	gnm_print_info_load_defaults (pi);
	return pi->printtofile_uri;
}


gboolean
print_load_repeat_range (char const *str, GnmRange *r, Sheet const *sheet)
{
	GnmParsePos pp;
	GnmRangeRef res;

	if (str == NULL || *str == '\0')
		return FALSE;

	if (str != rangeref_parse (&res, str,
				   parse_pos_init_sheet (&pp, sheet),
				   gnm_conventions_default)) {
		Sheet *start_sheet = (Sheet *)sheet;
		Sheet *end_sheet = (Sheet *)sheet;
		gnm_rangeref_normalize_pp (&res, &pp,
					   &start_sheet, &end_sheet,
					   r);
		return TRUE;
	} else
		return FALSE;
}
