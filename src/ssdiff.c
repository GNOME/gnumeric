/*
 * ssdiff.c: A diff program for spreadsheets.
 *
 * Author:
 *   Morten Welinder <terra@gnome.org>
 *
 * Copyright (C) 2012-2018 Morten Welinder (terra@gnome.org)
 */

#include <gnumeric-config.h>
#include <glib/gi18n.h>
#include <gnumeric.h>
#include <goffice/goffice.h>
#include <libgnumeric.h>
#include <gutils.h>
#include <command-context.h>
#include <command-context-stderr.h>
#include <gnm-plugin.h>
#include <workbook-view.h>
#include <workbook.h>
#include <sheet.h>
#include <sheet-style.h>
#include <style-border.h>
#include <style-color.h>
#include <cell.h>
#include <value.h>
#include <expr.h>
#include <ranges.h>
#include <mstyle.h>
#include <xml-sax.h>
#include <hlink.h>
#include <input-msg.h>
#include <expr-name.h>
#include <sheet-diff.h>
#include <gnumeric-conf.h>

#include <gsf/gsf-libxml.h>
#include <gsf/gsf-output-stdio.h>
#include <gsf/gsf-input.h>

#define DIFF "s:"
#define SSDIFF_DTD "http://www.gnumeric.org/ssdiff.dtd" // No such file yet

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

typedef struct {
	char *url;
	GsfInput *input;
	Workbook *wb;
	WorkbookView *wbv;
} GnmDiffStateFile;

typedef struct {
	GOIOContext *ioc;
	GnmDiffStateFile old, new;

	GsfOutput *output;

	// The following for xml mode.
	GsfXMLOut *xml;
	const char *xml_section;
	GnmConventions *xml_convs;

	// The following for highlight mode.
	Sheet *highlight_sheet;
	GnmDiffStateFile highlight;
	GOFileSaver const *highlight_fs;
	GnmStyle *highlight_style;
} DiffState;

/* -------------------------------------------------------------------------- */

