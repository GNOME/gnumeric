/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
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
xlsx_write_chart_int (GsfXMLOut *xml, char const *name, int def_val, int val)
{
	gsf_xml_out_start_element (xml, name);
	if (val != def_val)
		gsf_xml_out_add_int (xml, "val", val);
	gsf_xml_out_end_element (xml);
}

static void
xlsx_write_chart_uint (GsfXMLOut *xml, char const *name, int def_val, int val)
{
	gsf_xml_out_start_element (xml, name);
	if (val != def_val)
		gsf_xml_out_add_uint (xml, "val", val);
	gsf_xml_out_end_element (xml);
}

static void
xlsx_write_chart_float (GsfXMLOut *xml, char const *name, double def_val, double val)
{
	gsf_xml_out_start_element (xml, name);
	if (val != def_val)
		gsf_xml_out_add_float (xml, "val", val, -1);
	gsf_xml_out_end_element (xml);
}

static void
xlsx_write_plot_1_5_type (GsfXMLOut *xml, GogObject const *plot, gboolean is_barcol)
{
	char const *type;
	g_object_get (G_OBJECT (plot), "type", &type, NULL);
	if (0 == strcmp (type, "as_percentage"))
		type = "percentStacked";
	else if (0 == strcmp (type, "stacked"))
		type = "stacked";
	else
		type = (is_barcol)? "clustered": "standard";
	xlsx_write_chart_cstr_unchecked (xml, "c:grouping", type);
}

static void
xlsx_write_series_dim (XLSXWriteState *state, GsfXMLOut *xml, GogSeries const *series,
		       char const *name, GogMSDimType ms_type)
{
	GogSeriesDesc const *desc = &gog_plot_description (gog_series_get_plot (series))->series;
	int dim;
	GOData const *dat;

	for (dim = -1; dim < (int) desc->num_dim; dim++)
		if (desc->dim[dim].ms_type == ms_type)
			break;
	if (dim == (int) desc->num_dim)
		return;
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
	int alpha = GO_COLOR_UINT_A (color);
	gsf_xml_out_start_element (xml, "a:srgbClr");
	gsf_xml_out_add_cstr_unchecked (xml, "val", buf);
	g_free (buf);
	if (alpha < 255) {
		gsf_xml_out_start_element (xml, "a:alpha");
		gsf_xml_out_add_int (xml, "val", alpha * 100000 / 255);
		gsf_xml_out_end_element (xml);
	}
	gsf_xml_out_end_element (xml);
}

