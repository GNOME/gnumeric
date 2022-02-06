/*
 * xlsx-drawing-write.c : export MS Office Open xlsx drawings and charts.
 *
 * Copyright (C) 2006-2007 Jody Goldberg (jody@gnome.org)
 * Copyright (C) 2011 Jean Brefort (jean.brefort@normalesup.org)
 * Copyright (C) 2015 Morten Welinder (terra@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) version 3.
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

#undef DEBUG_AXIS

static void
xlsx_write_chart_cstr_unchecked (GsfXMLOut *xml, char const *name, char const *val)
{
	gsf_xml_out_start_element (xml, name);
	gsf_xml_out_add_cstr_unchecked (xml, "val", val);
	gsf_xml_out_end_element (xml);
}
static void
xlsx_write_chart_bool (GsfXMLOut *xml, char const *name, gboolean val)
{
	gsf_xml_out_start_element (xml, name);
	xlsx_add_bool (xml, "val", val);
	gsf_xml_out_end_element (xml);
}

static void
xlsx_write_chart_int (GsfXMLOut *xml, char const *name, int val)
{
	gsf_xml_out_start_element (xml, name);
	gsf_xml_out_add_int (xml, "val", val);
	gsf_xml_out_end_element (xml);
}

static void
xlsx_write_chart_uint (GsfXMLOut *xml, char const *name, int val)
{
	gsf_xml_out_start_element (xml, name);
	gsf_xml_out_add_uint (xml, "val", val);
	gsf_xml_out_end_element (xml);
}

static void
xlsx_write_chart_float (GsfXMLOut *xml, char const *name, double val)
{
	gsf_xml_out_start_element (xml, name);
	go_xml_out_add_double (xml, "val", val);
	gsf_xml_out_end_element (xml);
}

static void
xlsx_write_plot_1_5_type (GsfXMLOut *xml, GogPlot const *plot, gboolean is_barcol)
{
	char *type;
	const char *gtype;

	g_object_get (G_OBJECT (plot), "type", &type, NULL);
	if (0 == strcmp (type, "as_percentage"))
		gtype = "percentStacked";
	else if (0 == strcmp (type, "stacked"))
		gtype = "stacked";
	else
		gtype = is_barcol ? "clustered": "standard";
	xlsx_write_chart_cstr_unchecked (xml, "c:grouping", gtype);
	g_free (type);
}

static void
xlsx_write_series_dim (XLSXWriteState *state, GsfXMLOut *xml, GogSeries const *series,
		       char const *name, GogMSDimType ms_type)
{
	GogSeriesDesc const *desc = &gog_plot_description (gog_series_get_plot (series))->series;
	int dim;
	GOData const *dat;

	if (ms_type == GOG_MS_DIM_LABELS)
			dim = -1;
	else {
		for (dim = 0; dim < (int) desc->num_dim; dim++)
			if (desc->dim[dim].ms_type == ms_type)
				break;
		if (dim == (int) desc->num_dim)
			return;
	}
	dat = gog_dataset_get_dim (GOG_DATASET (series), dim);
	if (NULL != dat) {
		GnmExprTop const *texpr = gnm_go_data_get_expr (dat);
		if (NULL != texpr) {
			GnmParsePos pp;
			char *str = gnm_expr_top_as_string (texpr,
				parse_pos_init (&pp, (Workbook *)state->base.wb, NULL, 0,0 ),
				state->convs);
			gsf_xml_out_start_element (xml, name);
			gsf_xml_out_start_element (xml, (strcmp (name, "c:tx") && strcmp (name, "c:cat"))? "c:numRef": "c:strRef");
			gsf_xml_out_simple_element (xml, "c:f", str);
			gsf_xml_out_end_element (xml);
			/* FIXME: write values, they are mandatory, according to the schema */
			gsf_xml_out_end_element (xml);

			g_free (str);
		}
	}
}

static void
xlsx_write_rgbarea (GsfXMLOut *xml, GOColor color)
{
	char *buf = g_strdup_printf ("%06x", (guint) color >> 8);
	unsigned alpha = GO_COLOR_UINT_A (color);
	gsf_xml_out_start_element (xml, "a:srgbClr");
	gsf_xml_out_add_cstr_unchecked (xml, "val", buf);
	g_free (buf);
	if (alpha < 255) {
		gsf_xml_out_start_element (xml, "a:alpha");
		gsf_xml_out_add_uint (xml, "val", alpha * 100000u / 255u);
		gsf_xml_out_end_element (xml);
	}
	gsf_xml_out_end_element (xml);
}

static gboolean
xlsx_go_style_has_font (GOStyle *style)
{
	if (!(style->interesting_fields & GO_STYLE_FONT))
		return FALSE;

	return !style->font.auto_font;
}

static void
xlsx_write_rpr (GsfXMLOut *xml, GOStyle *style)
{
	gboolean has_font_color = ((style->interesting_fields & GO_STYLE_FONT) &&
				   !style->font.auto_color);
	gboolean has_font = xlsx_go_style_has_font (style);

	GOFont const *font = style->font.font;
	PangoFontDescription *desc = font->desc;

	if (has_font) {
		int sz = pango_font_description_get_size (desc);
		if (sz > 0) {
			sz = CLAMP (sz, 1 * PANGO_SCALE, 4000 * PANGO_SCALE);
			gsf_xml_out_add_uint (xml, "sz", sz * 100 / PANGO_SCALE);
		}

		if (pango_font_description_get_weight (desc) > PANGO_WEIGHT_NORMAL)
			xlsx_add_bool (xml, "b", TRUE);
		if (pango_font_description_get_style (desc) > PANGO_STYLE_NORMAL)
			xlsx_add_bool (xml, "i", TRUE);
	}
	if (has_font_color) {
		gsf_xml_out_start_element (xml, "a:solidFill");
		xlsx_write_rgbarea (xml, style->font.color);
		gsf_xml_out_end_element (xml);
	}
	if (has_font) {
		gsf_xml_out_start_element (xml, "a:latin");
		gsf_xml_out_add_cstr (xml, "typeface",
				      pango_font_description_get_family (desc));
		gsf_xml_out_end_element (xml);
	}
}

typedef struct {
	gboolean def_has_markers;
	gboolean def_has_lines;
	gboolean inhibit_marker;
	const char *spPr_ns;
	gboolean must_fill_line;
	gboolean must_fill_fill;
	XLSXWriteState *state;

	/* Not strictly context, but extensions to the style.  */
	const char *shapename;
	GOArrow *start_arrow;
	GOArrow *end_arrow;
	gboolean flipH;
	gboolean flipV;
} XLSXStyleContext;

static void
xlsx_style_context_init (XLSXStyleContext *sctx, XLSXWriteState *state)
{
	sctx->def_has_markers = FALSE;
	sctx->def_has_lines = TRUE;
	sctx->inhibit_marker = FALSE;
	sctx->spPr_ns = "c";
	sctx->must_fill_line = FALSE;
	sctx->must_fill_fill = FALSE;
	sctx->state = state;
	sctx->shapename = NULL;
	sctx->start_arrow = NULL;
	sctx->end_arrow = NULL;
	sctx->flipH = FALSE;
	sctx->flipV = FALSE;
}

