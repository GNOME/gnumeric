/* RGBMAX, HSLMAX must each fit in a byte. */
/* HSLMAX BEST IF DIVISIBLE BY 6 */
#define  HSLMAX   240 /* H,L, and S vary over 0-HSLMAX */
#define  RGBMAX   255 /* R,G, and B vary over 0-RGBMAX */

/* Hue is undefined if Saturation is 0 (grey-scale) */
/* This value determines where the Hue scrollbar is */
/* initially set for achromatic colors */
//#define UNDEFINED (HSLMAX*2/3)

/* utility routine for HSLtoRGB */
static int
hue_to_color (int m1, int m2, int h)
{
	if (h < 0)
		h += HSLMAX;
	if (h > HSLMAX)
		h -= HSLMAX;

	/* return r,g, or b value from this tridrant */
	if (h < HSLMAX / 6)
		return m1 + (((m2 - m1) * h + HSLMAX / 12) / (HSLMAX / 6));
	if (h < HSLMAX / 2)
		return m2;
	if (h < HSLMAX * 2 /3)
		return m1 + ((m2 - m1) * ((HSLMAX * 2 / 3 - h) + HSLMAX / 12) / (HSLMAX / 6));

	return m1;
}

static GOColor
gnm_go_color_from_hsla (int h, int s, int l, int a)
{
	int m2 = (l <= HSLMAX / 2)
		? (l * (HSLMAX + s) + HSLMAX / 2) / HSLMAX
		: l + s - (l * s + HSLMAX / 2) / HSLMAX;
	int m1 = 2 * l - m2;
	guint8 r = (hue_to_color (m1, m2, h + HSLMAX / 3) * RGBMAX + HSLMAX / 2) / HSLMAX;
	guint8 g = (hue_to_color (m1, m2, h             ) * RGBMAX + HSLMAX / 2) / HSLMAX;
	guint8 b = (hue_to_color (m1, m2, h - HSLMAX / 3) * RGBMAX + HSLMAX / 2) / HSLMAX;

	return GO_COLOR_FROM_RGBA (r,g,b,a);
}

static void
gnm_go_color_to_hsla (GOColor orig, int *ph, int *ps, int *pl, int *pa)
{
	int r = GO_COLOR_UINT_R (orig);
	int g = GO_COLOR_UINT_G (orig);
	int b = GO_COLOR_UINT_B (orig);
	int a = GO_COLOR_UINT_A (orig);
	int maxC = b, minC = b, delta, sum, h = 0, l, s;

	maxC = MAX (MAX (r,g),b);
	minC = MIN (MIN (r,g),b);
	l = (((maxC + minC)*HSLMAX) + RGBMAX)/(2*RGBMAX);

	delta = maxC - minC;
	sum   = maxC + minC;
	if (delta != 0) {
		if (l <= (HSLMAX/2))
			s = ( (delta*HSLMAX) + (sum/2) ) / sum;
		else
			s = ( (delta*HSLMAX) + ((2*RGBMAX - sum)/2) ) / (2*RGBMAX - sum);

		if (r == maxC)
			h =                ((g - b) * HSLMAX) / (6 * delta);
		else if (g == maxC)
			h = (  HSLMAX/3) + ((b - r) * HSLMAX) / (6 * delta);
		else if (b == maxC)
			h = (2*HSLMAX/3) + ((r - g) * HSLMAX) / (6 * delta);

		if (h < 0)
			h += HSLMAX;
		else if (h >= HSLMAX)
			h -= HSLMAX;
	} else {
		h = 0;
		s = 0;
	}

	*ph = h;
	*ps = s;
	*pl = l;
	*pa = a;
}

/* See http://en.wikipedia.org/wiki/SRGB#Specification_of_the_transformation */

#define GAMMA1(c_) do {						\
	double c = (c_) / 255.0;				\
	if (c < 0.04045)					\
		c = c / 12.92;					\
	else							\
		c = pow ((c + 0.055) / 1.055, (12.0 / 5.0));	\
	c = MIN (255, c * 256);					\
	(c_) = (int)c;						\
} while (0)