static void
xlsx_write_go_style_full (GsfXMLOut *xml, GOStyle *style,
			  gboolean def_has_markers)
{
	gsf_xml_out_start_element (xml, "c:spPr");

	if ((style->interesting_fields & GO_STYLE_FILL) &&
	    style->fill.type != GO_STYLE_FILL_NONE) {/* TODO add tests for transparent backgrounds */
		switch (style->fill.type) {
		default :
			g_warning ("invalid fill type, saving as none");
		case GO_STYLE_FILL_IMAGE:
			/* FIXME: export image */
		case GO_STYLE_FILL_PATTERN:
			switch (style->fill.pattern.pattern) {
			case GO_PATTERN_SOLID:
				if (!style->fill.auto_back) {
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
				}
				break;
			}
			break;
		case GO_STYLE_FILL_GRADIENT:
			break;
		}
	}

	if ((style->interesting_fields & (GO_STYLE_LINE | GO_STYLE_OUTLINE)) &&
	    (!style->line.auto_dash ||
	     !style->line.auto_width ||
	     !style->line.auto_color)) {
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
		gboolean is_none = (style->line.dash_type == GO_LINE_NONE);

		gsf_xml_out_start_element (xml, "a:ln");
		if (is_none) {
			/* Special meaning of zero width  */
			gsf_xml_out_add_int (xml, "w", 0);
		} else if (!style->line.auto_width && style->line.width > 0)
			gsf_xml_out_add_int (xml, "w", style->line.width * 12700);

		if (!style->line.auto_color) {
			gsf_xml_out_start_element (xml, "a:solidFill");
			xlsx_write_rgbarea (xml, style->line.color);
			gsf_xml_out_end_element (xml);
		}

		if (!style->line.auto_dash &&
		    style->line.dash_type < G_N_ELEMENTS (dashes) &&
		    dashes[style->line.dash_type]) {
			xlsx_write_chart_cstr_unchecked (xml,
							 "a:prstDash",
							 dashes[style->line.dash_type]);
		}

		gsf_xml_out_end_element (xml);
	}

	gsf_xml_out_end_element (xml);  /* "c:spPr" */

	if (style->interesting_fields & GO_STYLE_MARKER) {
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
		static gint8 nqturns[] = { 0, 0, 0, 2, 0, +1, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
		static gint8 flipH[] =   { 0, 0, 0, 0, 0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0 };
		gboolean need_spPr;
		GOMarkerShape s = style->marker.auto_shape
			? (def_has_markers ? GO_MARKER_MAX : GO_MARKER_NONE)
			: go_marker_get_shape (style->marker.mark);

		gsf_xml_out_start_element (xml, "c:marker");

		xlsx_write_chart_cstr_unchecked
			(xml, "c:symbol",
			 (s < G_N_ELEMENTS (markers) && markers[s]
			  ? markers[s]
			  : "auto"));

		/* We don't have an auto_size flag */
		if (TRUE) {
			int def = 5, s = go_marker_get_size (style->marker.mark);
			xlsx_write_chart_int (xml, "c:size", def, s);
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

		gsf_xml_out_end_element (xml);
	}
}

static void
xlsx_write_go_style (GsfXMLOut *xml, GOStyle *style)
{
	xlsx_write_go_style_full (xml, style, FALSE);
}

static void
xlsx_write_chart_text (XLSXWriteState *state, GsfXMLOut *xml,
		       GOData *data, GogObject const *label)
{
	/* I don't really know what I am doing here.  */
	char *text = go_data_get_scalar_string (data);
	GOStyle *style = go_styled_object_get_style (GO_STYLED_OBJECT (label));
	gboolean has_font_color = ((style->interesting_fields & GO_STYLE_FONT) &&
				   !style->font.auto_color);
	gboolean has_font = ((style->interesting_fields & GO_STYLE_FONT) &&
			     TRUE /* !style->font.auto_font */);
	gboolean allow_wrap;

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
		GOFont const *font = style->font.font;
		PangoFontDescription *desc = font->desc;

		gsf_xml_out_start_element (xml, "a:rPr");
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
		gsf_xml_out_end_element (xml); /* </a:rPr> */
	}

	gsf_xml_out_simple_element (xml, "a:t", text);

	gsf_xml_out_end_element (xml); /* </a:r> */
	gsf_xml_out_end_element (xml); /* </a:p> */

	gsf_xml_out_end_element (xml); /* </c:rich> */
	gsf_xml_out_end_element (xml); /* </c:tx> */

	xlsx_write_go_style (xml, style);

	g_free (text);
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
xlsx_write_axis (XLSXWriteState *state, GsfXMLOut *xml, GogAxis *axis, GogAxisType at)
{
	GogAxis *crossed = gog_axis_base_get_crossed_axis (GOG_AXIS_BASE (axis));
	GogAxisPosition pos;
	GogGridLine *grid;
	GogObject *label;
	GOFormat *format;
	double d;
	gboolean user_defined;

#ifdef DEBUG_AXIS
	g_printerr ("Writing axis %s.  (discrete = %d)\n",
		    gog_object_get_name (GOG_OBJECT (axis)),
		    gog_axis_is_discrete (axis));
#endif

	if (gog_axis_is_discrete (axis))
		gsf_xml_out_start_element (xml, "c:catAx");
	else
		gsf_xml_out_start_element (xml, "c:valAx");
	xlsx_write_chart_uint (xml, "c:axId", 0, xlsx_get_axid (state, axis));
	gsf_xml_out_start_element (xml, "c:scaling");
	xlsx_write_chart_cstr_unchecked (xml, "c:orientation", gog_axis_is_inverted (axis)? "maxMin": "minMax");
	d = gog_axis_get_entry (axis, GOG_AXIS_ELEM_MAX, &user_defined);
	if (user_defined) xlsx_write_chart_float (xml, "c:max", go_nan, d);
	d = gog_axis_get_entry (axis, GOG_AXIS_ELEM_MIN, &user_defined);
	if (user_defined) xlsx_write_chart_float (xml, "c:min", go_nan, d);
	gsf_xml_out_end_element (xml);
	xlsx_write_chart_uint (xml, "c:delete", 1, 0);
	/* FIXME position might be "t" or "r" */
	xlsx_write_chart_cstr_unchecked (xml, "c:axPos", (at == GOG_AXIS_X || at == GOG_AXIS_CIRCULAR)? "b": "l");

	/* grids */
	grid = gog_axis_get_grid_line (axis, TRUE);
	if (grid) {
		gsf_xml_out_start_element (xml, "c:majorGridlines");
		xlsx_write_go_style (xml, go_styled_object_get_style (GO_STYLED_OBJECT (grid)));
		gsf_xml_out_end_element (xml);
	}
	grid = gog_axis_get_grid_line (axis, FALSE);
	if (grid) {
		gsf_xml_out_start_element (xml, "c:minorGridlines");
		xlsx_write_go_style (xml, go_styled_object_get_style (GO_STYLED_OBJECT (grid)));
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
		gboolean mati, miti, mato, mito;
		static const char *marks[4] = { "none", "in", "out", "cross" };

		g_object_get (G_OBJECT (axis),
			      "major-tick-in", &mati,
			      "minor-tick-in", &miti,
			      "major-tick-out", &mato,
			      "minor-tick-out", &mito,
			      NULL);
		xlsx_write_chart_cstr_unchecked (xml, "c:majorTickMark",
						 marks[2 * mato + mati]);
		xlsx_write_chart_cstr_unchecked (xml, "c:minorTickMark",
						 marks[2 * mito + miti]);
	}

	xlsx_write_go_style (xml, go_styled_object_get_style (GO_STYLED_OBJECT (axis)));

	xlsx_write_chart_int (xml, "c:crossAx", 0, xlsx_get_axid (state, crossed));
	g_object_get (G_OBJECT (axis), "pos", &pos, NULL);
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
			xlsx_write_chart_float (xml, "c:crossesAt", 0., cross);
		break;
	}
	}

	d = gog_axis_get_entry (axis, GOG_AXIS_ELEM_MAJOR_TICK, &user_defined);
	if (user_defined && d > 0) xlsx_write_chart_float (xml, "c:majorUnit", go_nan, d);
	d = gog_axis_get_entry (axis, GOG_AXIS_ELEM_MINOR_TICK, &user_defined);
	if (user_defined && d > 0) xlsx_write_chart_float (xml, "c:minorUnit", go_nan, d);

	/* finished with axis */
	gsf_xml_out_end_element (xml);
}


