/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gog-renderer-svg.c :
 *
 * Copyright (C) 2003 Jody Goldberg (jody@gnome.org)
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include <gnumeric-config.h>
#include <goffice/graph/gog-renderer-svg.h>
#include <goffice/graph/gog-renderer-impl.h>
#include <goffice/graph/gog-style.h>
#include <goffice/graph/gog-view.h>
#include <goffice/utils/go-color.h>
#include <goffice/utils/go-units.h>

#include <gsf/gsf-libxml.h>
#include <gsf/gsf-impl-utils.h>

#include <libxml/tree.h>

#include <locale.h>

#define CC2XML(s) ((const xmlChar *)(s))

#define GOG_RENDERER_SVG_TYPE	(gog_renderer_svg_get_type ())
#define GOG_RENDERER_SVG(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), GOG_RENDERER_SVG_TYPE, GogRendererSvg))
#define IS_GOG_RENDERER_SVG(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GOG_RENDERER_SVG_TYPE))

typedef struct _GogRendererSvg GogRendererSvg;

struct _GogRendererSvg {
	GogRenderer base;

	xmlDocPtr doc;
};

typedef GogRendererClass GogRendererSvgClass;

static GObjectClass *parent_klass;

static GType gog_renderer_svg_get_type (void);

static void
draw_path (GogRendererSvg *prend, ArtVpath *path, GString *string)
{
	for ( ; path->code != ART_END ; path++)
		switch (path->code) {
		case ART_MOVETO :
			g_string_append_printf (string, "M%g %g", path->x, path->y);
			break;
		case ART_LINETO :
			g_string_append_printf (string, "L%g %g", path->x, path->y);
			break;
		default :
			break;
		}
}

static void
gog_renderer_svg_draw_path (GogRenderer *renderer, ArtVpath *path)
{
	GogRendererSvg *prend = GOG_RENDERER_SVG (renderer);
	GogStyle const *style = renderer->cur_style;
	xmlNodePtr node = xmlNewDocNode (prend->doc, NULL, "path", NULL);
	GString *string;
	char *buf;
	int opacity;

	xmlAddChild (prend->doc->children, node);
	string = g_string_new ("");
	draw_path (prend, path, string);
	xmlNewProp (node, CC2XML ("d"), CC2XML (string->str));
	g_string_free (string, TRUE);
	xmlNewProp (node, CC2XML ("fill"), CC2XML ("none"));
	buf = g_strdup_printf ("%g",  gog_renderer_line_size (renderer, style->line.width));
	xmlNewProp (node, CC2XML ("stroke-width"), CC2XML (buf));
	g_free (buf);
	buf = g_strdup_printf ("#%06x", style->line.color >> 8);
	xmlNewProp (node, CC2XML ("stroke"), CC2XML (buf));
	g_free (buf);
	opacity = style->line.color & 0xff;
	if (opacity != 255) {
		buf = g_strdup_printf ("%g", (double) opacity / 255.);
		xmlNewProp (node, CC2XML ("stroke-opacity"), CC2XML (buf));
		g_free (buf);
	}
}

static void
gog_renderer_svg_draw_polygon (GogRenderer *renderer, ArtVpath *path, gboolean narrow)
{
	GogRendererSvg *prend = GOG_RENDERER_SVG (renderer);
	GogStyle const *style = renderer->cur_style;
	gboolean with_outline = (!narrow && style->outline.width >= 0.);
	xmlNodePtr node;
	char *buf;
	int opacity;

	if (style->fill.type != GOG_FILL_STYLE_NONE || with_outline) {
		GString *string = g_string_new ("");
		node = xmlNewDocNode (prend->doc, NULL, "path", NULL);
		xmlAddChild (prend->doc->children, node);
		draw_path (prend, path, string);
		g_string_append (string, "z");
		xmlNewProp (node, CC2XML ("d"), CC2XML (string->str));
		g_string_free (string, TRUE);
	} else
		return;

	if (style->fill.type != GOG_FILL_STYLE_NONE) {

		switch (style->fill.type) {
		case GOG_FILL_STYLE_PATTERN:
			switch (style->fill.u.pattern.pat.pattern) {
				case GO_PATTERN_SOLID:
					buf = g_strdup_printf ("#%06x", style->fill.u.pattern.pat.back >> 8);
					xmlNewProp (node, CC2XML ("fill"), CC2XML (buf));
					g_free (buf);
					opacity = style->fill.u.pattern.pat.back & 0xff;
					if (opacity != 255) {
						buf = g_strdup_printf ("%g", (double) opacity / 255.);
						xmlNewProp (node, CC2XML ("fill-opacity"), CC2XML (buf));
						g_free (buf);
					}
					break;
				case GO_PATTERN_FOREGROUND_SOLID:
					buf = g_strdup_printf ("#%06x", style->fill.u.pattern.pat.fore >> 8);
					xmlNewProp (node, CC2XML ("fill"), CC2XML (buf));
					g_free (buf);
					opacity = style->fill.u.pattern.pat.fore & 0xff;
					if (opacity != 255) {
						buf = g_strdup_printf ("%g", (double) opacity / 255.);
						xmlNewProp (node, CC2XML ("fill-opacity"), CC2XML (buf));
						g_free (buf);
					}
					break;
				default:
				break;
			}
			break;

		case GOG_FILL_STYLE_GRADIENT:
			break;

		case GOG_FILL_STYLE_IMAGE:
			break;

		case GOG_FILL_STYLE_NONE:
			break; /* impossible */
		}
	}
	else
		xmlNewProp (node, CC2XML ("fill"), CC2XML ("none"));

	if (with_outline) {
		buf = g_strdup_printf ("%g",  gog_renderer_line_size (renderer, style->outline.width));
		xmlNewProp (node, CC2XML ("stroke-width"), CC2XML (buf));
		g_free (buf);
		buf = g_strdup_printf ("#%06x", style->outline.color >> 8);
		xmlNewProp (node, CC2XML ("stroke"), CC2XML (buf));
		g_free (buf);
		opacity = style->outline.color & 0xff;
		if (opacity != 255) {
			buf = g_strdup_printf ("%g", (double) opacity / 255.);
			xmlNewProp (node, CC2XML ("stroke-opacity"), CC2XML (buf));
			g_free (buf);
		}
	} else
		xmlNewProp (node, CC2XML ("stroke"), CC2XML ("none"));
}

