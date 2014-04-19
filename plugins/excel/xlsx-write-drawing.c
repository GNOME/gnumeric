/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * xlsx-drawing-write.c : export MS Office Open xlsx drawings and charts.
 *
 * Copyright (C) 2006-2007 Jody Goldberg (jody@gnome.org)
 * Copyright (C) 2011 Jean Brefort (jean.brefort@normalesup.org)
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
xlsx_write_go_style (GsfXMLOut *xml, GOStyle *style)
{
	gsf_xml_out_start_element (xml, "c:spPr");
	if ((style->interesting_fields & (GO_STYLE_LINE | GO_STYLE_OUTLINE)) &&
	    style->line.dash_type != GO_LINE_NONE) {/* TODO: add more tests for transparent line */
		/* export the line color */
		gsf_xml_out_start_element (xml, "a:ln");
		if (style->line.width > 0)
			gsf_xml_out_add_int (xml, "w", style->line.width * 12700);
		if (!style->line.auto_color) {
			gsf_xml_out_start_element (xml, "a:solidFill");
			xlsx_write_rgbarea (xml, style->line.color);
			gsf_xml_out_end_element (xml);
		}

		gsf_xml_out_end_element (xml);
	}
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
	gsf_xml_out_end_element (xml);
	if (style->interesting_fields & GO_STYLE_MARKER) {
	}
}