static void
xlsx_write_one_plot (XLSXWriteState *state, GsfXMLOut *xml, GogObject const *chart, GogObject const *plot)
{
	double explosion = 0.;
	gboolean vary_by_element;
	GogAxisType axis_type[3] = {GOG_AXIS_X, GOG_AXIS_Y, GOG_AXIS_UNKNOWN};
	unsigned i;
	XLSXPlotType plot_type;
	const char *plot_type_name;
	GSList const *series;
	unsigned count;
	gboolean use_xy = FALSE;
	gboolean set_smooth = FALSE;
	gboolean has_markers = FALSE;

	g_object_get (G_OBJECT (plot),
		      "vary-style-by-element", &vary_by_element,
		      NULL);
	plot_type_name = G_OBJECT_TYPE_NAME (plot);
	plot_type = xlsx_plottype_from_type_name (plot_type_name);

	switch (plot_type) {
	default:
	case XLSX_PT_UNKNOWN:
		g_warning ("unexpected plot type %s", plot_type_name);
		return;

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
		break;
	}

	case XLSX_PT_GOGLINEPLOT:
		gsf_xml_out_start_element (xml, "c:lineChart");
		xlsx_write_plot_1_5_type (xml, plot, FALSE);
		xlsx_write_chart_bool (xml, "c:varyColors", vary_by_element);
		set_smooth = TRUE;
		g_object_get (G_OBJECT (plot),
		              "default-style-has-markers", &has_markers,
		              NULL);
		break;

	case XLSX_PT_GOGPIEPLOT:
	case XLSX_PT_GOGRINGPLOT:
		if (plot_type == XLSX_PT_GOGRINGPLOT) {
			gint16 center;
			double center_size;
			gsf_xml_out_start_element (xml, "c:doughnutChart");
			g_object_get (G_OBJECT (plot), "center-size", &center_size, NULL);
			center = (int)floor (center_size * 100. + .5);
			xlsx_write_chart_int (xml, "c:holeSize", 10,
					      CLAMP (center, 10, 90));
		} else
			gsf_xml_out_start_element (xml, "c:pieChart");

		xlsx_write_chart_bool (xml, "c:varyColors", vary_by_element);
#if 0
		double default_separation = 0.;
		/* handled in series ? */
		"default-separation",	&default_separation,
		xlsx_write_chart_int (xml, "c:explosion", 0, default_separation);
#endif
		axis_type[0] = axis_type[1] = GOG_AXIS_UNKNOWN;
		g_object_get (G_OBJECT (plot), "default-separation", &explosion, NULL);
		break;

	case XLSX_PT_GOGRADARPLOT:
	case XLSX_PT_GOGRADARAREAPLOT:
		gsf_xml_out_start_element (xml, "c:radarChart");
		xlsx_write_chart_cstr_unchecked (xml, "c:radarStyle", "standard");
		axis_type[0] = GOG_AXIS_CIRCULAR;
		axis_type[1] = GOG_AXIS_RADIAL;
		break;

	case XLSX_PT_GOGBUBBLEPLOT:
		gsf_xml_out_start_element (xml, "c:bubbleChart");
		xlsx_write_chart_bool (xml, "c:varyColors", vary_by_element);
		use_xy = TRUE;
		break;

	case XLSX_PT_GOGXYPLOT: {
		gboolean has_lines, use_splines;
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
		gsf_xml_out_start_element (xml, "c:surfaceChart");
		break;
	}

	count = 0;
	for (series = gog_plot_get_series (GOG_PLOT (plot));
	     NULL != series;
	     series = series->next) {
		GogSeries *ser = series->data;
		GSList *l, *children;
		GOStyle *style = go_styled_object_get_style (GO_STYLED_OBJECT (ser));

		gsf_xml_out_start_element (xml, "c:ser");

		xlsx_write_chart_int (xml, "c:idx", -1, count);
		xlsx_write_chart_int (xml, "c:order", -1, count);
		xlsx_write_series_dim (state, xml, ser, "c:tx", GOG_MS_DIM_LABELS);
		if (!vary_by_element) /* FIXME: we might loose some style elements */
			xlsx_write_go_style_full (xml, style, has_markers);

		children = gog_object_get_children (GOG_OBJECT (ser), NULL);
		for (l = children; l; l = l->next) {
			GogObject *trend = l->data;
			const char *trend_type_name = G_OBJECT_TYPE_NAME (trend);
			const char *trend_type;
			GogObject *eq;
			GOData *dat;
			char *name;

			if (!GOG_IS_TREND_LINE (trend))
				continue;

			if (strcmp (trend_type_name, "GogExpRegCurve") == 0)
				trend_type = "exp";
			else if (strcmp (trend_type_name, "GogLinRegCurve") == 0)
				trend_type = "linear";
			else if (strcmp (trend_type_name, "GogLogRegCurve") == 0)
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
			name = go_data_get_scalar_string (dat);
			gsf_xml_out_simple_element (xml, "c:name", name);
			g_free (name);
			xlsx_write_go_style (xml, go_styled_object_get_style (GO_STYLED_OBJECT (trend)));
			xlsx_write_chart_cstr_unchecked (xml, "c:trendlineType", trend_type);
			gsf_xml_out_end_element (xml); /* </c:trendline> */

			eq = gog_object_get_child_by_name (trend, "Equation");
			if (eq) {
				gboolean has_r2, has_eq;
				g_object_get (eq, "show-r2", &has_r2, "show-eq", &has_eq, NULL);
				if (has_r2)
					xlsx_write_chart_bool (xml, "c:dispRSqr", TRUE);
				if (has_eq)
					xlsx_write_chart_bool (xml, "c:dispEq", TRUE);
			}
		}
		g_slist_free (children);

		if (explosion > 0.)
			xlsx_write_chart_uint (xml, "c:explosion", 0, (unsigned) (explosion * 100));
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
		xlsx_write_chart_int (xml, "c:gapWidth", 150, CLAMP (gap_percentage, 0, 500));

		/* Spec says add "%" at end; XL cannot handle that. */
		xlsx_write_chart_int (xml, "c:overlap", 0, CLAMP (overlap_percentage, 0, 100));
		break;
	}

	case XLSX_PT_GOGPIEPLOT:
	case XLSX_PT_GOGRINGPLOT: {
		double initial_angle = 0;
		g_object_get (G_OBJECT (plot),
			      "initial-angle", &initial_angle,
			      NULL);
		xlsx_write_chart_int (xml, "c:firstSliceAng", 0, (int) initial_angle);
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

	default:
		break; /* Nothing */
	}

	/* write axes Ids */
	for (i = 0; i < 3; i++)
		if (axis_type[i] != GOG_AXIS_UNKNOWN)
			xlsx_write_chart_uint (xml, "c:axId", 0, xlsx_get_axid (state, gog_plot_get_axis (GOG_PLOT (plot), axis_type[i])));


	gsf_xml_out_end_element (xml);

	/* Write axes */
	/* first category axis */
	/* FIXME: might be a date axis? */
	for (i = 0; i < 3; i++) {
		if (axis_type[i] != GOG_AXIS_UNKNOWN) {
			GSList *axes = gog_chart_get_axes (GOG_CHART (chart), axis_type[i]), *ptr;
			for (ptr = axes; ptr; ptr = ptr->next) {
				GogAxis *axis = ptr->data;
				xlsx_write_axis (state, xml, axis, axis_type[i]);
			}
		}
	}
}

