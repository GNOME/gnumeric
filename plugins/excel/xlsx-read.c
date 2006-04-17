/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * xlsx-read.c : Read MS Excel 2007 Office Open xml
 *
 * Copyright (C) 2006 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include "sheet-view.h"
#include "sheet-style.h"
#include "sheet-merge.h"
#include "sheet.h"
#include "ranges.h"
#include "style.h"
#include "style-border.h"
#include "style-color.h"
#include "gnm-format.h"
#include "cell.h"
#include "position.h"
#include "expr.h"
#include "expr-name.h"
#include "print-info.h"
#include "validation.h"
#include "value.h"
#include "selection.h"
#include "command-context.h"
#include "workbook-view.h"
#include "workbook.h"
#include <goffice/app/error-info.h>
#include <goffice/app/io-context.h>
#include <goffice/app/go-plugin.h>
#include <goffice/utils/datetime.h>
#include <goffice/utils/go-units.h>

#include <gsf/gsf-libxml.h>
#include <gsf/gsf-input.h>
#include <gsf/gsf-infile.h>
#include <gsf/gsf-infile-zip.h>

#include <glib/gi18n.h>
#include <gmodule.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <locale.h>

/*****************************************************************************/

typedef enum {
	XLXS_TYPE_NUM,
	XLXS_TYPE_SST_STR,	/* 0 based index into sst */
	XLXS_TYPE_BOOL,
	XLXS_TYPE_ERR,
	XLXS_TYPE_INLINE_STR,	/* inline string */
	/* How is this different from inlineStr ?? */
	XLXS_TYPE_STR2
} XLSXValueType;
typedef enum {
	XLSX_PANE_TOP_LEFT	= 0,
	XLSX_PANE_TOP_RIGHT	= 1,
	XLSX_PANE_BOTTOM_LEFT	= 2,
	XLSX_PANE_BOTTOM_RIGHT	= 3
} XLSXPanePos;

typedef struct {
	GsfInfile	*zip;
	GsfInput	*stream;	/* current stream.  FIXME should be in GsfXMLIn */
	IOContext 	*context;	/* The IOcontext managing things */
	WorkbookView	*wb_view;	/* View for the new workbook */
	Workbook	*wb;		/* The new workbook */

	GsfInput	 *sheet_stream;	/* current stream.  FIXME should be in GsfXMLIn */
	Sheet		 *sheet;	/* current sheet */
	GnmCellPos	  pos;		/* current cell */
	XLSXValueType	  pos_type;
	GnmValue	 *val;
	GnmExprTop const *texpr;
	GnmRange	  array;
	char		 *shared_id;
	GHashTable	 *shared_exprs;

	SheetView	*sv;		/* current sheetview */

	GArray		*sst;
	PangoAttrList	*rich_attrs;

	GHashTable	*num_fmts;
	GHashTable	*cell_styles;
	GPtrArray	*fonts;
	GPtrArray	*fills;
	GPtrArray	*borders;
	GPtrArray	*xfs;
	GPtrArray	*style_xfs;
	GPtrArray	*dxfs;
	GPtrArray	*table_styles;
	GnmStyle	*style_accum;
	StyleBorderType  border_style;
	GnmColor	*border_color;

	GPtrArray	*collection;	/* utility for the shared collection handlers */
	unsigned	 count;
	XLSXPanePos	 pane_pos;
} XLSXReadState;
typedef struct {
	GnmString	*str;
	GOFormat	*markup;
} XLSXStr;

enum {
	XL_NS_SS,
	XL_NS_DOC_REL,
	XL_NS_PKG_REL
};

static GsfXMLInNS const xlsx_ns[] = {
	GSF_XML_IN_NS (XL_NS_SS,	"http://schemas.microsoft.com/office/excel/2006/2"),
	GSF_XML_IN_NS (XL_NS_DOC_REL,	"http://schemas.openxmlformats.org/officeDocument/2006/relationships"),
	GSF_XML_IN_NS (XL_NS_PKG_REL,	"http://schemas.openxmlformats.org/package/2006/relationships"),
	{ NULL }
};

/****************************************************************************/
/* some utilities for Open Packaging format.
 * Move to gsf when the set of useful functionality clarifies */
typedef struct _GsfOpenPkgRel GsfOpenPkgRel;

struct _GsfOpenPkgRel {
	char *id, *type, *target;
};

static void
gsf_open_pkg_rel_free (GsfOpenPkgRel *rel)
{
	g_free (rel->id);	rel->id = NULL;
	g_free (rel->type);	rel->type = NULL;
	g_free (rel->target);	rel->target = NULL;
	g_free (rel);
}

static void
xlsx_pkg_rel_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	GsfOpenPkgRel *rel;
	xmlChar const *id = NULL;
	xmlChar const *type = NULL;
	xmlChar const *target = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], XL_NS_PKG_REL, "Id"))
			id = attrs[1];
		else if (gsf_xml_in_namecmp (xin, attrs[0], XL_NS_PKG_REL, "Type"))
			type = attrs[1];
		else if (gsf_xml_in_namecmp (xin, attrs[0], XL_NS_PKG_REL, "Target"))
			target = attrs[1];

	g_return_if_fail (id != NULL);
	g_return_if_fail (type != NULL);
	g_return_if_fail (target != NULL);

	rel = g_new (GsfOpenPkgRel, 1);
	rel->id		= g_strdup (id);
	rel->type	= g_strdup (type);
	rel->target	= g_strdup (target);

	g_hash_table_replace (xin->user_state, rel->id, rel);
}

static GsfXMLInNode const open_pkg_rel_dtd[] = {
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE_FULL (START, RELS, XL_NS_PKG_REL, "Relationships", GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
  GSF_XML_IN_NODE (RELS, REL, XL_NS_PKG_REL, "Relationship", GSF_XML_NO_CONTENT, xlsx_pkg_rel_start, NULL),

GSF_XML_IN_NODE_END
};

/**
 * gsf_open_pkg_get_rels :
 * @in : #GsfInput
 * 
 * Returns a hashtable of the relationships associated with @in
 **/
static GHashTable *
gsf_open_pkg_get_rels (GsfInput *in)
{
	GHashTable *rels;

	g_return_val_if_fail (in != NULL, NULL);

	if (NULL == (rels = g_object_get_data (G_OBJECT (in), "OpenPkgRels"))) {
		char *rel_name;
		GsfXMLInDoc *rel_doc;
		GsfInput *rel_stream;
		GsfInfile *container = gsf_input_container (in);

		g_return_val_if_fail (container != NULL, NULL);

		rel_name = g_strconcat (gsf_input_name (in), ".rels", NULL);
		rel_stream = gsf_infile_child_by_vname (container, "_rels", rel_name, NULL);
		g_free (rel_name);

		g_return_val_if_fail (rel_stream != NULL, NULL);

		rels = g_hash_table_new_full (g_str_hash, g_str_equal,
			NULL, (GDestroyNotify)gsf_open_pkg_rel_free);
		rel_doc = gsf_xml_in_doc_new (open_pkg_rel_dtd, xlsx_ns);
		(void) gsf_xml_in_doc_parse (rel_doc, rel_stream, rels);

		gsf_xml_in_doc_free (rel_doc);
		g_object_unref (G_OBJECT (rel_stream));

		g_object_set_data_full (G_OBJECT (in), "OpenPkgRels", rels,
			(GDestroyNotify) g_hash_table_destroy);
	}

	return rels;
}

static GsfInput *
gsf_open_pkg_get_rel (GsfInput *in, char const *id)
{
	GsfOpenPkgRel *rel = NULL;
	GHashTable *rels = gsf_open_pkg_get_rels (in);

	g_return_val_if_fail (rels != NULL, NULL);

	rel = g_hash_table_lookup (rels, id);
	if (rel != NULL) {
		gchar **elems;
		unsigned i;
		GsfInfile *container = gsf_input_container (in);

		g_return_val_if_fail (container != NULL, NULL);

		/* parts can not have '/' in their names ? TODO : PROVE THIS
		 * right now the only test is that worksheets can not have it
		 * in their names */
		elems = g_strsplit (rel->target, "/", 0);
		for (i = 0 ; elems[i] ;) {
			in = gsf_infile_child_by_name (container, elems[i]);
			if (i > 0)
				g_object_unref (G_OBJECT (container));
			if (NULL != elems[++i]) {
				g_return_val_if_fail (GSF_IS_INFILE (in), NULL);
				container = GSF_INFILE (in);
			}
		}
		g_strfreev (elems);

		return in;
	}

	return NULL;
}

/****************************************************************************/

static gboolean xlsx_warning (GsfXMLIn *xin, char const *fmt, ...)
	G_GNUC_PRINTF (2, 3);

static gboolean
xlsx_warning (GsfXMLIn *xin, char const *fmt, ...)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	char *msg;
	va_list args;

	va_start (args, fmt);
	msg = g_strdup_vprintf (fmt, args);
	va_end (args);

	if (IS_SHEET (state->sheet)) {
		char *tmp;
		if (state->pos.col >= 0 && state->pos.row >= 0)
			tmp = g_strdup_printf ("%s!%s : %s",
				state->sheet->name_quoted,
				cellpos_as_string (&state->pos), msg);
		else
			tmp = g_strdup_printf ("%s : %s",
				state->sheet->name_quoted, msg);
		g_free (msg);
		msg = tmp;
	}

	gnm_io_warning (state->context, "%s", msg);
	g_warning ("%s", msg);
	g_free (msg);

	return FALSE; /* convenience */
}

typedef struct {
	char const * const name;
	int val;
} EnumVal;

static gboolean
attr_enum (GsfXMLIn *xin, xmlChar const **attrs,
	   unsigned int ns_id, char const *target, EnumVal const *enums,
	   int *res)
{
	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (!gsf_xml_in_namecmp (xin, attrs[0], ns_id, target))
		return FALSE;

	for (; enums->name != NULL ; enums++)
		if (!strcmp (enums->name, attrs[1])) {
			*res = enums->val;
			return TRUE;
		}
	return xlsx_warning (xin,
		_("Unknown enum value '%s' for attribute %s"),
		attrs[1], target);
}