static void
xlsx_write_one_plot (XLSXWriteState *state, GsfXMLOut *xml, GogObject const *chart, GogObject const *plot)
{
	char const *plot_type;
	gboolean failed = FALSE;
	gboolean use_xy = FALSE;
	double explosion = 0.;
	gboolean vary_by_element;
	GogAxisType axis_type[3] = {GOG_AXIS_X, GOG_AXIS_Y, GOG_AXIS_UNKNOWN};
	unsigned i;

	g_object_get (G_OBJECT (plot),
		      "vary-style-by-element", &vary_by_element,
		      NULL);
	plot_type = G_OBJECT_TYPE_NAME (plot);

	if (0 == strcmp (plot_type, "GogAreaPlot")) {
		gsf_xml_out_start_element (xml, "c:areaChart");
		xlsx_write_plot_1_5_type (xml, plot, FALSE);
	} else if (0 == strcmp (plot_type, "GogBarColPlot")) {
		gboolean horizontal;
		int overlap_percentage, gap_percentage;
		g_object_get (G_OBJECT (plot),
			"horizontal",		&horizontal,
			"overlap-percentage",	&overlap_percentage,
			"gap-percentage",	&gap_percentage,
			NULL);
		if (horizontal) {
			axis_type[0] = GOG_AXIS_Y;
			axis_type[1] = GOG_AXIS_X;
		}
		gsf_xml_out_start_element (xml, "c:barChart");
		gsf_xml_out_simple_element (xml, "c:barDir",
			horizontal ? "bar" : "col");
		xlsx_write_plot_1_5_type (xml, plot, TRUE);

		gsf_xml_out_start_element (xml, "c:overlap");
		gsf_xml_out_add_int (xml, "val", overlap_percentage);
		gsf_xml_out_end_element (xml);

		gsf_xml_out_start_element (xml, "c:gapWidth");
		gsf_xml_out_add_int (xml, "val", gap_percentage);
		gsf_xml_out_end_element (xml);
	} else if (0 == strcmp (plot_type, "GogLinePlot")) {
		gsf_xml_out_start_element (xml, "c:lineChart");
		xlsx_write_plot_1_5_type (xml, plot, FALSE);
	} else if (0 == strcmp (plot_type, "GogPiePlot") ||
		   0 == strcmp (plot_type, "GogRingPlot")) {
		double initial_angle = 0., center_size = 0.;
		gint16 center = 0;
		if (0 == strcmp (plot_type, "GogRingPlot")) {
			gsf_xml_out_start_element (xml, "c:doughnutChart");
			g_object_get (G_OBJECT (plot), "center-size", &center_size, NULL);
			center = (int)floor (center_size * 100. + .5);
			xlsx_write_chart_int (xml, "c:holeSize", 10,
				CLAMP (center, 10, 90));
		} else
			gsf_xml_out_start_element (xml, "c:pieChart");

		xlsx_write_chart_bool (xml, "c:varyColors", vary_by_element);
		g_object_get (G_OBJECT (plot),
			"initial-angle",	 &initial_angle,
			NULL);
		xlsx_write_chart_int (xml, "c:firstSliceAng", 0, (int) initial_angle);
#if 0
		double default_separation = 0.;
		/* handled in series ? */
		"default-separation",	&default_separation,
		xlsx_write_chart_int (xml, "c:explosion", 0, default_separation);
#endif
		axis_type[0] = axis_type[1] = GOG_AXIS_UNKNOWN;
			   g_object_get (G_OBJECT (plot), "default-separation", &explosion, NULL);
	} else if (0 == strcmp (plot_type, "GogRadarPlot") ||
		   0 == strcmp (plot_type, "GogRadarAreaPlot")) {
		gsf_xml_out_start_element (xml, "c:radarChart");
		axis_type[0] = GOG_AXIS_CIRCULAR;
		axis_type[1] = GOG_AXIS_RADIAL;
	} else if (0 == strcmp (plot_type, "GogBubblePlot")) {
		gboolean show_neg = FALSE, in_3d = FALSE, as_area = TRUE;
		g_object_get (G_OBJECT (plot),
			"show-negatives",	&show_neg,
			"in-3d",		&in_3d,
			"size-as-area",		&as_area,
			NULL);
		gsf_xml_out_start_element (xml, "c:bubbleChart");
		xlsx_write_chart_bool (xml, "c:varyColors", vary_by_element);
		xlsx_write_chart_bool (xml, "c:showNegBubbles", show_neg);
		xlsx_write_chart_cstr_unchecked (xml, "c:sizeRepresents",
			as_area ? "area" : "w");
		if (in_3d)
			xlsx_write_chart_bool (xml, "c:bubble3D", TRUE);
		use_xy = TRUE;
	} else if ( 0 == strcmp (plot_type, "GogXYPlot")) {
		gboolean has_lines, has_markers, use_splines;
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
		gsf_xml_out_start_element (xml, "c:scatterChart");
		xlsx_write_chart_cstr_unchecked (xml, "c:scatterStyle", style);
	} else if (0 == strcmp (plot_type, "GogContourPlot") ||
		   0 == strcmp (plot_type, "XLContourPlot")) {
		gsf_xml_out_start_element (xml, "c:surfaceChart");
	} else {
		g_warning ("unexpected plot type %s", plot_type);
		failed = TRUE;
		axis_type[0] = axis_type[1] = GOG_AXIS_UNKNOWN;
	}
	if (!failed) {
		GSList const *series = gog_plot_get_series (GOG_PLOT (plot));
		unsigned count = 0;
		for ( ; NULL != series ; series = series->next) {
			gsf_xml_out_start_element (xml, "c:ser");

			xlsx_write_chart_int (xml, "c:idx", -1, count);
			xlsx_write_chart_int (xml, "c:order", -1, count);
			if (!vary_by_element) /* FIXME: we might loose some style elements */
				xlsx_write_go_style (xml, go_styled_object_get_style (GO_STYLED_OBJECT (series->data)));
			xlsx_write_series_dim (state, xml, series->data,
				"c:tx", GOG_MS_DIM_LABELS);
			if (use_xy) {
				xlsx_write_series_dim (state, xml, series->data,
					"c:yVal", GOG_MS_DIM_VALUES);
				xlsx_write_series_dim (state, xml, series->data,
					"c:xVal",  GOG_MS_DIM_CATEGORIES);
				xlsx_write_series_dim (state, xml, series->data,
					"c:bubbleSize", GOG_MS_DIM_BUBBLES);
			} else {
				xlsx_write_series_dim (state, xml, series->data,
					"c:val", GOG_MS_DIM_VALUES);
				xlsx_write_series_dim (state, xml, series->data,
					"c:cat",  GOG_MS_DIM_CATEGORIES);
			}
			if (explosion > 0.)
				xlsx_write_chart_uint (xml, "c:explosion", 0, (unsigned) (explosion * 100));
			gsf_xml_out_end_element (xml); /* </c:ser> */
		}
		/* write axes Ids */
		for (i = 0; i < 3; i++)
			if (axis_type[i] != GOG_AXIS_UNKNOWN)
				xlsx_write_chart_uint (xml, "c:axId", 0, GPOINTER_TO_UINT (gog_plot_get_axis (GOG_PLOT (plot), axis_type[i])));
		gsf_xml_out_end_element (xml);
	}
	/* Write axes */
	/* first category axis */
	/* FIXME: might be a date axis? */
	for (i = 0; i < 3; i++)
		if (axis_type[i] != GOG_AXIS_UNKNOWN) {
			GSList *axes = gog_chart_get_axes (GOG_CHART (chart), axis_type[i]), *ptr;
			for (ptr = axes; ptr; ptr = ptr->next) {
				GogAxis *crossed = gog_axis_base_get_crossed_axis (GOG_AXIS_BASE (ptr->data));
				GogAxisPosition pos;
				GogGridLine *grid;
				if (gog_axis_is_discrete (ptr->data))
					gsf_xml_out_start_element (xml, "c:catAx");
				else
					gsf_xml_out_start_element (xml, "c:valAx");
				xlsx_write_chart_int (xml, "c:axId", 0, GPOINTER_TO_UINT (ptr->data));
				gsf_xml_out_start_element (xml, "c:scaling");
				xlsx_write_chart_cstr_unchecked (xml, "c:orientation", gog_axis_is_inverted (GOG_AXIS (ptr->data))? "maxMin": "minMax");
				// TODO: export min, max, an others
				gsf_xml_out_end_element (xml);
				xlsx_write_go_style (xml, go_styled_object_get_style (GO_STYLED_OBJECT (ptr->data)));
				/* FIXME position might be "t" or "r" */
				xlsx_write_chart_cstr_unchecked (xml, "c:axPos", (axis_type[i] == GOG_AXIS_X || axis_type[i] == GOG_AXIS_CIRCULAR)? "b": "l");
				xlsx_write_chart_int (xml, "c:crossAx", 0, GPOINTER_TO_UINT (crossed));
				g_object_get (G_OBJECT (ptr->data), "pos", &pos, NULL);
				switch (pos) {
				default:
				case GOG_AXIS_AT_LOW:
					/* FIXME: might be wrong if the axis is inverted */
					xlsx_write_chart_cstr_unchecked (xml, "c:crosses", "min");
					break;
				case GOG_AXIS_CROSS: {
					double cross = gog_axis_base_get_cross_location (GOG_AXIS_BASE (ptr->data));
					if (cross == 0.)
						xlsx_write_chart_cstr_unchecked (xml, "c:crosses", "autoZero");
					else
						xlsx_write_chart_float (xml, "c:crossesAt", 0., cross);
					break;
				}
				case GOG_AXIS_AT_HIGH:
					xlsx_write_chart_cstr_unchecked (xml, "c:crosses", "max");
					break;
				}
				/* grids */
				grid = gog_axis_get_grid_line (GOG_AXIS (ptr->data), TRUE);
				if (grid) {
					gsf_xml_out_start_element (xml, "c:majorGridlines");
					xlsx_write_go_style (xml, go_styled_object_get_style (GO_STYLED_OBJECT (grid)));
					gsf_xml_out_end_element (xml);
				}
				grid = gog_axis_get_grid_line (GOG_AXIS (ptr->data), FALSE);
				if (grid) {
					gsf_xml_out_start_element (xml, "c:minorGridlines");
					xlsx_write_go_style (xml, go_styled_object_get_style (GO_STYLED_OBJECT (grid)));
					gsf_xml_out_end_element (xml);
				}

				/* finished with axis */
				gsf_xml_out_end_element (xml);
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

	xlsx_write_go_style (xml, go_styled_object_get_style (GO_STYLED_OBJECT (chart)));

	gsf_xml_out_start_element (xml, "c:chart");
	gsf_xml_out_start_element (xml, "c:plotArea");
	/* save grid style here */
	obj = gog_object_get_child_by_name (GOG_OBJECT (chart), "Backplane");
	if (obj)
		xlsx_write_go_style (xml, go_styled_object_get_style (GO_STYLED_OBJECT (obj)));

	xlsx_write_plots (state, xml, chart);

	gsf_xml_out_end_element (xml); /* </c:plotArea> */

	if ((obj = gog_object_get_child_by_name (chart, "Legend"))) {
		gsf_xml_out_start_element (xml, "c:legend");
		gsf_xml_out_end_element (xml); /* </c:legend> */
	}
	gsf_xml_out_end_element (xml); /* </c:chart> */
}

static void
xlsx_write_chart (XLSXWriteState *state, GsfOutput *chart_part, SheetObject *so)
{
	GogGraph const	*graph;
	GogObject const	*chart;
	GsfXMLOut *xml;

	xml = gsf_xml_out_new (chart_part);
	gsf_xml_out_start_element (xml, "c:chartSpace");
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns:c", ns_chart);
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns:a", ns_drawing);
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns:r", ns_rel);

	graph = sheet_object_graph_get_gog (so);
	if (graph != NULL) {
		chart = gog_object_get_child_by_name (GOG_OBJECT (graph), "Chart");
		if (chart != NULL)
			xlsx_write_one_chart (state, xml, chart);
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