static void
xlsx_write_plots (XLSXWriteState *state, GsfXMLOut *xml, GogObject const *chart)
{
	GSList *plots;
	GogObject const *plot;

	plots = gog_object_get_children
		(GOG_OBJECT (chart),
		 gog_object_find_role_by_name (GOG_OBJECT (chart), "Plot"));
	if (plots != NULL && plots->data != NULL) {
		plot = plots->data;
		if (plots->next != NULL) {
			int n = g_slist_length (plots) - 1;
			g_warning ("Dropping %d plots from a chart.", n);
		}
		xlsx_write_one_plot (state, xml, chart, plot);
	}
	g_slist_free (plots);
}

static void
xlsx_write_one_chart (XLSXWriteState *state, GsfXMLOut *xml, GogObject const *chart)
{
	GogObject const *obj;

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

	gsf_xml_out_start_element (xml, "c:plotArea");
	/* save grid style here */

	xlsx_write_plots (state, xml, chart);

	obj = gog_object_get_child_by_name (GOG_OBJECT (chart), "Backplane");
	if (obj)
		xlsx_write_go_style (xml, go_styled_object_get_style (GO_STYLED_OBJECT (obj)));

	gsf_xml_out_end_element (xml); /* </c:plotArea> */

	if ((obj = gog_object_get_child_by_name (chart, "Legend"))) {
		gsf_xml_out_start_element (xml, "c:legend");
		gsf_xml_out_end_element (xml); /* </c:legend> */
	}
	gsf_xml_out_end_element (xml); /* </c:chart> */

	xlsx_write_go_style (xml, go_styled_object_get_style (GO_STYLED_OBJECT (chart)));
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

	graph = sheet_object_graph_get_gog (so);
	if (graph != NULL) {
		GogObjectRole const *role = gog_object_find_role_by_name (GOG_OBJECT (graph), "Chart");
		GSList *charts = gog_object_get_children (GOG_OBJECT (graph), role);
		if (charts != NULL) {
			GogObject const	*chart1 = charts->data;
			xlsx_write_one_chart (state, xml, chart1);
			if (charts->next)
				g_warning ("Dropping %d charts on the floor!",
					   g_slist_length (charts));
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

static char const *
xlsx_write_objects (XLSXWriteState *state, GsfOutput *sheet_part, GSList *objects)
{
	GSList *obj, *chart_id, *chart_ids = NULL;
	char *name, *tmp;
	char const *rId, *rId1;
	int count = 1;
	GsfOutput *drawing_part, *chart_part;
	GsfXMLOut *xml;
	SheetObjectAnchor const *anchor;
	double res_pts[4] = {0.,0.,0.,0.};

	if (NULL == state->drawing.dir)
		state->drawing.dir = (GsfOutfile *)gsf_outfile_new_child (state->xl_dir, "drawings", TRUE);
	if (NULL == state->chart.dir)
		state->chart.dir = (GsfOutfile *)gsf_outfile_new_child (state->xl_dir, "charts", TRUE);

	name = g_strdup_printf ("drawing%u.xml", ++state->drawing.count);
	drawing_part = gsf_outfile_new_child_full (state->drawing.dir, name, FALSE,
		"content-type", "application/vnd.openxmlformats-officedocument.drawing+xml",
		NULL);
	g_free (name);

	rId = gsf_outfile_open_pkg_relate (GSF_OUTFILE_OPEN_PKG (drawing_part),
		GSF_OUTFILE_OPEN_PKG (sheet_part), ns_rel_draw);

	objects = sheet_objects_get (state->sheet, NULL, SHEET_OBJECT_GRAPH_TYPE);
	for (obj = objects ; obj != NULL ; obj = obj->next) {
		char *name = g_strdup_printf ("chart%u.xml", ++state->chart.count);
		chart_part = gsf_outfile_new_child_full (state->chart.dir, name, FALSE,
			"content-type", "application/vnd.openxmlformats-officedocument.drawingml.chart+xml",
			NULL);
		g_free (name);
		rId1 = gsf_outfile_open_pkg_relate (GSF_OUTFILE_OPEN_PKG (chart_part),
			GSF_OUTFILE_OPEN_PKG (drawing_part), ns_rel_chart);

		chart_ids = g_slist_prepend (chart_ids, (gpointer)rId1);

		xlsx_write_chart (state, chart_part, obj->data);
		gsf_output_close (chart_part);
		g_object_unref (chart_part);
	}

	xml = gsf_xml_out_new (drawing_part);
	gsf_xml_out_start_element (xml, "xdr:wsDr");
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns:xdr", ns_ss_drawing);
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns:a", ns_drawing);

	chart_id = g_slist_reverse (chart_ids);
	for (obj = objects; obj != NULL ; obj = obj->next, chart_id = chart_id->next) {
		anchor = sheet_object_get_anchor (obj->data);
		sheet_object_anchor_to_offset_pts (anchor, state->sheet, res_pts);

		gsf_xml_out_start_element (xml, "xdr:twoCellAnchor");
		xlsx_write_object_anchor (xml, &anchor->cell_bound.start, "xdr:from",
					  res_pts[0], res_pts[1]);
		xlsx_write_object_anchor (xml, &anchor->cell_bound.end, "xdr:to",
					  res_pts[2], res_pts[3]);

		gsf_xml_out_start_element (xml, "xdr:graphicFrame");
		gsf_xml_out_add_cstr_unchecked (xml, "macro", "");

		gsf_xml_out_start_element (xml, "xdr:nvGraphicFramePr");

		gsf_xml_out_start_element (xml, "xdr:cNvPr");
		gsf_xml_out_add_int (xml, "id",  count+1);
		gsf_xml_out_add_cstr_unchecked (xml, "name",
			(tmp = g_strdup_printf ("Chart %d", count)));
		g_free (tmp);
		count++;
		gsf_xml_out_end_element (xml);

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
		gsf_xml_out_add_cstr_unchecked (xml, "xmlns:r", ns_rel);

		gsf_xml_out_add_cstr_unchecked (xml, "r:id", chart_id->data);
		gsf_xml_out_end_element (xml); /* </c:chart> */
		gsf_xml_out_end_element (xml); /* </a:graphicData> */
		gsf_xml_out_end_element (xml); /* </a:graphic> */
		gsf_xml_out_end_element (xml); /* </xdr:graphicFrame> */
		gsf_xml_out_simple_element (xml, "xdr:clientData", NULL);
		gsf_xml_out_end_element (xml); /* </xdr:twoCellAnchor> */
	}
	g_slist_free (objects);
	g_slist_free (chart_ids);

	gsf_xml_out_end_element (xml); /* </wsDr> */
	g_object_unref (xml);
	gsf_output_close (drawing_part);
	g_object_unref (drawing_part);

	return rId;
}
