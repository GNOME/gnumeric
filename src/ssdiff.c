/*
 * ssdiff.c: A diff program for spreadsheets.
 *
 * Author:
 *   Morten Welinder <terra@gnome.org>
 *
 * Copyright (C) 2012 Morten Welinder (terra@gnome.org)
 */

#include <gnumeric-config.h>
#include <glib/gi18n.h>
#include "gnumeric.h"
#include <goffice/goffice.h>
#include "libgnumeric.h"
#include "gutils.h"
#include "command-context.h"
#include "command-context-stderr.h"
#include "gnm-plugin.h"
#include "workbook-view.h"
#include "workbook.h"
#include "sheet.h"
#include "sheet-style.h"
#include "style-border.h"
#include "style-color.h"
#include "cell.h"
#include "value.h"
#include "expr.h"
#include "ranges.h"
#include "mstyle.h"
#include "xml-sax.h"
#include "hlink.h"
#include "input-msg.h"
#include <gsf/gsf-libxml.h>
#include <gsf/gsf-output-stdio.h>
#include <gsf/gsf-input.h>

/* FIXME: Namespace?  */
#define DIFF "ssdiff:"

static gboolean ssdiff_show_version = FALSE;
static gboolean ssdiff_highlight = FALSE;
static gboolean ssdiff_xml = FALSE;
static char *ssdiff_output = NULL;

static const GOptionEntry ssdiff_options [] = {
	{
		"version", 'v',
		0, G_OPTION_ARG_NONE, &ssdiff_show_version,
		N_("Display program version"),
		NULL
	},

	{
		"output", 'o',
		0, G_OPTION_ARG_STRING, &ssdiff_output,
		N_("Send output to file"),
		N_("file")
	},

	{
		"highlight", 'h',
		0, G_OPTION_ARG_NONE, &ssdiff_highlight,
		N_("Output copy highlighting differences"),
		NULL
	},

	{
		"xml", 'x',
		0, G_OPTION_ARG_NONE, &ssdiff_xml,
		N_("Output in xml format"),
		NULL
	},

	/* ---------------------------------------- */

	{ NULL }
};

/* -------------------------------------------------------------------------- */

typedef struct GnmDiffState_ GnmDiffState;

typedef struct {
	/* Start comparison of two workbooks.  */
	gboolean (*diff_start) (GnmDiffState *state);

	/* Finish comparison started with above.  */
	void (*diff_end) (GnmDiffState *state);

	/* ------------------------------ */

	/* Start looking at a sheet.  Either sheet might be NULL.  */
	void (*sheet_start) (GnmDiffState *state,
			     Sheet const *os, Sheet const *ns);

	/* Finish sheet started with above.  */
	void (*sheet_end) (GnmDiffState *state);

	/* The order of sheets has changed.  */
	void (*sheet_order_changed) (GnmDiffState *state);

	/* An integer attribute of the sheet has changed.  */
	void (*sheet_attr_int_changed) (GnmDiffState *state, const char *name,
					int o, int n);

	/* ------------------------------ */

	void (*colrow_changed) (GnmDiffState *state,
				ColRowInfo const *oc, ColRowInfo const *nc,
				gboolean is_cols, int i);

	/* ------------------------------ */

	/* A cell was changed/added/removed.  */
	void (*cell_changed) (GnmDiffState *state,
			      GnmCell const *oc, GnmCell const *nc);

	/* ------------------------------ */

	/* The style of an area was changed.  */
	void (*style_changed) (GnmDiffState *state, GnmRange const *r,
			       Sheet const *osh, Sheet const *nsh,
			       GnmStyle const *os, GnmStyle const *ns);
} GnmDiffActions;

struct GnmDiffState_ {
	GOIOContext *ioc;
	struct GnmDiffStateFile_ {
		char *url;
		GsfInput *input;
		Workbook *wb;
		WorkbookView *wbv;
	} old, new;

	const GnmDiffActions *actions;

	gboolean diff_found;

	GsfOutput *output;

	/* The following for xml mode.  */
	GsfXMLOut *xml;
	const char *open_section;
	GnmConventions *convs;

	/* The following for highlight mode.  */
	struct GnmDiffStateFile_ highlight;
	GOFileSaver const *highlight_fs;
	GnmStyle *highlight_style;
};

static gboolean
null_diff_start (G_GNUC_UNUSED GnmDiffState *state)
{
	return FALSE;
}

static void
null_diff_end (G_GNUC_UNUSED GnmDiffState *state)
{
}

static void
null_sheet_start (G_GNUC_UNUSED GnmDiffState *state,
		  G_GNUC_UNUSED Sheet const *os,
		  G_GNUC_UNUSED Sheet const *ns)
{
}

static void
null_sheet_end (G_GNUC_UNUSED GnmDiffState *state)
{
}

static void
null_sheet_order_changed (G_GNUC_UNUSED GnmDiffState *state)
{
}

static void
null_sheet_attr_int_changed (G_GNUC_UNUSED GnmDiffState *state,
			     G_GNUC_UNUSED const char *name,
			     G_GNUC_UNUSED int o,
			     G_GNUC_UNUSED int n)
{
}

static void
null_colrow_changed (G_GNUC_UNUSED GnmDiffState *state,
		     G_GNUC_UNUSED ColRowInfo const *oc, G_GNUC_UNUSED ColRowInfo const *nc,
		     G_GNUC_UNUSED gboolean is_cols, G_GNUC_UNUSED int i)
{
}

