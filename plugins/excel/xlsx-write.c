/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * xlsx-write.c : export MS Office Open xlsx files.
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

/*****************************************************************************/

#include <gnumeric-config.h>
#include "ms-excel-write.h"
#include "xlsx-utils.h"

#include "parse-util.h"
#include "workbook.h"
#include "workbook-priv.h"
#include "workbook-view.h"
#include "sheet.h"
#include "sheet-style.h"
#include "sheet-view.h"
#include "sheet-filter.h"
#include "ranges.h"
#include "value.h"
#include "cell.h"
#include "expr.h"
#include "expr-impl.h"
#include "func.h"
#include "str.h"
#include "style-color.h"
#include "print-info.h"
#include "gutils.h"

#include <goffice/app/file.h>
#include <goffice/utils/go-format.h>
#include <gsf/gsf-output.h>
#include <gsf/gsf-outfile-impl.h>
#include <gsf/gsf-outfile-zip.h>
#include <gsf/gsf-utils.h>
#include <gsf/gsf-impl-utils.h>
#include <gsf/gsf-libxml.h>
#include <glib/gi18n-lib.h>
#include <gmodule.h>

/*************************************************************/
typedef struct _GsfOutfileOpenPkg GsfOutfileOpenPkg;

#define GSF_OUTFILE_OPEN_PKG_TYPE	(gsf_outfile_open_pkg_get_type ())
#define GSF_OUTFILE_OPEN_PKG(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GSF_OUTFILE_OPEN_PKG_TYPE, GsfOutfileOpenPkg))
#define GSF_IS_OUTFILE_OPEN_PKG(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GSF_OUTFILE_OPEN_PKG_TYPE))

static GType	    gsf_outfile_open_pkg_get_type (void);
static GsfOutfile *gsf_outfile_open_pkg_new	  (GsfOutfile *sink);

struct _GsfOutfileOpenPkg {
	GsfOutfile  parent;

	GsfOutput  *sink;
	gboolean    is_dir;
	char	   *content_type;
	GSList	   *children;
	GSList	   *relations;
};
typedef struct {
	char *id, *type, *target;
} GsfOutfileOpenPkgRel;

/*************************************************************/
#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "libgsf:open_pkg"

typedef GsfOutfileClass GsfOutfileOpenPkgClass;

enum {
	PROP_0,
	PROP_SINK,
	PROP_CONTENT_TYPE,
	PROP_IS_DIR,
	PROP_IS_ROOT
};

static GObjectClass *parent_class;
static void
gsf_outfile_open_pkg_set_sink (GsfOutfileOpenPkg *open_pkg, GsfOutput *sink)
{
	if (sink)
		g_object_ref (sink);
	if (open_pkg->sink)
		g_object_unref (open_pkg->sink);
	open_pkg->sink = sink;
}

static void
gsf_outfile_open_pkg_set_content_type (GsfOutfileOpenPkg *open_pkg,
				       char const *content_type)
{
	g_return_if_fail (content_type != NULL);

	if (open_pkg->content_type != content_type) {
		g_free (open_pkg->content_type);
		open_pkg->content_type = g_strdup (content_type);
	}
}

