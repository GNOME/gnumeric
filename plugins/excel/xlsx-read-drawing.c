/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * xlsx-drawing-read.c : import MS Office Open xlsx drawings and charts.
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

#include "sheet-object-widget.h"

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
	/* Should not be needed, but we might not fully import some color */
	state->gocolor = NULL;
	state->color_setter = NULL;
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
xlsx_chart_text_start (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	if (!GOG_IS_LABEL (state->cur_obj) && IS_SHEET_OBJECT_GRAPH (state->so) && NULL == state->series) { /* Hmm, why? */
		xlsx_push_text_object (state, "Label");
	}
}

static void
xlsx_tx_pr (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	if (GO_IS_STYLED_OBJECT (state->cur_obj) && state->cur_style) {
		state->gocolor = &state->cur_style->font.color;
		state->auto_color = NULL;
	}
}

static void
xlsx_chart_text (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;

	if (IS_GNM_SO_FILLED (state->so))
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
	}
	g_free (state->chart_tx);
	state->chart_tx = NULL;
	state->sp_type &= ~GO_STYLE_FONT;
}

static void
xlsx_chart_title_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;

	if (state->cur_obj == (GogObject *)state->chart) {
		xlsx_push_text_object (state, "Title");
		state->inhibit_text_pop = TRUE;
	} else {
		xlsx_chart_text_start (xin, attrs);
	}
}

static void
xlsx_chart_title_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	state->inhibit_text_pop = FALSE;
	xlsx_chart_text (xin, blob);
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
	/* FIXME: this should be for a text run, not for the full object */
	if (GO_IS_STYLED_OBJECT (state->cur_obj) && state->cur_style) {
		PangoFontDescription *desc = pango_font_description_new ();
		int size = 1000; /* seems 10*100 is the default */
		GOFont const *font;
		/* looks like the default font is Calibri, FIXME: import that from file instead */
		pango_font_description_set_family (desc, "Calibri");
		for (; attrs && *attrs; attrs += 2)
			attr_int (xin, attrs, "sz", &size);
		pango_font_description_set_size (desc, size * PANGO_SCALE / 100);
		/* FIXME: don't set the size to the whole object, only to the run,
		 * anyway, this has to wait until we support rich text in chart labels */
		font = go_font_new_by_desc (desc);
		go_style_set_font (state->cur_style, font);
	}
}