/* -------------------------------------------------------------------------- */

static gboolean
read_file (struct GnmDiffStateFile_ *dsf, const char *filename,
	   GOIOContext *ioc)
{
	GError *err = NULL;

	dsf->url = go_shell_arg_to_uri (filename);

	if (!dsf->input)
		dsf->input = go_file_open (dsf->url, &err);

	if (!dsf->input) {
		g_printerr (_("%s: Failed to read %s: %s\n"),
			    g_get_prgname (),
			    filename,
			    err ? err->message : "?");
		if (err)
			g_error_free (err);
		return TRUE;
	}

	dsf->wbv = workbook_view_new_from_input (dsf->input,
						 dsf->url, NULL,
						 ioc, NULL);
	if (!dsf->wbv)
		return TRUE;
	dsf->wb = wb_view_get_workbook (dsf->wbv);

	return FALSE;
}

static void
clear_file_state (struct GnmDiffStateFile_ *dsf)
{
	g_free (dsf->url);
	g_clear_object (&dsf->wb);
	g_clear_object (&dsf->input);
}

/* -------------------------------------------------------------------------- */

static const char *
def_cell_name (GnmCell const *oc)
{
	static char *res;
	g_free (res);
	res = oc
		? g_strconcat (oc->base.sheet->name_quoted,
			       "!",
			       cell_name (oc),
			       NULL)
		: NULL;
	return res;
}

static void
def_sheet_start (GnmDiffState *state, Sheet const *os, Sheet const *ns)
{
	if (os && ns)
		gsf_output_printf (state->output, _("Differences for sheet %s:\n"), os->name_quoted);
	else if (os)
		gsf_output_printf (state->output, _("Sheet %s removed.\n"), os->name_quoted);
	else if (ns)
		gsf_output_printf (state->output, _("Sheet %s added.\n"), ns->name_quoted);
	else
		g_assert_not_reached ();
}

static void
def_sheet_order_changed (GnmDiffState *state)
{
	gsf_output_printf (state->output, _("Sheet order changed.\n"));
}

static void
def_sheet_attr_int_changed (GnmDiffState *state, const char *name,
			    G_GNUC_UNUSED int o, G_GNUC_UNUSED int n)
{
	gsf_output_printf (state->output, _("Sheet attribute %s changed.\n"),
			   name);
}

static void
def_colrow_changed (GnmDiffState *state, ColRowInfo const *oc, ColRowInfo const *nc,
		    gboolean is_cols, int i)
{
	if (is_cols)
		gsf_output_printf (state->output, _("Width of column %d changed.\n"), i);
	else
		gsf_output_printf (state->output, _("Width of row %d changed.\n"), i);
}

static void
def_cell_changed (GnmDiffState *state, GnmCell const *oc, GnmCell const *nc)
{
	if (oc && nc)
		gsf_output_printf (state->output, _("Cell %s changed.\n"), def_cell_name (oc));
	else if (oc)
		gsf_output_printf (state->output, _("Cell %s removed.\n"), def_cell_name (oc));
	else if (nc)
		gsf_output_printf (state->output, _("Cell %s added.\n"), def_cell_name (nc));
	else
		g_assert_not_reached ();
}

static void
def_style_changed (GnmDiffState *state, GnmRange const *r,
		   G_GNUC_UNUSED Sheet const *osh,
		   G_GNUC_UNUSED Sheet const *nsh,
		   G_GNUC_UNUSED GnmStyle const *os,
		   G_GNUC_UNUSED GnmStyle const *ns)
{
	gsf_output_printf (state->output, _("Style of %s was changed.\n"),
			   range_as_string (r));
}

static const GnmDiffActions default_actions = {
	null_diff_start,
	null_diff_end,
	def_sheet_start,
	null_sheet_end,
	def_sheet_order_changed,
	def_sheet_attr_int_changed,
	def_colrow_changed,
	def_cell_changed,
	def_style_changed,
};

/* -------------------------------------------------------------------------- */

static gboolean
xml_diff_start (GnmDiffState *state)
{
	state->xml = gsf_xml_out_new (state->output);
	state->convs = gnm_xml_io_conventions ();

	gsf_xml_out_start_element (state->xml, DIFF "Diff");

	return FALSE;
}

static void
xml_diff_end (GnmDiffState *state)
{
	gsf_xml_out_end_element (state->xml); /* </Diff> */
}

static void
xml_sheet_start (GnmDiffState *state, Sheet const *os, Sheet const *ns)
{
	Sheet const *sheet = os ? os : ns;

	gsf_xml_out_start_element (state->xml, DIFF "Sheet");
	gsf_xml_out_add_cstr (state->xml, "Name", sheet->name_unquoted);
	if (os)
		gsf_xml_out_add_int (state->xml, "Old", os->index_in_wb);
	if (ns)
		gsf_xml_out_add_int (state->xml, "New", ns->index_in_wb);
}

static void
xml_close_section (GnmDiffState *state)
{
	if (state->open_section) {
		gsf_xml_out_end_element (state->xml);
		state->open_section = NULL;
	}
}

static void
xml_open_section (GnmDiffState *state, const char *section)
{

	xml_close_section (state);
	gsf_xml_out_start_element (state->xml, section);
	state->open_section = section;
}

static void
xml_sheet_end (GnmDiffState *state)
{
	xml_close_section (state);
	gsf_xml_out_end_element (state->xml); /* </Sheet> */
}