static void
gog_renderer_svg_draw_text (GogRenderer *rend, ArtPoint *pos,
			    char const *text, GogViewRequisition *size)
{
#if 0
	GogRendererSvg *prend = GOG_RENDERER_SVG (rend);
#endif
}

static void
gog_renderer_svg_draw_marker (GogRenderer *rend, double x, double y)
{
#if 0
	GogRendererSvg *prend = GOG_RENDERER_SVG (rend);
#endif
}

static void
gog_renderer_svg_measure_text (GogRenderer *rend,
				       char const *text, GogViewRequisition *size)
{
#warning TODO
	size->w = 10; /* gnome_font_get_width_utf8 (font, text); */
	size->h = 10;
}

static void
gog_renderer_svg_class_init (GogRendererClass *rend_klass)
{
	parent_klass = g_type_class_peek_parent (rend_klass);
	rend_klass->draw_path	  = gog_renderer_svg_draw_path;
	rend_klass->draw_polygon  = gog_renderer_svg_draw_polygon;
	rend_klass->draw_text	  = gog_renderer_svg_draw_text;
	rend_klass->draw_marker	  = gog_renderer_svg_draw_marker;
	rend_klass->measure_text  = gog_renderer_svg_measure_text;
}

static GSF_CLASS (GogRendererSvg, gog_renderer_svg,
		  gog_renderer_svg_class_init, NULL,
		  GOG_RENDERER_TYPE)

/**
 * gog_graph_export_to_svg :
 * @graph  : #GogGraph
 * @output : #GsfOutput
 * @width  :
 * @height :
 *
 * Renders @graph as SVG and stores it in @output.
 *
 * Returns TRUE on success.
 **/
gboolean
gog_graph_export_to_svg (GogGraph *graph, GsfOutput *output,
			 double width, double height)
{
	GogViewAllocation allocation;
	GogRendererSvg *prend;
	xmlNsPtr namespace;
	gboolean success = TRUE;
	char *buf;
	char *old_num_locale = g_strdup (setlocale (LC_NUMERIC, NULL));
	setlocale (LC_NUMERIC, "C");

	prend = g_object_new (GOG_RENDERER_SVG_TYPE,
			      "model", graph,
			      NULL);
	prend->doc = xmlNewDoc (CC2XML ("1.0"));

	xmlNewDtd (prend->doc,
		   CC2XML ("svg"), CC2XML ("-//W3C//DTD SVG 1.1//EN"),
		   CC2XML ("http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd"));
	prend->doc->children = xmlNewDocNode (prend->doc, NULL, "svg", NULL);

	namespace = xmlNewNs (prend->doc->children, CC2XML ("http://www.w3.org/2000/svg"), NULL);
	xmlSetNs (prend->doc->children, namespace);
	xmlNewProp (prend->doc->children, CC2XML ("version"), CC2XML ("1.1"));

	namespace = xmlNewNs (prend->doc->children, CC2XML ("http://www.w3.org/1999/xlink"), CC2XML ("xlink"));

	buf = g_strdup_printf ("%g", width);
	xmlNewProp (prend->doc->children, CC2XML ("width"), CC2XML (buf));
	g_free (buf);
	buf = g_strdup_printf ("%g", height);
	xmlNewProp (prend->doc->children, CC2XML ("height"), CC2XML (buf));
	g_free (buf);

	allocation.x = 0.;
	allocation.y = 0.;
	allocation.w = width;
	allocation.h = height;
	gog_view_size_allocate (prend->base.view, &allocation);
	gog_view_render	(prend->base.view, NULL);

	xmlIndentTreeOutput = TRUE;
	if (gsf_xmlDocFormatDump (output, prend->doc, "UTF-8", TRUE) < 0)
		success = FALSE;

	xmlFreeDoc (prend->doc);
	setlocale (LC_NUMERIC, old_num_locale);
	g_free (old_num_locale);
	g_object_unref (prend);

	return success;
}