static GOColor
gnm_go_color_gamma (GOColor col)
{
	int r = GO_COLOR_UINT_R (col);
	int g = GO_COLOR_UINT_G (col);
	int b = GO_COLOR_UINT_B (col);
	int a = GO_COLOR_UINT_A (col);

	GAMMA1 (r);
	GAMMA1 (g);
	GAMMA1 (b);

	return GO_COLOR_FROM_RGBA (r, g, b, a);
}
#undef GAMMA1

#define INVGAMMA1(c_) do {					\
	double c = (c_) / 255.0;				\
	if (c < 0.0031308)					\
		c = c * 12.92;					\
	else							\
		c = 1.055 * pow (c, 5.0 / 12.0) - 0.055;	\
	c = MIN (255, c * 256);					\
	(c_) = (int)c;						\
} while (0)

static GOColor
gnm_go_color_invgamma (GOColor col)
{
	int r = GO_COLOR_UINT_R (col);
	int g = GO_COLOR_UINT_G (col);
	int b = GO_COLOR_UINT_B (col);
	int a = GO_COLOR_UINT_A (col);

	INVGAMMA1 (r);
	INVGAMMA1 (g);
	INVGAMMA1 (b);

	return GO_COLOR_FROM_RGBA (r, g, b, a);
}
#undef INVGAMMA1


/*
 * Apply tinting or shading.
 *  0 <= tint <= 1: tinting -- l is increased
 * -1 <= tint <= 0: shading -- l is decreased
 */

static GOColor
gnm_go_color_apply_tint (GOColor orig, double tint)
{
	int h, s, l, a;

	if (fabs (tint) < .005)
		return orig;

	gnm_go_color_to_hsla (orig, &h, &s, &l, &a);

	tint = CLAMP (tint, -1.0, +1.0);

	if (tint < 0.)
		l = l * (1. + tint);
	else
		l = l * (1. - tint) + (HSLMAX - HSLMAX * (1.0 - tint));

	if (s == 0) {            /* achromatic case */
		int g = (l * RGBMAX) / HSLMAX;
		return GO_COLOR_FROM_RGBA (g, g, g, a);
	}

	return gnm_go_color_from_hsla (h, s, l, a);
}

typedef enum {
	XLSX_CS_NONE = 0,
	XLSX_CS_FONT = 1,
	XLSX_CS_LINE = 2,
	XLSX_CS_FILL_BACK = 3,
	XLSX_CS_FILL_FORE = 4,
	XLSX_CS_MARKER = 5,
	XLSX_CS_MARKER_OUTLINE = 6,
        XLSX_CS_ANY = 7 /* for pop */
} XLSXColorState;

static void
color_set_helper (XLSXReadState *state)
{
#ifdef DEBUG_COLOR
	g_printerr ("color: #%08x in state %d\n",
		    state->color, state->chart_color_state & 7);
#endif
	if (!state->cur_style)
		return;

	switch (state->chart_color_state & 7) {
	default:
	case XLSX_CS_NONE:
		break;
	case XLSX_CS_FONT:
		state->cur_style->font.color = state->color;
		state->cur_style->font.auto_color = FALSE;
		break;
	case XLSX_CS_LINE:
		state->cur_style->line.color = state->color;
		state->cur_style->line.auto_color = FALSE;
		break;
	case XLSX_CS_FILL_BACK:
		state->cur_style->fill.pattern.back = state->color;
		state->cur_style->fill.auto_back = FALSE;
		break;
	case XLSX_CS_FILL_FORE:
		state->cur_style->fill.pattern.fore = state->color;
		state->cur_style->fill.auto_fore = FALSE;
		break;
	case XLSX_CS_MARKER:
		go_marker_set_fill_color (state->marker, state->color);
		state->cur_style->marker.auto_fill_color = FALSE;
		break;
	case XLSX_CS_MARKER_OUTLINE:
		go_marker_set_outline_color (state->marker, state->color);
		state->cur_style->marker.auto_outline_color = FALSE;
		break;
	}
}