static void
xml_sheet_attr_int_changed (GnmDiffState *state, const char *name,
			    int o, int n)
{
	char *elem;

	elem = g_strconcat (DIFF, name, NULL);
	gsf_xml_out_start_element (state->xml, elem);
	gsf_xml_out_add_int (state->xml, "Old", o);
	gsf_xml_out_add_int (state->xml, "New", n);
	gsf_xml_out_end_element (state->xml); /* elem */
	g_free (elem);
}

static void
output_cell (GnmDiffState *state, GnmCell const *cell,
	     const char *tag, const char *valtag, const char *fmttag)
{
	GString *str;

	if (!cell)
		return;

	str = g_string_sized_new (100);
	if (gnm_cell_has_expr (cell)) {
		GnmConventionsOut out;
		GnmParsePos pp;

		out.accum = str;
		out.pp    = parse_pos_init_cell (&pp, cell);
		out.convs = state->convs;

		g_string_append_c (str, '=');
		gnm_expr_top_as_gstring (cell->base.texpr, &out);
	} else {
		GnmValue const *v = cell->value;
		value_get_as_gstring (v, str, state->convs);
		gsf_xml_out_add_int (state->xml, valtag, v->v_any.type);
		if (VALUE_FMT (v))
			gsf_xml_out_add_cstr (state->xml, fmttag, go_format_as_XL (VALUE_FMT (v)));
	}

	gsf_xml_out_add_cstr (state->xml, tag, str->str);
	g_string_free (str, TRUE);
}

static void
xml_colrow_changed (GnmDiffState *state, ColRowInfo const *oc, ColRowInfo const *nc,
		    gboolean is_cols, int i)
{
	xml_open_section (state, is_cols ? DIFF "Cols" : DIFF "Rows");

	gsf_xml_out_start_element (state->xml, is_cols ? DIFF "ColInfo" : DIFF "RowInfo");
	if (i >= 0) gsf_xml_out_add_int (state->xml, "No", i);

	if (oc->size_pts != nc->size_pts) {
		gsf_xml_out_add_float (state->xml, "OldUnit", oc->size_pts, 4);
		gsf_xml_out_add_float (state->xml, "NewUnit", nc->size_pts, 4);
	}
	if (oc->hard_size != nc->hard_size) {
		gsf_xml_out_add_bool (state->xml, "OldHardSize", oc->hard_size);
		gsf_xml_out_add_bool (state->xml, "NewHardSize", nc->hard_size);
	}
	if (oc->visible != nc->visible) {
		gsf_xml_out_add_bool (state->xml, "OldHidden", !oc->visible);
		gsf_xml_out_add_bool (state->xml, "NewHidden", !nc->visible);
	}
	if (oc->is_collapsed != nc->is_collapsed) {
		gsf_xml_out_add_bool (state->xml, "OldCollapsed", oc->is_collapsed);
		gsf_xml_out_add_bool (state->xml, "NewCollapsed", nc->is_collapsed);
	}
	if (oc->outline_level != nc->outline_level) {
		gsf_xml_out_add_int (state->xml, "OldOutlineLevel", oc->outline_level);
		gsf_xml_out_add_int (state->xml, "NewOutlineLevel", nc->outline_level);
	}

	gsf_xml_out_end_element (state->xml); /* </ColInfo> or </RowInfo> */
}

static void
xml_cell_changed (GnmDiffState *state, GnmCell const *oc, GnmCell const *nc)
{
	const GnmCellPos *pos;

	xml_open_section (state, DIFF "Cells");

	gsf_xml_out_start_element (state->xml, DIFF "Cell");

	pos = oc ? &oc->pos : &nc->pos;
	gsf_xml_out_add_int (state->xml, "Row", pos->row);
	gsf_xml_out_add_int (state->xml, "Col", pos->col);

	output_cell (state, oc, "Old", "OldValueType", "OldValueFormat");
	output_cell (state, nc, "New", "NewValueType", "NewValueFormat");

	gsf_xml_out_end_element (state->xml); /* </Cell> */
}

#define DO_INT(what,fun)					\
  do {								\
	  gsf_xml_out_start_element (state->xml, (what));	\
	  gsf_xml_out_add_int (state->xml, "Old", (fun) (os));	\
	  gsf_xml_out_add_int (state->xml, "New", (fun) (ns));	\
	  gsf_xml_out_end_element (state->xml);			\
  } while (0)

#define DO_INTS(what,fun,oobj,nobj)					\
  do {									\
  	  int oi = (oobj) ? (fun) (oobj) : 0;			\
	  int ni = (nobj) ? (fun) (nobj) : 0;			\
	  if (oi != ni || !(oobj) != !(nobj)) {				\
		  gsf_xml_out_start_element (state->xml, (what));	\
		  if (oobj) gsf_xml_out_add_int (state->xml, "Old", oi); \
		  if (nobj) gsf_xml_out_add_int (state->xml, "New", ni); \
		  gsf_xml_out_end_element (state->xml);			\
	  }								\
  } while (0)

#define DO_STRINGS(what,fun,oobj,nobj)					\
  do {									\
	  const char *ostr = (oobj) ? (fun) (oobj) : NULL;		\
	  const char *nstr = (nobj) ? (fun) (nobj) : NULL;		\
	  if (g_strcmp0 (ostr, nstr)) {					\
		  gsf_xml_out_start_element (state->xml, (what));	\
		  if (ostr) gsf_xml_out_add_cstr (state->xml, "Old", ostr); \
		  if (nstr) gsf_xml_out_add_cstr (state->xml, "New", nstr); \
		  gsf_xml_out_end_element (state->xml);			\
	  }								\
  } while (0)