static void
xlsx_body_pr (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	if (GO_IS_STYLED_OBJECT (state->cur_obj) && state->cur_style) {
		for (; attrs && *attrs; attrs += 2)
			if (!strcmp (attrs[0], "rot")) {
				/* FIXME: be careful if the "vert" property exists (not yet supported) */
				int rotation;
				if (attr_int (xin, attrs, "rot", &rotation)) {
					state->cur_style->text_layout.auto_angle = FALSE;
					state->cur_style->text_layout.angle = (double) rotation / 60000.;
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
        GSF_XML_IN_NODE (NV_SP_PR, C_NV_PR, XL_NS_CHART_DRAW, "cNvPr", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (NV_SP_PR, C_NV_SP_PR, XL_NS_CHART_DRAW, "cNvSpPr", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE_FULL (SHAPE, SHAPE_PR, XL_NS_CHART_DRAW, "spPr", GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
        GSF_XML_IN_NODE (SHAPE_PR, SP_PR_PRST_GEOM, XL_NS_DRAW, "prstGeom", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (SP_PR_PRST_GEOM, AV_LST, XL_NS_DRAW, "avLst", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (SHAPE_PR, SP_PR_XFRM, XL_NS_DRAW, "xfrm", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (SP_PR_XFRM, SP_XFRM_OFF, XL_NS_DRAW, "off", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (SP_PR_XFRM, SP_XFRM_EXT, XL_NS_DRAW, "ext", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (SHAPE, TX_BODY, XL_NS_CHART_DRAW, "txBody", GSF_XML_NO_CONTENT, &xlsx_chart_text_start, &xlsx_chart_text),
        GSF_XML_IN_NODE (TX_BODY, LST_STYLE, XL_NS_DRAW, "lstStyle", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (LST_STYLE, DEF_P_PR, XL_NS_DRAW, "defPPr", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (LST_STYLE, EXT_LST, XL_NS_DRAW, "extLst", GSF_XML_NO_CONTENT, NULL, NULL),
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
                GSF_XML_IN_NODE (TEXT_FILL_SOLID, COLOR_RGB, XL_NS_DRAW, "srgbClr", GSF_XML_NO_CONTENT, NULL, NULL),
              GSF_XML_IN_NODE (TX_RICH_R_PR, PR_P_PR_DEF_UFILLTX, XL_NS_DRAW, "uFillTx", GSF_XML_NO_CONTENT, NULL, NULL),
              GSF_XML_IN_NODE (TX_RICH_R_PR, PR_P_PR_DEF_ULNTX, XL_NS_DRAW, "uLnTx", GSF_XML_NO_CONTENT, NULL, NULL),
 	    GSF_XML_IN_NODE (TX_RICH_FLD, PR_P_PR,	XL_NS_DRAW, "pPr", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (PR_P_PR, PR_P_PR_DEF, XL_NS_DRAW, "defRPr", GSF_XML_NO_CONTENT, NULL, NULL),
                GSF_XML_IN_NODE (PR_P_PR_DEF, PR_P_PR_DEF_CS, XL_NS_DRAW, "cs", GSF_XML_NO_CONTENT, NULL, NULL),
                GSF_XML_IN_NODE (PR_P_PR_DEF, PR_P_PR_DEF_EA, XL_NS_DRAW, "ea", GSF_XML_NO_CONTENT, NULL, NULL),
                GSF_XML_IN_NODE (PR_P_PR_DEF, PR_P_PR_DEF_LATIN, XL_NS_DRAW, "latin", GSF_XML_NO_CONTENT, NULL, NULL),
                GSF_XML_IN_NODE (PR_P_PR_DEF, FILL_SOLID, XL_NS_DRAW, "solidFill", GSF_XML_NO_CONTENT, NULL, NULL),
                GSF_XML_IN_NODE (PR_P_PR_DEF, PR_P_PR_DEF_UFILLTX, XL_NS_DRAW, "uFillTx", GSF_XML_NO_CONTENT, NULL, NULL),
                GSF_XML_IN_NODE (PR_P_PR_DEF, PR_P_PR_DEF_ULNTX, XL_NS_DRAW, "uLnTx", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (TX_RICH_FLD, TX_RICH_R_T, XL_NS_DRAW,  "t", GSF_XML_CONTENT, NULL, &xlsx_chart_text_content),
	  GSF_XML_IN_NODE (TEXT_PR_P, TX_RICH_R, XL_NS_DRAW, "r", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (TX_RICH_R, TX_RICH_R_PR, XL_NS_DRAW, "rPr", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
            GSF_XML_IN_NODE (TX_RICH_R, TX_RICH_R_T, XL_NS_DRAW,  "t", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
	  GSF_XML_IN_NODE (TEXT_PR_P, PR_P_PR,	XL_NS_DRAW, "pPr", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
	  GSF_XML_IN_NODE (TEXT_PR_P, PR_P_PR_END,XL_NS_DRAW, "endParaRPr", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (TEXT_PR_P, TX_RICH_R_T, XL_NS_DRAW,  "t", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
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
	if (NULL != (state->plot = (GogPlot*) gog_plot_new_by_name (type)))
		/* Add _before_ setting styles so theme does not override */
		gog_object_add_by_name (GOG_OBJECT (state->chart),
			"Plot", GOG_OBJECT (state->plot));
}

/* shared with pie of pie, and bar of pie */
static void
xlsx_vary_colors (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	int vary;
	if (simple_bool (xin, attrs, &vary))
		g_object_set (G_OBJECT (state->plot),
			"vary-style-by-element", vary, NULL);
}

static void
xlsx_chart_pie_sep (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	int sep;
	if (simple_int (xin, attrs, &sep))
		g_object_set (G_OBJECT (state->plot),
			"default-separation", (double)(CLAMP (sep, 0, 500))/ 100., NULL);
}

/* shared with pie of pie, and bar of pie */
static void xlsx_chart_pie (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs) { xlsx_chart_add_plot (xin, "GogPiePlot"); }
static void xlsx_chart_ring (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs) { xlsx_chart_add_plot (xin, "GogRingPlot"); }

/***********************************************************************/

static void
xlsx_chart_bar_dir (GsfXMLIn *xin, xmlChar const **attrs)
{
	static const EnumVal const dirs[] = {
		{ "bar",	 TRUE },
		{ "col",	 FALSE },
		{ NULL, 0 }
	};
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	int dir;

	g_return_if_fail (state->plot != NULL);

	if (simple_enum (xin, attrs, dirs, &dir))
		g_object_set (G_OBJECT (state->plot), "horizontal", dir, NULL);
}

static void
xlsx_chart_bar_overlap (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	int overlap;
	g_return_if_fail (state->plot != NULL);
	if (simple_int (xin, attrs, &overlap))
		g_object_set (G_OBJECT (state->plot),
			"overlap-percentage", CLAMP (overlap, -100, 100), NULL);
}
static void
xlsx_chart_bar_group (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	char const *type = "normal";

	g_return_if_fail (state->plot != NULL);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (0 == strcmp (attrs[0], "val")) {
			if (0 == strcmp (attrs[1], "percentStacked"))
				type = "as_percentage";
			else if (0 == strcmp (attrs[1], "stacked"))
				type = "stacked";
			g_object_set (G_OBJECT (state->plot), "type", type, NULL);
		}
}
static void
xlsx_chart_bar_gap (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	int gap;
	if (simple_int (xin, attrs, &gap))
		g_object_set (G_OBJECT (state->plot),
			"gap-percentage", CLAMP (gap, 0, 500), NULL);
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
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	if (NULL == state->plot)
		return;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (0 == strcmp (attrs[0], "val")) {
			XLSXAxisInfo *res = g_hash_table_lookup (state->axis.by_id, attrs[1]);
			if (NULL == res) {
				res = g_new0 (XLSXAxisInfo, 1);
				res->id = g_strdup (attrs[1]);
				res->axis	= NULL;
				res->plots	= NULL;
				res->type	= XLSX_AXIS_UNKNOWN;
				res->compass	= GOG_POSITION_AUTO;
				res->cross	= GOG_AXIS_CROSS;
				res->cross_value = go_nan;
				g_hash_table_replace (state->axis.by_id, res->id, res);
#ifdef DEBUG_AXIS
				g_print ("create %s = %p\n", attrs[1], res);
#endif
			}
#ifdef DEBUG_AXIS
			g_print ("add plot %p to %p\n", state->plot, res);
#endif
			res->plots = g_slist_prepend (res->plots, state->plot);
		}
}

static void
xlsx_axis_start (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	state->axis.obj	 = g_object_new (GOG_TYPE_AXIS, NULL);
	state->axis.type = xin->node->user_data.v_int;
	state->axis.info = NULL;
	xlsx_chart_push_obj (state, GOG_OBJECT (state->axis.obj));
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

	simple_float (xin, attrs, &state->axis.info->cross_value);
}

static void
xlsx_axis_id (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (0 == strcmp (attrs[0], "val")) {
			state->axis.info = g_hash_table_lookup (state->axis.by_id, attrs[1]);
			if (NULL != state->axis.info) {
				g_return_if_fail (state->axis.info->axis == NULL);
				state->axis.info->axis = state->axis.obj;
				g_hash_table_replace (state->axis.by_obj,
					state->axis.obj, state->axis.info);
			}
#ifdef DEBUG_AXIS
			g_print ("define %s = %p\n", attrs[1], state->axis.info);
#endif
		}
}

static void
xlsx_axis_delete (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int del = 0;
	if (state->axis.info && simple_bool (xin, attrs, &del))
		state->axis.info->deleted = del;
	if (state->axis.info && del)
		g_object_set (state->axis.info->axis, "invisible", TRUE, NULL);
}
static void
xlsx_axis_orientation (GsfXMLIn *xin, xmlChar const **attrs)
{
	static const EnumVal const orients[] = {
		{ "minMax",	 FALSE },
		{ "maxMin",	 TRUE },
		{ NULL, 0 }
	};
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int orient;
	if (state->axis.info && simple_enum (xin, attrs, orients, &orient))
		g_object_set (G_OBJECT (state->axis.obj),
			"invert-axis", orient, NULL);
}
static void
xlsx_chart_logbase (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int base;
	if (state->axis.info && simple_int (xin, attrs, &base))
		g_object_set (G_OBJECT (state->axis.obj),
			"map-name", "Log", NULL);
}
static void
xlsx_axis_pos (GsfXMLIn *xin, xmlChar const **attrs)
{
	static const EnumVal const positions[] = {
		{ "t",	 GOG_POSITION_N },
		{ "b",	 GOG_POSITION_S },
		{ "l",	 GOG_POSITION_W },
		{ "r",	 GOG_POSITION_E },
		{ NULL, 0 }
	};
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int position;
#ifdef DEBUG_AXIS
	g_print ("SET POS %s for %p\n", attrs[1],  state->axis.info);
#endif
	if (state->axis.info && simple_enum (xin, attrs, positions, &position))
		state->axis.info->compass = position;
}

static void
xlsx_axis_bound (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	gnm_float val;
	if (state->axis.info && simple_float (xin, attrs, &val))
		gog_dataset_set_dim (GOG_DATASET (state->axis.obj),
			xin->node->user_data.v_int,
			go_data_scalar_val_new (val), NULL);
}

static void
xlsx_axis_crosses (GsfXMLIn *xin, xmlChar const **attrs)
{
	static const EnumVal const crosses[] = {
		{ "autoZero",	GOG_AXIS_CROSS },
		{ "max",	GOG_AXIS_AT_HIGH },
		{ "min",	GOG_AXIS_AT_LOW },
		{ NULL, 0 }
	};
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int cross = GOG_AXIS_CROSS;

	if (state->axis.info) {
		if (simple_enum (xin, attrs, crosses, &cross))
			state->axis.info->cross = cross;
		if (cross == GOG_AXIS_CROSS)
			state->axis.info->cross_value = 0.;
	}
}

static void
xlsx_axis_crossax (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	if (state->axis.info && !strcmp ((char const*) attrs[0], "val"))
		state->axis.info->cross_id = g_strdup (attrs[1]);
}

static void
xlsx_chart_gridlines (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	gboolean ismajor = xin->node->user_data.v_int;
	if (NULL != state->axis.obj) {
		GogObject *grid = gog_object_add_by_name
			(GOG_OBJECT (state->axis.obj),
			 ismajor ? "MajorGrid" : "MinorGrid",
			 NULL);
		xlsx_chart_push_obj (state, grid);
	}
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
	};
	int res;

	if (!simple_enum (xin, attrs, marks, &res))
		return;

	g_object_set (G_OBJECT (state->axis.obj),
		      (ismajor ? "major-tick-in" : "minor-tick-in"), (res & 1) != 0,
		      (ismajor ? "major-tick-out" : "minor-tick-out"), (res & 2) != 0,
		      NULL);
}

static void
xslx_chart_tick_label_pos (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	if (attrs && !strcmp (attrs[0], "val") && !strcmp (attrs[1], "none"))
	    g_object_set (G_OBJECT (state->axis.obj), "major-tick-labeled", FALSE, NULL);
}

static void
xlsx_axis_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;

	/* Try to guess what type of axis to use */
	if (NULL != state->axis.info) {
		GogPlot *plot = state->axis.info->plots->data; /* just use the first */
		char const *type = G_OBJECT_TYPE_NAME (plot);
		char const *role = NULL;
		GSList *ptr;

		if (0 == strcmp (type, "GogRadarPlot") ||
		    0 == strcmp (type, "GogRadarAreaPlot")) {
			role = (state->axis.type == XLSX_AXIS_CAT
				|| state->axis.type == XLSX_AXIS_DATE) ? "Circular-Axis" : "Radial-Axis";
		} else if (0 == strcmp (type, "GogBubblePlot") ||
			   0 == strcmp (type, "GogXYPlot")) {
			/* both are VAL, use the position to decide */
			if (state->axis.info->compass  == GOG_POSITION_N ||
			    state->axis.info->compass  == GOG_POSITION_S)
				role = "X-Axis";
			else
				role = "Y-Axis";
		} else if (0 == strcmp (type, "GogBarColPlot")) {
			gboolean h;
			/* swap for bar plots */
			g_object_get (G_OBJECT (plot), "horizontal", &h, NULL);
			if (h)
				role = (state->axis.type == XLSX_AXIS_CAT
				        || state->axis.type == XLSX_AXIS_DATE) ? "Y-Axis" : "X-Axis";
		}

		if (NULL == role)
			role = (state->axis.type == XLSX_AXIS_CAT
				|| state->axis.type == XLSX_AXIS_DATE) ? "X-Axis" : "Y-Axis";

		/* absorb a ref, and set the id, and atype */
		gog_object_add_by_name (GOG_OBJECT (state->chart),
			role, GOG_OBJECT (state->axis.obj));
		g_object_ref (state->axis.obj);
		for (ptr = state->axis.info->plots; ptr != NULL ; ptr = ptr->next) {
#ifdef DEBUG_AXIS
			g_print ("connect plot %p to %p in role %s\n", ptr->data, state->axis.obj, role);
#endif
			gog_plot_set_axis (ptr->data, state->axis.obj);
		}

		state->axis.obj  = NULL;
		state->axis.info = NULL;
	}

	xlsx_chart_pop_obj (state);
	state->axis.info = NULL;
}

static void xlsx_chart_area (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs) { xlsx_chart_add_plot (xin, "GogAreaPlot"); }
static void xlsx_chart_line (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs) { xlsx_chart_add_plot (xin, "GogLinePlot"); }

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
	static const EnumVal styles[] = {
		{"line",	0},
		{"lineMarker",  1},
		{"marker",      2},
		{"markers",     2}, /* We used to write this erroneously */
		{"none",	3},
		{"smooth",      4},
		{"smoothMarker", 5}
	};
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int style;

	if (simple_enum (xin, attrs, styles, &style))
		switch (style) {
		case 0:
			g_object_set (G_OBJECT (state->plot),
			              "default-style-has-markers", FALSE,
			              NULL);
			break;
		case 2:
			g_object_set (G_OBJECT (state->plot),
			              "default-style-has-lines", FALSE,
			              NULL);
			break;
		case 3:
			g_object_set (G_OBJECT (state->plot),
			              "default-style-has-markers", FALSE,
			              "default-style-has-lines", FALSE,
			              NULL);
			break;
		case 4:
			g_object_set (G_OBJECT (state->plot),
			              "use-splines", TRUE,
			              "default-style-has-markers", FALSE, NULL);
			break;
		case 5:
			g_object_set (G_OBJECT (state->plot),
			              "use-splines", TRUE,
			              NULL);
			break;
		}
}

static void 
xlsx_chart_bubble (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{ xlsx_chart_add_plot (xin, "GogBubblePlot"); }

static void 
xlsx_chart_radar (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{ xlsx_chart_add_plot (xin, "GogRadarPlot"); }

#if 0
	char const *type = "GogRadarPlot";
	gboolean with_markers = FALSE;
	/* Irritants.  They put the sub type into a child record ... */
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (0 == strcmp (attrs[0], "type")) {
			if (0 == strcmp (attrs[1], "filled"))
				type = "as_percentage";
			else if (0 == strcmp (attrs[1], "marker"))
				type = "stacked";
			g_object_set (G_OBJECT (state->plot), "type", type, NULL);
		}
		if (0 == strcmp (xin, attrs, "cx", state->drawing_pos + (COL | TO | OFFSET)))
			state->drawing_pos_flags |= (1 << (COL | TO | OFFSET));
	g_object_set (G_OBJECT (state->plot), "default-style-has-markers", with_markers, NULL);
#endif

static void
xlsx_plot_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	state->plot = NULL;
}

static void
xlsx_chart_ser_start (GsfXMLIn *xin, G_GNUC_UNUSED  xmlChar const **attrs)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	if (NULL != state->plot) {
		state->series = gog_plot_new_series (state->plot);
		xlsx_chart_push_obj (state, GOG_OBJECT (state->series));
	}
}
static void
xlsx_chart_ser_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	if (NULL != state->series) {
		xlsx_chart_pop_obj (state);
		state->series = NULL;
	}
}

static void
xlsx_ser_labels_show_val (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	gboolean has_val;
	if (GOG_IS_SERIES_LABELS (state->cur_obj) && attr_bool (xin, attrs, "val", &has_val)) {
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
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	gboolean has_cat;
	if (GOG_IS_SERIES_LABELS (state->cur_obj) && attr_bool (xin, attrs, "val", &has_cat)) {
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
		{"t", GOG_SERIES_LABELS_TOP}
	};
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int position;

	if (simple_enum (xin, attrs, pos, &position))
		gog_series_labels_set_position (GOG_SERIES_LABELS (state->cur_obj), position);
}

static void
xlsx_ser_labels_start (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	if (NULL != state->series) {
		GogObject *data = gog_object_add_by_name (GOG_OBJECT (state->series), "Data labels", NULL);
		GOData *sep = go_data_scalar_str_new (",", FALSE); /* FIXME, should be "\n" for pies */
		gog_dataset_set_dim (GOG_DATASET (data), 1, sep, NULL);
		g_object_set (data, "format", "", "offset", 3, NULL);
		xlsx_chart_push_obj (state, data);
	}
}

static void
xlsx_ser_labels_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
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
		{"t", GOG_SERIES_LABELS_TOP}
	};
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int position;

	if (simple_enum (xin, attrs, pos, &position))
		gog_data_label_set_position (GOG_DATA_LABEL (state->cur_obj), position);
}

static void
xlsx_data_label_index (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int index;
	if (simple_int (xin, attrs, &index))
		g_object_set (state->cur_obj, "index", index, NULL);
}

static void
xlsx_data_label_show_val (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	gboolean has_val;
	if (GOG_IS_DATA_LABEL (state->cur_obj) && attr_bool (xin, attrs, "val", &has_val)) {
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
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	gboolean has_cat;
	if (GOG_IS_DATA_LABEL (state->cur_obj) && attr_bool (xin, attrs, "val", &has_cat)) {
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
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	GogObject *data = gog_object_add_by_name (state->cur_obj, "Point", NULL);
	GOData *sep = go_data_scalar_str_new (",", FALSE); /* FIXME, should be "\n" for pies */
	gog_dataset_set_dim (GOG_DATASET (data), 1, sep, NULL);
	g_object_set (data, "format", "", "offset", 3, NULL);
	xlsx_chart_push_obj (state, data);
}

static void
xlsx_chart_ser_f (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
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
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	xlsx_chart_push_obj (state, gog_object_add_by_name (GOG_OBJECT (state->chart), "Legend", NULL));
}

static void
xlsx_chart_legend_pos (GsfXMLIn *xin, xmlChar const **attrs)
{
	static const EnumVal const positions[] = {
		{ "t",	 GOG_POSITION_N },
		{ "b",	 GOG_POSITION_S },
		{ "l",	 GOG_POSITION_W },
		{ "r",	 GOG_POSITION_E },
		{ "rt",	 GOG_POSITION_N | GOG_POSITION_E },
		/* adding extra values not in spec, but actually possible (found first one at least) */
		{ "tr",	 GOG_POSITION_N | GOG_POSITION_E },
		{ "lt",	 GOG_POSITION_N | GOG_POSITION_W },
		{ "tl",	 GOG_POSITION_N | GOG_POSITION_W },
		{ "rb",	 GOG_POSITION_S | GOG_POSITION_E },
		{ "br",	 GOG_POSITION_S | GOG_POSITION_E },
		{ "lb",	 GOG_POSITION_S | GOG_POSITION_W },
		{ "bl",	 GOG_POSITION_S| GOG_POSITION_W },
		{ NULL, 0 }
	};
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int position;
	if (GOG_IS_LEGEND (state->cur_obj) && simple_enum (xin, attrs, positions, &position)) {
		gog_object_set_position_flags (state->cur_obj, position, GOG_POSITION_COMPASS);
	}
}

static void
xlsx_chart_pt_start (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	if (NULL != state->series) {
		state->series_pt_has_index = FALSE;
		state->series_pt = gog_object_add_by_name (
			GOG_OBJECT (state->series), "Point", NULL);
		xlsx_chart_push_obj (state, state->series_pt);
	}
}

static void
xlsx_chart_pt_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	if (NULL != state->series) {
		xlsx_chart_pop_obj (state);
		if (!state->series_pt_has_index) {
			gog_object_clear_parent (state->series_pt);
			g_object_unref (state->series_pt);
		}
		state->series_pt = NULL;
	}
}

static void
xlsx_chart_pt_index (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int tmp;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_int (xin, attrs, "val", &tmp)) {
			state->series_pt_has_index = TRUE;
			g_object_set (state->series_pt, "index", tmp, NULL);
		}
}

static void
xlsx_chart_pt_sep (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int sep;
	if (simple_int (xin, attrs, &sep) &&
	    g_object_class_find_property (G_OBJECT_GET_CLASS (state->series_pt), "separation"))
		g_object_set (state->series_pt, "separation", (double)sep / 100., NULL);
}

static void
xlsx_style_line_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	int w = 0;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_int (xin, attrs, "w", &w))
			; /* Nothing */
	}

	state->sp_type |= GO_STYLE_LINE;
	if (!state->cur_style)
		state->cur_style = (GOStyle *) gog_style_new ();
	state->cur_style->line.width = w / 12700.;
	state->gocolor = &state->cur_style->line.color;
}

static void
xlsx_style_line_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	state->sp_type &= ~GO_STYLE_LINE;
	state->gocolor = NULL;
}

static void
xlsx_chart_no_fill (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	if (NULL != state->marker)
		;
	else if (NULL != state->cur_style) {
		if (!(state->sp_type & GO_STYLE_LINE)) {
			state->cur_style->fill.type = GO_STYLE_FILL_NONE;
			state->cur_style->fill.auto_type = FALSE;
		}
	}
}

static void
xlsx_chart_grad_fill (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	if (NULL != state->marker) /* do xlsx support gradients in markers */
		;
	else if (NULL != state->cur_style) {
		if (!(state->sp_type & GO_STYLE_LINE)) {
			state->cur_style->fill.type = GO_STYLE_FILL_GRADIENT;
			state->cur_style->fill.auto_type = FALSE;
		}
	}
}

static void
xlsx_chart_grad_linear (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	int ang;
	g_return_if_fail (state->cur_style);
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_int (xin, attrs, "ang", &ang))
			state->cur_style->fill.gradient.dir
				= xlsx_get_gradient_direction (ang / 60000.);
	/* FIXME: we do not support the "scaled" attribute */
}

static void
xlsx_chart_grad_stop (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	int pos;
	g_return_if_fail (state->cur_style);
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_int (xin, attrs, "pos", &pos)) {
			if (pos <= 50000) {
				/* FIXME: use betstate->auto_colorter gradients
				 * for now, we only support stops at 0 and 1 */
				state->gocolor = &state->cur_style->fill.pattern.back;
				state->auto_color = &state->cur_style->fill.auto_back;
			} else {
				state->gocolor = &state->cur_style->fill.pattern.fore;
				state->auto_color = &state->cur_style->fill.auto_fore;
			}
		}
}

static void
xlsx_chart_solid_fill (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	if (NULL != state->marker) {
		if (!(state->sp_type & GO_STYLE_LINE)) {
			state->color_setter = (void (*) (gpointer data, GOColor color)) go_marker_set_fill_color;
			state->color_data = state->marker;
		} else {
			state->color_setter = (void (*) (gpointer data, GOColor color)) go_marker_set_outline_color;
			state->color_data = state->marker;
		}
	} else if ((NULL != state->cur_style) && (state->gocolor == NULL)) {
		if (state->sp_type & GO_STYLE_LINE) {
			state->cur_style->line.dash_type = GO_LINE_SOLID;
			state->gocolor = &state->cur_style->line.color;
			state->auto_color = &state->cur_style->line.auto_color;
		} else if (state->sp_type & GO_STYLE_FONT) {
			state->gocolor = &state->cur_style->font.color;
			state->auto_color = &state->cur_style->font.auto_color;
		} else {
			state->cur_style->fill.type = GO_STYLE_FILL_PATTERN;
			state->cur_style->fill.auto_type = FALSE;
			state->cur_style->fill.pattern.pattern = GO_PATTERN_FOREGROUND_SOLID;
			state->gocolor = &state->cur_style->fill.pattern.fore;
			state->auto_color = &state->cur_style->fill.auto_fore;
		}
	}
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
	gpointer val = NULL;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (0 == strcmp (attrs[0], "val")) {
			val = g_hash_table_lookup (state->theme_colors_by_name, attrs[1]);
			if (NULL == val)
				xlsx_warning (xin, _("Unknown color '%s'"), attrs[1]);
		}

	state->color = GPOINTER_TO_UINT (val);
	if (state->gocolor) {
		if (*state->gocolor != state->color) {
			*state->gocolor = state->color;
			if (state->auto_color)
				*state->auto_color = FALSE;
		}
		state->gocolor = NULL;
		state->auto_color = NULL;
	} else if (state->color_setter) {
		state->color_setter (state->color_data, state->color);
		state->color_setter = NULL;
	}
}

static void
xlsx_draw_color_rgb (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_gocolor (xin, attrs, "val", &state->color))
			if (state->auto_color)
				*state->auto_color = FALSE;
}

static void
xlsx_draw_color_alpha (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	int val;
	if (simple_int (xin, attrs, &val)) {
		int level = 255 * val / 100000;
		state->color = GO_COLOR_CHANGE_A (state->color, level);
		if (state->auto_color)
			state->auto_color = FALSE;
	}
}

static void
xlsx_draw_color_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	if (state->gocolor) {
		if (*state->gocolor != state->color) {
			*state->gocolor = state->color;
			if (state->auto_color)
				*state->auto_color = FALSE;
		}
		state->gocolor = NULL;
		state->auto_color = NULL;
	} else if (state->color_setter) {
		state->color_setter (state->color_data, state->color);
		state->color_setter = NULL;
	}
}

static void
xlsx_draw_line_dash (GsfXMLIn *xin, xmlChar const **attrs)
{
	static const EnumVal const dashes[] = {
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
	int dash;

	if (!simple_enum (xin, attrs, dashes, &dash))
		return;

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
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	state->marker = go_marker_new ();
	state->marker_symbol = GO_MARKER_MAX;
}

static void
xlsx_chart_marker_symbol (GsfXMLIn *xin, xmlChar const **attrs)
{
	static const EnumVal const symbols[] = {
		{ "circle",	GO_MARKER_CIRCLE },
		{ "dash",	GO_MARKER_BAR },		/* FIXME */
		{ "diamond",	GO_MARKER_DIAMOND },
		{ "dot",	GO_MARKER_HALF_BAR },		/* FIXME */
		{ "none",	GO_MARKER_NONE },
		{ "plus",	GO_MARKER_CROSS },		/* CHECK ME */
		{ "square",	GO_MARKER_SQUARE },
		{ "star",	GO_MARKER_ASTERISK },		/* CHECK ME */
		{ "triangle",	GO_MARKER_TRIANGLE_UP },	/* FIXME */
		{ "x",		GO_MARKER_X },
		{ NULL, 0 }
	};
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int symbol;
	if (NULL != state->marker && simple_enum (xin, attrs, symbols, &symbol))
		state->marker_symbol = symbol;
}

static void
xlsx_chart_marker_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	if (NULL != state->cur_obj && GOG_IS_STYLED_OBJECT (state->cur_obj)) {
		if (state->marker_symbol != GO_MARKER_MAX) {
			state->cur_style->marker.auto_shape = FALSE;
			go_marker_set_shape (state->marker, state->marker_symbol);
		}
		go_style_set_marker (state->cur_style, state->marker);
		state->marker = NULL;
		state->gocolor = NULL;
		state->color_setter = NULL;
	}
}

static void
xlsx_plot_area (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	GogObject *backplane = gog_object_add_by_name (
		GOG_OBJECT (state->chart), "Backplane", NULL);
	/* set a transparent default background */
	GOStyle *style = go_styled_object_get_style (GO_STYLED_OBJECT (backplane));
	style->fill.type = GO_STYLE_FILL_NONE;
	style->fill.auto_type = FALSE;
	xlsx_chart_push_obj (state, backplane);
}

static void
xlsx_chart_pop (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	xlsx_chart_pop_obj ((XLSXReadState *)xin->user_state);
}

#if 0
/* this adds a NULL object so that the style is not polluted by unsupported things */
static void
xlsx_chart_start_dummy (GsfXMLIn *xin, xmlChar const **attrs)
{
	xlsx_chart_push_obj ((XLSXReadState *)xin->user_state, NULL);
}
#endif

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

	if (GOG_IS_GRID (state->cur_obj)) {

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
	state->chart_pos_target = !(attrs[1] && strcmp (attrs[1], "inner"));
}

static void
xlsx_chart_layout_dim (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	simple_float (xin, attrs, state->chart_pos + xin->node->user_data.v_int);
}

static void
xlsx_chart_layout_mode (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	static const EnumVal const choices[] = {
		{ "factor",	FALSE },
		{ "edge",	TRUE },
		{ NULL, 0 }
	};
	int choice = FALSE;

	if (simple_enum (xin, attrs, choices, &choice))
		state->chart_pos_mode[xin->node->user_data.v_int] = choice;
}

static GsfXMLInNode const xlsx_chart_dtd[] =
{
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE_FULL (START, CHART_SPACE, XL_NS_CHART, "chartSpace", GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
  GSF_XML_IN_NODE (CHART_SPACE, DATE1904, XL_NS_CHART, "date1904", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (CHART_SPACE, ROUNDEDCORNERS, XL_NS_CHART, "roundedCorners", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (CHART_SPACE, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (SHAPE_PR, SP_XFRM, XL_NS_DRAW, "xfrm", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (SP_XFRM, SP_XFRM_OFF, XL_NS_DRAW, "off", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (SP_XFRM, SP_XFRM_EXT, XL_NS_DRAW, "ext", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (SHAPE_PR, SP_PR_PRST_GEOM, XL_NS_DRAW, "prstGeom", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (SHAPE_PR, FILL_NONE,	XL_NS_DRAW, "noFill", GSF_XML_NO_CONTENT, &xlsx_chart_no_fill, NULL),
    GSF_XML_IN_NODE (SHAPE_PR, FILL_SOLID,	XL_NS_DRAW, "solidFill", GSF_XML_NO_CONTENT, &xlsx_chart_solid_fill, NULL),
      GSF_XML_IN_NODE (FILL_SOLID, COLOR_THEMED, XL_NS_DRAW, "schemeClr", GSF_XML_NO_CONTENT, &xlsx_draw_color_themed, &xlsx_draw_color_end),
        GSF_XML_IN_NODE (COLOR_THEMED, COLOR_LUM, XL_NS_DRAW, "lumMod", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (FILL_SOLID, COLOR_RGB,	 XL_NS_DRAW, "srgbClr", GSF_XML_NO_CONTENT, &xlsx_draw_color_rgb, &xlsx_draw_color_end),
        GSF_XML_IN_NODE (COLOR_RGB, RGB_ALPHA,	   XL_NS_DRAW, "alpha", GSF_XML_NO_CONTENT, &xlsx_draw_color_alpha, NULL),
        GSF_XML_IN_NODE (COLOR_RGB, RGB_GAMMA,	   XL_NS_DRAW, "gamma", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (COLOR_RGB, RGB_INV_GAMMA, XL_NS_DRAW, "invGamma", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (COLOR_RGB, RGB_SHADE,	   XL_NS_DRAW, "shade", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (COLOR_RGB, RGB_TINT,	   XL_NS_DRAW, "tint", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (COLOR_RGB, LN_DASH,	   XL_NS_DRAW, "prstDash", GSF_XML_NO_CONTENT, &xlsx_draw_line_dash, NULL),
      GSF_XML_IN_NODE (FILL_SOLID, LN_DASH,	   XL_NS_DRAW, "prstDash", GSF_XML_NO_CONTENT, NULL, NULL), /* 2nd Def */

    GSF_XML_IN_NODE (SHAPE_PR, FILL_BLIP,	XL_NS_DRAW, "blipFill", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (FILL_BLIP, FILL_BLIP_BLIP,	XL_NS_DRAW, "blip", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (FILL_BLIP, FILL_BLIP_SRC,	XL_NS_DRAW, "srcRect", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (FILL_BLIP, FILL_BLIP_TILE,	XL_NS_DRAW, "tile", GSF_XML_NO_CONTENT, NULL, NULL),

    GSF_XML_IN_NODE (SHAPE_PR, FILL_GRAD,	XL_NS_DRAW, "gradFill", GSF_XML_NO_CONTENT, &xlsx_chart_grad_fill, NULL),
      GSF_XML_IN_NODE (FILL_GRAD, GRAD_LIST,	XL_NS_DRAW, "gsLst", GSF_XML_NO_CONTENT, NULL, NULL),
       GSF_XML_IN_NODE (GRAD_LIST, GRAD_LIST_ITEM, XL_NS_DRAW, "gs", GSF_XML_NO_CONTENT, NULL, NULL),
         GSF_XML_IN_NODE (GRAD_LIST_ITEM, COLOR_RGB, XL_NS_DRAW, "srgbClr", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
      GSF_XML_IN_NODE (FILL_GRAD, GRAD_LINE,	XL_NS_DRAW, "lin", GSF_XML_NO_CONTENT, &xlsx_chart_grad_linear, NULL),

    GSF_XML_IN_NODE (SHAPE_PR, FILL_PATT,	XL_NS_DRAW, "pattFill", GSF_XML_NO_CONTENT, &xlsx_chart_solid_fill, NULL),
      GSF_XML_IN_NODE (FILL_PATT, FILL_PATT_BG,	XL_NS_DRAW, "bgClr", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (FILL_PATT_BG, COLOR_RGB, XL_NS_DRAW, "srgbClr", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
      GSF_XML_IN_NODE (FILL_PATT, FILL_PATT_FG,	XL_NS_DRAW, "fgClr", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (FILL_PATT_FG, COLOR_RGB, XL_NS_DRAW, "srgbClr", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */

    GSF_XML_IN_NODE (SHAPE_PR, SHAPE_PR_LN, XL_NS_DRAW, "ln", GSF_XML_NO_CONTENT, &xlsx_style_line_start, &xlsx_style_line_end),
      GSF_XML_IN_NODE (SHAPE_PR_LN, LN_NOFILL, XL_NS_DRAW, "noFill", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (SHAPE_PR_LN, LN_DASH, XL_NS_DRAW, "prstDash", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
      GSF_XML_IN_NODE (SHAPE_PR_LN, FILL_SOLID, XL_NS_DRAW, "solidFill", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
      GSF_XML_IN_NODE (SHAPE_PR_LN, FILL_PATT,	XL_NS_DRAW, "pattFill", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
      GSF_XML_IN_NODE (SHAPE_PR_LN, LN_MITER,	XL_NS_DRAW, "miter", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (SHAPE_PR_LN, LN_ROUND,	XL_NS_DRAW, "round", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (SHAPE_PR_LN, LN_HEAD,	XL_NS_DRAW, "headEnd", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (SHAPE_PR_LN, LN_TAIL,	XL_NS_DRAW, "tailEnd", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (SHAPE_PR, TEXT_PR, XL_NS_CHART, "txPr", GSF_XML_NO_CONTENT, &xlsx_tx_pr, NULL),
      GSF_XML_IN_NODE (TEXT_PR, TEXT_PR_BODY,	XL_NS_DRAW, "bodyPr", GSF_XML_NO_CONTENT, &xlsx_body_pr, NULL),
      GSF_XML_IN_NODE (TEXT_PR, TEXT_PR_STYLE,	XL_NS_DRAW, "lstStyle", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (TEXT_PR, TEXT_PR_P,	XL_NS_DRAW, "p", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (TEXT_PR_P, PR_P_PR,	XL_NS_DRAW, "pPr", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (PR_P_PR, PR_P_PR_DEF, XL_NS_DRAW, "defRPr", GSF_XML_NO_CONTENT, &xlsx_draw_text_run_props, NULL),
            GSF_XML_IN_NODE (PR_P_PR_DEF, PR_P_PR_DEF_CS, XL_NS_DRAW, "cs", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (PR_P_PR_DEF, PR_P_PR_DEF_EA, XL_NS_DRAW, "ea", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (PR_P_PR_DEF, PR_P_PR_DEF_LATIN, XL_NS_DRAW, "latin", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (PR_P_PR_DEF, FILL_SOLID, XL_NS_DRAW, "solidFill", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
            GSF_XML_IN_NODE (PR_P_PR_DEF, PR_P_PR_DEF_UFILLTX, XL_NS_DRAW, "uFillTx", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (PR_P_PR_DEF, PR_P_PR_DEF_ULNTX, XL_NS_DRAW, "uLnTx", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (TEXT_PR_P, PR_P_PR_END,XL_NS_DRAW, "endParaRPr", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (CHART_SPACE, TEXT_PR, XL_NS_CHART, "txPr", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */

  GSF_XML_IN_NODE (CHART_SPACE, CHART, XL_NS_CHART, "chart", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (CHART, SHOW_DBLS_OVER_MAX, XL_NS_CHART, "showDLblsOverMax", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (CHART, PLOTAREA, XL_NS_CHART, "plotArea", GSF_XML_NO_CONTENT, &xlsx_plot_area, &xlsx_chart_pop),
      GSF_XML_IN_NODE (PLOTAREA, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
      GSF_XML_IN_NODE_FULL (PLOTAREA, CAT_AXIS, XL_NS_CHART, "catAx", GSF_XML_NO_CONTENT, FALSE, TRUE,
			    &xlsx_axis_start, &xlsx_axis_end, XLSX_AXIS_CAT),
        GSF_XML_IN_NODE (CAT_AXIS, AXIS_AXID, XL_NS_CHART, "axId", GSF_XML_NO_CONTENT, &xlsx_axis_id, NULL),
        GSF_XML_IN_NODE (CAT_AXIS, AXIS_DELETE, XL_NS_CHART, "delete", GSF_XML_NO_CONTENT, &xlsx_axis_delete, NULL),
        GSF_XML_IN_NODE (CAT_AXIS, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */
        GSF_XML_IN_NODE (CAT_AXIS, AXIS_NUMFMT, XL_NS_CHART, "numFmt", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (CAT_AXIS, AXIS_DELETE, XL_NS_CHART, "delete", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE_FULL (CAT_AXIS, AXIS_MAJORTICKMARK, XL_NS_CHART, "majorTickMark", GSF_XML_NO_CONTENT, FALSE, TRUE, &xlsx_axis_mark, NULL, 1),
        GSF_XML_IN_NODE_FULL (CAT_AXIS, AXIS_MINORTICKMARK, XL_NS_CHART, "minorTickMark", GSF_XML_NO_CONTENT, FALSE, TRUE, &xlsx_axis_mark, NULL, 0),
        GSF_XML_IN_NODE_FULL (CAT_AXIS, AXIS_MAJORTICK_UNIT, XL_NS_CHART, "majorUnit", GSF_XML_NO_CONTENT, FALSE, TRUE,
			      &xlsx_axis_bound, NULL, GOG_AXIS_ELEM_MAJOR_TICK),
        GSF_XML_IN_NODE_FULL (CAT_AXIS, AXIS_MINORTICK_UNIT, XL_NS_CHART, "minorUnit", GSF_XML_NO_CONTENT, FALSE, TRUE,
			      &xlsx_axis_bound, NULL, GOG_AXIS_ELEM_MINOR_TICK),
        GSF_XML_IN_NODE (CAT_AXIS, AXIS_TICK_LBL_SKIP, XL_NS_CHART, "tickLblSkip", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (CAT_AXIS, AXIS_TICK_MARK_SKIP, XL_NS_CHART, "tickMarkSkip", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (CAT_AXIS, TEXT_PR, XL_NS_CHART, "txPr", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
        GSF_XML_IN_NODE (CAT_AXIS, AXIS_SCALING, XL_NS_CHART, "scaling", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE_FULL (AXIS_SCALING, AX_MIN, XL_NS_CHART, "min", GSF_XML_NO_CONTENT, FALSE, TRUE,
				&xlsx_axis_bound, NULL, GOG_AXIS_ELEM_MIN),
          GSF_XML_IN_NODE_FULL (AXIS_SCALING, AX_MAX, XL_NS_CHART, "max", GSF_XML_NO_CONTENT, FALSE, TRUE,
				&xlsx_axis_bound, NULL, GOG_AXIS_ELEM_MAX),
          GSF_XML_IN_NODE (AXIS_SCALING, AX_LOG, XL_NS_CHART, "logBase", GSF_XML_NO_CONTENT, &xlsx_chart_logbase, NULL),
          GSF_XML_IN_NODE (AXIS_SCALING, AX_ORIENTATION, XL_NS_CHART, "orientation", GSF_XML_NO_CONTENT, &xlsx_axis_orientation, NULL),
        GSF_XML_IN_NODE_FULL (CAT_AXIS, MAJOR_GRID, XL_NS_CHART, "majorGridlines", GSF_XML_NO_CONTENT,
			      FALSE, FALSE, &xlsx_chart_gridlines, &xlsx_chart_pop, 1),
          GSF_XML_IN_NODE (MAJOR_GRID, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
        GSF_XML_IN_NODE_FULL (CAT_AXIS, MINOR_GRID, XL_NS_CHART, "minorGridlines", GSF_XML_NO_CONTENT,
			      FALSE, FALSE, &xlsx_chart_gridlines, &xlsx_chart_pop, 0),
          GSF_XML_IN_NODE (MINOR_GRID, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
        GSF_XML_IN_NODE (CAT_AXIS, AXIS_POS, XL_NS_CHART, "axPos", GSF_XML_NO_CONTENT, &xlsx_axis_pos, NULL),
        GSF_XML_IN_NODE (CAT_AXIS, CAT_AXIS_TICKLBLPOS, XL_NS_CHART, "tickLblPos", GSF_XML_NO_CONTENT, &xslx_chart_tick_label_pos, NULL),
        GSF_XML_IN_NODE (CAT_AXIS, CAT_AXIS_AUTO, XL_NS_CHART, "auto", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (CAT_AXIS, AXIS_CROSSAX, XL_NS_CHART, "crossAx", GSF_XML_NO_CONTENT, &xlsx_axis_crossax, NULL),
        GSF_XML_IN_NODE (CAT_AXIS, AXIS_CROSSES, XL_NS_CHART, "crosses", GSF_XML_NO_CONTENT, &xlsx_axis_crosses, NULL),
        GSF_XML_IN_NODE (CAT_AXIS, AXIS_CROSSES_AT, XL_NS_CHART, "crossesAt", GSF_XML_NO_CONTENT, &xlsx_axis_crosses_at, NULL),

        GSF_XML_IN_NODE (CAT_AXIS, CAT_AXIS_LBLALGN, XL_NS_CHART, "lblAlgn", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (CAT_AXIS, CAT_AXIS_LBLOFFSET, XL_NS_CHART, "lblOffset", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (CAT_AXIS, TEXT_PR, XL_NS_CHART, "txPr", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
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
              GSF_XML_IN_NODE (TITLE, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
              GSF_XML_IN_NODE (TITLE, TEXT, XL_NS_CHART, "tx", GSF_XML_NO_CONTENT, &xlsx_chart_text_start, &xlsx_chart_text),
            GSF_XML_IN_NODE (TEXT, TX_RICH, XL_NS_CHART, "rich", GSF_XML_NO_CONTENT, NULL, NULL),
              GSF_XML_IN_NODE (TX_RICH, TX_RICH_BODY, XL_NS_CHART, "bodyP", GSF_XML_NO_CONTENT, NULL, NULL),
              GSF_XML_IN_NODE (TX_RICH, TEXT_PR_BODY, XL_NS_DRAW, "bodyPr", GSF_XML_NO_CONTENT, NULL, NULL),
              GSF_XML_IN_NODE (TX_RICH, TX_RICH_STYLES, XL_NS_DRAW, "lstStyle", GSF_XML_NO_CONTENT, NULL, NULL),
              GSF_XML_IN_NODE (TX_RICH, TX_RICH_P, XL_NS_DRAW, "p", GSF_XML_NO_CONTENT, &xlsx_chart_p_start, NULL),
                GSF_XML_IN_NODE (TX_RICH_P, PR_P_PR, XL_NS_DRAW, "pPr", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
                GSF_XML_IN_NODE (TX_RICH_P, TX_RICH_R, XL_NS_DRAW, "r", GSF_XML_NO_CONTENT, NULL, NULL),
                  GSF_XML_IN_NODE (TX_RICH_R, TX_RICH_R_PR, XL_NS_DRAW, "rPr", GSF_XML_NO_CONTENT, NULL, NULL),
		    GSF_XML_IN_NODE (TX_RICH_R_PR, PR_P_PR_DEF_CS, XL_NS_DRAW, "cs", GSF_XML_NO_CONTENT, NULL, NULL),
		    GSF_XML_IN_NODE (TX_RICH_R_PR, PR_P_PR_DEF_EA, XL_NS_DRAW, "ea", GSF_XML_NO_CONTENT, NULL, NULL),
		    GSF_XML_IN_NODE (TX_RICH_R_PR, PR_P_PR_DEF_LATIN, XL_NS_DRAW, "latin", GSF_XML_NO_CONTENT, NULL, NULL),
		    GSF_XML_IN_NODE (TX_RICH_R_PR, FILL_SOLID, XL_NS_DRAW, "solidFill", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
		    GSF_XML_IN_NODE (TX_RICH_R_PR, PR_P_PR_DEF_UFILLTX, XL_NS_DRAW, "uFillTx", GSF_XML_NO_CONTENT, NULL, NULL),
		    GSF_XML_IN_NODE (TX_RICH_R_PR, PR_P_PR_DEF_ULNTX, XL_NS_DRAW, "uLnTx", GSF_XML_NO_CONTENT, NULL, NULL),
                  GSF_XML_IN_NODE (TX_RICH_R, TX_RICH_R_T, XL_NS_DRAW,  "t", GSF_XML_CONTENT, NULL, &xlsx_chart_text_content),
            GSF_XML_IN_NODE (TEXT, STR_REF, XL_NS_CHART, "strRef", GSF_XML_NO_CONTENT, NULL, NULL),
              GSF_XML_IN_NODE (STR_REF, FUNC,	XL_NS_CHART,	"f",	GSF_XML_CONTENT, NULL, &xlsx_chart_ser_f),
              GSF_XML_IN_NODE (STR_REF, STR_CACHE, XL_NS_CHART,	"strCache", GSF_XML_NO_CONTENT, NULL, NULL),
                GSF_XML_IN_NODE (STR_CACHE, STR_CACHE_COUNT, XL_NS_CHART,"ptCount", GSF_XML_NO_CONTENT, NULL, NULL),
                GSF_XML_IN_NODE (STR_CACHE, STR_PT, XL_NS_CHART,"pt", GSF_XML_NO_CONTENT, NULL, NULL),
                  GSF_XML_IN_NODE (STR_PT, STR_VAL, XL_NS_CHART,"v", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (TITLE, TEXT_PR, XL_NS_CHART, "txPr", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */

      GSF_XML_IN_NODE_FULL (PLOTAREA, VAL_AXIS, XL_NS_CHART, "valAx", GSF_XML_NO_CONTENT, FALSE, TRUE,
			    &xlsx_axis_start, &xlsx_axis_end, XLSX_AXIS_VAL),
	GSF_XML_IN_NODE (VAL_AXIS, AXIS_AXID, XL_NS_CHART, "axId", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */
        GSF_XML_IN_NODE (VAL_AXIS, AXIS_DELETE, XL_NS_CHART, "delete", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */
        GSF_XML_IN_NODE (VAL_AXIS, TITLE, XL_NS_CHART, "title", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */
        GSF_XML_IN_NODE (VAL_AXIS, AXIS_SCALING, XL_NS_CHART, "scaling", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
	GSF_XML_IN_NODE (VAL_AXIS, AXIS_POS, XL_NS_CHART, "axPos", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */
        GSF_XML_IN_NODE (VAL_AXIS, MAJOR_GRID, XL_NS_CHART, "majorGridlines", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
        GSF_XML_IN_NODE (VAL_AXIS, MINOR_GRID, XL_NS_CHART, "minorGridlines", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
	GSF_XML_IN_NODE (VAL_AXIS, AXIS_NUMFMT, XL_NS_CHART, "numFmt", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */
	GSF_XML_IN_NODE (VAL_AXIS, AXIS_MAJORTICKMARK, XL_NS_CHART, "majorTickMark", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
	GSF_XML_IN_NODE (VAL_AXIS, AXIS_MINORTICKMARK, XL_NS_CHART, "minorTickMark", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (VAL_AXIS, AXIS_MAJORTICK_UNIT, XL_NS_CHART, "majorUnit", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (VAL_AXIS, AXIS_MINORTICK_UNIT, XL_NS_CHART, "minorUnit", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (VAL_AXIS, VAL_AXIS_TICKLBLPOS, XL_NS_CHART, "tickLblPos", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
	GSF_XML_IN_NODE (VAL_AXIS, AXIS_CROSSAX, XL_NS_CHART, "crossAx", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
        GSF_XML_IN_NODE (VAL_AXIS, AXIS_CROSSES, XL_NS_CHART, "crosses", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
        GSF_XML_IN_NODE (VAL_AXIS, AXIS_CROSSES_AT, XL_NS_CHART, "crossesAt", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
        GSF_XML_IN_NODE (VAL_AXIS, VAL_AXIS_CROSSBETWEEN, XL_NS_CHART, "crossBetween", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (VAL_AXIS, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
        GSF_XML_IN_NODE (VAL_AXIS, TEXT_PR, XL_NS_CHART, "txPr", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */

      GSF_XML_IN_NODE_FULL (PLOTAREA, DATE_AXIS, XL_NS_CHART, "dateAx", GSF_XML_NO_CONTENT, FALSE, TRUE,
                            &xlsx_axis_start, &xlsx_axis_end, XLSX_AXIS_DATE),
        GSF_XML_IN_NODE (DATE_AXIS, AXIS_AUTO, XL_NS_CHART, "auto", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */
        GSF_XML_IN_NODE (DATE_AXIS, AXIS_AXID, XL_NS_CHART, "axId", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */
        GSF_XML_IN_NODE (DATE_AXIS, AXIS_POS, XL_NS_CHART, "axPos", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */
        GSF_XML_IN_NODE (DATE_AXIS, AXIS_CROSSAX, XL_NS_CHART, "crossAx", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (DATE_AXIS, AXIS_CROSSES, XL_NS_CHART, "crosses", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (DATE_AXIS, AXIS_CROSSES_AT, XL_NS_CHART, "crossesAt", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (DATE_AXIS, AXIS_DELETE, XL_NS_CHART, "delete", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (DATE_AXIS, AXIS_SCALING, XL_NS_CHART, "scaling", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
        GSF_XML_IN_NODE (DATE_AXIS, MAJOR_GRID, XL_NS_CHART, "majorGridlines", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
        GSF_XML_IN_NODE (DATE_AXIS, MINOR_GRID, XL_NS_CHART, "minorGridlines", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
	GSF_XML_IN_NODE (DATE_AXIS, AXIS_NUMFMT, XL_NS_CHART, "numFmt", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */
        GSF_XML_IN_NODE (DATE_AXIS, AXIS_MAJORTICKMARK, XL_NS_CHART, "majorTickMark", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
        GSF_XML_IN_NODE (DATE_AXIS, VAL_AXIS_TICKLBLPOS, XL_NS_CHART, "tickLblPos", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
        GSF_XML_IN_NODE (DATE_AXIS, CAT_AXIS_LBLOFFSET, XL_NS_CHART, "lblOffset", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (DATE_AXIS, VAL_AXIS_TICKLBLPOS, XL_NS_CHART, "tickLblPos", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
	GSF_XML_IN_NODE (DATE_AXIS, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
        GSF_XML_IN_NODE (DATE_AXIS, TEXT_PR, XL_NS_CHART, "txPr", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */

      GSF_XML_IN_NODE (PLOTAREA, LAYOUT, XL_NS_CHART, "layout", GSF_XML_NO_CONTENT, NULL, NULL),
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
        GSF_XML_IN_NODE (LAYOUT, MAN_LAYOUT, XL_NS_CHART, "manualLayout", GSF_XML_NO_CONTENT, NULL, NULL),

      GSF_XML_IN_NODE (PLOTAREA, SCATTER, XL_NS_CHART,	"scatterChart", GSF_XML_NO_CONTENT, xlsx_chart_xy, &xlsx_plot_end),
        GSF_XML_IN_NODE (SCATTER, VARYCOLORS, XL_NS_CHART,	"varyColors", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (SCATTER, SCATTER_DLBLS, XL_NS_CHART,	"dLbls", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (SCATTER_DLBLS, SCATTER_DLBLS_LEGEND, XL_NS_CHART,	"showLegendKey", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (SCATTER_DLBLS, SCATTER_DLBLS_VAL, XL_NS_CHART,	"showVal", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (SCATTER_DLBLS, SCATTER_DLBLS_CAT_NAME, XL_NS_CHART,	"showCatName", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (SCATTER_DLBLS, SCATTER_DLBLS_SERIES_NAME, XL_NS_CHART,	"showSerName", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (SCATTER_DLBLS, SCATTER_DLBLS_PERCENT, XL_NS_CHART,	"showPercent", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (SCATTER_DLBLS, SCATTER_DLBLS_BUBBLE, XL_NS_CHART,	"showBubbleSize", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (SCATTER, SCATTER_STYLE, XL_NS_CHART,	"scatterStyle", GSF_XML_NO_CONTENT, &xlsx_scatter_style, NULL),
        GSF_XML_IN_NODE (SCATTER, PLOT_AXIS_ID, XL_NS_CHART,           "axId", GSF_XML_NO_CONTENT, &xlsx_plot_axis_id, NULL),

        GSF_XML_IN_NODE (SCATTER, SERIES, XL_NS_CHART,	"ser", GSF_XML_NO_CONTENT, &xlsx_chart_ser_start, &xlsx_chart_ser_end),
          GSF_XML_IN_NODE (SERIES, SERIES_TRENDLINE, XL_NS_CHART,	"trendline", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (SERIES_TRENDLINE, SERIES_TRENDLINE_TYPE, XL_NS_CHART,	"trendlineType", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (SERIES_TRENDLINE, SERIES_TRENDLINE_RSQR, XL_NS_CHART,	"dispRSqr", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (SERIES_TRENDLINE, SERIES_TRENDLINE_EQ, XL_NS_CHART,	"dispEq", GSF_XML_NO_CONTENT, NULL, NULL),
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
            GSF_XML_IN_NODE (SERIES_CAT, STR_REF, XL_NS_CHART,	"strRef", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
            GSF_XML_IN_NODE (SERIES_CAT, NUM_LIT, XL_NS_CHART,  "numLit", GSF_XML_NO_CONTENT, NULL, NULL),
              GSF_XML_IN_NODE (NUM_LIT, NUM_LIT_FMT, XL_NS_CHART,   "formatCode", GSF_XML_NO_CONTENT, NULL, NULL),
              GSF_XML_IN_NODE (NUM_LIT, NUM_LIT_COUNT, XL_NS_CHART, "ptCount", GSF_XML_NO_CONTENT, NULL, NULL),
              GSF_XML_IN_NODE (NUM_LIT, NUM_LIT_PT, XL_NS_CHART,     "pt", GSF_XML_NO_CONTENT, NULL, NULL),
                GSF_XML_IN_NODE (NUM_LIT_PT, NUM_LIT_PT_VAL, XL_NS_CHART,     "v", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (SERIES_CAT, NUM_REF, XL_NS_CHART,	"numRef", GSF_XML_NO_CONTENT, NULL, NULL),
              GSF_XML_IN_NODE (NUM_REF, FUNC, XL_NS_CHART,	"f", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */
              GSF_XML_IN_NODE (NUM_REF, NUM_CACHE, XL_NS_CHART,	"numCache", GSF_XML_NO_CONTENT, NULL, NULL),
                GSF_XML_IN_NODE (NUM_CACHE, NUM_CACHE_FMT, XL_NS_CHART,	 "formatCode", GSF_XML_NO_CONTENT, NULL, NULL),
                GSF_XML_IN_NODE (NUM_CACHE, NUM_CACHE_COUNT, XL_NS_CHART,"ptCount", GSF_XML_NO_CONTENT, NULL, NULL),
                GSF_XML_IN_NODE (NUM_CACHE, NUM_PT, XL_NS_CHART,"pt", GSF_XML_NO_CONTENT, NULL, NULL),
                  GSF_XML_IN_NODE (NUM_PT, NUM_VAL, XL_NS_CHART,"v", GSF_XML_NO_CONTENT, NULL, NULL),

          GSF_XML_IN_NODE_FULL (SERIES, SERIES_VAL, XL_NS_CHART,	"val", GSF_XML_NO_CONTENT, FALSE, TRUE,
			   &xlsx_ser_type_start, &xlsx_ser_type_end, GOG_MS_DIM_VALUES),
            GSF_XML_IN_NODE (SERIES_VAL, NUM_REF, XL_NS_CHART,	"numRef", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */

          GSF_XML_IN_NODE_FULL (SERIES, SERIES_X_VAL, XL_NS_CHART,	"xVal", GSF_XML_NO_CONTENT, FALSE, TRUE,
			   &xlsx_ser_type_start, &xlsx_ser_type_end, GOG_MS_DIM_CATEGORIES),
            GSF_XML_IN_NODE (SERIES_X_VAL, NUM_REF, XL_NS_CHART,	"numRef", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
            GSF_XML_IN_NODE (SERIES_X_VAL, NUM_LIT, XL_NS_CHART,	"numLit", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */

          GSF_XML_IN_NODE_FULL (SERIES, SERIES_Y_VAL, XL_NS_CHART,	"yVal", GSF_XML_NO_CONTENT, FALSE, TRUE,
			   &xlsx_ser_type_start, &xlsx_ser_type_end, GOG_MS_DIM_VALUES),
            GSF_XML_IN_NODE (SERIES_Y_VAL, NUM_REF, XL_NS_CHART,	"numRef", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
            GSF_XML_IN_NODE (SERIES_Y_VAL, NUM_LIT, XL_NS_CHART,	"numLit", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */

          GSF_XML_IN_NODE_FULL (SERIES, SERIES_BUBBLES, XL_NS_CHART,	"bubbleSize", GSF_XML_NO_CONTENT, FALSE, TRUE,
			   &xlsx_ser_type_start, &xlsx_ser_type_end, GOG_MS_DIM_BUBBLES),
            GSF_XML_IN_NODE (SERIES_BUBBLES, NUM_REF, XL_NS_CHART,	"numRef", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
            GSF_XML_IN_NODE (SERIES_BUBBLES, NUM_LIT, XL_NS_CHART,	"numLit", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */

          GSF_XML_IN_NODE (SERIES, TEXT, XL_NS_CHART,	"tx", GSF_XML_NO_CONTENT, NULL, NULL),				/* 2nd Def */

          GSF_XML_IN_NODE (SERIES, SERIES_BUBBLES_3D, XL_NS_CHART,	"bubble3D", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (SERIES, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */
	GSF_XML_IN_NODE (SERIES, SERIES_SMOOTH, XL_NS_CHART, "smooth", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (SERIES, SERIES_IDX, XL_NS_CHART,	"idx", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (SERIES, SERIES_D_LBLS, XL_NS_CHART,	"dLbls", GSF_XML_NO_CONTENT, &xlsx_ser_labels_start, &xlsx_ser_labels_end),
            GSF_XML_IN_NODE (SERIES_D_LBLS, SERIES_D_LBLS_POS, XL_NS_CHART,	"dLblPos", GSF_XML_NO_CONTENT, &xlsx_ser_labels_pos, NULL),
            GSF_XML_IN_NODE (SERIES_D_LBLS, SERIES_D_LBL, XL_NS_CHART,	"dLbl", GSF_XML_NO_CONTENT, &xlsx_data_label_start, &xlsx_chart_pop),
              GSF_XML_IN_NODE (SERIES_D_LBL, SERIES_D_LBL_POS, XL_NS_CHART,	"dLblPos", GSF_XML_NO_CONTENT, &xlsx_data_label_pos, NULL),
              GSF_XML_IN_NODE (SERIES_D_LBL, SERIES_D_LBL_IDX, XL_NS_CHART,	"idx", GSF_XML_NO_CONTENT, &xlsx_data_label_index, NULL),
              GSF_XML_IN_NODE (SERIES_D_LBL, SERIES_D_LBL_LAYOUT, XL_NS_CHART,	"layout", GSF_XML_NO_CONTENT, NULL, NULL),
              GSF_XML_IN_NODE (SERIES_D_LBL, SERIES_D_LBL_SHOW_VAL, XL_NS_CHART,	"showVal", GSF_XML_NO_CONTENT, &xlsx_data_label_show_val, NULL),
              GSF_XML_IN_NODE (SERIES_D_LBL, SERIES_D_LBL_SHOW_CAT, XL_NS_CHART,	"showCatName", GSF_XML_NO_CONTENT, &xlsx_data_label_show_cat, NULL),
              GSF_XML_IN_NODE (SERIES_D_LBL, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */
              GSF_XML_IN_NODE (SERIES_D_LBL, TEXT, XL_NS_CHART,	"tx", GSF_XML_NO_CONTENT, NULL, NULL),
              GSF_XML_IN_NODE (SERIES_D_LBL, TEXT_PR, XL_NS_CHART, "txPr", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
	  GSF_XML_IN_NODE (SERIES_D_LBLS, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
	    GSF_XML_IN_NODE (SERIES_D_LBLS, TEXT_PR, XL_NS_CHART, "txPr", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
	    GSF_XML_IN_NODE (SERIES_D_LBLS, SHOW_VAL, XL_NS_CHART, "showVal", GSF_XML_NO_CONTENT, &xlsx_ser_labels_show_val, NULL),
	    GSF_XML_IN_NODE (SERIES_D_LBLS, NUM_FMT, XL_NS_CHART, "numFmt", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (SERIES_D_LBLS, SHOW_BUBBLE, XL_NS_CHART, "showBubbleSize", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (SERIES_D_LBLS, SHOW_CAT_NAME, XL_NS_CHART, "showCatName", GSF_XML_NO_CONTENT, &xlsx_ser_labels_show_cat, NULL),
	    GSF_XML_IN_NODE (SERIES_D_LBLS, SHOW_LEADERS, XL_NS_CHART, "showLeaderLines", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (SERIES_D_LBLS, SHOW_PERCENT, XL_NS_CHART, "showPercent", GSF_XML_NO_CONTENT, NULL, NULL),

          GSF_XML_IN_NODE (SERIES, SERIES_PT, XL_NS_CHART,	"dPt", GSF_XML_NO_CONTENT, &xlsx_chart_pt_start, &xlsx_chart_pt_end),
            GSF_XML_IN_NODE (SERIES_PT, PT_IDX, XL_NS_CHART,	"idx", GSF_XML_NO_CONTENT, &xlsx_chart_pt_index, NULL),
            GSF_XML_IN_NODE (SERIES_PT, SHAPE_PR, XL_NS_CHART,	"spPr", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (SERIES_PT, PT_SEP, XL_NS_CHART,	"explosion", GSF_XML_NO_CONTENT, &xlsx_chart_pt_sep, NULL),
            GSF_XML_IN_NODE (SERIES_PT, MARKER, XL_NS_CHART,	"marker", GSF_XML_NO_CONTENT, &xlsx_chart_marker_start, &xlsx_chart_marker_end),
              GSF_XML_IN_NODE (MARKER, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */
              GSF_XML_IN_NODE (MARKER, MARKER_SYMBOL, XL_NS_CHART, "symbol", GSF_XML_NO_CONTENT, &xlsx_chart_marker_symbol, NULL),
              GSF_XML_IN_NODE (MARKER, MARKER_SIZE, XL_NS_CHART, "size", GSF_XML_NO_CONTENT, NULL, NULL),

          GSF_XML_IN_NODE (SERIES, SERIES_ERR_BARS, XL_NS_CHART,"errBars", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (SERIES_ERR_BARS, SERIES_ERR_BARS_ERRBARTYPE, XL_NS_CHART, "errBarType",  GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (SERIES_ERR_BARS, SERIES_ERR_BARS_ERRDIR, XL_NS_CHART, "errDir", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (SERIES_ERR_BARS, SERIES_ERR_BARS_ERRVALTYPE, XL_NS_CHART, "errValType", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (SERIES_ERR_BARS, SERIES_ERR_BARS_MINUS, XL_NS_CHART, "minus", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (SERIES_ERR_BARS_MINUS, NUM_REF, XL_NS_CHART, "numRef", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
              GSF_XML_IN_NODE (SERIES_ERR_BARS_MINUS, NUM_LIT, XL_NS_CHART, "numLit", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
	    GSF_XML_IN_NODE (SERIES_ERR_BARS, SERIES_ERR_BARS_PLUS, XL_NS_CHART, "plus", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (SERIES_ERR_BARS_PLUS, NUM_REF, XL_NS_CHART, "numRef", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
              GSF_XML_IN_NODE (SERIES_ERR_BARS_PLUS, NUM_LIT, XL_NS_CHART, "numLit", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
	    GSF_XML_IN_NODE (SERIES_ERR_BARS, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */

      GSF_XML_IN_NODE (PLOTAREA, BUBBLE, XL_NS_CHART,	"bubbleChart", GSF_XML_NO_CONTENT, &xlsx_chart_bubble, &xlsx_plot_end),
        GSF_XML_IN_NODE (BUBBLE, PLOT_AXIS_ID, XL_NS_CHART,	"axId", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
        GSF_XML_IN_NODE (BUBBLE, SERIES, XL_NS_CHART,		"ser", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (BUBBLE, BUBBLE_SCALE, XL_NS_CHART,	"bubbleScale", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (BUBBLE, BUBBLE_NEGATIVES, XL_NS_CHART,	"showNegBubbles", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (BUBBLE, BUBBLE_SIZE_REP, XL_NS_CHART,	"sizeRepresents", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (BUBBLE, VARY_COLORS, XL_NS_CHART,	"varyColors", GSF_XML_NO_CONTENT, &xlsx_vary_colors, NULL),

      GSF_XML_IN_NODE (PLOTAREA, BARCOL, XL_NS_CHART,	"barChart", GSF_XML_NO_CONTENT, &xlsx_chart_bar, &xlsx_plot_end),
        GSF_XML_IN_NODE (BARCOL, PLOT_AXIS_ID,	XL_NS_CHART, "axId", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */
        GSF_XML_IN_NODE (BARCOL, SERIES,	XL_NS_CHART, "ser", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */
        GSF_XML_IN_NODE (BARCOL, BARCOL_DIR,	XL_NS_CHART, "barDir", GSF_XML_NO_CONTENT, &xlsx_chart_bar_dir, NULL),
        GSF_XML_IN_NODE (BARCOL, BARCOL_OVERLAP, XL_NS_CHART,"overlap", GSF_XML_NO_CONTENT, &xlsx_chart_bar_overlap, NULL),
        GSF_XML_IN_NODE (BARCOL, GROUPING,	XL_NS_CHART, "grouping", GSF_XML_NO_CONTENT, &xlsx_chart_bar_group, NULL),
        GSF_XML_IN_NODE (BARCOL, GAP_WIDTH,	XL_NS_CHART, "gapWidth", GSF_XML_NO_CONTENT, &xlsx_chart_bar_gap, NULL),

      GSF_XML_IN_NODE (PLOTAREA, LINE, XL_NS_CHART,	"lineChart", GSF_XML_NO_CONTENT, &xlsx_chart_line, &xlsx_plot_end),
        GSF_XML_IN_NODE (LINE, PLOT_AXIS_ID, XL_NS_CHART,"axId", GSF_XML_NO_CONTENT, NULL, NULL),				/* 2nd Def */
        GSF_XML_IN_NODE (LINE, SERIES, XL_NS_CHART,	"ser", GSF_XML_NO_CONTENT, NULL, NULL),					/* 2nd Def */
          GSF_XML_IN_NODE (SERIES, MARKER, XL_NS_CHART,	"marker", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (MARKER, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_NO_CONTENT, NULL, NULL),				/* 2nd Def */
            GSF_XML_IN_NODE (MARKER, MARKER_SYMBOL, XL_NS_CHART, "symbol", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (MARKER, MARKER_SIZE, XL_NS_CHART, "size", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (LINE, PLOT_AXIS_ID, XL_NS_CHART,"axId", GSF_XML_NO_CONTENT, NULL, NULL),				/* 2nd Def */
        GSF_XML_IN_NODE (LINE, GROUPING, XL_NS_CHART,	"grouping", GSF_XML_NO_CONTENT, NULL, NULL),				/* 2nd Def */
        GSF_XML_IN_NODE (LINE, MARKER, XL_NS_CHART,	"marker", GSF_XML_NO_CONTENT, NULL, NULL),				/* 2nd Def */

      GSF_XML_IN_NODE (PLOTAREA, AREA, XL_NS_CHART,	"areaChart", GSF_XML_NO_CONTENT, &xlsx_chart_area, &xlsx_plot_end),
        GSF_XML_IN_NODE (AREA, PLOT_AXIS_ID, XL_NS_CHART,"axId", GSF_XML_NO_CONTENT, NULL, NULL),				/* 2nd Def */
        GSF_XML_IN_NODE (AREA, SERIES, XL_NS_CHART,	"ser", GSF_XML_NO_CONTENT, NULL, NULL),					/* 2nd Def */
        GSF_XML_IN_NODE (AREA, GROUPING, XL_NS_CHART,	"grouping", GSF_XML_NO_CONTENT, NULL, NULL),				/* 2nd Def */

      GSF_XML_IN_NODE (PLOTAREA, RADAR, XL_NS_CHART,	"radarChart", GSF_XML_NO_CONTENT, &xlsx_chart_radar, &xlsx_plot_end),
        GSF_XML_IN_NODE (RADAR, PLOT_AXIS_ID, XL_NS_CHART,  "axId", GSF_XML_NO_CONTENT, NULL, NULL),				/* 2nd Def */
        GSF_XML_IN_NODE (RADAR, SERIES, XL_NS_CHART,	  "ser", GSF_XML_NO_CONTENT, NULL, NULL),				/* 2nd Def */
        GSF_XML_IN_NODE (RADAR, RADAR_STYLE, XL_NS_CHART, "radarStyle", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (RADAR, VARY_COLORS, XL_NS_CHART, "varyColors", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */

      GSF_XML_IN_NODE (PLOTAREA, PIE, XL_NS_CHART,	"pieChart", GSF_XML_NO_CONTENT, &xlsx_chart_pie, &xlsx_plot_end),
        GSF_XML_IN_NODE (PIE, SERIES, XL_NS_CHART,	"ser", GSF_XML_NO_CONTENT, NULL, NULL),					/* 2nd Def */
          GSF_XML_IN_NODE (SERIES, PIE_SER_SEP, XL_NS_CHART,	"explosion", GSF_XML_NO_CONTENT, &xlsx_chart_pie_sep, NULL),
        GSF_XML_IN_NODE (PIE, VARY_COLORS, XL_NS_CHART,	"varyColors", GSF_XML_NO_CONTENT, NULL, NULL),				/* 2nd Def */
        GSF_XML_IN_NODE (PIE, PIE_FIRST_SLICE, XL_NS_CHART,	"firstSliceAng", GSF_XML_NO_CONTENT, NULL, NULL),

      GSF_XML_IN_NODE (PLOTAREA, OF_PIE, XL_NS_CHART,	"ofPieChart", GSF_XML_NO_CONTENT, &xlsx_chart_pie, &xlsx_plot_end),
        GSF_XML_IN_NODE (OF_PIE, OF_PIE_TYPE,	XL_NS_CHART, "ofPieType", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (OF_PIE, SERIES,	XL_NS_CHART, "ser", GSF_XML_NO_CONTENT, NULL, NULL),				/* 2nd Def */
        GSF_XML_IN_NODE (OF_PIE, SERIES_LINES,	XL_NS_CHART, "serLines", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (SERIES_LINES, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */
        GSF_XML_IN_NODE (OF_PIE, PIE_GAP_WIDTH,	XL_NS_CHART, "gapWidth", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (OF_PIE, VARY_COLORS,	XL_NS_CHART, "varyColors", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */
        GSF_XML_IN_NODE (OF_PIE, OF_2ND_PIE,	XL_NS_CHART, "secondPieSize", GSF_XML_NO_CONTENT, NULL, NULL),

      GSF_XML_IN_NODE (PLOTAREA, DOUGHNUT, XL_NS_CHART,	"doughnutChart", GSF_XML_NO_CONTENT, &xlsx_chart_ring, &xlsx_plot_end),
        GSF_XML_IN_NODE (DOUGHNUT, SERIES, XL_NS_CHART,	"ser", GSF_XML_NO_CONTENT, NULL, NULL),					/* 2nd Def */
        GSF_XML_IN_NODE (DOUGHNUT, VARY_COLORS, XL_NS_CHART,	"varyColors", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */
        GSF_XML_IN_NODE (DOUGHNUT, PIE_FIRST_SLICE, XL_NS_CHART,	"firstSliceAng", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
        GSF_XML_IN_NODE (DOUGHNUT, HOLE_SIZE, XL_NS_CHART,		"holeSize", GSF_XML_NO_CONTENT, NULL, NULL),

      GSF_XML_IN_NODE (PLOTAREA, DATA_TABLE, XL_NS_CHART, "dTable", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (DATA_TABLE, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
        GSF_XML_IN_NODE (DATA_TABLE, TEXT_PR, XL_NS_CHART,  "txPr", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
        GSF_XML_IN_NODE (DATA_TABLE, DT_SHOW_H_BORDER, XL_NS_CHART, "showHorzBorder", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (DATA_TABLE, DT_SHOW_V_BORDER, XL_NS_CHART, "showVertBorder", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (DATA_TABLE, DT_SHOW_KEYS, XL_NS_CHART, "showKeys", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (DATA_TABLE, DT_SHOW_OUTLINE, XL_NS_CHART, "showOutline", GSF_XML_NO_CONTENT, NULL, NULL),

    GSF_XML_IN_NODE (CHART, TITLE, XL_NS_CHART, "title", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */

    GSF_XML_IN_NODE (CHART, LEGEND, XL_NS_CHART, "legend", GSF_XML_NO_CONTENT, &xlsx_chart_legend, &xlsx_chart_pop),
      GSF_XML_IN_NODE (LEGEND, OVERLAY, XL_NS_CHART, "overlay", GSF_XML_NO_CONTENT, NULL, NULL), /* 2nd Def */
      GSF_XML_IN_NODE (LEGEND, SHAPE_PR, XL_NS_CHART, "spPr", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
      GSF_XML_IN_NODE (LEGEND, TEXT_PR, XL_NS_CHART, "txPr", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
      GSF_XML_IN_NODE (LEGEND, LAYOUT, XL_NS_CHART, "layout", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
      GSF_XML_IN_NODE (LEGEND, LEGEND_POS, XL_NS_CHART, "legendPos", GSF_XML_NO_CONTENT, &xlsx_chart_legend_pos, NULL),
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

static void
cb_axis_set_position (GObject *axis, XLSXAxisInfo *info,
		      XLSXReadState *state)
{
	GogObject *obj = NULL;
	if (info->cross_id) {
		XLSXAxisInfo *cross_info = g_hash_table_lookup (state->axis.by_id, info->cross_id);
		g_return_if_fail (cross_info != NULL);
		obj = GOG_OBJECT (cross_info->axis);
		if (go_finite (cross_info->cross_value)) {
			GnmValue *value = value_new_float (cross_info->cross_value);
			GnmExprTop const *texpr = gnm_expr_top_new_constant (value);
			gog_dataset_set_dim (GOG_DATASET (obj), GOG_AXIS_ELEM_CROSS_POINT,
				gnm_go_data_scalar_new_expr (state->sheet, texpr), NULL);
		}
		if (gog_axis_is_inverted (GOG_AXIS (axis)))
			cross_info->cross = 2 - cross_info->cross; /* KLUDGE */
		g_object_set (obj, "pos", cross_info->cross, "cross-axis-id", gog_object_get_id (GOG_OBJECT (axis)), NULL);
	}
	if (info->deleted) {
		GSList *l = gog_chart_get_axes (state->chart, gog_axis_get_atype (GOG_AXIS (axis))), *cur;
		GogAxis *visible = NULL;

		for (cur = l; cur; cur = cur->next) {
			gboolean invisible;
			g_object_get (cur->data, "invisible", &invisible, NULL);
			if (!invisible) {
				visible = GOG_AXIS (cur->data);
				break;
			}
		}
		if (visible) {
			GSList *l1, *cur1;

			if (obj)
				g_object_set (obj, "cross-axis-id", gog_object_get_id (GOG_OBJECT (visible)), NULL);
			l1 = g_slist_copy ((GSList *) gog_axis_contributors (GOG_AXIS (axis)));
			for (cur1 = l1; cur1; cur1 = cur1->next) {
				if (GOG_IS_PLOT (cur1->data))
					gog_plot_set_axis (GOG_PLOT (cur1->data), visible);
			}
			g_slist_free (l1);
			/* now reparent the children of the hidden axis */
			l1 = gog_object_get_children (GOG_OBJECT (axis), NULL);
			for (cur1 = l1; cur1; cur1 = cur1->next) {
				GogObject *obj = GOG_OBJECT (cur1->data);
				GogObjectRole const *role = obj->role;
				gog_object_clear_parent (obj);
				gog_object_set_parent (obj, GOG_OBJECT (visible), role, obj->id);
			}
			g_slist_free (l1);
		}
	}
}

static void
xlsx_axis_cleanup (XLSXReadState *state)
{
	GSList *list, *ptr;

	/* clean out axis that were auto created */
	list = gog_object_get_children (GOG_OBJECT (state->chart), NULL);
	for (ptr = list; ptr != NULL ; ptr = ptr->next)
		if (GOG_IS_AXIS (ptr->data) &&
		    NULL == g_hash_table_lookup (state->axis.by_obj, ptr->data)) {
			if (gog_object_is_deletable (GOG_OBJECT (ptr->data))) {
				gog_object_clear_parent	(GOG_OBJECT (ptr->data));
				g_object_unref (ptr->data);
			}
		}
	g_slist_free (list);

	g_hash_table_foreach (state->axis.by_obj,
		(GHFunc)cb_axis_set_position, state);
	g_hash_table_destroy (state->axis.by_obj);
	g_hash_table_destroy (state->axis.by_id);
	state->axis.by_obj = state->axis.by_id = NULL;
}

static void
xlsx_read_chart (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
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
		xlsx_parse_rel_by_id (xin, part_id, xlsx_chart_dtd, xlsx_ns);

		if (NULL != state->obj_stack) {
			g_warning ("left over content on chart object stack");
			g_slist_free (state->obj_stack);
			state->obj_stack = NULL;
		}

		xlsx_axis_cleanup (state);
		g_object_set (state->chart, "style", state->cur_style, NULL);
		g_object_unref (state->cur_style);
		state->cur_style = NULL;
		if (NULL != state->style_stack) {
			g_warning ("left over style");
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
						GogDataset *ds = plot? GOG_DATASET (gog_plot_get_series  (plot)->data): NULL;
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
		state->gocolor = NULL;
		state->color_setter = NULL;
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
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;

	g_return_if_fail (state->so == NULL);

	memset ((gpointer)state->drawing_pos, 0, sizeof (state->drawing_pos));
	state->drawing_pos_flags = 0;
}

static void
xlsx_drawing_twoCellAnchor_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;

	if (NULL == state->so) {
		xlsx_warning (xin,
			_("Dropping missing object"));
	} else {
		if ((state->drawing_pos_flags & 0xFF) == 0xFF) {
			SheetObjectAnchor anchor;
			GnmRange r;
			double coords[4];
			double size;
			int i;

			range_init (&r,
				state->drawing_pos[COL | FROM],
				state->drawing_pos[ROW | FROM],
				state->drawing_pos[COL | TO],
				state->drawing_pos[ROW | TO]);

			for (i = 0; i < 8; i+=2) {
				ColRowInfo const *cri;
				if (i & 2) {
					cri = sheet_row_get (state->sheet, state->drawing_pos[i]);
					size = cri? cri->size_pts: sheet_row_get_default_size_pts (state->sheet);
				} else {
					cri = sheet_col_get (state->sheet, state->drawing_pos[i]);
					/* FIXME: scaling horizontally just like in xlsx_CT_Col */
					size = cri? cri->size_pts: sheet_col_get_default_size_pts (state->sheet) * 1.16191275167785;
				}
				coords[i / 2] = (double) state->drawing_pos[i + 1] / 12700. / size;
			}
			sheet_object_anchor_init (&anchor, &r, coords, GOD_ANCHOR_DIR_DOWN_RIGHT);
			sheet_object_set_anchor (state->so, &anchor);
			sheet_object_set_sheet (state->so, state->sheet);
		} else
			xlsx_warning (xin,
				_("Dropping object with incomplete anchor %2x"), state->drawing_pos_flags);

		if (state->cur_style) {
			g_object_set (state->so, "style", state->cur_style, NULL);
			g_object_unref (state->cur_style);
			state->cur_style = NULL;
		}
		g_object_unref (state->so);
		state->so = NULL;
	}
}

static void
xlsx_drawing_oneCellAnchor_end (GsfXMLIn *xin, GsfXMLBlob *blob)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;

	state->drawing_pos[COL | TO] = state->drawing_pos[COL | FROM] + 5;
	state->drawing_pos[ROW | TO] = state->drawing_pos[ROW | FROM] + 5;
	state->drawing_pos_flags |= ((1 << (COL | TO)) | (1 << (ROW | TO)));
	xlsx_drawing_twoCellAnchor_end (xin, blob);
}

static void
xlsx_drawing_ext (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_int64 (xin, attrs, "cx", state->drawing_pos + (COL | TO | OFFSET)))
			state->drawing_pos_flags |= (1 << (COL | TO | OFFSET));
		else if (attr_int64 (xin, attrs, "cy", state->drawing_pos + (ROW | TO | OFFSET)))
			state->drawing_pos_flags |= (1 << (ROW | TO | OFFSET));
}

static void
xlsx_drawing_pos (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
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
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	if (NULL != state->so) /* FIXME FIXME FIXME: how does this happen? */
		return;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (!strcmp (attrs[0], "prst")) {
			/* TODO, use a hash here for all preset geometries */
			if (!strcmp (attrs[1], "rect"))
				state->so = g_object_new (GNM_SO_FILLED_TYPE, "is_oval", FALSE, NULL);
			else if (!strcmp (attrs[1], "line"))
				state->so = g_object_new (GNM_SO_LINE_TYPE, NULL);
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
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	state->so = g_object_new (SHEET_OBJECT_IMAGE_TYPE, NULL);
}

static void
xlsx_blip_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	g_return_if_fail (IS_SHEET_OBJECT_IMAGE (state->so));
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (!strcmp (attrs[0], "r:embed")) {
			GsfOpenPkgRel const *rel = gsf_open_pkg_lookup_rel_by_id (
				gsf_xml_in_get_input (xin), attrs[1]);
			GsfInput *input = gsf_open_pkg_open_rel (
			        gsf_xml_in_get_input (xin), rel, NULL);
			size_t size;
			guint8 *data;

			g_return_if_fail (input != NULL);
			size = gsf_input_size (input);
			data = g_new (guint8, size);
			gsf_input_read (input, size, data);
			sheet_object_image_set_image (SHEET_OBJECT_IMAGE (state->so),
				NULL, data, size, FALSE);
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
	    GSF_XML_IN_NODE (LN_REF, SCRGB_CLR, XL_NS_DRAW, "scrgbClr", GSF_XML_NO_CONTENT, xlsx_draw_color_rgb, xlsx_draw_color_end),
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
        GSF_XML_IN_NODE (SHAPE_PR, SP_PR_XFRM, XL_NS_DRAW, "xfrm", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (SP_PR_XFRM, SP_XFRM_STYLE, XL_NS_SS_DRAW, "style", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (SP_PR_XFRM, SP_XFRM_OFF, XL_NS_DRAW, "off", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (SP_XFRM_OFF, SP_PR_PRST_GEOM, XL_NS_DRAW, "prstGeom", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (SP_XFRM_OFF, SHAPE_PR_LN, XL_NS_DRAW, "ln", GSF_XML_NO_CONTENT, xlsx_style_line_start, &xlsx_style_line_end),
	      GSF_XML_IN_NODE (SHAPE_PR_LN, LN_NOFILL, XL_NS_DRAW, "noFill", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (SHAPE_PR_LN, LN_DASH, XL_NS_DRAW, "prstDash", GSF_XML_NO_CONTENT, &xlsx_draw_line_dash, NULL),		/* 2nd Def */
	      GSF_XML_IN_NODE (SHAPE_PR_LN, FILL_SOLID, XL_NS_DRAW, "solidFill", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
	      GSF_XML_IN_NODE (SHAPE_PR_LN, FILL_PATT,	XL_NS_DRAW, "pattFill", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
	      GSF_XML_IN_NODE (SHAPE_PR_LN, LN_MITER,	XL_NS_DRAW, "miter", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (SHAPE_PR_LN, LN_ROUND,	XL_NS_DRAW, "round", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (SHAPE_PR_LN, LN_HEAD,	XL_NS_DRAW, "headEnd", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (SHAPE_PR_LN, LN_TAIL,	XL_NS_DRAW, "tailEnd", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (SP_PR_XFRM, CHILD_OFF, XL_NS_DRAW, "chOff", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (SP_PR_XFRM, CHILD_EXT, XL_NS_DRAW, "chExt", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (SHAPE_PR, SP_FILL_NONE,	XL_NS_DRAW, "noFill", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (SHAPE_PR, SP_FILL_SOLID,	XL_NS_DRAW, "solidFill", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (FILL_SOLID, COLOR_THEMED, XL_NS_DRAW, "schemeClr", GSF_XML_NO_CONTENT, &xlsx_draw_color_themed, &xlsx_draw_color_end),
	    GSF_XML_IN_NODE (COLOR_THEMED, COLOR_LUM, XL_NS_DRAW, "lumMod", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (FILL_SOLID, COLOR_RGB,	 XL_NS_DRAW, "srgbClr", GSF_XML_NO_CONTENT, &xlsx_draw_color_rgb, &xlsx_draw_color_end),
	    GSF_XML_IN_NODE (COLOR_RGB, RGB_ALPHA,	   XL_NS_DRAW, "alpha", GSF_XML_NO_CONTENT, &xlsx_draw_color_alpha, NULL),
	    GSF_XML_IN_NODE (COLOR_RGB, RGB_GAMMA,	   XL_NS_DRAW, "gamma", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (COLOR_RGB, RGB_INV_GAMMA, XL_NS_DRAW, "invGamma", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (COLOR_RGB, RGB_SHADE,	   XL_NS_DRAW, "shade", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (COLOR_RGB, RGB_TINT,	   XL_NS_DRAW, "tint", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (COLOR_RGB, LN_DASH,	   XL_NS_DRAW, "prstDash", GSF_XML_NO_CONTENT, NULL, NULL),

	GSF_XML_IN_NODE (SHAPE_PR, FILL_BLIP,	XL_NS_DRAW, "blipFill", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (FILL_BLIP, FILL_BLIP_BLIP,	XL_NS_DRAW, "blip", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (FILL_BLIP, FILL_BLIP_SRC,	XL_NS_DRAW, "srcRect", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (FILL_BLIP, FILL_BLIP_TILE,	XL_NS_DRAW, "tile", GSF_XML_NO_CONTENT, NULL, NULL),

	GSF_XML_IN_NODE (SHAPE_PR, FILL_GRAD,	XL_NS_DRAW, "gradFill", GSF_XML_NO_CONTENT, xlsx_chart_grad_fill, NULL),
	  GSF_XML_IN_NODE (FILL_GRAD, GRAD_LIST,	XL_NS_DRAW, "gsLst", GSF_XML_NO_CONTENT, NULL, NULL),
	   GSF_XML_IN_NODE (GRAD_LIST, GRAD_LIST_ITEM, XL_NS_DRAW, "gs", GSF_XML_NO_CONTENT, xlsx_chart_grad_stop, NULL),
	     GSF_XML_IN_NODE (GRAD_LIST_ITEM, COLOR_RGB, XL_NS_DRAW, "srgbClr", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
	  GSF_XML_IN_NODE (FILL_GRAD, GRAD_LINE,	XL_NS_DRAW, "lin", GSF_XML_NO_CONTENT, NULL, NULL),

	GSF_XML_IN_NODE (SHAPE_PR, FILL_PATT,	XL_NS_DRAW, "pattFill", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (FILL_PATT, FILL_PATT_BG,	XL_NS_DRAW, "bgClr", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (FILL_PATT_BG, COLOR_RGB, XL_NS_DRAW, "srgbClr", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
	  GSF_XML_IN_NODE (FILL_PATT, FILL_PATT_FG,	XL_NS_DRAW, "fgClr", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (FILL_PATT_FG, COLOR_RGB, XL_NS_DRAW, "srgbClr", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */

	GSF_XML_IN_NODE (SHAPE_PR, SHAPE_PR_LN, XL_NS_DRAW, "ln", GSF_XML_NO_CONTENT, NULL, NULL),

	GSF_XML_IN_NODE (SHAPE_PR, TEXT_PR, XL_NS_CHART, "txPr", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (TEXT_PR, TEXT_PR_BODY,	XL_NS_DRAW, "bodyPr", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (TEXT_PR, TEXT_PR_STYLE,	XL_NS_DRAW, "lstStyle", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (TEXT_PR, TEXT_PR_P,	XL_NS_DRAW, "p", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */
	    GSF_XML_IN_NODE (TEXT_PR_P, TX_RICH_R, XL_NS_DRAW, "r", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (TX_RICH_R, TX_RICH_R_PR, XL_NS_DRAW, "rPr", GSF_XML_NO_CONTENT, NULL, NULL),
		GSF_XML_IN_NODE (TX_RICH_R_PR, PR_P_PR_DEF_CS, XL_NS_DRAW, "cs", GSF_XML_NO_CONTENT, NULL, NULL),
		GSF_XML_IN_NODE (TX_RICH_R_PR, PR_P_PR_DEF_EA, XL_NS_DRAW, "ea", GSF_XML_NO_CONTENT, NULL, NULL),
		GSF_XML_IN_NODE (TX_RICH_R_PR, PR_P_PR_DEF_LATIN, XL_NS_DRAW, "latin", GSF_XML_NO_CONTENT, NULL, NULL),
		GSF_XML_IN_NODE (TX_RICH_R_PR, TEXT_FILL_SOLID, XL_NS_DRAW, "solidFill", GSF_XML_NO_CONTENT, NULL, NULL),
		  GSF_XML_IN_NODE (TEXT_FILL_SOLID, COLOR_RGB, XL_NS_DRAW, "srgbClr", GSF_XML_NO_CONTENT, NULL, NULL),
		GSF_XML_IN_NODE (TX_RICH_R_PR, PR_P_PR_DEF_UFILLTX, XL_NS_DRAW, "uFillTx", GSF_XML_NO_CONTENT, NULL, NULL),
		GSF_XML_IN_NODE (TX_RICH_R_PR, PR_P_PR_DEF_ULNTX, XL_NS_DRAW, "uLnTx", GSF_XML_NO_CONTENT, NULL, NULL),

	      GSF_XML_IN_NODE (TX_RICH_R, TX_RICH_R_T, XL_NS_DRAW,  "t", GSF_XML_CONTENT, NULL,  &xlsx_chart_text_content),
	    GSF_XML_IN_NODE (TEXT_PR_P, PR_P_PR,	XL_NS_DRAW, "pPr", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (PR_P_PR, PR_P_PR_DEF, XL_NS_DRAW, "defRPr", GSF_XML_NO_CONTENT, NULL, NULL),
		GSF_XML_IN_NODE (PR_P_PR_DEF, PR_P_PR_DEF_CS, XL_NS_DRAW, "cs", GSF_XML_NO_CONTENT, NULL, NULL),
		GSF_XML_IN_NODE (PR_P_PR_DEF, PR_P_PR_DEF_EA, XL_NS_DRAW, "ea", GSF_XML_NO_CONTENT, NULL, NULL),
		GSF_XML_IN_NODE (PR_P_PR_DEF, PR_P_PR_DEF_LATIN, XL_NS_DRAW, "latin", GSF_XML_NO_CONTENT, NULL, NULL),
		GSF_XML_IN_NODE (PR_P_PR_DEF, FILL_SOLID, XL_NS_DRAW, "solidFill", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
		GSF_XML_IN_NODE (PR_P_PR_DEF, PR_P_PR_DEF_UFILLTX, XL_NS_DRAW, "uFillTx", GSF_XML_NO_CONTENT, NULL, NULL),
		GSF_XML_IN_NODE (PR_P_PR_DEF, PR_P_PR_DEF_ULNTX, XL_NS_DRAW, "uLnTx", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (TEXT_PR_P, PR_P_PR_END,XL_NS_DRAW, "endParaRPr", GSF_XML_NO_CONTENT, NULL, NULL),

        GSF_XML_IN_NODE (SHAPE_PR, CXN_SP, XL_NS_SS_DRAW, "cxnSp", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (CXN_SP, CXN_SP_PR, XL_NS_SS_DRAW, "nvCxnSpPr", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (CXN_SP_PR, C_NV_CXN_SP, XL_NS_SS_DRAW, "cNvCxnSpPr", GSF_XML_NO_CONTENT, NULL, NULL),
            GSF_XML_IN_NODE (CXN_SP_PR, C_NV_PR, XL_NS_SS_DRAW, "cNvPr", GSF_XML_NO_CONTENT, NULL, NULL),
              GSF_XML_IN_NODE (C_NV_PR, HLINK_CLICK, XL_NS_DRAW, "hlinkClick", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (CXN_SP, SHAPE_PR, XL_NS_SS_DRAW, "spPr", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (CXN_SP, SP_XFRM_STYLE, XL_NS_SS_DRAW, "style", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (SHAPE_PR, SP_PR_PRST_GEOM, XL_NS_DRAW, "prstGeom", GSF_XML_NO_CONTENT, NULL, NULL),

      GSF_XML_IN_NODE (SHAPE, TX_BODY, XL_NS_SS_DRAW, "txBody", GSF_XML_NO_CONTENT, &xlsx_chart_text_start, &xlsx_chart_text),
        GSF_XML_IN_NODE (TX_BODY, LST_STYLE, XL_NS_DRAW, "lstStyle", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */
        GSF_XML_IN_NODE (TX_BODY, TX_BODY_PR, XL_NS_DRAW, "bodyPr", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (TX_BODY, TEXT_PR_P,	XL_NS_DRAW, "p", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */

      GSF_XML_IN_NODE (SHAPE, NV_SP_PR, XL_NS_SS_DRAW, "nvSpPr", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (NV_SP_PR, C_NV_SP_PR, XL_NS_SS_DRAW, "cNvSpPr", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (C_NV_SP_PR, SP_LOCKS, XL_NS_DRAW, "spLocks", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (NV_SP_PR, C_NV_PR, XL_NS_SS_DRAW, "cNvPr", GSF_XML_NO_CONTENT, NULL, NULL),

	GSF_XML_IN_NODE (SP_PR_XFRM, SP_XFRM_EXT, XL_NS_DRAW, "ext", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (SP_XFRM_EXT, SP_PR_PRST_GEOM, XL_NS_DRAW, "prstGeom", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (SP_PR_PRST_GEOM, AV_LST, XL_NS_DRAW, "avLst", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (SHAPE_PR, FILL_NONE,	XL_NS_DRAW, "noFill", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (SHAPE_PR, FILL_SOLID,	XL_NS_DRAW, "solidFill", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (SHAPE_PR, FILL_BLIP,	XL_NS_DRAW, "blipFill", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (SHAPE_PR, FILL_GRAD,	XL_NS_DRAW, "gradFill", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (SHAPE_PR, FILL_PATT,	XL_NS_DRAW, "pattFill", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (SHAPE_PR, SHAPE_PR_LN, XL_NS_DRAW, "ln", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (SHAPE_PR, TEXT_PR, XL_NS_CHART, "txPr", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (SHAPE, TX_BODY, XL_NS_SS_DRAW, "txBody", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (TX_BODY, LST_STYLE, XL_NS_DRAW, "lstStyle", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */
        GSF_XML_IN_NODE (TX_BODY, TX_BODY_PR, XL_NS_DRAW, "bodyPr", GSF_XML_NO_CONTENT, NULL, NULL),
	GSF_XML_IN_NODE (TX_BODY, TEXT_PR_P,	XL_NS_DRAW, "p", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */

      GSF_XML_IN_NODE (SHAPE, NV_SP_PR, XL_NS_SS_DRAW, "nvSpPr", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (NV_SP_PR, C_NV_SP_PR, XL_NS_SS_DRAW, "cNvSpPr", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (C_NV_SP_PR, SP_LOCKS, XL_NS_DRAW, "spLocks", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (C_NV_SP_PR, HLINK_CLICK, XL_NS_DRAW, "hlinkClick", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (NV_SP_PR, C_NV_PR, XL_NS_SS_DRAW, "cNvPr", GSF_XML_NO_CONTENT, NULL, NULL),

    GSF_XML_IN_NODE (TWO_CELL, GRAPHIC_FRAME, XL_NS_SS_DRAW, "graphicFrame", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (GRAPHIC_FRAME, GRAPHIC_PR, XL_NS_SS_DRAW, "nvGraphicFramePr", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (GRAPHIC_PR, CNVPR, XL_NS_SS_DRAW, "cNvPr", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (GRAPHIC_PR, GRAPHIC_PR_CHILD, XL_NS_SS_DRAW, "cNvGraphicFramePr", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (GRAPHIC_PR_CHILD, GRAPHIC_LOCKS, XL_NS_DRAW, "graphicFrameLocks", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (GRAPHIC_FRAME, GRAPHIC, XL_NS_DRAW, "graphic", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE_FULL (GRAPHIC, GRAPHIC_DATA, XL_NS_DRAW, "graphicData",
			      GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
          GSF_XML_IN_NODE (GRAPHIC_DATA, CHART, XL_NS_CHART, "chart", GSF_XML_NO_CONTENT, &xlsx_read_chart, NULL),
          GSF_XML_IN_NODE (GRAPHIC_DATA, GRAPHIC_PR_CHILD, XL_NS_SS_DRAW, "cNvGraphicFramePr", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
      GSF_XML_IN_NODE (GRAPHIC_FRAME, TWO_CELL_XFRM, XL_NS_SS_DRAW, "xfrm", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (TWO_CELL_XFRM, XFRM_OFF, XL_NS_DRAW, "off", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (TWO_CELL_XFRM, XFRM_EXT, XL_NS_DRAW, "ext", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (TWO_CELL, CLIENT_DATA, XL_NS_SS_DRAW, "clientData", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (TWO_CELL, CONTENT_PART, XL_NS_SS_DRAW, "contentPart", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (TWO_CELL, CXN_SP, XL_NS_SS_DRAW, "cxnSp", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (TWO_CELL, PICTURE, XL_NS_SS_DRAW, "pic", GSF_XML_NO_CONTENT, &xlsx_drawing_picture, NULL),
      GSF_XML_IN_NODE (PICTURE, PIC_FILL_BLIP, XL_NS_SS_DRAW, "blipFill", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (PIC_FILL_BLIP, BLIP, XL_NS_DRAW, "blip", GSF_XML_NO_CONTENT, &xlsx_blip_start, NULL),
        GSF_XML_IN_NODE (BLIP, EXTLST, XL_NS_DRAW, "extLst", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (EXTLST, EXTITEM, XL_NS_DRAW, "ext", GSF_XML_NO_CONTENT, &xlsx_ext_begin, NULL),
        GSF_XML_IN_NODE (PIC_FILL_BLIP, BLIP_STRETCH, XL_NS_DRAW, "stretch", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (BLIP_STRETCH, BLIP_FILL_RECT, XL_NS_DRAW, "fillRect", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (PIC_FILL_BLIP, BLIP_SRC_RECT, XL_NS_DRAW, "srcRect", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (BLIP_SRC_RECT, BLIP_FILL_RECT, XL_NS_DRAW, "fillRect", GSF_XML_NO_CONTENT, NULL, NULL), /* 2nd */
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
    GSF_XML_IN_NODE (ONE_CELL, ANCHOR_FROM, XL_NS_SS_DRAW, "from", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
    GSF_XML_IN_NODE (ONE_CELL, ONE_CELL_EXT, XL_NS_SS_DRAW, "ext", GSF_XML_NO_CONTENT, &xlsx_drawing_ext, NULL),
    GSF_XML_IN_NODE (ONE_CELL, CLIENT_DATA, XL_NS_SS_DRAW, "clientData", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
    GSF_XML_IN_NODE (ONE_CELL, GRAPHIC_FRAME, XL_NS_SS_DRAW, "graphicFrame", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd Def */
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
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	char **elts, **cur, *key, *value, *end;
	double coords[4], local_coords[4], *cur_offs, dim;
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
					dim = g_ascii_strtod (value, &end);
					if (!strcmp (end, "pt"))
						dim *= 4./3.;
					coords[0] = (double) dim * XLSX_SHEET_HSCALE;
				} else if (!strcmp (key, "margin-top") || !strcmp (key, "top")) {
					dim = g_ascii_strtod (value, &end);
					if (!strcmp (end, "pt"))
						dim *= 4./3.;
					coords[1] = dim;
				} else if (!strcmp (key, "width")) {
					dim = g_ascii_strtod (value, &end);
					if (!strcmp (end, "pt"))
						dim *= 4./3.;
					coords[2] = (double) dim * XLSX_SHEET_HSCALE;
				} else if (!strcmp (key, "height")) {
					dim = g_ascii_strtod (value, &end);
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
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	/* assuming that a group can't be inside another group, otherwise we need a stack */
	memcpy (state->grp_offset, state->grp_stack->data, sizeof (double) * 4);
	g_free (state->grp_stack->data);
	state->grp_stack = g_slist_delete_link (state->grp_stack, state->grp_stack);
}

static void
xlsx_vml_shape (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (!strcmp (attrs[0], "style")) {
			char **elts = g_strsplit (attrs[1], ";", 0), **cur, *key, *value, *end;
			int dim;
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
					dim = g_ascii_strtod (value, &end);
					if (!strcmp (end, "pt"))
						dim *= 4./3.;
					state->chart_pos[0] = (double) dim * XLSX_SHEET_HSCALE;
				} else if (!strcmp (key, "margin-top") || !strcmp (key, "top")) {
					dim = g_ascii_strtod (value, &end);
					if (!strcmp (end, "pt"))
						dim *= 4./3.;
					state->chart_pos[1] = dim;
				} else if (!strcmp (key, "width")) {
					dim = g_ascii_strtod (value, &end);
					if (!strcmp (end, "pt"))
						dim *= 4./3.;
					state->chart_pos[2] = (double) dim * XLSX_SHEET_HSCALE;
				} else if (!strcmp (key, "height")) {
					dim = g_ascii_strtod (value, &end);
					if (!strcmp (end, "pt"))
						dim *= 4./3.;
					state->chart_pos[3] = dim;
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

static void
xlsx_vml_drop_style (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	if (!strcmp (xin->content->str, "Combo"))
		/* adding a combo box */
		state->so = SHEET_OBJECT (g_object_new (sheet_widget_combo_get_type (), NULL));
		sheet_object_set_sheet (state->so, state->sheet);
}

static void
xlsx_vml_client_data (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	if (state->so) {
		SheetObjectAnchor anchor;
		ColRowInfo *cri;
		GnmRange r;
		double coords[4];
		int default_size = sheet_col_get_default_size_pixels (state->sheet);
		int pos, sum, size;
		for (pos = 0, sum = 0; /* no test */; pos++) {
			cri = sheet_col_get (state->sheet, pos);
			size = (cri)? cri->size_pixels: default_size;
			if (sum + size > state->chart_pos[0])
				break;
			sum += size;
		}
		r.start.col = pos;
		coords[0] = (state->chart_pos[0] - sum) / size;
		while (sum < state->chart_pos[2]) {
			if (sum + size > state->chart_pos[2])
				break;
			sum += size;
			cri = sheet_col_get (state->sheet, pos);
			size = (cri)? cri->size_pixels: default_size;
			pos++;
		}
		r.end.col = pos;
		coords[2] = (state->chart_pos[2] - sum) / size;
		default_size = sheet_row_get_default_size_pixels (state->sheet);
		for (pos = 0, sum = 0; /* no test */; pos++) {
			cri = sheet_row_get (state->sheet, pos);
			size = (cri)? cri->size_pixels: default_size;
			if (sum + size > state->chart_pos[1])
				break;
			sum += size;
		}
		r.start.row = pos;
		coords[1] = (state->chart_pos[1] - sum) / size;
		while (sum < state->chart_pos[3]) {
			if (sum + size > state->chart_pos[3])
				break;
			sum += size;
			cri = sheet_row_get (state->sheet, pos);
			size = (cri)? cri->size_pixels: default_size;
			pos++;
		}
		r.end.row = pos;
		coords[3] = (state->chart_pos[3] - sum) / size;
		sheet_object_anchor_init (&anchor, &r, coords, GOD_ANCHOR_DIR_DOWN_RIGHT);
		sheet_object_set_anchor (state->so, &anchor);
		if (GNM_IS_SOW_LIST (state->so) || GNM_IS_SOW_COMBO (state->so))
			sheet_widget_list_base_set_links (state->so, state->link_texpr, state->texpr);
		g_object_unref (state->so);
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
}

static void
xlsx_vml_fmla_link (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	GnmValue *value = value_new_cellrange_str (state->sheet, xin->content->str);
	if (value)
		state->link_texpr = gnm_expr_top_new_constant (value);
}

static void
xlsx_vml_fmla_range (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	XLSXReadState	*state = (XLSXReadState *)xin->user_state;
	GnmValue *value = value_new_cellrange_str (state->sheet, xin->content->str);
	if (value)
		state->texpr = gnm_expr_top_new_constant (value);
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
    GSF_XML_IN_NODE (TEXTBOX, DIV, -1, "div", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (SP, LOCK, XL_NS_LEG_OFF, "lock", GSF_XML_NO_CONTENT, NULL, NULL), /* already defined */
  GSF_XML_IN_NODE (SP, CLIENT_DATA, XL_NS_LEG_XL, "ClientData", GSF_XML_NO_CONTENT, NULL, &xlsx_vml_client_data),
    GSF_XML_IN_NODE (CLIENT_DATA, ANCHOR, XL_NS_LEG_XL, "Anchor", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (CLIENT_DATA, AUTO_FILL, XL_NS_LEG_XL, "AutoFill", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (CLIENT_DATA, AUTO_LINE, XL_NS_LEG_XL, "AutoLine", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (CLIENT_DATA, DROP_LINES, XL_NS_LEG_XL, "DropLines", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (CLIENT_DATA, COLUMN, XL_NS_LEG_XL, "Column", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (CLIENT_DATA, DROP_STYLE, XL_NS_LEG_XL, "DropStyle", GSF_XML_CONTENT, NULL, &xlsx_vml_drop_style),
    GSF_XML_IN_NODE (CLIENT_DATA, DX, XL_NS_LEG_XL, "Dx", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (CLIENT_DATA, FMLA_LINK, XL_NS_LEG_XL, "FmlaLink", GSF_XML_CONTENT, NULL, &xlsx_vml_fmla_link),
    GSF_XML_IN_NODE (CLIENT_DATA, FMLA_RANGE, XL_NS_LEG_XL, "FmlaRange", GSF_XML_CONTENT, NULL, &xlsx_vml_fmla_range),
    GSF_XML_IN_NODE (CLIENT_DATA, INC, XL_NS_LEG_XL, "Inc", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (CLIENT_DATA, LCT, XL_NS_LEG_XL, "LCT", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (CLIENT_DATA, XMIN, XL_NS_LEG_XL, "Min", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (CLIENT_DATA, MOVE_WITH_CELLS, XL_NS_LEG_XL, "MoveWithCells", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (CLIENT_DATA, XMAX, XL_NS_LEG_XL, "Max", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (CLIENT_DATA, MOVE_WITH_CELLS, XL_NS_LEG_XL, "MoveWithCells", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (CLIENT_DATA, PAGE, XL_NS_LEG_XL, "Page", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (CLIENT_DATA, PRINT_OBJECT, XL_NS_LEG_XL, "PrintObject", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (CLIENT_DATA, RECALC_ALWAYS, XL_NS_LEG_XL, "RecalcAlways", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (CLIENT_DATA, ROW, XL_NS_LEG_XL, "Row", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (CLIENT_DATA, SEL, XL_NS_LEG_XL, "Sel", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (CLIENT_DATA, SEL_TYPE, XL_NS_LEG_XL, "SelType", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (CLIENT_DATA, SIZE_WITH_CELLS, XL_NS_LEG_XL, "SizeWithCells", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (CLIENT_DATA, VAL, XL_NS_LEG_XL, "Val", GSF_XML_NO_CONTENT, NULL, NULL),
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
	if (NULL != part_id)
		xlsx_parse_rel_by_id (xin, part_id, xlsx_legacy_drawing_dtd, xlsx_ns);
}