static void
xlsx_draw_color_rgba_channel (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	guint action = xin->node->user_data.v_int & 3;
	guint channel = xin->node->user_data.v_int >> 2; /* a=3, r=2, g=1, b=0 */
	int val;
	if (simple_int (xin, attrs, &val)) {
		const double f = val / 100000.0;
		int v;
		double vf;

		switch (channel) {
		case 3: v = GO_COLOR_UINT_A (state->color); break;
		case 2: v = GO_COLOR_UINT_R (state->color); break;
		case 1: v = GO_COLOR_UINT_G (state->color); break;
		case 0: v = GO_COLOR_UINT_B (state->color); break;
		default: g_assert_not_reached ();
		}
		switch (action) {
		case 0:	vf = 256 * f; break;
		case 1: vf = v + 256 * f; break;
		case 2: vf = v * f; break;
		default: g_assert_not_reached ();
		}
		v = CLAMP (vf, 0, 255);
		switch (channel) {
		case 3: state->color = GO_COLOR_CHANGE_A (state->color, v); break;
		case 2: state->color = GO_COLOR_CHANGE_R (state->color, v); break;
		case 1: state->color = GO_COLOR_CHANGE_G (state->color, v); break;
		case 0: state->color = GO_COLOR_CHANGE_B (state->color, v); break;
		default: g_assert_not_reached ();
		}
		color_set_helper (state);
	}
}

static void
xlsx_draw_color_gray (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	int g = (22 * GO_COLOR_UINT_R (state->color) +
		 72 * GO_COLOR_UINT_G (state->color) +
		 06 * GO_COLOR_UINT_B (state->color)) / 100;
	state->color = GO_COLOR_GREY (g);
	color_set_helper (state);
}

static void
xlsx_draw_color_comp (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	g_warning ("Unhandled hsl complement of #%08x\n", state->color);
}

static void
xlsx_draw_color_invert (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	state->color = GO_COLOR_FROM_RGBA (0xff, 0xff, 0xff, 0) ^ state->color;
	color_set_helper (state);
}

static void
xlsx_draw_color_hsl_channel (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	guint action = xin->node->user_data.v_int & 3;
	guint channel = xin->node->user_data.v_int >> 2; /* hue=2, sat=1, lum=0 */
	int val;
	if (simple_int (xin, attrs, &val)) {
		const double f = val / 100000.0;
		int hsl[3], a, v;
		double vf;

		gnm_go_color_to_hsla (state->color, &hsl[2], &hsl[1], &hsl[0], &a);
		v = hsl[channel];

		switch (action) {
		case 0:	vf = (HSLMAX + 1) * f; break;
		case 1: vf = v + (HSLMAX + 1) * f; break;
		case 2: vf = v * f; break;
		default: g_assert_not_reached ();
		}

		hsl[channel] = CLAMP (vf, 0, HSLMAX);
		state->color = gnm_go_color_from_hsla (hsl[2], hsl[1], hsl[0], a);
		color_set_helper (state);
	}
}

static void
xlsx_draw_color_gamma (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	gboolean inv = xin->node->user_data.v_int;

	if (inv)
		state->color = gnm_go_color_invgamma (state->color);
	else
		state->color = gnm_go_color_gamma (state->color);

	color_set_helper (state);
}


static void
xlsx_draw_color_shade (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	unsigned val;
	if (simple_uint (xin, attrs, &val)) {
		const double scale = 100000;
		double f = val / scale;
		state->color = gnm_go_color_apply_tint (state->color, -f);
		color_set_helper (state);
	}
}

static void
xlsx_draw_color_tint (GsfXMLIn *xin, xmlChar const **attrs)
{
	XLSXReadState *state = (XLSXReadState *)xin->user_state;
	unsigned val;
	if (simple_uint (xin, attrs, &val)) {
		const double scale = 100000;
		double f = val / scale;
		state->color = gnm_go_color_apply_tint (state->color, f);
		color_set_helper (state);
	}
}


#define COLOR_MODIFIER_NODE(parent,node,name,first,handler,user) \
	GSF_XML_IN_NODE_FULL (parent, node, XL_NS_DRAW, name, (first ? GSF_XML_NO_CONTENT : GSF_XML_2ND), FALSE, FALSE, handler, NULL, user)