static const char *
cb_validation_message (GnmValidation const *v)
{
	return v->msg ? v->msg->str : NULL;
}

static const char *
cb_validation_title (GnmValidation const *v)
{
	return v->title ? v->title->str : NULL;
}

static gboolean
cb_validation_allow_blank (GnmValidation const *v)
{
	return v->allow_blank;
}

static gboolean
cb_validation_use_dropdown (GnmValidation const *v)
{
	return v->use_dropdown;
}

static void
xml_style_changed (GnmDiffState *state, GnmRange const *r,
		   G_GNUC_UNUSED Sheet const *osh,
		   G_GNUC_UNUSED Sheet const *nsh,
		   GnmStyle const *os, GnmStyle const *ns)
{
	unsigned int conflicts;
	GnmStyleElement e;

	xml_open_section (state, DIFF "Styles");

	gsf_xml_out_start_element (state->xml, DIFF "StyleRegion");
	gsf_xml_out_add_uint (state->xml, "startCol", r->start.col);
	gsf_xml_out_add_uint (state->xml, "startRow", r->start.row);
	gsf_xml_out_add_uint (state->xml, "endCol", r->end.col);
	gsf_xml_out_add_uint (state->xml, "endRow", r->end.row);

	conflicts = gnm_style_find_differences (os, ns, TRUE);
	for (e = 0; e < MSTYLE_ELEMENT_MAX; e++) {
		if ((conflicts & (1u << e)) == 0)
			continue;
		switch (e) {
		case MSTYLE_COLOR_BACK: {
			GnmColor *oc = gnm_style_get_back_color (os);
			GnmColor *nc = gnm_style_get_back_color (ns);

			gsf_xml_out_start_element (state->xml, "BackColor");
			gnm_xml_out_add_gocolor (state->xml, "Old", oc->go_color);
			gnm_xml_out_add_gocolor (state->xml, "New", nc->go_color);
			if (oc->is_auto != nc->is_auto) {
				gsf_xml_out_add_int (state->xml, "OldAuto", oc->is_auto);
				gsf_xml_out_add_int (state->xml, "NewAuto", nc->is_auto);
			}
			gsf_xml_out_end_element (state->xml);
			break;
		}

		case MSTYLE_COLOR_PATTERN:
			gsf_xml_out_start_element (state->xml, "PatternColor");
			gnm_xml_out_add_gocolor (state->xml, "Old", gnm_style_get_pattern_color (os)->go_color);
			gnm_xml_out_add_gocolor (state->xml, "New", gnm_style_get_pattern_color (ns)->go_color);
			gsf_xml_out_end_element (state->xml);
			break;

		case MSTYLE_BORDER_TOP:
		case MSTYLE_BORDER_BOTTOM:
		case MSTYLE_BORDER_LEFT:
		case MSTYLE_BORDER_RIGHT:
		case MSTYLE_BORDER_REV_DIAGONAL:
		case MSTYLE_BORDER_DIAGONAL: {
			static char const *border_names[] = {
				"Top",
				"Bottom",
				"Left",
				"Right",
				"Rev-Diagonal",
				"Diagonal"
			};

			char *tag = g_strconcat ("Border",
						 border_names[e - MSTYLE_BORDER_TOP],
						 NULL);
			GnmBorder const *ob = gnm_style_get_border (os, e);
			GnmBorder const *nb = gnm_style_get_border (ns, e);
			gsf_xml_out_start_element (state->xml, tag);
			gsf_xml_out_add_int (state->xml, "OldType", ob->line_type);
			gsf_xml_out_add_int (state->xml, "NewType", nb->line_type);
			if (ob->line_type != GNM_STYLE_BORDER_NONE)
				gnm_xml_out_add_gocolor (state->xml, "OldColor", ob->color->go_color);
			if (nb->line_type != GNM_STYLE_BORDER_NONE)
				gnm_xml_out_add_gocolor (state->xml, "NewColor", nb->color->go_color);
			gsf_xml_out_end_element (state->xml);
			g_free (tag);
			break;
		}

		case MSTYLE_PATTERN:
			DO_INT ("Pattern", gnm_style_get_pattern);
			break;

		case MSTYLE_FONT_COLOR:
			gsf_xml_out_start_element (state->xml, "FontColor");
			gnm_xml_out_add_gocolor (state->xml, "Old", gnm_style_get_font_color (os)->go_color);
			gnm_xml_out_add_gocolor (state->xml, "New", gnm_style_get_font_color (ns)->go_color);
			gsf_xml_out_end_element (state->xml);
			break;

		case MSTYLE_FONT_NAME:
			gsf_xml_out_start_element (state->xml, "FontName");
			gsf_xml_out_add_cstr (state->xml, "Old", gnm_style_get_font_name (os));
			gsf_xml_out_add_cstr (state->xml, "New", gnm_style_get_font_name (ns));
			gsf_xml_out_end_element (state->xml);
			break;

		case MSTYLE_FONT_BOLD:
			DO_INT ("Bold", gnm_style_get_font_bold);
			break;

		case MSTYLE_FONT_ITALIC:
			DO_INT ("Italic", gnm_style_get_font_italic);
			break;

		case MSTYLE_FONT_UNDERLINE:
			DO_INT ("Underline", gnm_style_get_font_uline);
			break;

		case MSTYLE_FONT_STRIKETHROUGH:
			DO_INT ("Strike", gnm_style_get_font_strike);
			break;

		case MSTYLE_FONT_SCRIPT:
			DO_INT ("Script", gnm_style_get_font_script);
			break;

		case MSTYLE_FONT_SIZE:
			gsf_xml_out_start_element (state->xml, "FontSize");
			gsf_xml_out_add_float (state->xml, "Old", gnm_style_get_font_size (os), 4);
			gsf_xml_out_add_float (state->xml, "New", gnm_style_get_font_size (ns), 4);
			gsf_xml_out_end_element (state->xml);
			break;

		case MSTYLE_FORMAT:
			gsf_xml_out_start_element (state->xml, "Format");
			gsf_xml_out_add_cstr (state->xml, "Old", go_format_as_XL (gnm_style_get_format (os)));
			gsf_xml_out_add_cstr (state->xml, "New", go_format_as_XL (gnm_style_get_format (ns)));
			gsf_xml_out_end_element (state->xml);
			break;

		case MSTYLE_ALIGN_V:
			DO_INT ("VALign", gnm_style_get_align_v);
			break;

		case MSTYLE_ALIGN_H:
			DO_INT ("HALign", gnm_style_get_align_h);
			break;

		case MSTYLE_INDENT:
			DO_INT ("Indent", gnm_style_get_indent);
			break;

		case MSTYLE_ROTATION:
			DO_INT ("Rotation", gnm_style_get_rotation);
			break;

		case MSTYLE_TEXT_DIR:
			DO_INT ("TextDirection", gnm_style_get_text_dir);
			break;

		case MSTYLE_WRAP_TEXT:
			DO_INT ("WrapText", gnm_style_get_wrap_text);
			break;

		case MSTYLE_SHRINK_TO_FIT:
			DO_INT ("ShrinkToFit", gnm_style_get_shrink_to_fit);
			break;

		case MSTYLE_CONTENTS_LOCKED:
			DO_INT ("Locked", gnm_style_get_contents_locked);
			break;

		case MSTYLE_CONTENTS_HIDDEN:
			DO_INT ("Hidden", gnm_style_get_contents_hidden);
			break;

		case MSTYLE_HLINK: {
			GnmHLink const *ol = gnm_style_get_hlink (os);
			GnmHLink const *nl = gnm_style_get_hlink (ns);

			gsf_xml_out_start_element (state->xml, "HLink");
			if (ol) {
				gsf_xml_out_add_cstr (state->xml, "OldTarget", gnm_hlink_get_target (ol));
				gsf_xml_out_add_cstr (state->xml, "OldTip", gnm_hlink_get_tip (ol));
			}
			if (nl) {
				gsf_xml_out_add_cstr (state->xml, "NewTarget", gnm_hlink_get_target (nl));
				gsf_xml_out_add_cstr (state->xml, "NewTip", gnm_hlink_get_tip (nl));
			}
			gsf_xml_out_end_element (state->xml); /* </HLink> */

			break;
		}

		case MSTYLE_VALIDATION: {
			GnmValidation const *ov = gnm_style_get_validation (os);
			GnmValidation const *nv = gnm_style_get_validation (ns);
			gsf_xml_out_start_element (state->xml, "Validation");
			DO_STRINGS ("Message", cb_validation_message, ov, nv);
			DO_STRINGS ("Title", cb_validation_title, ov, nv);
			DO_INTS ("AllowBlank", cb_validation_allow_blank, ov, nv);
			DO_INTS ("UseDropdown", cb_validation_use_dropdown, ov, nv);
			gsf_xml_out_end_element (state->xml); /* </Validation> */
			break;
		}

		case MSTYLE_INPUT_MSG: {
			GnmInputMsg const *om = gnm_style_get_input_msg (os);
			GnmInputMsg const *nm = gnm_style_get_input_msg (ns);

			gsf_xml_out_start_element (state->xml, "InputMessage");
			DO_STRINGS ("Message", gnm_input_msg_get_msg, om, nm);
			DO_STRINGS ("Title", gnm_input_msg_get_title, om, nm);
			gsf_xml_out_end_element (state->xml); /* </InputMessage> */
			break;
		}

		case MSTYLE_CONDITIONS:
			gsf_xml_out_start_element (state->xml, "Conditions");
			gsf_xml_out_end_element (state->xml); /* </Conditions> */
			break;

		default:
			gsf_xml_out_start_element (state->xml, "Other");
			gsf_xml_out_end_element (state->xml); /* </Other> */
			break;
		}
	}

	gsf_xml_out_end_element (state->xml); /* </StyleRegion> */
}