/**
 * Take an _int_ as a result to allow the caller to use -1 as an undefined state.
 **/
static gboolean
attr_bool (GsfXMLIn *xin, xmlChar const **attrs,
	   unsigned int ns_id, char const *target,
	   int *res)
{
	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (!gsf_xml_in_namecmp (xin, attrs[0], ns_id, target))
		return FALSE;

	*res = 0 == strcmp (attrs[1], "1");

	return TRUE;
}

static gboolean
attr_int (GsfXMLIn *xin, xmlChar const **attrs,
	  unsigned int ns_id, char const *target,
	  int *res)
{
	char *end;
	int tmp;

	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (!gsf_xml_in_namecmp (xin, attrs[0], ns_id, target))
		return FALSE;

	errno = 0;
	tmp = strtol (attrs[1], &end, 10);
	if (errno == ERANGE)
		return xlsx_warning (xin,
			_("Integer '%s' is out of range, for attribute %s"),
			attrs[1], target);
	if (*end)
		return xlsx_warning (xin,
			_("Invalid integer '%s' for attribute %s"),
			attrs[1], target);

	*res = tmp;
	return TRUE;
}

static gboolean
attr_float (GsfXMLIn *xin, xmlChar const **attrs,
	    unsigned int ns_id, char const *target,
	    gnm_float *res)
{
	char *end;
	double tmp;

	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (!gsf_xml_in_namecmp (xin, attrs[0], ns_id, target))
		return FALSE;

	tmp = gnm_strto (attrs[1], &end);
	if (*end)
		return xlsx_warning (xin,
			_("Invalid number '%s' for attribute %s"),
			attrs[1], target);
	*res = tmp;
	return TRUE;
}

static gboolean
attr_pos (GsfXMLIn *xin, xmlChar const **attrs,
	  unsigned int ns_id, char const *target,
	  GnmCellPos *res)
{
	char const *end;
	GnmCellPos tmp;

	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (!gsf_xml_in_namecmp (xin, attrs[0], ns_id, target))
		return FALSE;

	end = cellpos_parse (attrs[1], &tmp, TRUE);
	if (NULL == end || *end != '\0')
		return xlsx_warning (xin,
			_("Invalid cell position '%s' for attribute %s"),
			attrs[1], target);
	*res = tmp;
	return TRUE;
}

static gboolean
attr_range (GsfXMLIn *xin, xmlChar const **attrs,
	    unsigned int ns_id, char const *target,
	    GnmRange *res)
{
	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (!gsf_xml_in_namecmp (xin, attrs[0], ns_id, target))
		return FALSE;

	if (!parse_range (attrs[1], res))
		xlsx_warning (xin,
			_("Invalid range '%s' for attribute %s"),
			attrs[1], target);
	return TRUE;
}

static struct {
	guint8 r, g, b;
} excel_default_palette_v8 [] = {
	{  0,  0,  0}, {255,255,255}, {255,  0,  0}, {  0,255,  0},
	{  0,  0,255}, {255,255,  0}, {255,  0,255}, {  0,255,255},

	{128,  0,  0}, {  0,128,  0}, {  0,  0,128}, {128,128,  0},
	{128,  0,128}, {  0,128,128}, {192,192,192}, {128,128,128},

	{153,153,255}, {153, 51,102}, {255,255,204}, {204,255,255},
	{102,  0,102}, {255,128,128}, {  0,102,204}, {204,204,255},

	{  0,  0,128}, {255,  0,255}, {255,255,  0}, {  0,255,255},
	{128,  0,128}, {128,  0,  0}, {  0,128,128}, {  0,  0,255},

	{  0,204,255}, {204,255,255}, {204,255,204}, {255,255,153},
	{153,204,255}, {255,153,204}, {204,153,255}, {255,204,153},

	{ 51,102,255}, { 51,204,204}, {153,204,  0}, {255,204,  0},
	{255,153,  0}, {255,102,  0}, {102,102,153}, {150,150,150},

	{  0, 51,102}, { 51,153,102}, {  0, 51,  0}, { 51, 51,  0},
	{153, 51,  0}, {153, 51,102}, { 51, 51,153}, { 51, 51, 51}
};

static GnmColor *
indexed_color (gint idx)
{
	/* NOTE: not documented but seems close
	 * If you find a normative reference please forward it.
	 *
	 * The color index field seems to use
	 *	8-63 = Palette index 0-55
	 *	64  = auto pattern, auto border
	 *      65  = auto background
	 *      127 = auto font
	 *
	 *      65 is always white, and 127 always black. 64 is black
	 *      if the fDefaultHdr flag in WINDOW2 is unset, otherwise it's
	 *      the grid color from WINDOW2.
	 */

	if (idx == 1 || idx == 65)
		return style_color_white ();
	switch (idx) {
	case 0:   /* black */
	case 64 : /* system text ? */
	case 81 : /* tooltip text */
	case 0x7fff : /* system text ? */
		return style_color_black ();
	case 1 :  /* white */
	case 65 : /* system back ? */
		return style_color_white ();

	case 80 : /* tooltip background */
		return style_color_new_gdk (&gs_yellow);

	case 2 : return style_color_new_i8 (0xff,    0,    0); /* red */
	case 3 : return style_color_new_i8 (   0, 0xff,    0); /* green */
	case 4 : return style_color_new_i8 (   0,    0, 0xff); /* blue */
	case 5 : return style_color_new_i8 (0xff, 0xff,    0); /* yellow */
	case 6 : return style_color_new_i8 (0xff,    0, 0xff); /* magenta */
	case 7 : return style_color_new_i8 (   0, 0xff, 0xff); /* cyan */
	default :
		 break;
	}

	idx -= 8;
	if (idx < 0 || (int) G_N_ELEMENTS (excel_default_palette_v8) <= idx) {
		g_warning ("EXCEL: color index (%d) is out of range (8..%d). Defaulting to black",
			   idx + 8, G_N_ELEMENTS (excel_default_palette_v8) + 8);
		return style_color_black ();
	}

	return style_color_new_i8 (excel_default_palette_v8[idx].r,
				   excel_default_palette_v8[idx].g,
				   excel_default_palette_v8[idx].b);
}

static GnmColor *
elem_color (GsfXMLIn *xin, xmlChar const **attrs)
{
	int indx;
	guint a, r, g, b;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (gsf_xml_in_namecmp (xin, attrs[0], XL_NS_SS, "rgb")) {
			if (4 != sscanf (attrs[1], "%02x%02x%02x%02x", &a, &r, &g, &b)) {
				xlsx_warning (xin,
					_("Invalid color '%s' for attribute rgb"),
					attrs[1]);
				return NULL;
			}

			return style_color_new_i8 (r, g, b);
		} else if (attr_int (xin, attrs, XL_NS_SS, "indexed", &indx))
			return indexed_color (indx);
#if 0
	"type"	opt rgb {auto, icv, rgb, theme }
	"val"	opt ??
	"tint"	opt 0.
#endif
	}
	return NULL;
}

static GnmStyle *
xlsx_get_xf (GsfXMLIn *xin, int xf)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	if (0 <= xf && NULL != state->xfs && xf < (int)state->xfs->len)
		return g_ptr_array_index (state->xfs, xf);
	xlsx_warning (xin, _("Undefined style record '%d'"), xf);
	return NULL;
}
static GOFormat *
xlsx_get_num_fmt (GsfXMLIn *xin, char const *id)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	GOFormat *res = g_hash_table_lookup (state->num_fmts, id);
	if (NULL != res)
		return res;

	/* TODO : other builtins ?? */
	if (0 == strcmp ("0", id))
		return go_format_general ();

	xlsx_warning (xin, _("Undefined number format id '%s'"), id);
	return NULL;
}

static GnmExprTop const *
xlsx_parse_expr (GsfXMLIn *xin, xmlChar const *expr_str,
		 GnmParsePos const *pp)
{
	GnmParseError err;
	GnmExprTop const *texpr;
	
	/* Odd, some time IF and CHOOSE show up with leading spaces ??
	 * = IF(....
	 * = CHOOSE(...
	 * I wonder if it is related to some of the funky old
	 * optimizations in * xls ? */
	while (' ' == *expr_str)
		expr_str++;

	texpr = gnm_expr_parse_str (expr_str, pp,
		GNM_EXPR_PARSE_DEFAULT, gnm_expr_conventions_default,
		parse_error_init (&err));
	if (NULL == texpr)
		xlsx_warning (xin, "'%s' %s", expr_str, err.err->message);
	parse_error_free (&err);

	return texpr;
}

/****************************************************************************/

static void
xlsx_cell_val_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	XLSXStr const	*entry;
	char		*end;
	long		 i;

	switch (state->pos_type) {
	case XLXS_TYPE_NUM :
		if (*xin->content->str)
			state->val = value_new_float (strtod (xin->content->str, &end));
		break;
	case XLXS_TYPE_SST_STR :
		i = strtol (xin->content->str, &end, 10);
		if (end != xin->content->str && *end == '\0' &&
		    0 <= i  && i < (int)state->sst->len) {
			entry = &g_array_index (state->sst, XLSXStr, i);
			gnm_string_ref (entry->str);
			state->val = value_new_string_str (entry->str);
			if (NULL != entry->markup)
				value_set_fmt (state->val, entry->markup);
		} else {
			xlsx_warning (xin, _("Invalid sst ref '%s'"), xin->content->str);
		}
		break;
	case XLXS_TYPE_BOOL :
		if (*xin->content->str)
			state->val = value_new_bool (*xin->content->str != '0');
		break;
	case XLXS_TYPE_ERR :
		if (*xin->content->str)
			state->val = value_new_error (NULL, xin->content->str);
		break;

	case XLXS_TYPE_STR2 : /* What is this ? */
	case XLXS_TYPE_INLINE_STR :
		state->val = value_new_string (xin->content->str);
		break;
	}
}