static void
xlsx_write_go_style_marker (GsfXMLOut *xml, GOStyle *style, const XLSXStyleContext *sctx)
{
	static const char *const markers[] = {
		"none",       /* GO_MARKER_NONE */
		"square",     /* GO_MARKER_SQUARE */
		"diamond",    /* GO_MARKER_DIAMOND */
		"triangle",   /* GO_MARKER_TRIANGLE_DOWN */
		"triangle",   /* GO_MARKER_TRIANGLE_UP */
		"triangle",   /* GO_MARKER_TRIANGLE_RIGHT */
		"triangle",   /* GO_MARKER_TRIANGLE_LEFT */
		"circle",     /* GO_MARKER_CIRCLE */
		"x",          /* GO_MARKER_X */
		"plus",       /* GO_MARKER_CROSS */
		"star",       /* GO_MARKER_ASTERISK */
		"dash",       /* GO_MARKER_BAR */
		"dot",        /* GO_MARKER_HALF_BAR */
		"diamond",    /* GO_MARKER_BUTTERFLY */       /* FIXME: dubious */
		"diamond",    /* GO_MARKER_HOURGLASS */       /* FIXME: dubious */
		"dot"         /* GO_MARKER_LEFT_HALF_BAR */
	};
	static const gint8 nqturns[] = { 0, 0, 0, 2, 0, +1, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	static const gint8 flipH[] =   { 0, 0, 0, 0, 0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0 };
	static const gint8 extS[] =    { 0, 0, 0, 0, 0,  0,  0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0 };
	gboolean need_spPr;
	gboolean ext_symbol = FALSE;
	GOMarkerShape s;

	if ((style->interesting_fields & GO_STYLE_MARKER) == 0)
		return;

	s = style->marker.auto_shape
		? (sctx->def_has_markers ? GO_MARKER_MAX : GO_MARKER_NONE)
		: go_marker_get_shape (style->marker.mark);
	if (!style->marker.auto_shape && s < G_N_ELEMENTS (extS) && extS[s])
		ext_symbol = TRUE;
	if (style->marker.auto_shape && s == GO_MARKER_NONE)
		ext_symbol = TRUE;

	gsf_xml_out_start_element (xml, "c:marker");

	xlsx_write_chart_cstr_unchecked
		(xml, "c:symbol",
		 (s < G_N_ELEMENTS (markers) && markers[s]
		  ? markers[s]
		  : "auto"));

	/* We don't have an auto_size flag */
	if (TRUE) {
		int s = go_marker_get_size (style->marker.mark);
		xlsx_write_chart_int (xml, "c:size", s);
	}

	need_spPr = (!style->marker.auto_fill_color ||
		     !style->marker.auto_outline_color);
	if (need_spPr) {
		gsf_xml_out_start_element (xml, "c:spPr");

		if (nqturns[s] || flipH[s]) {
			gsf_xml_out_start_element (xml, "a:xfrm");
			if (nqturns[s])
				gsf_xml_out_add_int (xml, "rot", nqturns[s] * (90 * 60000));
			if (flipH[s])
				gsf_xml_out_add_int (xml, "flipH", flipH[s]);
			gsf_xml_out_end_element (xml);
		}

		if (!style->marker.auto_fill_color) {
			gsf_xml_out_start_element (xml, "a:solidFill");
			xlsx_write_rgbarea (xml, go_marker_get_fill_color (style->marker.mark));
			gsf_xml_out_end_element (xml);
		}

		if (!style->marker.auto_outline_color) {
			gsf_xml_out_start_element (xml, "a:ln");
			gsf_xml_out_start_element (xml, "a:solidFill");
			xlsx_write_rgbarea (xml, go_marker_get_outline_color (style->marker.mark));
			gsf_xml_out_end_element (xml);
			gsf_xml_out_end_element (xml);
		}

		gsf_xml_out_end_element (xml);
	}

	if (sctx->state->with_extension && ext_symbol) {
		gsf_xml_out_start_element (xml, "c:extLst");
		gsf_xml_out_start_element (xml, "c:ext");
		gsf_xml_out_add_cstr_unchecked (xml, "uri", ns_gnm_ext);
		gsf_xml_out_start_element (xml, "gnmx:gostyle");

		if (ext_symbol) {
			gsf_xml_out_add_cstr (xml, "markerSymbol",
					      style->marker.auto_shape
					      ? "auto"
					      : go_marker_shape_as_str (s));
		}

		gsf_xml_out_end_element (xml);  /* "gnmx:gostyle" */
		gsf_xml_out_end_element (xml);  /* "c:ext" */
		gsf_xml_out_end_element (xml);  /* "c:extLst" */
	}

	gsf_xml_out_end_element (xml);
}

static void
xlsx_write_go_style_full (GsfXMLOut *xml, GOStyle *style, const XLSXStyleContext *sctx)
{
	gboolean has_font_color = ((style->interesting_fields & GO_STYLE_FONT) &&
				   !style->font.auto_color);
	gboolean has_font = xlsx_go_style_has_font (style);
	gboolean has_layout_angle = ((style->interesting_fields & GO_STYLE_TEXT_LAYOUT) &&
				     !style->text_layout.auto_angle);
	gboolean ext_fill_pattern = FALSE;
	gboolean ext_fill_auto_pattern = FALSE;
	gboolean ext_fill_auto_back = FALSE;
	gboolean ext_start_arrow = FALSE;
	gboolean ext_end_arrow = FALSE;
	gboolean ext_gradient_rev = FALSE;
	gboolean ext_dash_type = FALSE;

	char *spPr_tag = g_strconcat (sctx->spPr_ns, ":spPr", NULL);

	gsf_xml_out_start_element (xml, spPr_tag);

	if (sctx->flipH || sctx->flipV) {
		gsf_xml_out_start_element (xml, "a:xfrm");
		if (sctx->flipH)
			gsf_xml_out_add_uint (xml, "flipH", 1);
		if (sctx->flipV)
			gsf_xml_out_add_uint (xml, "flipV", 1);
		gsf_xml_out_end_element (xml); /* </a:xfrm> */
	}

	if (sctx->shapename) {
		gsf_xml_out_start_element (xml, "a:prstGeom");
		gsf_xml_out_add_cstr_unchecked (xml, "prst", sctx->shapename);
		gsf_xml_out_end_element (xml); /* </a:prstGeom> */
	}

	if (style->interesting_fields & GO_STYLE_FILL) {
		switch (style->fill.type) {
		default:
			g_warning ("invalid fill type, saving as none");
		case GO_STYLE_FILL_IMAGE:
			/* FIXME: export image */
		case GO_STYLE_FILL_NONE:
			if (!style->fill.auto_type)
				gsf_xml_out_simple_element (xml, "a:noFill", NULL);
			break;
		case GO_STYLE_FILL_PATTERN: {
			const char *pattname = NULL;
			ext_fill_auto_pattern = TRUE;
			switch (style->fill.pattern.pattern) {
			case GO_PATTERN_SOLID:
				ext_fill_pattern = TRUE;
				if (!style->fill.auto_back || sctx->must_fill_fill) {
					if (style->fill.auto_back)
						ext_fill_auto_back = TRUE;
					gsf_xml_out_start_element (xml, "a:solidFill");
					xlsx_write_rgbarea (xml, style->fill.pattern.back);
					gsf_xml_out_end_element (xml);
				}
				break;
			case GO_PATTERN_FOREGROUND_SOLID:
				if (!style->fill.auto_fore) {
					gsf_xml_out_start_element (xml, "a:solidFill");
					xlsx_write_rgbarea (xml, style->fill.pattern.fore);
					gsf_xml_out_end_element (xml);
				} else if (sctx->must_fill_fill) {
					/* No colour needed.  */
					gsf_xml_out_start_element (xml, "a:solidFill");
					gsf_xml_out_end_element (xml);
				}
				break;
			case GO_PATTERN_GREY75: pattname = "pct75"; break;
			case GO_PATTERN_GREY50: pattname = "pct50"; break;
			case GO_PATTERN_GREY25: pattname = "pct25"; break;
			case GO_PATTERN_GREY125: pattname = "pct10"; break;
			case GO_PATTERN_GREY625: pattname = "pct5"; break;
			case GO_PATTERN_HORIZ: pattname = "horz"; break;
			case GO_PATTERN_VERT: pattname = "vert"; break;
			case GO_PATTERN_REV_DIAG: pattname = "dnDiag"; break;
			case GO_PATTERN_DIAG: pattname = "upDiag"; break;
			case GO_PATTERN_DIAG_CROSS: pattname = "diagCross"; break;
			case GO_PATTERN_THICK_DIAG_CROSS: pattname = "trellis"; break;
			case GO_PATTERN_THIN_HORIZ: pattname = "ltHorz"; break;
			case GO_PATTERN_THIN_VERT: pattname = "ltVert"; break;
			case GO_PATTERN_THIN_REV_DIAG: pattname = "ltDnDiag"; break;
			case GO_PATTERN_THIN_DIAG: pattname = "ltUpDiag"; break;
			case GO_PATTERN_THIN_HORIZ_CROSS: pattname = "smGrid"; break;
			case GO_PATTERN_THIN_DIAG_CROSS: pattname = "openDmnd"; break;
			case GO_PATTERN_SMALL_CIRCLES: pattname = "smConfetti"; break;
			case GO_PATTERN_SEMI_CIRCLES: pattname = "shingle"; break;
			case GO_PATTERN_THATCH: pattname = "weave"; break;
			case GO_PATTERN_LARGE_CIRCLES: pattname = "sphere"; break;
			case GO_PATTERN_BRICKS: pattname = "horzBrick"; break;
			}

			if (pattname) {
				gsf_xml_out_start_element (xml, "a:pattFill");
				gsf_xml_out_add_cstr_unchecked (xml, "prst", pattname);
				if (!style->fill.auto_fore) {
					gsf_xml_out_start_element (xml, "a:fgClr");
					xlsx_write_rgbarea (xml, style->fill.pattern.fore);
					gsf_xml_out_end_element (xml);
				}
				if (!style->fill.auto_back) {
					gsf_xml_out_start_element (xml, "a:bgClr");
					xlsx_write_rgbarea (xml, style->fill.pattern.back);
					gsf_xml_out_end_element (xml);
				}
				gsf_xml_out_end_element (xml);
			}

			break;
		}
		case GO_STYLE_FILL_GRADIENT: {
			GOGradientDirection dir = style->fill.gradient.dir;
			gboolean mirrored = xlsx_gradient_info[dir].mirrored;
			gboolean rev = xlsx_gradient_info[dir].reversed;
			unsigned angle = xlsx_gradient_info[dir].angle;
			int i, N = mirrored ? 3 : 2;

			/* Different angle convention. */
			angle = (360 - angle) % (mirrored ? 180 : 360);

			/* FIXME: Unicolor? */

			gsf_xml_out_start_element (xml, "a:gradFill");
			gsf_xml_out_start_element (xml, "a:gsLst");
			for (i = 0; i < N; i++) {
				gboolean fore = rev ^ (i == 1);
				unsigned pos = (i == 0)
					? 0
					: (i == N - 1 ? 100 : 50);
				char *spos = g_strdup_printf ("%d%%", pos);
				gsf_xml_out_start_element (xml, "a:gs");
				gsf_xml_out_add_cstr_unchecked (xml, "pos", spos);
				g_free (spos);
				xlsx_write_rgbarea (xml,
						    fore
						    ? style->fill.pattern.fore
						    : style->fill.pattern.back);
				gsf_xml_out_end_element (xml); /* "a:gs" */
			}
			gsf_xml_out_end_element (xml); /* "a:gsLst" */
			gsf_xml_out_start_element (xml, "a:lin");
			gsf_xml_out_add_uint (xml, "ang", 60000 * angle);
			gsf_xml_out_end_element (xml);
			gsf_xml_out_end_element (xml); /* "a:gradFill" */

			if (rev)
				ext_gradient_rev = TRUE;
			break;
		}
		}
	}

	if ((style->interesting_fields & (GO_STYLE_LINE | GO_STYLE_OUTLINE)) &&
	    (!sctx->def_has_lines ||
	     !style->line.auto_dash ||
	     !style->line.auto_width ||
	     !style->line.auto_color ||
	     sctx->must_fill_line)) {
		int i;

		static const char * const dashes[] = {
			NULL,            /* GO_LINE_NONE */
			"solid",         /* GO_LINE_SOLID */
			"sysDot",        /* GO_LINE_S_DOT */
			"sysDashDot",    /* GO_LINE_S_DASH_DOT */
			"sysDashDotDot", /* GO_LINE_S_DASH_DOT_DOT */
			"lgDashDotDot",  /* GO_LINE_DASH_DOT_DOT_DOT */
			"dot",           /* GO_LINE_DOT */
			"sysDash",       /* GO_LINE_S_DASH */
			"dash",          /* GO_LINE_DASH */
			"lgDash",        /* GO_LINE_LONG_DASH */
			"dashDot",       /* GO_LINE_DASH_DOT */
			"lgDashDot",     /* GO_LINE_DASH_DOT_DOT */
		};
		gboolean is_none = style->line.auto_dash
			? !sctx->def_has_lines
			: style->line.dash_type == GO_LINE_NONE;

		gsf_xml_out_start_element (xml, "a:ln");
		if (!style->line.auto_width && style->line.width > 0)
			gsf_xml_out_add_int (xml, "w", style->line.width * 12700);

		if (!style->line.auto_color) {
			gsf_xml_out_start_element (xml, "a:solidFill");
			xlsx_write_rgbarea (xml, style->line.color);
			gsf_xml_out_end_element (xml);
		} else {
			gsf_xml_out_simple_element
				(xml,
				 is_none ? "a:noFill" : "a:solidFill",
				 NULL);
		}

		if (style->line.auto_dash) {
			ext_dash_type = TRUE;
		} else {
			if (style->line.dash_type < G_N_ELEMENTS (dashes) &&
			    dashes[style->line.dash_type]) {
				xlsx_write_chart_cstr_unchecked (xml,
								 "a:prstDash",
								 dashes[style->line.dash_type]);
			}
		}

		for (i = 0; i < 2; i++) {
			GOArrow const *arr = i ? sctx->end_arrow : sctx->start_arrow;
			XLArrowType typ;
			int l, w;
			static const char *types[] = {
				"none", "triangle", "stealth",
				"diamond", "oval", "arrow"
			};
			static const char *sizes[] = { "sm", "med", "lg" };
			double width;
			GOArrow res_arrow;

			if (!arr) continue;

			width = style->line.auto_width ? 0 : style->line.width;

			xls_arrow_to_xl (arr, width, &typ, &l, &w);
			xls_arrow_from_xl (&res_arrow, width, typ, l, w);
			if (!go_arrow_equal (&res_arrow, arr)) {
				if (i)
					ext_end_arrow = TRUE;
				else
					ext_start_arrow = TRUE;
			}

			gsf_xml_out_start_element (xml, i ? "a:tailEnd" : "a:headEnd");
			gsf_xml_out_add_cstr_unchecked (xml, "type", types[typ]);
			if (typ) {
				gsf_xml_out_add_cstr_unchecked (xml, "w", sizes[w]);
				gsf_xml_out_add_cstr_unchecked (xml, "len", sizes[l]);
			}
			gsf_xml_out_end_element (xml);
		}

		gsf_xml_out_end_element (xml);
	}

	if (sctx->state->with_extension &&
	    (ext_fill_pattern || ext_fill_auto_pattern || ext_fill_auto_back ||
	     ext_start_arrow || ext_end_arrow ||
	     ext_gradient_rev || ext_dash_type)) {
		gsf_xml_out_start_element (xml, "a:extLst");
		gsf_xml_out_start_element (xml, "a:ext");
		gsf_xml_out_add_cstr_unchecked (xml, "uri", ns_gnm_ext);
		gsf_xml_out_start_element (xml, "gnmx:gostyle");
		if (ext_dash_type) {
			gsf_xml_out_add_cstr (xml, "dashType",
					      style->line.auto_dash
					      ? "auto"
					      : go_line_dash_as_str (style->line.dash_type));
		}
		if (ext_fill_pattern) {
			gsf_xml_out_add_cstr (xml, "pattern",
					      go_pattern_as_str (style->fill.pattern.pattern));
		}
		if (ext_fill_auto_pattern) {
			xlsx_add_bool (xml, "auto-pattern", style->fill.auto_type);
		}
		if (ext_fill_auto_back) {
			xlsx_add_bool (xml, "auto-back", style->fill.auto_back);
		}
		if (ext_start_arrow) {
			GOArrow const *arrow = sctx->start_arrow;
			gsf_xml_out_add_cstr (xml, "StartArrowType", go_arrow_type_as_str (arrow->typ));
			go_xml_out_add_double (xml, "StartArrowShapeA", arrow->a);
			go_xml_out_add_double (xml, "StartArrowShapeB", arrow->b);
			go_xml_out_add_double (xml, "StartArrowShapeC", arrow->c);
		}
		if (ext_end_arrow) {
			GOArrow const *arrow = sctx->end_arrow;
			gsf_xml_out_add_cstr (xml, "EndArrowType", go_arrow_type_as_str (arrow->typ));
			go_xml_out_add_double (xml, "EndArrowShapeA", arrow->a);
			go_xml_out_add_double (xml, "EndArrowShapeB", arrow->b);
			go_xml_out_add_double (xml, "EndArrowShapeC", arrow->c);
		}
		if (ext_gradient_rev) {
			gsf_xml_out_add_uint (xml, "reverse-gradient", 1);
		}
		gsf_xml_out_end_element (xml);  /* "gnmx:gostyle" */
		gsf_xml_out_end_element (xml);  /* "a:ext" */
		gsf_xml_out_end_element (xml);  /* "a:extLst" */
	}

	gsf_xml_out_end_element (xml);  /* "NS:spPr" */
	g_free (spPr_tag);

	if (has_font_color || has_font || has_layout_angle) {
		gsf_xml_out_start_element (xml, "c:txPr");

		gsf_xml_out_start_element (xml, "a:bodyPr");
		if (has_layout_angle) {
			double angle = fmod (360 - style->text_layout.angle, 360.0);
			if (angle <= -180) angle += 360;
			if (angle > 180) angle -= 360;
			gsf_xml_out_add_int (xml, "rot", (int)(angle * 60000));
		}
		gsf_xml_out_end_element (xml);  /* "a:bodyPr" */

		gsf_xml_out_simple_element (xml, "a:lstStyle", NULL);
		gsf_xml_out_start_element (xml, "a:p");
		gsf_xml_out_start_element (xml, "a:pPr");
		gsf_xml_out_start_element (xml, "a:defRPr");
		xlsx_write_rpr (xml, style);
		gsf_xml_out_end_element (xml);  /* "a:defRPr" */
		gsf_xml_out_end_element (xml);  /* "a:pPr" */
		gsf_xml_out_end_element (xml);  /* "a:p" */
		gsf_xml_out_end_element (xml);  /* "c:txPr" */
	}

	if (!sctx->inhibit_marker)
		xlsx_write_go_style_marker (xml, style, sctx);
}

static void
xlsx_write_go_style (GsfXMLOut *xml, XLSXWriteState *state, GOStyle *style)
{
	XLSXStyleContext sctx;
	xlsx_style_context_init (&sctx, state);
	xlsx_write_go_style_full (xml, style, &sctx);
}

static void
xlsx_write_chart_text (XLSXWriteState *state, GsfXMLOut *xml,
		       GOData *data, GogObject const *label)
{
	char *text = go_data_get_scalar_string (data);
	GOStyle *style = go_styled_object_get_style (GO_STYLED_OBJECT (label));
	gboolean has_font_color = ((style->interesting_fields & GO_STYLE_FONT) &&
				   !style->font.auto_color);
	gboolean has_font = xlsx_go_style_has_font (style);
	gboolean allow_wrap;
	GOStyle *style_minus_font;

	gsf_xml_out_start_element (xml, "c:tx");
	gsf_xml_out_start_element (xml, "c:rich");

	gsf_xml_out_start_element (xml, "a:bodyPr");
	g_object_get (G_OBJECT (label), "allow-wrap", &allow_wrap, NULL);
	if (!allow_wrap)
		gsf_xml_out_add_cstr_unchecked (xml, "wrap", "none");
	gsf_xml_out_end_element (xml); /* </a:bodyPr> */

	gsf_xml_out_start_element (xml, "a:p");
	gsf_xml_out_start_element (xml, "a:r");

	if (has_font_color || has_font) {
		gsf_xml_out_start_element (xml, "a:rPr");
		xlsx_write_rpr (xml, style);
		gsf_xml_out_end_element (xml); /* </a:rPr> */
	}

	gsf_xml_out_simple_element (xml, "a:t", text);

	gsf_xml_out_end_element (xml); /* </a:r> */
	gsf_xml_out_end_element (xml); /* </a:p> */

	gsf_xml_out_end_element (xml); /* </c:rich> */
	gsf_xml_out_end_element (xml); /* </c:tx> */

	xlsx_write_chart_uint (xml, "c:overlay", 0);

	style_minus_font = go_style_dup (style);
	style_minus_font->interesting_fields &= ~GO_STYLE_FONT;
	xlsx_write_go_style (xml, state, style_minus_font);
	g_object_unref (style_minus_font);

	g_free (text);
}

static void
xlsx_write_layout (GsfXMLOut *xml, GogObject const *obj)
{
	unsigned pos = gog_object_get_position_flags (obj, GOG_POSITION_ANY_MANUAL);
	if (pos & GOG_POSITION_MANUAL) {
		/* FIXME: we suppose that the position is relative to start, and not absolute */
		GogViewAllocation alloc;
		gsf_xml_out_start_element (xml, "c:layout");
		gsf_xml_out_start_element (xml, "c:manualLayout");
		gog_object_get_manual_position ((GogObject *) obj, &alloc);
		xlsx_write_chart_cstr_unchecked (xml, "c:xMode", "edge");
		xlsx_write_chart_cstr_unchecked (xml, "c:yMode", "edge");
		xlsx_write_chart_float (xml, "c:x", alloc.x);
		xlsx_write_chart_float (xml, "c:y", alloc.y);
		xlsx_write_chart_float (xml, "c:w", alloc.w);
		xlsx_write_chart_float (xml, "c:h", alloc.h);
		gsf_xml_out_end_element (xml); /* </c:manualLayout> */
		gsf_xml_out_end_element (xml); /* </c:layout> */
	}
}

static unsigned
xlsx_get_axid (XLSXWriteState *state, GogAxis *axis)
{
	gpointer l = g_hash_table_lookup (state->axids, axis);
	if (!l) {
		l = GUINT_TO_POINTER (1 + g_hash_table_size (state->axids));
		g_hash_table_insert (state->axids, axis, l);
	}
	return GPOINTER_TO_UINT (l);
}


static void
xlsx_write_axis (XLSXWriteState *state, GsfXMLOut *xml,
		 GogPlot *plot, XLSXPlotType plot_type, GogAxis *axis)
{
	GogAxisType at = gog_axis_get_atype (axis);
	GogAxis *crossed = gog_axis_base_get_crossed_axis_for_plot (GOG_AXIS_BASE (axis), plot);
	GogAxisPosition pos;
	GogGridLine *grid;
	GogObject *label;
	GOFormat *format;
	double d;
	gboolean user_defined;
	char *map_name;
	const char *axis_tag = NULL;

#ifdef DEBUG_AXIS
	g_printerr ("Writing axis %s [id=%d].  (discrete = %d)\n",
		    gog_object_get_name (GOG_OBJECT (axis)),
		    xlsx_get_axid (state, axis),
		    gog_axis_is_discrete (axis));
#endif

	g_object_get (G_OBJECT (axis),
		      "pos", &pos,
		      "map-name", &map_name,
		      NULL);

	switch (plot_type) {
	case XLSX_PT_GOGCONTOURPLOT:
	case XLSX_PT_XLCONTOURPLOT:
		// Both X and Y axes are discrete, but XL wants valAx for the
		// X axis.
		switch (at) {
		case GOG_AXIS_X: axis_tag = "c:valAx"; break;
		case GOG_AXIS_Y: axis_tag = "c:catAx"; break;
		case GOG_AXIS_PSEUDO_3D: axis_tag = "c:serAx"; break;
		default: g_assert_not_reached ();
		}
		break;
	default:
		break;
	}

	if (!axis_tag)
		axis_tag = gog_axis_is_discrete (axis) ? "c:catAx" : "c:valAx";

	gsf_xml_out_start_element (xml, axis_tag);
	xlsx_write_chart_uint (xml, "c:axId", xlsx_get_axid (state, axis));

	gsf_xml_out_start_element (xml, "c:scaling");
	if (g_strcmp0 (map_name, "Log") == 0) {
		double base = 10;
		xlsx_write_chart_float (xml, "c:logBase", base);
	}
	g_free (map_name);
	xlsx_write_chart_cstr_unchecked (xml, "c:orientation",
					 gog_axis_is_inverted (axis)? "maxMin": "minMax");
	d = gog_axis_get_entry (axis, GOG_AXIS_ELEM_MAX, &user_defined);
	if (user_defined) xlsx_write_chart_float (xml, "c:max", d);
	d = gog_axis_get_entry (axis, GOG_AXIS_ELEM_MIN, &user_defined);
	if (user_defined) xlsx_write_chart_float (xml, "c:min", d);
	gsf_xml_out_end_element (xml);

	xlsx_write_chart_uint (xml, "c:delete", 0);

	/*
	 * It is unclear what this is good for.  The information is in the
	 * crossing location.
	 */
	{
		const char * const axpos[4] = { "l", "r", "b", "t" };
		gboolean tb = (at == GOG_AXIS_X || at == GOG_AXIS_CIRCULAR);
		gboolean tr = (pos == GOG_AXIS_AT_HIGH);
		xlsx_write_chart_cstr_unchecked (xml,
						 "c:axPos",
						 axpos[2 * tb + tr]);
	}

	/* grids */
	grid = gog_axis_get_grid_line (axis, TRUE);
	if (grid) {
		gsf_xml_out_start_element (xml, "c:majorGridlines");
		xlsx_write_go_style (xml, state, go_styled_object_get_style (GO_STYLED_OBJECT (grid)));
		gsf_xml_out_end_element (xml);
	}
	grid = gog_axis_get_grid_line (axis, FALSE);
	if (grid) {
		gsf_xml_out_start_element (xml, "c:minorGridlines");
		xlsx_write_go_style (xml, state, go_styled_object_get_style (GO_STYLED_OBJECT (grid)));
		gsf_xml_out_end_element (xml);
	}

	label = gog_object_get_child_by_name (GOG_OBJECT (axis), "Label");
	if (label) {
		GOData *text = gog_dataset_get_dim (GOG_DATASET (label), 0);
		if (text != NULL) {
			gsf_xml_out_start_element (xml, "c:title");
			xlsx_write_chart_text (state, xml, text, label);
			gsf_xml_out_end_element (xml);
		}
	}

	gsf_xml_out_start_element (xml, "c:numFmt");
	format = gog_axis_get_format (axis);
	xlsx_add_bool (xml, "sourceLinked", format == NULL || go_format_is_general (format));
	format = gog_axis_get_effective_format (axis);
	gsf_xml_out_add_cstr (xml, "formatCode", (format)? go_format_as_XL (format): "General");
	gsf_xml_out_end_element (xml);

	{
		gboolean mati, miti, mato, mito, mal;
		static const char *marks[4] = { "none", "in", "out", "cross" };

		g_object_get (G_OBJECT (axis),
			      "major-tick-in", &mati,
			      "minor-tick-in", &miti,
			      "major-tick-out", &mato,
			      "minor-tick-out", &mito,
			      "major-tick-labeled", &mal,
			      NULL);
		xlsx_write_chart_cstr_unchecked (xml, "c:majorTickMark",
						 marks[2 * mato + mati]);
		xlsx_write_chart_cstr_unchecked (xml, "c:minorTickMark",
						 marks[2 * mito + miti]);
		if (!mal)
			xlsx_write_chart_cstr_unchecked (xml, "c:tickLblPos", "none");
	}

	xlsx_write_go_style (xml, state, go_styled_object_get_style (GO_STYLED_OBJECT (axis)));

	xlsx_write_chart_int (xml, "c:crossAx", xlsx_get_axid (state, crossed));
	switch (pos) {
	default:
	case GOG_AXIS_AT_LOW:
	case GOG_AXIS_AT_HIGH: {
		gboolean is_low = (pos == GOG_AXIS_AT_LOW);
		gboolean cross_inv = gog_axis_is_inverted (crossed);
		xlsx_write_chart_cstr_unchecked (xml, "c:crosses",
						 is_low ^ cross_inv ? "min" : "max");
		break;
	}
	case GOG_AXIS_CROSS: {
		double cross = gog_axis_base_get_cross_location (GOG_AXIS_BASE (axis));
		if (cross == 0.)
			xlsx_write_chart_cstr_unchecked (xml, "c:crosses", "autoZero");
		else
			xlsx_write_chart_float (xml, "c:crossesAt", cross);
		break;
	}
	}

	d = gog_axis_get_entry (axis, GOG_AXIS_ELEM_MAJOR_TICK, &user_defined);
	if (user_defined && d > 0) xlsx_write_chart_float (xml, "c:majorUnit", d);
	d = gog_axis_get_entry (axis, GOG_AXIS_ELEM_MINOR_TICK, &user_defined);
	if (user_defined && d > 0) xlsx_write_chart_float (xml, "c:minorUnit", d);

	g_object_get (axis, "display-factor", &d, NULL);
	if (d != 1.) {
		gsf_xml_out_start_element (xml, "c:dispUnits");
		gsf_xml_out_start_element (xml, "c:custUnit");
		gsf_xml_out_add_float (xml, "val", d, -1);
		gsf_xml_out_end_element (xml);
		gsf_xml_out_end_element (xml);
	}

	/* finished with axis */
	gsf_xml_out_end_element (xml);
}


static GSList *
xlsx_write_one_plot (XLSXWriteState *state, GsfXMLOut *xml,
		     GogObject const *chart, GogPlot *plot, int *ser_count)
{
	double explosion = 0.;
	gboolean vary_by_element;
	GogAxisType axis_type[3] = {GOG_AXIS_X, GOG_AXIS_Y, GOG_AXIS_UNKNOWN};
	unsigned i;
	XLSXPlotType plot_type;
	const char *plot_type_name;
	GSList const *series;
	gboolean has_markers = FALSE;
	gboolean has_lines = FALSE;
	gboolean use_xy = FALSE;
	gboolean set_smooth = FALSE;
	gboolean set_invert = FALSE;
	GSList *axes = NULL;

	g_object_get (G_OBJECT (plot),
		      "vary-style-by-element", &vary_by_element,
		      NULL);
	plot_type_name = G_OBJECT_TYPE_NAME (plot);
	plot_type = xlsx_plottype_from_type_name (plot_type_name);

	switch (plot_type) {
	default:
	case XLSX_PT_UNKNOWN:
		g_warning ("unexpected plot type %s", plot_type_name);
		return NULL;

	case XLSX_PT_GOGAREAPLOT:
		gsf_xml_out_start_element (xml, "c:areaChart");
		xlsx_write_plot_1_5_type (xml, plot, FALSE);
		xlsx_write_chart_bool (xml, "c:varyColors", vary_by_element);
		break;

	case XLSX_PT_GOGBARCOLPLOT: {
		gboolean horizontal;

		g_object_get (G_OBJECT (plot), "horizontal", &horizontal, NULL);
		if (horizontal) {
			axis_type[0] = GOG_AXIS_Y;
			axis_type[1] = GOG_AXIS_X;
		}
		gsf_xml_out_start_element (xml, "c:barChart");

		xlsx_write_chart_cstr_unchecked (xml, "c:barDir",
						 horizontal ? "bar" : "col");

		xlsx_write_plot_1_5_type (xml, plot, TRUE);
		xlsx_write_chart_bool (xml, "c:varyColors", vary_by_element);
		set_invert = TRUE;
		break;
	}

	case XLSX_PT_GOGLINEPLOT:
		gsf_xml_out_start_element (xml, "c:lineChart");
		xlsx_write_plot_1_5_type (xml, plot, FALSE);
		xlsx_write_chart_bool (xml, "c:varyColors", vary_by_element);
		set_smooth = TRUE;
		has_lines = TRUE;
		g_object_get (G_OBJECT (plot),
		              "default-style-has-markers", &has_markers,
		              NULL);
		break;

	case XLSX_PT_GOGPIEPLOT:
	case XLSX_PT_GOGRINGPLOT:
		if (plot_type == XLSX_PT_GOGRINGPLOT) {
			gsf_xml_out_start_element (xml, "c:doughnutChart");
		} else
			gsf_xml_out_start_element (xml, "c:pieChart");

		xlsx_write_chart_bool (xml, "c:varyColors", vary_by_element);

#if 0
		double default_separation = 0.;
		/* handled in series ? */
		"default-separation",	&default_separation,
		xlsx_write_chart_int (xml, "c:explosion", default_separation);
#endif
		axis_type[0] = axis_type[1] = GOG_AXIS_UNKNOWN;
		g_object_get (G_OBJECT (plot), "default-separation", &explosion, NULL);
		break;

	case XLSX_PT_GOGRADARPLOT:
	case XLSX_PT_GOGRADARAREAPLOT:
		gsf_xml_out_start_element (xml, "c:radarChart");
		xlsx_write_chart_cstr_unchecked (xml, "c:radarStyle", "standard");
		xlsx_write_chart_bool (xml, "c:varyColors", vary_by_element);
		has_lines = TRUE;
		axis_type[0] = GOG_AXIS_CIRCULAR;
		axis_type[1] = GOG_AXIS_RADIAL;
		break;

	case XLSX_PT_GOGBUBBLEPLOT:
		gsf_xml_out_start_element (xml, "c:bubbleChart");
		xlsx_write_chart_bool (xml, "c:varyColors", vary_by_element);
		use_xy = TRUE;
		set_invert = TRUE;
		break;

	case XLSX_PT_GOGXYPLOT: {
		gboolean use_splines;
		char const *style;
		g_object_get (G_OBJECT (plot),
		              "default-style-has-lines", &has_lines,
		              "default-style-has-markers", &has_markers,
		              "use-splines", &use_splines,
		              NULL);
		style = (has_lines)?
				(use_splines?
					(has_markers? "smoothMarker": "smooth"):
					(has_markers? "lineMarker": "line")):
				(has_markers? "marker": "none");
		use_xy = TRUE;
		set_smooth = TRUE;
		gsf_xml_out_start_element (xml, "c:scatterChart");
		xlsx_write_chart_cstr_unchecked (xml, "c:scatterStyle", style);
		xlsx_write_chart_bool (xml, "c:varyColors", vary_by_element);
		break;
	}

	case XLSX_PT_GOGCONTOURPLOT:
	case XLSX_PT_XLCONTOURPLOT:
		axis_type[2] = GOG_AXIS_PSEUDO_3D;
		gsf_xml_out_start_element (xml, "c:surfaceChart");
		xlsx_write_chart_bool (xml, "c:wireframe", FALSE);
		break;

	case XLSX_PT_GOGSURFACEPLOT:
	case XLSX_PT_XLSURFACEPLOT:
		axis_type[2] = GOG_AXIS_Z;
		gsf_xml_out_start_element (xml, "c:surface3DChart");
		xlsx_write_chart_bool (xml, "c:wireframe", FALSE);
		break;
	}

	for (series = gog_plot_get_series (GOG_PLOT (plot));
	     NULL != series;
	     series = series->next) {
		GogSeries *ser = series->data;
		GSList *l, *children;
		GOStyle *style = go_styled_object_get_style (GO_STYLED_OBJECT (ser));
		XLSXStyleContext sctx;
		int count = (*ser_count)++;

		gsf_xml_out_start_element (xml, "c:ser");

		xlsx_write_chart_int (xml, "c:idx", count);
		xlsx_write_chart_int (xml, "c:order", count);
		xlsx_write_series_dim (state, xml, ser, "c:tx", GOG_MS_DIM_LABELS);

		xlsx_style_context_init (&sctx, state);
		sctx.def_has_markers = has_markers;
		sctx.def_has_lines = has_lines;
		xlsx_write_go_style_full (xml, style, &sctx);

		if (set_invert)
			xlsx_write_chart_uint (xml, "c:invertIfNegative", 0);
		if (explosion > 0.)
			xlsx_write_chart_uint (xml, "c:explosion", (unsigned) (explosion * 100));

		children = gog_object_get_children (GOG_OBJECT (ser), NULL);

		for (l = children; l; l = l->next) {
			GogObject *pt = l->data;
			int idx;
			GOStyle *style;
			XLSXStyleContext sctx;

			if (!GOG_IS_SERIES_ELEMENT (pt))
				continue;

			gsf_xml_out_start_element (xml, "c:dPt");

			g_object_get (pt, "index", &idx, NULL);
			xlsx_write_chart_int (xml, "c:idx", MAX (0, idx));

			xlsx_style_context_init (&sctx, state);
			sctx.def_has_markers = TRUE;
			style = go_styled_object_get_style (GO_STYLED_OBJECT (pt));
			xlsx_write_go_style_marker (xml, style, &sctx);
			sctx.inhibit_marker = TRUE;
			xlsx_write_go_style_full (xml, style, &sctx);

			gsf_xml_out_end_element (xml); /* </c:dPt> */
		}

		for (l = children; l; l = l->next) {
			GogObject *trend = l->data;
			const char *trend_type_name = G_OBJECT_TYPE_NAME (trend);
			const char *trend_type;
			GogObject *eq;
			GOData *dat;
			double intercept = gnm_nan;

			if (!GOG_IS_TREND_LINE (trend))
				continue;

			if (strcmp (trend_type_name, "GogExpRegCurve") == 0)
				trend_type = "exp";
			else if (strcmp (trend_type_name, "GogLinRegCurve") == 0) {
				trend_type = "linear";
				if (!gnm_object_get_bool (trend, "affine"))
					intercept = 0;
			} else if (strcmp (trend_type_name, "GogLogRegCurve") == 0)
				trend_type = "log";
			else if (strcmp (trend_type_name, "GogMovingAvg") == 0)
				trend_type = "movingAvg";
			else if (strcmp (trend_type_name, "GogPolynomRegCurve") == 0)
				trend_type = "poly";
			else if (strcmp (trend_type_name, "GogPowerRegCurve") == 0)
				trend_type = "power";
			else {
				trend_type = "linear";
				g_warning ("Unknown regression mapped to %s\n", trend_type);
			}

			gsf_xml_out_start_element (xml, "c:trendline");
			dat = gog_dataset_get_dim (GOG_DATASET (trend), -1);
			if (dat) {
				char *name = go_data_get_scalar_string (dat);
				gsf_xml_out_simple_element (xml, "c:name", name);
				g_free (name);
			}
			xlsx_write_go_style (xml, state, go_styled_object_get_style (GO_STYLED_OBJECT (trend)));
			xlsx_write_chart_cstr_unchecked (xml, "c:trendlineType", trend_type);

			if (!gnm_isnan (intercept))
				xlsx_write_chart_float (xml, "c:intercept", intercept);

			eq = gog_object_get_child_by_name (trend, "Equation");
			if (eq) {
				gboolean has_r2, has_eq;
				g_object_get (eq, "show-r2", &has_r2, "show-eq", &has_eq, NULL);
				if (has_r2)
					xlsx_write_chart_bool (xml, "c:dispRSqr", TRUE);
				if (has_eq)
					xlsx_write_chart_bool (xml, "c:dispEq", TRUE);
			}

			gsf_xml_out_end_element (xml); /* </c:trendline> */
		}
		g_slist_free (children);

		if (use_xy) {
			xlsx_write_series_dim (state, xml, ser, "c:xVal", GOG_MS_DIM_CATEGORIES);
			xlsx_write_series_dim (state, xml, ser, "c:yVal", GOG_MS_DIM_VALUES);
			xlsx_write_series_dim (state, xml, ser, "c:bubbleSize", GOG_MS_DIM_BUBBLES);
		} else {
			xlsx_write_series_dim (state, xml, ser, "c:cat", GOG_MS_DIM_CATEGORIES);
			xlsx_write_series_dim (state, xml, ser, "c:val", GOG_MS_DIM_VALUES);
		}

		if (set_smooth) {
			gboolean smooth;
			GOLineInterpolation inter;
			char *s;

			g_object_get (ser, "interpolation", &s, NULL);
			inter = go_line_interpolation_from_str (s);
			g_free (s);

			smooth = inter != GO_LINE_INTERPOLATION_LINEAR;
			xlsx_write_chart_bool (xml, "c:smooth", smooth);
		}

		gsf_xml_out_end_element (xml); /* </c:ser> */
	}

	switch (plot_type) {
	case XLSX_PT_GOGBARCOLPLOT: {
		int overlap_percentage, gap_percentage;

		g_object_get (G_OBJECT (plot),
			"overlap-percentage",	&overlap_percentage,
			"gap-percentage",	&gap_percentage,
			NULL);

		/* Spec says add "%" at end; XL cannot handle that. */
		xlsx_write_chart_int (xml, "c:gapWidth", CLAMP (gap_percentage, 0, 500));

		/* Spec says add "%" at end; XL cannot handle that. */
		xlsx_write_chart_int (xml, "c:overlap", CLAMP (overlap_percentage, 0, 100));
		break;
	}

	case XLSX_PT_GOGPIEPLOT:
	case XLSX_PT_GOGRINGPLOT: {
		double initial_angle = 0;
		g_object_get (G_OBJECT (plot),
			      "initial-angle", &initial_angle,
			      NULL);
		xlsx_write_chart_int (xml, "c:firstSliceAng", (int) initial_angle);

		if (plot_type == XLSX_PT_GOGRINGPLOT) {
			int center;
			double center_size;
			g_object_get (G_OBJECT (plot), "center-size", &center_size, NULL);
			center = (int)floor (center_size * 100. + .5);
			xlsx_write_chart_int (xml, "c:holeSize", CLAMP (center, 10, 90));
		}
		break;
	}

	case XLSX_PT_GOGBUBBLEPLOT: {
		gboolean show_neg = FALSE, in_3d = FALSE, as_area = TRUE;
		g_object_get (G_OBJECT (plot),
			      "show-negatives",	&show_neg,
			      "in-3d",		&in_3d,
			      "size-as-area",	&as_area,
			      NULL);
		if (in_3d)
			xlsx_write_chart_bool (xml, "c:bubble3D", TRUE);
		xlsx_write_chart_bool (xml, "c:showNegBubbles", show_neg);
		xlsx_write_chart_cstr_unchecked (xml, "c:sizeRepresents",
			as_area ? "area" : "w");
		break;
	}

	case XLSX_PT_GOGLINEPLOT:
		if (has_markers)
			xlsx_write_chart_bool (xml, "c:marker", has_markers);
		break;

	default:
		break; /* Nothing */
	}

	/* write axes Ids */
	for (i = 0; i < 3; i++) {
		if (axis_type[i] != GOG_AXIS_UNKNOWN) {
			GogAxis *axis = gog_plot_get_axis (GOG_PLOT (plot), axis_type[i]);
			if (axis) {
				xlsx_write_chart_uint (xml, "c:axId", xlsx_get_axid (state, axis));
				axes = g_slist_append (axes, axis);
			}
		}
	}

	gsf_xml_out_end_element (xml);

	return axes;
}

static void
xlsx_write_plots (XLSXWriteState *state, GsfXMLOut *xml, GogObject const *chart,
		  GSList *plots, int *ser_count)
{
	GSList *l;
	GHashTable *axis_to_plot = g_hash_table_new (NULL, NULL);
	GSList *axes = NULL;

	for (l = plots; l; l = l->next) {
		GogPlot *plot = l->data;
		GSList *plot_axes, *al;

		plot_axes = xlsx_write_one_plot (state, xml, chart, plot, ser_count);
		for (al = plot_axes; al; al = al->next) {
			GogAxis *axis = al->data;
			if (!g_hash_table_lookup (axis_to_plot, axis)) {
				g_hash_table_insert (axis_to_plot, axis, plot);
				axes = g_slist_append (axes, axis);
			}
		}
		g_slist_free (plot_axes);
	}

	for (l = axes; l; l = l->next) {
		GogAxis *axis = l->data;
		GogPlot *plot = g_hash_table_lookup (axis_to_plot, axis);
		const char *plot_type_name = G_OBJECT_TYPE_NAME (plot);
		XLSXPlotType plot_type = xlsx_plottype_from_type_name (plot_type_name);
		xlsx_write_axis (state, xml, plot, plot_type, axis);
	}
	g_slist_free (axes);
	g_hash_table_destroy (axis_to_plot);
}

static void
xlsx_write_one_chart (XLSXWriteState *state, GsfXMLOut *xml, GogObject const *chart)
{
	GogObject const *obj;
	int ser_count = 0;
	GogObjectRole const *role;
	GSList *plots, *l;
	gboolean done;

	gsf_xml_out_start_element (xml, "c:chart");

	obj = gog_object_get_child_by_name (chart, "Title");
	if (obj) {
		GOData *text = gog_dataset_get_dim (GOG_DATASET (obj), 0);
		if (text != NULL) {
			gsf_xml_out_start_element (xml, "c:title");
			xlsx_write_chart_text (state, xml, text, obj);
			gsf_xml_out_end_element (xml);
		}
	}

	role = gog_object_find_role_by_name (GOG_OBJECT (chart), "Plot");
	plots = gog_object_get_children (GOG_OBJECT (chart), role);

	for (l = plots, done = FALSE; l && !done; l = l->next) {
		GogPlot *plot = l->data;
		const char *plot_type_name = G_OBJECT_TYPE_NAME (plot);
		XLSXPlotType plot_type = xlsx_plottype_from_type_name (plot_type_name);
		switch (plot_type) {
		case XLSX_PT_GOGCONTOURPLOT:
		case XLSX_PT_XLCONTOURPLOT:
			// XL wants a 3D view for a 2D chart
			gsf_xml_out_start_element (xml, "c:view3D");
			xlsx_write_chart_float (xml, "c:rotX", 90);
			xlsx_write_chart_float (xml, "c:rotY", 0);
			xlsx_write_chart_float (xml, "c:rAngAx", 0);
			xlsx_write_chart_float (xml, "c:perspective", 0);
			gsf_xml_out_end_element (xml);
			done = TRUE;
			break;
		default:
			; // Nothing
		}
	}

	gsf_xml_out_start_element (xml, "c:plotArea");
	/* save grid style here */

	xlsx_write_plots (state, xml, chart, plots, &ser_count);

	obj = gog_object_get_child_by_name (GOG_OBJECT (chart), "Backplane");
	if (obj) {
		XLSXStyleContext sctx;
		xlsx_style_context_init (&sctx, state);
		sctx.must_fill_fill = TRUE;
		xlsx_write_go_style_full
			(xml, go_styled_object_get_style (GO_STYLED_OBJECT (obj)),
			 &sctx);
	}

	gsf_xml_out_end_element (xml); /* </c:plotArea> */
	g_slist_free (plots);

	if ((obj = gog_object_get_child_by_name (chart, "Legend"))) {
		char const *str;
		unsigned pos = gog_object_get_position_flags (obj, GOG_POSITION_COMPASS);
		gsf_xml_out_start_element (xml, "c:legend");
		switch (pos) {
		case GOG_POSITION_N:
			str = "t";
			break;
		case GOG_POSITION_S:
			str = "b";
			break;
		case GOG_POSITION_W:
			str = "l";
			break;
		default:
		case GOG_POSITION_E:
			str = "r";
			break;
		case GOG_POSITION_N | GOG_POSITION_E:
			str = "tr";
			break;
		case GOG_POSITION_N | GOG_POSITION_W:
			str = "tl";
			break;
		case GOG_POSITION_S | GOG_POSITION_E:
			str = "br";
			break;
		case GOG_POSITION_S | GOG_POSITION_W:
			str = "bl";
			break;
		}
		xlsx_write_chart_cstr_unchecked (xml, "c:legendPos", str);
		xlsx_write_layout (xml, obj);
		xlsx_write_go_style (xml, state, go_styled_object_get_style (GO_STYLED_OBJECT (obj)));
		gsf_xml_out_end_element (xml); /* </c:legend> */
	}
	gsf_xml_out_end_element (xml); /* </c:chart> */

	xlsx_write_go_style (xml, state, go_styled_object_get_style (GO_STYLED_OBJECT (chart)));
}

static void
xlsx_write_chart (XLSXWriteState *state, GsfOutput *chart_part, SheetObject *so)
{
	GogGraph const *graph;
	GsfXMLOut *xml;

	xml = gsf_xml_out_new (chart_part);
	gsf_xml_out_start_element (xml, "c:chartSpace");
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns:c", ns_chart);
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns:a", ns_drawing);
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns:r", ns_rel);
	if (state->with_extension)
		gsf_xml_out_add_cstr_unchecked (xml, "xmlns:gnmx", ns_gnm_ext);
	xlsx_write_chart_uint (xml, "c:roundedCorners", 0);

	graph = sheet_object_graph_get_gog (so);
	if (graph != NULL) {
		GogObjectRole const *role = gog_object_find_role_by_name (GOG_OBJECT (graph), "Chart");
		GSList *charts = gog_object_get_children (GOG_OBJECT (graph), role);
		if (charts != NULL) {
			GogObject const	*chart1 = charts->data;
			xlsx_write_one_chart (state, xml, chart1);
			if (charts->next)
				g_warning ("Dropping %d charts on the floor!",
					   g_slist_length (charts->next));
			g_slist_free (charts);
		}
	}
	gsf_xml_out_end_element (xml); /* </c:chartSpace> */
	g_object_unref (xml);
}

static int
xlsx_pts_to_emu (double pts)
{
	return (double) gnm_floor (12700. * pts);
}

static void
xlsx_write_object_anchor (GsfXMLOut *xml, GnmCellPos const *pos, char const *element, double col_off_pts, double row_off_pts)
{
	/* For some reason we are using this additional scaling factor when we read!? */
	/* FIXME: scaling horizontally just like in xlsx_CT_Col */
	gsf_xml_out_start_element (xml, element);
	gsf_xml_out_simple_int_element (xml, "xdr:col", pos->col);
	gsf_xml_out_simple_int_element (xml, "xdr:colOff",
					xlsx_pts_to_emu (col_off_pts * 1.16191275167785));
	gsf_xml_out_simple_int_element (xml, "xdr:row", pos->row);
	gsf_xml_out_simple_int_element (xml, "xdr:rowOff",
					xlsx_pts_to_emu (row_off_pts));
	gsf_xml_out_end_element (xml);
}

static void
xlsx_write_nvpr (GsfXMLOut *xml, SheetObject *so, int id)
{
	// "Non-Visual Properties"
	char *tmp;

	gsf_xml_out_start_element (xml, "xdr:cNvPr");
	gsf_xml_out_add_int (xml, "id",  id);
	g_object_get (so, "name", &tmp, NULL);
	// attribute "name" is required
	gsf_xml_out_add_cstr_unchecked (xml, "name", tmp ? tmp : "");
	g_free (tmp);
	gsf_xml_out_end_element (xml);
}

static void
xlsx_write_drawing_text (XLSXWriteState *stat, GsfXMLOut *xml, const char *text)
{
	gsf_xml_out_start_element (xml, "xdr:txBody");

	gsf_xml_out_start_element (xml, "a:bodyPr");
	// Something likely ought to go here
	gsf_xml_out_end_element (xml); /* </a:bodyPr> */

	gsf_xml_out_start_element (xml, "a:p");
	gsf_xml_out_start_element (xml, "a:r");

	gsf_xml_out_simple_element (xml, "a:t", text);

	gsf_xml_out_end_element (xml); /* </a:r> */
	gsf_xml_out_end_element (xml); /* </a:p> */

	gsf_xml_out_end_element (xml);  // </xdr:txBody>
}



static char const *
xlsx_write_drawing_objects (XLSXWriteState *state, GsfOutput *sheet_part,
			    GSList *objects, GHashTable *zorder)
{
	GSList *obj, *rId_ptr, *rIds = NULL;
	char *name;
	char const *rId;
	int chart_count = 0;
	int pic_count = 0;
	GsfOutput *drawing_part;
	GsfXMLOut *xml;

	name = g_strdup_printf ("drawing%u.xml", ++state->drawing_dir.count);
	drawing_part = gsf_outfile_new_child_full (xlsx_dir_get (&state->drawing_dir), name, FALSE,
		"content-type", "application/vnd.openxmlformats-officedocument.drawing+xml",
		NULL);
	g_free (name);

	rId = gsf_outfile_open_pkg_relate (GSF_OUTFILE_OPEN_PKG (drawing_part),
					   GSF_OUTFILE_OPEN_PKG (sheet_part),
					   ns_rel_draw);

	for (obj = objects ; obj != NULL ; obj = obj->next) {
		SheetObject *so = obj->data;
		const char *rId1;

		if (GNM_IS_SO_GRAPH (so)) {
			char *name = g_strdup_printf ("chart%u.xml", ++state->chart_dir.count);
			GsfOutput *chart_part = gsf_outfile_new_child_full
				(xlsx_dir_get (&state->chart_dir), name, FALSE,
				 "content-type",
				 "application/vnd.openxmlformats-officedocument.drawingml.chart+xml",
				 NULL);
			g_free (name);
			rId1 = gsf_outfile_open_pkg_relate (GSF_OUTFILE_OPEN_PKG (chart_part),
							    GSF_OUTFILE_OPEN_PKG (drawing_part),
							    ns_rel_chart);

			xlsx_write_chart (state, chart_part, so);
			gsf_output_close (chart_part);
			g_object_unref (chart_part);
		} else if (GNM_IS_SO_IMAGE (so)) {
			const char *ext;
			char *name, *mime_type;
			GsfOutput *image_part;
			GOImage *image;
			GOImageFormatInfo const *info;

			g_object_get (so, "image", &image, NULL);
			info = go_image_get_info (image);

			ext = info ? info->ext : "image";
			mime_type = info ? go_image_format_to_mime (info->name) : NULL;
			name = g_strdup_printf ("image%u.%s", ++state->media_dir.count, ext);
			image_part = gsf_outfile_new_child_full
				(xlsx_dir_get (&state->media_dir), name, FALSE,
				 "content-type", (mime_type ? mime_type : "image"),
				 NULL);
			rId1 = gsf_outfile_open_pkg_relate (GSF_OUTFILE_OPEN_PKG (image_part),
							    GSF_OUTFILE_OPEN_PKG (drawing_part),
							    ns_rel_image);
			sheet_object_write_image (so, NULL, -1, image_part, NULL);
			gsf_output_close (image_part);
			g_object_unref (image_part);
			g_free (name);
			g_free (mime_type);
			g_object_unref (image);
		} else {
			/* Lines etc. go here.  */
			rId1 = NULL;
		}

		rIds = g_slist_prepend (rIds, (gpointer)rId1);
	}
	rIds = g_slist_reverse (rIds);

	xml = gsf_xml_out_new (drawing_part);
	gsf_xml_out_start_element (xml, "xdr:wsDr");
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns:xdr", ns_ss_drawing);
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns:a", ns_drawing);
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns:r", ns_rel);
	if (state->with_extension)
		gsf_xml_out_add_cstr_unchecked (xml, "xmlns:gnmx", ns_gnm_ext);

	for (obj = objects, rId_ptr = rIds;
	     obj != NULL ;
	     obj = obj->next, rId_ptr = rId_ptr->next) {
		SheetObject *so = obj->data;
		const char *rId1 = rId_ptr->data;
		SheetObjectAnchor const *anchor = sheet_object_get_anchor (so);
		double res_pts[4] = {0.,0.,0.,0.};

		sheet_object_anchor_to_offset_pts (anchor, state->sheet, res_pts);

		switch (anchor->mode) {
		case GNM_SO_ANCHOR_TWO_CELLS:
			gsf_xml_out_start_element (xml, "xdr:twoCellAnchor");
			xlsx_write_object_anchor (xml, &anchor->cell_bound.start, "xdr:from",
						  res_pts[0], res_pts[1]);
			xlsx_write_object_anchor (xml, &anchor->cell_bound.end, "xdr:to",
						  res_pts[2], res_pts[3]);
			break;
		case GNM_SO_ANCHOR_ONE_CELL:
			gsf_xml_out_start_element (xml, "xdr:oneCellAnchor");
			xlsx_write_object_anchor (xml, &anchor->cell_bound.start, "xdr:from",
						  res_pts[0], res_pts[1]);
			gsf_xml_out_start_element (xml, "xdr:ext");
			gsf_xml_out_add_int  (xml, "cx",
								xlsx_pts_to_emu (anchor->offset[2]));
			gsf_xml_out_add_int (xml, "cy",
								xlsx_pts_to_emu (anchor->offset[3]));
			gsf_xml_out_end_element (xml);
			break;
		case GNM_SO_ANCHOR_ABSOLUTE:
			gsf_xml_out_start_element (xml, "xdr:absoluteAnchor");
			gsf_xml_out_start_element (xml, "xdr:pos");
			gsf_xml_out_add_int  (xml, "x",
								xlsx_pts_to_emu (anchor->offset[0]));
			gsf_xml_out_add_int (xml, "y",
								xlsx_pts_to_emu (anchor->offset[1]));
			gsf_xml_out_end_element (xml);
			gsf_xml_out_start_element (xml, "xdr:ext");
			gsf_xml_out_add_int  (xml, "cx",
								xlsx_pts_to_emu (anchor->offset[2]));
			gsf_xml_out_add_int (xml, "cy",
								xlsx_pts_to_emu (anchor->offset[3]));
			gsf_xml_out_end_element (xml);
			break;
		}

		if (GNM_IS_SO_GRAPH (so)) {
			gsf_xml_out_start_element (xml, "xdr:graphicFrame");
			gsf_xml_out_add_cstr_unchecked (xml, "macro", "");

			gsf_xml_out_start_element (xml, "xdr:nvGraphicFramePr");

			xlsx_write_nvpr (xml, so, ++chart_count);

			gsf_xml_out_simple_element (xml, "xdr:cNvGraphicFramePr", NULL);
			gsf_xml_out_end_element (xml); /* </xdr:nvGraphicFramePr> */

			gsf_xml_out_start_element (xml, "xdr:xfrm");

			gsf_xml_out_start_element (xml, "a:off");
			gsf_xml_out_add_int (xml, "x", 0);
			gsf_xml_out_add_int (xml, "y", 0);
			gsf_xml_out_end_element (xml); /* </a:off> */

			gsf_xml_out_start_element (xml, "a:ext");
			gsf_xml_out_add_int (xml, "cx", 0);
			gsf_xml_out_add_int (xml, "cy", 0);
			gsf_xml_out_end_element (xml); /* </a:ext> */

			gsf_xml_out_end_element (xml); /* </xdr:xfrm> */

			gsf_xml_out_start_element (xml, "a:graphic");
			gsf_xml_out_start_element (xml, "a:graphicData");
			gsf_xml_out_add_cstr_unchecked (xml, "uri", ns_chart);
			gsf_xml_out_start_element (xml, "c:chart");
			gsf_xml_out_add_cstr_unchecked (xml, "xmlns:c", ns_chart);

			gsf_xml_out_add_cstr_unchecked (xml, "r:id", rId1);
			gsf_xml_out_end_element (xml); /* </c:chart> */
			gsf_xml_out_end_element (xml); /* </a:graphicData> */
			gsf_xml_out_end_element (xml); /* </a:graphic> */
			gsf_xml_out_end_element (xml); /* </xdr:graphicFrame> */
		} else if (GNM_IS_SO_IMAGE (so)) {
			gsf_xml_out_start_element (xml, "xdr:pic");

			gsf_xml_out_start_element (xml, "xdr:nvPicPr");
			xlsx_write_nvpr (xml, so, ++pic_count);
			gsf_xml_out_start_element (xml, "xdr:cNvPicPr");
			gsf_xml_out_end_element (xml); /* </xdr:cNvPicPr> */
			gsf_xml_out_end_element (xml); /* </xdr:nvPicPr> */

			gsf_xml_out_start_element (xml, "xdr:blipFill");
			gsf_xml_out_start_element (xml, "a:blip");
			gsf_xml_out_add_cstr (xml, "r:embed", rId1);
			gsf_xml_out_end_element (xml); /* </a:blip> */
			gsf_xml_out_end_element (xml); /* </xdr:blipFill> */

			gsf_xml_out_start_element (xml, "xdr:spPr");
			gsf_xml_out_start_element (xml, "a:prstGeom");
			gsf_xml_out_add_cstr (xml, "prst", "rect");
			gsf_xml_out_simple_element (xml, "a:avLst", NULL);
			gsf_xml_out_end_element (xml); /* </a:prstGeom> */
			gsf_xml_out_end_element (xml); /* </xdr:spPr> */

			gsf_xml_out_end_element (xml); /* </xdr:pic> */
		} else if (GNM_IS_SO_LINE (so) ||
			   GNM_IS_SO_FILLED (so)) {
			GOStyle *style = NULL;
			XLSXStyleContext sctx;
			char *text = NULL;

			xlsx_style_context_init (&sctx, state);
			sctx.spPr_ns = "xdr";
			sctx.must_fill_line = TRUE;
			sctx.must_fill_fill = GNM_IS_SO_FILLED (so);
			sctx.flipH = (anchor->base.direction & GOD_ANCHOR_DIR_H_MASK) != GOD_ANCHOR_DIR_RIGHT;
			sctx.flipV = (anchor->base.direction & GOD_ANCHOR_DIR_V_MASK) != GOD_ANCHOR_DIR_DOWN;

			if (GNM_IS_SO_LINE (so)) {
				g_object_get (G_OBJECT (so),
					      "start-arrow", &sctx.start_arrow,
					      "end-arrow", &sctx.end_arrow,
					      NULL);
				sctx.shapename = "line";
			} else if (GNM_IS_SO_FILLED (so)) {
				gboolean oval;
				g_object_get (so, "is-oval", &oval, NULL);
				sctx.shapename = oval ? "ellipse" : "rect";
			} else {
				g_assert_not_reached ();
			}

			if (g_object_class_find_property (G_OBJECT_GET_CLASS (so), "style"))
				g_object_get (so, "style", &style, NULL);
			if (g_object_class_find_property (G_OBJECT_GET_CLASS (so), "text"))
				g_object_get (so, "text", &text, NULL);

			if (style) {
				gsf_xml_out_start_element (xml, "xdr:sp");
				gsf_xml_out_start_element (xml, "xdr:nvSpPr");
				xlsx_write_nvpr (xml, so, state->drawing_elem_id++);
				gsf_xml_out_start_element (xml, "xdr:cNvSpPr");
				gsf_xml_out_end_element (xml); /* </xdr:cNvSpPr> */
				gsf_xml_out_end_element (xml); /* </xdr:nvSpPr> */

				xlsx_write_go_style_full (xml, style, &sctx);

				if (text)
					xlsx_write_drawing_text (state, xml, text);

				gsf_xml_out_end_element (xml); /* </xdr:sp> */
				g_object_unref (style);
			}

			g_free (text);

			g_free (sctx.start_arrow);
			g_free (sctx.end_arrow);
		}

		gsf_xml_out_start_element (xml, "xdr:clientData");
		if (!sheet_object_get_print_flag (so))
			xlsx_add_bool (xml, "fPrintsWithSheet", FALSE);
		gsf_xml_out_end_element (xml); /* </xdr:clientData> */

		gsf_xml_out_end_element (xml); /* </xdr:twoCellAnchor> */
	}
	g_slist_free (rIds);

	gsf_xml_out_end_element (xml); /* </wsDr> */
	g_object_unref (xml);
	gsf_output_close (drawing_part);
	g_object_unref (drawing_part);

	return rId;
}

static int
cb_radio_value_cmp (void const *ptr_a, void const *ptr_b)
{
	GnmValue const *va = sheet_widget_radio_button_get_value ((SheetObject *)ptr_a);
	GnmValue const *vb = sheet_widget_radio_button_get_value ((SheetObject *)ptr_b);
	return value_cmp (&va, &vb);
}

static GHashTable *
xlsx_preprocess_radio (XLSXWriteState *state, GSList *objects)
{
	GHashTable *radio_by_link = g_hash_table_new_full
		((GHashFunc)gnm_expr_top_hash, (GEqualFunc)gnm_expr_top_equal,
		 (GDestroyNotify)gnm_expr_top_unref, (GDestroyNotify)g_slist_free);
	GHashTableIter hiter;
	gpointer hkey, hval;
	GSList *obj;
	GnmParsePos pp0;

	parse_pos_init_sheet (&pp0, state->sheet);

	/* Collect objects on a per-link basis. */
	for (obj = objects ; obj != NULL ; obj = obj->next) {
		SheetObject *so = obj->data;
		GSList *sos;
		GnmExprTop const *tlink;

		if (!GNM_IS_SOW_RADIO_BUTTON (so))
			continue;

		tlink = sheet_widget_radio_button_get_link (so);
		if (!tlink)
			continue;

		sos = g_hash_table_lookup (radio_by_link, tlink);
		if (sos) {
			gnm_expr_top_unref (tlink);
			sos->next = g_slist_prepend (sos->next, so);
		} else {
			sos = g_slist_prepend (sos, so);
			g_hash_table_insert (radio_by_link, (gpointer)tlink, sos);
		}
	}
	/* Sort by value. */
	g_hash_table_iter_init (&hiter, radio_by_link);
	while (g_hash_table_iter_next (&hiter, &hkey, &hval)) {
		GSList *sos = g_slist_copy (hval);
		int i;

		sos = g_slist_sort (sos, cb_radio_value_cmp);
		g_hash_table_iter_replace (&hiter, sos);

		for (i = 1; sos; sos = sos->next, i++) {
			SheetObject *so = sos->data;
			GnmValue const *v = sheet_widget_radio_button_get_value (so);
			if (!v || !VALUE_IS_FLOAT (v) || value_get_as_float (v) != i) {
				char *etxt = gnm_expr_top_as_string (hkey, &pp0, state->sheet->convs);
				g_printerr ("One or more radio buttons linked to %s has non-sequential values, first one [%s]\n",
					    etxt, value_peek_string (v));
				g_free (etxt);
				break;
			}
		}
	}

	return radio_by_link;
}


static void
xlsx_write_legacy_object (XLSXWriteState *state, GsfXMLOut *xml, SheetObject *so, GHashTable *zorder, GHashTable *radio_by_link)
{
	const char *otype = NULL;
	GnmExprTop const *tlink = NULL;
	GnmExprTop const *trange = NULL;
	double res_pts[4] = {0.,0.,0.,0.};
	SheetObjectAnchor const *anchor = sheet_object_get_anchor (so);
	GtkAdjustment *adj = NULL;
	int horiz = -1;
	int checked = -1;
	int selected = -1;
	const char *seltype = NULL;
	gboolean has_text_prop =
		g_object_class_find_property (G_OBJECT_GET_CLASS (so), "text") != NULL;
	char *text = NULL;
	GnmParsePos pp0;
	const char *shapetype = "#_x0000_t201";
	gboolean firstbutton = FALSE;
	gboolean movewithcells = FALSE;
	gboolean sizewithcells = FALSE;
	gboolean rowandcolumn = FALSE;
	gboolean hidden = FALSE;
	int autofill = -1;
	char *anchor_text = NULL;
	const char *fillcolor = NULL;

	parse_pos_init_sheet (&pp0, state->sheet);

	sheet_object_position_pts_get (so, res_pts);

	if (GNM_IS_SOW_SCROLLBAR (so) || GNM_IS_SOW_SLIDER (so)) {
		otype = "Scroll";
		tlink = sheet_widget_adjustment_get_link (so);
		adj = sheet_widget_adjustment_get_adjustment (so);
		g_object_ref (adj);
		horiz = sheet_widget_adjustment_get_horizontal (so);
	} else if (GNM_IS_SOW_SPINBUTTON (so)) {
		otype = "Spin";
		tlink = sheet_widget_adjustment_get_link (so);
		adj = sheet_widget_adjustment_get_adjustment (so);
		g_object_ref (adj);
	} else if (GNM_IS_SOW_BUTTON (so)) {
		otype = "Button";
		tlink = sheet_widget_button_get_link (so);
	} else if (GNM_IS_SOW_RADIO_BUTTON (so)) {
		gboolean c;
		GSList *sos;
		otype = "Radio";
		tlink = sheet_widget_radio_button_get_link (so);
		sos = tlink ? g_hash_table_lookup (radio_by_link, tlink) : NULL;
		firstbutton = (!sos || so == sos->data);
		g_object_get (so, "active", &c, NULL);
		checked = c;
	} else if (GNM_IS_SOW_CHECKBOX (so)) {
		gboolean c;
		otype = "Checkbox";
		tlink = sheet_widget_checkbox_get_link (so);
		g_object_get (so, "active", &c, NULL);
		checked = c;
	} else if (GNM_IS_SOW_COMBO (so)) {
		otype = "Drop";
		tlink = sheet_widget_list_base_get_result_link (so);
		trange = sheet_widget_list_base_get_content_link (so);
		adj = sheet_widget_list_base_get_adjustment (so);
		// selected = ;
	} else if (GNM_IS_SOW_LIST (so)) {
		otype = "List";
		tlink = sheet_widget_list_base_get_result_link (so);
		trange = sheet_widget_list_base_get_content_link (so);
		adj = sheet_widget_list_base_get_adjustment (so);
		// selected = ;
		seltype = "Single";
	} else if (GNM_IS_CELL_COMMENT (so)) {
		int LeftColumn, LeftOffset, TopRow, TopOffset;
		int RightColumn, RightOffset, BottomRow, BottomOffset;

		otype = "Note";
		shapetype = "#_x0000_t202";
		movewithcells = TRUE;
		sizewithcells = TRUE;
		rowandcolumn = TRUE;
		has_text_prop = FALSE; // We have it, but need to inhibit it
		autofill = FALSE;
		hidden = TRUE;
		fillcolor = "#ffffc0"; // Not a great place to put this

		LeftColumn = anchor->cell_bound.start.col + 1;
		LeftOffset = 15;
		TopRow = MAX (0, anchor->cell_bound.start.row - 1);
		TopOffset = 10;
		RightColumn = LeftColumn + 2;
		RightOffset = 15;
		BottomRow = TopRow + 4;
		BottomOffset = 4;

		anchor_text = g_strdup_printf ("%d, %d, %d, %d, %d, %d, %d, %d",
					       LeftColumn, LeftOffset,
					       TopRow, TopOffset,
					       RightColumn, RightOffset,
					       BottomRow, BottomOffset);
	} else {
		g_assert_not_reached ();
	}

	gsf_xml_out_start_element (xml, "v:shape");
	gsf_xml_out_add_cstr (xml, "type", shapetype);
	if (fillcolor)
		gsf_xml_out_add_cstr (xml, "fillcolor", fillcolor);

	{
		int z = GPOINTER_TO_INT (g_hash_table_lookup (zorder, so));
		GString *str = g_string_new (NULL);
		g_string_append (str, "position:absolute;");
		g_string_append_printf (str, "margin-left:%.2fpt;", res_pts[0]);
		g_string_append_printf (str, "margin-top:%.2fpt;", res_pts[1]);
		g_string_append_printf (str, "width:%.2fpt;", res_pts[2] - res_pts[0]);
		g_string_append_printf (str, "height:%.2fpt;", res_pts[3] - res_pts[1]);
		g_string_append_printf (str, "z-index:%d;", z);
		if (hidden)
			g_string_append (str, "visibility:hidden;");
		gsf_xml_out_add_cstr (xml, "style", str->str);
		g_string_free (str, TRUE);
	}

	if (has_text_prop)
		g_object_get (so, "text", &text, NULL);
	if (text) {
		gsf_xml_out_start_element (xml, "v:textbox");
		gsf_xml_out_start_element (xml, "div");
		gsf_xml_out_add_cstr (xml, NULL, text);
		gsf_xml_out_end_element (xml);  /* </div> */
		gsf_xml_out_end_element (xml);  /* </v:textbox> */
		g_free (text);
	}

	gsf_xml_out_start_element (xml, "x:ClientData");
	gsf_xml_out_add_cstr_unchecked (xml, "ObjectType", otype);
	gsf_xml_out_simple_element (xml, "x:Anchor", anchor_text);
	g_free (anchor_text);
	if (checked != -1)
		gsf_xml_out_simple_int_element (xml, "x:Checked", checked);
	if (tlink) {
		char *s = gnm_expr_top_as_string (tlink, &pp0, state->convs);
		gsf_xml_out_start_element (xml, "x:FmlaLink");
		gsf_xml_out_add_cstr (xml, NULL, s);
		gsf_xml_out_end_element (xml);  /* </x:FmlaLink> */
		g_free (s);
		gnm_expr_top_unref (tlink);
	}
	if (firstbutton)
		gsf_xml_out_simple_element (xml, "x:FirstButton", NULL);
	if (adj) {
		gsf_xml_out_simple_float_element (xml, "x:Val",
						  gtk_adjustment_get_value (adj), -1);
		gsf_xml_out_simple_float_element (xml, "x:Min",
						  gtk_adjustment_get_lower (adj), -1);
		gsf_xml_out_simple_float_element (xml, "x:Max",
						  gtk_adjustment_get_upper (adj), -1);
		gsf_xml_out_simple_float_element (xml, "x:Inc",
						  gtk_adjustment_get_step_increment (adj), -1);
		gsf_xml_out_simple_float_element (xml, "x:Page",
						  gtk_adjustment_get_page_increment (adj), -1);
		g_object_unref (adj);
	}
	if (trange) {
		char *s = gnm_expr_top_as_string (trange, &pp0, state->convs);
		gsf_xml_out_simple_element (xml, "x:FmlaRange", s);
		g_free (s);
		gnm_expr_top_unref (trange);
	}
	if (selected >= 1)
		gsf_xml_out_simple_int_element (xml, "x:Sel", selected);
	if (seltype)
		gsf_xml_out_simple_element (xml, "x:SelType", seltype);
	if (horiz >= 0)
		gsf_xml_out_simple_element (xml, "x:Horiz", horiz ? "t" : "f");

	if (movewithcells)
		gsf_xml_out_simple_element (xml, "x:MoveWithCells", NULL);
	if (sizewithcells)
		gsf_xml_out_simple_element (xml, "x:SizeWithCells", NULL);

	if (autofill >= 0)
		gsf_xml_out_simple_element (xml, "x:AutoFill", autofill ? "True" : "False");

	if (rowandcolumn) {
		gsf_xml_out_simple_int_element (xml, "x:Row", anchor->cell_bound.start.row);
		gsf_xml_out_simple_int_element (xml, "x:Column", anchor->cell_bound.start.col);
	}

	gsf_xml_out_end_element (xml);  /* </x:ClientData> */

	gsf_xml_out_end_element (xml);  /* </v:shape> */
}

static char const *
xlsx_write_legacy_drawing_objects (XLSXWriteState *state, GsfOutput *sheet_part,
				   GSList *objects, GHashTable *zorder)
{
	GSList *obj;
	char *name;
	char const *rId;
	GsfOutput *drawing_part;
	GsfXMLOut *xml;
	const char *shapetype = "#_x0000_t201";
	GHashTable *radio_by_link;

	/*
	 * Radio buttons need extra work.  Excel doesn't have our concept of
	 * a value field, so the buttons need to be written in value order.
	 */
	radio_by_link = xlsx_preprocess_radio (state, objects);

	name = g_strdup_printf ("vmlDrawing%u.vml", ++state->legacy_drawing_dir.count);
	/* Note: we use drawing.dir here.  */
	drawing_part = gsf_outfile_new_child_full (xlsx_dir_get (&state->drawing_dir), name, FALSE,
						   "content-type", NULL,
						   NULL);
	g_free (name);

	rId = gsf_outfile_open_pkg_relate (GSF_OUTFILE_OPEN_PKG (drawing_part),
					   GSF_OUTFILE_OPEN_PKG (sheet_part),
					   ns_rel_leg_draw);

	xml = gsf_xml_out_new (drawing_part);
	gsf_xml_out_start_element (xml, "xml");
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns:v", ns_vml);
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns:o", ns_leg_office);
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns:x", ns_leg_excel);

	gsf_xml_out_start_element (xml, "v:shapetype");
	gsf_xml_out_add_cstr (xml, "id", shapetype);
	gsf_xml_out_end_element (xml);  /* </v:shapetype> */

	for (obj = objects ; obj != NULL ; obj = obj->next) {
		SheetObject *so = obj->data;

		if (GNM_IS_SOW_RADIO_BUTTON (so)) {
			GnmExprTop const *tlink = sheet_widget_radio_button_get_link (so);
			if (tlink) {
				GSList *sos = g_hash_table_lookup (radio_by_link, tlink);
				gnm_expr_top_unref (tlink);
				if (so == sos->data) {
					for (; sos; sos = sos->next) {
						so = sos->data;
						xlsx_write_legacy_object (state, xml, so, zorder, radio_by_link);
					}

				}
				continue;
			}
		}

		xlsx_write_legacy_object (state, xml, so, zorder, radio_by_link);
	}

	gsf_xml_out_end_element (xml);  /* </xml> */
	g_object_unref (xml);
	gsf_output_close (drawing_part);
	g_object_unref (drawing_part);

	g_hash_table_destroy (radio_by_link);

	return rId;
}