#undef DO_INT

static const GnmDiffActions xml_actions = {
	xml_diff_start,
	xml_diff_end,
	xml_sheet_start,
	xml_sheet_end,
	null_sheet_order_changed,
	xml_sheet_attr_int_changed,
	xml_colrow_changed,
	xml_cell_changed,
	xml_style_changed,
};

/* -------------------------------------------------------------------------- */

static gboolean
highlight_diff_start (GnmDiffState *state)
{
	const char *dst = state->new.url;

	state->highlight_fs = go_file_saver_for_file_name (dst);
	if (!state->highlight_fs) {
		g_printerr (_("%s: Unable to guess exporter to use for %s.\n"),
			    g_get_prgname (),
			    dst);

		return TRUE;
	}

	/* We need a copy of one of the files.  Rereading is easy.  */
	g_object_ref ((state->highlight.input = state->new.input));
	gsf_input_seek (state->highlight.input, 0, G_SEEK_SET);
	if (read_file (&state->highlight, dst, state->ioc))
		return TRUE;

	/* We apply a solid #F3F315 to changed cells.  */
	state->highlight_style = gnm_style_new ();
	gnm_style_set_back_color (state->highlight_style,
				  gnm_color_new_rgb8 (0xf3, 0xf3, 0x15));
	gnm_style_set_pattern (state->highlight_style, 1);

	return FALSE;
}