static void
xlsx_cell_expr_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	gboolean has_range = FALSE, is_array = FALSE;
	GnmRange range;
	xmlChar const *shared_id = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], XL_NS_SS, "t")) {
			if (0 == strcmp (attrs[1], "array"))
				is_array = TRUE;
		} else if (gsf_xml_in_namecmp (xin, attrs[0], XL_NS_SS, "si"))
			shared_id = attrs[1];
		else if (attr_range (xin, attrs, XL_NS_SS, "ref", &range))
			has_range = TRUE;

	state->shared_id = NULL;
	if (NULL != shared_id) {
		state->texpr = g_hash_table_lookup (state->shared_exprs, shared_id);
		if (NULL != state->texpr)
			gnm_expr_top_ref (state->texpr);
		else
			state->shared_id = g_strdup (shared_id);
	} else
		state->texpr = NULL;

	/* if the shared expr is already parsed expression do not even collect content */
	((GsfXMLInNode *)(xin->node))->has_content =
		(NULL != state->texpr) ? GSF_XML_NO_CONTENT : GSF_XML_CONTENT;

	if (is_array && has_range)
		state->array = range;
}

static void
xlsx_cell_expr_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GnmParsePos pp;

	if (NULL == state->texpr) {
		parse_pos_init (&pp, NULL, state->sheet,
			state->pos.col, state->pos.row);
		state->texpr = xlsx_parse_expr (xin, xin->content->str, &pp);
		if (NULL != state->texpr &&
		    NULL != state->shared_id) {
			gnm_expr_top_ref (state->texpr);
			g_hash_table_replace (state->shared_exprs,
				state->shared_id, (gpointer)state->texpr);
			state->shared_id = NULL;
		}
	}
	g_free (state->shared_id);
	state->shared_id = NULL;
}

static void
xlsx_cell_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	static EnumVal const types[] = {
		{ "n",		XLXS_TYPE_NUM },
		{ "s", 		XLXS_TYPE_SST_STR },
		{ "str", 	XLXS_TYPE_STR2 },
		{ "b",		XLXS_TYPE_BOOL },
		{ "inlineStr", 	XLXS_TYPE_INLINE_STR },
		{ "e", 		XLXS_TYPE_ERR },
		{ NULL, 0 }
	};
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int tmp;
	GnmStyle *style = NULL;

	state->pos.col = state->pos.row = -1;
	state->pos_type = XLXS_TYPE_NUM; /* the default */
	state->val = NULL;
	state->texpr = NULL;
	range_init (&state->array, -1, -1, -1, -1); /* invalid */

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_pos (xin, attrs, XL_NS_SS, "r", &state->pos))
			;
		else if (attr_enum (xin, attrs, XL_NS_SS, "t", types, &tmp))
			state->pos_type = tmp;
		else if (attr_int (xin, attrs, XL_NS_SS, "s", &tmp))
			style = xlsx_get_xf (xin, tmp);

	if (NULL != style) {
		gnm_style_ref (style);
		sheet_style_set_pos (state->sheet,
			state->pos.col, state->pos.row, style);
	}
}
static void
xlsx_cell_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GnmCell *cell = sheet_cell_fetch (state->sheet,
		state->pos.col, state->pos.row);

	if (NULL == cell) {
		xlsx_warning (xin, _("Invalid cell %s"),
			cellpos_as_string (&state->pos));
		if (NULL != state->val)
			value_release (state->val);
		if (NULL != state->texpr)
			gnm_expr_top_unref (state->texpr);
	} else if (NULL != state->texpr) {
		if (state->array.start.col >= 0) {
			cell_set_array_formula (state->sheet,
				state->array.start.col,
				state->array.start.row,
				state->array.end.col,
				state->array.end.row,
				state->texpr);
			if (NULL != state->val)
				cell_assign_value (cell, state->val);
		} else if (NULL != state->val) {
			cell_set_expr_and_value	(cell,
				state->texpr, state->val, TRUE);
			gnm_expr_top_unref (state->texpr);
		} else {
			cell_set_expr (cell, state->texpr);
			gnm_expr_top_unref (state->texpr);
		}
		state->texpr = NULL;
	} else if (NULL != state->val)
		cell_assign_value (cell, state->val);
	state->val = NULL;
}

static void
xlsx_CT_Row (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int row = -1, xf_index;
	double h = -1.;
	int cust_fmt = FALSE, cust_height = FALSE, collapsed = FALSE;
	int hidden = -1;
	int outline = -1;
	GnmStyle *style = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_int (xin, attrs, XL_NS_SS, "r", &row)) ;
		else if (attr_float (xin, attrs, XL_NS_SS, "ht", &h)) ;
		else if (attr_bool (xin, attrs, XL_NS_SS, "customFormat", &cust_fmt)) ;
		else if (attr_bool (xin, attrs, XL_NS_SS, "customHeight", &cust_height)) ;
		else if (attr_int (xin, attrs, XL_NS_SS, "s", &xf_index))
			style = xlsx_get_xf (xin, xf_index);
		else if (attr_int (xin, attrs, XL_NS_SS, "outlineLevel", &outline)) ;
		else if (attr_bool (xin, attrs, XL_NS_SS, "hidden", &hidden)) ;
		else if (attr_bool (xin, attrs, XL_NS_SS, "collapsed", &collapsed)) ;

	if (row > 0) {
		row--;
		if (h >= 0.)
			sheet_row_set_size_pts (state->sheet, row, h, cust_height);
		if (hidden > 0)
			colrow_set_visibility (state->sheet, FALSE, FALSE, row, row);
		if (outline >= 0)
			colrow_set_outline (sheet_row_fetch (state->sheet, row),
				outline, collapsed);

		if (NULL != style) {
			GnmRange r;
			r.start.row = r.end.row = row;
			r.start.col = 0;
			r.end.col  = SHEET_MAX_COLS - 1;
			gnm_style_ref (style);
			sheet_style_set_range (state->sheet, &r, style);
		}
	}
}

static void
xlsx_CT_Col (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int first = -1, last = -1, xf_index;
	double width = -1.;
	gboolean cust_width = FALSE, collapsed = FALSE;
	int i, hidden = -1;
	int outline = -1;
	GnmStyle *style = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_int (xin, attrs, XL_NS_SS, "min", &first)) ;
		else if (attr_int (xin, attrs, XL_NS_SS, "max", &last)) ;
		else if (attr_float (xin, attrs, XL_NS_SS, "width", &width))
			/* FIXME FIXME FIXME arbitrary map from 130 pixels to
			 * the value stored for a column with 130 pixel width*/
			width *= (130. / 18.5703125) * (72./96.);
		else if (attr_bool (xin, attrs, XL_NS_SS, "customWidth", &cust_width)) ;
		else if (attr_int (xin, attrs, XL_NS_SS, "style", &xf_index))
			style = xlsx_get_xf (xin, xf_index);
		else if (attr_int (xin, attrs, XL_NS_SS, "outlineLevel", &outline)) ;
		else if (attr_bool (xin, attrs, XL_NS_SS, "hidden", &hidden)) ;
		else if (attr_bool (xin, attrs, XL_NS_SS, "collapsed", &collapsed)) ;

	if (first < 0) {
		if (last < 0) {
			xlsx_warning (xin, _("Ignoring column information that does not specify first or last."));
			return;
		}
		first = --last;
	} else if (last < 0)
		last = --first;
	else {
		first--;
		last--;
	}


	if (last >= SHEET_MAX_COLS)
		last = SHEET_MAX_COLS - 1;
	for (i = first; i <= last; i++) {
		if (width > 4)
			sheet_col_set_size_pts (state->sheet, i, width, cust_width);
		if (outline > 0)
			colrow_set_outline (sheet_col_fetch (state->sheet, i),
				outline, collapsed);
	}
	if (NULL != style) {
		GnmRange r;
		r.start.col = first;
		r.end.col   = last;
		r.start.row = 0;
		r.end.row  = SHEET_MAX_ROWS - 1;
		gnm_style_ref (style);
		sheet_style_set_range (state->sheet, &r, style);
	}
	if (hidden > 0)
		colrow_set_visibility (state->sheet, TRUE, FALSE, first, last);
}

static void
xlsx_sheet_tabcolor (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GnmColor *text_color, *color = elem_color (xin, attrs);
	if (NULL != color) {
		int contrast = color->gdk_color.red +
			color->gdk_color.green +
			color->gdk_color.blue;
		if (contrast >= 0x18000)
			text_color = style_color_black ();
		else
			text_color = style_color_white ();
		g_object_set (state->sheet,
			      "tab-foreground", text_color,
			      "tab-background", color,
			      NULL);
		style_color_unref (text_color);
		style_color_unref (color);
	}
}

static void
xlsx_CT_SheetFormatPr (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	double h;
	int i;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_float (xin, attrs, XL_NS_SS, "defaultRowHeight", &h))
			sheet_row_set_default_size_pts (state->sheet, h);
		else if (attr_int (xin, attrs, XL_NS_SS, "outlineLevelRow", &i)) {
			if (i > 0)
				sheet_colrow_gutter (state->sheet, FALSE, i);
		} else if (attr_int (xin, attrs, XL_NS_SS, "outlineLevelCol", &i)) {
			if (i > 0)
				sheet_colrow_gutter (state->sheet, TRUE, i);
		}
}

static void
xlsx_CT_PageMargins (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	PrintInformation *pi = state->sheet->print_info;
	double margin;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_float (xin, attrs, XL_NS_SS, "left", &margin))
			print_info_set_margin_left (pi, GO_IN_TO_PT (margin));
		else if (attr_float (xin, attrs, XL_NS_SS, "right", &margin))
			print_info_set_margin_right (pi, GO_IN_TO_PT (margin));
		else if (attr_float (xin, attrs, XL_NS_SS, "top", &margin))
			/* pi->margin.top = margin; */
			;
		else if (attr_float (xin, attrs, XL_NS_SS, "bottom", &margin))
			/* pi->margin.bottom = margin; */
			;
		else if (attr_float (xin, attrs, XL_NS_SS, "header", &margin))
			print_info_set_margin_header (pi, margin);
		else if (attr_float (xin, attrs, XL_NS_SS, "footer", &margin))
			print_info_set_margin_footer (pi, margin);
}

