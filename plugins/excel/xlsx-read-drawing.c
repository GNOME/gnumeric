/*
 * xlsx-drawing-read.c : import MS Office Open xlsx drawings and charts.
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

#include <sheet-object-widget.h>

#undef DEBUG_AXIS
#undef DEBUG_COLOR

/*****************************************************************************
 * Various functions common to at least charts and user shapes               *
 *****************************************************************************/

static void
xlsx_chart_push_obj (XLSXReadState *state, GogObject *obj)
{
	state->obj_stack = g_slist_prepend (state->obj_stack, state->cur_obj);
	state->cur_obj = obj;
	state->style_stack = g_slist_prepend (state->style_stack, state->cur_style);
	state->cur_style = GO_IS_STYLED_OBJECT (obj)? go_style_dup (go_styled_object_get_style (GO_STYLED_OBJECT (obj))): NULL;
#if 0
	g_print ("push %s\n", G_OBJECT_TYPE_NAME (obj));
#endif

	if (obj) {
		const char *name = gog_object_get_name (obj);
		go_debug_check_finalized (obj, name);
	}
	if (state->cur_style) {
		go_debug_check_finalized (state->cur_style, "Anonymous style");
	}
}

static void
xlsx_chart_pop_obj (XLSXReadState *state)
{
	GSList *obj_stack = state->obj_stack;
	g_return_if_fail (obj_stack != NULL);

#if 0
	g_print ("pop %s\n", G_OBJECT_TYPE_NAME (state->cur_obj));
#endif

	if (state->cur_style) {
		if (state->cur_obj)
			g_object_set (G_OBJECT (state->cur_obj), "style", state->cur_style, NULL);
		g_object_unref (state->cur_style);
	}
	state->cur_obj = obj_stack->data;
	state->obj_stack = g_slist_remove (state->obj_stack, state->cur_obj);
	state->cur_style = state->style_stack->data;
	state->style_stack = g_slist_remove (state->style_stack, state->cur_style);
}

static void
xlsx_reset_chart_pos (XLSXReadState *state)
{
	int i;
	for (i = 0; i < 4; i++) {
		state->chart_pos[i] = go_nan;
		state->chart_pos_mode[i] = FALSE;
	}
	state->chart_pos_target = FALSE;
}


static void
xlsx_push_text_object (XLSXReadState *state, const char *name)
{
	GogObject *label = gog_object_add_by_name (state->cur_obj, name, NULL);
	state->sp_type |= GO_STYLE_FONT;
	g_object_set (G_OBJECT (label), "allow-wrap", TRUE, "justification", "center", NULL);
	xlsx_chart_push_obj (state, label);
}

static void
xlsx_chart_push_color_state (XLSXReadState *state, XLSXColorState s)
{
	/* We need only a few levels.  */
	state->chart_color_state = (state->chart_color_state << 3) | s;
}

static void
xlsx_chart_pop_color_state (XLSXReadState *state, XLSXColorState s)
{
	XLSXColorState s0 = (state->chart_color_state & 7);
	state->chart_color_state = (state->chart_color_state >> 3);
	if (s != XLSX_CS_ANY)
		g_return_if_fail (s == s0);
}



static void
xlsx_tx_pr (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	state->sp_type |= GO_STYLE_FONT;
	xlsx_chart_push_color_state (state, XLSX_CS_FONT);
}

static void
xlsx_tx_pr_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	state->sp_type &= ~GO_STYLE_FONT;
	xlsx_chart_pop_color_state (state, XLSX_CS_FONT);
}

static void
xlsx_chart_text_start (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	if (!GOG_IS_LABEL (state->cur_obj) && GNM_IS_SO_GRAPH (state->so) && NULL == state->series) { /* Hmm, why? */
		xlsx_push_text_object (state, "Label");
	}
}

static void
xlsx_chart_text (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;

	if (GNM_IS_SO_FILLED (state->so))
		g_object_set (G_OBJECT (state->so), "text", state->chart_tx, NULL);
	else if (NULL == state->series) {
		if (GOG_IS_LABEL (state->cur_obj)) {
			if (state->chart_tx) {
				GnmValue *value = value_new_string_nocopy (state->chart_tx);
				GnmExprTop const *texpr = gnm_expr_top_new_constant (value);
				gog_dataset_set_dim (GOG_DATASET (state->cur_obj), 0,
					gnm_go_data_scalar_new_expr (state->sheet, texpr), NULL);
				state->chart_tx = NULL;
			} else if (state->texpr) {
				gog_dataset_set_dim (GOG_DATASET (state->cur_obj), 0,
					gnm_go_data_scalar_new_expr (state->sheet, state->texpr), NULL);
				state->texpr = NULL;
			}
			if (go_finite (state->chart_pos[0])) {
				GogViewAllocation alloc;
				alloc.x = state->chart_pos[0];
				alloc.w = state->chart_pos[1] - alloc.x;
				alloc.y = state->chart_pos[2];
				alloc.h = state->chart_pos[3] - alloc.y;
				xlsx_reset_chart_pos (state);
				gog_object_set_position_flags (state->cur_obj, GOG_POSITION_MANUAL, GOG_POSITION_ANY_MANUAL);
				gog_object_set_manual_position (state->cur_obj, &alloc);
			}
			if (!state->inhibit_text_pop)
				xlsx_chart_pop_obj (state);
		}
	} else if (state->chart_tx != NULL) {
		/* set the series title */
		gog_series_set_dim (state->series, -1,
			gnm_go_data_scalar_new_expr (state->sheet,
				gnm_expr_top_new_constant (
					value_new_string (state->chart_tx))), NULL);
	}
	g_free (state->chart_tx);
	state->chart_tx = NULL;
	state->sp_type &= ~GO_STYLE_FONT;
}

static void
xlsx_text_value (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	g_return_if_fail (state->chart_tx == NULL);
	state->chart_tx = g_strdup (xin->content->str);
}

static void
xlsx_chart_title_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;

	if (state->cur_obj == (GogObject *)state->chart) {
		xlsx_push_text_object (state, "Title");
	} else {
		xlsx_push_text_object (state, "Label");
	}
	state->inhibit_text_pop = TRUE;
	state->sp_type |= GO_STYLE_LINE;
}

static void
xlsx_chart_title_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	state->inhibit_text_pop = FALSE;
	if (GOG_IS_CHART (state->cur_obj)) {
		xlsx_chart_text (xin, blob);
	} else {
		xlsx_chart_pop_obj (state);
	}
	state->sp_type &= ~GO_STYLE_LINE;
}


static void
xlsx_chart_p_start (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	if (state->texpr)
		return;
	if (state->chart_tx) {
		char *buf = g_strconcat (state->chart_tx, "\n", NULL);
		g_free (state->chart_tx);
		state->chart_tx = buf;
	}
}

static void
xlsx_chart_text_content (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	/* a rich node can contain several t children, if this happens, concatenate
	 the contents */
	if (state->texpr)
		return;
	if (*xin->content->str) {
		if (state->chart_tx) {
			char *buf = g_strconcat (state->chart_tx, xin->content->str, NULL);
			g_free (state->chart_tx);
			state->chart_tx = buf;
		} else
			state->chart_tx = g_strdup (xin->content->str);
	}
}

static void
xlsx_draw_text_run_props (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	PangoFontDescription *desc;
	GOStyle *style = state->cur_style;
	gboolean auto_font;

	if (!GO_IS_STYLED_OBJECT (state->cur_obj) || !style)
		return;

	/* FIXME: this should be for a text run, not for the full style */

	if (style->font.font) {
		desc = pango_font_description_copy (style->font.font->desc);
		auto_font = style->font.auto_font;
	} else {
		desc = pango_font_description_new ();
		pango_font_description_set_family (desc, "Calibri");
		pango_font_description_set_size (desc, 10 * PANGO_SCALE);
		auto_font = TRUE;
	}

	for (; attrs && *attrs; attrs += 2) {
		int i;
		if (attr_int (xin, attrs, "sz", &i)) {
			int psize = i * PANGO_SCALE / 100;
			if (psize != pango_font_description_get_size (desc)) {
				auto_font = FALSE;
				pango_font_description_set_size (desc, psize);
			}
		} else if (attr_int (xin, attrs, "b", &i)) {
			PangoWeight pw = i ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL;
			if (pw != pango_font_description_get_weight (desc)) {
				pango_font_description_set_weight (desc, pw);
				auto_font = FALSE;
			}
		} else if (attr_int (xin, attrs, "i", &i)) {
			PangoStyle ps = i ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL;
			if (ps != pango_font_description_get_style (desc)) {
				pango_font_description_set_style (desc, ps);
				auto_font = FALSE;
			}
		}
	}

	style->font.auto_font = auto_font;
	if (auto_font)
		pango_font_description_free (desc);
	else
		go_style_set_font (style, go_font_new_by_desc (desc));
}

static void
xlsx_rpr_latin (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GOStyle *style = state->cur_style;

	if (!GO_IS_STYLED_OBJECT (state->cur_obj) || !style)
		return;

	for (; attrs && *attrs; attrs += 2) {
		if (strcmp (attrs[0], "typeface") == 0) {
			PangoFontDescription *desc = pango_font_description_copy (style->font.font->desc);
			pango_font_description_set_family (desc, attrs[1]);
			style->font.auto_font = FALSE;
			go_style_set_font (style, go_font_new_by_desc (desc));
		}
	}
}

static void
xlsx_body_pr (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	static EnumVal const wrap_types[] = {
		{ "none", 0 },
		{ "square", 1 },
		{ NULL, 0 }
	};

	if (GO_IS_STYLED_OBJECT (state->cur_obj) && state->cur_style && !GOG_IS_LEGEND (state->cur_obj)) {
		for (; attrs && *attrs; attrs += 2) {
			int val;

			if (attr_enum (xin, attrs, "wrap", wrap_types, &val)) {
				g_object_set (state->cur_obj, "allow-wrap", val, NULL);
			} else if (!strcmp (attrs[0], "rot")) {
				/* FIXME: be careful if the "vert" property exists (not yet supported) */
				int rotation;
				if (attr_int (xin, attrs, "rot", &rotation)) {
					state->cur_style->text_layout.auto_angle = FALSE;
					state->cur_style->text_layout.angle = -rotation / 60000.0;
				}
			}
		}
	}
}

/*****************************************************************************
 * User shapes                                                               *
 *****************************************************************************/

static void
xlsx_rel_size_anchor_start (G_GNUC_UNUSED  GsfXMLIn *xin, G_GNUC_UNUSED  xmlChar const **attrs)
{
}

static void
xlsx_rel_size_anchor (G_GNUC_UNUSED  GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
}

static void
xlsx_user_shape_pos (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	char *end;
	double tmp = gnm_strto (xin->content->str, &end);
	if (*end) {
		xlsx_warning (xin,
			_("Invalid number '%s' for node %s"),
			xin->content->str, xin->node->name);
		return;
	}
	state->chart_pos[xin->node->user_data.v_int] = tmp;
}

static void
xlsx_user_shape (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;

	for (; *attrs; attrs += 2)
		if (!strcmp (attrs[0], "textlink") && *attrs[1]) {
			GnmParsePos pp;
			state->texpr = xlsx_parse_expr (xin, attrs[1],
				parse_pos_init_sheet (&pp, state->sheet));
		}
}

static void
xlsx_user_shape_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	if (state->texpr) {
		/* this should not happen if we import everything */
		g_warning ("unused expression %p.",state->texpr);
		gnm_expr_top_unref (state->texpr);
		state->texpr = NULL;
	}

}

static void
xlsx_sppr_xfrm (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int rot = 0, flipH = 0, flipV = 0;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_int (xin, attrs, "rot", &rot) ||
		    attr_bool (xin, attrs, "flipH", &flipH) ||
		    attr_bool (xin, attrs, "flipV", &flipV))
			; /* Nothing */
	}

	rot = rot % (360 * 60000);
	if (rot < 0) rot += 360 * 60000;

	if (state->marker) {
		if (go_marker_get_shape (state->marker) == GO_MARKER_TRIANGLE_UP) {
			switch ((rot + 45 * 60000) / (90 * 60000)) {
			case 1:
				go_marker_set_shape (state->marker, GO_MARKER_TRIANGLE_RIGHT);
				break;
			case 2:
				go_marker_set_shape (state->marker, GO_MARKER_TRIANGLE_DOWN);
				break;
			case 3:
				go_marker_set_shape (state->marker, GO_MARKER_TRIANGLE_LEFT);
				break;
			default:
				break;
			}
		}

		if (flipH) {
			if (go_marker_get_shape (state->marker) == GO_MARKER_HALF_BAR) {
				go_marker_set_shape (state->marker, GO_MARKER_LEFT_HALF_BAR);
			}
		}
		return;
	}

	if (flipH)
		state->so_direction ^= GOD_ANCHOR_DIR_RIGHT;
	if (flipV)
		state->so_direction ^= GOD_ANCHOR_DIR_DOWN;
}

static void
xlsx_draw_color_themed (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	static EnumVal const colors[] = {
		{ "bg1",	 0 }, /* Background Color 1 */
		{ "tx1",	 1 }, /* Text Color 1 */
		{ "bg2",	 2 }, /* Background Color 2 */
		{ "tx2",	 3 }, /* Text Color 2 */
		{ "accent1",	 4 }, /* Accent Color 1 */
		{ "accent2",	 5 }, /* Accent Color 2 */
		{ "accent3",	 6 }, /* Accent Color 3 */
		{ "accent4",	 7 }, /* Accent Color 4 */
		{ "accent5",	 8 }, /* Accent Color 5 */
		{ "accent6",	 9 }, /* Accent Color 6 */
		{ "hlink",	10 }, /* Hyperlink Color */
		{ "folHlink",	11 }, /* Followed Hyperlink Color */
		{ "phClr",	12 }, /* Style Color */
		{ "dk1",	13 }, /* Dark Color 1 */
		{ "lt1",	14 }, /* Light Color 1 */
		{ "dk2",	15 }, /* Dark Color 2 */
		{ "lt2",	16 }, /* Light Color 2 */
		{ NULL, 0 }
	};
#endif
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	const char *colname = simple_string (xin, attrs);

	if (colname) {
		if (themed_color_from_name (state, colname, &state->color))
			color_set_helper (state);
		else
			xlsx_warning (xin, _("Unknown color '%s'"), colname);
	}
}

static void
xlsx_draw_color_rgb (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_gocolor (xin, attrs, "val", &state->color))
			color_set_helper (state);
	}
}

static void
xlsx_draw_color_scrgb (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int r = 0, g = 0, b = 0;
	int scale = 100000;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_int (xin, attrs, "r", &r) ||
		    attr_int (xin, attrs, "g", &g) ||
		    attr_int (xin, attrs, "b", &b)) {
			/* Nothing more */
		}
	}

	r = 255 * CLAMP (r, 0, scale) / scale;
	g = 255 * CLAMP (g, 0, scale) / scale;
	b = 255 * CLAMP (b, 0, scale) / scale;
	state->color = GO_COLOR_FROM_RGB (r, g, b);
	color_set_helper (state);
}

static void
xlsx_draw_clientdata (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	gboolean print = TRUE;

	if (state->so == NULL) /* happens if gnumeric does not support the object */
		return;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_bool (xin, attrs, "fPrintsWithSheet", &print)) {
			/* Nothing more */
		}
	}

	sheet_object_set_print_flag (state->so, &print);
}

static void
xlsx_read_cnvpr (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (!strcmp (attrs[0], "name")) {
			g_free (state->object_name);
			state->object_name = g_strdup (attrs[1]);
		}
	}
}