static void
highlight_diff_end (GnmDiffState *state)
{
	wbv_save_to_output (state->highlight.wbv, state->highlight_fs,
			    state->output, state->ioc);
}

static void
highlight_apply (GnmDiffState *state, const char *sheetname,
		 const GnmRange *r)
{
	Sheet *sheet = workbook_sheet_by_name (state->highlight.wb,
					       sheetname);
	if (!sheet)
		return;

	gnm_style_ref (state->highlight_style);
	sheet_style_apply_range (sheet, r, state->highlight_style);
}

static void
highlight_cell_changed (GnmDiffState *state,
			GnmCell const *oc, GnmCell const *nc)
{
	GnmRange r;
	const char *sheetname;

	r.start = nc ? nc->pos : oc->pos;
	r.end = r.start;

	sheetname = nc ? nc->base.sheet->name_unquoted : oc->base.sheet->name_unquoted;
	highlight_apply (state, sheetname, &r);
}

static void
highlight_style_changed (GnmDiffState *state, GnmRange const *r,
			 G_GNUC_UNUSED Sheet const *osh,
			 Sheet const *nsh,
			 G_GNUC_UNUSED GnmStyle const *os,
			 G_GNUC_UNUSED GnmStyle const *ns)
{
	highlight_apply (state, nsh->name_unquoted, r);
}


static const GnmDiffActions highlight_actions = {
	highlight_diff_start,
	highlight_diff_end,
	null_sheet_start,
	null_sheet_end,
	null_sheet_order_changed,
	null_sheet_attr_int_changed,
	null_colrow_changed,
	highlight_cell_changed,
	highlight_style_changed,
};

/* -------------------------------------------------------------------------- */

static gboolean
compare_corresponding_cells (GnmCell const *co, GnmCell const *cn)
{
	gboolean has_expr = gnm_cell_has_expr (co);
	gboolean has_value = co->value != NULL;

	if (has_expr != gnm_cell_has_expr (cn))
		return TRUE;
	if (has_expr) {
		char *so, *sn;
		GnmParsePos ppo, ppn;
		gboolean eq;

		if (gnm_expr_top_equal (co->base.texpr, cn->base.texpr))
			return FALSE;

		// Not equal, but with references to sheets, that is not
		// necessary.  Compare as strings.

		parse_pos_init_cell (&ppo, co);
		so = gnm_expr_top_as_string (co->base.texpr, &ppo, sheet_get_conventions (co->base.sheet));

		parse_pos_init_cell (&ppn, cn);
		sn = gnm_expr_top_as_string (cn->base.texpr, &ppn, sheet_get_conventions (cn->base.sheet));

		eq = g_strcmp0 (so, sn) == 0;

		g_free (so);
		g_free (sn);

		return !eq;
	}

	if (has_value != (cn->value != NULL))
		return TRUE;
	if (has_value)
		return !(value_equal (co->value, cn->value) &&
			 go_format_eq (VALUE_FMT (co->value),
				       VALUE_FMT (cn->value)));


	return FALSE;
}

static gboolean
ignore_cell (GnmCell const *cell)
{
	if (cell) {
		if (gnm_cell_has_expr (cell)) {
			return gnm_expr_top_is_array_elem (cell->base.texpr,
							   NULL, NULL);
		} else {
			return VALUE_IS_EMPTY (cell->value);
		}
	}
	return FALSE;
}

static void
diff_sheets_cells (GnmDiffState *state, Sheet *old_sheet, Sheet *new_sheet)
{
	GPtrArray *old_cells = sheet_cells (old_sheet, NULL);
	GPtrArray *new_cells = sheet_cells (new_sheet, NULL);
	size_t io = 0, in = 0;

	/* Make code below simpler.  */
	g_ptr_array_add (old_cells, NULL);
	g_ptr_array_add (new_cells, NULL);

	while (TRUE) {
		GnmCell const *co, *cn;

		while (ignore_cell ((co = g_ptr_array_index (old_cells, io))))
			io++;

		while (ignore_cell ((cn = g_ptr_array_index (new_cells, in))))
			in++;

		if (co && cn) {
			int order = co->pos.row == cn->pos.row
				? co->pos.col - cn->pos.col
				: co->pos.row - cn->pos.row;
			if (order < 0)
				cn = NULL;
			else if (order > 0)
				co = NULL;
			else {
				if (compare_corresponding_cells (co, cn)) {
					state->diff_found = TRUE;
					state->actions->cell_changed (state, co, cn);
				}
				io++, in++;
				continue;
			}
		}

		if (co) {
			state->diff_found = TRUE;
			state->actions->cell_changed (state, co, NULL);
			io++;
		} else if (cn) {
			state->diff_found = TRUE;
			state->actions->cell_changed (state, NULL, cn);
			in++;
		} else
			break;
	}

	g_ptr_array_free (old_cells, TRUE);
	g_ptr_array_free (new_cells, TRUE);
}