static void
xlsx_CT_MergeCell (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GnmRange r;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_range (xin, attrs, XL_NS_SS, "ref", &r))
			sheet_merge_add (state->sheet, &r, FALSE,
				GO_CMD_CONTEXT (state->context));
}

static void
xlsx_CT_SheetView_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int showGridLines	= TRUE;
	int showFormulas	= FALSE;
	int showRowColHeaders	= TRUE;
	int showZeros		= TRUE;
	int frozen		= FALSE;
	int frozenSplit		= TRUE;
	int rightToLeft	 	= FALSE;
	int tabSelected	 	= FALSE;
	int active		= FALSE;
	int showRuler		= TRUE;
	int showOutlineSymbols	= TRUE;
	int defaultGridColor	= TRUE;
	int showWhiteSpace	= TRUE;
	int scale = 100;
	GnmCellPos topLeft = { -1, -1 };

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_pos (xin, attrs, XL_NS_SS, "topLeftCell", &topLeft)) ;
		else if (attr_bool (xin, attrs, XL_NS_SS, "showGridLines", &showGridLines)) ;
		else if (attr_bool (xin, attrs, XL_NS_SS, "showFormulas", &showFormulas)) ;
		else if (attr_bool (xin, attrs, XL_NS_SS, "showRowColHeaders", &showRowColHeaders)) ;
		else if (attr_bool (xin, attrs, XL_NS_SS, "showZeros", &showZeros)) ;
		else if (attr_bool (xin, attrs, XL_NS_SS, "frozen", &frozen)) ;
		else if (attr_bool (xin, attrs, XL_NS_SS, "frozenSplit", &frozenSplit)) ;
		else if (attr_bool (xin, attrs, XL_NS_SS, "rightToLeft", &rightToLeft)) ;
		else if (attr_bool (xin, attrs, XL_NS_SS, "tabSelected", &tabSelected)) ;
		else if (attr_bool (xin, attrs, XL_NS_SS, "active", &active)) ;
		else if (attr_bool (xin, attrs, XL_NS_SS, "showRuler", &showRuler)) ;
		else if (attr_bool (xin, attrs, XL_NS_SS, "showOutlineSymbols", &showOutlineSymbols)) ;
		else if (attr_bool (xin, attrs, XL_NS_SS, "defaultGridColor", &defaultGridColor)) ;
		else if (attr_bool (xin, attrs, XL_NS_SS, "showWhiteSpace", &showWhiteSpace)) ;
		else if (attr_int (xin, attrs, XL_NS_SS, "zoomScale", &scale)) ;
#if 0
"zoomScaleNormal"		type="xs:unsignedInt" use="optional" default="0"
"zoomScaleSheetLayoutView"	type="xs:unsignedInt" use="optional" default="0"
"zoomScalePageLayoutView"	type="xs:unsignedInt" use="optional" default="0"
"workbookViewId"		type="xs:unsignedInt" use="required"
"view"				type="ST_SheetViewType" use="optional" default="normal"
"colorId"			type="xs:int" use="optional" default="64"
#endif

	/* get this from the workbookViewId */
	g_return_if_fail (state->sv == NULL);
	state->sv = sheet_get_view (state->sheet, state->wb_view);
	state->pane_pos = XLSX_PANE_TOP_LEFT;

	/* until we import multiple views unfreeze just in case a previous view
	 * had frozen */
	sv_freeze_panes (state->sv, NULL, NULL);

	if (topLeft.col >= 0)
		sv_set_initial_top_left (state->sv, topLeft.col, topLeft.row);
	g_object_set (state->sheet,
		"text-is-rtl",		rightToLeft,
		"display-formulas",	showFormulas,
		"display-zeros",	showZeros,
		"display-grid",		showGridLines,
		"display-column-header", showRowColHeaders,
		"display-row-header",	showRowColHeaders,
		"display-outlines",	showOutlineSymbols,
		"zoom-factor",		((double)scale) / 100.,
#if 0
		gboolean active			= FALSE;
		gboolean showRuler		= TRUE;
		gboolean defaultGridColor	= TRUE;
		gboolean showWhiteSpace		= TRUE;
#endif
		NULL);

	if (tabSelected)
		wb_view_sheet_focus (state->wb_view, state->sheet);
}
static void
xlsx_CT_SheetView_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	g_return_if_fail (state->sv != NULL);
	state->sv = NULL;
}

static EnumVal const pane_types[] = {
	{ "topLeft",     XLSX_PANE_TOP_LEFT },
	{ "topRight",    XLSX_PANE_TOP_RIGHT },
	{ "bottomLeft",  XLSX_PANE_BOTTOM_LEFT },
	{ "bottomRight", XLSX_PANE_BOTTOM_RIGHT },
	{ NULL, 0 }
};
static void
xlsx_CT_Selection (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GnmCellPos edit_pos = { -1, -1 };
	int i, sel_with_edit_pos = 0;
	char const *refs = NULL;
	XLSXPanePos pane_pos = XLSX_PANE_TOP_LEFT;
	GnmRange r;

	g_return_if_fail (state->sv != NULL);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], XL_NS_SS, "sqref"))
			refs = attrs[1];
		else if (attr_enum (xin, attrs, XL_NS_SS, "activePane", pane_types, &i))
			pane_pos = i;
		else if (attr_pos (xin, attrs, XL_NS_SS, "activeCell", &edit_pos)) ;
		else if (attr_int (xin, attrs, XL_NS_SS, "activeCellId", &sel_with_edit_pos))
			;

	if (pane_pos != state->pane_pos)
		return;

	for (i = 0 ; NULL != refs && *refs ; i++) {
		if (NULL == (refs = cellpos_parse (refs, &r.start, FALSE)))
			return;

		if (*refs == '\0' || *refs == ' ')
			r.end = r.start;
		else if (*refs != ':' ||
			 NULL == (refs = cellpos_parse (refs + 1, &r.end, FALSE)))
			return;

		if (i == 0)
			sv_selection_reset (state->sv);

		/* FIXME : gnumeric assumes the edit_pos is in the last
		 * selected range.  We need to re-order the selection list. */
		if (i == sel_with_edit_pos && edit_pos.col >= 0)
			sv_selection_add_range (state->sv,
				edit_pos.col, edit_pos.row,
				r.start.col, r.start.row,
				r.end.col, r.end.row);
		else
			sv_selection_add_range (state->sv,
				r.start.col, r.start.row,
				r.start.col, r.start.row,
				r.end.col, r.end.row);
		while (*refs == ' ')
			refs++;
	}
}
static void
xlsx_CT_Pane (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GnmCellPos topLeft;
	int tmp;
	double xSplit = -1., ySplit = -1.;
	gboolean frozen = FALSE;

	g_return_if_fail (state->sv != NULL);

	/* <pane xSplit="2" ySplit="3" topLeftCell="J15" activePane="bottomRight" state="frozen"/> */
	state->pane_pos = XLSX_PANE_TOP_LEFT;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], XL_NS_SS, "state"))
			frozen = (0 == strcmp (attrs[1], "frozen"));
		else if (attr_pos (xin, attrs, XL_NS_SS, "topLeftCell", &topLeft)) ;
		else if (attr_float (xin, attrs, XL_NS_SS, "xSplit", &xSplit)) ;
		else if (attr_float (xin, attrs, XL_NS_SS, "ySplit", &ySplit)) ;
		else if (attr_enum (xin, attrs, XL_NS_SS, "pane", pane_types, &tmp))
			state->pane_pos = tmp;

	if (frozen) {
		GnmCellPos frozen, unfrozen;
		frozen = unfrozen = state->sv->initial_top_left;
		if (xSplit > 0)
			unfrozen.col += xSplit;
		else
			topLeft.col = state->sv->initial_top_left.col;
		if (ySplit > 0)
			unfrozen.row += ySplit;
		else
			topLeft.row = state->sv->initial_top_left.row;
		sv_freeze_panes (state->sv, &frozen, &unfrozen);
		sv_set_initial_top_left (state->sv, topLeft.col, topLeft.row);
	}
}