static gboolean
read_file (GnmDiffStateFile *dsf, const char *filename, GOIOContext *ioc)
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
clear_file_state (GnmDiffStateFile *dsf)
{
	g_free (dsf->url);
	dsf->url = NULL;
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
def_sheet_start (gpointer user, Sheet const *os, Sheet const *ns)
{
	DiffState *state = user;
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
def_sheet_order_changed (gpointer user)
{
	DiffState *state = user;
	gsf_output_printf (state->output, _("Sheet order changed.\n"));
}

static void
def_sheet_attr_int_changed (gpointer user, const char *name,
			    G_GNUC_UNUSED int o, G_GNUC_UNUSED int n)
{
	DiffState *state = user;
	gsf_output_printf (state->output, _("Sheet attribute %s changed.\n"),
			   name);
}

static void
def_colrow_changed (gpointer user, ColRowInfo const *oc, ColRowInfo const *nc,
		    gboolean is_cols, int i)
{
	DiffState *state = user;
	if (is_cols)
		gsf_output_printf (state->output, _("Column %s changed.\n"),
				   col_name (i));
	else
		gsf_output_printf (state->output, _("Row %d changed.\n"),
				   i + 1);
}

static void
def_cell_changed (gpointer user, GnmCell const *oc, GnmCell const *nc)
{
	DiffState *state = user;
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
def_style_changed (gpointer user, GnmRange const *r,
		   G_GNUC_UNUSED GnmStyle const *os,
		   G_GNUC_UNUSED GnmStyle const *ns)
{
	DiffState *state = user;
	gsf_output_printf (state->output, _("Style of %s was changed.\n"),
			   range_as_string (r));
}

static void
def_name_changed (gpointer user,
		  GnmNamedExpr const *on, GnmNamedExpr const *nn)
{
	DiffState *state = user;
	if (on && nn)
		gsf_output_printf (state->output, _("Name %s changed.\n"), expr_name_name (on));
	else if (on)
		gsf_output_printf (state->output, _("Name %s removed.\n"), expr_name_name (on));
	else if (nn)
		gsf_output_printf (state->output, _("Name %s added.\n"), expr_name_name (nn));
	else
		g_assert_not_reached ();
}

static const GnmDiffActions default_actions = {
	.sheet_start = def_sheet_start,
	.sheet_order_changed = def_sheet_order_changed,
	.sheet_attr_int_changed = def_sheet_attr_int_changed,
	.colrow_changed = def_colrow_changed,
	.cell_changed = def_cell_changed,
	.style_changed = def_style_changed,
	.name_changed = def_name_changed,
};

/* -------------------------------------------------------------------------- */

static gboolean
xml_diff_start (gpointer user)
{
	DiffState *state = user;
	char *attr;

	state->xml = gsf_xml_out_new (state->output);
	state->xml_convs = gnm_xml_io_conventions ();

	gsf_xml_out_start_element (state->xml, DIFF "Diff");
	attr = g_strdup ("xmlns:" DIFF);
	attr[strlen (attr) - 1] = 0;
	gsf_xml_out_add_cstr (state->xml, attr, SSDIFF_DTD);
	g_free (attr);

	return FALSE;
}

static void
xml_diff_end (gpointer user)
{
	DiffState *state = user;
	gsf_xml_out_end_element (state->xml); /* </Diff> */
}

static void
xml_dtor (gpointer user)
{
	DiffState *state = user;
	g_clear_object (&state->xml);

	if (state->xml_convs) {
		gnm_conventions_unref (state->xml_convs);
		state->xml_convs = NULL;
	}
}

static void
xml_close_section (DiffState *state)
{
	if (state->xml_section) {
		gsf_xml_out_end_element (state->xml);
		state->xml_section = NULL;
	}
}

static void
xml_open_section (DiffState *state, const char *section)
{
	if (state->xml_section && g_str_equal (section, state->xml_section))
		return;

	xml_close_section (state);
	gsf_xml_out_start_element (state->xml, section);
	state->xml_section = section;
}

static void
xml_sheet_start (gpointer user, Sheet const *os, Sheet const *ns)
{
	DiffState *state = user;
	Sheet const *sheet = os ? os : ns;

	// We might have an open section for global names
	xml_close_section (state);

	gsf_xml_out_start_element (state->xml, DIFF "Sheet");
	gsf_xml_out_add_cstr (state->xml, "Name", sheet->name_unquoted);
	if (os)
		gsf_xml_out_add_int (state->xml, "Old", os->index_in_wb);
	if (ns)
		gsf_xml_out_add_int (state->xml, "New", ns->index_in_wb);
}

static void
xml_sheet_end (gpointer user)
{
	DiffState *state = user;
	xml_close_section (state);
	gsf_xml_out_end_element (state->xml); /* </Sheet> */
}

static void
xml_sheet_attr_int_changed (gpointer user, const char *name,
			    int o, int n)
{
	DiffState *state = user;
	char *elem;

	elem = g_strconcat (DIFF, name, NULL);
	gsf_xml_out_start_element (state->xml, elem);
	gsf_xml_out_add_int (state->xml, "Old", o);
	gsf_xml_out_add_int (state->xml, "New", n);
	gsf_xml_out_end_element (state->xml); /* elem */
	g_free (elem);
}

static void
xml_output_texpr (DiffState *state, GnmExprTop const *texpr, GnmParsePos const *pos,
		  const char *tag)
{
	GnmConventionsOut out;
	GString *str;

	out.accum = str = g_string_sized_new (100);
	out.pp    = pos;
	out.convs = state->xml_convs;

	g_string_append_c (str, '=');
	gnm_expr_top_as_gstring (texpr, &out);

	gsf_xml_out_add_cstr (state->xml, tag, str->str);
	g_string_free (str, TRUE);
}

static void
xml_output_cell (DiffState *state, GnmCell const *cell,
		 const char *tag, const char *valtag, const char *fmttag)
{
	if (!cell)
		return;

	if (gnm_cell_has_expr (cell)) {
		GnmParsePos pp;
		parse_pos_init_cell (&pp, cell);
		xml_output_texpr (state, cell->base.texpr, &pp, tag);
	} else {
		GnmValue const *v = cell->value;
		GString *str = g_string_sized_new (100);
		value_get_as_gstring (v, str, state->xml_convs);
		gsf_xml_out_add_cstr (state->xml, tag, str->str);
		g_string_free (str, TRUE);
		gsf_xml_out_add_int (state->xml, valtag, v->v_any.type);
		if (VALUE_FMT (v))
			gsf_xml_out_add_cstr (state->xml, fmttag, go_format_as_XL (VALUE_FMT (v)));
	}
}

static void
xml_colrow_changed (gpointer user, ColRowInfo const *oc, ColRowInfo const *nc,
		    gboolean is_cols, int i)
{
	DiffState *state = user;
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
xml_cell_changed (gpointer user, GnmCell const *oc, GnmCell const *nc)
{
	DiffState *state = user;
	const GnmCellPos *pos;

	xml_open_section (state, DIFF "Cells");

	gsf_xml_out_start_element (state->xml, DIFF "Cell");

	pos = oc ? &oc->pos : &nc->pos;
	gsf_xml_out_add_int (state->xml, "Row", pos->row);
	gsf_xml_out_add_int (state->xml, "Col", pos->col);

	xml_output_cell (state, oc, "Old", "OldValueType", "OldValueFormat");
	xml_output_cell (state, nc, "New", "NewValueType", "NewValueFormat");

	gsf_xml_out_end_element (state->xml); /* </Cell> */
}

#define DO_INT(what,fun)					\
  do {								\
	  gsf_xml_out_start_element (state->xml, DIFF what);	\
	  gsf_xml_out_add_int (state->xml, "Old", (fun) (os));	\
	  gsf_xml_out_add_int (state->xml, "New", (fun) (ns));	\
	  gsf_xml_out_end_element (state->xml);			\
  } while (0)

#define DO_INTS(what,fun,oobj,nobj)					\
  do {									\
	  int oi = (oobj) ? (fun) (oobj) : 0;				\
	  int ni = (nobj) ? (fun) (nobj) : 0;				\
	  if (oi != ni || !(oobj) != !(nobj)) {				\
		  gsf_xml_out_start_element (state->xml, DIFF what);	\
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
		  gsf_xml_out_start_element (state->xml, DIFF what);	\
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
xml_style_changed (gpointer user, GnmRange const *r,
		   GnmStyle const *os, GnmStyle const *ns)
{
	DiffState *state = user;
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

			gsf_xml_out_start_element (state->xml, DIFF "BackColor");
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
			gsf_xml_out_start_element (state->xml, DIFF "PatternColor");
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

			char *tag = g_strconcat (DIFF "Border",
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
			gsf_xml_out_start_element (state->xml, DIFF "FontColor");
			gnm_xml_out_add_gocolor (state->xml, "Old", gnm_style_get_font_color (os)->go_color);
			gnm_xml_out_add_gocolor (state->xml, "New", gnm_style_get_font_color (ns)->go_color);
			gsf_xml_out_end_element (state->xml);
			break;

		case MSTYLE_FONT_NAME:
			gsf_xml_out_start_element (state->xml, DIFF "FontName");
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
			gsf_xml_out_start_element (state->xml, DIFF "FontSize");
			gsf_xml_out_add_float (state->xml, "Old", gnm_style_get_font_size (os), 4);
			gsf_xml_out_add_float (state->xml, "New", gnm_style_get_font_size (ns), 4);
			gsf_xml_out_end_element (state->xml);
			break;

		case MSTYLE_FORMAT:
			gsf_xml_out_start_element (state->xml, DIFF "Format");
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

			gsf_xml_out_start_element (state->xml, DIFF "HLink");
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
			gsf_xml_out_start_element (state->xml, DIFF "Validation");
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

			gsf_xml_out_start_element (state->xml, DIFF "InputMessage");
			DO_STRINGS ("Message", gnm_input_msg_get_msg, om, nm);
			DO_STRINGS ("Title", gnm_input_msg_get_title, om, nm);
			gsf_xml_out_end_element (state->xml); /* </InputMessage> */
			break;
		}

		case MSTYLE_CONDITIONS:
			gsf_xml_out_start_element (state->xml, DIFF "Conditions");
			gsf_xml_out_end_element (state->xml); /* </Conditions> */
			break;

		default:
			gsf_xml_out_start_element (state->xml, DIFF "Other");
			gsf_xml_out_end_element (state->xml); /* </Other> */
			break;
		}
	}

	gsf_xml_out_end_element (state->xml); /* </StyleRegion> */
}

#undef DO_INT
#undef DO_INTS
#undef DO_STRINGS

static void
xml_name_changed (gpointer user,
		  GnmNamedExpr const *on, GnmNamedExpr const *nn)
{
	DiffState *state = user;
	xml_open_section (state, DIFF "Names");

	gsf_xml_out_start_element (state->xml, DIFF "Name");
	gsf_xml_out_add_cstr (state->xml, "Name", expr_name_name (on ? on : nn));
	if (on)
		xml_output_texpr (state, on->texpr, &on->pos, "Old");
	if (nn)
		xml_output_texpr (state, nn->texpr, &nn->pos, "New");
	gsf_xml_out_end_element (state->xml); /* </Name> */
}

static const GnmDiffActions xml_actions = {
	.diff_start = xml_diff_start,
	.diff_end = xml_diff_end,
	.dtor = xml_dtor,
	.sheet_start = xml_sheet_start,
	.sheet_end = xml_sheet_end,
	.sheet_attr_int_changed = xml_sheet_attr_int_changed,
	.colrow_changed = xml_colrow_changed,
	.cell_changed = xml_cell_changed,
	.style_changed = xml_style_changed,
	.name_changed = xml_name_changed,
};

/* -------------------------------------------------------------------------- */

static gboolean
highlight_diff_start (gpointer user)
{
	DiffState *state = user;
	const char *dst = state->new.url;

	state->highlight_fs = go_file_saver_for_file_name (ssdiff_output);
	if (!state->highlight_fs) {
		g_printerr (_("%s: Unable to guess exporter to use for %s.\n"),
			    g_get_prgname (),
			    ssdiff_output);

		return TRUE;
	}

	// We need a copy of one of the files.  Rereading is easy.
	g_object_ref ((state->highlight.input = state->new.input));
	gsf_input_seek (state->highlight.input, 0, G_SEEK_SET);
	if (read_file (&state->highlight, dst, state->ioc))
		return TRUE;

	// We apply a solid #F3F315 to changed cells.
	state->highlight_style = gnm_style_new ();
	gnm_style_set_back_color (state->highlight_style,
				  gnm_color_new_rgb8 (0xf3, 0xf3, 0x15));
	gnm_style_set_pattern (state->highlight_style, 1);

	return FALSE;
}

static void
highlight_diff_end (gpointer user)
{
	DiffState *state = user;
	workbook_view_save_to_output (state->highlight.wbv,
				      state->highlight_fs,
				      state->output, state->ioc);
}

static void
highlight_dtor (gpointer user)
{
	DiffState *state = user;
	clear_file_state (&state->highlight);
	if (state->highlight_style) {
		gnm_style_unref (state->highlight_style);
		state->highlight_style = NULL;
	}
}

static void
highlight_sheet_start (gpointer user,
		       G_GNUC_UNUSED Sheet const *os, Sheet const *ns)
{
	DiffState *state = user;

	// We want the highlight sheet corresponding to new_sheet.
	state->highlight_sheet = ns
		? workbook_sheet_by_index (state->highlight.wb, ns->index_in_wb)
		: NULL;
}

static void
highlight_sheet_end (gpointer user)
{
	DiffState *state = user;
	state->highlight_sheet = NULL;
}

static void
highlight_apply (DiffState *state, const GnmRange *r)
{
	Sheet *sheet = state->highlight_sheet;

	g_return_if_fail (IS_SHEET (sheet));

	sheet_style_apply_range2 (sheet, r, state->highlight_style);
}

static void
highlight_cell_changed (gpointer user,
			GnmCell const *oc, GnmCell const *nc)
{
	DiffState *state = user;
	GnmRange r;
	highlight_apply (state, range_init_cellpos (&r, &(nc ? nc : oc)->pos));
}

static void
highlight_style_changed (gpointer user, GnmRange const *r,
			 G_GNUC_UNUSED GnmStyle const *os,
			 G_GNUC_UNUSED GnmStyle const *ns)
{
	DiffState *state = user;
	highlight_apply (state, r);
}


static const GnmDiffActions highlight_actions = {
	.diff_start = highlight_diff_start,
	.diff_end = highlight_diff_end,
	.dtor = highlight_dtor,
	.sheet_start = highlight_sheet_start,
	.sheet_end = highlight_sheet_end,
	.cell_changed = highlight_cell_changed,
	.style_changed = highlight_style_changed,
};

/* -------------------------------------------------------------------------- */

static int
diff (char const *oldfilename, char const *newfilename,
      GOIOContext *ioc,
      GnmDiffActions const *actions, GsfOutput *output)
{
	DiffState state;
	int res = 0;
	GnmLocale *locale;

	locale = gnm_push_C_locale ();

	memset (&state, 0, sizeof (state));
	state.ioc = ioc;
	state.output = output;

	if (read_file (&state.old, oldfilename, ioc))
		goto error;
	if (read_file (&state.new, newfilename, ioc))
		goto error;

	/* ---------------------------------------- */

	res = gnm_diff_workbooks (actions, &state, state.old.wb, state.new.wb);

out:
	clear_file_state (&state.old);
	clear_file_state (&state.new);
	if (actions->dtor)
		actions->dtor (&state);

	gnm_pop_C_locale (locale);

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

	// No code before here, we need to init threads
	argv = gnm_pre_parse_init (argc, argv);

	gnm_conf_set_persistence (FALSE);

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
		// FIXME: What do we want to do here?
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

	// Release cached string.
	def_cell_name (NULL);
	g_object_unref (output);

	go_component_set_default_command_context (NULL);
	g_object_unref (cc);
	gnm_shutdown ();
	gnm_pre_parse_shutdown ();

	return res;
}