static void
diff_sheets_colrow (GnmDiffState *state, Sheet *old_sheet, Sheet *new_sheet, gboolean is_cols)
{
	ColRowInfo const *old_def = sheet_colrow_get_default (old_sheet, is_cols);
	ColRowInfo const *new_def = sheet_colrow_get_default (new_sheet, is_cols);
	int i, N;

	if (!colrow_equal (old_def, new_def))
		state->actions->colrow_changed (state, old_def, new_def, is_cols, -1);

	N = MIN (colrow_max (is_cols, old_sheet), colrow_max (is_cols, new_sheet));
	for (i = 0; i < N; i++) {
		ColRowInfo const *ocr = sheet_colrow_get (old_sheet, i, is_cols);
		ColRowInfo const *ncr = sheet_colrow_get (new_sheet, i, is_cols);

		if (ocr == ncr)
			continue; // Considered equal, even if defaults are different
		if (!ocr) ocr = old_def;
		if (!ncr) ncr = new_def;
		if (!colrow_equal (ocr, ncr))
			state->actions->colrow_changed (state, ocr, ncr, is_cols, i);
	}
}


#define DO_INT(field,attr)						\
	do {								\
		if (old_sheet->field != new_sheet->field) {		\
			state->diff_found = TRUE;			\
			state->actions->sheet_attr_int_changed		\
				(state, attr, old_sheet->field, new_sheet->field); \
		}							\
} while (0)

static void
diff_sheets_attrs (GnmDiffState *state, Sheet *old_sheet, Sheet *new_sheet)
{
	GnmSheetSize const *os = gnm_sheet_get_size (old_sheet);
	GnmSheetSize const *ns = gnm_sheet_get_size (new_sheet);

	if (os->max_cols != ns->max_cols) {
		state->diff_found = TRUE;
		state->actions->sheet_attr_int_changed
			(state, "Cols", os->max_cols, ns->max_cols);
	}
	if (os->max_rows != ns->max_rows) {
		state->diff_found = TRUE;
		state->actions->sheet_attr_int_changed
			(state, "Rows", os->max_rows, ns->max_rows);
	}

	DO_INT (display_formulas, "DisplayFormulas");
	DO_INT (hide_zero, "HideZero");
	DO_INT (hide_grid, "HideGrid");
	DO_INT (hide_col_header, "HideColHeader");
	DO_INT (hide_row_header, "HideRowHeader");
	DO_INT (display_outlines, "DisplayOutlines");
	DO_INT (outline_symbols_below, "OutlineSymbolsBelow");
	DO_INT (outline_symbols_right, "OutlineSymbolsRight");
	DO_INT (text_is_rtl, "RTL_Layout");
	DO_INT (is_protected, "Protected");
	DO_INT (visibility, "Visibility");
}
#undef DO_INT

struct cb_diff_sheets_styles {
	GnmDiffState *state;
	Sheet const *old_sheet;
	Sheet const *new_sheet;
	GnmStyle *old_style;
};

static void
cb_diff_sheets_styles_2 (G_GNUC_UNUSED gpointer key,
			 gpointer sr_, gpointer user_data)
{
	GnmStyleRegion *sr = sr_;
	struct cb_diff_sheets_styles *data = user_data;
	GnmRange r = sr->range;

	if (gnm_style_find_differences (data->old_style, sr->style, TRUE) == 0)
		return;

	data->state->diff_found = TRUE;

	data->state->actions->style_changed (data->state, &r,
					     data->old_sheet, data->new_sheet,
					     data->old_style, sr->style);
}

static void
cb_diff_sheets_styles_1 (G_GNUC_UNUSED gpointer key,
			 gpointer sr_, gpointer user_data)
{
	GnmStyleRegion *sr = sr_;
	struct cb_diff_sheets_styles *data = user_data;

	data->old_style = sr->style;
	sheet_style_range_foreach (data->new_sheet, &sr->range,
				   cb_diff_sheets_styles_2,
				   data);
}

static void
diff_sheets_styles (GnmDiffState *state, Sheet *old_sheet, Sheet *new_sheet)
{
	GnmSheetSize const *os = gnm_sheet_get_size (old_sheet);
	GnmSheetSize const *ns = gnm_sheet_get_size (new_sheet);
	GnmRange r;
	struct cb_diff_sheets_styles data;

	/* Compare largest common area only.  */
	range_init (&r, 0, 0,
		    MIN (os->max_cols, ns->max_cols) - 1,
		    MIN (os->max_rows, ns->max_rows) - 1);

	data.state = state;
	data.old_sheet = old_sheet;
	data.new_sheet = new_sheet;
	sheet_style_range_foreach (old_sheet, &r,
				   cb_diff_sheets_styles_1,
				   &data);
}

static void
diff_sheets (GnmDiffState *state, Sheet *old_sheet, Sheet *new_sheet)
{
	diff_sheets_attrs (state, old_sheet, new_sheet);
	diff_sheets_colrow (state, old_sheet, new_sheet, TRUE);
	diff_sheets_colrow (state, old_sheet, new_sheet, FALSE);
	diff_sheets_cells (state, old_sheet, new_sheet);
	diff_sheets_styles (state, old_sheet, new_sheet);
}