static GsfXMLInNode const xlsx_sheet_dtd[] = {
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE_FULL (START, SHEET, XL_NS_SS, "worksheet", GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
  GSF_XML_IN_NODE (SHEET, PROPS, XL_NS_SS, "sheetPr", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (PROPS, OUTLINE_PROPS, XL_NS_SS, "outlinePr", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (PROPS, TAB_COLOR, XL_NS_SS, "tabColor", GSF_XML_NO_CONTENT, &xlsx_sheet_tabcolor, NULL),
  GSF_XML_IN_NODE (SHEET, DIMENSION, XL_NS_SS, "dimension", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (SHEET, VIEWS, XL_NS_SS, "sheetViews", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (VIEWS, VIEW, XL_NS_SS, "sheetView",  GSF_XML_NO_CONTENT, &xlsx_CT_SheetView_start, &xlsx_CT_SheetView_end),
      GSF_XML_IN_NODE (VIEW, SELECTION, XL_NS_SS, "selection",  GSF_XML_NO_CONTENT, &xlsx_CT_Selection, NULL),
      GSF_XML_IN_NODE (VIEW, PANE, XL_NS_SS, "pane",  GSF_XML_NO_CONTENT, &xlsx_CT_Pane, NULL),

  GSF_XML_IN_NODE (SHEET, DEFAULT_FMT, XL_NS_SS, "sheetFormatPr", GSF_XML_NO_CONTENT, &xlsx_CT_SheetFormatPr, NULL),

  GSF_XML_IN_NODE (SHEET, COLS,	XL_NS_SS, "cols", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (COLS, COL,	XL_NS_SS, "col", GSF_XML_NO_CONTENT, &xlsx_CT_Col, NULL),

  GSF_XML_IN_NODE (SHEET, CONTENT, XL_NS_SS, "sheetData", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (CONTENT, ROW, XL_NS_SS, "row", GSF_XML_NO_CONTENT, &xlsx_CT_Row, NULL),
      GSF_XML_IN_NODE (ROW, CELL, XL_NS_SS, "c", GSF_XML_NO_CONTENT, &xlsx_cell_start, &xlsx_cell_end),
	GSF_XML_IN_NODE (CELL, VALUE, XL_NS_SS, "v", GSF_XML_CONTENT, NULL, &xlsx_cell_val_end), 
	GSF_XML_IN_NODE (CELL, FMLA, XL_NS_SS,  "f", GSF_XML_CONTENT, &xlsx_cell_expr_start, &xlsx_cell_expr_end),

  GSF_XML_IN_NODE (SHEET, MERGES, XL_NS_SS, "mergeCells", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (MERGES, MERGE, XL_NS_SS, "mergeCell", GSF_XML_NO_CONTENT, &xlsx_CT_MergeCell, NULL),

  GSF_XML_IN_NODE (SHEET, PROTECTION, XL_NS_SS, "sheetProtection", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (SHEET, PHONETIC, XL_NS_SS, "phoneticPr", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (SHEET, COND_FMTS, XL_NS_SS, "conditionalFormatting", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (COND_FMTS, COND_RULE, XL_NS_SS, "cfRule", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (COND_RULE, COND_FMLA, XL_NS_SS, "formula", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (SHEET, HYPERLINKS, XL_NS_SS, "hyperlinks", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (HYPERLINKS, HYPERLINK, XL_NS_SS, "hyperlink", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (SHEET, PRINT_OPTS, XL_NS_SS, "printOptions", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (SHEET, PRINT_MARGINS, XL_NS_SS, "pageMargins", GSF_XML_NO_CONTENT, &xlsx_CT_PageMargins, NULL),
  GSF_XML_IN_NODE (SHEET, PRINT_SETUP, XL_NS_SS, "pageSetup", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (SHEET, PRINT_HEADER_FOOTER, XL_NS_SS, "headerFooter", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (SHEET, LEGACY_DRAW, XL_NS_SS, "legacyDrawing", GSF_XML_NO_CONTENT, NULL, NULL),

GSF_XML_IN_NODE_END
};

/****************************************************************************/

static void
xlsx_sheet_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	char const *name = NULL;
	char const *part_id = NULL;
	Sheet *sheet;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], XL_NS_SS, "name"))
			name = attrs[1];
		else if (gsf_xml_in_namecmp (xin, attrs[0], XL_NS_DOC_REL, "id"))
			part_id = attrs[1];

	if (name == NULL) {
		xlsx_warning (xin, _("Ignoring a sheet without a name"));
		return;
	}

	sheet =  workbook_sheet_by_name (state->wb, name);
	if (sheet == NULL) {
		sheet = sheet_new (state->wb, name);
		workbook_sheet_attach (state->wb, sheet);
	}

	g_object_set_data_full (G_OBJECT (sheet), "_XLSX_RelID", g_strdup (part_id),
			(GDestroyNotify) g_free);
}

static void
xlsx_wb_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int i, n = workbook_sheet_count (state->wb);
	char const *part_id;
	GnmStyle *style;

	/* Load sheets after setting up the workbooks to give us time to create
	 * all of them and parse names */
	for (i = 0 ; i < n ; i++, state->sheet = NULL) {
		if (NULL == (state->sheet = workbook_sheet_by_index (state->wb, i)))
			continue;
		if (NULL == (part_id = g_object_get_data (G_OBJECT (state->sheet), "_XLSX_RelID"))) {
			xlsx_warning (xin, _("Missing part-id for sheet '%s'"),
				      state->sheet->name_unquoted);
			continue;
		}

		/* Apply the 'Normal' style (aka builtin 0) to the entire sheet */
		if (NULL != (style = g_hash_table_lookup(state->cell_styles, "0"))) {
			GnmRange r;
			gnm_style_ref (style);
			sheet_style_set_range (state->sheet,
				range_init_full_sheet (&r), style);
		}

		if (NULL != (state->sheet_stream = gsf_open_pkg_get_rel (state->stream, part_id))) {
			GsfXMLInDoc *doc = gsf_xml_in_doc_new (xlsx_sheet_dtd, xlsx_ns);
			if (!gsf_xml_in_doc_parse (doc, state->sheet_stream, state))
				gnumeric_io_error_string (state->context, _("is corrupt!"));
			gsf_xml_in_doc_free (doc);
			g_object_unref (G_OBJECT (state->sheet_stream));
			state->sheet_stream = NULL;
		}
		/* Flag a respan here in case nothing else does */
		sheet_flag_recompute_spans (state->sheet);
	}
}

static GsfXMLInNode const xlsx_workbook_dtd[] = {
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE_FULL (START, WORKBOOK, XL_NS_SS, "workbook", GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, &xlsx_wb_end, 0),
  GSF_XML_IN_NODE (WORKBOOK, VERSION, XL_NS_SS,	   "fileVersion", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (WORKBOOK, PROPERTIES, XL_NS_SS, "workbookPr", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (WORKBOOK, CALC_PROPS, XL_NS_SS, "calcPr", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (WORKBOOK, VIEWS,	 XL_NS_SS, "bookViews",	GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (VIEWS,  VIEW,	 XL_NS_SS, "workbookView",  GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (WORKBOOK, SHEETS,	 XL_NS_SS, "sheets", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (SHEETS, SHEET,	 XL_NS_SS, "sheet", GSF_XML_NO_CONTENT, &xlsx_sheet_start, NULL),
  GSF_XML_IN_NODE (WORKBOOK, WEB_PUB,	 XL_NS_SS, "webPublishing", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (WORKBOOK, EXTERNS,	 XL_NS_SS, "externalReferences", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (EXTERNS, EXTERN,	 XL_NS_SS, "externalReference", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (WORKBOOK, NAMES,	 XL_NS_SS, "definedNames", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (NAMES, NAME,	 XL_NS_SS, "definedName", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (WORKBOOK, RECOVERY,	 XL_NS_SS, "fileRecoveryPr", GSF_XML_NO_CONTENT, NULL, NULL),

GSF_XML_IN_NODE_END
};

/****************************************************************************/

static void
xlsx_sst_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int count;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_int (xin, attrs, XL_NS_SS, "uniqueCount", &count))
			g_array_set_size (state->sst, count);
	state->count = 0;
}

static void
xlsx_sstitem_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	XLSXStr *entry;

	if (state->count >= state->sst->len)
		g_array_set_size (state->sst, state->count+1);
	entry = &g_array_index (state->sst, XLSXStr, state->count);
	state->count++;
	entry->str = gnm_string_get (xin->content->str);
	if (state->rich_attrs) {
		entry->markup = go_format_new_markup (state->rich_attrs, FALSE);
		state->rich_attrs = NULL;
	}
}

static GsfXMLInNode const xlsx_shared_strings_dtd[] = {
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE_FULL (START, SST, XL_NS_SS, "sst", GSF_XML_NO_CONTENT, FALSE, TRUE, &xlsx_sst_start, NULL, 0),
  GSF_XML_IN_NODE (SST, ITEM, XL_NS_SS, "sstItem", GSF_XML_CONTENT, NULL, &xlsx_sstitem_end),
    GSF_XML_IN_NODE (ITEM, TEXT, XL_NS_SS, "t", GSF_XML_SHARED_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (ITEM, RICH, XL_NS_SS, "r", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (RICH, RICH_TEXT, XL_NS_SS, "t", GSF_XML_SHARED_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (RICH, RICH_PROPS, XL_NS_SS, "rPr", GSF_XML_NO_CONTENT, NULL, NULL),
#if 0
	GSF_XML_IN_NODE (RICH_PROPS, RICH_FONT, XL_NS_SS, "font", GSF_XML_NO_CONTENT, NULL, NULL),
	/* docs say 'font' xl is generating rFont */
#endif
	GSF_XML_IN_NODE (RICH_PROPS, RICH_FONT, XL_NS_SS, "rFont", GSF_XML_NO_CONTENT, NULL, NULL),

	GSF_XML_IN_NODE (RICH_PROPS, RICH_CHARSET, XL_NS_SS, "charset", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (RICH_PROPS, RICH_FAMILY, XL_NS_SS, "family", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (RICH_PROPS, RICH_BOLD, XL_NS_SS, "b", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (RICH_PROPS, RICH_ITALIC, XL_NS_SS, "i", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (RICH_PROPS, RICH_STRIKE, XL_NS_SS, "strike", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (RICH_PROPS, RICH_OUTLINE, XL_NS_SS, "outline", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (RICH_PROPS, RICH_SHADOW, XL_NS_SS, "shadow", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (RICH_PROPS, RICH_CONDENSE, XL_NS_SS, "condense", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (RICH_PROPS, RICH_EXTEND, XL_NS_SS, "extend", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (RICH_PROPS, RICH_COLOR, XL_NS_SS, "color", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (RICH_PROPS, RICH_SZ, XL_NS_SS, "sz", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (RICH_PROPS, RICH_ULINE, XL_NS_SS, "u", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (RICH_PROPS, RICH_VALIGN, XL_NS_SS, "vertAlign", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (RICH_PROPS, RICH_SCHEME, XL_NS_SS, "scheme", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (RICH, RICH_PROPS, XL_NS_SS, "rPr", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (ITEM, ITEM_PHONETIC_RUN, XL_NS_SS, "rPh", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (ITEM_PHONETIC_RUN, PHONETIC_TEXT, XL_NS_SS, "t", GSF_XML_SHARED_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (ITEM, ITEM_PHONETIC, XL_NS_SS, "phoneticPr", GSF_XML_NO_CONTENT, NULL, NULL),

GSF_XML_IN_NODE_END
};

/****************************************************************************/

static void
xlsx_style_numfmt (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	xmlChar const *fmt = NULL;
	xmlChar const *id = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], XL_NS_SS, "numFmtId"))
			id = attrs[1];
		else if (gsf_xml_in_namecmp (xin, attrs[0], XL_NS_SS, "formatCode"))
			fmt = attrs[1];

	if (NULL != id && NULL != fmt)
		g_hash_table_replace (state->num_fmts, g_strdup (id), 
			go_format_new_from_XL (fmt, FALSE));
}

enum {
	XLSX_COLLECT_FONT,
	XLSX_COLLECT_FILLS,
	XLSX_COLLECT_BORDERS,
	XLSX_COLLECT_XFS,
	XLSX_COLLECT_STYLE_XFS,
	XLSX_COLLECT_DXFS,
	XLSX_COLLECT_TABLE_STYLES
};

static void
xlsx_collection_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int count = 0;

	g_return_if_fail (NULL == state->collection);

	state->count = 0;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_int (xin, attrs, XL_NS_SS, "count", &count))
			;
	state->collection = g_ptr_array_new ();
	g_ptr_array_set_size (state->collection, count);

	switch (xin->node->user_data.v_int) {
	case XLSX_COLLECT_FONT :	state->fonts = state->collection;	 break;
	case XLSX_COLLECT_FILLS :	state->fills = state->collection;	 break;
	case XLSX_COLLECT_BORDERS :	state->borders = state->collection;	 break;
	case XLSX_COLLECT_XFS :		state->xfs = state->collection;	  	 break;
	case XLSX_COLLECT_STYLE_XFS :	state->style_xfs = state->collection;	 break;
	case XLSX_COLLECT_DXFS :	state->dxfs = state->collection;	 break;
	case XLSX_COLLECT_TABLE_STYLES: state->table_styles = state->collection; break;
	}
}

static void
xlsx_collection_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;

	/* resize just in case the count hint was wrong */
	g_ptr_array_set_size (state->collection, state->count);
	state->count = 0;
	state->collection = NULL;
}

static void
xlsx_col_elem_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;

	state->style_accum = gnm_style_new ();
}
static void
xlsx_col_elem_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;

	if (state->count >= state->collection->len)
		g_ptr_array_add (state->collection, state->style_accum);
	else
		g_ptr_array_index (state->collection, state->count) = state->style_accum;
	state->count++;
	state->style_accum = NULL;
}

static void
xlsx_font_name (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], XL_NS_SS, "val"))
			gnm_style_set_font_name	(state->style_accum, attrs[1]);
}
static void
xlsx_font_bold (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int val = TRUE;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_bool (xin, attrs, XL_NS_SS, "val", &val)) ;
			;
	gnm_style_set_font_bold (state->style_accum, val);
}
static void
xlsx_font_italic (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int val = TRUE;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_bool (xin, attrs, XL_NS_SS, "val", &val)) ;
			;
	gnm_style_set_font_italic (state->style_accum, val);
}
static void
xlsx_font_strike (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int val = TRUE;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_bool (xin, attrs, XL_NS_SS, "val", &val))
			;
	gnm_style_set_font_strike (state->style_accum, val);
}
static void
xlsx_font_color (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GnmColor *color = elem_color (xin, attrs);

	if (NULL != color)
		gnm_style_set_font_color (state->style_accum, color);
}
static void
xlsx_CT_FontSize (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	double val;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_float (xin, attrs, XL_NS_SS, "val", &val))
			gnm_style_set_font_size	(state->style_accum, val);
}
static void
xlsx_font_uline (GsfXMLIn *xin, xmlChar const **attrs)
{
	static EnumVal const types[] = {
		{ "single", UNDERLINE_SINGLE }, 
		{ "double", UNDERLINE_DOUBLE },
		{ "singleAccounting", UNDERLINE_SINGLE }, 
		{ "doubleAccounting", UNDERLINE_DOUBLE },
		{ "none", UNDERLINE_NONE },
		{ NULL, 0 }
	};
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int val = UNDERLINE_SINGLE; 

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_enum (xin, attrs, XL_NS_SS, "val", types, &val))
			;
	gnm_style_set_font_uline (state->style_accum, val);
}

static void
xlsx_font_valign (GsfXMLIn *xin, xmlChar const **attrs)
{
	static EnumVal const types[] = {
		{ "baseline",	 GO_FONT_SCRIPT_STANDARD },
		{ "superscript", GO_FONT_SCRIPT_SUPER },
		{ "subscript",   GO_FONT_SCRIPT_SUB },
		{ NULL, 0 }
	};
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int val = UNDERLINE_SINGLE; 

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_enum (xin, attrs, XL_NS_SS, "val", types, &val))
			gnm_style_set_font_script (state->style_accum, val);
}

static void
xlsx_pattern (GsfXMLIn *xin, xmlChar const **attrs)
{
	static EnumVal const patterns[] = {
		{ "none",		0 },
		{ "solid",		1 },
		{ "mediumGray",		3 },
		{ "darkGray",		2 },
		{ "lightGray",		4 },
		{ "darkHorizontal",	7 },
		{ "darkVertical",	8 },
		{ "darkDown",		10},
		{ "darkUp",		9 },
		{ "darkGrid",		11 },
		{ "darkTrellis",	12 },
		{ "lightHorizontal",	13 },
		{ "lightVertical",	14 },
		{ "lightDown",		15 },
		{ "lightUp",		16 },
		{ "lightGrid",		17 },
		{ "lightTrellis",	18 },
		{ "gray125",		5 },
		{ "gray0625",		6 },
		{ NULL, 0 }
	};
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int val = 0; /* none */

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_enum (xin, attrs, XL_NS_SS, "patternType", patterns, &val))
			gnm_style_set_pattern (state->style_accum, val);
}
static void
xlsx_pattern_fg (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GnmColor *color = elem_color (xin, attrs);

	if (NULL != color) {
		if (gnm_style_get_pattern (state->style_accum) == 1)
			gnm_style_set_back_color (state->style_accum, color);
		else
			gnm_style_set_pattern_color (state->style_accum, color);
	}
}
static void
xlsx_pattern_bg (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GnmColor *color = elem_color (xin, attrs);
	if (NULL != color) {
		if (gnm_style_get_pattern (state->style_accum) != 1)
			gnm_style_set_back_color (state->style_accum, color);
		else
			gnm_style_set_pattern_color (state->style_accum, color);
	}
}

static void
xlsx_border_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	static EnumVal const borders[] = {
		{ "none",		STYLE_BORDER_NONE },
		{ "thin",		STYLE_BORDER_THIN },
		{ "medium",		STYLE_BORDER_MEDIUM },
		{ "dashed",		STYLE_BORDER_DASHED },
		{ "dotted",		STYLE_BORDER_DOTTED },
		{ "thick",		STYLE_BORDER_THICK },
		{ "double",		STYLE_BORDER_DOUBLE },
		{ "hair",		STYLE_BORDER_HAIR },
		{ "mediumDashed",	STYLE_BORDER_MEDIUM_DASH },
		{ "dashDot",		STYLE_BORDER_DASH_DOT },
		{ "mediumDashDot",	STYLE_BORDER_MEDIUM_DASH_DOT },
		{ "dashDotDot",		STYLE_BORDER_DASH_DOT_DOT },
		{ "mediumDashDotDot",	STYLE_BORDER_MEDIUM_DASH_DOT_DOT },
		{ "slantDashDot",	STYLE_BORDER_SLANTED_DASH_DOT },
		{ NULL, 0 }
	};
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int border_style = STYLE_BORDER_NONE; 

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_enum (xin, attrs, XL_NS_SS, "style", borders, &border_style))
			;
	state->border_style = border_style;
	state->border_color = NULL;
}

static void
xlsx_border_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	StyleBorderLocation const loc = xin->node->user_data.v_int;
	GnmBorder *border = style_border_fetch (state->border_style,
		state->border_color, style_border_get_orientation (loc));
	gnm_style_set_border (state->style_accum,
		STYLE_BORDER_LOCATION_TO_STYLE_ELEMENT (loc),
		border);
}

static void
xlsx_border_color (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GnmColor *color = elem_color (xin, attrs);
	if (state->border_color)
		style_color_unref (state->border_color);
	state->border_color = color;
}

static void
xlsx_xf_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GnmStyle *style;
	GPtrArray *elem = NULL;
	int indx;

	state->style_accum = gnm_style_new_default ();
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (gsf_xml_in_namecmp (xin, attrs[0], XL_NS_SS, "numFmtId")) {
			GOFormat *fmt = xlsx_get_num_fmt (xin, attrs[1]);
			if (NULL != fmt)
				gnm_style_set_format (state->style_accum, fmt);
		} else if (attr_int (xin, attrs, XL_NS_SS, "fontId", &indx))
			elem = state->fonts;
		else if (attr_int (xin, attrs, XL_NS_SS, "fillId", &indx))
			elem = state->fills;
		else if (attr_int (xin, attrs, XL_NS_SS, "borderId", &indx))
			elem = state->borders;

		if (NULL != elem) {
			if (0 <= indx && indx < (int)elem->len)
				style = g_ptr_array_index (elem, indx);
			else
				style = NULL;
			if (NULL != style) {
				style = gnm_style_merge (state->style_accum, style);
				gnm_style_unref (state->style_accum);
				state->style_accum = style;
			} else
				xlsx_warning (xin, _("Undefined font record '%d'"), indx);
			elem = NULL;
		}
	}
#if 0
		"xfId"
		"quotePrefix"
		"applyNumberFormat"
		"applyFont"
		"applyFill"
		"applyBorder"
		"applyAlignment"
		"applyProtection"
#endif
}
static void
xlsx_xf_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;

	if (state->count >= state->collection->len)
		g_ptr_array_add (state->collection, state->style_accum);
	else
		g_ptr_array_index (state->collection, state->count) = state->style_accum;
	state->count++;
	state->style_accum = NULL;
}

static void
xlsx_xf_align (GsfXMLIn *xin, xmlChar const **attrs)
{
	static EnumVal const haligns[] = {
		{ "general" , HALIGN_GENERAL },
		{ "left" , HALIGN_LEFT },
		{ "center" , HALIGN_CENTER },
		{ "right" , HALIGN_RIGHT },
		{ "fill" , HALIGN_FILL },
		{ "justify" , HALIGN_JUSTIFY },
		{ "centerContinuous" , HALIGN_CENTER_ACROSS_SELECTION },
		{ "distributed" , HALIGN_DISTRIBUTED },
		{ NULL, 0 }
	};

	static EnumVal const valigns[] = {
		{ "top", VALIGN_TOP },
		{ "center", VALIGN_CENTER },
		{ "bottom", VALIGN_BOTTOM },
		{ "justify", VALIGN_JUSTIFY },
		{ "distributed", VALIGN_DISTRIBUTED },
		{ NULL, 0 }
	};

	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int halign = HALIGN_GENERAL;
	int valign = VALIGN_BOTTOM;
	int rotation = 0, indent = 0;
	int wrapText = FALSE, justifyLastLine = FALSE, shrinkToFit = FALSE;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_enum (xin, attrs, XL_NS_SS, "horizontal", haligns, &halign)) ;
		else if (attr_enum (xin, attrs, XL_NS_SS, "vertical", valigns, &valign)) ;
		else if (attr_int (xin, attrs, XL_NS_SS, "textRotation", &rotation));
		else if (attr_bool (xin, attrs, XL_NS_SS, "wrapText", &wrapText)) ;
		else if (attr_int (xin, attrs, XL_NS_SS, "indent", &indent)) ;
		else if (attr_bool (xin, attrs, XL_NS_SS, "justifyLastLine", &justifyLastLine)) ;
		else if (attr_bool (xin, attrs, XL_NS_SS, "shrinkToFit", &shrinkToFit)) ;
		/* "mergeCell" type="xs:boolean" use="optional" default="false" */
		/* "readingOrder" type="xs:unsignedInt" use="optional" default="0" */

		gnm_style_set_align_h	   (state->style_accum, halign);
		gnm_style_set_align_v	   (state->style_accum, valign);
		gnm_style_set_rotation	   (state->style_accum,
			(rotation == 0xff) ? -1 : ((rotation > 90) ? (360 + 90 - rotation) : rotation));
		gnm_style_set_wrap_text   (state->style_accum, wrapText);
		gnm_style_set_indent	   (state->style_accum, indent);
		gnm_style_set_shrink_to_fit (state->style_accum, shrinkToFit);
}
static void
xlsx_xf_protect (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int locked = TRUE;
	int hidden = TRUE;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_bool (xin, attrs, XL_NS_SS, "locked", &locked)) ;
		else if (attr_bool (xin, attrs, XL_NS_SS, "hidden", &hidden)) ;
	gnm_style_set_contents_locked (state->style_accum, locked);
	gnm_style_set_contents_hidden (state->style_accum, hidden);
}

static void
xlsx_cell_style (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	xmlChar const *name = NULL;
	xmlChar const *id = NULL;
	GnmStyle *style = NULL;
	int tmp;

	/* cellStyle name="Normal" xfId="0" builtinId="0" */
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_int (xin, attrs, XL_NS_SS, "xfId", &tmp))
			style = xlsx_get_xf (xin, tmp);
		else if (gsf_xml_in_namecmp (xin, attrs[0], XL_NS_SS, "name"))
			name = attrs[1];
		else if (gsf_xml_in_namecmp (xin, attrs[0], XL_NS_SS, "builtinId"))
			id = attrs[1];

	if (NULL != style && NULL != id) {
		gnm_style_ref (style);
		g_hash_table_replace (state->cell_styles, g_strdup (id), style);
	}
}

static GsfXMLInNode const xlsx_styles_dtd[] = {
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE_FULL (START, STYLE_INFO, XL_NS_SS, "styleSheet", GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),

  GSF_XML_IN_NODE (STYLE_INFO, NUM_FMTS, XL_NS_SS, "numFmts", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (NUM_FMTS, NUM_FMT, XL_NS_SS, "numFmt", GSF_XML_NO_CONTENT, &xlsx_style_numfmt, NULL),

  GSF_XML_IN_NODE_FULL (STYLE_INFO, FONTS, XL_NS_SS, "fonts", GSF_XML_NO_CONTENT, 
			FALSE, FALSE, &xlsx_collection_start, &xlsx_collection_end, XLSX_COLLECT_FONT),
    GSF_XML_IN_NODE (FONTS, FONT, XL_NS_SS, "font", GSF_XML_NO_CONTENT, &xlsx_col_elem_start, &xlsx_col_elem_end),
      GSF_XML_IN_NODE (FONT, FONT_NAME,	     XL_NS_SS, "name",	    GSF_XML_NO_CONTENT, &xlsx_font_name, NULL),
      GSF_XML_IN_NODE (FONT, FONT_CHARSET,   XL_NS_SS, "charset",   GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (FONT, FONT_FAMILY,    XL_NS_SS, "family",    GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (FONT, FONT_BOLD,	     XL_NS_SS, "b",	    GSF_XML_NO_CONTENT, &xlsx_font_bold, NULL),
      GSF_XML_IN_NODE (FONT, FONT_ITALIC,    XL_NS_SS, "i",	    GSF_XML_NO_CONTENT, &xlsx_font_italic, NULL),
      GSF_XML_IN_NODE (FONT, FONT_STRIKE,    XL_NS_SS, "strike",    GSF_XML_NO_CONTENT, &xlsx_font_strike, NULL),
      GSF_XML_IN_NODE (FONT, FONT_OUTLINE,   XL_NS_SS, "outline",   GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (FONT, FONT_SHADOW,    XL_NS_SS, "shadow",    GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (FONT, FONT_CONDENSE,  XL_NS_SS, "condense",  GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (FONT, FONT_EXTEND,    XL_NS_SS, "extend",    GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (FONT, FONT_COLOR,     XL_NS_SS, "color",     GSF_XML_NO_CONTENT, &xlsx_font_color, NULL),
      GSF_XML_IN_NODE (FONT, FONT_SZ,	     XL_NS_SS, "sz",	    GSF_XML_NO_CONTENT,	&xlsx_CT_FontSize, NULL),
      GSF_XML_IN_NODE (FONT, FONT_ULINE,     XL_NS_SS, "u",	    GSF_XML_NO_CONTENT,	&xlsx_font_uline, NULL),
      GSF_XML_IN_NODE (FONT, FONT_VERTALIGN, XL_NS_SS, "vertAlign", GSF_XML_NO_CONTENT, &xlsx_font_valign, NULL),
      GSF_XML_IN_NODE (FONT, FONT_SCHEME,    XL_NS_SS, "scheme",    GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE_FULL (STYLE_INFO, FILLS, XL_NS_SS, "fills", GSF_XML_NO_CONTENT,
			FALSE, FALSE, &xlsx_collection_start, &xlsx_collection_end, XLSX_COLLECT_FILLS),
    GSF_XML_IN_NODE (FILLS, FILL, XL_NS_SS, "fill", GSF_XML_NO_CONTENT, &xlsx_col_elem_start, &xlsx_col_elem_end),
      GSF_XML_IN_NODE (FILL, PATTERN_FILL, XL_NS_SS, "patternFill", GSF_XML_NO_CONTENT, xlsx_pattern, NULL),
	GSF_XML_IN_NODE (PATTERN_FILL, PATTERN_FILL_FG,  XL_NS_SS, "fgColor", GSF_XML_NO_CONTENT, xlsx_pattern_fg, NULL),
	GSF_XML_IN_NODE (PATTERN_FILL, PATTERN_FILL_BG,  XL_NS_SS, "bgColor", GSF_XML_NO_CONTENT, xlsx_pattern_bg, NULL),
      GSF_XML_IN_NODE (FILL, IMAGE_FILL, XL_NS_SS, "image", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (FILL, GRADIENT_FILL, XL_NS_SS, "gradient", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (GRADIENT_FILL, GRADIENT_STOPS, XL_NS_SS, "stop", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE_FULL (STYLE_INFO, BORDERS, XL_NS_SS, "borders", GSF_XML_NO_CONTENT,
			FALSE, FALSE, &xlsx_collection_start, &xlsx_collection_end, XLSX_COLLECT_BORDERS),
    GSF_XML_IN_NODE (BORDERS, BORDER, XL_NS_SS, "border", GSF_XML_NO_CONTENT, &xlsx_col_elem_start, &xlsx_col_elem_end),
      GSF_XML_IN_NODE_FULL (BORDER, LEFT_B, XL_NS_SS, "left", GSF_XML_NO_CONTENT, FALSE, FALSE,
			    &xlsx_border_start, &xlsx_border_end, STYLE_BORDER_LEFT),
        GSF_XML_IN_NODE (LEFT_B, LEFT_COLOR, XL_NS_SS, "color", GSF_XML_NO_CONTENT, &xlsx_border_color, NULL),
      GSF_XML_IN_NODE_FULL (BORDER, RIGHT_B, XL_NS_SS, "right", GSF_XML_NO_CONTENT, FALSE, FALSE,
			    &xlsx_border_start, &xlsx_border_end, STYLE_BORDER_RIGHT),
        GSF_XML_IN_NODE (RIGHT_B, RIGHT_COLOR, XL_NS_SS, "color", GSF_XML_NO_CONTENT, &xlsx_border_color, NULL),
      GSF_XML_IN_NODE_FULL (BORDER, TOP_B, XL_NS_SS,	"top", GSF_XML_NO_CONTENT, FALSE, FALSE,
			    &xlsx_border_start, &xlsx_border_end, STYLE_BORDER_TOP),
        GSF_XML_IN_NODE (TOP_B, TOP_COLOR, XL_NS_SS, "color", GSF_XML_NO_CONTENT, &xlsx_border_color, NULL),
      GSF_XML_IN_NODE_FULL (BORDER, BOTTOM_B, XL_NS_SS, "bottom", GSF_XML_NO_CONTENT, FALSE, FALSE,
			    &xlsx_border_start, &xlsx_border_end, STYLE_BORDER_BOTTOM),
        GSF_XML_IN_NODE (BOTTOM_B, BOTTOM_COLOR, XL_NS_SS, "color", GSF_XML_NO_CONTENT, &xlsx_border_color, NULL),
      GSF_XML_IN_NODE_FULL (BORDER, DIAG_B, XL_NS_SS, "diagonal", GSF_XML_NO_CONTENT, FALSE, FALSE,
			    &xlsx_border_start, &xlsx_border_end, STYLE_BORDER_DIAG),
        GSF_XML_IN_NODE (DIAG_B, DIAG_COLOR, XL_NS_SS, "color", GSF_XML_NO_CONTENT, &xlsx_border_color, NULL),

      GSF_XML_IN_NODE (BORDER, BORDER_VERT, XL_NS_SS,	"vertical", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (BORDER_VERT, VERT_COLOR, XL_NS_SS, "color", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (BORDER, BORDER_HORIZ, XL_NS_SS,	"horizontal", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (BORDER_HORIZ, HORIZ_COLOR, XL_NS_SS, "color", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE_FULL (STYLE_INFO, XFS, XL_NS_SS, "cellXfs", GSF_XML_NO_CONTENT,
			FALSE, FALSE, &xlsx_collection_start, &xlsx_collection_end, XLSX_COLLECT_XFS),
    GSF_XML_IN_NODE (XFS, XF, XL_NS_SS, "xf", GSF_XML_NO_CONTENT, &xlsx_xf_start, &xlsx_xf_end),
      GSF_XML_IN_NODE (XF, ALIGNMENT, XL_NS_SS, "alignment", GSF_XML_NO_CONTENT, &xlsx_xf_align, NULL),
      GSF_XML_IN_NODE (XF, PROTECTION, XL_NS_SS, "protection", GSF_XML_NO_CONTENT, &xlsx_xf_protect, NULL),

  GSF_XML_IN_NODE_FULL (STYLE_INFO, STYLE_XFS, XL_NS_SS, "cellStyleXfs", GSF_XML_NO_CONTENT,
		   FALSE, TRUE, &xlsx_collection_start, &xlsx_collection_end, XLSX_COLLECT_STYLE_XFS),
    GSF_XML_IN_NODE (STYLE_XFS, STYLE_XF, XL_NS_SS, "xf", GSF_XML_NO_CONTENT, &xlsx_xf_start, &xlsx_xf_end),
      GSF_XML_IN_NODE (STYLE_XF, STYLE_ALIGNMENT, XL_NS_SS, "alignment", GSF_XML_NO_CONTENT, &xlsx_xf_align, NULL),
      GSF_XML_IN_NODE (STYLE_XF, STYLE_PROTECTION, XL_NS_SS, "protection", GSF_XML_NO_CONTENT, &xlsx_xf_protect, NULL),

  GSF_XML_IN_NODE (STYLE_INFO, STYLE_NAMES, XL_NS_SS, "cellStyles", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (STYLE_NAMES, STYLE_NAME, XL_NS_SS, "cellStyle", GSF_XML_NO_CONTENT, &xlsx_cell_style, NULL),

  GSF_XML_IN_NODE_FULL (STYLE_INFO, PARTIAL_XFS, XL_NS_SS, "dxfs", GSF_XML_NO_CONTENT,
			FALSE, FALSE, &xlsx_collection_start, &xlsx_collection_end, XLSX_COLLECT_DXFS),
    GSF_XML_IN_NODE (PARTIAL_XFS, PARTIAL_XF, XL_NS_SS, "dxf", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (PARTIAL_XF, NUM_FMT, XL_NS_SS, "numFmt", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (PARTIAL_XF, FONT,    XL_NS_SS, "font", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (PARTIAL_XF, DXF_FILL,    XL_NS_SS, "fill", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (DXF_FILL, DXF_PATTERN_FILL, XL_NS_SS, "patternFill", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (DXF_PATTERN_FILL, DXF_PATTERN_FILL_FG,  XL_NS_SS, "fgColor", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (DXF_PATTERN_FILL, DXF_PATTERN_FILL_BG,  XL_NS_SS, "bgColor", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (PARTIAL_XF, BORDER,  XL_NS_SS, "border", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (PARTIAL_XF, DXF_ALIGNMENT, XL_NS_SS, "alignment", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (PARTIAL_XF, DXF_PROTECTION, XL_NS_SS, "protection", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (PARTIAL_XF, DXF_FSB, XL_NS_SS, "fsb", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE_FULL (STYLE_INFO, TABLE_STYLES, XL_NS_SS, "tableStyles", GSF_XML_NO_CONTENT,
			FALSE, FALSE, &xlsx_collection_start, &xlsx_collection_end, XLSX_COLLECT_TABLE_STYLES),
    GSF_XML_IN_NODE (TABLE_STYLES, TABLE_STYLE, XL_NS_SS, "tableStyle", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (STYLE_INFO, COLORS, XL_NS_SS, "colors", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (COLORS, INDEXED_COLORS, XL_NS_SS, "indexedColors", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (INDEXED_COLORS, INDEXED_RGB, XL_NS_SS, "rgbColor", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (COLORS, THEME_COLORS, XL_NS_SS, "themeColors", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (THEME_COLORS, THEMED_RGB, XL_NS_SS, "rgbColor", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (COLORS, MRU_COLORS, XL_NS_SS, "mruColors", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (MRU_COLORS, MRU_COLOR, XL_NS_SS, "color", GSF_XML_NO_CONTENT, NULL, NULL),

GSF_XML_IN_NODE_END
};
/****************************************************************************/

G_MODULE_EXPORT gboolean
xlsx_file_probe (GOFileOpener const *fo, GsfInput *input, FileProbeLevel pl);

gboolean
xlsx_file_probe (GOFileOpener const *fo, GsfInput *input, FileProbeLevel pl)
{
	GsfInfile *zip;
	GsfInput  *stream;
	gboolean   res = FALSE;

	if (NULL != (zip = gsf_infile_zip_new (input, NULL))) {
		if (NULL != (stream = gsf_infile_child_by_vname (zip, "xl", "workbook.xml", NULL))) {
			g_object_unref (G_OBJECT (stream));
			res = TRUE;
		}
		g_object_unref (G_OBJECT (zip));
	}
	return res;
}

static gboolean
xlsx_parse_stream (XLSXReadState *state, char const *stream_name, GsfXMLInNode const *dtd)
{
	gboolean	 success = FALSE;

	if (NULL != (state->stream = gsf_infile_child_by_vname (state->zip, "xl", stream_name, NULL))) {
		GsfXMLInDoc *doc = gsf_xml_in_doc_new (dtd, xlsx_ns);
		if (gsf_xml_in_doc_parse (doc, state->stream, state))
			success = TRUE;
		else
			gnumeric_io_error_string (state->context, _("is corrupt!"));
		gsf_xml_in_doc_free (doc);
		g_object_unref (G_OBJECT (state->stream));
		state->stream = NULL;
	}
	return success;
}

static void
xlsx_style_array_free (GPtrArray *styles)
{
	if (styles != NULL) {
		unsigned i = styles->len;
		GnmStyle *style;
		while (i-- > 0)
			if (NULL != (style = g_ptr_array_index (styles, i)))
				gnm_style_unref (style);

		g_ptr_array_free (styles, TRUE);
	}
}

G_MODULE_EXPORT void
xlsx_file_open (GOFileOpener const *fo, IOContext *context,
		WorkbookView *wbv, GsfInput *input);

void
xlsx_file_open (GOFileOpener const *fo, IOContext *io_context,
		WorkbookView *wb_view, GsfInput *input)
{
	char *old_num_locale, *old_monetary_locale;
	XLSXReadState	 state;

	memset (&state, 0, sizeof (XLSXReadState));
	state.context	= io_context;
	state.wb_view	= wb_view;
	state.wb	= wb_view_get_workbook (wb_view);
	state.sheet	= NULL;
	state.sst = g_array_new (FALSE, TRUE, sizeof (XLSXStr));
	state.shared_exprs = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify)g_free, (GDestroyNotify) gnm_expr_top_unref);
	state.cell_styles = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify)g_free, (GDestroyNotify) gnm_style_unref);
	state.num_fmts = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify)g_free, (GDestroyNotify) go_format_unref);

	old_num_locale = g_strdup (go_setlocale (LC_NUMERIC, NULL));
	go_setlocale (LC_NUMERIC, "C");
	old_monetary_locale = g_strdup (go_setlocale (LC_MONETARY, NULL));
	go_setlocale (LC_MONETARY, "C");
	go_set_untranslated_bools ();

	if (NULL != (state.zip = gsf_infile_zip_new (input, NULL))) {
		/* optional */
		xlsx_parse_stream (&state, "sharedStrings.xml", xlsx_shared_strings_dtd);

		xlsx_parse_stream (&state, "styles.xml", xlsx_styles_dtd);
		xlsx_parse_stream (&state, "workbook.xml", xlsx_workbook_dtd);
		g_object_unref (G_OBJECT (state.zip));
	}

	/* go_setlocale restores bools to locale translation */
	go_setlocale (LC_MONETARY, old_monetary_locale);
	g_free (old_monetary_locale);
	go_setlocale (LC_NUMERIC, old_num_locale);
	g_free (old_num_locale);

	if (NULL != state.sst) {
		unsigned i = state.sst->len;
		XLSXStr *entry;
		while (i-- > 0) {
			entry = &g_array_index (state.sst, XLSXStr, i);
			gnm_string_unref (entry->str);
			if (NULL != entry->markup)
				go_format_unref (entry->markup);
		}
		g_array_free (state.sst, TRUE);
	}
	g_hash_table_destroy (state.num_fmts);
	g_hash_table_destroy (state.cell_styles);
	g_hash_table_destroy (state.shared_exprs);
	xlsx_style_array_free (state.fonts);
	xlsx_style_array_free (state.fills);
	xlsx_style_array_free (state.borders);
	xlsx_style_array_free (state.xfs);
	xlsx_style_array_free (state.style_xfs);
	xlsx_style_array_free (state.dxfs);
	xlsx_style_array_free (state.table_styles);
}

/* TODO * TODO * TODO
 *
 * Named expressions
 * rich text
 * conditional formats
 * validation
 * autofilters
 * workbook/calc properties
 * print settings
 * comments
 * text direction in styles
 *
 * IMPROVE
 * 	- column widths : Don't use hard coded font side
 * 	- share colours
 **/