#define COLOR_MODIFIER_NODES(parent,first)				\
	COLOR_MODIFIER_NODE(parent, COLOR_SHADE, "shade", first, &xlsx_draw_color_shade, 0), \
	COLOR_MODIFIER_NODE(parent, COLOR_TINT, "tint", first, &xlsx_draw_color_tint, 0), \
	COLOR_MODIFIER_NODE(parent, COLOR_COMP, "comp", first, &xlsx_draw_color_comp, 0), \
	COLOR_MODIFIER_NODE(parent, COLOR_INV, "inv", first, &xlsx_draw_color_invert, 0), \
	COLOR_MODIFIER_NODE(parent, COLOR_GRAY, "gray", first, &xlsx_draw_color_gray, 0), \
	COLOR_MODIFIER_NODE(parent, COLOR_ALPHA, "alpha", first, &xlsx_draw_color_rgba_channel, 12), \
	COLOR_MODIFIER_NODE(parent, COLOR_ALPHA_OFF, "alphaOff", first, &xlsx_draw_color_rgba_channel, 13), \
	COLOR_MODIFIER_NODE(parent, COLOR_ALPHA_MOD, "alphaMod", first, &xlsx_draw_color_rgba_channel, 14), \
	COLOR_MODIFIER_NODE(parent, COLOR_HUE, "hue", first, xlsx_draw_color_hsl_channel, 8), \
	COLOR_MODIFIER_NODE(parent, COLOR_HUE_OFF, "hueOff", first, xlsx_draw_color_hsl_channel, 9), \
	COLOR_MODIFIER_NODE(parent, COLOR_HUE_MOD, "hueMod", first, xlsx_draw_color_hsl_channel, 10), \
	COLOR_MODIFIER_NODE(parent, COLOR_SAT, "sat", first, xlsx_draw_color_hsl_channel, 4), \
	COLOR_MODIFIER_NODE(parent, COLOR_SAT_OFF, "satOff", first, xlsx_draw_color_hsl_channel, 5), \
	COLOR_MODIFIER_NODE(parent, COLOR_SAT_MOD, "satMod", first, xlsx_draw_color_hsl_channel, 6), \
	COLOR_MODIFIER_NODE(parent, COLOR_LUM, "lum", first, xlsx_draw_color_hsl_channel, 0), \
	COLOR_MODIFIER_NODE(parent, COLOR_LUM_OFF, "lumOff", first, xlsx_draw_color_hsl_channel, 1), \
	COLOR_MODIFIER_NODE(parent, COLOR_LUM_MOD, "lumMod", first, xlsx_draw_color_hsl_channel, 2), \
	COLOR_MODIFIER_NODE(parent, COLOR_RED, "red", first, &xlsx_draw_color_rgba_channel, 8), \
	COLOR_MODIFIER_NODE(parent, COLOR_RED_OFF, "redOff", first, &xlsx_draw_color_rgba_channel, 9), \
	COLOR_MODIFIER_NODE(parent, COLOR_RED_MOD, "redMod", first, &xlsx_draw_color_rgba_channel, 10), \
	COLOR_MODIFIER_NODE(parent, COLOR_GREEN, "green", first, &xlsx_draw_color_rgba_channel, 4), \
	COLOR_MODIFIER_NODE(parent, COLOR_GREEN_OFF, "greenOff", first, &xlsx_draw_color_rgba_channel, 5), \
	COLOR_MODIFIER_NODE(parent, COLOR_GREEN_MOD, "greenMod", first, &xlsx_draw_color_rgba_channel, 6), \
	COLOR_MODIFIER_NODE(parent, COLOR_BLUE, "blue", first, &xlsx_draw_color_rgba_channel, 0), \
	COLOR_MODIFIER_NODE(parent, COLOR_BLUE_OFF, "blueOff", first, &xlsx_draw_color_rgba_channel, 1), \
	COLOR_MODIFIER_NODE(parent, COLOR_BLUE_MOD, "blueMod", first, &xlsx_draw_color_rgba_channel, 2), \
	COLOR_MODIFIER_NODE(parent, COLOR_GAMMA, "gamma", first, &xlsx_draw_color_gamma, 0), \
	COLOR_MODIFIER_NODE(parent, COLOR_INV_GAMMA, "invGamma", first, &xlsx_draw_color_gamma, 1)