static int
diff (char const *oldfilename, char const *newfilename,
      GOIOContext *ioc,
      GnmDiffActions const *actions, GsfOutput *output)
{
	GnmDiffState state;
	int res = 0;
	int i, count;
	gboolean sheet_order_changed = FALSE;
	int last_index = -1;
	GnmLocale *locale;

	locale = gnm_push_C_locale ();

	memset (&state, 0, sizeof (state));
	state.actions = actions;
	state.ioc = ioc;
	state.output = output;

	if (read_file (&state.old, oldfilename, ioc))
		goto error;
	if (read_file (&state.new, newfilename, ioc))
		goto error;

	/* ---------------------------------------- */

	if (state.actions->diff_start (&state))
		goto error;

	/*
	 * This doesn't handle sheet renames very well, but simply considers
	 * that a sheet deletion and a sheet insert.
	 */

	count = workbook_sheet_count (state.old.wb);
	for (i = 0; i < count; i++) {
		Sheet *old_sheet = workbook_sheet_by_index (state.old.wb, i);
		Sheet *new_sheet = workbook_sheet_by_name (state.new.wb,
							   old_sheet->name_unquoted);
		state.actions->sheet_start (&state, old_sheet, new_sheet);

		if (new_sheet) {
			if (new_sheet->index_in_wb < last_index)
				sheet_order_changed = TRUE;
			last_index = new_sheet->index_in_wb;

			diff_sheets (&state, old_sheet, new_sheet);
		}

		state.actions->sheet_end (&state);
	}

	count = workbook_sheet_count (state.new.wb);
	for (i = 0; i < count; i++) {
		Sheet *new_sheet = workbook_sheet_by_index (state.new.wb, i);
		Sheet *old_sheet = workbook_sheet_by_name (state.old.wb,
							   new_sheet->name_unquoted);
		if (old_sheet)
			; /* Nothing -- already done above. */
		else {
			state.actions->sheet_start (&state, NULL, new_sheet);
			state.actions->sheet_end (&state);
		}
	}

	if (sheet_order_changed) {
		state.diff_found = TRUE;
		state.actions->sheet_order_changed (&state);
	}

	state.actions->diff_end (&state);

out:
	clear_file_state (&state.old);
	clear_file_state (&state.new);
	clear_file_state (&state.highlight);
	g_clear_object (&state.xml);
	if (state.convs)
		gnm_conventions_unref (state.convs);
	if (state.highlight_style)
		gnm_style_unref (state.highlight_style);

	gnm_pop_C_locale (locale);

	if (res == 0)
		res = state.diff_found ? 1 : 0;

	return res;

error:
	res = 2;
	goto out;
}

int
main (int argc, char const **argv)
{
	GOErrorInfo	*plugin_errs;
	int		 res = 0;
	GOCmdContext	*cc;
	GOptionContext *ocontext;
	GError *error = NULL;
	const GnmDiffActions *actions;
	char *output_uri;
	GsfOutput *output;

	/* No code before here, we need to init threads */
	argv = gnm_pre_parse_init (argc, argv);

	ocontext = g_option_context_new (_("OLDFILE NEWFILE"));
	g_option_context_add_main_entries (ocontext, ssdiff_options, GETTEXT_PACKAGE);
	g_option_context_add_group	  (ocontext, gnm_get_option_group ());
	g_option_context_parse (ocontext, &argc, (char ***)&argv, &error);
	g_option_context_free (ocontext);

	if (error) {
		g_printerr (_("%s\nRun '%s --help' to see a full list of available command line options.\n"),
			    error->message, argv[0]);
		g_error_free (error);
		return 1;
	}

	if (ssdiff_show_version) {
		g_print (_("ssdiff version '%s'\ndatadir := '%s'\nlibdir := '%s'\n"),
			 GNM_VERSION_FULL, gnm_sys_data_dir (), gnm_sys_lib_dir ());
		return 0;
	}

	if (ssdiff_xml + ssdiff_highlight > 1) {
		g_printerr (_("%s: Only one output format may be specified.\n"),
			    g_get_prgname ());
		return 1;
	}

	if (ssdiff_highlight) {
		actions = &highlight_actions;
	} else if (ssdiff_xml) {
		actions = &xml_actions;
	} else {
		actions = &default_actions;
	}

	if (!ssdiff_output)
		ssdiff_output = g_strdup ("fd://1");
	output_uri = go_shell_arg_to_uri (ssdiff_output);
	output = go_file_create (output_uri, &error);
	g_free (output_uri);
	if (!output) {
		g_printerr (_("%s: Failed to create output file: %s\n"),
			    g_get_prgname (),
			    error ? error->message : "?");
		if (error)
			g_error_free (error);
		return 1;
	}

	gnm_init ();

	cc = gnm_cmd_context_stderr_new ();
	gnm_plugins_init (GO_CMD_CONTEXT (cc));
	go_plugin_db_activate_plugin_list (
		go_plugins_get_available_plugins (), &plugin_errs);
	if (plugin_errs) {
		/* FIXME: What do we want to do here? */
		go_error_info_free (plugin_errs);
	}
	go_component_set_default_command_context (cc);

	if (argc == 3) {
		GOIOContext *ioc = go_io_context_new (cc);
		res = diff (argv[1], argv[2], ioc, actions, output);
		g_object_unref (ioc);
	} else {
		g_printerr (_("Usage: %s [OPTION...] %s\n"),
			    g_get_prgname (),
			    _("OLDFILE NEWFILE"));
		res = 2;
	}

	/* Release cached string. */
	def_cell_name (NULL);
	g_object_unref (output);

	go_component_set_default_command_context (NULL);
	g_object_unref (cc);
	gnm_shutdown ();
	gnm_pre_parse_shutdown ();

	return res;
}