static GsfXMLInNode const xlsx_chart_drawing_dtd[] =
{
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE_FULL (START, USER_SHAPES, XL_NS_CHART, "userShapes", GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
  GSF_XML_IN_NODE (USER_SHAPES, REL_SIZE_ANCHOR, XL_NS_CHART_DRAW, "relSizeAnchor", GSF_XML_NO_CONTENT,  &xlsx_rel_size_anchor_start, &xlsx_rel_size_anchor),
    GSF_XML_IN_NODE (REL_SIZE_ANCHOR, FROM, XL_NS_CHART_DRAW, "from", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE_FULL (FROM, FROM_X, XL_NS_CHART_DRAW, "x", GSF_XML_CONTENT, FALSE, TRUE, NULL, &xlsx_user_shape_pos, 0),
      GSF_XML_IN_NODE_FULL (FROM, FROM_Y, XL_NS_CHART_DRAW, "y", GSF_XML_CONTENT, FALSE, TRUE, NULL, &xlsx_user_shape_pos, 2),
    GSF_XML_IN_NODE (REL_SIZE_ANCHOR, TO, XL_NS_CHART_DRAW, "to", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE_FULL (TO, TO_X, XL_NS_CHART_DRAW, "x", GSF_XML_CONTENT, FALSE, TRUE, NULL, &xlsx_user_shape_pos, 1),
      GSF_XML_IN_NODE_FULL (TO, TO_Y, XL_NS_CHART_DRAW, "y", GSF_XML_CONTENT, FALSE, TRUE, NULL, &xlsx_user_shape_pos, 3),
    GSF_XML_IN_NODE (REL_SIZE_ANCHOR, SHAPE, XL_NS_CHART_DRAW, "sp", GSF_XML_NO_CONTENT, &xlsx_user_shape, &xlsx_user_shape_end),
      GSF_XML_IN_NODE (SHAPE, NV_SP_PR, XL_NS_CHART_DRAW, "nvSpPr", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (NV_SP_PR, C_NV_PR, XL_NS_CHART_DRAW, "cNvPr", GSF_XML_NO_CONTENT, &xlsx_read_cnvpr, NULL),
        GSF_XML_IN_NODE (NV_SP_PR, C_NV_SP_PR, XL_NS_CHART_DRAW, "cNvSpPr", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (C_NV_SP_PR, SP_LOCKS, XL_NS_DRAW, "spLocks", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (C_NV_SP_PR, HLINK_CLICK, XL_NS_DRAW, "hlinkClick", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE_FULL (SHAPE, SHAPE_PR, XL_NS_CHART_DRAW, "spPr", GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
        GSF_XML_IN_NODE (SHAPE_PR, SP_PR_PRST_GEOM, XL_NS_DRAW, "prstGeom", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (SP_PR_PRST_GEOM, AV_LST, XL_NS_DRAW, "avLst", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (SHAPE_PR, SP_PR_XFRM, XL_NS_DRAW, "xfrm", GSF_XML_NO_CONTENT, &xlsx_sppr_xfrm, NULL),
          GSF_XML_IN_NODE (SP_PR_XFRM, SP_XFRM_OFF, XL_NS_DRAW, "off", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (SP_PR_XFRM, SP_XFRM_EXT, XL_NS_DRAW, "ext", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (SHAPE_PR, FILL_GRAD, XL_NS_DRAW, "gradFill", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (FILL_GRAD, GRAD_LIST,	XL_NS_DRAW, "gsLst", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (GRAD_LIST, GRAD_LIST_ITEM, XL_NS_DRAW, "gs", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (GRAD_LIST_ITEM, COLOR_RGB, XL_NS_DRAW, "srgbClr", GSF_XML_NO_CONTENT, NULL, NULL),
                COLOR_MODIFIER_NODES(COLOR_RGB,1),
	      GSF_XML_IN_NODE (GRAD_LIST_ITEM, SCHEME_CLR, XL_NS_DRAW, "schemeClr", GSF_XML_NO_CONTENT, NULL, NULL),
                COLOR_MODIFIER_NODES(SCHEME_CLR,0),
	      GSF_XML_IN_NODE (GRAD_LIST_ITEM, COLOR_SYS, XL_NS_DRAW, "sysClr", GSF_XML_NO_CONTENT, NULL, NULL),
                COLOR_MODIFIER_NODES(COLOR_SYS,0),
	  GSF_XML_IN_NODE (FILL_GRAD, GRAD_LIN,	XL_NS_DRAW, "lin", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (FILL_GRAD, GRAD_TILE,	XL_NS_DRAW, "tileRect", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (SHAPE, TX_BODY, XL_NS_CHART_DRAW, "txBody", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (TX_BODY, LST_STYLE, XL_NS_DRAW, "lstStyle", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (LST_STYLE, DEF_P_PR, XL_NS_DRAW, "defPPr", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (LST_STYLE, EXTLST, XL_NS_DRAW, "extLst", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (LST_STYLE, LVL1_P_PR, XL_NS_DRAW, "lvl1pPr", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (LVL1_P_PR, DEF_R_PR, XL_NS_DRAW, "defRPr", GSF_XML_NO_CONTENT, NULL, NULL),
              GSF_XML_IN_NODE (DEF_R_PR, CS, XL_NS_DRAW, "cs", GSF_XML_NO_CONTENT, NULL, NULL),
              GSF_XML_IN_NODE (DEF_R_PR, EA, XL_NS_DRAW, "ea", GSF_XML_NO_CONTENT, NULL, NULL),
              GSF_XML_IN_NODE (DEF_R_PR, LATIN, XL_NS_DRAW, "latin", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (LST_STYLE, LVL2_P_PR, XL_NS_DRAW, "lvl2pPr", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (LVL2_P_PR, DEF_R_PR, XL_NS_DRAW, "defRPr", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (LST_STYLE, LVL3_P_PR, XL_NS_DRAW, "lvl3pPr", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (LVL3_P_PR, DEF_R_PR, XL_NS_DRAW, "defRPr", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (LST_STYLE, LVL4_P_PR, XL_NS_DRAW, "lvl4pPr", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (LVL4_P_PR, DEF_R_PR, XL_NS_DRAW, "defRPr", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (LST_STYLE, LVL5_P_PR, XL_NS_DRAW, "lvl5pPr", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (LVL5_P_PR, DEF_R_PR, XL_NS_DRAW, "defRPr", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (LST_STYLE, LVL6_P_PR, XL_NS_DRAW, "lvl6pPr", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (LVL6_P_PR, DEF_R_PR, XL_NS_DRAW, "defRPr", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (LST_STYLE, LVL7_P_PR, XL_NS_DRAW, "lvl7pPr", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (LVL7_P_PR, DEF_R_PR, XL_NS_DRAW, "defRPr", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (LST_STYLE, LVL8_P_PR, XL_NS_DRAW, "lvl8pPr", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (LVL8_P_PR, DEF_R_PR, XL_NS_DRAW, "defRPr", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (LST_STYLE, LVL9_P_PR, XL_NS_DRAW, "lvl9pPr", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (LVL9_P_PR, DEF_R_PR, XL_NS_DRAW, "defRPr", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (TX_BODY, TX_BODY_PR, XL_NS_DRAW, "bodyPr", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (TX_BODY, TEXT_PR_P, XL_NS_DRAW, "p", GSF_XML_NO_CONTENT, &xlsx_chart_p_start, NULL),
          GSF_XML_IN_NODE (TEXT_PR_P, TX_RICH_FLD, XL_NS_DRAW, "fld", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (TX_RICH_FLD, TX_RICH_R_PR, XL_NS_DRAW, "rPr", GSF_XML_NO_CONTENT, &xlsx_draw_text_run_props, NULL),
              GSF_XML_IN_NODE (TX_RICH_R_PR, PR_P_PR_DEF_CS, XL_NS_DRAW, "cs", GSF_XML_NO_CONTENT, NULL, NULL),
              GSF_XML_IN_NODE (TX_RICH_R_PR, PR_P_PR_DEF_EA, XL_NS_DRAW, "ea", GSF_XML_NO_CONTENT, NULL, NULL),
              GSF_XML_IN_NODE (TX_RICH_R_PR, PR_P_PR_DEF_LATIN, XL_NS_DRAW, "latin", GSF_XML_NO_CONTENT, NULL, NULL),
              GSF_XML_IN_NODE (TX_RICH_R_PR, TEXT_FILL_SOLID, XL_NS_DRAW, "solidFill", GSF_XML_NO_CONTENT, NULL, NULL),
                GSF_XML_IN_NODE (TEXT_FILL_SOLID, COLOR_RGB, XL_NS_DRAW, "srgbClr", GSF_XML_2ND, NULL, NULL),
                GSF_XML_IN_NODE (TEXT_FILL_SOLID, SCHEME_CLR, XL_NS_DRAW, "schemeClr", GSF_XML_2ND, NULL, NULL),
                GSF_XML_IN_NODE (TEXT_FILL_SOLID, COLOR_SYS, XL_NS_DRAW, "sysClr", GSF_XML_2ND, NULL, NULL),
              GSF_XML_IN_NODE (TX_RICH_R_PR, PR_P_PR_DEF_UFILLTX, XL_NS_DRAW, "uFillTx", GSF_XML_NO_CONTENT, NULL, NULL),
              GSF_XML_IN_NODE (TX_RICH_R_PR, PR_P_PR_DEF_ULNTX, XL_NS_DRAW, "uLnTx", GSF_XML_NO_CONTENT, NULL, NULL),
 	    GSF_XML_IN_NODE (TX_RICH_FLD, PR_P_PR,	XL_NS_DRAW, "pPr", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (PR_P_PR, PR_P_PR_DEF, XL_NS_DRAW, "defRPr", GSF_XML_NO_CONTENT, NULL, NULL),
                GSF_XML_IN_NODE (PR_P_PR_DEF, PR_P_PR_DEF_CS, XL_NS_DRAW, "cs", GSF_XML_NO_CONTENT, NULL, NULL),
                GSF_XML_IN_NODE (PR_P_PR_DEF, PR_P_PR_DEF_EA, XL_NS_DRAW, "ea", GSF_XML_NO_CONTENT, NULL, NULL),
                GSF_XML_IN_NODE (PR_P_PR_DEF, PR_P_PR_DEF_LATIN, XL_NS_DRAW, "latin", GSF_XML_NO_CONTENT, NULL, NULL),
                GSF_XML_IN_NODE (PR_P_PR_DEF, TEXT_FILL_SOLID, XL_NS_DRAW, "solidFill", GSF_XML_2ND, NULL, NULL),
                GSF_XML_IN_NODE (PR_P_PR_DEF, PR_P_PR_DEF_UFILLTX, XL_NS_DRAW, "uFillTx", GSF_XML_NO_CONTENT, NULL, NULL),
                GSF_XML_IN_NODE (PR_P_PR_DEF, PR_P_PR_DEF_ULNTX, XL_NS_DRAW, "uLnTx", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (TX_RICH_FLD, TX_RICH_R_T, XL_NS_DRAW,  "t", GSF_XML_CONTENT, NULL, &xlsx_chart_text_content),
	  GSF_XML_IN_NODE (TEXT_PR_P, TX_RICH_R, XL_NS_DRAW, "r", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (TX_RICH_R, TX_RICH_R_PR, XL_NS_DRAW, "rPr", GSF_XML_2ND, NULL, NULL),
            GSF_XML_IN_NODE (TX_RICH_R, TX_RICH_R_T, XL_NS_DRAW,  "t", GSF_XML_2ND, NULL, NULL),
	  GSF_XML_IN_NODE (TEXT_PR_P, PR_P_PR,	XL_NS_DRAW, "pPr", GSF_XML_2ND, NULL, NULL),
	  GSF_XML_IN_NODE (TEXT_PR_P, PR_P_PR_END,XL_NS_DRAW, "endParaRPr", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (PR_P_PR_END, TEXT_FILL_SOLID, XL_NS_DRAW, "solidFill", GSF_XML_2ND, NULL, NULL),
            GSF_XML_IN_NODE (PR_P_PR_END, PR_P_PR_DEF_LATIN, XL_NS_DRAW, "latin", GSF_XML_2ND, NULL, NULL),
          GSF_XML_IN_NODE (TEXT_PR_P, TX_RICH_R_T, XL_NS_DRAW,  "t", GSF_XML_2ND, NULL, NULL),
	GSF_XML_IN_NODE_END
};

static void
xlsx_chart_user_shapes (GsfXMLIn *xin, xmlChar const **attrs)
{
	xmlChar const *part_id = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], XL_NS_DOC_REL, "id"))
			part_id = attrs[1];
	if (NULL != part_id) {
		xlsx_parse_rel_by_id (xin, part_id, xlsx_chart_drawing_dtd, xlsx_ns);
	}
}

/*****************************************************************************/

static void
xlsx_chart_add_plot (GsfXMLIn *xin, char const *type)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	if (NULL != (state->plot = (GogPlot*) gog_plot_new_by_name (type))) {
		/* Add _before_ setting styles so theme does not override */
		gog_object_add_by_name (GOG_OBJECT (state->chart),
			"Plot", GOG_OBJECT (state->plot));

		if (state->cur_obj == NULL) {
			/* Add a backplane if compatible with plot.  */

			const char *bp_name = "Backplane";
			GogObjectRole const *role =
				gog_object_find_role_by_name (GOG_OBJECT (state->chart),
							      bp_name);
			if (role->can_add (GOG_OBJECT (state->chart))) {
				GogObject *bp = gog_object_add_by_name (GOG_OBJECT (state->chart),
									bp_name,
									NULL);
				/* Replace dummy object.  */
				xlsx_chart_pop_obj (state);
				xlsx_chart_push_obj (state, bp);

				/* If there is no style, we will remove the backplane.  */
				state->cur_style->fill.type = GO_STYLE_FILL_NONE;
			}
		}
	}
}

/* shared with pie of pie, and bar of pie */
static void
xlsx_chart_vary_colors (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int vary = TRUE;  /* A somewhat crazy default */
	(void)simple_bool (xin, attrs, &vary);
	g_object_set (G_OBJECT (state->plot), "vary-style-by-element", vary, NULL);
}

static void
xlsx_chart_pie_sep (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	unsigned sep = 0;
	(void)simple_uint (xin, attrs, &sep);
	g_object_set (G_OBJECT (state->plot),
		      "default-separation", (double)(CLAMP (sep, 0u, 500u)) / 100.0, NULL);
}

static void
xlsx_chart_pie_angle (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	unsigned angle = 0;
	(void)simple_uint (xin, attrs, &angle);
	g_object_set (G_OBJECT (state->plot),
		      "initial-angle", (double)angle, NULL);
}

static void
xlsx_chart_ring_hole (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	unsigned size = 50;
	(void)simple_uint (xin, attrs, &size);
	/* Allow full range for size.  Spec says 10-90.  */
	g_object_set (G_OBJECT (state->plot),
		      "center-size", CLAMP (size, 0, 100) / 100.0, NULL);
}

/* shared with pie of pie, and bar of pie */
static void xlsx_chart_pie (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs) { xlsx_chart_add_plot (xin, "GogPiePlot"); }
static void xlsx_chart_ring (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs) { xlsx_chart_add_plot (xin, "GogRingPlot"); }

/***********************************************************************/

static void
xlsx_chart_bar_dir (GsfXMLIn *xin, xmlChar const **attrs)
{
	static const EnumVal dirs[] = {
		{ "bar",	 TRUE },
		{ "col",	 FALSE },
		{ NULL, 0 }
	};
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int dir = FALSE;

	g_return_if_fail (state->plot != NULL);

	(void)simple_enum (xin, attrs, dirs, &dir);
	g_object_set (G_OBJECT (state->plot), "horizontal", dir, NULL);
}

static void
xlsx_chart_bar_overlap (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	const char *soverlap = simple_string (xin, attrs);
	if (soverlap) {
		/* Spec says add "%" at end; XL cannot handle that. */
		int overlap = strtol (soverlap, NULL, 10);
		g_object_set (G_OBJECT (state->plot),
			      "overlap-percentage", CLAMP (overlap, -100, 100),
			      NULL);
	}
}

static void
xlsx_chart_bar_group (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	static const EnumVal grps[] = {
		{ "percentStacked", 0 },
		{ "clustered", 1 },
		{ "standard", 2 },
		{ "stacked", 3 },
		{ NULL, 0 }
	};
	static const char *types[] = { "as_percentage", "normal", "normal", "stacked" };
	int grp = 1;

	g_return_if_fail (state->plot != NULL);

	(void)simple_enum (xin, attrs, grps, &grp);
	g_object_set (G_OBJECT (state->plot), "type", types[grp], NULL);
}

static void
xlsx_chart_bar_gap (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	const char *sgap = simple_string (xin, attrs);
	if (sgap) {
		/* Spec says add "%" at end; XL cannot handle that. */
		int gap = strtol (sgap, NULL, 10);
		g_object_set (G_OBJECT (state->plot),
			      "gap-percentage", CLAMP (gap, 0, 500), NULL);
	}
}

static void
xlsx_chart_bar (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	xlsx_chart_add_plot (xin, "GogBarColPlot");
}

/***********************************************************************/

static void
xlsx_axis_info_free (XLSXAxisInfo *info)
{
	g_free (info->id);
	g_free (info->cross_id);
	if (NULL != info->axis)
		g_object_unref (info->axis);
	g_slist_free (info->plots);
	g_free (info);
}

static void
xlsx_plot_axis_id (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	const char *axid = simple_string (xin, attrs);
	XLSXAxisInfo *res;

	if (!state->plot || !axid)
		return;

	res = g_hash_table_lookup (state->axis.by_id, axid);
	if (NULL == res) {
		res = g_new0 (XLSXAxisInfo, 1);
		res->id = g_strdup (axid);
		res->axis	= NULL;
		res->plots	= NULL;
		res->type	= XLSX_AXIS_UNKNOWN;
		res->compass	= GOG_POSITION_AUTO;
		res->cross	= GOG_AXIS_CROSS;
		res->cross_value = go_nan;
		res->invert_axis = FALSE;
		res->logbase = 0;
		g_hash_table_replace (state->axis.by_id, res->id, res);
#ifdef DEBUG_AXIS
		g_printerr ("create info for %s = %p\n", res->id, res);
#endif
	}

#ifdef DEBUG_AXIS
	g_printerr ("add plot %p to info %p\n", state->plot, res);
#endif
	res->plots = g_slist_prepend (res->plots, state->plot);
}

static void
xlsx_axis_start (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	state->axis.obj	 = NULL;
	state->axis.type = xin->node->user_data.v_int;
	state->axis.info = NULL;

	/* Push dummy object for now until we can deduce the role.  */
	xlsx_chart_push_obj (state, NULL);
#ifdef DEBUG_AXIS
	g_printerr ("Create NULL object for axis\n");
#endif
}

static void
xlsx_axis_crosses_at (GsfXMLIn *xin, xmlChar const **attrs)
{
	/* This element specifies where on the axis the perpendicular axis crosses.
	   The units are dependent on the type of axis.
	   When specified as a child element of valAx, the value is a decimal number on the value axis. When specified as a
	   child element of dateAx, the date is defined as a integer number of days relative to the base date of the current
	   date base. When specified as a child element of catAx, the value is an integer category number, starting with 1
	   as the first category.
	*/
 	XLSXReadState *state = (XLSXReadState *)xin->user_state;

	g_return_if_fail (state->axis.info != NULL);

	simple_double (xin, attrs, &state->axis.info->cross_value);
}

static void
xlsx_axis_id (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	const char *axid = simple_string (xin, attrs);
	if (axid) {
		state->axis.info = g_hash_table_lookup (state->axis.by_id, axid);
#ifdef DEBUG_AXIS
		g_printerr ("define %s = %p\n", axid, state->axis.info);
#endif
	}
}

static void
xlsx_axis_delete (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int del = TRUE; /* A crazy default */
	(void)simple_bool (xin, attrs, &del);
	if (state->axis.info)
		state->axis.info->deleted = del;
}

static void
xlsx_axis_orientation (GsfXMLIn *xin, xmlChar const **attrs)
{
	static const EnumVal orients[] = {
		{ "minMax",	 FALSE },
		{ "maxMin",	 TRUE },
		{ NULL, 0 }
	};
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int orient = FALSE;

	(void)simple_enum (xin, attrs, orients, &orient);
	if (state->axis.info)
		state->axis.info->invert_axis = orient;
}

static void
xlsx_axis_format (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	gboolean shared = TRUE;
	char const *fmt = NULL;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (0 == strcmp (attrs[0], "sourceLinked"))
			attr_bool (xin, attrs, "sourceLinked", &shared);
		else if (0 == strcmp (attrs[0], "formatCode"))
			fmt = attrs[1];
	if (fmt && !shared)
		g_object_set (G_OBJECT (state->axis.obj),
			"assigned-format-string-XL", fmt, NULL);
}
static void
xlsx_chart_logbase (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	double base;

	/*
	 * The documented limits are 2-1000.  The example uses 1.0.  *Sigh*
	 * We choose to ignore a base outside the limits rather than clamping
	 * and pretending it makes sense.  (That's not to say that we currently
	 * do anything useful with it even if it is in range.)
	 */

	if (state->axis.info &&
	    simple_double (xin, attrs, &base) &&
	    base >= 2 && base <= 1000)
		state->axis.info->logbase = base;
}

/* See bug 743347 for discussion.  */
static void
xlsx_create_axis_object (XLSXReadState *state)
{
	GogPlot *plot;
	char const *type;
	char const *role = NULL;
	gboolean inverted = FALSE;
	gboolean cat_or_date = (state->axis.type == XLSX_AXIS_CAT ||
				state->axis.type == XLSX_AXIS_DATE);
	GogObject *axis;
	gboolean dummy;

	if (state->cur_obj)
		return;

	if (state->axis.info && state->axis.info->axis) {
		axis = GOG_OBJECT (state->axis.obj = state->axis.info->axis);
		/* Replace dummy object.  */
		xlsx_chart_pop_obj (state);
		xlsx_chart_push_obj (state, axis);
#ifdef DEBUG_AXIS
		g_printerr ("Re-using axis object %s with role %s\n",
			    gog_object_get_name (axis), role);
#endif
		return;
	}

	dummy = (!state->axis.info || !state->axis.info->plots);
	if (dummy) {
		plot = NULL;
		type = "GogLinePlot";
	} else {
		plot = state->axis.info->plots->data; /* just use the first */
		type = G_OBJECT_TYPE_NAME (plot);
	}

	switch (xlsx_plottype_from_type_name (type)) {
	case XLSX_PT_GOGRADARPLOT:
	case XLSX_PT_GOGRADARAREAPLOT:
		role = cat_or_date ? "Circular-Axis" : "Radial-Axis";
		break;
	case XLSX_PT_GOGBUBBLEPLOT:
	case XLSX_PT_GOGXYPLOT:
		if (state->axis.info->compass  == GOG_POSITION_N ||
		    state->axis.info->compass  == GOG_POSITION_S)
			role = "X-Axis";
		else
			role = "Y-Axis";
		break;
	case XLSX_PT_GOGBARCOLPLOT:
		/* swap for bar plots */
		g_object_get (G_OBJECT (plot), "horizontal", &inverted, NULL);
		break;

	case XLSX_PT_GOGCONTOURPLOT:
	case XLSX_PT_XLCONTOURPLOT:
		if (state->axis.type == XLSX_AXIS_SER)
			role = "Pseudo-3D-Axis";
		break;

	default:
		break;
	}

	if (NULL == role)
		role = (inverted ^ cat_or_date) ? "X-Axis" : "Y-Axis";

	axis = gog_object_add_by_name (GOG_OBJECT (state->chart), role, NULL);
	state->axis.obj = GOG_AXIS (axis);
#ifdef DEBUG_AXIS
	g_printerr ("Created axis object %s with role %s\n",
		    gog_object_get_name (axis), role);
#endif

	/* Replace dummy object.  */
	xlsx_chart_pop_obj (state);
	xlsx_chart_push_obj (state, axis);

	if (dummy)
		g_object_set (axis, "invisible", TRUE, NULL);

	if (state->axis.info) {
		if (dummy)
			state->axis.info->deleted = TRUE;
		state->axis.info->axis = g_object_ref (state->axis.obj);
		g_hash_table_replace (state->axis.by_obj, axis, state->axis.info);

		g_object_set (G_OBJECT (state->axis.obj),
			      "invisible", state->axis.info->deleted,
			      "invert-axis", state->axis.info->invert_axis, NULL);

		if (state->axis.info->logbase > 0) {
			g_object_set (G_OBJECT (state->axis.obj),
				      "map-name", "Log",
				      NULL);
			/* Base?  */
		}
	}
}


static void
xlsx_axis_pos (GsfXMLIn *xin, xmlChar const **attrs)
{
	static const EnumVal positions[] = {
		{ "t",	 GOG_POSITION_N },
		{ "b",	 GOG_POSITION_S },
		{ "l",	 GOG_POSITION_W },
		{ "r",	 GOG_POSITION_E },
		{ NULL, 0 }
	};
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int position = GOG_POSITION_AUTO;

#ifdef DEBUG_AXIS
	g_printerr ("SET POS %s for %p\n", simple_string (xin, attrs), state->axis.info);
#endif
	/* Apparently no default value.  */
	(void)simple_enum (xin, attrs, positions, &position);
	if (state->axis.info)
		state->axis.info->compass = position;

	if (state->axis.obj == NULL)
		xlsx_create_axis_object (state);
}

static void
xlsx_axis_bound (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	double val;
	GogAxisElemType et = xin->node->user_data.v_int;

	if (state->axis.info && simple_double (xin, attrs, &val)) {
		state->axis.info->axis_elements[et] = val;
		state->axis.info->axis_element_set[et] = TRUE;
	}
}

static void
xlsx_axis_crosses (GsfXMLIn *xin, xmlChar const **attrs)
{
	static const EnumVal crosses[] = {
		{ "autoZero",	GOG_AXIS_CROSS },
		{ "max",	GOG_AXIS_AT_HIGH },
		{ "min",	GOG_AXIS_AT_LOW },
		{ NULL, 0 }
	};
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int cross = GOG_AXIS_CROSS;

	/* No explicit default in docs -- assuming autoZero.  */
	(void)simple_enum (xin, attrs, crosses, &cross);

	if (state->axis.info) {
		state->axis.info->cross = cross;
		if (cross == GOG_AXIS_CROSS)
			state->axis.info->cross_value = 0.;
	}
}

static void
xlsx_axis_crossax (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	const char *axid = simple_string (xin, attrs);
	if (state->axis.info && axid)
		state->axis.info->cross_id = g_strdup (axid);
}

static void
xlsx_axis_custom_unit (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	double f = 1;
	(void)simple_double (xin, attrs, &f);
	if (state->axis.obj && f != 0)
		g_object_set (state->axis.obj, "display-factor", f, NULL);
}

static void
xlsx_axis_builtin_unit (GsfXMLIn *xin, xmlChar const **attrs)
{
	/* It's absolutely moronic, but these values are spelled out.  */
	static const EnumVal units[] = {
		{ "hundreds", 2 },
		{ "thousands", 3 },
		{ "tenThousands", 4 },
		{ "hundredThousands", 5 },
		{ "millions", 6 },
		{ "tenMillions", 7 },
		{ "hundredMillions", 8 },
		{ "billions", 9 },  /* Small billions */
		{ "trillions", 12 },  /* Small trillions */
		{ NULL, 0 }
	};
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int e = 3;

	(void)simple_enum (xin, attrs, units, &e);
	if (state->axis.obj)
		g_object_set (state->axis.obj, "display-factor",
			      go_pow10 (e), NULL);
}

static void
xlsx_chart_gridlines (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	gboolean ismajor = xin->node->user_data.v_int;
	GogObject *grid = gog_object_add_by_name
		(GOG_OBJECT (state->axis.obj),
		 ismajor ? "MajorGrid" : "MinorGrid",
		 NULL);
	xlsx_chart_push_obj (state, grid);
}

static void
xlsx_axis_mark (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	gboolean ismajor = xin->node->user_data.v_int;
	static const EnumVal marks[] = {
		{"none",	0},
		{"in",		1},
		{"out",		2},
		{"cross",	3},
		{NULL, 0}
	};
	int res = 3;

	(void)simple_enum (xin, attrs, marks, &res);
	g_object_set (G_OBJECT (state->axis.obj),
		      (ismajor ? "major-tick-in" : "minor-tick-in"), (res & 1) != 0,
		      (ismajor ? "major-tick-out" : "minor-tick-out"), (res & 2) != 0,
		      NULL);
}

static void
xslx_chart_tick_label_pos (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	static const EnumVal positions[] = {
		{"high",	0},
		{"low",		1},
		{"nextTo",	2},
		{"none",	3},
		{NULL, 0}
	};
	int res = 2;

	(void)simple_enum (xin, attrs, positions, &res);
	g_object_set (G_OBJECT (state->axis.obj), "major-tick-labeled", res != 3, NULL);
}

static void
xlsx_axis_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GogAxis *axis = state->axis.obj;

	if (state->axis.info) {
		GSList *ptr;
		GogAxisElemType et;
		XLSXAxisInfo *info = state->axis.info;

		/* Apply pending attributes.  */

		for (et = GOG_AXIS_ELEM_MIN; et < GOG_AXIS_ELEM_MAX_ENTRY; et++) {
			if (info->axis_element_set[et]) {
				double d = info->axis_elements[et];
				GnmExprTop const *te = gnm_expr_top_new_constant (value_new_float (d));
				gog_dataset_set_dim (GOG_DATASET (axis),
						     et,
						     gnm_go_data_scalar_new_expr (state->sheet, te),
						     NULL);
			}
		}

		for (ptr = info->plots; ptr != NULL ; ptr = ptr->next) {
			GogPlot *plot = ptr->data;
#ifdef DEBUG_AXIS
			g_printerr ("connect plot %p to %p\n", plot, axis);
#endif
			gog_plot_set_axis (plot, axis);
		}
	}

	xlsx_chart_pop_obj (state);

	if (state->axis.info)
		state->axis.info = NULL;
	else if (axis) {
		if (gog_object_is_deletable (GOG_OBJECT (axis))) {
#ifdef DEBUG_AXIS
			g_printerr ("Deleting axis %p\n", axis);
#endif
			gog_object_clear_parent (GOG_OBJECT (axis));
			g_object_unref (axis);
		} else {
#ifdef DEBUG_AXIS
			g_printerr ("Axis %p is not deletable\n", axis);
#endif
		}
	}

	state->axis.obj  = NULL;
}

static void xlsx_chart_area (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs) { xlsx_chart_add_plot (xin, "GogAreaPlot"); }

static void
xlsx_chart_line (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	xlsx_chart_add_plot (xin, "GogLinePlot");
	g_object_set (G_OBJECT (state->plot), "default-style-has-markers", FALSE, NULL);
}

static void
xlsx_chart_line_marker (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	gboolean has_marker = TRUE;

	(void)simple_bool (xin, attrs, &has_marker);
	g_object_set (G_OBJECT (state->plot),
		      "default-style-has-markers", has_marker, NULL);
}

static void
xlsx_chart_xy (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	xlsx_chart_add_plot (xin, "GogXYPlot");
	g_object_set (G_OBJECT (state->plot), "default-style-has-fill", FALSE, NULL);
}

static void
xlsx_scatter_style (GsfXMLIn *xin, xmlChar const **attrs)
{
	enum { SCATTER_LINES = 1, SCATTER_MARKERS = 2, SCATTER_SPLINES = 4 };
	static const EnumVal styles[] = {
		{"line",	SCATTER_LINES },
		{"lineMarker",  SCATTER_LINES | SCATTER_MARKERS },
		{"marker",      SCATTER_MARKERS },
		{"markers",     SCATTER_MARKERS }, /* We used to write this erroneously */
		{"none",	0 },
		{"smooth",      SCATTER_SPLINES },
		{"smoothMarker", SCATTER_SPLINES | SCATTER_MARKERS },
		{NULL, 0}
	};
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int style = SCATTER_MARKERS;

	(void)simple_enum (xin, attrs, styles, &style);
	g_object_set (G_OBJECT (state->plot),
		      "default-style-has-markers", (style & SCATTER_MARKERS) != 0,
		      "default-style-has-lines", (style & SCATTER_LINES) != 0,
		      "use-splines", (style & SCATTER_SPLINES) != 0,
		      NULL);
}

static void
xlsx_chart_bubble (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	xlsx_chart_add_plot (xin, "GogBubblePlot");
}

static void
xlsx_chart_contour (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	// This is called "surfaceChart" in xlsx.
	xlsx_chart_add_plot (xin, "XLContourPlot");
}

static void
xlsx_chart_radar (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	xlsx_chart_add_plot (xin, "GogRadarPlot");
}


static void
xlsx_plot_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	state->plot = NULL;
}

static void
xlsx_chart_ser_start (GsfXMLIn *xin, G_GNUC_UNUSED  xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	state->series = gog_plot_new_series (state->plot);
	xlsx_chart_push_obj (state, GOG_OBJECT (state->series));
}

static void
xlsx_chart_ser_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	xlsx_chart_pop_obj (state);
	state->series = NULL;
}

static void
xlsx_ser_trendline_start (GsfXMLIn *xin, G_GNUC_UNUSED  xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	xlsx_chart_push_obj (state, NULL); /* Dummy object for now. */
}

static void
xlsx_ser_trendline_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	xlsx_chart_pop_obj (state);
}

static void
xlsx_ser_trendline_name (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	const char *name = xin->content->str;
	g_free (state->chart_tx);
	state->chart_tx = g_strdup (name);
}

static void
xlsx_ser_trendline_type (GsfXMLIn *xin, G_GNUC_UNUSED  xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	static const EnumVal styles[] = {
		{"exp",	0 },
		{"linear", 1 },
		{"log", 2 },
		{"movingAvg", 3 },
		{"poly", 4 },
		{"power", 5 },
		{NULL, 0}
	};
	static const char *types[] = {
		"GogExpRegCurve", "GogLinRegCurve", "GogLogRegCurve",
		"GogMovingAvg", "GogPolynomRegCurve", "GogPowerRegCurve"
	};
	int typ = 1;

	(void)simple_enum (xin, attrs, styles, &typ);
	state->cur_obj = GOG_OBJECT (gog_trend_line_new_by_name (types[typ]));
	if (state->cur_obj) {
		GogObject *trend =
			gog_object_add_by_name (GOG_OBJECT (state->series),
						"Trend line",
						state->cur_obj);
		if (state->chart_tx) {
			GOData *dat =
				gnm_go_data_scalar_new_expr (state->sheet,
							     gnm_expr_top_new_constant (value_new_string (state->chart_tx)));
			gog_dataset_set_dim (GOG_DATASET (trend), -1, dat, NULL);
		}
	}

	g_free (state->chart_tx);
	state->chart_tx = NULL;
}

static GogObject *
xlsx_get_trend_eq (XLSXReadState *state)
{
	const char *role = "Equation";
	GogObject *eq = gog_object_get_child_by_name (state->cur_obj, role);

	if (!eq) {
		eq = gog_object_add_by_name (state->cur_obj, role, NULL);
		g_object_set (eq, "show-r2", FALSE, "show-eq", FALSE, NULL);
	}

	return eq;
}


static void
xlsx_ser_trendline_intercept (GsfXMLIn *xin, G_GNUC_UNUSED  xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	double intercept = 1;

	(void)simple_double (xin, attrs, &intercept);
	// We don't have _writeable_ yet.
	if (gnm_object_has_readable_prop (state->cur_obj, "affine", G_TYPE_BOOLEAN, NULL)) {
		g_object_set (state->cur_obj, "affine", intercept != 0, NULL);
	}
}

static void
xlsx_ser_trendline_disprsqr (GsfXMLIn *xin, G_GNUC_UNUSED  xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	gboolean disp = TRUE;

	(void)simple_bool (xin, attrs, &disp);
	g_object_set (xlsx_get_trend_eq (state), "show-r2", disp, NULL);
}

static void
xlsx_ser_trendline_dispeq (GsfXMLIn *xin, G_GNUC_UNUSED  xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	gboolean disp = TRUE;

	(void)simple_bool (xin, attrs, &disp);
	g_object_set (xlsx_get_trend_eq (state), "show-eq", disp, NULL);
}

static void
xlsx_ser_smooth (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	gboolean smooth = TRUE;
	GOLineInterpolation inter;

	(void)simple_bool (xin, attrs, &smooth);
	inter = smooth ? GO_LINE_INTERPOLATION_CUBIC_SPLINE : GO_LINE_INTERPOLATION_LINEAR;
	g_object_set (state->cur_obj,
		      "interpolation", go_line_interpolation_as_str (inter),
		      NULL);
}


static void
xlsx_ser_labels_show_val (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	gboolean has_val = TRUE;

	(void)simple_bool (xin, attrs, &has_val);
	if (GOG_IS_SERIES_LABELS (state->cur_obj) && has_val) {
		GogPlotDesc const *desc = gog_plot_description (state->plot);
		unsigned i;
		char *f, *new_f;
		g_object_get (state->cur_obj, "format", &f, NULL);
		/* seems that xl does not show anything else if a custom label is given */
		if (strstr (f, "%c") != NULL) {
			g_free (f);
			return;
		}
		for (i = 0; i < desc->series.num_dim; i++)
			if (desc->series.dim[i].ms_type == GOG_MS_DIM_VALUES)
				break;
		if (i != desc->series.num_dim) {
			new_f = (f && *f)? g_strdup_printf ("%s%%s%%%d", f, i): g_strdup_printf ("%%%d", i);
			g_object_set (state->cur_obj, "format", new_f, NULL);
			g_free (new_f);
		}
		g_free (f);
	}
}

static void
xlsx_ser_labels_show_cat (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	gboolean has_cat = TRUE;

	(void)simple_bool (xin, attrs, &has_cat);
	if (GOG_IS_SERIES_LABELS (state->cur_obj) && has_cat) {
		GogPlotDesc const *desc = gog_plot_description (state->plot);
		unsigned i;
		char *f, *new_f;
		g_object_get (state->cur_obj, "format", &f, NULL);
		/* seems that xl does not show anything else if a custom label is given */
		if (strstr (f, "%c") != NULL) {
			g_free (f);
			return;
		}
		for (i = 0; i < desc->series.num_dim; i++)
			if (desc->series.dim[i].ms_type == GOG_MS_DIM_CATEGORIES)
				break;
		if (i != desc->series.num_dim) {
			new_f = (f && *f)? g_strdup_printf ("%s%%s%%%d", f, i): g_strdup_printf ("%%%d", i);
			g_object_set (state->cur_obj, "format", new_f, NULL);
			g_free (new_f);
		}
		g_free (f);
	}
}

static void
xlsx_ser_labels_pos (GsfXMLIn *xin, xmlChar const **attrs)
{
	static const EnumVal pos[] = {
		{"b", GOG_SERIES_LABELS_BOTTOM},
		{"bestFit", GOG_SERIES_LABELS_DEFAULT_POS},
		{"ctr", GOG_SERIES_LABELS_CENTERED},
		{"inBase", GOG_SERIES_LABELS_NEAR_ORIGIN},
		{"inEnd", GOG_SERIES_LABELS_INSIDE},
		{"l", GOG_SERIES_LABELS_LEFT},
		{"outEnd", GOG_SERIES_LABELS_OUTSIDE},
		{"r", GOG_SERIES_LABELS_RIGHT},
		{"t", GOG_SERIES_LABELS_TOP},
		{NULL, 0}
	};
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int position = GOG_SERIES_LABELS_DEFAULT_POS;

	/* No documented default.  */
	(void)simple_enum (xin, attrs, pos, &position);
	gog_series_labels_set_position (GOG_SERIES_LABELS (state->cur_obj), position);
}

static void
xlsx_ser_labels_start (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GogObject *data = gog_object_add_by_name (GOG_OBJECT (state->series), "Data labels", NULL);
	GOData *sep = go_data_scalar_str_new (",", FALSE); /* FIXME, should be "\n" for pies */
	gog_dataset_set_dim (GOG_DATASET (data), 1, sep, NULL);
	g_object_set (data, "format", "", "offset", 3, NULL);
	xlsx_chart_push_obj (state, data);
}

static void
xlsx_ser_labels_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	if (!go_style_is_auto (state->cur_style)) {
		/* NOTE: this is a hack, but seems xl uses something like that */
		GSList *children, *ptr;
		children = gog_object_get_children (state->cur_obj, NULL);
		for (ptr = children; ptr; ptr = ptr->next)
			if (go_style_is_auto (go_styled_object_get_style (GO_STYLED_OBJECT (ptr->data))))
			    g_object_set (ptr->data, "style", state->cur_style, NULL);
		g_slist_free (children);
	}
	xlsx_chart_pop_obj (state);
}

static void
xlsx_data_label_pos (GsfXMLIn *xin, xmlChar const **attrs)
{
	static const EnumVal pos[] = {
		{"b", GOG_SERIES_LABELS_BOTTOM},
		{"bestFit", GOG_SERIES_LABELS_DEFAULT_POS},
		{"ctr", GOG_SERIES_LABELS_CENTERED},
		{"inBase", GOG_SERIES_LABELS_NEAR_ORIGIN},
		{"inEnd", GOG_SERIES_LABELS_INSIDE},
		{"l", GOG_SERIES_LABELS_LEFT},
		{"outEnd", GOG_SERIES_LABELS_OUTSIDE},
		{"r", GOG_SERIES_LABELS_RIGHT},
		{"t", GOG_SERIES_LABELS_TOP},
		{NULL, 0}
	};
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int position = GOG_SERIES_LABELS_DEFAULT_POS;

	/* No documented default.  */
	(void)simple_enum (xin, attrs, pos, &position);
	gog_data_label_set_position (GOG_DATA_LABEL (state->cur_obj), position);
}

static void
xlsx_data_label_index (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	unsigned index;
	if (simple_uint (xin, attrs, &index))
		g_object_set (state->cur_obj, "index", index, NULL);
}

static void
xlsx_data_label_show_val (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	gboolean has_val = TRUE;

	(void)simple_bool (xin, attrs, &has_val);
	if (GOG_IS_DATA_LABEL (state->cur_obj) && has_val) {
		GogPlotDesc const *desc = gog_plot_description (state->plot);
		unsigned i;
		char *f, *new_f;
		g_object_get (state->cur_obj, "format", &f, NULL);
		/* seems that xl does not show anything else if a custom label is given */
		if (strstr (f, "%c") != NULL) {
			g_free (f);
			return;
		}
		for (i = 0; i < desc->series.num_dim; i++)
			if (desc->series.dim[i].ms_type == GOG_MS_DIM_VALUES)
				break;
		if (i != desc->series.num_dim) {
			new_f = (f && *f)? g_strdup_printf ("%s%%s%%%d", f, i): g_strdup_printf ("%%%d", i);
			g_object_set (state->cur_obj, "format", new_f, NULL);
			g_free (new_f);
		}
		g_free (f);
	}
}

static void
xlsx_data_label_show_cat (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	gboolean has_cat = TRUE;

	(void)simple_bool (xin, attrs, &has_cat);
	if (GOG_IS_DATA_LABEL (state->cur_obj) && has_cat) {
		GogPlotDesc const *desc = gog_plot_description (state->plot);
		unsigned i;
		char *f, *new_f;
		g_object_get (state->cur_obj, "format", &f, NULL);
		/* seems that xl does not show anything else if a custom label is given */
		if (strstr (f, "%c") != NULL) {
			g_free (f);
			return;
		}
		for (i = 0; i < desc->series.num_dim; i++)
			if (desc->series.dim[i].ms_type == GOG_MS_DIM_CATEGORIES)
				break;
		if (i != desc->series.num_dim) {
			new_f = (f && *f)? g_strdup_printf ("%s%%s%%%d", f, i): g_strdup_printf ("%%%d", i);
			g_object_set (state->cur_obj, "format", new_f, NULL);
			g_free (new_f);
		}
		g_free (f);
	}
}

static void
xlsx_data_label_start (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GogObject *data = gog_object_add_by_name (state->cur_obj, "Point", NULL);
	GOData *sep = go_data_scalar_str_new (",", FALSE); /* FIXME, should be "\n" for pies */
	gog_dataset_set_dim (GOG_DATASET (data), 1, sep, NULL);
	g_object_set (data, "format", "", "offset", 3, NULL);
	xlsx_chart_push_obj (state, data);
}

static void
xlsx_chart_ser_f (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	if (NULL != state->series && state->cur_obj == (GogObject *) state->series) {
		GnmParsePos pp;
		GnmExprTop const *texpr = xlsx_parse_expr (xin, xin->content->str,
			parse_pos_init_sheet (&pp, state->sheet));

		gog_series_set_XL_dim (state->series, state->dim_type,
			((state->dim_type != GOG_MS_DIM_LABELS)
			? gnm_go_data_vector_new_expr (state->sheet, texpr)
			: gnm_go_data_scalar_new_expr (state->sheet, texpr)), NULL);
	} else if (GOG_IS_LABEL (state->cur_obj)) {
		GnmParsePos pp;
		GnmExprTop const *texpr = xlsx_parse_expr (xin, xin->content->str,
			parse_pos_init_sheet (&pp, state->sheet));

		gog_dataset_set_dim (GOG_DATASET (state->cur_obj), 0,
			gnm_go_data_scalar_new_expr (state->sheet, texpr), NULL);
	} else if (GOG_IS_SERIES_LABELS (state->cur_obj)) {
		GnmParsePos pp;
		GnmExprTop const *texpr = xlsx_parse_expr (xin, xin->content->str,
			parse_pos_init_sheet (&pp, state->sheet));
		char *new_f;

		gog_dataset_set_dim (GOG_DATASET (state->cur_obj), 0,
			gnm_go_data_vector_new_expr (state->sheet, texpr), NULL);
		/* seems that xl does not show anything else in that case, even if showVal is there */
		new_f = g_strdup ("%c");
		g_object_set (state->cur_obj, "format", new_f, NULL);
		g_free (new_f);
	} else if (GOG_IS_DATA_LABEL (state->cur_obj)) {
		GnmParsePos pp;
		GnmExprTop const *texpr = xlsx_parse_expr (xin, xin->content->str,
			parse_pos_init_sheet (&pp, state->sheet));
		char *new_f;

		gog_dataset_set_dim (GOG_DATASET (state->cur_obj), 0,
			gnm_go_data_scalar_new_expr (state->sheet, texpr), NULL);
		/* seems that xl does not show anything else in that case, even if showVal is there */
		new_f = g_strdup ("%c");
		g_object_set (state->cur_obj, "format", new_f, NULL);
		g_free (new_f);
	}
}

static void
xlsx_ser_type_start (GsfXMLIn *xin, G_GNUC_UNUSED  xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	state->dim_type = xin->node->user_data.v_int;
}

static void
xlsx_ser_type_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	state->dim_type = GOG_MS_DIM_LABELS;
}

static void
xlsx_chart_legend (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	xlsx_chart_push_obj (state, gog_object_add_by_name (GOG_OBJECT (state->chart), "Legend", NULL));
}

static void
xlsx_chart_legend_pos (GsfXMLIn *xin, xmlChar const **attrs)
{
	static const EnumVal positions[] = {
		{ "t",	 GOG_POSITION_N },
		{ "b",	 GOG_POSITION_S },
		{ "l",	 GOG_POSITION_W },
		{ "r",	 GOG_POSITION_E },
		{ "tr",	 GOG_POSITION_N | GOG_POSITION_E },
		/* adding extra values not in spec, but actually possible */
		{ "rt",	 GOG_POSITION_N | GOG_POSITION_E },
		{ "lt",	 GOG_POSITION_N | GOG_POSITION_W },
		{ "tl",	 GOG_POSITION_N | GOG_POSITION_W },
		{ "rb",	 GOG_POSITION_S | GOG_POSITION_E },
		{ "br",	 GOG_POSITION_S | GOG_POSITION_E },
		{ "lb",	 GOG_POSITION_S | GOG_POSITION_W },
		{ "bl",	 GOG_POSITION_S| GOG_POSITION_W },
		{ NULL, 0 }
	};
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int position = GOG_POSITION_E;

	(void)simple_enum (xin, attrs, positions, &position);
	if (GOG_IS_LEGEND (state->cur_obj))
		gog_object_set_position_flags (state->cur_obj, position, GOG_POSITION_COMPASS);
}

static void
xlsx_chart_pt_start (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	state->series_pt_has_index = FALSE;
	state->series_pt = gog_object_add_by_name (
		GOG_OBJECT (state->series), "Point", NULL);
	xlsx_chart_push_obj (state, state->series_pt);
}

static void
xlsx_chart_pt_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	xlsx_chart_pop_obj (state);
	if (!state->series_pt_has_index && state->series_pt) {
		gog_object_clear_parent (state->series_pt);
		g_object_unref (state->series_pt);
	}
	state->series_pt = NULL;
}

static void
xlsx_chart_pt_index (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	unsigned idx;

	if (simple_uint (xin, attrs, &idx) && state->series_pt) {
		state->series_pt_has_index = TRUE;
		g_object_set (state->series_pt, "index", idx, NULL);
	}
}

static void
xlsx_chart_pt_sep (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	unsigned sep;
	if (simple_uint (xin, attrs, &sep) &&
	    g_object_class_find_property (G_OBJECT_GET_CLASS (state->series_pt), "separation"))
		g_object_set (state->series_pt, "separation", (double)sep / 100.0, NULL);
}

static void
xlsx_style_line_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int w = -1;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_int (xin, attrs, "w", &w))
			; /* Nothing */
	}

	state->sp_type |= GO_STYLE_LINE;
	if (!state->cur_style)
		state->cur_style = (GOStyle *) gog_style_new ();

	if (w == 0) {
		/* Special meaning of zero width  */
		state->cur_style->line.auto_dash = FALSE;
		state->cur_style->line.dash_type = GO_LINE_NONE;
	} else if (w > 0) {
		state->cur_style->line.auto_width = FALSE;
		state->cur_style->line.width = w / 12700.;
	}

	xlsx_chart_push_color_state (state, XLSX_CS_LINE);
}

static void
xlsx_style_line_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	state->sp_type &= ~GO_STYLE_LINE;
	xlsx_chart_pop_color_state (state, XLSX_CS_LINE);
}

static void
xlsx_draw_no_fill (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	if (NULL != state->marker)
		;
	else if (NULL != state->cur_style) {
		if (state->sp_type & GO_STYLE_LINE) {
			state->cur_style->line.dash_type = GO_LINE_NONE;
			state->cur_style->line.auto_dash = FALSE;
		} else {
			state->cur_style->fill.type = GO_STYLE_FILL_NONE;
			state->cur_style->fill.auto_type = FALSE;
		}
	}
}

static void
xlsx_draw_grad_fill (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	if (NULL != state->marker) /* do xlsx support gradients in markers */
		;
	else if (NULL != state->cur_style) {
		if (!(state->sp_type & GO_STYLE_LINE)) {
			state->cur_style->fill.type = GO_STYLE_FILL_GRADIENT;
			state->cur_style->fill.auto_type = FALSE;
			state->gradient_count = 0;
		}
	}
}

static void
xlsx_draw_grad_linear (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int ang = 0, ang_deg;
	GOGradientDirection dir;

	if (!state->cur_style)
		return;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_int (xin, attrs, "ang", &ang))
			; /* Nothing */
	}

	ang_deg = ((ang + 30000) / 60000);
	for (dir = 0; dir < GO_GRADIENT_MAX; dir++) {
		int this_ang = xlsx_gradient_info[dir].angle;
		gboolean this_mirrored = xlsx_gradient_info[dir].mirrored;
		int a;

		if (state->gradient_count != (this_mirrored ? 3 : 2))
			continue;
		/* We cannot distinguish the reversed case */

		/* Different angle convention. */
		this_ang = (360 - this_ang) % (this_mirrored ? 180 : 360);
		a = ang_deg % (this_mirrored ? 180 : 360);
		if (this_ang != a)
			continue;

		state->cur_style->fill.gradient.dir = dir;
		break;
	}

	/* FIXME: we do not support the "scaled" attribute */
}

static void
xlsx_draw_grad_stop (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int pos = 0;
	XLSXColorState s = XLSX_CS_NONE;

	if (!state->cur_style)
		return;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_percent (xin, attrs, "pos", &pos))
			; /* Nothing */
	}

	state->gradient_count++;

	if (state->gradient_count == 1 && pos == 0)
		s = XLSX_CS_FILL_BACK;
	else if (state->gradient_count == 2 && (pos == 50000 || pos == 100000))
		s = XLSX_CS_FILL_FORE;
	else
		s = XLSX_CS_NONE; /* I.e., ignore.  */

	xlsx_chart_push_color_state (state, s);
}

static void
xlsx_draw_grad_stop_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;

	if (!state->cur_style)
		return;

	xlsx_chart_pop_color_state (state, XLSX_CS_ANY);
}

static void
xlsx_draw_solid_fill (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	if (NULL != state->marker) {
		if (!(state->sp_type & GO_STYLE_LINE))
			xlsx_chart_push_color_state (state, XLSX_CS_MARKER);
		else
			xlsx_chart_push_color_state (state, XLSX_CS_MARKER_OUTLINE);
	} else if (state->cur_style) {
		if (state->sp_type & GO_STYLE_FONT)
			xlsx_chart_push_color_state (state, XLSX_CS_FONT);
		else if (state->sp_type & GO_STYLE_LINE) {
			state->cur_style->line.dash_type = GO_LINE_SOLID;
			xlsx_chart_push_color_state (state, XLSX_CS_LINE);
		} else {
			state->cur_style->fill.type = GO_STYLE_FILL_PATTERN;
			state->cur_style->fill.auto_type = FALSE;
			state->cur_style->fill.pattern.pattern = GO_PATTERN_FOREGROUND_SOLID;
			xlsx_chart_push_color_state (state, XLSX_CS_FILL_FORE);
		}
	} else
		  xlsx_chart_push_color_state (state, XLSX_CS_NONE);
}

static void
xlsx_draw_solid_fill_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	xlsx_chart_pop_color_state (state, XLSX_CS_ANY);
}

static void
xlsx_draw_patt_fill (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	static EnumVal const patterns[] = {
		{ "pct5", GO_PATTERN_GREY625 },
		{ "pct10", GO_PATTERN_GREY125 },
		{ "pct20", GO_PATTERN_GREY25 },
		{ "pct25", GO_PATTERN_GREY25 },
		{ "pct30", GO_PATTERN_GREY25 },
		{ "pct40", GO_PATTERN_GREY50 },
		{ "pct50", GO_PATTERN_GREY50 },
		{ "pct60", GO_PATTERN_GREY50 },
		{ "pct70", GO_PATTERN_GREY75 },
		{ "pct75", GO_PATTERN_GREY75 },
		{ "pct80", GO_PATTERN_GREY75 },
		{ "pct90", GO_PATTERN_GREY75 },
		{ "horz", GO_PATTERN_HORIZ },
		{ "vert", GO_PATTERN_VERT },
		{ "ltHorz", GO_PATTERN_THIN_HORIZ },
		{ "ltVert", GO_PATTERN_THIN_VERT },
		{ "dkHorz", GO_PATTERN_HORIZ },
		{ "dkVert", GO_PATTERN_VERT },
		{ "narHorz", GO_PATTERN_HORIZ },
		{ "narVert", GO_PATTERN_VERT },
		{ "dashHorz", GO_PATTERN_THIN_HORIZ },
		{ "dashVert", GO_PATTERN_THIN_VERT },
		{ "cross", -1 },
		{ "dnDiag", GO_PATTERN_REV_DIAG },
		{ "upDiag", GO_PATTERN_DIAG },
		{ "ltDnDiag", GO_PATTERN_THIN_REV_DIAG },
		{ "ltUpDiag", GO_PATTERN_THIN_DIAG },
		{ "dkDnDiag", GO_PATTERN_REV_DIAG },
		{ "dkUpDiag", GO_PATTERN_DIAG },
		{ "wdDnDiag", GO_PATTERN_REV_DIAG },
		{ "wdUpDiag", GO_PATTERN_DIAG },
		{ "dashDnDiag", GO_PATTERN_THIN_REV_DIAG },
		{ "dashUpDiag", GO_PATTERN_THIN_DIAG },
		{ "diagCross", GO_PATTERN_DIAG_CROSS },
		{ "smCheck", -1 },
		{ "lgCheck", -1 },
		{ "smGrid", GO_PATTERN_THIN_HORIZ_CROSS },
		{ "lgGrid", GO_PATTERN_THIN_HORIZ_CROSS },
		{ "dotGrid", -1 },
		{ "smConfetti", GO_PATTERN_SMALL_CIRCLES },
		{ "lgConfetti", GO_PATTERN_SMALL_CIRCLES },
		{ "horzBrick", GO_PATTERN_BRICKS },
		{ "diagBrick", GO_PATTERN_BRICKS },
		{ "solidDmnd", -1 },
		{ "openDmnd", GO_PATTERN_THIN_DIAG_CROSS },
		{ "dotDmnd", -1 },
		{ "plaid", -1 },
		{ "sphere", GO_PATTERN_LARGE_CIRCLES },
		{ "weave", GO_PATTERN_THATCH },
		{ "divot", -1 },
		{ "shingle", GO_PATTERN_SEMI_CIRCLES },
		{ "wave", -1 },
		{ "trellis", GO_PATTERN_THICK_DIAG_CROSS },
		{ "zigZag", -1 },
		{ NULL, 0 }
	};
	int pat = -1;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_enum (xin, attrs, "prst", patterns, &pat)) {
			/* Nothing */
		}
	}

	state->cur_style->fill.type = GO_STYLE_FILL_PATTERN;
	state->cur_style->fill.auto_type = (pat < 0);
	state->cur_style->fill.pattern.pattern = (pat < 0 ? GO_PATTERN_SOLID : pat);
}

static void
xlsx_draw_patt_fill_clr (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	gboolean is_fg = xin->node->user_data.v_int;

	xlsx_chart_push_color_state (state, is_fg ? XLSX_CS_FILL_FORE : XLSX_CS_FILL_BACK);
}

static void
xlsx_draw_patt_fill_clr_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	xlsx_chart_pop_color_state (state, XLSX_CS_ANY);
}

static void
xlsx_draw_line_headtail (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	gboolean is_tail = xin->node->user_data.v_int;

	static EnumVal const types[] = {
		{ "arrow", GO_ARROW_KITE },
		{ "diamond", GO_ARROW_KITE },
		{ "none", GO_ARROW_NONE },
		{ "oval", GO_ARROW_OVAL },
		{ "stealth", GO_ARROW_KITE },
		{ "triangle", GO_ARROW_KITE },
		{ NULL, 0 }
	};
	static EnumVal const sizes[] = {
		{ "sm", 0 },
		{ "med", 1 },
		{ "lg", 2 },
		{ NULL, 0 }
	};
	int typ = XL_ARROW_NONE;
	int w = 1, l = 1;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_enum (xin, attrs, "type", types, &typ) ||
		    attr_enum (xin, attrs, "w", sizes, &w) ||
		    attr_enum (xin, attrs, "len", sizes, &l)) {
			/* Nothing */
		}
	}

	if (GNM_IS_SO_LINE (state->so)) {
		GOArrow arrow;
		GOStyle const *style = state->cur_style;
		double width = style->line.auto_width ? 0 : style->line.width;
		xls_arrow_from_xl (&arrow, width, typ, l, w);
		g_object_set (state->so,
			      (is_tail ? "end-arrow" : "start-arrow"), &arrow,
			      NULL);
	}
}


static void
xlsx_draw_line_dash (GsfXMLIn *xin, xmlChar const **attrs)
{
	static const EnumVal dashes[] = {
		{ "solid",		GO_LINE_SOLID },
		{ "dot",		GO_LINE_DOT },
		{ "dash",		GO_LINE_DASH },
		{ "lgDash",		GO_LINE_LONG_DASH },
		{ "dashDot",		GO_LINE_DASH_DOT },
		{ "lgDashDot",		GO_LINE_DASH_DOT_DOT },
		{ "lgDashDotDot",	GO_LINE_DASH_DOT_DOT_DOT },
		{ "sysDash",		GO_LINE_S_DASH },
		{ "sysDot",		GO_LINE_S_DOT },
		{ "sysDashDot",		GO_LINE_S_DASH_DOT },
		{ "sysDashDotDot",	GO_LINE_S_DASH_DOT_DOT },
		{ NULL, 0 }
	};
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int dash = GO_LINE_SOLID;

	/* No documented default -- solid seems reasonable.   */
	(void)simple_enum (xin, attrs, dashes, &dash);

	if (NULL != state->marker)
		; /* what goes here ?*/
	else if (NULL != state->cur_style) {
		if (state->sp_type & GO_STYLE_LINE) {
			state->cur_style->line.auto_dash = FALSE;
			state->cur_style->line.dash_type = dash;
		} else {
			; /* what goes here ?*/
		}
	}
}

static void
xlsx_chart_marker_start (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	state->marker = go_marker_new ();
}

static void
xlsx_chart_marker_symbol (GsfXMLIn *xin, xmlChar const **attrs)
{
	static const EnumVal symbols[] = {
		{ "circle",	GO_MARKER_CIRCLE },
		{ "dash",	GO_MARKER_BAR },		/* FIXME */
		{ "diamond",	GO_MARKER_DIAMOND },
		{ "dot",	GO_MARKER_HALF_BAR },		/* FIXME */
		{ "none",	GO_MARKER_NONE },
		{ "plus",	GO_MARKER_CROSS },		/* CHECK ME */
		{ "square",	GO_MARKER_SQUARE },
		{ "star",	GO_MARKER_ASTERISK },		/* CHECK ME */
		{ "triangle",	GO_MARKER_TRIANGLE_UP },
		{ "x",		GO_MARKER_X },
		{ "auto",       GO_MARKER_MAX },  /* Not in all versions of spec. */
		{ NULL, 0 }
	};
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int symbol = GO_MARKER_MAX;

	simple_enum (xin, attrs, symbols, &symbol);
	if (NULL != state->marker) {
		if (symbol < GO_MARKER_MAX) {
			go_marker_set_shape (state->marker, symbol);
			state->cur_style->marker.auto_shape = FALSE;
		} else
			state->cur_style->marker.auto_shape = TRUE;
	}
}

static void
xlsx_chart_marker_size (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	unsigned sz = 5;  /* Documented default. */

	(void)simple_uint (xin, attrs, &sz);
	go_marker_set_size (state->marker, CLAMP (sz, 2, 72));
}

static void
xlsx_chart_marker_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	if (NULL != state->cur_obj && GOG_IS_STYLED_OBJECT (state->cur_obj)) {
		go_style_set_marker (state->cur_style, state->marker);
	}
	state->marker = NULL;
}

static void
xlsx_plot_area (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	/* Push a NULL object for backplane.  */
	xlsx_chart_push_obj (state, NULL);
}

static void
xlsx_plot_area_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GogObject *bp = state->cur_obj;
	GOStyle const *s = state->cur_style;
	gboolean delete;

	delete = (GOG_IS_GRID (bp) &&
		  !go_style_is_fill_visible (s) &&
		  gog_object_is_deletable (bp));
	if (delete)
		gog_object_clear_parent (bp);

	xlsx_chart_pop_obj (state);

	if (delete)
		g_object_unref (bp);  /* from _clear_parent. */
}


static void
xlsx_chart_pop (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	xlsx_chart_pop_obj ((XLSXReadState *)xin->user_state);
}

static void
xlsx_chart_layout_manual (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GogViewAllocation alloc;

	/* FIXME: we don't take xMode and yMode into account for now */

	alloc.x = state->chart_pos[0];
	alloc.w = state->chart_pos[1];
	alloc.y = state->chart_pos[2];
	alloc.h = state->chart_pos[3];

	if (state->cur_obj == NULL) {
		/* plot area position, see xlsx_plot_area() and #748016 */

		if (0 && state->chart_pos_mode[0]) {
			alloc.x = 0;
			alloc.w = 1;
		}
		if (0 && state->chart_pos_mode[2]) {
			alloc.y = 0;
			alloc.h = 1;
		}

		if (state->chart_pos_target) {/* inner mode */
			gog_chart_set_plot_area (state->chart, &alloc);
		}
		/* else FIXME: how can we deal cleanly with an outer target? */
	} else {
		/* FIXME: this is correct only for chart children */
		gog_object_set_position_flags (state->cur_obj, GOG_POSITION_MANUAL, GOG_POSITION_ANY_MANUAL);
		gog_object_set_manual_position (state->cur_obj, &alloc);
	}

	xlsx_reset_chart_pos (state);
}

static void
xlsx_chart_layout_target (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	static const EnumVal choices[] = {
		{ "outer",	FALSE },
		{ "inner",	TRUE },
		{ NULL, 0 }
	};
	int choice = FALSE;
	(void)simple_enum (xin, attrs, choices, &choice);
	state->chart_pos_target = choice;
}

static void
xlsx_chart_layout_dim (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	simple_double (xin, attrs, state->chart_pos + xin->node->user_data.v_int);
}

static void
xlsx_chart_layout_mode (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	static const EnumVal choices[] = {
		{ "factor",	FALSE },
		{ "edge",	TRUE },
		{ NULL, 0 }
	};
	int choice = FALSE;

	(void)simple_enum (xin, attrs, choices, &choice);
	state->chart_pos_mode[xin->node->user_data.v_int] = choice;
}

static void
xlsx_ext_gostyle (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GOStyle *style = state->cur_style;
	GOArrow *start_arrow = NULL;
	GOArrow *end_arrow = NULL;
	gboolean has_arrow = GNM_IS_SO_LINE (state->so);
	int rev_gradient = 0;

	if (!style)
		return;

	if (has_arrow)
		g_object_get (state->so, "start_arrow", &start_arrow, "end_arrow", &end_arrow, NULL);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		gnm_float f;
		int i;

		if (strcmp (attrs[0], "pattern") == 0) {
			GOPatternType p = go_pattern_from_str (attrs[1]);
			if (style->fill.pattern.pattern == GO_PATTERN_FOREGROUND_SOLID &&
			    p == GO_PATTERN_SOLID) {
				/* We read the wrong color */
				style->fill.pattern.back = style->fill.pattern.fore;
				style->fill.auto_back = style->fill.auto_fore;
				style->fill.auto_fore = TRUE;
				style->fill.pattern.fore = GO_COLOR_BLACK;
			}
			style->fill.pattern.pattern = p;
		} else if (attr_bool (xin, attrs, "auto-pattern", &i)) {
			style->fill.auto_type = i != 0;
		} else if (attr_bool (xin, attrs, "auto-back", &i)) {
			style->fill.auto_back = i != 0;
		} else if (start_arrow && strcmp (attrs[0], "StartArrowType") == 0) {
			start_arrow->typ = go_arrow_type_from_str (attrs[1]);
		} else if (start_arrow && attr_float (xin, attrs, "StartArrowShapeA", &f)) {
			start_arrow->a = f;
		} else if (start_arrow && attr_float (xin, attrs, "StartArrowShapeB", &f)) {
			start_arrow->b = f;
		} else if (start_arrow && attr_float (xin, attrs, "StartArrowShapeC", &f)) {
			start_arrow->c = f;
		} else if (start_arrow && strcmp (attrs[0], "EndArrowType") == 0) {
			end_arrow->typ = go_arrow_type_from_str (attrs[1]);
		} else if (end_arrow && attr_float (xin, attrs, "EndArrowShapeA", &f)) {
			end_arrow->a = f;
		} else if (end_arrow && attr_float (xin, attrs, "EndArrowShapeB", &f)) {
			end_arrow->b = f;
		} else if (end_arrow && attr_float (xin, attrs, "EndArrowShapeC", &f)) {
			end_arrow->c = f;
		} else if (attr_bool (xin, attrs, "reverse-gradient", &rev_gradient)) {
			/* Nothing */
		} else if (strcmp (attrs[0], "markerSymbol") == 0) {
			const char *s = attrs[1];
			if (strcmp (s, "auto") == 0) {
				style->marker.auto_shape = TRUE;
			} else {
				style->marker.auto_shape = FALSE;
				go_marker_set_shape (state->marker, go_marker_shape_from_str (s));
			}
		} else if (strcmp (attrs[0], "dashType") == 0) {
			const char *s = attrs[1];
			if (strcmp (s, "auto") == 0) {
				style->line.auto_dash = TRUE;
			} else {
				style->line.auto_dash = FALSE;
				style->line.dash_type = go_line_dash_from_str (s);
			}
		}
	}

	if (has_arrow) {
		g_object_set (state->so, "start_arrow", start_arrow, "end_arrow", end_arrow, NULL);
		g_free (start_arrow);
		g_free (end_arrow);
	}

	if (rev_gradient) {
		GOGradientDirection dir0 = style->fill.gradient.dir;
		GOGradientDirection dir;
		for (dir = 0; dir < GO_GRADIENT_MAX; dir++) {
			if (xlsx_gradient_info[dir0].angle == xlsx_gradient_info[dir].angle &&
			    xlsx_gradient_info[dir0].mirrored == xlsx_gradient_info[dir].mirrored &&
			    xlsx_gradient_info[dir0].reversed == !xlsx_gradient_info[dir].reversed) {
				GOColor c = style->fill.pattern.back;
				gboolean a = style->fill.auto_back;
				style->fill.gradient.dir = dir;
				style->fill.pattern.back = style->fill.pattern.fore;
				style->fill.pattern.fore = c;
				style->fill.auto_back = style->fill.auto_fore;
				style->fill.auto_fore = a;
				break;
			}
		}
	}
}

static GsfXMLInNode const xlsx_chart_dtd[] =
{
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE_FULL (START, CHART_SPACE, XL_NS_CHART, "chartSpace", GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
  GSF_XML_IN_NODE (CHART_SPACE, DATE1904, XL_NS_CHART, "date1904", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (CHART_SPACE, ROUNDEDCORNERS, XL_NS_CHART, "roundedCorners", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (CHART_SPACE, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (SHAPE_PR, SP_XFRM, XL_NS_DRAW, "xfrm", GSF_XML_NO_CONTENT, &xlsx_sppr_xfrm, NULL),
      GSF_XML_IN_NODE (SP_XFRM, SP_XFRM_OFF, XL_NS_DRAW, "off", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (SP_XFRM, SP_XFRM_EXT, XL_NS_DRAW, "ext", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (SHAPE_PR, SP_PR_PRST_GEOM, XL_NS_DRAW, "prstGeom", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (SHAPE_PR, FILL_NONE,	XL_NS_DRAW, "noFill", GSF_XML_NO_CONTENT, &xlsx_draw_no_fill, NULL),
    GSF_XML_IN_NODE (SHAPE_PR, FILL_SOLID,	XL_NS_DRAW, "solidFill", GSF_XML_NO_CONTENT, &xlsx_draw_solid_fill, &xlsx_draw_solid_fill_end),
      GSF_XML_IN_NODE (FILL_SOLID, COLOR_THEMED, XL_NS_DRAW, "schemeClr", GSF_XML_NO_CONTENT, &xlsx_draw_color_themed, NULL),
        COLOR_MODIFIER_NODES(COLOR_THEMED,1),
      GSF_XML_IN_NODE (FILL_SOLID, COLOR_RGB,	 XL_NS_DRAW, "srgbClr", GSF_XML_NO_CONTENT, &xlsx_draw_color_rgb, NULL),
        COLOR_MODIFIER_NODES(COLOR_RGB,0),
      GSF_XML_IN_NODE (FILL_SOLID, LN_DASH,	   XL_NS_DRAW, "prstDash", GSF_XML_NO_CONTENT, &xlsx_draw_line_dash, NULL),

    GSF_XML_IN_NODE (SHAPE_PR, SP_EFFECTLST, XL_NS_DRAW, "effectLst", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (SHAPE_PR, FILL_BLIP,	XL_NS_DRAW, "blipFill", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (FILL_BLIP, FILL_BLIP_BLIP,	XL_NS_DRAW, "blip", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (FILL_BLIP, FILL_BLIP_SRC,	XL_NS_DRAW, "srcRect", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (FILL_BLIP, FILL_BLIP_STRETCH,	XL_NS_DRAW, "stretch", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (FILL_BLIP_STRETCH, BLIP_FILL_RECT, XL_NS_DRAW, "fillRect", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (FILL_BLIP, FILL_BLIP_TILE,	XL_NS_DRAW, "tile", GSF_XML_NO_CONTENT, NULL, NULL),

    GSF_XML_IN_NODE (SHAPE_PR, FILL_GRAD,	XL_NS_DRAW, "gradFill", GSF_XML_NO_CONTENT, &xlsx_draw_grad_fill, NULL),
      GSF_XML_IN_NODE (FILL_GRAD, GRAD_LIST,	XL_NS_DRAW, "gsLst", GSF_XML_NO_CONTENT, NULL, NULL),
       GSF_XML_IN_NODE (GRAD_LIST, GRAD_LIST_ITEM, XL_NS_DRAW, "gs", GSF_XML_NO_CONTENT, xlsx_draw_grad_stop, xlsx_draw_grad_stop_end),
         GSF_XML_IN_NODE (GRAD_LIST_ITEM, COLOR_RGB, XL_NS_DRAW, "srgbClr", GSF_XML_2ND, NULL, NULL),
	 GSF_XML_IN_NODE (GRAD_LIST_ITEM, COLOR_SYS, XL_NS_DRAW, "sysClr", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (FILL_GRAD, GRAD_LIN,	XL_NS_DRAW, "lin", GSF_XML_NO_CONTENT, &xlsx_draw_grad_linear, NULL),
      GSF_XML_IN_NODE (FILL_GRAD, GRAD_TILE,	XL_NS_DRAW, "tileRect", GSF_XML_NO_CONTENT, NULL, NULL),

    GSF_XML_IN_NODE (SHAPE_PR, FILL_PATT,	XL_NS_DRAW, "pattFill", GSF_XML_NO_CONTENT, &xlsx_draw_patt_fill, NULL),
      GSF_XML_IN_NODE_FULL (FILL_PATT, FILL_PATT_BG,	XL_NS_DRAW, "bgClr", GSF_XML_NO_CONTENT, FALSE, TRUE, &xlsx_draw_patt_fill_clr, &xlsx_draw_patt_fill_clr_end, FALSE),
        GSF_XML_IN_NODE (FILL_PATT_BG, COLOR_RGB, XL_NS_DRAW, "srgbClr", GSF_XML_2ND, NULL, NULL),
      GSF_XML_IN_NODE_FULL (FILL_PATT, FILL_PATT_FG,	XL_NS_DRAW, "fgClr", GSF_XML_NO_CONTENT, FALSE, TRUE, &xlsx_draw_patt_fill_clr, &xlsx_draw_patt_fill_clr_end, TRUE),
        GSF_XML_IN_NODE (FILL_PATT_FG, COLOR_RGB, XL_NS_DRAW, "srgbClr", GSF_XML_2ND, NULL, NULL),

    GSF_XML_IN_NODE (SHAPE_PR, SHAPE_PR_LN, XL_NS_DRAW, "ln", GSF_XML_NO_CONTENT, &xlsx_style_line_start, &xlsx_style_line_end),
      GSF_XML_IN_NODE (SHAPE_PR_LN, LN_NOFILL, XL_NS_DRAW, "noFill", GSF_XML_NO_CONTENT, &xlsx_draw_no_fill, NULL),
      GSF_XML_IN_NODE (SHAPE_PR_LN, LN_DASH, XL_NS_DRAW, "prstDash", GSF_XML_2ND, NULL, NULL),
      GSF_XML_IN_NODE (SHAPE_PR_LN, FILL_SOLID, XL_NS_DRAW, "solidFill", GSF_XML_2ND, NULL, NULL),
      GSF_XML_IN_NODE (SHAPE_PR_LN, FILL_PATT,	XL_NS_DRAW, "pattFill", GSF_XML_2ND, NULL, NULL),
      GSF_XML_IN_NODE (SHAPE_PR_LN, LN_MITER,	XL_NS_DRAW, "miter", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (SHAPE_PR_LN, LN_ROUND,	XL_NS_DRAW, "round", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (SHAPE_PR_LN, LN_HEAD,	XL_NS_DRAW, "headEnd", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (SHAPE_PR_LN, LN_TAIL,	XL_NS_DRAW, "tailEnd", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (SHAPE_PR, TEXT_PR, XL_NS_CHART, "txPr", GSF_XML_NO_CONTENT, &xlsx_tx_pr, &xlsx_tx_pr_end),
      GSF_XML_IN_NODE (TEXT_PR, TEXT_PR_BODY,	XL_NS_DRAW, "bodyPr", GSF_XML_NO_CONTENT, &xlsx_body_pr, NULL),
      GSF_XML_IN_NODE (TEXT_PR, TEXT_PR_STYLE,	XL_NS_DRAW, "lstStyle", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (TEXT_PR, TEXT_PR_P,	XL_NS_DRAW, "p", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (TEXT_PR_P, PR_P_PR,	XL_NS_DRAW, "pPr", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (PR_P_PR, PR_P_PR_DEF, XL_NS_DRAW, "defRPr", GSF_XML_NO_CONTENT, &xlsx_draw_text_run_props, NULL),
            GSF_XML_IN_NODE (PR_P_PR_DEF, PR_P_PR_DEF_CS, XL_NS_DRAW, "cs", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (PR_P_PR_DEF, PR_P_PR_DEF_EA, XL_NS_DRAW, "ea", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (PR_P_PR_DEF, PR_P_PR_DEF_LATIN, XL_NS_DRAW, "latin", GSF_XML_NO_CONTENT, &xlsx_rpr_latin, NULL),
	    GSF_XML_IN_NODE (PR_P_PR_DEF, FILL_SOLID, XL_NS_DRAW, "solidFill", GSF_XML_2ND, NULL, NULL),
            GSF_XML_IN_NODE (PR_P_PR_DEF, PR_P_PR_DEF_UFILLTX, XL_NS_DRAW, "uFillTx", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (PR_P_PR_DEF, PR_P_PR_DEF_ULNTX, XL_NS_DRAW, "uLnTx", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (PR_P_PR, PR_P_PR_LNSPC, XL_NS_DRAW, "lnSpc", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (PR_P_PR_LNSPC, LNSPC_SPCPCT, XL_NS_DRAW, "spcPct", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (PR_P_PR, PR_P_PR_SPCBEF, XL_NS_DRAW, "spcBef", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (PR_P_PR_SPCBEF, SPCBEF_SPCPTS, XL_NS_DRAW, "spcPts", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (PR_P_PR, PR_P_PR_SPCAFT, XL_NS_DRAW, "spcAft", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (PR_P_PR_SPCAFT, SPCBEF_SPCPTS, XL_NS_DRAW, "spcPts", GSF_XML_2ND, NULL, NULL),
	  GSF_XML_IN_NODE (PR_P_PR, PR_P_PR_TABLST, XL_NS_DRAW, "tabLst", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (TEXT_PR_P, PR_P_PR_END,XL_NS_DRAW, "endParaRPr", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (SHAPE_PR, EXTLST, XL_NS_DRAW, "extLst", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (EXTLST, EXTITEM, XL_NS_DRAW, "ext", GSF_XML_NO_CONTENT, &xlsx_ext_begin, &xlsx_ext_end),
        GSF_XML_IN_NODE (EXTITEM, EXT_GOSTYLE, XL_NS_GNM_EXT, "gostyle", GSF_XML_NO_CONTENT, &xlsx_ext_gostyle, NULL),

  GSF_XML_IN_NODE (CHART_SPACE, TEXT_PR, XL_NS_CHART, "txPr", GSF_XML_2ND, NULL, NULL),

  GSF_XML_IN_NODE (CHART_SPACE, CHART, XL_NS_CHART, "chart", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (CHART, SHOW_DBLS_OVER_MAX, XL_NS_CHART, "showDLblsOverMax", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (CHART, FLOOR, XL_NS_CHART, "floor", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (FLOOR, THICKNESS, XL_NS_CHART, "thickness", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (FLOOR, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_2ND, NULL, NULL),
    GSF_XML_IN_NODE (CHART, SIDEWALL, XL_NS_CHART, "sideWall", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (SIDEWALL, THICKNESS, XL_NS_CHART, "thickness", GSF_XML_2ND, NULL, NULL),
      GSF_XML_IN_NODE (SIDEWALL, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_2ND, NULL, NULL),
    GSF_XML_IN_NODE (CHART, BACKWALL, XL_NS_CHART, "backWall", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (BACKWALL, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_2ND, NULL, NULL),
      GSF_XML_IN_NODE (BACKWALL, THICKNESS, XL_NS_CHART, "thickness", GSF_XML_2ND, NULL, NULL),
    GSF_XML_IN_NODE (CHART, PLOTAREA, XL_NS_CHART, "plotArea", GSF_XML_NO_CONTENT, &xlsx_plot_area, &xlsx_plot_area_end),
      GSF_XML_IN_NODE (PLOTAREA, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_2ND, NULL, NULL),
      GSF_XML_IN_NODE_FULL (PLOTAREA, CAT_AXIS, XL_NS_CHART, "catAx", GSF_XML_NO_CONTENT, FALSE, TRUE,
			    &xlsx_axis_start, &xlsx_axis_end, XLSX_AXIS_CAT),
        GSF_XML_IN_NODE (CAT_AXIS, AXIS_AXID, XL_NS_CHART, "axId", GSF_XML_NO_CONTENT, &xlsx_axis_id, NULL),
        GSF_XML_IN_NODE (CAT_AXIS, AXIS_SCALING, XL_NS_CHART, "scaling", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE_FULL (AXIS_SCALING, AX_MIN, XL_NS_CHART, "min", GSF_XML_NO_CONTENT, FALSE, TRUE,
				&xlsx_axis_bound, NULL, GOG_AXIS_ELEM_MIN),
          GSF_XML_IN_NODE_FULL (AXIS_SCALING, AX_MAX, XL_NS_CHART, "max", GSF_XML_NO_CONTENT, FALSE, TRUE,
				&xlsx_axis_bound, NULL, GOG_AXIS_ELEM_MAX),
          GSF_XML_IN_NODE (AXIS_SCALING, AX_LOG, XL_NS_CHART, "logBase", GSF_XML_NO_CONTENT, &xlsx_chart_logbase, NULL),
          GSF_XML_IN_NODE (AXIS_SCALING, AX_ORIENTATION, XL_NS_CHART, "orientation", GSF_XML_NO_CONTENT, &xlsx_axis_orientation, NULL),
        GSF_XML_IN_NODE (CAT_AXIS, AXIS_DELETE, XL_NS_CHART, "delete", GSF_XML_NO_CONTENT, &xlsx_axis_delete, NULL),
        GSF_XML_IN_NODE (CAT_AXIS, AXIS_POS, XL_NS_CHART, "axPos", GSF_XML_NO_CONTENT, &xlsx_axis_pos, NULL),
        GSF_XML_IN_NODE_FULL (CAT_AXIS, AXIS_MAJORGRID, XL_NS_CHART, "majorGridlines", GSF_XML_NO_CONTENT,
			      FALSE, FALSE, &xlsx_chart_gridlines, &xlsx_chart_pop, 1),
          GSF_XML_IN_NODE (AXIS_MAJORGRID, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE_FULL (CAT_AXIS, AXIS_MINORGRID, XL_NS_CHART, "minorGridlines", GSF_XML_NO_CONTENT,
			      FALSE, FALSE, &xlsx_chart_gridlines, &xlsx_chart_pop, 0),
          GSF_XML_IN_NODE (AXIS_MINORGRID, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (CAT_AXIS, TITLE, XL_NS_CHART, "title", GSF_XML_NO_CONTENT, &xlsx_chart_title_start, xlsx_chart_title_end),		/* ID is used */
	  GSF_XML_IN_NODE (TITLE, OVERLAY, XL_NS_CHART, "overlay", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (TITLE, LAYOUT, XL_NS_CHART, "layout", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (LAYOUT, LAST_LAYOUT,	    XL_NS_CHART, "lastLayout", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (LAST_LAYOUT, LAYOUT_X, XL_NS_CHART, "x", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (LAST_LAYOUT, LAYOUT_Y, XL_NS_CHART, "y", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (LAST_LAYOUT, LAYOUT_W, XL_NS_CHART, "w", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (LAST_LAYOUT, LAYOUT_H, XL_NS_CHART, "h", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (LAYOUT, LAST_LAYOUT_OUTER, XL_NS_CHART, "lastLayoutOuter", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (LAST_LAYOUT_OUTER, LAYOUT_X, XL_NS_CHART, "x", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (LAST_LAYOUT_OUTER, LAYOUT_Y, XL_NS_CHART, "y", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (LAST_LAYOUT_OUTER, LAYOUT_W, XL_NS_CHART, "w", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (LAST_LAYOUT_OUTER, LAYOUT_H, XL_NS_CHART, "h", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (LAYOUT, MAN_LAYOUT, XL_NS_CHART, "manualLayout", GSF_XML_NO_CONTENT, NULL, &xlsx_chart_layout_manual),
	      GSF_XML_IN_NODE (MAN_LAYOUT, MAN_LAYOUT_TARGET, XL_NS_CHART, "layoutTarget", GSF_XML_NO_CONTENT, &xlsx_chart_layout_target, NULL),
              GSF_XML_IN_NODE_FULL (MAN_LAYOUT, MAN_LAYOUT_H, XL_NS_CHART, "h", GSF_XML_NO_CONTENT, FALSE, TRUE, &xlsx_chart_layout_dim, NULL, 3),
              GSF_XML_IN_NODE_FULL (MAN_LAYOUT, MAN_LAYOUT_HMODE, XL_NS_CHART, "hMode", GSF_XML_NO_CONTENT, FALSE, TRUE, &xlsx_chart_layout_mode, NULL, 3),
              GSF_XML_IN_NODE_FULL (MAN_LAYOUT, MAN_LAYOUT_W, XL_NS_CHART, "w", GSF_XML_NO_CONTENT, FALSE, TRUE, &xlsx_chart_layout_dim, NULL, 1),
              GSF_XML_IN_NODE_FULL (MAN_LAYOUT, MAN_LAYOUT_WMODE, XL_NS_CHART, "wMode", GSF_XML_NO_CONTENT, FALSE, TRUE, &xlsx_chart_layout_mode, NULL, 1),
              GSF_XML_IN_NODE_FULL (MAN_LAYOUT, MAN_LAYOUT_X, XL_NS_CHART, "x", GSF_XML_NO_CONTENT, FALSE, TRUE, &xlsx_chart_layout_dim, NULL, 0),
              GSF_XML_IN_NODE_FULL (MAN_LAYOUT, MAN_LAYOUT_XMODE, XL_NS_CHART, "xMode", GSF_XML_NO_CONTENT, FALSE, TRUE, &xlsx_chart_layout_mode, NULL, 0),
              GSF_XML_IN_NODE_FULL (MAN_LAYOUT, MAN_LAYOUT_Y, XL_NS_CHART, "y", GSF_XML_NO_CONTENT, FALSE, TRUE, &xlsx_chart_layout_dim, NULL, 2),
              GSF_XML_IN_NODE_FULL (MAN_LAYOUT, MAN_LAYOUT_YMODE, XL_NS_CHART, "yMode", GSF_XML_NO_CONTENT, FALSE, TRUE,  &xlsx_chart_layout_mode, NULL, 2),
              GSF_XML_IN_NODE (TITLE, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_2ND, NULL, NULL),
              GSF_XML_IN_NODE (TITLE, TEXT, XL_NS_CHART, "tx", GSF_XML_NO_CONTENT, &xlsx_chart_text_start, &xlsx_chart_text),
            GSF_XML_IN_NODE (TEXT, TEXT_VALUE, XL_NS_CHART, "v", GSF_XML_CONTENT, NULL, &xlsx_text_value),
            GSF_XML_IN_NODE (TEXT, TX_RICH, XL_NS_CHART, "rich", GSF_XML_NO_CONTENT, NULL, NULL),
              GSF_XML_IN_NODE (TX_RICH, TX_RICH_BODY, XL_NS_CHART, "bodyP", GSF_XML_NO_CONTENT, NULL, NULL),
              GSF_XML_IN_NODE (TX_RICH, TEXT_PR_BODY, XL_NS_DRAW, "bodyPr", GSF_XML_NO_CONTENT, NULL, NULL),
              GSF_XML_IN_NODE (TX_RICH, TX_RICH_STYLES, XL_NS_DRAW, "lstStyle", GSF_XML_NO_CONTENT, NULL, NULL),
              GSF_XML_IN_NODE (TX_RICH, TX_RICH_P, XL_NS_DRAW, "p", GSF_XML_NO_CONTENT, &xlsx_chart_p_start, NULL),
                GSF_XML_IN_NODE (TX_RICH_P, PR_P_PR, XL_NS_DRAW, "pPr", GSF_XML_2ND, NULL, NULL),
                GSF_XML_IN_NODE (TX_RICH_P, TX_RICH_R, XL_NS_DRAW, "r", GSF_XML_NO_CONTENT, NULL, NULL),
                  GSF_XML_IN_NODE (TX_RICH_R, TX_RICH_R_PR, XL_NS_DRAW, "rPr", GSF_XML_NO_CONTENT, &xlsx_draw_text_run_props, NULL),
		    GSF_XML_IN_NODE (TX_RICH_R_PR, PR_P_PR_DEF_CS, XL_NS_DRAW, "cs", GSF_XML_NO_CONTENT, NULL, NULL),
		    GSF_XML_IN_NODE (TX_RICH_R_PR, PR_P_PR_DEF_EA, XL_NS_DRAW, "ea", GSF_XML_NO_CONTENT, NULL, NULL),
		    GSF_XML_IN_NODE (TX_RICH_R_PR, PR_P_PR_DEF_LATIN, XL_NS_DRAW, "latin", GSF_XML_NO_CONTENT, NULL, NULL),
		    GSF_XML_IN_NODE (TX_RICH_R_PR, FILL_SOLID, XL_NS_DRAW, "solidFill", GSF_XML_2ND, NULL, NULL),
		    GSF_XML_IN_NODE (TX_RICH_R_PR, PR_P_PR_DEF_UFILLTX, XL_NS_DRAW, "uFillTx", GSF_XML_NO_CONTENT, NULL, NULL),
		    GSF_XML_IN_NODE (TX_RICH_R_PR, PR_P_PR_DEF_ULNTX, XL_NS_DRAW, "uLnTx", GSF_XML_NO_CONTENT, NULL, NULL),
                  GSF_XML_IN_NODE (TX_RICH_R, TX_RICH_R_T, XL_NS_DRAW,  "t", GSF_XML_CONTENT, NULL, &xlsx_chart_text_content),
            GSF_XML_IN_NODE (TEXT, STR_REF, XL_NS_CHART, "strRef", GSF_XML_NO_CONTENT, NULL, NULL),
              GSF_XML_IN_NODE (STR_REF, FUNC,	XL_NS_CHART,	"f",	GSF_XML_CONTENT, NULL, &xlsx_chart_ser_f),
              GSF_XML_IN_NODE (STR_REF, STR_CACHE, XL_NS_CHART,	"strCache", GSF_XML_NO_CONTENT, NULL, NULL),
                GSF_XML_IN_NODE (STR_CACHE, STR_CACHE_COUNT, XL_NS_CHART,"ptCount", GSF_XML_NO_CONTENT, NULL, NULL),
                GSF_XML_IN_NODE (STR_CACHE, STR_PT, XL_NS_CHART,"pt", GSF_XML_NO_CONTENT, NULL, NULL),
                  GSF_XML_IN_NODE (STR_PT, STR_VAL, XL_NS_CHART,"v", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (TITLE, TEXT_PR, XL_NS_CHART, "txPr", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (CAT_AXIS, AXIS_NUMFMT, XL_NS_CHART, "numFmt", GSF_XML_NO_CONTENT, &xlsx_axis_format, NULL),
        GSF_XML_IN_NODE_FULL (CAT_AXIS, AXIS_MAJORTICKMARK, XL_NS_CHART, "majorTickMark", GSF_XML_NO_CONTENT, FALSE, TRUE, &xlsx_axis_mark, NULL, 1),
        GSF_XML_IN_NODE_FULL (CAT_AXIS, AXIS_MINORTICKMARK, XL_NS_CHART, "minorTickMark", GSF_XML_NO_CONTENT, FALSE, TRUE, &xlsx_axis_mark, NULL, 0),
        GSF_XML_IN_NODE (CAT_AXIS, AXIS_TICKLBLPOS, XL_NS_CHART, "tickLblPos", GSF_XML_NO_CONTENT, &xslx_chart_tick_label_pos, NULL),
        GSF_XML_IN_NODE (CAT_AXIS, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (CAT_AXIS, TEXT_PR, XL_NS_CHART, "txPr", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (CAT_AXIS, AXIS_CROSSAX, XL_NS_CHART, "crossAx", GSF_XML_NO_CONTENT, &xlsx_axis_crossax, NULL),
        GSF_XML_IN_NODE (CAT_AXIS, AXIS_CROSSES, XL_NS_CHART, "crosses", GSF_XML_NO_CONTENT, &xlsx_axis_crosses, NULL),
        GSF_XML_IN_NODE (CAT_AXIS, AXIS_CROSSES_AT, XL_NS_CHART, "crossesAt", GSF_XML_NO_CONTENT, &xlsx_axis_crosses_at, NULL),
        GSF_XML_IN_NODE (CAT_AXIS, AXIS_AUTO, XL_NS_CHART, "auto", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (CAT_AXIS, CAT_AXIS_LBLALGN, XL_NS_CHART, "lblAlgn", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (CAT_AXIS, AXIS_LBLOFFSET, XL_NS_CHART, "lblOffset", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (CAT_AXIS, AXIS_TICK_LBL_SKIP, XL_NS_CHART, "tickLblSkip", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (CAT_AXIS, AXIS_TICK_MARK_SKIP, XL_NS_CHART, "tickMarkSkip", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (CAT_AXIS, AXIS_NOMULTILVLLBL, XL_NS_CHART, "noMultiLvlLbl", GSF_XML_NO_CONTENT, NULL, NULL),

      GSF_XML_IN_NODE_FULL (PLOTAREA, VAL_AXIS, XL_NS_CHART, "valAx", GSF_XML_NO_CONTENT, FALSE, TRUE,
			    &xlsx_axis_start, &xlsx_axis_end, XLSX_AXIS_VAL),
	GSF_XML_IN_NODE (VAL_AXIS, AXIS_AXID, XL_NS_CHART, "axId", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (VAL_AXIS, AXIS_SCALING, XL_NS_CHART, "scaling", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (VAL_AXIS, AXIS_DELETE, XL_NS_CHART, "delete", GSF_XML_2ND, NULL, NULL),
	  GSF_XML_IN_NODE (VAL_AXIS, AXIS_DISPUNITS, XL_NS_CHART, "dispUnits", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (AXIS_DISPUNITS, AXIS_CUSTUNIT, XL_NS_CHART, "custUnit", GSF_XML_NO_CONTENT, &xlsx_axis_custom_unit, NULL),
	  GSF_XML_IN_NODE (AXIS_DISPUNITS, AXIS_DISPUNITSLBL, XL_NS_CHART, "dispUnitsLbl", GSF_XML_NO_CONTENT, NULL, NULL),
#if 0
             GSF_XML_IN_NODE (AXIS_DISPUNITSLBL, LAYOUT, XL_NS_CHART, "layout", GSF_XML_2ND, NULL, NULL),
#endif
             GSF_XML_IN_NODE (AXIS_DISPUNITSLBL, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_2ND, NULL, NULL),
             GSF_XML_IN_NODE (AXIS_DISPUNITSLBL, TEXT_PR, XL_NS_CHART, "txPr", GSF_XML_2ND, NULL, NULL),
	  GSF_XML_IN_NODE (AXIS_DISPUNITS, AXIS_BUILTINUNIT, XL_NS_CHART, "builtInUnit", GSF_XML_NO_CONTENT, &xlsx_axis_builtin_unit, NULL),
	GSF_XML_IN_NODE (VAL_AXIS, AXIS_POS, XL_NS_CHART, "axPos", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (VAL_AXIS, AXIS_MAJORGRID, XL_NS_CHART, "majorGridlines", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (VAL_AXIS, AXIS_MINORGRID, XL_NS_CHART, "minorGridlines", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (VAL_AXIS, TITLE, XL_NS_CHART, "title", GSF_XML_2ND, NULL, NULL),
	GSF_XML_IN_NODE (VAL_AXIS, AXIS_NUMFMT, XL_NS_CHART, "numFmt", GSF_XML_2ND, NULL, NULL),
	GSF_XML_IN_NODE (VAL_AXIS, AXIS_MAJORTICKMARK, XL_NS_CHART, "majorTickMark", GSF_XML_2ND, NULL, NULL),
	GSF_XML_IN_NODE (VAL_AXIS, AXIS_MINORTICKMARK, XL_NS_CHART, "minorTickMark", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (VAL_AXIS, AXIS_TICKLBLPOS, XL_NS_CHART, "tickLblPos", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (VAL_AXIS, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (VAL_AXIS, TEXT_PR, XL_NS_CHART, "txPr", GSF_XML_2ND, NULL, NULL),
	GSF_XML_IN_NODE (VAL_AXIS, AXIS_CROSSAX, XL_NS_CHART, "crossAx", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (VAL_AXIS, AXIS_CROSSES, XL_NS_CHART, "crosses", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (VAL_AXIS, AXIS_CROSSES_AT, XL_NS_CHART, "crossesAt", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (VAL_AXIS, VAL_AXIS_CROSSBETWEEN, XL_NS_CHART, "crossBetween", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE_FULL (VAL_AXIS, AXIS_MAJORTICK_UNIT, XL_NS_CHART, "majorUnit", GSF_XML_NO_CONTENT, FALSE, TRUE,
			      &xlsx_axis_bound, NULL, GOG_AXIS_ELEM_MAJOR_TICK),
        GSF_XML_IN_NODE_FULL (VAL_AXIS, AXIS_MINORTICK_UNIT, XL_NS_CHART, "minorUnit", GSF_XML_NO_CONTENT, FALSE, TRUE,
			      &xlsx_axis_bound, NULL, GOG_AXIS_ELEM_MINOR_TICK),

      GSF_XML_IN_NODE_FULL (PLOTAREA, DATE_AXIS, XL_NS_CHART, "dateAx", GSF_XML_NO_CONTENT, FALSE, TRUE,
                            &xlsx_axis_start, &xlsx_axis_end, XLSX_AXIS_DATE),
        GSF_XML_IN_NODE (DATE_AXIS, AXIS_AXID, XL_NS_CHART, "axId", GSF_XML_2ND, NULL, NULL),
	GSF_XML_IN_NODE (DATE_AXIS, AXIS_SCALING, XL_NS_CHART, "scaling", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (DATE_AXIS, AXIS_DELETE, XL_NS_CHART, "delete", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (DATE_AXIS, AXIS_POS, XL_NS_CHART, "axPos", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (DATE_AXIS, AXIS_MAJORGRID, XL_NS_CHART, "majorGridlines", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (DATE_AXIS, AXIS_MINORGRID, XL_NS_CHART, "minorGridlines", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (DATE_AXIS, TITLE, XL_NS_CHART, "title", GSF_XML_2ND, NULL, NULL),
	GSF_XML_IN_NODE (DATE_AXIS, AXIS_NUMFMT, XL_NS_CHART, "numFmt", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (DATE_AXIS, AXIS_MAJORTICKMARK, XL_NS_CHART, "majorTickMark", GSF_XML_2ND, NULL, NULL),
	GSF_XML_IN_NODE (DATE_AXIS, AXIS_MINORTICKMARK, XL_NS_CHART, "minorTickMark", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (DATE_AXIS, AXIS_TICKLBLPOS, XL_NS_CHART, "tickLblPos", GSF_XML_2ND, NULL, NULL),
	GSF_XML_IN_NODE (DATE_AXIS, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (DATE_AXIS, TEXT_PR, XL_NS_CHART, "txPr", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (DATE_AXIS, AXIS_CROSSAX, XL_NS_CHART, "crossAx", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (DATE_AXIS, AXIS_CROSSES, XL_NS_CHART, "crosses", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (DATE_AXIS, AXIS_CROSSES_AT, XL_NS_CHART, "crossesAt", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (DATE_AXIS, AXIS_AUTO, XL_NS_CHART, "auto", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (DATE_AXIS, AXIS_LBLOFFSET, XL_NS_CHART, "lblOffset", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (DATE_AXIS, AXIS_BASETIME_UNIT, XL_NS_CHART, "baseTimeUnit", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (DATE_AXIS, AXIS_MAJORTICK_UNIT, XL_NS_CHART, "majorUnit", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (DATE_AXIS, AXIS_MAJORTIME_UNIT, XL_NS_CHART, "majorTimeUnit", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (DATE_AXIS, AXIS_MINORTICK_UNIT, XL_NS_CHART, "minorUnit", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (DATE_AXIS, AXIS_MINORTIME_UNIT, XL_NS_CHART, "minorTimeUnit", GSF_XML_NO_CONTENT, NULL, NULL),

      GSF_XML_IN_NODE_FULL (PLOTAREA, SER_AXIS, XL_NS_CHART, "serAx", GSF_XML_NO_CONTENT, FALSE, TRUE,
                            &xlsx_axis_start, &xlsx_axis_end, XLSX_AXIS_SER),
        GSF_XML_IN_NODE (SER_AXIS, AXIS_AXID, XL_NS_CHART, "axId", GSF_XML_2ND, NULL, NULL),
	GSF_XML_IN_NODE (SER_AXIS, AXIS_SCALING, XL_NS_CHART, "scaling", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (SER_AXIS, AXIS_DELETE, XL_NS_CHART, "delete", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (SER_AXIS, AXIS_POS, XL_NS_CHART, "axPos", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (SER_AXIS, AXIS_MAJORGRID, XL_NS_CHART, "majorGridlines", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (SER_AXIS, AXIS_MINORGRID, XL_NS_CHART, "minorGridlines", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (SER_AXIS, TITLE, XL_NS_CHART, "title", GSF_XML_2ND, NULL, NULL),
	GSF_XML_IN_NODE (SER_AXIS, AXIS_NUMFMT, XL_NS_CHART, "numFmt", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (SER_AXIS, AXIS_MAJORTICKMARK, XL_NS_CHART, "majorTickMark", GSF_XML_2ND, NULL, NULL),
	GSF_XML_IN_NODE (SER_AXIS, AXIS_MINORTICKMARK, XL_NS_CHART, "minorTickMark", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (SER_AXIS, AXIS_TICKLBLPOS, XL_NS_CHART, "tickLblPos", GSF_XML_2ND, NULL, NULL),
	GSF_XML_IN_NODE (SER_AXIS, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (SER_AXIS, TEXT_PR, XL_NS_CHART, "txPr", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (SER_AXIS, AXIS_CROSSAX, XL_NS_CHART, "crossAx", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (SER_AXIS, AXIS_CROSSES, XL_NS_CHART, "crosses", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (SER_AXIS, AXIS_CROSSES_AT, XL_NS_CHART, "crossesAt", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (SER_AXIS, AXIS_TICK_LBL_SKIP, XL_NS_CHART, "tickLblSkip", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (SER_AXIS, AXIS_TICK_MARK_SKIP, XL_NS_CHART, "tickMarkSkip", GSF_XML_2ND, NULL, NULL),

      GSF_XML_IN_NODE (PLOTAREA, LAYOUT, XL_NS_CHART, "layout", GSF_XML_NO_CONTENT, NULL, NULL),

      GSF_XML_IN_NODE (PLOTAREA, SCATTER, XL_NS_CHART,	"scatterChart", GSF_XML_NO_CONTENT, xlsx_chart_xy, &xlsx_plot_end),
        GSF_XML_IN_NODE (SCATTER, VARY_COLORS, XL_NS_CHART,	"varyColors", GSF_XML_NO_CONTENT, &xlsx_chart_vary_colors, NULL),
        GSF_XML_IN_NODE (SCATTER, PLOT_DLBLS, XL_NS_CHART,	"dLbls", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (PLOT_DLBLS, PLOT_DLBLS_LEGEND, XL_NS_CHART,	"showLegendKey", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (PLOT_DLBLS, PLOT_DLBLS_VAL, XL_NS_CHART,	"showVal", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (PLOT_DLBLS, PLOT_DLBLS_CAT_NAME, XL_NS_CHART,	"showCatName", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (PLOT_DLBLS, PLOT_DLBLS_SERIES_NAME, XL_NS_CHART,	"showSerName", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (PLOT_DLBLS, PLOT_DLBLS_PERCENT, XL_NS_CHART,	"showPercent", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (PLOT_DLBLS, PLOT_DLBLS_BUBBLE, XL_NS_CHART,	"showBubbleSize", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (PLOT_DLBLS, PLOT_DLBLS_SHOWLEADER, XL_NS_CHART, "showLeaderLines", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (SCATTER, SCATTER_STYLE, XL_NS_CHART,	"scatterStyle", GSF_XML_NO_CONTENT, &xlsx_scatter_style, NULL),
        GSF_XML_IN_NODE (SCATTER, PLOT_AXIS_ID, XL_NS_CHART,           "axId", GSF_XML_NO_CONTENT, &xlsx_plot_axis_id, NULL),

        GSF_XML_IN_NODE (SCATTER, SERIES, XL_NS_CHART,	"ser", GSF_XML_NO_CONTENT, &xlsx_chart_ser_start, &xlsx_chart_ser_end),
	  GSF_XML_IN_NODE (SERIES, SERIES_INVERTIFNEG, XL_NS_CHART,	"invertIfNegative", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (SERIES, SERIES_TRENDLINE, XL_NS_CHART,	"trendline", GSF_XML_NO_CONTENT, &xlsx_ser_trendline_start, &xlsx_ser_trendline_end),
            GSF_XML_IN_NODE (SERIES_TRENDLINE, SERIES_TRENDLINE_NAME, XL_NS_CHART, "name", GSF_XML_CONTENT, NULL, &xlsx_ser_trendline_name),
            GSF_XML_IN_NODE (SERIES_TRENDLINE, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_2ND, NULL, NULL),
            GSF_XML_IN_NODE (SERIES_TRENDLINE, SERIES_TRENDLINE_TYPE, XL_NS_CHART,	"trendlineType", GSF_XML_NO_CONTENT, &xlsx_ser_trendline_type, NULL),
            GSF_XML_IN_NODE (SERIES_TRENDLINE, SERIES_TRENDLINE_INTERCEPT, XL_NS_CHART,	"intercept", GSF_XML_NO_CONTENT, &xlsx_ser_trendline_intercept, NULL),
            GSF_XML_IN_NODE (SERIES_TRENDLINE, SERIES_TRENDLINE_RSQR, XL_NS_CHART,	"dispRSqr", GSF_XML_NO_CONTENT, &xlsx_ser_trendline_disprsqr, NULL),
            GSF_XML_IN_NODE (SERIES_TRENDLINE, SERIES_TRENDLINE_EQ, XL_NS_CHART,	"dispEq", GSF_XML_NO_CONTENT, &xlsx_ser_trendline_dispeq, NULL),
            GSF_XML_IN_NODE (SERIES_TRENDLINE, SERIES_TRENDLINE_LABEL, XL_NS_CHART,	"trendlineLbl", GSF_XML_NO_CONTENT, NULL, NULL),
              GSF_XML_IN_NODE (SERIES_TRENDLINE_LABEL, SERIES_TRENDLINE_LABEL_LAYOUT, XL_NS_CHART, "layout", GSF_XML_NO_CONTENT, NULL, NULL),
              GSF_XML_IN_NODE (SERIES_TRENDLINE_LABEL_LAYOUT, SERIES_TRENDLINE_LABEL_LAYOUT_MANUAL, XL_NS_CHART, "manualLayout", GSF_XML_NO_CONTENT, NULL, NULL),
                GSF_XML_IN_NODE (SERIES_TRENDLINE_LABEL_LAYOUT_MANUAL, SERIES_TRENDLINE_LABEL_LAYOUT_MANUAL_X, XL_NS_CHART, "x", GSF_XML_NO_CONTENT, NULL, NULL),
                GSF_XML_IN_NODE (SERIES_TRENDLINE_LABEL_LAYOUT_MANUAL, SERIES_TRENDLINE_LABEL_LAYOUT_MANUAL_Y, XL_NS_CHART, "y", GSF_XML_NO_CONTENT, NULL, NULL),
              GSF_XML_IN_NODE (SERIES_TRENDLINE_LABEL, SERIES_TRENDLINE_LABEL_FORMAT, XL_NS_CHART, "numFmt", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (SERIES, SERIES_IDX, XL_NS_CHART,	"idx", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (SERIES, SERIES_ORDER, XL_NS_CHART,	"order", GSF_XML_NO_CONTENT, NULL, NULL),

          GSF_XML_IN_NODE_FULL (SERIES, SERIES_CAT, XL_NS_CHART,"cat", GSF_XML_NO_CONTENT, FALSE, TRUE,
			   &xlsx_ser_type_start, &xlsx_ser_type_end, GOG_MS_DIM_CATEGORIES),
            GSF_XML_IN_NODE (SERIES_CAT, STR_REF, XL_NS_CHART,	"strRef", GSF_XML_2ND, NULL, NULL),
            GSF_XML_IN_NODE (SERIES_CAT, NUM_LIT, XL_NS_CHART,  "numLit", GSF_XML_NO_CONTENT, NULL, NULL),
              GSF_XML_IN_NODE (NUM_LIT, NUM_LIT_FMT, XL_NS_CHART,   "formatCode", GSF_XML_NO_CONTENT, NULL, NULL),
              GSF_XML_IN_NODE (NUM_LIT, NUM_LIT_COUNT, XL_NS_CHART, "ptCount", GSF_XML_NO_CONTENT, NULL, NULL),
              GSF_XML_IN_NODE (NUM_LIT, NUM_LIT_PT, XL_NS_CHART,     "pt", GSF_XML_NO_CONTENT, NULL, NULL),
                GSF_XML_IN_NODE (NUM_LIT_PT, NUM_LIT_PT_VAL, XL_NS_CHART,     "v", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (SERIES_CAT, NUM_REF, XL_NS_CHART,	"numRef", GSF_XML_NO_CONTENT, NULL, NULL),
              GSF_XML_IN_NODE (NUM_REF, FUNC, XL_NS_CHART,	"f", GSF_XML_2ND, NULL, NULL),
              GSF_XML_IN_NODE (NUM_REF, NUM_CACHE, XL_NS_CHART,	"numCache", GSF_XML_NO_CONTENT, NULL, NULL),
                GSF_XML_IN_NODE (NUM_CACHE, NUM_CACHE_FMT, XL_NS_CHART,	 "formatCode", GSF_XML_NO_CONTENT, NULL, NULL),
                GSF_XML_IN_NODE (NUM_CACHE, NUM_CACHE_COUNT, XL_NS_CHART,"ptCount", GSF_XML_NO_CONTENT, NULL, NULL),
                GSF_XML_IN_NODE (NUM_CACHE, NUM_PT, XL_NS_CHART,"pt", GSF_XML_NO_CONTENT, NULL, NULL),
                  GSF_XML_IN_NODE (NUM_PT, NUM_VAL, XL_NS_CHART,"v", GSF_XML_NO_CONTENT, NULL, NULL),

          GSF_XML_IN_NODE_FULL (SERIES, SERIES_VAL, XL_NS_CHART,	"val", GSF_XML_NO_CONTENT, FALSE, TRUE,
			   &xlsx_ser_type_start, &xlsx_ser_type_end, GOG_MS_DIM_VALUES),
            GSF_XML_IN_NODE (SERIES_VAL, NUM_REF, XL_NS_CHART,	"numRef", GSF_XML_2ND, NULL, NULL),

          GSF_XML_IN_NODE_FULL (SERIES, SERIES_X_VAL, XL_NS_CHART,	"xVal", GSF_XML_NO_CONTENT, FALSE, TRUE,
			   &xlsx_ser_type_start, &xlsx_ser_type_end, GOG_MS_DIM_CATEGORIES),
            GSF_XML_IN_NODE (SERIES_X_VAL, NUM_REF, XL_NS_CHART,	"numRef", GSF_XML_2ND, NULL, NULL),
            GSF_XML_IN_NODE (SERIES_X_VAL, STR_REF, XL_NS_CHART,	"strRef", GSF_XML_2ND, NULL, NULL),
            GSF_XML_IN_NODE (SERIES_X_VAL, NUM_LIT, XL_NS_CHART,	"numLit", GSF_XML_2ND, NULL, NULL),

          GSF_XML_IN_NODE_FULL (SERIES, SERIES_Y_VAL, XL_NS_CHART,	"yVal", GSF_XML_NO_CONTENT, FALSE, TRUE,
			   &xlsx_ser_type_start, &xlsx_ser_type_end, GOG_MS_DIM_VALUES),
            GSF_XML_IN_NODE (SERIES_Y_VAL, NUM_REF, XL_NS_CHART,	"numRef", GSF_XML_2ND, NULL, NULL),
            GSF_XML_IN_NODE (SERIES_X_VAL, STR_REF, XL_NS_CHART,	"strRef", GSF_XML_2ND, NULL, NULL),
            GSF_XML_IN_NODE (SERIES_Y_VAL, NUM_LIT, XL_NS_CHART,	"numLit", GSF_XML_2ND, NULL, NULL),

          GSF_XML_IN_NODE_FULL (SERIES, SERIES_BUBBLES, XL_NS_CHART,	"bubbleSize", GSF_XML_NO_CONTENT, FALSE, TRUE,
			   &xlsx_ser_type_start, &xlsx_ser_type_end, GOG_MS_DIM_BUBBLES),
            GSF_XML_IN_NODE (SERIES_BUBBLES, NUM_REF, XL_NS_CHART,	"numRef", GSF_XML_2ND, NULL, NULL),
            GSF_XML_IN_NODE (SERIES_BUBBLES, NUM_LIT, XL_NS_CHART,	"numLit", GSF_XML_2ND, NULL, NULL),

          GSF_XML_IN_NODE (SERIES, TEXT, XL_NS_CHART,	"tx", GSF_XML_2ND, NULL, NULL),

          GSF_XML_IN_NODE (SERIES, SERIES_BUBBLES_3D, XL_NS_CHART,	"bubble3D", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (SERIES, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_2ND, NULL, NULL),
	  GSF_XML_IN_NODE (SERIES, SERIES_SMOOTH, XL_NS_CHART, "smooth", GSF_XML_NO_CONTENT, xlsx_ser_smooth, NULL),
          GSF_XML_IN_NODE (SERIES, SERIES_D_LBLS, XL_NS_CHART,	"dLbls", GSF_XML_NO_CONTENT, &xlsx_ser_labels_start, &xlsx_ser_labels_end),
            GSF_XML_IN_NODE (SERIES_D_LBLS, SERIES_D_LBLS_POS, XL_NS_CHART,	"dLblPos", GSF_XML_NO_CONTENT, &xlsx_ser_labels_pos, NULL),
            GSF_XML_IN_NODE (SERIES_D_LBLS, SERIES_D_LBL, XL_NS_CHART,	"dLbl", GSF_XML_NO_CONTENT, &xlsx_data_label_start, &xlsx_chart_pop),
              GSF_XML_IN_NODE (SERIES_D_LBL, SERIES_D_LBL_POS, XL_NS_CHART,	"dLblPos", GSF_XML_NO_CONTENT, &xlsx_data_label_pos, NULL),
              GSF_XML_IN_NODE (SERIES_D_LBL, SERIES_D_LBL_IDX, XL_NS_CHART,	"idx", GSF_XML_NO_CONTENT, &xlsx_data_label_index, NULL),
              GSF_XML_IN_NODE (SERIES_D_LBL, SERIES_D_LBL_LAYOUT, XL_NS_CHART,	"layout", GSF_XML_NO_CONTENT, NULL, NULL),
              GSF_XML_IN_NODE (SERIES_D_LBL, SERIES_D_LBL_SHOW_VAL, XL_NS_CHART,	"showVal", GSF_XML_NO_CONTENT, &xlsx_data_label_show_val, NULL),
              GSF_XML_IN_NODE (SERIES_D_LBL, SERIES_D_LBL_SHOW_CAT, XL_NS_CHART,	"showCatName", GSF_XML_NO_CONTENT, &xlsx_data_label_show_cat, NULL),
              GSF_XML_IN_NODE (SERIES_D_LBL, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_2ND, NULL, NULL),
              GSF_XML_IN_NODE (SERIES_D_LBL, TEXT, XL_NS_CHART,	"tx", GSF_XML_NO_CONTENT, NULL, NULL),
              GSF_XML_IN_NODE (SERIES_D_LBL, TEXT_PR, XL_NS_CHART, "txPr", GSF_XML_2ND, NULL, NULL),
	  GSF_XML_IN_NODE (SERIES_D_LBLS, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_2ND, NULL, NULL),
	    GSF_XML_IN_NODE (SERIES_D_LBLS, TEXT_PR, XL_NS_CHART, "txPr", GSF_XML_2ND, NULL, NULL),
	    GSF_XML_IN_NODE (SERIES_D_LBLS, SHOW_VAL, XL_NS_CHART, "showVal", GSF_XML_NO_CONTENT, &xlsx_ser_labels_show_val, NULL),
	    GSF_XML_IN_NODE (SERIES_D_LBLS, NUM_FMT, XL_NS_CHART, "numFmt", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (SERIES_D_LBLS, SHOW_BUBBLE, XL_NS_CHART, "showBubbleSize", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (SERIES_D_LBLS, SHOW_CAT_NAME, XL_NS_CHART, "showCatName", GSF_XML_NO_CONTENT, &xlsx_ser_labels_show_cat, NULL),
	    GSF_XML_IN_NODE (SERIES_D_LBLS, SHOW_LEADERS, XL_NS_CHART, "showLeaderLines", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (SERIES_D_LBLS, SHOW_PERCENT, XL_NS_CHART, "showPercent", GSF_XML_NO_CONTENT, NULL, NULL),

          GSF_XML_IN_NODE (SERIES, SERIES_PT, XL_NS_CHART,	"dPt", GSF_XML_NO_CONTENT, &xlsx_chart_pt_start, &xlsx_chart_pt_end),
            GSF_XML_IN_NODE (SERIES_PT, BUBBLE3D, XL_NS_CHART,	"bubble3D", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (SERIES_PT, PT_IDX, XL_NS_CHART,	"idx", GSF_XML_NO_CONTENT, &xlsx_chart_pt_index, NULL),
            GSF_XML_IN_NODE (SERIES_PT, SHAPE_PR, XL_NS_CHART,	"spPr", GSF_XML_2ND, NULL, NULL),
            GSF_XML_IN_NODE (SERIES_PT, PT_SEP, XL_NS_CHART,	"explosion", GSF_XML_NO_CONTENT, &xlsx_chart_pt_sep, NULL),
            GSF_XML_IN_NODE (SERIES_PT, MARKER, XL_NS_CHART,	"marker", GSF_XML_NO_CONTENT, &xlsx_chart_marker_start, &xlsx_chart_marker_end),
              GSF_XML_IN_NODE (MARKER, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_2ND, NULL, NULL),
              GSF_XML_IN_NODE (MARKER, MARKER_SYMBOL, XL_NS_CHART, "symbol", GSF_XML_NO_CONTENT, &xlsx_chart_marker_symbol, NULL),
              GSF_XML_IN_NODE (MARKER, MARKER_SIZE, XL_NS_CHART, "size", GSF_XML_NO_CONTENT, &xlsx_chart_marker_size, NULL),
              GSF_XML_IN_NODE (MARKER, EXTLST_C, XL_NS_CHART, "extLst", GSF_XML_NO_CONTENT, NULL, NULL),
                GSF_XML_IN_NODE (EXTLST_C, EXTITEM_C, XL_NS_CHART, "ext", GSF_XML_NO_CONTENT, &xlsx_ext_begin, &xlsx_ext_end),
                  GSF_XML_IN_NODE (EXTITEM_C, EXT_GOSTYLE, XL_NS_GNM_EXT, "gostyle", GSF_XML_2ND, NULL, NULL),

          GSF_XML_IN_NODE (SERIES, SERIES_ERR_BARS, XL_NS_CHART,"errBars", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (SERIES_ERR_BARS, SERIES_ERR_BARS_ERRBARTYPE, XL_NS_CHART, "errBarType",  GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (SERIES_ERR_BARS, SERIES_ERR_BARS_ERRDIR, XL_NS_CHART, "errDir", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (SERIES_ERR_BARS, SERIES_ERR_BARS_ERRVALTYPE, XL_NS_CHART, "errValType", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (SERIES_ERR_BARS, SERIES_ERR_BARS_MINUS, XL_NS_CHART, "minus", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (SERIES_ERR_BARS_MINUS, NUM_REF, XL_NS_CHART, "numRef", GSF_XML_2ND, NULL, NULL),
              GSF_XML_IN_NODE (SERIES_ERR_BARS_MINUS, NUM_LIT, XL_NS_CHART, "numLit", GSF_XML_2ND, NULL, NULL),
	    GSF_XML_IN_NODE (SERIES_ERR_BARS, SERIES_ERR_BARS_PLUS, XL_NS_CHART, "plus", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (SERIES_ERR_BARS_PLUS, NUM_REF, XL_NS_CHART, "numRef", GSF_XML_2ND, NULL, NULL),
              GSF_XML_IN_NODE (SERIES_ERR_BARS_PLUS, NUM_LIT, XL_NS_CHART, "numLit", GSF_XML_2ND, NULL, NULL),
	    GSF_XML_IN_NODE (SERIES_ERR_BARS, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_2ND, NULL, NULL),

        GSF_XML_IN_NODE (SERIES, EXTLST_S, XL_NS_CHART, "extLst", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (EXTLST_S, EXTITEM_S, XL_NS_CHART, "ext", GSF_XML_NO_CONTENT, &xlsx_ext_begin, &xlsx_ext_end),

      GSF_XML_IN_NODE (PLOTAREA, BUBBLE, XL_NS_CHART,	"bubbleChart", GSF_XML_NO_CONTENT, &xlsx_chart_bubble, &xlsx_plot_end),
        GSF_XML_IN_NODE (BUBBLE, PLOT_AXIS_ID, XL_NS_CHART,	"axId", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (BUBBLE, SERIES, XL_NS_CHART,		"ser", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (BUBBLE, BUBBLE_SCALE, XL_NS_CHART,	"bubbleScale", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (BUBBLE, BUBBLE_NEGATIVES, XL_NS_CHART,	"showNegBubbles", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (BUBBLE, BUBBLE_SIZE_REP, XL_NS_CHART,	"sizeRepresents", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (BUBBLE, VARY_COLORS, XL_NS_CHART,	"varyColors", GSF_XML_2ND, NULL, NULL),
	GSF_XML_IN_NODE (BUBBLE, PLOT_DLBLS,    XL_NS_CHART, "dLbls", GSF_XML_2ND, NULL, NULL),

      GSF_XML_IN_NODE (PLOTAREA, SURFACE, XL_NS_CHART,	"surfaceChart", GSF_XML_NO_CONTENT, &xlsx_chart_contour, &xlsx_plot_end),
        GSF_XML_IN_NODE (SURFACE, PLOT_AXIS_ID, XL_NS_CHART,	"axId", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (SURFACE, SERIES, XL_NS_CHART,		"ser", GSF_XML_2ND, NULL, NULL),

      GSF_XML_IN_NODE (PLOTAREA, BARCOL, XL_NS_CHART,	"barChart", GSF_XML_NO_CONTENT, &xlsx_chart_bar, &xlsx_plot_end),
        GSF_XML_IN_NODE (BARCOL, VARY_COLORS, XL_NS_CHART, "varyColors", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (BARCOL, PLOT_AXIS_ID,	XL_NS_CHART, "axId", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (BARCOL, SERIES,	XL_NS_CHART, "ser", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (BARCOL, BARCOL_DIR,	XL_NS_CHART, "barDir", GSF_XML_NO_CONTENT, &xlsx_chart_bar_dir, NULL),
        GSF_XML_IN_NODE (BARCOL, BARCOL_OVERLAP, XL_NS_CHART,"overlap", GSF_XML_NO_CONTENT, &xlsx_chart_bar_overlap, NULL),
        GSF_XML_IN_NODE (BARCOL, GROUPING,	XL_NS_CHART, "grouping", GSF_XML_NO_CONTENT, &xlsx_chart_bar_group, NULL),
        GSF_XML_IN_NODE (BARCOL, GAP_WIDTH,	XL_NS_CHART, "gapWidth", GSF_XML_NO_CONTENT, &xlsx_chart_bar_gap, NULL),
	GSF_XML_IN_NODE (BARCOL, PLOT_DLBLS,    XL_NS_CHART, "dLbls", GSF_XML_2ND, NULL, NULL),

      GSF_XML_IN_NODE (PLOTAREA, LINE, XL_NS_CHART,	"lineChart", GSF_XML_NO_CONTENT, &xlsx_chart_line, &xlsx_plot_end),
        GSF_XML_IN_NODE (LINE, VARY_COLORS, XL_NS_CHART, "varyColors", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (LINE, PLOT_AXIS_ID, XL_NS_CHART,"axId", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (LINE, SERIES, XL_NS_CHART,	"ser", GSF_XML_2ND, NULL, NULL),
          GSF_XML_IN_NODE (SERIES, MARKER, XL_NS_CHART,	"marker", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (LINE, GROUPING, XL_NS_CHART,	"grouping", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (LINE, LINE_MARKER, XL_NS_CHART, "marker", GSF_XML_NO_CONTENT, &xlsx_chart_line_marker, NULL),
        GSF_XML_IN_NODE (LINE, LINE_SMOOTH, XL_NS_CHART, "smooth", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (LINE, PLOT_DLBLS, XL_NS_CHART,	"dLbls", GSF_XML_2ND, NULL, NULL),

      GSF_XML_IN_NODE (PLOTAREA, AREA, XL_NS_CHART,	"areaChart", GSF_XML_NO_CONTENT, &xlsx_chart_area, &xlsx_plot_end),
        GSF_XML_IN_NODE (AREA, PLOT_AXIS_ID, XL_NS_CHART,"axId", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (AREA, SERIES, XL_NS_CHART,	"ser", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (AREA, GROUPING, XL_NS_CHART,	"grouping", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (AREA, VARY_COLORS, XL_NS_CHART,"varyColors", GSF_XML_2ND, NULL, NULL),

      GSF_XML_IN_NODE (PLOTAREA, RADAR, XL_NS_CHART,	"radarChart", GSF_XML_NO_CONTENT, &xlsx_chart_radar, &xlsx_plot_end),
        GSF_XML_IN_NODE (RADAR, PLOT_AXIS_ID, XL_NS_CHART,  "axId", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (RADAR, SERIES, XL_NS_CHART,	  "ser", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (RADAR, RADAR_STYLE, XL_NS_CHART, "radarStyle", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (RADAR, VARY_COLORS, XL_NS_CHART, "varyColors", GSF_XML_2ND, NULL, NULL),
	GSF_XML_IN_NODE (RADAR, PLOT_DLBLS,    XL_NS_CHART, "dLbls", GSF_XML_2ND, NULL, NULL),

      GSF_XML_IN_NODE (PLOTAREA, PIE, XL_NS_CHART,	"pieChart", GSF_XML_NO_CONTENT, &xlsx_chart_pie, &xlsx_plot_end),
        GSF_XML_IN_NODE (PIE, SERIES, XL_NS_CHART,	"ser", GSF_XML_2ND, NULL, NULL),
          GSF_XML_IN_NODE (SERIES, PIE_SER_SEP, XL_NS_CHART,	"explosion", GSF_XML_NO_CONTENT, &xlsx_chart_pie_sep, NULL),
        GSF_XML_IN_NODE (PIE, VARY_COLORS, XL_NS_CHART,	"varyColors", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (PIE, PIE_FIRST_SLICE, XL_NS_CHART,	"firstSliceAng", GSF_XML_NO_CONTENT, &xlsx_chart_pie_angle, NULL),
	GSF_XML_IN_NODE (PIE, PLOT_DLBLS,    XL_NS_CHART, "dLbls", GSF_XML_2ND, NULL, NULL),

      GSF_XML_IN_NODE (PLOTAREA, OF_PIE, XL_NS_CHART,	"ofPieChart", GSF_XML_NO_CONTENT, &xlsx_chart_pie, &xlsx_plot_end),
        GSF_XML_IN_NODE (OF_PIE, OF_PIE_TYPE,	XL_NS_CHART, "ofPieType", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (OF_PIE, SERIES,	XL_NS_CHART, "ser", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (OF_PIE, SERIES_LINES,	XL_NS_CHART, "serLines", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (SERIES_LINES, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (OF_PIE, PIE_GAP_WIDTH,	XL_NS_CHART, "gapWidth", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (OF_PIE, VARY_COLORS,	XL_NS_CHART, "varyColors", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (OF_PIE, OF_2ND_PIE,	XL_NS_CHART, "secondPieSize", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (OF_PIE, PLOT_DLBLS,    XL_NS_CHART, "dLbls", GSF_XML_2ND, NULL, NULL),

      GSF_XML_IN_NODE (PLOTAREA, DOUGHNUT, XL_NS_CHART,	"doughnutChart", GSF_XML_NO_CONTENT, &xlsx_chart_ring, &xlsx_plot_end),
        GSF_XML_IN_NODE (DOUGHNUT, SERIES, XL_NS_CHART,	"ser", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (DOUGHNUT, VARY_COLORS, XL_NS_CHART,	"varyColors", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (DOUGHNUT, PIE_FIRST_SLICE, XL_NS_CHART,	"firstSliceAng", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (DOUGHNUT, HOLE_SIZE, XL_NS_CHART,		"holeSize", GSF_XML_NO_CONTENT, &xlsx_chart_ring_hole, NULL),
	GSF_XML_IN_NODE (DOUGHNUT, PLOT_DLBLS,    XL_NS_CHART, "dLbls", GSF_XML_2ND, NULL, NULL),

      GSF_XML_IN_NODE (PLOTAREA, DATA_TABLE, XL_NS_CHART, "dTable", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (DATA_TABLE, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (DATA_TABLE, TEXT_PR, XL_NS_CHART,  "txPr", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (DATA_TABLE, DT_SHOW_H_BORDER, XL_NS_CHART, "showHorzBorder", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (DATA_TABLE, DT_SHOW_V_BORDER, XL_NS_CHART, "showVertBorder", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (DATA_TABLE, DT_SHOW_KEYS, XL_NS_CHART, "showKeys", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (DATA_TABLE, DT_SHOW_OUTLINE, XL_NS_CHART, "showOutline", GSF_XML_NO_CONTENT, NULL, NULL),

    GSF_XML_IN_NODE (CHART, TITLE, XL_NS_CHART, "title", GSF_XML_2ND, NULL, NULL),

    GSF_XML_IN_NODE (CHART, LEGEND, XL_NS_CHART, "legend", GSF_XML_NO_CONTENT, &xlsx_chart_legend, &xlsx_chart_pop),
      GSF_XML_IN_NODE (LEGEND, LEGEND_POS, XL_NS_CHART, "legendPos", GSF_XML_NO_CONTENT, &xlsx_chart_legend_pos, NULL),
      GSF_XML_IN_NODE (LEGEND, LEGEND_ENTRY, XL_NS_CHART, "legendEntry", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (LEGEND_ENTRY, LEGEND_ENTRYIDX, XL_NS_CHART, "idx", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (LEGEND_ENTRY, LEGEND_ENTRYDELETE, XL_NS_CHART, "delete", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (LEGEND_ENTRY, TEXT_PR, XL_NS_CHART, "txPr", GSF_XML_2ND, NULL, NULL),
      GSF_XML_IN_NODE (LEGEND, LAYOUT, XL_NS_CHART, "layout", GSF_XML_2ND, NULL, NULL),
      GSF_XML_IN_NODE (LEGEND, OVERLAY, XL_NS_CHART, "overlay", GSF_XML_2ND, NULL, NULL),
      GSF_XML_IN_NODE (LEGEND, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_2ND, NULL, NULL),
      GSF_XML_IN_NODE (LEGEND, TEXT_PR, XL_NS_CHART, "txPr", GSF_XML_2ND, NULL, NULL),
    GSF_XML_IN_NODE (CHART, CHART_HIDDEN, XL_NS_CHART, "plotVisOnly", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (CHART, CHART_BLANKS, XL_NS_CHART, "dispBlanksAs", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (CHART, AUTO_TITLE_DEL, XL_NS_CHART, "autoTitleDeleted", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (CHART_SPACE, STYLE, XL_NS_CHART, "style", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (CHART_SPACE, PRINT_SETTINGS, XL_NS_CHART, "printSettings", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (PRINT_SETTINGS, PAGE_SETUP, XL_NS_CHART, "pageSetup", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (PRINT_SETTINGS, PAGE_MARGINS, XL_NS_CHART, "pageMargins", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (PRINT_SETTINGS, HEADER_FOOTER, XL_NS_CHART, "headerFooter", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (HEADER_FOOTER, ODD_HEADER, XL_NS_CHART, "oddHeader", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (HEADER_FOOTER, ODD_FOOTER, XL_NS_CHART, "oddFooter", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (CHART_SPACE, LANG, XL_NS_CHART, "lang", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (CHART_SPACE, USER_SHAPE, XL_NS_CHART, "userShapes", GSF_XML_NO_CONTENT, &xlsx_chart_user_shapes, NULL),
GSF_XML_IN_NODE_END
};

/***********************************************************************/

static int
cb_by_id (gconstpointer a, gconstpointer b)
{
	return gog_object_get_id (a) - gog_object_get_id (b);
}

static GSList *
xlsx_get_axes (GogObject *chart)
{
	GSList *children, *waste = NULL, *axes = NULL;

	children = gog_object_get_children (chart, NULL);

	while (children) {
		GSList *next = children->next;
		GogObject *obj = children->data;

		if (GOG_IS_AXIS (obj)) {
			children->next = axes;
			axes = children;
		} else {
			children->next = waste;
			waste = children;
		}

		children = next;
	}

	g_slist_free (waste);
	return g_slist_sort (axes, cb_by_id);
}

static void
cb_axis_set_position (GObject *axis, XLSXAxisInfo *info, XLSXReadState *state)
{
	XLSXAxisInfo *cross_info = info->cross_id
		? g_hash_table_lookup (state->axis.by_id, info->cross_id)
		: NULL;
	GogAxisPosition pos = info->cross;

	if (cross_info && cross_info->invert_axis) {
		switch (pos) {
		case GOG_AXIS_AT_LOW: pos = GOG_AXIS_AT_HIGH; break;
		case GOG_AXIS_AT_HIGH: pos = GOG_AXIS_AT_LOW; break;
		default: break;
		}
	}

	g_object_set (axis, "pos", pos, NULL);
}

/*
 * Rename axes so we get numbers 1, 2, 3, etc.  This is partly cosmetic
 * and partly for roundtrips.  Note, that different axis types have their
 * own numbering.
 */
static void
xlsx_axes_rename (XLSXReadState *state)
{
	GogObject *chart = GOG_OBJECT (state->chart);
	GSList *axes, *l, *l2;
	GHashTable *role_to_id = g_hash_table_new (NULL, NULL);

	axes = xlsx_get_axes (chart);

	for (l = axes; l; l = l->next) {
		GogObject *axis = l->data;
		GogObjectRole const *role = axis->role;
		guint old_id = axis->id;
		guint new_id = 1 + GPOINTER_TO_UINT (g_hash_table_lookup (role_to_id, (gpointer)role));
		GogAxisType cross_atype;

		if (new_id == old_id)
			continue;
#ifdef DEBUG_AXIS
		g_printerr ("Changing id of %s to %u\n",
			    gog_object_get_name (axis), new_id);
#endif
		g_object_set (axis, "id", new_id, NULL);
		g_hash_table_replace (role_to_id, (gpointer)role, GUINT_TO_POINTER (new_id));

		/*
		 * We shouldn't have to fixup cross-axis-id of other axes, but
		 * looking for the old id won't hurt either.
		 */
		cross_atype = gog_axis_base_get_crossed_axis_type (GOG_AXIS_BASE (axis));
		for (l2 = axes; l2; l2 = l2->next) {
			GogObject *axis2 = l2->data;
			guint cross_id;

			if (gog_axis_get_atype (GOG_AXIS (axis2)) != cross_atype)
				continue;

			g_object_get (axis2, "cross-axis-id", &cross_id, NULL);
			if (cross_id == old_id)
				g_object_set (axis2, "cross-axis-id", new_id, NULL);
		}
	}

	g_slist_free (axes);
	g_hash_table_destroy (role_to_id);
}

static void
xlsx_axes_redirect_deleted (XLSXReadState *state)
{
	GogObject *chart = GOG_OBJECT (state->chart);
	GSList *l, *axes = xlsx_get_axes (chart);

	for (l = axes; l; l = l->next) {
		GogAxis *axis = l->data;
		XLSXAxisInfo *info;
		GogAxisType atype;
		GSList *l2, *plots, *children;
		GogAxis *visible = NULL;

		info = g_hash_table_lookup (state->axis.by_obj, axis);
		if (!info || !info->deleted)
			continue;

		/* Find a visible alternative for the deleted axis.  */
		atype = gog_axis_get_atype (GOG_AXIS (axis));
		for (l2 = axes; l2; l2 = l2->next) {
			GogAxis *axis2 = GOG_AXIS (l2->data);
			if (gog_axis_get_atype (GOG_AXIS (axis2)) == atype &&
			    !gnm_object_get_bool (axis2, "invisible")) {
				visible = axis2;
				break;
			}
		}
		if (!visible)
			continue;

		/* Make all plots use the new, visible axis.  */
		plots = g_slist_copy ((GSList *) gog_axis_contributors (GOG_AXIS (axis)));
		for (l2 = plots; l2; l2 = l2->next) {
			GogPlot *plot = l2->data;
			if (GOG_IS_PLOT (plot))
				gog_plot_set_axis (plot, visible);
		}
		g_slist_free (plots);

		/* Reparent the children of the deleted axis */
		children = gog_object_get_children (GOG_OBJECT (axis), NULL);
		for (l2 = children; l2; l2 = l2->next) {
			GogObject *obj = l2->data;
			GogObjectRole const *role = obj->role;
			gog_object_clear_parent (obj);
			gog_object_set_parent (obj, GOG_OBJECT (visible), role, obj->id);
		}
		g_slist_free (children);
	}

	g_slist_free (axes);
}

static void
xlsx_axes_remove_deleted (XLSXReadState *state)
{
	GHashTableIter hiter;
	gpointer key, val;

	g_hash_table_iter_init (&hiter, state->axis.by_obj);
	while (g_hash_table_iter_next (&hiter, &key, &val)) {
		GogAxis *axis = key;
		XLSXAxisInfo *info = val;
		if (info->deleted) {
			gboolean may_delete =
				gog_object_is_deletable (GOG_OBJECT (axis));
#ifdef DEBUG_AXIS
			g_printerr ("Would like to delete file axis %p (%s): %s\n",
				    axis,
				    gog_object_get_name (GOG_OBJECT (axis)),
				    may_delete ? "allowed" : "not allowed");
#endif
			if (may_delete) {
				gog_object_clear_parent	(GOG_OBJECT (axis));
				g_object_unref (axis);
			}
		}
	}
}

static void
xlsx_axis_cleanup (XLSXReadState *state)
{
	GSList *axes, *l;

	/* clean out axes that were auto created */
	axes = xlsx_get_axes (GOG_OBJECT (state->chart));
	for (l = axes; l; l = l->next) {
		GogAxis *axis = l->data;
		XLSXAxisInfo *info;
		gboolean may_delete;

		info = g_hash_table_lookup (state->axis.by_obj, axis);
		if (info)
			continue;

		may_delete = gog_object_is_deletable (GOG_OBJECT (axis));
#ifdef DEBUG_AXIS
		g_printerr ("Would like to delete auto axis %p (%s): %s\n",
			    axis, gog_object_get_name (GOG_OBJECT (axis)),
			    may_delete ? "allowed" : "not allowed");
#endif
		if (may_delete) {
			gog_object_clear_parent	(GOG_OBJECT (axis));
			g_object_unref (axis);
		}
	}
	g_slist_free (axes);

	xlsx_axes_redirect_deleted (state);

	g_hash_table_foreach (state->axis.by_obj,
		(GHFunc)cb_axis_set_position, state);

	xlsx_axes_remove_deleted (state);

	g_hash_table_destroy (state->axis.by_obj);
	g_hash_table_destroy (state->axis.by_id);
	state->axis.by_obj = state->axis.by_id = NULL;

	xlsx_axes_rename (state);
}

static void
xlsx_read_chart (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	xmlChar const *part_id = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], XL_NS_DOC_REL, "id"))
			part_id = attrs[1];
	if (NULL != part_id) {
		/* leave it in 'state' for the frame to insert */
		state->so = sheet_object_graph_new (NULL);

		state->graph	 = sheet_object_graph_get_gog (state->so);
		state->cur_obj   = gog_object_add_by_name (GOG_OBJECT (state->graph), "Chart", NULL);
		state->chart	 = GOG_CHART (state->cur_obj);
		state->cur_style = go_style_dup (go_styled_object_get_style (GO_STYLED_OBJECT (state->chart)));
		state->obj_stack = NULL;
		state->style_stack = NULL;
		state->dim_type  = GOG_MS_DIM_LABELS;
		state->axis.by_id  = g_hash_table_new_full (g_str_hash, g_str_equal,
			NULL, (GDestroyNotify) xlsx_axis_info_free);
		state->axis.by_obj = g_hash_table_new (g_direct_hash, g_direct_equal);
		xlsx_reset_chart_pos (state);

		xlsx_parse_rel_by_id (xin, part_id, xlsx_chart_dtd, xlsx_ns);

		if (NULL != state->obj_stack) {
			g_warning ("left-over content on chart object stack");
			g_slist_free (state->obj_stack);
			state->obj_stack = NULL;
		}

		xlsx_axis_cleanup (state);
		g_object_set (state->chart, "style", state->cur_style, NULL);
		g_object_unref (state->cur_style);
		state->cur_style = NULL;
		if (NULL != state->style_stack) {
			GSList *l;
			g_warning ("left-over style");
			for (l = state->style_stack; l; l = l->next) {
				GOStyle *style = l->data;
				if (style)
					g_object_unref (style);
			}
			g_slist_free (state->style_stack);
			state->style_stack = NULL;
		}
		if (state->chart) {
			GogObject *title = gog_object_get_child_by_name (GOG_OBJECT (state->chart), "Title");
			if (title) {
				/* test if the title s empty */
				GOData *dat = gog_dataset_get_dim (GOG_DATASET (title), 0);
				GError *err = NULL;
				char *str = dat != NULL? go_data_get_scalar_string (dat): NULL;
				/* if no title, use the first series label */
				if (!str || !*str) {
					GSList *plots = gog_chart_get_plots (state->chart);
					if (plots != NULL && plots->data != NULL) {
						GogPlot *plot = GOG_PLOT (plots->data);
						GSList const *series = plot ? gog_plot_get_series  (plot) : NULL;
						GogDataset *ds = series ? GOG_DATASET (series->data) : NULL;
						if (ds)
							dat = gog_dataset_get_dim (ds, -1);
						if (dat)
							gog_dataset_set_dim (GOG_DATASET (title), 0,
									     GO_DATA (g_object_ref (dat)), &err);
						if (err)
							g_error_free (err);
					}
				}
				g_free (str);
			}
		}
		xlsx_reset_chart_pos (state);
		state->cur_obj   = NULL;
		state->chart = NULL;
		state->graph = NULL;
	}
}



/**************************************************************************/
#define CELL	0
#define OFFSET	1
#define FROM	0
#define TO	4
#define COL	0
#define ROW	2

static void
xlsx_draw_anchor_start (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;

	g_return_if_fail (state->so == NULL);

	memset ((gpointer)state->drawing_pos, 0, sizeof (state->drawing_pos));
	state->drawing_pos_flags = 0;
	state->so_direction = GOD_ANCHOR_DIR_DOWN_RIGHT;
	state->so_anchor_mode = GNM_SO_ANCHOR_TWO_CELLS;
}

static void
xlsx_drawing_twoCellAnchor_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;

	if (NULL == state->so) {
		xlsx_warning (xin,
			_("Dropping missing object"));
	} else if ((state->drawing_pos_flags & 0xFF) != 0xFF) {
		xlsx_warning (xin,
			      _("Dropping object with incomplete anchor %2x"), state->drawing_pos_flags);
		g_object_unref (state->so);
	} else {
		SheetObjectAnchor anchor;
		GnmRange r;
		double coords[4];
		double size;
		int i, max;

		range_init (&r,
			    state->drawing_pos[COL | FROM],
			    state->drawing_pos[ROW | FROM],
			    state->drawing_pos[COL | TO],
			    state->drawing_pos[ROW | TO]);

		switch (state->so_anchor_mode) {
		default:
		case GNM_SO_ANCHOR_TWO_CELLS:
			max = 8;
			break;
		case GNM_SO_ANCHOR_ONE_CELL:
			max = 4;
			break;
		case GNM_SO_ANCHOR_ABSOLUTE:
			max = 0;
			break;
		}
		for (i = 0; i < 8; i+=2) {
			ColRowInfo const *cri;
			if (i < max) {
				if (i & 2) {
					cri = sheet_row_get (state->sheet, state->drawing_pos[i]);
					size = cri? cri->size_pts: sheet_row_get_default_size_pts (state->sheet);
				} else {
					cri = sheet_col_get (state->sheet, state->drawing_pos[i]);
					/* FIXME: scaling horizontally just like in xlsx_CT_Col */
					size = (cri? cri->size_pts: sheet_col_get_default_size_pts (state->sheet)) * 1.16191275167785;
				}
				coords[i / 2] = (double) state->drawing_pos[i + 1] / 12700. / size;
			} else
				coords[i / 2] = (double) state->drawing_pos[i + 1] / 12700.;
		}
		sheet_object_anchor_init (&anchor, &r, coords, state->so_direction, state->so_anchor_mode);
		sheet_object_set_anchor (state->so, &anchor);
		if (state->cur_style &&
		    g_object_class_find_property (G_OBJECT_GET_CLASS (state->so), "style"))
			g_object_set (state->so, "style", state->cur_style, NULL);

		state->pending_objects = g_slist_prepend (state->pending_objects, state->so);

		// "xlsx" cannot leave out the name.  Treat an
		// empty name as a missing name
		sheet_object_set_name (state->so,
				       state->object_name && *state->object_name
				       ? state->object_name
				       : NULL);
	}

	if (state->cur_style) {
		g_object_unref (state->cur_style);
		state->cur_style = NULL;
	}
	g_free (state->object_name);
	state->object_name = NULL;
	state->so = NULL;
}


static void
xlsx_drawing_oneCellAnchor_end (GsfXMLIn *xin, GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;

	state->drawing_pos[COL | TO] = state->drawing_pos[COL | FROM];
	state->drawing_pos[ROW | TO] = state->drawing_pos[ROW | FROM];
	state->drawing_pos_flags |= ((1 << (COL | TO)) | (1 << (ROW | TO)));
	state->so_anchor_mode = GNM_SO_ANCHOR_ONE_CELL;
	xlsx_drawing_twoCellAnchor_end (xin, blob);
}

static void
xlsx_drawing_absoluteAnchor_end (GsfXMLIn *xin, GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;

	state->drawing_pos_flags |= ((1 << (COL | TO)) | (1 << (ROW | TO)) |
				(1 << (COL | FROM)) | (1 << (ROW | FROM)));
	state->so_anchor_mode = GNM_SO_ANCHOR_ABSOLUTE;
	xlsx_drawing_twoCellAnchor_end (xin, blob);
}

static void
xlsx_drawing_anchor_pos (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_int64 (xin, attrs, "x", state->drawing_pos + (COL | FROM | OFFSET)))
			state->drawing_pos_flags |= (1 << (COL | FROM | OFFSET));
		else if (attr_int64 (xin, attrs, "y", state->drawing_pos + (ROW | FROM | OFFSET)))
			state->drawing_pos_flags |= (1 << (ROW | FROM | OFFSET));
}

static void
xlsx_drawing_ext (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_int64 (xin, attrs, "cx", state->drawing_pos + (COL | TO | OFFSET)))
			state->drawing_pos_flags |= (1 << (COL | TO | OFFSET));
		else if (attr_int64 (xin, attrs, "cy", state->drawing_pos + (ROW | TO | OFFSET)))
			state->drawing_pos_flags |= (1 << (ROW | TO | OFFSET));
}

static void
xlsx_drawing_pos (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	gint64 val;
	char  *end;

	errno = 0;
	val = g_ascii_strtoll (xin->content->str, &end, 10);
	if (errno == ERANGE || end == xin->content->str || *end != '\0')
		return;

	state->drawing_pos[xin->node->user_data.v_int] = val;
	state->drawing_pos_flags |= 1 << xin->node->user_data.v_int;
#if 0
	g_printerr ("%s %s %s = %" G_GINT64_FORMAT "\n",
		 (xin->node->user_data.v_int & TO) ? "To" : "From",
		 (xin->node->user_data.v_int & ROW) ? "Row" : "Col",
		 (xin->node->user_data.v_int & OFFSET) ? "Offset" : "",
		 val);
#endif
}

static void
xlsx_drawing_preset_geom (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;

	static EnumVal const types[] = {
		{ "rect", 0 },
		{ "ellipse", 1 },
		{ "line", 2 },
		{ NULL, 0 }
	};
	int typ = -1;

	if (NULL != state->so) /* FIXME FIXME FIXME: how does this happen? */
		return;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_enum (xin, attrs, "prst", types, &typ))
			; /* Nothing */
	}

	switch (typ) {
	case 0:
		state->so = g_object_new (GNM_SO_FILLED_TYPE, "is_oval", FALSE, NULL);
		break;
	case 1:
		state->so = g_object_new (GNM_SO_FILLED_TYPE, "is_oval", TRUE, NULL);
		break;
	case 2:
		state->so = g_object_new (GNM_SO_LINE_TYPE, NULL);
		break;
	default:
		break;
	}

	if (state->so) {
		GOStyle *style = NULL;
		if (g_object_class_find_property (G_OBJECT_GET_CLASS (state->so), "style"))
			g_object_get (state->so, "style", &style, NULL);
		if (style) {
			state->cur_style = go_style_dup (style);
			g_object_unref (style);
		}
	}
}

static void
xlsx_drawing_picture (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	state->so = g_object_new (GNM_SO_IMAGE_TYPE, NULL);
}

static void
xlsx_blip_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	g_return_if_fail (GNM_IS_SO_IMAGE (state->so));
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], XL_NS_DOC_REL, "embed")) {
			GsfOpenPkgRel const *rel = gsf_open_pkg_lookup_rel_by_id (
				gsf_xml_in_get_input (xin), attrs[1]);
			GsfInput *input = gsf_open_pkg_open_rel (
			        gsf_xml_in_get_input (xin), rel, NULL);
			size_t size;
			gconstpointer data;

			g_return_if_fail (input != NULL);
			size = gsf_input_size (input);
			data = gsf_input_read (input, size, NULL);
			sheet_object_image_set_image (GNM_SO_IMAGE (state->so),
						      NULL, data, size);
			g_object_unref (input);
	}

}

static GsfXMLInNode const xlsx_drawing_dtd[] = {
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE_FULL (START, DRAWING, XL_NS_SS_DRAW, "wsDr", GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),

  GSF_XML_IN_NODE (DRAWING, TWO_CELL, XL_NS_SS_DRAW, "twoCellAnchor", GSF_XML_NO_CONTENT,
		   &xlsx_draw_anchor_start, &xlsx_drawing_twoCellAnchor_end),
    GSF_XML_IN_NODE (TWO_CELL, ANCHOR_FROM, XL_NS_SS_DRAW, "from", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE_FULL (ANCHOR_FROM, ANCHOR_FROM_COL,	XL_NS_SS_DRAW, "col",	 GSF_XML_CONTENT, FALSE, TRUE, NULL, &xlsx_drawing_pos, FROM | COL | CELL),
      GSF_XML_IN_NODE_FULL (ANCHOR_FROM, ANCHOR_FROM_COL_OFF,	XL_NS_SS_DRAW, "colOff", GSF_XML_CONTENT, FALSE, TRUE, NULL, &xlsx_drawing_pos, FROM | COL | OFFSET),
      GSF_XML_IN_NODE_FULL (ANCHOR_FROM, ANCHOR_FROM_ROW,	XL_NS_SS_DRAW, "row",    GSF_XML_CONTENT, FALSE, TRUE, NULL, &xlsx_drawing_pos, FROM | ROW | CELL),
      GSF_XML_IN_NODE_FULL (ANCHOR_FROM, ANCHOR_FROM_ROW_OFF,	XL_NS_SS_DRAW, "rowOff", GSF_XML_CONTENT, FALSE, TRUE, NULL, &xlsx_drawing_pos, FROM | ROW | OFFSET),
    GSF_XML_IN_NODE (TWO_CELL, TWO_CELL_TO, XL_NS_SS_DRAW, "to", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE_FULL (TWO_CELL_TO, TWO_CELL_TO_COL,	XL_NS_SS_DRAW, "col",    GSF_XML_CONTENT, FALSE, TRUE, NULL, &xlsx_drawing_pos, TO | COL | CELL),
      GSF_XML_IN_NODE_FULL (TWO_CELL_TO, TWO_CELL_TO_COL_OFF,	XL_NS_SS_DRAW, "colOff", GSF_XML_CONTENT, FALSE, TRUE, NULL, &xlsx_drawing_pos, TO | COL | OFFSET),
      GSF_XML_IN_NODE_FULL (TWO_CELL_TO, TWO_CELL_TO_ROW,	XL_NS_SS_DRAW, "row",	 GSF_XML_CONTENT, FALSE, TRUE, NULL, &xlsx_drawing_pos, TO | ROW | CELL),
      GSF_XML_IN_NODE_FULL (TWO_CELL_TO, TWO_CELL_TO_ROW_OFF,	XL_NS_SS_DRAW, "rowOff", GSF_XML_CONTENT, FALSE, TRUE, NULL, &xlsx_drawing_pos, TO | ROW | OFFSET),
#undef FROM
#undef TO
#undef COL
#undef ROW
#undef CELL
#undef OFFSET
    GSF_XML_IN_NODE (TWO_CELL, SHAPE, XL_NS_SS_DRAW, "sp", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (SHAPE, SP_XFRM_STYLE, XL_NS_SS_DRAW, "style", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (SP_XFRM_STYLE, LN_REF, XL_NS_DRAW, "lnRef", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (LN_REF, SCHEME_CLR, XL_NS_DRAW, "schemeClr", GSF_XML_NO_CONTENT, NULL, NULL),
              COLOR_MODIFIER_NODES(SCHEME_CLR,1),
	    GSF_XML_IN_NODE (LN_REF, SCRGB_CLR, XL_NS_DRAW, "scrgbClr", GSF_XML_NO_CONTENT, xlsx_draw_color_scrgb, NULL),
              COLOR_MODIFIER_NODES(SCRGB_CLR,0),
          GSF_XML_IN_NODE (SP_XFRM_STYLE, FILL_REF, XL_NS_DRAW, "fillRef", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (FILL_REF, SCHEME_CLR, XL_NS_DRAW, "schemeClr", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (FILL_REF, SCRGB_CLR, XL_NS_DRAW, "scrgbClr", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (SP_XFRM_STYLE, EFFECT_REF, XL_NS_DRAW, "effectRef", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (EFFECT_REF, SCHEME_CLR, XL_NS_DRAW, "schemeClr", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (EFFECT_REF, SCRGB_CLR, XL_NS_DRAW, "scrgbClr", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (SP_XFRM_STYLE, FONT_REF, XL_NS_DRAW, "fontRef", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (FONT_REF, SCHEME_CLR, XL_NS_DRAW, "schemeClr", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (SHAPE, SHAPE_PR, XL_NS_SS_DRAW, "spPr", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (SHAPE_PR, SP_PR_PRST_GEOM, XL_NS_DRAW, "prstGeom", GSF_XML_NO_CONTENT, &xlsx_drawing_preset_geom, NULL),
        GSF_XML_IN_NODE (SHAPE_PR, SP_PR_XFRM, XL_NS_DRAW, "xfrm", GSF_XML_NO_CONTENT, &xlsx_sppr_xfrm, NULL),
          GSF_XML_IN_NODE (SP_PR_XFRM, SP_XFRM_STYLE, XL_NS_SS_DRAW, "style", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (SP_PR_XFRM, SP_XFRM_OFF, XL_NS_DRAW, "off", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (SP_XFRM_OFF, SP_PR_PRST_GEOM, XL_NS_DRAW, "prstGeom", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (SP_XFRM_OFF, SHAPE_PR_LN, XL_NS_DRAW, "ln", GSF_XML_NO_CONTENT, xlsx_style_line_start, &xlsx_style_line_end),
	      GSF_XML_IN_NODE (SHAPE_PR_LN, LN_NOFILL, XL_NS_DRAW, "noFill", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (SHAPE_PR_LN, LN_DASH, XL_NS_DRAW, "prstDash", GSF_XML_NO_CONTENT, &xlsx_draw_line_dash, NULL),
	      GSF_XML_IN_NODE (SHAPE_PR_LN, FILL_SOLID, XL_NS_DRAW, "solidFill", GSF_XML_NO_CONTENT, &xlsx_draw_solid_fill, &xlsx_draw_solid_fill_end),
	      GSF_XML_IN_NODE (SHAPE_PR_LN, LN_FILL_PATT,	XL_NS_DRAW, "pattFill", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (SHAPE_PR_LN, LN_MITER,	XL_NS_DRAW, "miter", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (SHAPE_PR_LN, LN_ROUND,	XL_NS_DRAW, "round", GSF_XML_NO_CONTENT, NULL, NULL),
              GSF_XML_IN_NODE_FULL (SHAPE_PR_LN, LN_HEAD,	XL_NS_DRAW, "headEnd", GSF_XML_NO_CONTENT, FALSE, TRUE, &xlsx_draw_line_headtail, NULL, FALSE),
              GSF_XML_IN_NODE_FULL (SHAPE_PR_LN, LN_TAIL,	XL_NS_DRAW, "tailEnd", GSF_XML_NO_CONTENT, FALSE, TRUE, &xlsx_draw_line_headtail, NULL, TRUE),
          GSF_XML_IN_NODE (SP_PR_XFRM, CHILD_OFF, XL_NS_DRAW, "chOff", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (SP_PR_XFRM, CHILD_EXT, XL_NS_DRAW, "chExt", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (SHAPE_PR, SP_FILL_NONE,	XL_NS_DRAW, "noFill", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (SHAPE_PR, SP_FILL_SOLID,	XL_NS_DRAW, "solidFill", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (FILL_SOLID, COLOR_THEMED, XL_NS_DRAW, "schemeClr", GSF_XML_NO_CONTENT, &xlsx_draw_color_themed, NULL),
            COLOR_MODIFIER_NODES(COLOR_THEMED,FALSE),
	  GSF_XML_IN_NODE (FILL_SOLID, COLOR_RGB,	 XL_NS_DRAW, "srgbClr", GSF_XML_NO_CONTENT, &xlsx_draw_color_rgb, NULL),
            COLOR_MODIFIER_NODES(COLOR_RGB,FALSE),

	GSF_XML_IN_NODE (SHAPE_PR, FILL_GRAD,	XL_NS_DRAW, "gradFill", GSF_XML_NO_CONTENT, xlsx_draw_grad_fill, NULL),
	  GSF_XML_IN_NODE (FILL_GRAD, GRAD_LIST,	XL_NS_DRAW, "gsLst", GSF_XML_NO_CONTENT, NULL, NULL),
	   GSF_XML_IN_NODE (GRAD_LIST, GRAD_LIST_ITEM, XL_NS_DRAW, "gs", GSF_XML_NO_CONTENT, xlsx_draw_grad_stop, xlsx_draw_grad_stop_end),
	     GSF_XML_IN_NODE (GRAD_LIST_ITEM, COLOR_RGB, XL_NS_DRAW, "srgbClr", GSF_XML_2ND, NULL, NULL),
	  GSF_XML_IN_NODE (FILL_GRAD, GRAD_LIN,	XL_NS_DRAW, "lin", GSF_XML_NO_CONTENT, &xlsx_draw_grad_linear, NULL),
          GSF_XML_IN_NODE (FILL_GRAD, GRAD_TILE, XL_NS_DRAW, "tileRect", GSF_XML_NO_CONTENT, NULL, NULL),

        GSF_XML_IN_NODE (SHAPE_PR, FILL_PATT,	XL_NS_DRAW, "pattFill", GSF_XML_NO_CONTENT, &xlsx_draw_patt_fill, NULL),
          GSF_XML_IN_NODE_FULL (FILL_PATT, FILL_PATT_BG,	XL_NS_DRAW, "bgClr", GSF_XML_NO_CONTENT, FALSE, TRUE, &xlsx_draw_patt_fill_clr, &xlsx_draw_patt_fill_clr_end, FALSE),
	    GSF_XML_IN_NODE (FILL_PATT_BG, COLOR_RGB, XL_NS_DRAW, "srgbClr", GSF_XML_2ND, NULL, NULL),
          GSF_XML_IN_NODE_FULL (FILL_PATT, FILL_PATT_FG,	XL_NS_DRAW, "fgClr", GSF_XML_NO_CONTENT, FALSE, TRUE, &xlsx_draw_patt_fill_clr, &xlsx_draw_patt_fill_clr_end, TRUE),
	    GSF_XML_IN_NODE (FILL_PATT_FG, COLOR_RGB, XL_NS_DRAW, "srgbClr", GSF_XML_2ND, NULL, NULL),

	GSF_XML_IN_NODE (SHAPE_PR, SHAPE_PR_LN, XL_NS_DRAW, "ln", GSF_XML_NO_CONTENT, NULL, NULL),

	GSF_XML_IN_NODE (SHAPE_PR, TEXT_PR, XL_NS_CHART, "txPr", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (TEXT_PR, TEXT_PR_BODY,	XL_NS_DRAW, "bodyPr", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (TEXT_PR, TEXT_PR_STYLE,	XL_NS_DRAW, "lstStyle", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (TEXT_PR, TEXT_PR_P,	XL_NS_DRAW, "p", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (TEXT_PR_P, TX_RICH_R, XL_NS_DRAW, "r", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (TX_RICH_R, TX_RICH_R_PR, XL_NS_DRAW, "rPr", GSF_XML_NO_CONTENT, NULL, NULL),
                GSF_XML_IN_NODE (TX_RICH_R_PR, EFFECT_LST, XL_NS_DRAW, "effectLst", GSF_XML_NO_CONTENT, NULL, NULL),
		GSF_XML_IN_NODE (TX_RICH_R_PR, PR_P_PR_DEF_CS, XL_NS_DRAW, "cs", GSF_XML_NO_CONTENT, NULL, NULL),
		GSF_XML_IN_NODE (TX_RICH_R_PR, PR_P_PR_DEF_EA, XL_NS_DRAW, "ea", GSF_XML_NO_CONTENT, NULL, NULL),
		GSF_XML_IN_NODE (TX_RICH_R_PR, PR_P_PR_DEF_LATIN, XL_NS_DRAW, "latin", GSF_XML_NO_CONTENT, &xlsx_rpr_latin, NULL),
		GSF_XML_IN_NODE (TX_RICH_R_PR, TEXT_FILL_SOLID, XL_NS_DRAW, "solidFill", GSF_XML_NO_CONTENT, NULL, NULL),
		  GSF_XML_IN_NODE (TEXT_FILL_SOLID, COLOR_RGB, XL_NS_DRAW, "srgbClr", GSF_XML_2ND, NULL, NULL),
	          GSF_XML_IN_NODE (TEXT_FILL_SOLID, COLOR_THEMED, XL_NS_DRAW, "schemeClr", GSF_XML_2ND, NULL, NULL),
		GSF_XML_IN_NODE (TX_RICH_R_PR, PR_P_PR_DEF_UFILLTX, XL_NS_DRAW, "uFillTx", GSF_XML_NO_CONTENT, NULL, NULL),
		GSF_XML_IN_NODE (TX_RICH_R_PR, PR_P_PR_DEF_ULNTX, XL_NS_DRAW, "uLnTx", GSF_XML_NO_CONTENT, NULL, NULL),

	      GSF_XML_IN_NODE (TX_RICH_R, TX_RICH_R_T, XL_NS_DRAW,  "t", GSF_XML_CONTENT, NULL,  &xlsx_chart_text_content),
	    GSF_XML_IN_NODE (TEXT_PR_P, PR_P_PR,	XL_NS_DRAW, "pPr", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (PR_P_PR, PR_P_PR_DEF, XL_NS_DRAW, "defRPr", GSF_XML_NO_CONTENT, NULL, NULL),
		GSF_XML_IN_NODE (PR_P_PR_DEF, PR_P_PR_DEF_CS, XL_NS_DRAW, "cs", GSF_XML_NO_CONTENT, NULL, NULL),
		GSF_XML_IN_NODE (PR_P_PR_DEF, PR_P_PR_DEF_EA, XL_NS_DRAW, "ea", GSF_XML_NO_CONTENT, NULL, NULL),
		GSF_XML_IN_NODE (PR_P_PR_DEF, PR_P_PR_DEF_LATIN, XL_NS_DRAW, "latin", GSF_XML_NO_CONTENT, NULL, NULL),
		GSF_XML_IN_NODE (PR_P_PR_DEF, FILL_SOLID, XL_NS_DRAW, "solidFill", GSF_XML_2ND, NULL, NULL),
		GSF_XML_IN_NODE (PR_P_PR_DEF, PR_P_PR_DEF_UFILLTX, XL_NS_DRAW, "uFillTx", GSF_XML_NO_CONTENT, NULL, NULL),
		GSF_XML_IN_NODE (PR_P_PR_DEF, PR_P_PR_DEF_ULNTX, XL_NS_DRAW, "uLnTx", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (TEXT_PR_P, PR_P_PR_END,XL_NS_DRAW, "endParaRPr", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (PR_P_PR_END, FILL_SOLID, XL_NS_DRAW, "solidFill", GSF_XML_2ND, NULL, NULL),
              GSF_XML_IN_NODE (PR_P_PR_END, EFFECT_LST, XL_NS_DRAW, "effectLst", GSF_XML_2ND, NULL, NULL),
	      GSF_XML_IN_NODE (PR_P_PR_END, PR_P_PR_DEF_CS, XL_NS_DRAW, "cs", GSF_XML_2ND, NULL, NULL),
	      GSF_XML_IN_NODE (PR_P_PR_END, PR_P_PR_DEF_EA, XL_NS_DRAW, "ea", GSF_XML_2ND, NULL, NULL),
	      GSF_XML_IN_NODE (PR_P_PR_END, PR_P_PR_DEF_LATIN, XL_NS_DRAW, "latin", GSF_XML_2ND, NULL, NULL),

        GSF_XML_IN_NODE (SHAPE_PR, CXN_SP, XL_NS_SS_DRAW, "cxnSp", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (CXN_SP, CXN_SP_PR, XL_NS_SS_DRAW, "nvCxnSpPr", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (CXN_SP_PR, C_NV_CXN_SP, XL_NS_SS_DRAW, "cNvCxnSpPr", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (CXN_SP_PR, C_NV_PR, XL_NS_SS_DRAW, "cNvPr", GSF_XML_NO_CONTENT, &xlsx_read_cnvpr, NULL),
              GSF_XML_IN_NODE (C_NV_PR, HLINK_CLICK, XL_NS_DRAW, "hlinkClick", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (CXN_SP, SHAPE_PR, XL_NS_SS_DRAW, "spPr", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (CXN_SP, SP_XFRM_STYLE, XL_NS_SS_DRAW, "style", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (SHAPE_PR, EXTLST, XL_NS_DRAW, "extLst", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (EXTLST, EXTITEM, XL_NS_DRAW, "ext", GSF_XML_NO_CONTENT, &xlsx_ext_begin, &xlsx_ext_end),
            GSF_XML_IN_NODE (EXTITEM, EXT_GOSTYLE, XL_NS_GNM_EXT, "gostyle", GSF_XML_NO_CONTENT, &xlsx_ext_gostyle, NULL),

      GSF_XML_IN_NODE (SHAPE, TX_BODY, XL_NS_SS_DRAW, "txBody", GSF_XML_NO_CONTENT, &xlsx_chart_text_start, &xlsx_chart_text),
        GSF_XML_IN_NODE (TX_BODY, LST_STYLE, XL_NS_DRAW, "lstStyle", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (TX_BODY, TX_BODY_PR, XL_NS_DRAW, "bodyPr", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (TX_BODY, TEXT_PR_P,	XL_NS_DRAW, "p", GSF_XML_2ND, NULL, NULL),

      GSF_XML_IN_NODE (SHAPE, NV_SP_PR, XL_NS_SS_DRAW, "nvSpPr", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (NV_SP_PR, C_NV_PR, XL_NS_SS_DRAW, "cNvPr", GSF_XML_2ND, &xlsx_read_cnvpr, NULL),
        GSF_XML_IN_NODE (NV_SP_PR, C_NV_SP_PR, XL_NS_SS_DRAW, "cNvSpPr", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (C_NV_SP_PR, SP_LOCKS, XL_NS_DRAW, "spLocks", GSF_XML_NO_CONTENT, NULL, NULL),

	GSF_XML_IN_NODE (SP_PR_XFRM, SP_XFRM_EXT, XL_NS_DRAW, "ext", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (SP_XFRM_EXT, SP_PR_PRST_GEOM, XL_NS_DRAW, "prstGeom", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (SP_PR_PRST_GEOM, AV_LST, XL_NS_DRAW, "avLst", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (SHAPE_PR, FILL_NONE,	XL_NS_DRAW, "noFill", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (SHAPE_PR, FILL_SOLID,	XL_NS_DRAW, "solidFill", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (SHAPE_PR, FILL_BLIP,	XL_NS_DRAW, "blipFill", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (SHAPE_PR, SHAPE_PR_LN, XL_NS_DRAW, "ln", GSF_XML_NO_CONTENT, NULL, NULL),


    GSF_XML_IN_NODE (TWO_CELL, GRAPHIC_FRAME, XL_NS_SS_DRAW, "graphicFrame", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (GRAPHIC_FRAME, GRAPHIC_PR, XL_NS_SS_DRAW, "nvGraphicFramePr", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (GRAPHIC_PR, CNVPR, XL_NS_SS_DRAW, "cNvPr", GSF_XML_NO_CONTENT, &xlsx_read_cnvpr, NULL),
        GSF_XML_IN_NODE (GRAPHIC_PR, GRAPHIC_PR_CHILD, XL_NS_SS_DRAW, "cNvGraphicFramePr", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (GRAPHIC_PR_CHILD, GRAPHIC_LOCKS, XL_NS_DRAW, "graphicFrameLocks", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (GRAPHIC_FRAME, GRAPHIC, XL_NS_DRAW, "graphic", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE_FULL (GRAPHIC, GRAPHIC_DATA, XL_NS_DRAW, "graphicData",
			      GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
          GSF_XML_IN_NODE (GRAPHIC_DATA, CHART, XL_NS_CHART, "chart", GSF_XML_NO_CONTENT, &xlsx_read_chart, NULL),
          GSF_XML_IN_NODE (GRAPHIC_DATA, GRAPHIC_PR_CHILD, XL_NS_SS_DRAW, "cNvGraphicFramePr", GSF_XML_2ND, NULL, NULL),
      GSF_XML_IN_NODE (GRAPHIC_FRAME, TWO_CELL_XFRM, XL_NS_SS_DRAW, "xfrm", GSF_XML_NO_CONTENT, &xlsx_sppr_xfrm, NULL),
        GSF_XML_IN_NODE (TWO_CELL_XFRM, XFRM_OFF, XL_NS_DRAW, "off", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (TWO_CELL_XFRM, XFRM_EXT, XL_NS_DRAW, "ext", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (TWO_CELL, CLIENT_DATA, XL_NS_SS_DRAW, "clientData", GSF_XML_NO_CONTENT, &xlsx_draw_clientdata, NULL),
    GSF_XML_IN_NODE (TWO_CELL, CONTENT_PART, XL_NS_SS_DRAW, "contentPart", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (TWO_CELL, CXN_SP, XL_NS_SS_DRAW, "cxnSp", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (TWO_CELL, PICTURE, XL_NS_SS_DRAW, "pic", GSF_XML_NO_CONTENT, &xlsx_drawing_picture, NULL),
      GSF_XML_IN_NODE_FULL (PICTURE, PIC_FILL_BLIP, XL_NS_SS_DRAW, "blipFill", GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
        GSF_XML_IN_NODE (PIC_FILL_BLIP, BLIP, XL_NS_DRAW, "blip", GSF_XML_NO_CONTENT, &xlsx_blip_start, NULL),
        GSF_XML_IN_NODE (BLIP, EXTLST, XL_NS_DRAW, "extLst", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (PIC_FILL_BLIP, BLIP_STRETCH, XL_NS_DRAW, "stretch", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (BLIP_STRETCH, BLIP_FILL_RECT, XL_NS_DRAW, "fillRect", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (PIC_FILL_BLIP, BLIP_SRC_RECT, XL_NS_DRAW, "srcRect", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (BLIP_SRC_RECT, BLIP_FILL_RECT, XL_NS_DRAW, "fillRect", GSF_XML_2ND, NULL, NULL),
        GSF_XML_IN_NODE (PIC_FILL_BLIP, BLIP_TILE, XL_NS_DRAW, "tile", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (PICTURE, NV_PIC_PR, XL_NS_SS_DRAW, "nvPicPr", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (NV_PIC_PR, C_NV_PIC_PR, XL_NS_SS_DRAW, "cNvPicPr", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (C_NV_PIC_PR, PIC_LOCKS, XL_NS_DRAW, "picLocks", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (NV_PIC_PR, C_NV_PR, XL_NS_SS_DRAW, "cNvPr", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (PICTURE, SHAPE_PR, XL_NS_SS_DRAW, "spPr", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (PICTURE, SP_XFRM_STYLE, XL_NS_SS_DRAW, "style", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (TWO_CELL, GROUP_SP, XL_NS_SS_DRAW, "grpSp", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (GROUP_SP, SHAPE, XL_NS_SS_DRAW, "sp", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (GROUP_SP, CXN_SP, XL_NS_SS_DRAW, "cxnSp", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (GROUP_SP, PICTURE, XL_NS_SS_DRAW, "pic", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (GROUP_SP, GROUP_SP_PR, XL_NS_SS_DRAW, "grpSpPr", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (GROUP_SP_PR, PIC_FILL_BLIP, XL_NS_SS_DRAW, "blipFill", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (GROUP_SP_PR, EFFECT_DAG, XL_NS_SS_DRAW, "effectDag", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (GROUP_SP_PR, EFFECT_LST, XL_NS_SS_DRAW, "effectLst", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (GROUP_SP_PR, EXT_LST, XL_NS_SS_DRAW, "extLst", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (GROUP_SP_PR, FILL_GRAD, XL_NS_DRAW, "gradFill", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (GROUP_SP_PR, FILL_GRP, XL_NS_DRAW, "grpFill", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (GROUP_SP_PR, FILL_NONE, XL_NS_DRAW, "noFill", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (GROUP_SP_PR, FILL_PATT, XL_NS_DRAW, "pattFill", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (GROUP_SP_PR, SCENE3D, XL_NS_DRAW, "scene3d", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (GROUP_SP_PR, FILL_SOLID, XL_NS_DRAW, "solidFill", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (GROUP_SP_PR, SP_PR_XFRM, XL_NS_DRAW, "xfrm", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (GROUP_SP, GROUP_SP, XL_NS_SS_DRAW, "grpSp", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (GROUP_SP, NV_GRP_SP_PR, XL_NS_SS_DRAW, "nvGrpSpPr", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (NV_GRP_SP_PR, C_NV_GRP_SP_PR, XL_NS_SS_DRAW, "cNvGrpSpPr", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (NV_GRP_SP_PR, C_NV_PR, XL_NS_SS_DRAW, "cNvPr", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (GROUP_SP, GRAPHIC_FRAME, XL_NS_SS_DRAW, "graphicFrame", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (DRAWING, ONE_CELL, XL_NS_SS_DRAW, "oneCellAnchor", GSF_XML_NO_CONTENT,
		   &xlsx_draw_anchor_start, &xlsx_drawing_oneCellAnchor_end),
    GSF_XML_IN_NODE (ONE_CELL, ANCHOR_FROM, XL_NS_SS_DRAW, "from", GSF_XML_2ND, NULL, NULL),
    GSF_XML_IN_NODE (ONE_CELL, ONE_CELL_EXT, XL_NS_SS_DRAW, "ext", GSF_XML_NO_CONTENT, &xlsx_drawing_ext, NULL),
    GSF_XML_IN_NODE (ONE_CELL, CLIENT_DATA, XL_NS_SS_DRAW, "clientData", GSF_XML_2ND, NULL, NULL),
    GSF_XML_IN_NODE (ONE_CELL, GRAPHIC_FRAME, XL_NS_SS_DRAW, "graphicFrame", GSF_XML_2ND, NULL, NULL),
    GSF_XML_IN_NODE (ONE_CELL, SHAPE, XL_NS_SS_DRAW, "sp", GSF_XML_2ND, NULL, NULL),
    GSF_XML_IN_NODE (ONE_CELL, CLIENT_DATA, XL_NS_SS_DRAW, "clientData", GSF_XML_2ND, NULL, NULL),
    GSF_XML_IN_NODE (ONE_CELL, CONTENT_PART, XL_NS_SS_DRAW, "contentPart", GSF_XML_2ND, NULL, NULL),
    GSF_XML_IN_NODE (ONE_CELL, CXN_SP, XL_NS_SS_DRAW, "cxnSp", GSF_XML_2ND, NULL, NULL),
    GSF_XML_IN_NODE (ONE_CELL, PICTURE, XL_NS_SS_DRAW, "pic", GSF_XML_2ND, NULL, NULL),
  GSF_XML_IN_NODE (DRAWING, ABSOLUTE, XL_NS_SS_DRAW, "absoluteAnchor", GSF_XML_NO_CONTENT,
		   &xlsx_draw_anchor_start, &xlsx_drawing_absoluteAnchor_end),
    GSF_XML_IN_NODE (ABSOLUTE, ABSOLUTE_POS, XL_NS_SS_DRAW, "pos", GSF_XML_NO_CONTENT, &xlsx_drawing_anchor_pos, NULL),
    GSF_XML_IN_NODE (ABSOLUTE, ONE_CELL_EXT, XL_NS_SS_DRAW, "ext", GSF_XML_2ND, NULL, NULL),
    GSF_XML_IN_NODE (ABSOLUTE, CLIENT_DATA, XL_NS_SS_DRAW, "clientData", GSF_XML_2ND, NULL, NULL),
    GSF_XML_IN_NODE (ABSOLUTE, GRAPHIC_FRAME, XL_NS_SS_DRAW, "graphicFrame", GSF_XML_2ND, NULL, NULL),
    GSF_XML_IN_NODE (ABSOLUTE, SHAPE, XL_NS_SS_DRAW, "sp", GSF_XML_2ND, NULL, NULL),
    GSF_XML_IN_NODE (ABSOLUTE, CLIENT_DATA, XL_NS_SS_DRAW, "clientData", GSF_XML_2ND, NULL, NULL),
    GSF_XML_IN_NODE (ABSOLUTE, CONTENT_PART, XL_NS_SS_DRAW, "contentPart", GSF_XML_2ND, NULL, NULL),
    GSF_XML_IN_NODE (ABSOLUTE, CXN_SP, XL_NS_SS_DRAW, "cxnSp", GSF_XML_2ND, NULL, NULL),
    GSF_XML_IN_NODE (ABSOLUTE, PICTURE, XL_NS_SS_DRAW, "pic", GSF_XML_2ND, NULL, NULL),
GSF_XML_IN_NODE_END
};

static void
xlsx_sheet_drawing (GsfXMLIn *xin, xmlChar const **attrs)
{
	xmlChar const *part_id = NULL;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], XL_NS_DOC_REL, "id"))
			part_id = attrs[1];
	if (NULL != part_id)
		xlsx_parse_rel_by_id (xin, part_id, xlsx_drawing_dtd, xlsx_ns);
}

/******************************************************************************
 * Legacy drawing, just need to import, but should not be exported            *
 ******************************************************************************/

/* define an approximate horizontal scale factor to align controls with cells */
#define XLSX_SHEET_HSCALE 1.165

static void
xlsx_vml_group (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	char **elts, **cur, *key, *value, *end;
	double coords[4], local_coords[4], *cur_offs;
	int i;
	for (i = 0; i < 4; i++)
		coords[i] = local_coords[i] = 0.;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (!strcmp (attrs[0], "style")) {
			elts =  g_strsplit (attrs[1], ";", 0);
			for (cur =  elts; *cur; cur++) {
				value = strchr (*cur, ':');
				if (!value)
					continue; /* Hope this does not occur */
				*value = 0;
				value++;
				key = *cur;
				while (g_ascii_isspace (*key))
					key++;
				/* *cur is now the field name and sep is the associated value */
				/* for now we get only left, top, width and height, assuming they are in pixels */
				/* FIXME: scaling just like in xlsx_CT_Col */
				if (!strcmp (key, "margin-left") || !strcmp (key, "left")) {
					double dim = g_ascii_strtod (value, &end);
					if (!strcmp (end, "pt"))
						dim *= 4./3.;
					coords[0] = (double) dim * XLSX_SHEET_HSCALE;
				} else if (!strcmp (key, "margin-top") || !strcmp (key, "top")) {
					double dim = g_ascii_strtod (value, &end);
					if (!strcmp (end, "pt"))
						dim *= 4./3.;
					coords[1] = dim;
				} else if (!strcmp (key, "width")) {
					double dim = g_ascii_strtod (value, &end);
					if (!strcmp (end, "pt"))
						dim *= 4./3.;
					coords[2] = (double) dim * XLSX_SHEET_HSCALE;
				} else if (!strcmp (key, "height")) {
					double dim = g_ascii_strtod (value, &end);
					if (!strcmp (end, "pt"))
						dim *= 4./3.;
					coords[3] = dim;
				}
			}
			g_strfreev (elts);
		} else if (!strcmp (attrs[0], "coordorigin")) {
			local_coords[0] = strtol (attrs[1], &end, 10) * XLSX_SHEET_HSCALE;
			if (*end == ',')
				local_coords[1] = strtol (end + 1, &end, 10);
		} else if (!strcmp (attrs[0], "coordsize")) {
			local_coords[2] = strtol (attrs[1], &end, 10) * XLSX_SHEET_HSCALE;
			if (*end == ',')
				local_coords[3] = strtol (end + 1, &end, 10);
		}
	/* setting the group local transform */
	cur_offs = g_new (double, 4);
	memcpy (cur_offs, state->grp_offset, sizeof (double) * 4);
	state->grp_stack = g_slist_prepend (state->grp_stack, cur_offs);
	/* evaluate new tranform */
	if (cur_offs[2] != 0.) {
		state->grp_offset[2] = coords[2] / local_coords[2] * cur_offs[2];
		state->grp_offset[0] = coords[0] - local_coords[0] + cur_offs[0];
		state->grp_offset[3] = coords[3] / local_coords[3] * cur_offs[3];
		state->grp_offset[1] = coords[1] - local_coords[1] + cur_offs[1];
	} else {
		/* Not sure if the offset should be multiplied by the scale, don't do that, all known sample have unit scales */
		state->grp_offset[2] = coords[2] / local_coords[2];
		state->grp_offset[0] = coords[0] - local_coords[0];
		state->grp_offset[3] = coords[3] / local_coords[3];
		state->grp_offset[1] = coords[1] - local_coords[1];
	}
}

static void
xlsx_vml_group_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	/* assuming that a group can't be inside another group, otherwise we need a stack */
	memcpy (state->grp_offset, state->grp_stack->data, sizeof (double) * 4);
	g_free (state->grp_stack->data);
	state->grp_stack = g_slist_delete_link (state->grp_stack, state->grp_stack);
}

static void
xlsx_vml_shape (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int z = -1;

	xlsx_reset_chart_pos (state);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (!strcmp (attrs[0], "style")) {
			char **elts = g_strsplit (attrs[1], ";", 0), **cur;
			for (cur =  elts; *cur; cur++) {
				double dim;
				char *key, *end;
				char *value = strchr (*cur, ':');
				if (!value)
					continue; /* Hope this does not occur */
				*value = 0;
				value++;
				key = *cur;
				while (g_ascii_isspace (*key))
					key++;
				/* *cur is now the field name and sep is the associated value */
				/* for now we get only left, top, width and height, assuming they are in pixels */
				/* FIXME: scaling just like in xlsx_CT_Col */
				if (!strcmp (key, "margin-left") || !strcmp (key, "left")) {
					dim = g_ascii_strtod (value, &end);
					state->chart_pos[0] = dim;
				} else if (!strcmp (key, "margin-top") || !strcmp (key, "top")) {
					dim = g_ascii_strtod (value, &end);
					state->chart_pos[1] = dim;
				} else if (!strcmp (key, "width")) {
					dim = g_ascii_strtod (value, &end);
					state->chart_pos[2] = dim;
				} else if (!strcmp (key, "height")) {
					dim = g_ascii_strtod (value, &end);
					state->chart_pos[3] = dim;
				} else if (!strcmp (key, "z-index")) {
					z = strtol (value, &end, 10);
				}
			}
			g_strfreev (elts);
			if (state->grp_offset[2] != 0.) {
				state->chart_pos[0] += state->grp_offset[0];
				state->chart_pos[1] += state->grp_offset[1];
				state->chart_pos[2] *= state->grp_offset[2];
				state->chart_pos[3] *= state->grp_offset[3];
			}
			state->chart_pos[2] += state->chart_pos[0];
			state->chart_pos[3] += state->chart_pos[1];
		}
	}

	state->zindex = z;
}

static void
xlsx_vml_drop_style (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
}

static void
xlsx_vml_client_data_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GType typ = G_TYPE_NONE;
	const char *tname = NULL;

	static EnumVal const types[] = {
		{ "Scroll", 0 },
		{ "Radio", 1 },
		{ "Spin", 2 },
		{ "Button", 3 },
		{ "Checkbox", 4 },
		{ "Note", 5 },
		{ "Dialog", 6 },
		{ "Drop", 7 },
		{ "Edit", 8 },
		{ "GBox", 9 },
		{ "Label", 10 },
		{ "LineA", 11 },
		{ "List", 12 },
		{ "Movie", 13 },
		{ "Pict", 14 },
		{ "RectA", 15 },
		{ "Shape", 16 },
		{ "Group", 17 },
		{ "Rect", 18 },
		{ NULL, 0 }
	};
	static GType gtypes[G_N_ELEMENTS(types) - 1];

	if (!gtypes[0]) {
		int i = 0;
		gtypes[i++] = GNM_SOW_SCROLLBAR_TYPE;
		gtypes[i++] = GNM_SOW_RADIO_BUTTON_TYPE;
		gtypes[i++] = GNM_SOW_SPIN_BUTTON_TYPE;
		gtypes[i++] = GNM_SOW_BUTTON_TYPE;
		gtypes[i++] = GNM_SOW_CHECKBOX_TYPE;
		gtypes[i++] = G_TYPE_NONE;
		gtypes[i++] = G_TYPE_NONE;
		gtypes[i++] = GNM_SOW_COMBO_TYPE;
		gtypes[i++] = G_TYPE_NONE;
		gtypes[i++] = G_TYPE_NONE;
		gtypes[i++] = G_TYPE_NONE;
		gtypes[i++] = G_TYPE_NONE;
		gtypes[i++] = GNM_SOW_LIST_TYPE;
		gtypes[i++] = G_TYPE_NONE;
		gtypes[i++] = G_TYPE_NONE;
		gtypes[i++] = G_TYPE_NONE;
		gtypes[i++] = G_TYPE_NONE;
		gtypes[i++] = G_TYPE_NONE;
	}

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		int tmp;
		if (attr_enum (xin, attrs, "ObjectType", types, &tmp)) {
			typ = gtypes[tmp];
			tname = attrs[1];
		}
	}

	if (state->so) {
		g_warning ("New object when one is in progress.");
	} else if (typ == G_TYPE_NONE) {
		g_printerr ("Unhandled object of type %s\n", tname);
	} else {
		state->so = GNM_SO (g_object_new (typ, NULL));
		state->so_direction = GOD_ANCHOR_DIR_DOWN_RIGHT;
		state->pending_objects = g_slist_prepend (state->pending_objects, state->so);
		if (state->zindex >= 1)
			g_hash_table_insert (state->zorder, state->so, GINT_TO_POINTER (state->zindex));
	}
}

static void
xlsx_vml_client_data_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	if (state->so) {
		SheetObjectAnchor anchor;
		GnmRange r;
		double coords[4];
		Sheet *sheet = state->sheet;
		int cols = gnm_sheet_get_max_cols (sheet);
		int rows = gnm_sheet_get_max_rows (sheet);
		int i, pos;
		double sum, size;

		for (i = 0; i < 4; i++)
			if (!go_finite (state->chart_pos[i]))
				state->chart_pos[i] = 0;

		size = sum = 0;
		for (pos = 0; pos < cols; pos++) {
			size = sheet_col_get_distance_pts (sheet, pos, pos + 1);
			if (sum + size > state->chart_pos[0])
				break;
			sum += size;
		}
		r.start.col = pos;
		coords[0] = (state->chart_pos[0] - sum) / size;
		while (sum < state->chart_pos[2] && pos < cols) {
			if (sum + size > state->chart_pos[2])
				break;
			sum += size;
			size = sheet_col_get_distance_pts (sheet, pos, pos + 1);
			pos++;
		}
		r.end.col = pos;
		coords[2] = (state->chart_pos[2] - sum) / size;

		size = sum = 0;
		for (pos = 0; pos < cols; pos++) {
			size = sheet_row_get_distance_pts (sheet, pos, pos + 1);
			if (sum + size > state->chart_pos[1])
				break;
			sum += size;
		}
		r.start.row = pos;
		coords[1] = (state->chart_pos[1] - sum) / size;
		while (sum < state->chart_pos[3] && pos < rows) {
			if (sum + size > state->chart_pos[3])
				break;
			sum += size;
			size = sheet_row_get_distance_pts (sheet, pos, pos + 1);
			pos++;
		}
		r.end.row = pos;
		coords[3] = (state->chart_pos[3] - sum) / size;
		sheet_object_anchor_init (&anchor, &r, coords, state->so_direction, state->so_anchor_mode);
		sheet_object_set_anchor (state->so, &anchor);
		if (GNM_IS_SOW_LIST (state->so) ||
		    GNM_IS_SOW_COMBO (state->so))
			sheet_widget_list_base_set_links (state->so, state->link_texpr, state->texpr);
		else if (GNM_IS_SOW_SCROLLBAR (state->so) ||
			 GNM_IS_SOW_SPINBUTTON (state->so) ||
			 GNM_IS_SOW_SLIDER (state->so))
			sheet_widget_adjustment_set_link (state->so, state->link_texpr);
		else if (GNM_IS_SOW_RADIO_BUTTON (state->so)) {
			GnmValue *v = value_new_int (state->radio_value++);
			sheet_widget_radio_button_set_link (state->so, state->link_texpr);
			sheet_widget_radio_button_set_value (state->so, v);
			value_release (v);
		}
		else if (GNM_IS_SOW_BUTTON (state->so))
			sheet_widget_button_set_link (state->so, state->link_texpr);
		else if (GNM_IS_SOW_CHECKBOX (state->so))
			sheet_widget_checkbox_set_link (state->so, state->link_texpr);

		if (state->chart_tx &&
		    g_object_class_find_property (G_OBJECT_GET_CLASS (state->so), "text") != NULL)
			g_object_set (state->so, "text", state->chart_tx, NULL);

		state->so = NULL;
	}
	if (state->texpr) {
		gnm_expr_top_unref (state->texpr);
		state->texpr = NULL;
	}
	if (state->link_texpr) {
		gnm_expr_top_unref (state->link_texpr);
		state->link_texpr = NULL;
	}

	g_free (state->chart_tx);
	state->chart_tx = NULL;
}

static void
xlsx_vml_firstbutton (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	state->radio_value = 1;
}

static void
xlsx_vml_fmla_link (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GnmParsePos pp;
	GnmValue *value;

	parse_pos_init_sheet (&pp, state->sheet);
	value = value_new_cellrange_parsepos_str (&pp, xin->content->str, 0);
	if (value)
		state->link_texpr = gnm_expr_top_new_constant (value);
}

static void
xlsx_vml_fmla_range (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GnmParsePos pp;
	GnmValue *value;

	parse_pos_init_sheet (&pp, state->sheet);
	value = value_new_cellrange_parsepos_str (&pp, xin->content->str, 0);
	if (value)
		state->texpr = gnm_expr_top_new_constant (value);
}

static void
xlsx_vml_horiz (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	if (state->so) {
		const char *s = xin->content->str;
		gboolean horiz;
		if (g_ascii_toupper (s[0]) == 'T')
			horiz = TRUE;
		else if (g_ascii_toupper (s[0]) == 'F')
			horiz = FALSE;
		else
			return; /* Blank means default.  */
		sheet_widget_adjustment_set_horizontal (state->so, horiz);
	}
}

static void
xlsx_vml_adj (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	if (GNM_IS_SOW_ADJUSTMENT (state->so)) {
		GtkAdjustment *adj = sheet_widget_adjustment_get_adjustment (state->so);
		double x = g_ascii_strtod (xin->content->str, NULL);
		switch (xin->node->user_data.v_int) {
		case 0: gtk_adjustment_set_lower (adj, x); break;
		case 1: gtk_adjustment_set_upper (adj, x); break;
		case 2: gtk_adjustment_set_step_increment (adj, x); break;
		case 3: gtk_adjustment_set_page_increment (adj, x); break;
		case 4: gtk_adjustment_set_value (adj, x); break;
		default: break;
		}
	}
}

static void
xlsx_vml_textbox_div (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	const char *text = xin->content->str;
	char *newtext = state->chart_tx
		? g_strconcat (state->chart_tx, text, NULL)
		: g_strdup (text);
	g_free (state->chart_tx);
	state->chart_tx = newtext;
}

static void
xlsx_vml_checked (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	const char *s = xin->content->str;
	gboolean checked = strtol (s, NULL, 10) > 0;

	if (GNM_IS_SOW_CHECKBOX (state->so) ||
	    GNM_IS_SOW_RADIO_BUTTON (state->so)) {
		g_object_set (state->so, "active", checked, NULL);
	}
}



static GsfXMLInNode const xlsx_legacy_drawing_dtd[] = {
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE_FULL (START, SP_LAYOUT, XL_NS_LEG_OFF, "shapelayout", GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
  GSF_XML_IN_NODE (SP_LAYOUT, IDMAP, XL_NS_LEG_OFF, "idmap", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (SP_LAYOUT, REGROUPTABLE, XL_NS_LEG_OFF, "regrouptable", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (REGROUPTABLE, ENTRY, XL_NS_LEG_OFF, "entry", GSF_XML_NO_CONTENT, NULL, NULL),
GSF_XML_IN_NODE_FULL (START, SP_TYPE, XL_NS_LEG_VML, "shapetype", GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
  GSF_XML_IN_NODE (SP_TYPE, PATH, XL_NS_LEG_VML, "path", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (SP_TYPE, STROKE, XL_NS_LEG_VML, "stroke", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (SP_TYPE, LOCK, XL_NS_LEG_OFF, "lock", GSF_XML_NO_CONTENT, NULL, NULL),
GSF_XML_IN_NODE_FULL (START, SP, XL_NS_LEG_VML, "shape", GSF_XML_NO_CONTENT, FALSE, TRUE, &xlsx_vml_shape, NULL, 0),
  GSF_XML_IN_NODE (SP, FILL, XL_NS_LEG_VML, "fill", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (SP, PATH, XL_NS_LEG_VML, "path", GSF_XML_NO_CONTENT, NULL, NULL), /* already defined */
  GSF_XML_IN_NODE (SP, SHADOW, XL_NS_LEG_VML, "shadow", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (SP, STROKE, XL_NS_LEG_VML, "stroke", GSF_XML_NO_CONTENT, NULL, NULL), /* already defined */
  GSF_XML_IN_NODE (SP, TEXTBOX, XL_NS_LEG_VML, "textbox", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (TEXTBOX, DIV, -1, "div", GSF_XML_CONTENT, NULL, &xlsx_vml_textbox_div),
  GSF_XML_IN_NODE (SP, LOCK, XL_NS_LEG_OFF, "lock", GSF_XML_NO_CONTENT, NULL, NULL), /* already defined */
  GSF_XML_IN_NODE (SP, CLIENT_DATA, XL_NS_LEG_XL, "ClientData", GSF_XML_NO_CONTENT, &xlsx_vml_client_data_start, &xlsx_vml_client_data_end),
    GSF_XML_IN_NODE (CLIENT_DATA, ANCHOR, XL_NS_LEG_XL, "Anchor", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (CLIENT_DATA, AUTO_FILL, XL_NS_LEG_XL, "AutoFill", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (CLIENT_DATA, AUTO_LINE, XL_NS_LEG_XL, "AutoLine", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (CLIENT_DATA, CHECKED, XL_NS_LEG_XL, "Checked", GSF_XML_CONTENT, NULL, &xlsx_vml_checked),
    GSF_XML_IN_NODE (CLIENT_DATA, COLUMN, XL_NS_LEG_XL, "Column", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (CLIENT_DATA, DROP_LINES, XL_NS_LEG_XL, "DropLines", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (CLIENT_DATA, DROP_STYLE, XL_NS_LEG_XL, "DropStyle", GSF_XML_CONTENT, NULL, &xlsx_vml_drop_style),
    GSF_XML_IN_NODE (CLIENT_DATA, DX, XL_NS_LEG_XL, "Dx", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (CLIENT_DATA, FIRSTBUTTON, XL_NS_LEG_XL, "FirstButton", GSF_XML_CONTENT, NULL, &xlsx_vml_firstbutton),
    GSF_XML_IN_NODE (CLIENT_DATA, FMLA_LINK, XL_NS_LEG_XL, "FmlaLink", GSF_XML_CONTENT, NULL, &xlsx_vml_fmla_link),
    GSF_XML_IN_NODE (CLIENT_DATA, FMLA_RANGE, XL_NS_LEG_XL, "FmlaRange", GSF_XML_CONTENT, NULL, &xlsx_vml_fmla_range),
    GSF_XML_IN_NODE (CLIENT_DATA, HORIZ, XL_NS_LEG_XL, "Horiz", GSF_XML_CONTENT, NULL, &xlsx_vml_horiz),
    GSF_XML_IN_NODE_FULL (CLIENT_DATA, INC, XL_NS_LEG_XL, "Inc", GSF_XML_CONTENT, FALSE, TRUE, NULL, &xlsx_vml_adj, 2),
    GSF_XML_IN_NODE (CLIENT_DATA, LCT, XL_NS_LEG_XL, "LCT", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE_FULL (CLIENT_DATA, XMIN, XL_NS_LEG_XL, "Min", GSF_XML_CONTENT, FALSE, TRUE, NULL, &xlsx_vml_adj, 0),
    GSF_XML_IN_NODE_FULL (CLIENT_DATA, XMAX, XL_NS_LEG_XL, "Max", GSF_XML_CONTENT, FALSE, TRUE, NULL, &xlsx_vml_adj, 1),
    GSF_XML_IN_NODE (CLIENT_DATA, MOVE_WITH_CELLS, XL_NS_LEG_XL, "MoveWithCells", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE_FULL (CLIENT_DATA, PAGE, XL_NS_LEG_XL, "Page", GSF_XML_CONTENT, FALSE, TRUE, NULL, &xlsx_vml_adj, 3),
    GSF_XML_IN_NODE (CLIENT_DATA, PRINT_OBJECT, XL_NS_LEG_XL, "PrintObject", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (CLIENT_DATA, RECALC_ALWAYS, XL_NS_LEG_XL, "RecalcAlways", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (CLIENT_DATA, ROW, XL_NS_LEG_XL, "Row", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (CLIENT_DATA, SEL, XL_NS_LEG_XL, "Sel", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (CLIENT_DATA, SEL_TYPE, XL_NS_LEG_XL, "SelType", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (CLIENT_DATA, SIZE_WITH_CELLS, XL_NS_LEG_XL, "SizeWithCells", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE_FULL (CLIENT_DATA, VAL, XL_NS_LEG_XL, "Val", GSF_XML_CONTENT, FALSE, TRUE, NULL, &xlsx_vml_adj, 4),
GSF_XML_IN_NODE_FULL (START, GROUP, XL_NS_LEG_VML, "group", GSF_XML_NO_CONTENT, FALSE, TRUE, &xlsx_vml_group, &xlsx_vml_group_end, 0),
  GSF_XML_IN_NODE (GROUP, LOCK, XL_NS_LEG_OFF, "lock", GSF_XML_NO_CONTENT, NULL, NULL), /* already defined */
	GSF_XML_IN_NODE (LOCK, SP, XL_NS_LEG_VML, "shape", GSF_XML_NO_CONTENT, NULL, NULL),  /* already defined */
  GSF_XML_IN_NODE (GROUP, GROUP, XL_NS_LEG_VML, "group", GSF_XML_NO_CONTENT, NULL, NULL),  /* already defined */
  GSF_XML_IN_NODE (GROUP, SP, XL_NS_LEG_VML, "shape", GSF_XML_NO_CONTENT, NULL, NULL),  /* already defined */
  GSF_XML_IN_NODE (GROUP, SP_TYPE, XL_NS_LEG_VML, "shapetype", GSF_XML_NO_CONTENT, NULL, NULL),  /* already defined */
GSF_XML_IN_NODE_END
};

static void
xlsx_sheet_legacy_drawing (GsfXMLIn *xin, xmlChar const **attrs)
{
	xmlChar const *part_id = NULL;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], XL_NS_DOC_REL, "id"))
			part_id = attrs[1];
	if (NULL != part_id) {
		XLSXReadState *state = (XLSXReadState *)xin->user_state;
		state->radio_value = 1;
		xlsx_parse_rel_by_id (xin, part_id, xlsx_legacy_drawing_dtd, xlsx_ns);
	}
}