static void
gsf_outfile_open_pkg_get_property (GObject     *object,
				   guint        property_id,
				   GValue      *value,
				   GParamSpec  *pspec)
{
	GsfOutfileOpenPkg *open_pkg = (GsfOutfileOpenPkg *)object;

	switch (property_id) {
	case PROP_SINK:
		g_value_set_object (value, open_pkg->sink);
		break;
	case PROP_CONTENT_TYPE:
		g_value_set_string (value, open_pkg->content_type);
		break;
	case PROP_IS_DIR:
		g_value_set_boolean (value, open_pkg->is_dir);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
gsf_outfile_open_pkg_set_property (GObject      *object,
				   guint         property_id,
				   GValue const *value,
				   GParamSpec   *pspec)
{
	GsfOutfileOpenPkg *open_pkg = (GsfOutfileOpenPkg *)object;

	switch (property_id) {
	case PROP_SINK:
		gsf_outfile_open_pkg_set_sink (open_pkg, g_value_get_object (value));
		break;
	case PROP_CONTENT_TYPE:
		gsf_outfile_open_pkg_set_content_type (open_pkg, g_value_get_string (value));
		break;
	case PROP_IS_DIR:
		open_pkg->is_dir = g_value_get_boolean (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
gsf_outfile_open_pkg_init (GObject *obj)
{
	GsfOutfileOpenPkg *open_pkg = GSF_OUTFILE_OPEN_PKG (obj);

	open_pkg->sink = NULL;
	open_pkg->content_type = NULL;
	open_pkg->is_dir = FALSE;
	open_pkg->children = NULL;
	open_pkg->relations = NULL;
}

static void
gsf_outfile_open_pkg_finalize (GObject *obj)
{
	GsfOutfileOpenPkg *open_pkg = GSF_OUTFILE_OPEN_PKG (obj);
	GSList *ptr;

	if (open_pkg->sink != NULL) {
		g_object_unref (open_pkg->sink);
		open_pkg->sink = NULL;
	}
	g_free (open_pkg->content_type);
	open_pkg->content_type = NULL;

	for (ptr = open_pkg->children ; ptr != NULL ; ptr = ptr->next)
		g_object_unref (ptr->data);
	g_slist_free (open_pkg->children);
	parent_class->finalize (obj);
}

static gboolean
gsf_outfile_open_pkg_write (GsfOutput *output, size_t num_bytes, guint8 const *data)
{
	GsfOutfileOpenPkg *open_pkg = GSF_OUTFILE_OPEN_PKG (output);
	return gsf_output_write (open_pkg->sink, num_bytes, data);
}
static gboolean
gsf_outfile_open_pkg_seek (GsfOutput *output, gsf_off_t offset, GSeekType whence)
{
	GsfOutfileOpenPkg *open_pkg = GSF_OUTFILE_OPEN_PKG (output);
	return gsf_output_seek (open_pkg->sink, offset, whence);
}
static GsfOutput *
gsf_outfile_open_pkg_new_child (GsfOutfile *parent,
				char const *name, gboolean is_dir,
				char const *first_property_name, va_list args)
{
	GsfOutfileOpenPkg *child, *open_pkg = GSF_OUTFILE_OPEN_PKG (parent);
	GsfOutput *sink;

	if (!open_pkg->is_dir)
		return NULL;

	child = (GsfOutfileOpenPkg *)g_object_new_valist (
		GSF_OUTFILE_OPEN_PKG_TYPE, first_property_name, args);
	gsf_output_set_name (GSF_OUTPUT (child), name);
	gsf_output_set_container (GSF_OUTPUT (child), parent);
	child->is_dir = is_dir;

	sink = gsf_outfile_new_child (GSF_OUTFILE (open_pkg->sink), name, is_dir);
	gsf_outfile_open_pkg_set_sink (child, sink);
	g_object_unref (sink);

	open_pkg->children = g_slist_prepend (open_pkg->children, child);
	g_object_ref (child);

	return GSF_OUTPUT (child);
}

static void
gsf_open_pkg_write_content_default (GsfXMLOut *xml, char const *ext, char const *type)
{
	gsf_xml_out_start_element (xml, "Default");
	gsf_xml_out_add_cstr (xml, "Extension", ext);
	gsf_xml_out_add_cstr (xml, "ContentType", type);
	gsf_xml_out_end_element (xml); /* </Default> */
}
static void
gsf_open_pkg_write_content_override (GsfOutfileOpenPkg const *open_pkg,
				     char const *base,
				     GsfXMLOut *xml)
{
	GsfOutfileOpenPkg const *child;
	char   *path;
	GSList *ptr;

	for (ptr = open_pkg->children ; ptr != NULL ; ptr = ptr->next) {
		child = ptr->data;
		if (child->is_dir) {
			path = g_strconcat (base, gsf_output_name (GSF_OUTPUT (child)), "/", NULL);
			gsf_open_pkg_write_content_override (child, path, xml);
		} else {
			path = g_strconcat (base, gsf_output_name (GSF_OUTPUT (child)), NULL);
			/* rels files do need content types, the defaults handle them */
			if (NULL != child->content_type) {
				gsf_xml_out_start_element (xml, "Override");
				gsf_xml_out_add_cstr (xml, "PartName", path);
				gsf_xml_out_add_cstr (xml, "ContentType", child->content_type);
				gsf_xml_out_end_element (xml); /* </Override> */
			}
		}
		g_free (path);
	}
}

static gboolean
gsf_outfile_open_pkg_close (GsfOutput *output)
{
	GsfOutfileOpenPkg *open_pkg = GSF_OUTFILE_OPEN_PKG (output);
	GsfOutput *dir;
	gboolean res = FALSE;
	char *rels_name;

	if (NULL == open_pkg->sink || gsf_output_is_closed (open_pkg->sink))
		return TRUE;

	/* Generate [Content_types].xml when we close the root dir */
	if (NULL == gsf_output_name (output)) {
		GsfOutput *out = gsf_outfile_new_child (GSF_OUTFILE (open_pkg->sink),
			"[Content_Types].xml", FALSE);
		GsfXMLOut *xml = gsf_xml_out_new (out);

		gsf_xml_out_start_element (xml, "Types");
		gsf_xml_out_add_cstr_unchecked (xml, "xmlns",
			"http://schemas.openxmlformats.org/package/2006/content-types");
		gsf_open_pkg_write_content_default (xml, "rels",
			"application/vnd.openxmlformats-package.relationships+xml");
		gsf_open_pkg_write_content_default (xml, "xlbin",
			"application/vnd.openxmlformats-officedocument.spreadsheetml.printerSettings");
		gsf_open_pkg_write_content_default (xml, "xml",
			"application/xml");
		gsf_open_pkg_write_content_override (open_pkg, "/", xml);
		gsf_xml_out_end_element (xml); /* </Types> */
		g_object_unref (xml);

		gsf_output_close (out);
		g_object_unref (out);

		dir = open_pkg->sink;
		rels_name = g_strdup (".rels");
	} else {
		res = gsf_output_close (open_pkg->sink);

		dir = (GsfOutput *)gsf_output_container (open_pkg->sink);
		rels_name = g_strconcat (gsf_output_name (output), ".rels", NULL);
	}

	if (NULL != open_pkg->relations) {
		GsfOutput *rels;
		GsfXMLOut *xml;
		GsfOutfileOpenPkgRel *rel;
		GSList *ptr;

		dir = gsf_outfile_new_child (GSF_OUTFILE (dir), "_rels", TRUE);
		rels = gsf_outfile_new_child (GSF_OUTFILE (dir), rels_name, FALSE);
		xml = gsf_xml_out_new (rels);

		gsf_xml_out_start_element (xml, "Relationships");
		gsf_xml_out_add_cstr_unchecked (xml, "xmlns",
			"http://schemas.openxmlformats.org/package/2006/relationships");

		for (ptr = open_pkg->relations ; ptr != NULL ; ptr = ptr->next) {
			rel = ptr->data;
			gsf_xml_out_start_element (xml, "Relationship");
			gsf_xml_out_add_cstr (xml, "Id", rel->id);
			gsf_xml_out_add_cstr (xml, "Type", rel->type);
			gsf_xml_out_add_cstr (xml, "Target", rel->target);
			gsf_xml_out_end_element (xml); /* </Relationship> */

			g_free (rel->id);
			g_free (rel->type);
			g_free (rel->target);
			g_free (rel);
		}
		g_slist_free (open_pkg->relations);

		gsf_xml_out_end_element (xml); /* </Relationships> */
		g_object_unref (xml);
		gsf_output_close (rels);
		g_object_unref (rels);
		g_object_unref (dir);
	}
	g_free (rels_name);

	/* close the container */
	if (NULL == gsf_output_name (output))
		return gsf_output_close (open_pkg->sink);
	return res;
}

static void
gsf_outfile_open_pkg_class_init (GObjectClass *gobject_class)
{
	GsfOutputClass  *output_class  = GSF_OUTPUT_CLASS (gobject_class);
	GsfOutfileClass *outfile_class = GSF_OUTFILE_CLASS (gobject_class);

	gobject_class->finalize		= gsf_outfile_open_pkg_finalize;
	gobject_class->get_property     = gsf_outfile_open_pkg_get_property;
	gobject_class->set_property     = gsf_outfile_open_pkg_set_property;

	output_class->Write		= gsf_outfile_open_pkg_write;
	output_class->Seek		= gsf_outfile_open_pkg_seek;
	output_class->Close		= gsf_outfile_open_pkg_close;
	outfile_class->new_child	= gsf_outfile_open_pkg_new_child;

	parent_class = g_type_class_peek_parent (gobject_class);

	g_object_class_install_property (gobject_class, PROP_SINK,
		 g_param_spec_object ("sink", "Sink", "The GsfOutput that stores the Open Package content.",
			GSF_OUTFILE_TYPE, GSF_PARAM_STATIC | G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (gobject_class, PROP_CONTENT_TYPE,
		 g_param_spec_string ("content-type", "ContentType", "The ContentType stored in the root [Content_Types].xml file.",
			"", GSF_PARAM_STATIC | G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (gobject_class, PROP_IS_DIR,
		 g_param_spec_boolean ("is-dir", "IsDir", "Can the outfile have children.",
			FALSE, GSF_PARAM_STATIC | G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

GSF_CLASS (GsfOutfileOpenPkg, gsf_outfile_open_pkg,
	   gsf_outfile_open_pkg_class_init, gsf_outfile_open_pkg_init,
	   GSF_OUTFILE_TYPE)

/**
 * gsf_outfile_open_pkg_new :
 * @sink : #GsfOutfile
 *
 * Convenience routine to create a GsfOutfileOpenPkg inside @sink.
 * Returns a GsfOutfile that the caller is responsible for.
 **/
static GsfOutfile *
gsf_outfile_open_pkg_new (GsfOutfile *sink)
{
	return g_object_new (GSF_OUTFILE_OPEN_PKG_TYPE,
		"sink", sink,	"is-dir", TRUE,
		NULL);
}

/**
 * gsf_outfile_open_pkg_relate:
 * @child : #GsfOutfileOpenPkg
 * @parent : #GsfOutfileOpenPkg
 * @type : 
 *
 * Create a relationship between @child and @parent of @type.
 * Return the relID which the caller does not owm but will live as long as
 * @parent.
 **/
static char const *
gsf_outfile_open_pkg_relate (GsfOutfileOpenPkg *child,
			     GsfOutfileOpenPkg *parent,
			     char const *type)
{
	GsfOutfileOpenPkgRel *rel = g_new (GsfOutfileOpenPkgRel, 1);
	char *tmp, *path = g_strdup (gsf_output_name (GSF_OUTPUT (child)));
	GsfOutfile *ptr, *base = parent->is_dir ? GSF_OUTFILE (parent)
		: gsf_output_container (GSF_OUTPUT (parent));

	ptr = GSF_OUTFILE (child);
	while (NULL != (ptr = gsf_output_container (GSF_OUTPUT (ptr))) &&
	       ptr != base) {
		path = g_strconcat (gsf_output_name (GSF_OUTPUT (ptr)), "/",
			(tmp = path), NULL);
		g_free (tmp);
	}

	/* Calculate the path from @child to @parent */
	rel->id = g_strdup_printf ("rId%u", g_slist_length (parent->relations) + 1);
	rel->type = g_strdup (type);
	rel->target = path;
	parent->relations = g_slist_prepend (parent->relations, rel);
	return rel->id;
}

/**
 * gsf_outfile_open_pkg_add_rel:
 * @dir : #GsfOutfile
 * @name : 
 * @content_type :
 * @parent : #GsfOutfile
 * @type :
 *
 * A convenience wrapper to create a child in @dir of @content_type then create
 * a @type relation to @parent
 * Returns the new part.
 **/
static GsfOutput *
gsf_outfile_open_pkg_add_rel (GsfOutfile *dir,
			      char const *name,
			      char const *content_type,
			      GsfOutfile *parent,
			      char const *type)
{
	GsfOutput *part = gsf_outfile_new_child_full (dir, name, FALSE,
		"content-type", content_type,
		NULL);
	(void) gsf_outfile_open_pkg_relate (GSF_OUTFILE_OPEN_PKG (part),
		GSF_OUTFILE_OPEN_PKG (parent), type);
	return part;
}

/**************************************************************************/

static char const *ns_ss  = "http://schemas.openxmlformats.org/spreadsheetml/2006/main";
static char const *ns_rel = "http://schemas.openxmlformats.org/officeDocument/2006/relationships";

typedef struct {
	XLExportBase base;

	Sheet const 	   *sheet;
	GHashTable	   *shared_string_hash;
	GPtrArray	   *shared_string_array;
	GnmExprConventions *expr_conv;
	IOContext	   *io_context;
} XLSXWriteState;

static void
xlsx_add_bool (GsfXMLOut *xml, char const *id, gboolean val)
{
	gsf_xml_out_add_cstr_unchecked (xml, id, val ? "1" : "0");
}
static void
xlsx_add_rgb (GsfXMLOut *xml, char const *id, GOColor c)
{
	char buf [3 * 4 * sizeof (unsigned int) + 1];
	sprintf (buf, "%02X%02X%02X%02X",
		 UINT_RGBA_A (c), UINT_RGBA_R (c),
		 UINT_RGBA_G (c), UINT_RGBA_B (c));
	gsf_xml_out_add_cstr_unchecked (xml, id, buf);
}
static void
xlsx_add_pos (GsfXMLOut *xml, char const *id, GnmCellPos const *pos)
{
	gsf_xml_out_add_cstr_unchecked (xml, id,
		cellpos_as_string (pos));
}
static void
xlsx_add_range (GsfXMLOut *xml, char const *id, GnmRange const *range)
{
	gsf_xml_out_add_cstr_unchecked (xml, id,
		range_as_string (range));
}
static void
xlsx_add_range_list (GsfXMLOut *xml, char const *id, GSList const *ranges)
{
	GString *accum = g_string_new (NULL);

	for (; NULL != ranges ; ranges = ranges->next) {
		g_string_append (accum, range_as_string (ranges->data));
		if (NULL != ranges->next)
			g_string_append_c (accum, ' ');
	}

	gsf_xml_out_add_cstr_unchecked (xml, id, accum->str);
	g_string_free (accum, TRUE);
}

static void
xlsx_write_shared_strings (XLSXWriteState *state, GsfOutfile *dir, GsfOutfile *wb_part)
{
	if (state->shared_string_array->len > 0) {
		unsigned i;
		GnmString const *str;
		GsfOutput *part = gsf_outfile_open_pkg_add_rel (dir, "sharedStrings.xml",
			"application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml",
			wb_part,
			"http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings");
		GsfXMLOut *xml = gsf_xml_out_new (part);

		gsf_xml_out_start_element (xml, "sst");
		gsf_xml_out_add_cstr_unchecked (xml, "xmlns", ns_ss);
		gsf_xml_out_add_cstr_unchecked (xml, "xml:space", "preserve");
		gsf_xml_out_add_int (xml, "uniqueCount", state->shared_string_array->len);
		gsf_xml_out_add_int (xml, "count", state->shared_string_array->len);

		for (i = 0 ; i < state->shared_string_array->len ; i++) {
			gsf_xml_out_start_element (xml, "si");
			gsf_xml_out_start_element (xml, "t");
			str = g_ptr_array_index (state->shared_string_array, i);
			gsf_xml_out_add_cstr (xml, NULL, str->str);
			gsf_xml_out_end_element (xml); /* </t> */
			gsf_xml_out_end_element (xml); /* </si> */
		}

		gsf_xml_out_end_element (xml); /* </sst> */

		g_object_unref (xml);
		gsf_output_close (part);
		g_object_unref (part);
	}
}

static void
xlsx_write_fonts (XLSXWriteState *state, GsfXMLOut *xml)
{
}
static void
xlsx_write_fills (XLSXWriteState *state, GsfXMLOut *xml)
{
}
static void
xlsx_write_borders (XLSXWriteState *state, GsfXMLOut *xml)
{
}

/*
cellStyleXfs
cellXfs
cellStyles
dxfs
tableStyles
colors
*/

static void
xlsx_write_styles (XLSXWriteState *state, GsfOutfile *dir, GsfOutfile *wb_part)
{
	GsfOutput *part = gsf_outfile_open_pkg_add_rel (dir, "styles.xml",
		"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml",
		wb_part,
		"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles");
	GsfXMLOut *xml = gsf_xml_out_new (part);

	gsf_xml_out_start_element (xml, "styleSheet");
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns", ns_ss);
	gsf_xml_out_add_cstr_unchecked (xml, "xml:space", "preserve");

	xlsx_write_fonts (state, xml);
	xlsx_write_fills (state, xml);
	xlsx_write_borders (state, xml);

	gsf_xml_out_end_element (xml); /* </styleSheet> */

	g_object_unref (xml);
	gsf_output_close (part);
	g_object_unref (part);
}

#define XLSX_MAX_COLS	SHEET_MAX_COLS	/* (2^14) */
#define XLSX_MAX_ROWS	SHEET_MAX_ROWS	/* (2^20) */

static void
xlsx_write_sheet_view (GsfXMLOut *xml, SheetView const *sv)
{
	Sheet const *sheet = sv_sheet (sv);
	GnmColor *sheet_auto   = sheet_style_get_auto_pattern_color (sheet);
	GnmColor *default_auto = style_color_auto_pattern ();
	GnmCellPos topLeft, frozen_topLeft;
	char const *activePane = NULL;
	int const frozen_height = sv->unfrozen_top_left.row -
		sv->frozen_top_left.row;
	int const frozen_width = sv->unfrozen_top_left.col -
		sv->frozen_top_left.col;
	int tmp;

	if (frozen_width > 0) {
		topLeft.col		= sv->frozen_top_left.col;
		frozen_topLeft.col	= sv->initial_top_left.col;
	} else {
		topLeft.col		= sv->initial_top_left.col;
		frozen_topLeft.col	= sv->frozen_top_left.col;
	}
	if (frozen_height > 0) {
		topLeft.row		= sv->frozen_top_left.row;
		frozen_topLeft.row	= sv->initial_top_left.row;
	} else {
		topLeft.row		= sv->initial_top_left.row;
		frozen_topLeft.row	= sv->frozen_top_left.row;
	}

	gsf_xml_out_start_element (xml, "sheetView");
	if (topLeft.col > 0 || topLeft.row > 0) /* A1 is the default */
		xlsx_add_pos (xml, "topLeftCell", &topLeft);
	gsf_xml_out_add_int (xml, "workbookViewId",
		wb_view_get_index_in_wb (sv_wbv (sv)));

	tmp = (int) (100.* sheet->last_zoom_factor_used + .5);
	if (tmp != 100)
		gsf_xml_out_add_int (xml, "zoomScale", tmp);

	switch (sv->view_mode) {
	case GNM_SHEET_VIEW_NORMAL_MODE : break;
	case GNM_SHEET_VIEW_PAGE_BREAK_MODE :
		gsf_xml_out_add_cstr_unchecked (xml, "view", "pageBreakPreview"); break;
	case GNM_SHEET_VIEW_LAYOUT_MODE :
		gsf_xml_out_add_cstr_unchecked (xml, "view", "pageLayout"); break;
	}

	if (sheet->hide_grid)
		gsf_xml_out_add_cstr_unchecked (xml, "showGridLines", "0");
	if (sheet->display_formulas)
		gsf_xml_out_add_cstr_unchecked (xml, "showFormulas", "1");
	if (sheet->hide_col_header || sheet->hide_row_header)
		gsf_xml_out_add_cstr_unchecked (xml, "showRowColHeaders", "0");
	if (sheet->hide_zero)
		gsf_xml_out_add_cstr_unchecked (xml, "showZeros", "0");
	if (!sheet->display_outlines)
		gsf_xml_out_add_cstr_unchecked (xml, "showOutlineSymbols", "0");
	if (sheet->text_is_rtl)
		gsf_xml_out_add_cstr_unchecked (xml, "rightToLeft", "1");
	if (sheet == wb_view_cur_sheet (sv_wbv (sv)))
		gsf_xml_out_add_cstr_unchecked (xml, "tabSelected", "1");

	if (!style_color_equal (sheet_auto, default_auto)) {
		gsf_xml_out_add_cstr_unchecked (xml, "defaultGridColor", "1");
#if 0
		gsf_xml_out_add_int (xml, "colorId", grid_color_index);
#endif
	}
	style_color_unref (sheet_auto);
	style_color_unref (default_auto);

	if (sv_is_frozen (sv)) {
		activePane = "bottomRight"; /* h&v freeze */

		gsf_xml_out_start_element (xml, "pane");
		if (frozen_width > 0)
			gsf_xml_out_add_int (xml, "xSplit", frozen_width);
		else
			activePane = "bottomLeft"; /* v freeze */
		if (frozen_height > 0)
			gsf_xml_out_add_int (xml, "ySplit", frozen_height);
		else
			activePane = "topRight"; /* h freeze */
		xlsx_add_pos (xml, "topLeftCell", &frozen_topLeft);
		gsf_xml_out_add_cstr_unchecked (xml, "activePane", activePane);
		gsf_xml_out_add_cstr_unchecked (xml, "state", "frozen");
		gsf_xml_out_end_element (xml); /* </pane> */
	}

	gsf_xml_out_start_element (xml, "selection");
	if (NULL != activePane)
		gsf_xml_out_add_cstr_unchecked (xml, "pane", activePane);
	/* activeCellId is always 0 for gnumeric */
	xlsx_add_pos (xml, "activeCell", &sv->edit_pos);
	xlsx_add_range_list (xml, "sqref", sv->selections);
	gsf_xml_out_end_element (xml); /* </selection> */

	gsf_xml_out_end_element (xml); /* </sheetView> */
}

static void
xlsx_write_cells (XLSXWriteState *state, GsfXMLOut *xml, GnmRange const *extent)
{
	int r, c;
	char const *type;
	char *content;
	int   str_id = -1;
	GnmParsePos pp;
	GnmCell const *cell;
	GnmExprTop const *texpr;
	GnmExprArrayCorner const *array;
	ColRowInfo const *ri;
	GnmValue const *val;
	gpointer tmp;
	char *cheesy_span = g_strdup_printf ("%d:%d", extent->start.col+1, extent->end.col+1);

	gsf_xml_out_start_element (xml, "sheetData");
	for (r = extent->start.row ; r <= extent->end.row ; r++) {
		gsf_xml_out_start_element (xml, "row");
		gsf_xml_out_add_int (xml, "r", r+1);
		gsf_xml_out_add_cstr_unchecked (xml, "spans", cheesy_span);;

		if (NULL != (ri = sheet_row_get (state->sheet, r))) {
			if (ri->hard_size) {
				gsf_xml_out_add_float (xml, "ht", ri->size_pts, 4);
				gsf_xml_out_add_cstr_unchecked (xml, "customHeight", "1");
			}
			if (ri->is_collapsed)
				gsf_xml_out_add_cstr_unchecked (xml, "collapsed", "1");
			if (!ri->visible)
				gsf_xml_out_add_cstr_unchecked (xml, "hidden", "1");
			if (ri->outline_level > 0)
				gsf_xml_out_add_int (xml, "outlineLevel", ri->outline_level);
		}

		for (c = extent->start.col ; c <= extent->end.col ; c++) {
			if (NULL != (cell = sheet_cell_get (state->sheet, c, r))) {
				val = cell->value;
				gsf_xml_out_start_element (xml, "c");
				gsf_xml_out_add_cstr_unchecked (xml, "r",
					cell_coord_name (c, r));

				switch (val->type) {
				default :
				case VALUE_EMPTY :	type = NULL; break; /* FIXME : what to do ? */
				case VALUE_BOOLEAN :	type = "b"; break;
				case VALUE_FLOAT :	type = ""; break; /* "n" is the default */
				case VALUE_ERROR :	type = "e"; break;
				case VALUE_STRING :
					if (val->v_str.val->ref_count > 1) {
						if (NULL == (tmp = g_hash_table_lookup (state->shared_string_hash, val->v_str.val))) {
							tmp = GINT_TO_POINTER (state->shared_string_array->len);
							g_ptr_array_add (state->shared_string_array, val->v_str.val);
							g_hash_table_insert (state->shared_string_hash, val->v_str.val, tmp);
						}
						str_id = GPOINTER_TO_INT (tmp);
						type = "s";
					} else
						type = "str";
					break;
				case VALUE_CELLRANGE :
				case VALUE_ARRAY :	type = NULL; break;	/* FIXME */
				}

				if (NULL != type && *type)
					gsf_xml_out_add_cstr_unchecked (xml, "t", type);

				if (gnm_cell_has_expr (cell)) {
					texpr = cell->base.texpr;
					if (!gnm_expr_top_is_array_elem (texpr)) {
						gsf_xml_out_start_element (xml, "f");

						array = gnm_expr_top_get_array_corner (texpr);
						if (NULL != array) {
							GnmRange r;
							range_init_cellpos_size (&r, &cell->pos,
								array->cols, array->rows);
							gsf_xml_out_add_cstr_unchecked (xml, "t", "array");
							xlsx_add_range (xml, "ref", &r);
						}
						content = gnm_expr_top_as_string (cell->base.texpr,
							parse_pos_init_cell (&pp, cell), state->expr_conv);
						gsf_xml_out_add_cstr (xml, NULL, content);
						g_free (content);

						gsf_xml_out_end_element (xml); /* </f> */
					}
				}
				if (NULL != type) {
					gsf_xml_out_start_element (xml, "v");
					if (str_id >= 0) {
						gsf_xml_out_add_int (xml, NULL, str_id);
						str_id = -1;
					} else if (val->type != VALUE_BOOLEAN) {
						content = value_get_as_string (cell->value);
						gsf_xml_out_add_cstr_unchecked (xml, NULL, content);
						g_free (content);
					} else
						xlsx_add_bool (xml, NULL, val->v_bool.val);
					gsf_xml_out_end_element (xml); /* </v> */
				}

				gsf_xml_out_end_element (xml); /* </c> */
			}
		}
		gsf_xml_out_end_element (xml); /* </row> */
	}
	gsf_xml_out_end_element (xml); /* </sheetData> */
	g_free (cheesy_span);;
}

static gboolean
xlsx_write_col (XLSXWriteState *state, GsfXMLOut *xml,
		ColRowInfo const *ci, int first, int last, gboolean has_child)
{
	float const def_width = state->sheet->cols.default_style.size_pts;

	if (NULL == ci)
		return has_child;

	if (!has_child)
		gsf_xml_out_start_element (xml, "cols");

	gsf_xml_out_start_element (xml, "col");
	gsf_xml_out_add_int (xml, "min", first+1) ;
	gsf_xml_out_add_int (xml, "max", last+1) ;

	gsf_xml_out_add_float (xml, "width",
		ci->size_pts / ((130. / 18.5703125) * (72./96.)), 7);
	if (!ci->visible)
		gsf_xml_out_add_cstr_unchecked (xml, "hidden", "1");
	if (ci->hard_size)
		gsf_xml_out_add_cstr_unchecked (xml, "customWidth", "1");
	else if (fabs (def_width - ci->size_pts) > .1) {
		gsf_xml_out_add_cstr_unchecked (xml, "bestFit", "1");
		gsf_xml_out_add_cstr_unchecked (xml, "customWidth", "1");
	}

	if (ci->outline_level > 0)
		gsf_xml_out_add_int (xml, "outlineLevel", ci->outline_level);
	if (ci->is_collapsed)
		gsf_xml_out_add_cstr_unchecked (xml, "collapsed", "1");
	gsf_xml_out_end_element (xml); /* </col> */

	return TRUE;
}

static void
xlsx_write_cols (XLSXWriteState *state, GsfXMLOut *xml, GnmRange const *extent)
{
	ColRowInfo const *ci, *info;
	gboolean has_child = FALSE;
	int first_col = 0, i;

	info = sheet_col_get (state->sheet, first_col);
	while (info == NULL && first_col <= extent->end.col)
		info = sheet_col_get (state->sheet, ++first_col);
	if (info == NULL)
		return;

	for (i = first_col + 1 ; i <= extent->end.col ; i++) {
		ci = sheet_col_get (state->sheet, i);
		if (!colrow_equal (info, ci)) {
			has_child |= xlsx_write_col (state, xml, info, first_col, i-1, has_child);
			info	  = ci;
			first_col = i;
		}
	}
	has_child |= xlsx_write_col (state, xml, info, first_col, i-1, has_child);

	if (has_child)
		gsf_xml_out_end_element (xml); /* </cols> */
}

static void
xlsx_write_autofilters (XLSXWriteState *state, GsfXMLOut *xml)
{
	GnmFilter const *filter;
	GnmFilterCondition const *cond;
	unsigned i;

	if (NULL == state->sheet->filters)
		return;

	filter = state->sheet->filters->data;
	gsf_xml_out_start_element (xml, "autoFilter");
	xlsx_add_range (xml, "ref", &filter->r);

	for (i = 0; i < filter->fields->len ; i++) {
		/* filter unused or bucket filters in excel5 */
		if (NULL == (cond = gnm_filter_get_condition (filter, i)) ||
		    cond->op[0] == GNM_FILTER_UNUSED)
			continue;

		gsf_xml_out_start_element (xml, "filterColumn");
		gsf_xml_out_add_int (xml, "colId", i);

		switch (cond->op[0]) {
		case GNM_FILTER_OP_EQUAL :
		case GNM_FILTER_OP_GT :
		case GNM_FILTER_OP_LT :
		case GNM_FILTER_OP_GTE :
		case GNM_FILTER_OP_LTE :
		case GNM_FILTER_OP_NOT_EQUAL :
			break;

		case GNM_FILTER_OP_BLANKS :
		case GNM_FILTER_OP_NON_BLANKS :
			break;

		case GNM_FILTER_OP_TOP_N :
		case GNM_FILTER_OP_BOTTOM_N :
		case GNM_FILTER_OP_TOP_N_PERCENT :
		case GNM_FILTER_OP_BOTTOM_N_PERCENT :
			gsf_xml_out_start_element (xml, "top10");
			gsf_xml_out_add_float (xml, "val", cond->count, -1);
			if (cond->op[0] & GNM_FILTER_OP_BOTTOM_MASK)
				gsf_xml_out_add_cstr_unchecked (xml, "top", "0");
			if (cond->op[0] & GNM_FILTER_OP_PERCENT_MASK)
				gsf_xml_out_add_cstr_unchecked (xml, "percent", "1");
			gsf_xml_out_end_element (xml); /* </top10> */
			break;

		default :
			continue;
		}

		gsf_xml_out_end_element (xml); /* </filterColumn> */
	}
	gsf_xml_out_end_element (xml); /* </autoFilter> */
}

static void
xlsx_write_print_info (XLSXWriteState *state, GsfXMLOut *xml)
{
	PrintInformation const *pi = state->sheet->print_info;
	double h_margin, f_margin;
	double left;
	double right;

	g_return_if_fail (pi != NULL);

	gsf_xml_out_start_element (xml, "printOptions");
	gsf_xml_out_end_element (xml); /* </printOptions> */

	gsf_xml_out_start_element (xml, "pageMargins");
	print_info_get_margins (pi, &h_margin, &f_margin, &left, &right);
	gsf_xml_out_add_float (xml, "left",	left / 72., 4);
	gsf_xml_out_add_float (xml, "right",	right / 72., 4);
	gsf_xml_out_add_float (xml, "top",	pi->margin.top.points / 72., 4);
	gsf_xml_out_add_float (xml, "bottom",	pi->margin.bottom.points / 72., 4);
	gsf_xml_out_add_float (xml, "header",	h_margin / 72., 4);
	gsf_xml_out_add_float (xml, "footer",	f_margin / 72., 4);
	gsf_xml_out_end_element (xml); /* </pageMargins> */

	gsf_xml_out_start_element (xml, "pageSetup");
	gsf_xml_out_end_element (xml); /* </pageSetup> */
	gsf_xml_out_start_element (xml, "headerFooter");
	gsf_xml_out_end_element (xml); /* </headerFooter> */

}

static char const *
xlsx_write_sheet (XLSXWriteState *state, GsfOutfile *dir, GsfOutfile *wb_part, unsigned i)
{
	char *name = g_strdup_printf ("sheet%u.xml", i+1);
	GsfOutput *part = gsf_outfile_new_child_full (dir, name, FALSE,
		"content-type", "application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml",
		NULL);
	char const *rId = gsf_outfile_open_pkg_relate (GSF_OUTFILE_OPEN_PKG (part),
		GSF_OUTFILE_OPEN_PKG (wb_part),
		"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet");
	GsfXMLOut *xml = gsf_xml_out_new (part);
	GnmRange  extent;
	GnmStyle *col_styles [MIN (XLSX_MAX_COLS, SHEET_MAX_COLS)];
	GSList *ptr;

	state->sheet = workbook_sheet_by_index (state->base.wb, i);
	excel_sheet_extent (state->sheet, &extent, col_styles,
		MIN (XLSX_MAX_COLS, SHEET_MAX_COLS),
		MIN (XLSX_MAX_ROWS, SHEET_MAX_ROWS), state->io_context);

	gsf_xml_out_start_element (xml, "worksheet");
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns", ns_ss);
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns:r", ns_rel);

	/* for now we only use tabColor, move sheetPr outside when we add more
	 * features */
	if (NULL != state->sheet->tab_color) {
		gsf_xml_out_start_element (xml, "sheetPr");

		gsf_xml_out_start_element (xml, "tabColor");
		xlsx_add_rgb (xml, "rgb", state->sheet->tab_color->go_color);
		gsf_xml_out_end_element (xml); /* </tabColor> */

		gsf_xml_out_end_element (xml); /* </sheetPr> */
	}

	gsf_xml_out_start_element (xml, "dimension");
	xlsx_add_range (xml, "ref", &extent);
	gsf_xml_out_end_element (xml); /* </dimension> */

	gsf_xml_out_start_element (xml, "sheetViews");
	SHEET_FOREACH_VIEW (state->sheet, sv, xlsx_write_sheet_view (xml, sv););
	gsf_xml_out_end_element (xml); /* </sheetViews> */

	gsf_xml_out_start_element (xml, "sheetFormatPr");
	gsf_xml_out_add_float (xml, "defaultRowHeight",
		sheet_row_get_default_size_pts (state->sheet), 4);
	if (state->sheet->rows.max_outline_level > 0)
		gsf_xml_out_add_int (xml, "outlineLevelRow", 
			state->sheet->rows.max_outline_level);
	if (state->sheet->cols.max_outline_level > 0)
		gsf_xml_out_add_int (xml, "outlineLevelCol",
			state->sheet->cols.max_outline_level);
	gsf_xml_out_end_element (xml); /* </sheetFormatPr> */

	xlsx_write_cols (state, xml, &extent);

	xlsx_write_cells (state, xml, &extent);

	gsf_xml_out_start_element (xml, "sheetProtection");
	gsf_xml_out_end_element (xml); /* </sheetProtection> */

	if (NULL != (ptr = state->sheet->list_merged)) {
		gsf_xml_out_start_element (xml, "mergeCells");
		for (; ptr != NULL ; ptr = ptr->next) {
			gsf_xml_out_start_element (xml, "mergeCell");
			xlsx_add_range (xml, "ref", ptr->data);
			gsf_xml_out_end_element (xml); /* </mergeCell> */
		}
		gsf_xml_out_end_element (xml); /* </mergeCells> */
	}

	xlsx_write_autofilters (state, xml);
	xlsx_write_print_info (state, xml);
	gsf_xml_out_end_element (xml); /* </worksheet> */

	state->sheet = NULL;
	g_object_unref (xml);
	gsf_output_close (part);
	g_object_unref (part);
	g_free (name);

	return rId;
}

static void
xlsx_write_workbook (XLSXWriteState *state, GsfOutfile *root_part)
{
	int i;
	GsfXMLOut  *xml;
	GPtrArray  *sheetIds  = g_ptr_array_new ();
	GsfOutfile *xl_dir    = (GsfOutfile *)gsf_outfile_new_child (root_part, "xl", TRUE);
	GsfOutfile *sheet_dir = (GsfOutfile *)gsf_outfile_new_child (xl_dir, "worksheets", TRUE);
	GsfOutfile *wb_part   = (GsfOutfile *)gsf_outfile_open_pkg_add_rel (xl_dir, "workbook.xml",
		"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml",
		root_part,
		"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument");

	state->shared_string_hash = g_hash_table_new (g_direct_hash, g_direct_equal);
	state->shared_string_array = g_ptr_array_new ();
	state->expr_conv	 = xlsx_expr_conv_new ();

	g_ptr_array_set_size (sheetIds, workbook_sheet_count (state->base.wb));
	for (i = 0 ; i < workbook_sheet_count (state->base.wb); i++)
		g_ptr_array_index (sheetIds, i) =
			(gpointer) xlsx_write_sheet (state, sheet_dir, wb_part, i);

	xlsx_write_shared_strings (state, xl_dir, wb_part);
	xlsx_write_styles (state, xl_dir, wb_part);

	xml = gsf_xml_out_new (GSF_OUTPUT (wb_part));
	gsf_xml_out_start_element (xml, "workbook");
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns", ns_ss);
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns:r", ns_rel);
	gsf_xml_out_add_cstr_unchecked (xml, "xml:space", "preserve");

	gsf_xml_out_start_element (xml, "fileVersion");
	gsf_xml_out_add_int (xml, "lastEdited", 4);
	gsf_xml_out_add_int (xml, "lowestEdited", 4);
	gsf_xml_out_add_int (xml, "rupBuild", 3820);
	gsf_xml_out_end_element (xml);

	gsf_xml_out_simple_element (xml, "workbookPr", NULL);

	gsf_xml_out_start_element (xml, "bookViews");
	WORKBOOK_FOREACH_VIEW (state->base.wb, view, {
		gsf_xml_out_start_element (xml, "workbookView");
		gsf_xml_out_add_int (xml, "activeTab",
			view->current_sheet->index_in_wb);
		gsf_xml_out_end_element (xml);
	});
	gsf_xml_out_end_element (xml);

	gsf_xml_out_start_element (xml, "sheets");
	for (i = 0 ; i < workbook_sheet_count (state->base.wb); i++) {
		Sheet const *sheet = workbook_sheet_by_index (state->base.wb, i);
		gsf_xml_out_start_element (xml, "sheet");
		gsf_xml_out_add_cstr (xml, "name", sheet->name_unquoted);
		gsf_xml_out_add_int (xml, "sheetId", i+1);	/* FIXME What is this ?? */
		gsf_xml_out_add_cstr_unchecked (xml, "r:id",
			g_ptr_array_index (sheetIds, i));
		gsf_xml_out_end_element (xml); /* </sheet> */
	}
	gsf_xml_out_end_element (xml); /* </sheets> */

	gsf_xml_out_start_element (xml, "webPublishing");
	gsf_xml_out_add_int (xml, "codePage", 1252);	/* FIXME : Use utf-8 ? */
	gsf_xml_out_end_element (xml);

	gsf_xml_out_end_element (xml); /* </workbook> */
	g_object_unref (xml);

	xlsx_expr_conv_free (state->expr_conv);
	g_hash_table_destroy (state->shared_string_hash);
	g_ptr_array_free (state->shared_string_array, TRUE);

	gsf_output_close (GSF_OUTPUT (wb_part));
	g_ptr_array_free (sheetIds, TRUE);
}

G_MODULE_EXPORT void
xlsx_file_save (GOFileSaver const *fs, IOContext *io_context,
		gconstpointer wb_view, GsfOutput *output);
void
xlsx_file_save (GOFileSaver const *fs, IOContext *io_context,
		gconstpointer wb_view, GsfOutput *output)
{
	XLSXWriteState state;
	GsfOutfile *root_part;
	GnmLocale  *locale;

	locale = gnm_push_C_locale ();

	state.io_context	= io_context;
	state.base.wb	 	= wb_view_get_workbook (wb_view);
	root_part = gsf_outfile_open_pkg_new (
		gsf_outfile_zip_new (output, NULL));

	xlsx_write_workbook (&state, root_part);
	gsf_output_close (GSF_OUTPUT (root_part));
	g_object_unref (root_part);

	gnm_pop_C_locale (locale);
}

/* TODO : (Just about everything)
 * 	Figure out why XL 12 complains about cells and cols
 * 	styles
 * 	rich text
 * 	shared expressions
 * 	external refs
 * 	...
 * 	*/
