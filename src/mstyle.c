/*
 * mstyle.c: Storing a style
 *
 * Authors:
 *   Michael Meeks <mmeeks@gnu.org>
 *   Almer S. Tigelaar <almer@gnome.org>
 *   Jody Goldberg <jody@gnome.org>
 *   Morten Welinder <terra@gnome.org>
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include <style.h>

#include <sheet-style.h>
#include <style-border.h>
#include <style-font.h>
#include <style-color.h>
#include <style-conditions.h>
#include <sheet-conditions.h>
#include <validation.h>
#include <pattern.h>
#include <hlink.h>
#include <input-msg.h>
#include <application.h>
#include <parse-util.h>
#include <expr.h>
#include <value.h>
#include <gutils.h>
#include <ranges.h>
#include <sheet.h>
#include <gnumeric-conf.h>
#include <goffice/goffice.h>
#include <string.h>

static gboolean debug_style_deps;

#define DEBUG_STYLES
#ifndef USE_MSTYLE_POOL
#define USE_MSTYLE_POOL 1
#endif

#if USE_MSTYLE_POOL
/* Memory pool for GnmStyles.  */
static GOMemChunk *gnm_style_pool;
#define CHUNK_ALLOC(T,p) ((T*)go_mem_chunk_alloc (p))
#define CHUNK_ALLOC0(T,p) ((T*)go_mem_chunk_alloc0 (p))
#define CHUNK_FREE(p,v) go_mem_chunk_free ((p), (v))
#else
#define CHUNK_ALLOC(T,c) g_new (T,1)
#define CHUNK_ALLOC0(T,c) g_new0 (T,1)
#define CHUNK_FREE(p,v) g_free ((v))
#endif


struct _GnmStyle {
	unsigned int	changed;
	unsigned int	set;

	unsigned int    hash_key;
	unsigned int    hash_key_xl;
	unsigned int    ref_count;
	unsigned int    link_count;
	Sheet	       *linked_sheet;

	PangoAttrList *pango_attrs;
	double         pango_attrs_zoom;
	int            pango_attrs_height;

	GnmFont       *font;
	PangoContext  *font_context;

/* public */
	struct _GnmStyleColor {
		GnmColor *font;
		GnmColor *back;
		GnmColor *pattern;
	} color;
	GnmBorder	*borders[MSTYLE_BORDER_DIAGONAL - MSTYLE_BORDER_TOP + 1];
	guint32          pattern;

	/* FIXME: TODO use GOFont */
	struct _GnmStyleFontDetails {
		GOString	*name;
		gboolean	bold;
		gboolean	italic;
		GnmUnderline	underline;
		gboolean	strikethrough;
		GOFontScript	script;
		double		size;
	} font_detail;

	GOFormat const *format;
	GnmHAlign	 h_align;
	GnmVAlign	 v_align;
	int		 indent;
	int		 rotation;
	int		 text_dir;
	gboolean         wrap_text;
	gboolean         shrink_to_fit;
	gboolean         contents_locked;
	gboolean         contents_hidden;

	GnmValidation		*validation;
	GnmHLink		*hlink;
	GnmInputMsg		*input_msg;
	GnmStyleConditions	*conditions;
	GPtrArray		*cond_styles;
};

#define elem_changed(style, elem) do { (style)->changed |= (1u << (elem)); } while(0)
#define elem_set(style, elem)	  do { (style)->set |=  (1u << (elem)); } while(0)
#define elem_unset(style, elem)	  do { (style)->set &= ~(1u << (elem)); } while(0)
#define elem_is_set(style, elem)  (((style)->set & (1u << (elem))) != 0)

#define MSTYLE_ANY_BORDER            MSTYLE_BORDER_TOP: \
				case MSTYLE_BORDER_BOTTOM: \
				case MSTYLE_BORDER_LEFT: \
				case MSTYLE_BORDER_RIGHT: \
				case MSTYLE_BORDER_DIAGONAL: \
				case MSTYLE_BORDER_REV_DIAGONAL


#define UNROLLED_FOR(init_,cond_,step_,code_)			\
do {								\
	init_;							\
	if (cond_) { code_; step_;				\
	if (cond_) { code_; step_;				\
	if (cond_) { code_; step_;				\
	if (cond_) { code_; step_;				\
	if (cond_) { code_; step_;				\
	if (cond_) { code_; step_;				\
	if (cond_) { code_; step_;				\
	if (cond_) { code_; step_;				\
	if (cond_) { code_; step_;				\
	if (cond_) { code_; step_;				\
	if (cond_) { code_; step_;				\
	if (cond_) { code_; step_;				\
	if (cond_) { code_; step_;				\
	if (cond_) { code_; step_;				\
	if (cond_) { code_; step_;				\
	if (cond_) { code_; step_;				\
	if (cond_) { code_; step_;				\
	if (cond_) { code_; step_;				\
	if (cond_) { code_; step_;				\
	if (cond_) { code_; step_;				\
	if (cond_) { code_; step_;				\
	if (cond_) { code_; step_;				\
	if (cond_) { code_; step_;				\
	if (cond_) { code_; step_;				\
	if (cond_) { code_; step_;				\
	if (cond_) { code_; step_;				\
	if (cond_) { code_; step_;				\
	if (cond_) { code_; step_;				\
	if (cond_) { code_; step_;				\
	if (cond_) { code_; step_;				\
	if (cond_) { code_; step_;				\
	if (cond_) { code_; step_;				\
	if (cond_) { code_; step_;				\
	if (cond_) { code_; step_;				\
	if (cond_) { code_; step_;				\
	if (cond_) { code_; step_;				\
	if (cond_) { code_; step_;				\
	if (cond_) { code_; step_;				\
	if (cond_) { code_; step_;				\
	if (cond_) { code_; step_;				\
	if (cond_) { code_; step_;				\
	if (cond_) { code_; step_;				\
	if (cond_) { code_; step_;				\
	if (cond_) { code_; step_;				\
	if (cond_) { code_; step_;				\
	if (cond_) { code_; step_;				\
	if (cond_) { code_; step_;				\
	if (cond_) { code_; step_;				\
	if (cond_) { code_; step_;				\
	if (cond_) { code_; step_;				\
	g_assert_not_reached ();				\
	}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}	\
} while (0)



static char const * const
gnm_style_element_name[MSTYLE_ELEMENT_MAX] = {
	"Color.Back",
	"Color.Pattern",
	"Border.Top",
	"Border.Bottom",
	"Border.Left",
	"Border.Right",
	"Border.RevDiagonal",
	"Border.Diagonal",
	"Pattern",
	"Color.Fore",
	"Font.Name",
	"Font.Bold",
	"Font.Italic",
	"Font.Underline",
	"Font.Strikethrough",
	"Font.Script",
	"Font.Size",
	"Format",
	"Align.v",
	"Align.h",
	"Indent",
	"Rotation",
	"WrapText",
	"ShrinkToFit",
	"Contents.Locked",
	"Contents.Hidden",
	"Validation",
	"Hyper Link",
	"Input Msg"
};

/* Some ref/link count debugging */
#if 0
#define d(arg)	g_printerr arg
#else
#define d(arg)	do { } while (0)
#endif

static void
clear_conditional_merges (GnmStyle *style)
{
	if (style->cond_styles) {
		unsigned i = style->cond_styles->len;
		while (i-- > 0)
			gnm_style_unref (g_ptr_array_index (style->cond_styles, i));
		g_ptr_array_free (style->cond_styles, TRUE);
		style->cond_styles = NULL;
	}
}

#define MIX(H) do {				\
  H *= G_GUINT64_CONSTANT(123456789012345);	\
  H ^= (H >> 31);				\
} while (0)

static void
gnm_style_update (GnmStyle *style)
{
	guint64 hash = 0;
	int i;

	g_return_if_fail (style->changed);

	style->changed = 0;

	clear_conditional_merges (style);
	if (elem_is_set (style, MSTYLE_CONDITIONS) && style->conditions)
		style->cond_styles = gnm_style_conditions_overlay (style->conditions, style);

	/* ---------------------------------------- */

	if (elem_is_set (style, MSTYLE_COLOR_BACK)) {
		if (!style->color.back->is_auto)
			hash ^= GPOINTER_TO_UINT (style->color.back);
		else
			hash++;
	}
	MIX (hash);

	if (elem_is_set (style, MSTYLE_COLOR_PATTERN)) {
		if (!style->color.pattern->is_auto)
			hash ^= GPOINTER_TO_UINT (style->color.pattern);
		else
			hash++;
	}
	MIX (hash);

	if (elem_is_set (style, MSTYLE_FONT_COLOR)) {
		if (!style->color.font->is_auto)
			hash ^= GPOINTER_TO_UINT (style->color.font);
		else
			hash++;
	}
	MIX (hash);

	for (i = MSTYLE_BORDER_TOP; i <= MSTYLE_BORDER_DIAGONAL; i++) {
		if (elem_is_set (style, i))
			hash ^= GPOINTER_TO_UINT (style->borders[i - MSTYLE_BORDER_TOP]);
		else
			hash++;
		MIX (hash);
	}

	if (elem_is_set (style, MSTYLE_PATTERN))
		hash ^= style->pattern;
	MIX (hash);

	if (elem_is_set (style, MSTYLE_FONT_NAME))
		hash ^= GPOINTER_TO_UINT (style->font_detail.name);
	MIX (hash);

	if (elem_is_set (style, MSTYLE_FONT_BOLD))
		hash ^= (style->font_detail.bold ? 1 : 2);
	MIX (hash);

	if (elem_is_set (style, MSTYLE_FONT_ITALIC))
		hash ^= (style->font_detail.italic ? 1 : 2);
	MIX (hash);

	if (elem_is_set (style, MSTYLE_FONT_UNDERLINE))
		hash ^= (style->font_detail.underline ? 1 : 2);
	MIX (hash);

	if (elem_is_set (style, MSTYLE_FONT_STRIKETHROUGH))
		hash ^= (style->font_detail.strikethrough ? 1 : 2);
	MIX (hash);

	if (elem_is_set (style, MSTYLE_FONT_SCRIPT))
		hash ^= (style->font_detail.script + 0x100);
	MIX (hash);

	if (elem_is_set (style, MSTYLE_FONT_SIZE))
		hash ^= ((int)(style->font_detail.size * 97));
	MIX (hash);

	if (elem_is_set (style, MSTYLE_FORMAT))
		hash ^= GPOINTER_TO_UINT (style->format);
	MIX (hash);

	if (elem_is_set (style, MSTYLE_ALIGN_H))
		hash ^= (style->h_align + 0x100);
	MIX (hash);

	if (elem_is_set (style, MSTYLE_ALIGN_V))
		hash ^= (style->v_align + 0x100);
	MIX (hash);

	if (elem_is_set (style, MSTYLE_INDENT))
		hash ^= style->indent;
	MIX (hash);

	if (elem_is_set (style, MSTYLE_ROTATION))
		hash ^= style->rotation;
	MIX (hash);

	if (elem_is_set (style, MSTYLE_TEXT_DIR))
		hash ^= (style->text_dir + 0x100);
	MIX (hash);

	if (elem_is_set (style, MSTYLE_WRAP_TEXT))
		hash ^= (style->wrap_text ? 1 : 2);
	MIX (hash);

	if (elem_is_set (style, MSTYLE_SHRINK_TO_FIT))
		hash ^= (style->shrink_to_fit ? 1 : 2);
	MIX (hash);

	if (elem_is_set (style, MSTYLE_CONTENTS_LOCKED))
		hash ^= (style->contents_locked ? 1 : 2);
	MIX (hash);

	if (elem_is_set (style, MSTYLE_CONTENTS_HIDDEN))
		hash ^= (style->contents_hidden ? 1 : 2);
	MIX (hash);

	style->hash_key_xl = (guint32)hash;

	/* From here on, fields are not in MS XL */

	if (elem_is_set (style, MSTYLE_VALIDATION)) {
		/*
		 * The hash used must not depend on the expressions inside
		 * the validation.
		 */
		hash ^= (style->validation != NULL ? 1 : 2);
	}
	MIX (hash);

	if (elem_is_set (style, MSTYLE_HLINK))
		hash ^= GPOINTER_TO_UINT (style->hlink);
	MIX (hash);

	if (elem_is_set (style, MSTYLE_INPUT_MSG))
		hash ^= GPOINTER_TO_UINT (style->input_msg);
	MIX (hash);

	if (elem_is_set (style, MSTYLE_CONDITIONS)) {
		/*
		 * The hash used must not depend on the expressions inside
		 * the conditions.
		 */
		hash ^= style->conditions
			? gnm_style_conditions_hash (style->conditions)
			: 1u;
	}
	MIX (hash);

	style->hash_key = (guint32)hash;

	if (G_UNLIKELY (style->set == 0)) {
		/*
		 * gnm_style_new and gnm_style_dup both assume that the
		 * correct hash values (both of them) for the empty style
		 * is zero.
		 */
		g_assert (style->hash_key == 0);
		g_assert (style->hash_key_xl == 0);
	}
}

#undef MIX

guint
gnm_style_hash_XL (gconstpointer style)
{
	if (((GnmStyle const *)style)->changed)
		gnm_style_update ((GnmStyle *)style);
	return ((GnmStyle const *)style)->hash_key_xl;
}

guint
gnm_style_hash (gconstpointer style)
{
	if (((GnmStyle const *)style)->changed)
		gnm_style_update ((GnmStyle *)style);
	return ((GnmStyle const *)style)->hash_key;
}

#define ELEM_IS_EQ(a,b,elem)						\
    (elem == MSTYLE_COLOR_BACK						\
    ? a->color.back == b->color.back ||	(a->color.back->is_auto && b->color.back->is_auto) \
    : (elem == MSTYLE_COLOR_PATTERN					\
    ? a->color.pattern == b->color.pattern || (a->color.pattern->is_auto && b->color.pattern->is_auto) \
    : (elem >= MSTYLE_BORDER_TOP && elem <= MSTYLE_BORDER_DIAGONAL)	\
    ? a->borders[elem - MSTYLE_BORDER_TOP] == b->borders[elem - MSTYLE_BORDER_TOP] \
    : (elem == MSTYLE_PATTERN						\
    ? a->pattern == b->pattern						\
    : (elem == MSTYLE_FONT_COLOR					\
    ? a->color.font == b->color.font || (a->color.font->is_auto && b->color.font->is_auto) \
    : (elem == MSTYLE_FONT_NAME						\
    ? a->font_detail.name == b->font_detail.name			\
    : (elem == MSTYLE_FONT_BOLD						\
    ? a->font_detail.bold == b->font_detail.bold			\
    : (elem == MSTYLE_FONT_ITALIC					\
    ? a->font_detail.italic == b->font_detail.italic			\
    : (elem == MSTYLE_FONT_UNDERLINE					\
    ? a->font_detail.underline == b->font_detail.underline		\
    : (elem == MSTYLE_FONT_STRIKETHROUGH				\
    ? a->font_detail.strikethrough == b->font_detail.strikethrough	\
    : (elem == MSTYLE_FONT_SCRIPT					\
    ? a->font_detail.script == b->font_detail.script			\
    : (elem == MSTYLE_FONT_SIZE						\
    ? a->font_detail.size == b->font_detail.size			\
    : (elem == MSTYLE_FORMAT						\
    ? a->format == b->format						\
    : (elem == MSTYLE_ALIGN_V						\
    ? a->v_align == b->v_align						\
    : (elem == MSTYLE_ALIGN_H						\
    ? a->h_align == b->h_align						\
    : (elem == MSTYLE_INDENT						\
    ? a->indent == b->indent						\
    : (elem == MSTYLE_ROTATION						\
    ? a->rotation == b->rotation					\
    : (elem == MSTYLE_TEXT_DIR						\
    ? a->text_dir == b->text_dir					\
    : (elem == MSTYLE_WRAP_TEXT						\
    ? a->wrap_text == b->wrap_text					\
    : (elem == MSTYLE_SHRINK_TO_FIT					\
    ? a->shrink_to_fit == b->shrink_to_fit				\
    : (elem == MSTYLE_CONTENTS_LOCKED					\
    ? a->contents_locked == b->contents_locked				\
    : (elem == MSTYLE_CONTENTS_HIDDEN					\
    ? a->contents_hidden == b->contents_hidden				\
    : (elem == MSTYLE_VALIDATION					\
    ? a->validation == b->validation					\
    : (elem == MSTYLE_HLINK						\
    ? a->hlink == b->hlink						\
    : (elem == MSTYLE_INPUT_MSG						\
    ? a->input_msg == b->input_msg					\
    : (elem == MSTYLE_CONDITIONS					\
    ? (a->conditions == b->conditions ||				\
       (a->conditions && b->conditions &&				\
	gnm_style_conditions_equal (a->conditions, b->conditions, FALSE)))	\
    : FALSE)))))))))))))))))))))))))

/*
 * Note: the above is suboptimal for validation, hlink, input_msg.
 *
 * We are comparing pointers (which at least safely matches what we do
 * with the hash), but I think we want proper equality.
 */

static gboolean
elem_is_eq (GnmStyle const *a, GnmStyle const *b, GnmStyleElement elem)
{
	return ELEM_IS_EQ (a, b, elem);
}

static void
elem_assign_contents (GnmStyle *dst, GnmStyle const *src, GnmStyleElement elem)
{
#ifdef DEBUG_STYLES
	g_return_if_fail (src != dst);
	g_return_if_fail (elem_is_set (src, elem));
#endif
	switch (elem) {
	case MSTYLE_COLOR_BACK :	style_color_ref (dst->color.back = src->color.back); return;
	case MSTYLE_COLOR_PATTERN :	style_color_ref (dst->color.pattern = src->color.pattern); return;
	case MSTYLE_ANY_BORDER:
		elem -= MSTYLE_BORDER_TOP;
		gnm_style_border_ref (dst->borders[elem] = src->borders[elem]);
		return;
	case MSTYLE_PATTERN:		dst->pattern = src->pattern; return;
	case MSTYLE_FONT_COLOR :	style_color_ref (dst->color.font = src->color.font); return;
	case MSTYLE_FONT_NAME:		go_string_ref (dst->font_detail.name = src->font_detail.name); return;
	case MSTYLE_FONT_BOLD:		dst->font_detail.bold = src->font_detail.bold; return;
	case MSTYLE_FONT_ITALIC:	dst->font_detail.italic = src->font_detail.italic; return;
	case MSTYLE_FONT_UNDERLINE:	dst->font_detail.underline = src->font_detail.underline; return;
	case MSTYLE_FONT_STRIKETHROUGH: dst->font_detail.strikethrough = src->font_detail.strikethrough; return;
	case MSTYLE_FONT_SCRIPT:	dst->font_detail.script = src->font_detail.script; return;
	case MSTYLE_FONT_SIZE:		dst->font_detail.size = src->font_detail.size; return;
	case MSTYLE_FORMAT:		go_format_ref (dst->format = src->format); return;
	case MSTYLE_ALIGN_V:		dst->v_align = src->v_align; return;
	case MSTYLE_ALIGN_H:		dst->h_align = src->h_align; return;
	case MSTYLE_INDENT:		dst->indent = src->indent; return;
	case MSTYLE_ROTATION:		dst->rotation = src->rotation; return;
	case MSTYLE_TEXT_DIR:		dst->text_dir = src->text_dir; return;
	case MSTYLE_WRAP_TEXT:		dst->wrap_text = src->wrap_text; return;
	case MSTYLE_SHRINK_TO_FIT:	dst->shrink_to_fit = src->shrink_to_fit; return;
	case MSTYLE_CONTENTS_LOCKED:	dst->contents_locked = src->contents_locked; return;
	case MSTYLE_CONTENTS_HIDDEN:	dst->contents_hidden = src->contents_hidden; return;
	case MSTYLE_VALIDATION:
		if ((dst->validation = src->validation))
			gnm_validation_ref (dst->validation);
		return;
	case MSTYLE_HLINK:
		if ((dst->hlink = src->hlink))
			g_object_ref (dst->hlink);
		return;
	case MSTYLE_INPUT_MSG:
		if ((dst->input_msg = src->input_msg))
			g_object_ref (dst->input_msg);
		return;
	case MSTYLE_CONDITIONS:
		if ((dst->conditions = src->conditions))
			g_object_ref (dst->conditions);
		return;
	default:
		;
	}
}

static void
elem_clear_contents (GnmStyle *style, GnmStyleElement elem)
{
#ifdef DEBUG_STYLES
	g_return_if_fail (style != NULL);
#endif
	if (!elem_is_set (style, elem))
		return;

	switch (elem) {
	case MSTYLE_COLOR_BACK :	style_color_unref (style->color.back); return;
	case MSTYLE_COLOR_PATTERN :	style_color_unref (style->color.pattern); return;
	case MSTYLE_ANY_BORDER:
		gnm_style_border_unref (style->borders[elem - MSTYLE_BORDER_TOP]);
		return;
	case MSTYLE_FONT_COLOR :	style_color_unref (style->color.font); return;
	case MSTYLE_FONT_NAME:		go_string_unref (style->font_detail.name); return;
	case MSTYLE_FORMAT:		go_format_unref (style->format); return;
	case MSTYLE_VALIDATION:
		if (style->validation) {
			gnm_validation_unref (style->validation);
			style->validation = NULL;
		}
		return;
	case MSTYLE_HLINK:
		g_clear_object (&style->hlink);
		return;
	case MSTYLE_INPUT_MSG:
		g_clear_object (&style->input_msg);
		return;
	case MSTYLE_CONDITIONS:
		if (style->conditions) {
			clear_conditional_merges (style);
			g_object_unref (style->conditions);
			style->conditions = NULL;
		}
		return;
	default:
		;
	}
}

/**
 * gnm_style_find_conflicts:
 * @accum: accumulator #GnmStyle
 * @overlay: #GnmStyle
 * @conflicts: flags
 *
 * Copy any items from @overlay that do not conflict with the values in @accum.
 * If an element had a previous conflict (flagged via @conflicts) it is ignored.
 *
 * Returns @conflicts with any new conflicts added.
 **/
unsigned int
gnm_style_find_conflicts (GnmStyle *accum, GnmStyle const *overlay,
			  unsigned int conflicts)
{
	int i;

	g_assert (MSTYLE_ELEMENT_MAX <= CHAR_BIT * sizeof (conflicts));

	for (i = 0; i < MSTYLE_ELEMENT_MAX; i++) {
		if (conflicts & (1u << i) || !elem_is_set (overlay, i)) {
			/* Nothing */
		} else if (!elem_is_set (accum, i)) {
			elem_assign_contents (accum, overlay, i);
			elem_set (accum, i);
			elem_changed (accum, i);
		} else if (!elem_is_eq (accum, overlay, i))
			conflicts |= (1u << i);
	}

	return conflicts;
}

#define GNM_INPUT_MSG_EQUAL3(a,b,r) (gnm_input_msg_equal (a,b))

#define RELAX_CHECK(op_,field_,checker_) do {			\
	if (diffs & (1u << (op_)) &&				\
	    elem_is_set (a, (op_)) &&				\
	    elem_is_set (b, (op_)) &&				\
	    ((a->field_ == NULL) != (b->field_ == NULL) ||	\
	     checker_ (a->field_, b->field_, relax_sheet)))	\
		diffs &= ~(1u << (op_));			\
	} while (0)

/**
 * gnm_style_find_differences:
 * @a: A #GnmStyle
 * @b: A #GnmStyle
 * @relax_sheet: if %TRUE, ignore differences solely caused by being linked into different sheets.
 *
 * Determine how two fully-qualified styles differ.
 *
 * Returns differences as a bitset of #GnmStyleElement.
 **/
unsigned int
gnm_style_find_differences (GnmStyle const *a, GnmStyle const *b,
			    gboolean relax_sheet)
{
	int i;
	unsigned int diffs = 0;

	g_assert (MSTYLE_ELEMENT_MAX <= CHAR_BIT * sizeof (diffs));

	for (i = 0; i < MSTYLE_ELEMENT_MAX; i++) {
		if (elem_is_set (a, i) != elem_is_set (b, i) ||
		    (elem_is_set (a, i) && !elem_is_eq (a, b, i)))
			diffs |= (1u << i);
	}

	if (relax_sheet) {
		RELAX_CHECK (MSTYLE_HLINK, hlink, gnm_hlink_equal);
		RELAX_CHECK (MSTYLE_VALIDATION, validation, gnm_validation_equal);
		RELAX_CHECK (MSTYLE_INPUT_MSG, input_msg, GNM_INPUT_MSG_EQUAL3);
		RELAX_CHECK (MSTYLE_CONDITIONS, conditions, gnm_style_conditions_equal);
	}

	return diffs;
}

#undef RELAX_CHECK
#undef GNM_INPUT_MSG_EQUAL3

static inline void
gnm_style_clear_pango (GnmStyle *style)
{
	if (style->pango_attrs) {
		pango_attr_list_unref (style->pango_attrs);
		style->pango_attrs = NULL;
	}
}


static inline void
gnm_style_clear_font (GnmStyle *style)
{
	if (style->font) {
		gnm_font_unref (style->font);
		style->font = NULL;
	}
	g_clear_object (&style->font_context);
}

/**
 * gnm_style_new:
 *
 * Returns: (transfer full): a new style with _no_ elements set.
 **/
GnmStyle *
gnm_style_new (void)
{
	GnmStyle *style = CHUNK_ALLOC0 (GnmStyle, gnm_style_pool);

	style->ref_count = 1;
	style->link_count = 0;
	style->linked_sheet = NULL;
	style->pango_attrs = NULL;
	style->font = NULL;
	style->validation = NULL;

	style->set = style->changed = 0;
	style->validation = NULL;
	style->hlink = NULL;
	style->input_msg = NULL;
	style->conditions = NULL;

	d(("new %p\n", style));

	return style;
}

/**
 * gnm_style_new_default:
 *
 * Returns: (transfer full): a new style initialized to the default state.
 **/
GnmStyle *
gnm_style_new_default (void)
{
	GnmStyle *new_style = gnm_style_new ();
	int i;

	gnm_style_set_font_name	  (new_style, gnm_conf_get_core_defaultfont_name ());
	gnm_style_set_font_size	  (new_style, gnm_conf_get_core_defaultfont_size ());
	gnm_style_set_font_bold	  (new_style, gnm_conf_get_core_defaultfont_bold ());
	gnm_style_set_font_italic (new_style, gnm_conf_get_core_defaultfont_italic ());

	gnm_style_set_format      (new_style, go_format_general ());
	gnm_style_set_align_v     (new_style, GNM_VALIGN_BOTTOM);
	gnm_style_set_align_h     (new_style, GNM_HALIGN_GENERAL);
	gnm_style_set_indent      (new_style, 0);
	gnm_style_set_rotation    (new_style, 0);
	gnm_style_set_text_dir    (new_style, GNM_TEXT_DIR_CONTEXT);
	gnm_style_set_wrap_text   (new_style, FALSE);
	gnm_style_set_shrink_to_fit (new_style, FALSE);
	gnm_style_set_contents_locked (new_style, TRUE);
	gnm_style_set_contents_hidden (new_style, FALSE);
	gnm_style_set_font_uline  (new_style, UNDERLINE_NONE);
	gnm_style_set_font_strike (new_style, FALSE);
	gnm_style_set_font_script (new_style, GO_FONT_SCRIPT_STANDARD);

	gnm_style_set_validation (new_style, NULL);
	gnm_style_set_hlink      (new_style, NULL);
	gnm_style_set_input_msg  (new_style, NULL);
	gnm_style_set_conditions (new_style, NULL);

	gnm_style_set_font_color (new_style, style_color_black ());
	gnm_style_set_back_color (new_style, style_color_auto_back ());
	gnm_style_set_pattern_color (new_style, style_color_black ());

	for (i = MSTYLE_BORDER_TOP; i <= MSTYLE_BORDER_DIAGONAL; ++i)
		gnm_style_set_border (new_style, i,
			gnm_style_border_ref (gnm_style_border_none ()));
	gnm_style_set_pattern (new_style, 0);

	return new_style;
}

GnmStyle *
gnm_style_dup (GnmStyle const *src)
{
	GnmStyle *new_style = CHUNK_ALLOC0 (GnmStyle, gnm_style_pool);
	int i;

	new_style->ref_count = 1;
	for (i = 0; i < MSTYLE_ELEMENT_MAX; i++)
		if (elem_is_set (src, i)) {
			elem_assign_contents (new_style, src, i);
			elem_set (new_style, i);
			elem_changed (new_style, i);
		}

	if ((new_style->pango_attrs = src->pango_attrs)) {
		pango_attr_list_ref (new_style->pango_attrs);
		new_style->pango_attrs_zoom = src->pango_attrs_zoom;
	}

	if ((new_style->font = src->font)) {
		gnm_font_ref (new_style->font);
		new_style->font_context = g_object_ref (src->font_context);
	}

	d(("dup %p\n", new_style));
	return new_style;
}

/**
 * gnm_style_new_merged:
 * @base: #GnmStyle
 * @overlay: #GnmStyle
 *
 * A new GnmStyle that contains any elements of @overlay that are set, and uses
 * @base for anything that is not set in @overlay.
 *
 * Returns: (transfer full): A ref to a new GnmStyle.
 **/
GnmStyle *
gnm_style_new_merged (GnmStyle const *base, GnmStyle const *overlay)
{
	GnmStyle *new_style = CHUNK_ALLOC0 (GnmStyle, gnm_style_pool);
	int i;

	new_style->ref_count = 1;
	for (i = 0; i < MSTYLE_ELEMENT_MAX; i++) {
		if (elem_is_set (overlay, i))
			elem_assign_contents (new_style, overlay, i);
		else if (elem_is_set (base, i))
			elem_assign_contents (new_style, base, i);
		else
			continue;
		elem_set (new_style, i);
		elem_changed (new_style, i);
	}
	d(("copy merge %p\n", new_style));
	return new_style;
}

/**
 * gnm_style_ref: (skip)
 * @style: #GnmStyle
 *
 * Returns: (transfer full): A new reference to @style.
 */
GnmStyle *
gnm_style_ref (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, NULL);
	g_return_val_if_fail (style->ref_count > 0, NULL);

	((GnmStyle *)style)->ref_count++;
	d(("ref %p = %d\n", style, style->ref_count));

	return ((GnmStyle *)style);
}

/**
 * gnm_style_unref: (skip)
 * @style: #GnmStyle const
 *
 * Unrefs and _potentially frees_ @style.
 * Takes a _const_ pointer to facilitate life cycles.  The const indicates that
 * the content cannot be changed, mainly when handling styles that are in the
 * style hash.
 **/
void
gnm_style_unref (GnmStyle const *style)
{
	g_return_if_fail (style != NULL);
	g_return_if_fail (style->ref_count > 0);

	d(("unref %p = %d\n", style, style->ref_count-1));
	if (((GnmStyle *)style)->ref_count-- <= 1) {
		GnmStyle *unconst = (GnmStyle *)style;
		int i;

		g_return_if_fail (style->link_count == 0);
		g_return_if_fail (style->linked_sheet == NULL);

		for (i = 0; i < MSTYLE_ELEMENT_MAX; i++)
			elem_clear_contents (unconst, i);
		unconst->set = 0;
		clear_conditional_merges (unconst);
		gnm_style_clear_pango (unconst);
		gnm_style_clear_font (unconst);

		CHUNK_FREE (gnm_style_pool, unconst);
	}
}

GType
gnm_style_get_type (void)
{
	static GType t = 0;

	if (t == 0) {
		t = g_boxed_type_register_static ("GnmStyle",
			 (GBoxedCopyFunc)gnm_style_ref,
			 (GBoxedFreeFunc)gnm_style_unref);
	}
	return t;
}

/*
 * Replace auto pattern color in style with sheet's auto pattern color.
 * make_copy tells if we are allowed to modify the style in place or we must
 * make a copy first.
 */
static GnmStyle *
link_pattern_color (GnmStyle *style, GnmColor *auto_color, gboolean make_copy)
{
	GnmColor *pattern_color = style->color.pattern;

	if (pattern_color->is_auto && auto_color != pattern_color) {
		style_color_ref (auto_color);
		if (make_copy) {
			GnmStyle *orig = style;
			style = gnm_style_dup (style);
			gnm_style_unref (orig);
		}
		gnm_style_set_pattern_color (style, auto_color);
	}
	return style;
}

/*
 * Replace auto border colors in style with sheet's auto pattern
 * color. (pattern is *not* a typo.)
 * make_copy tells if we are allowed to modify the style in place or we must
 * make a copy first.
 *
 * FIXME: We conjecture that XL color 64 in border should change with the
 * pattern, but not color 127. That distinction is not yet represented in
 * our data structures.
 */
static GnmStyle *
link_border_colors (GnmStyle *style, GnmColor *auto_color, gboolean make_copy)
{
	int i;

	for (i = MSTYLE_BORDER_TOP; i <= MSTYLE_BORDER_DIAGONAL; ++i) {
		if (elem_is_set (style, i)) {
			GnmBorder *border =
				style->borders[i- MSTYLE_BORDER_TOP];
			GnmColor *color;

			if (!border)
				continue;

			color = border->color;
			if (color->is_auto && auto_color != color) {
				GnmBorder *new_border;
				GnmStyleBorderOrientation orientation;

				switch (i) {
				case MSTYLE_BORDER_LEFT:
				case MSTYLE_BORDER_RIGHT:
					orientation = GNM_STYLE_BORDER_VERTICAL;
					break;
				case MSTYLE_BORDER_REV_DIAGONAL:
				case MSTYLE_BORDER_DIAGONAL:
					orientation = GNM_STYLE_BORDER_DIAGONAL;
					break;
				case MSTYLE_BORDER_TOP:
				case MSTYLE_BORDER_BOTTOM:
				default:
					orientation = GNM_STYLE_BORDER_HORIZONTAL;
					break;
				}
				style_color_ref (auto_color);
				new_border = gnm_style_border_fetch (
					border->line_type, auto_color,
					orientation);

				if (make_copy) {
					GnmStyle *orig = style;
					style = gnm_style_dup (style);
					gnm_style_unref (orig);
					make_copy = FALSE;
				}
				gnm_style_set_border (style, i, new_border);
			}
		}
	}
	return style;
}

static void
gnm_style_linked_sheet_changed (GnmStyle *style)
{
	Sheet *sheet = style->linked_sheet;

	if (elem_is_set (style, MSTYLE_VALIDATION) &&
	    style->validation &&
	    gnm_validation_get_sheet (style->validation) != sheet) {
		GnmValidation *new_v = gnm_validation_dup_to (style->validation, sheet);
		gnm_style_set_validation (style, new_v);
	}

	if (elem_is_set (style, MSTYLE_HLINK) &&
	    style->hlink &&
	    gnm_hlink_get_sheet (style->hlink) != sheet) {
		GnmHLink *new_l = gnm_hlink_dup_to (style->hlink, sheet);
		gnm_style_set_hlink (style, new_l);
	}

	if (elem_is_set (style, MSTYLE_CONDITIONS) &&
	    style->conditions &&
	    gnm_style_conditions_get_sheet (style->conditions) != sheet) {
		GnmStyleConditions *new_c, *new_sc;

		sheet_conditions_share_conditions_remove (style->conditions);
		new_c = gnm_style_conditions_dup_to (style->conditions, sheet);
		new_sc = sheet_conditions_share_conditions_add (new_c);
		if (new_sc) {
			g_object_unref (new_c);
			new_c = g_object_ref (new_sc);
		}
		gnm_style_set_conditions (style, new_c);
	}
}

/**
 * gnm_style_link_sheet:
 * @style: (transfer full): the style for which we need to set the sheet
 * @sheet: the new sheet
 *
 * Returns: (transfer full): new style which may or may not be identical
 * to the incoming.
 *
 * ABSORBS a reference to the style and sets the link count to 1.
 *
 * Where auto pattern color occurs in the style (it may for pattern and
 * borders), it is replaced with the sheet's auto pattern color. We make
 * sure that we do not modify the style which was passed in to us, but also
 * that we don't copy more than once. The final argument to the
 * link_xxxxx_color functions tell whether or not to copy.
 */
GnmStyle *
gnm_style_link_sheet (GnmStyle *style, Sheet *sheet)
{
	GnmColor *auto_color;
	gboolean style_is_orig = TRUE;

	if (style->linked_sheet != NULL) {
		GnmStyle *orig = style;
		style = gnm_style_dup (style);
		gnm_style_unref (orig);
		style_is_orig = FALSE;

		/* safety test */
		g_return_val_if_fail (style->linked_sheet != sheet, style);
	}

	g_return_val_if_fail (style->link_count == 0, style);
	g_return_val_if_fail (style->linked_sheet == NULL, style);

	auto_color = sheet_style_get_auto_pattern_color (sheet);
	if (elem_is_set (style, MSTYLE_COLOR_PATTERN))
		style = link_pattern_color (style, auto_color, style_is_orig);
	style = link_border_colors (style, auto_color, style_is_orig);
	style_color_unref (auto_color);

	if (elem_is_set (style, MSTYLE_CONDITIONS) && style->conditions) {
		// We actually change the style here, but the resulting
		// ->conditions should be equivalent.
		GnmStyleConditions *sc_new = sheet_conditions_share_conditions_add (style->conditions);
		if (sc_new)
			gnm_style_set_conditions (style, g_object_ref (sc_new));
	}

	style->linked_sheet = sheet;
	style->link_count = 1;

	gnm_style_linked_sheet_changed (style);

	d(("link sheet %p = 1\n", style));
	return style;
}

void
gnm_style_link (GnmStyle *style)
{
	g_return_if_fail (style->link_count > 0);

	style->link_count++;
	d(("link %p = %d\n", style, style->link_count));
}

void
gnm_style_unlink (GnmStyle *style)
{
	g_return_if_fail (style->link_count > 0);

	d(("unlink %p = %d\n", style, style->link_count-1));
	if (style->link_count-- == 1) {
		if (elem_is_set (style, MSTYLE_CONDITIONS) && style->conditions)
			sheet_conditions_share_conditions_remove (style->conditions);
		sheet_style_unlink (style->linked_sheet, style);
		style->linked_sheet = NULL;
		gnm_style_unref (style);
	}
}

// Internal function for sheet-style.c use only
void
gnm_style_abandon_link (GnmStyle *style)
{
	style->link_count = 0;
	style->linked_sheet = NULL;
}

gboolean
gnm_style_eq (GnmStyle const *a, GnmStyle const *b)
{
	return a == b;
}

gboolean
gnm_style_equal (GnmStyle const *a, GnmStyle const *b)
{
	int i;

	if (a == b)
		return TRUE;
	if (a->set != b->set || !gnm_style_equal_XL (a, b))
		return FALSE;
	UNROLLED_FOR (i = MSTYLE_VALIDATION, i < MSTYLE_ELEMENT_MAX, i++, {
		if (elem_is_set (a, i) && !ELEM_IS_EQ (a, b, i))
			return FALSE;
	});

	return TRUE;
}

gboolean
gnm_style_equal_XL (GnmStyle const *a, GnmStyle const *b)
{
	int i;

	g_return_val_if_fail (a != NULL, FALSE);
	g_return_val_if_fail (b != NULL, FALSE);

	if (a == b)
		return TRUE;

	if ((a->set ^ b->set) & ((1u << MSTYLE_VALIDATION) - 1))
		return FALSE;

	UNROLLED_FOR (i = MSTYLE_COLOR_BACK, i < MSTYLE_VALIDATION, i++, {
		if (elem_is_set (a, i) && !ELEM_IS_EQ (a, b, i))
			return FALSE;
	});
	return TRUE;
}

/**
 * gnm_style_equal_elem:
 * @a: first style
 * @b: second style
 * @e: style element
 *
 * Returns: %TRUE, if the two styles have the same contents for the
 * given element, either because neither have it set, or because both
 * have it set and to the same value.
 */
gboolean
gnm_style_equal_elem (GnmStyle const *a, GnmStyle const *b, GnmStyleElement e)
{
	if (elem_is_set (a, e))
		return elem_is_set (b, e) && elem_is_eq (a, b, e);
	else
		return !elem_is_set (b, e);
}



#define CMP_TRY_NUMBER_RAW(a_,b_)		\
  do {						\
    if ((a_) < (b_)) return -1;			\
    if ((a_) > (b_)) return -1;			\
  } while (0)

#define CMP_TRY_NUMBER(e_,f_)			\
  do {						\
    if (elem_is_set (a, (e_)))			\
      CMP_TRY_NUMBER_RAW(a->f_, b->f_);		\
  } while (0)

#define CMP_TRY_COLOR(e_,f_)					\
  do {								\
    if (elem_is_set (a, (e_))) {				\
      CMP_TRY_NUMBER_RAW(a->f_->is_auto, b->f_->is_auto);	\
      CMP_TRY_NUMBER_RAW(a->f_->go_color, b->f_->go_color);	\
    }								\
  } while (0)

/*
 * Ordering of GnmStyles.  Apart from FIXMEs, this shouldn't change
 * from one run to the next.
 */
int
gnm_style_cmp (GnmStyle const *a, GnmStyle const *b)
{
	GnmStyleElement e;

	if (a == b)
		return 0;

	/*
	 * Very quick comparison based on what is set.  This also allows
	 * us to check on one elem_is_set below.
	 */
	CMP_TRY_NUMBER_RAW (a->set, b->set);

	CMP_TRY_COLOR (MSTYLE_FONT_COLOR, color.font);
	CMP_TRY_COLOR (MSTYLE_COLOR_BACK, color.back);
	CMP_TRY_COLOR (MSTYLE_COLOR_PATTERN, color.pattern);
	for (e = MSTYLE_BORDER_TOP; e <= MSTYLE_BORDER_DIAGONAL; e++) {
		GnmBorder const *ba, *bb;
		if (!elem_is_set (a, e))
			continue;
		ba = a->borders[e - MSTYLE_BORDER_TOP];
		bb = b->borders[e - MSTYLE_BORDER_TOP];
		if (ba == bb)
			continue;  /* Handles both being NULL */
		CMP_TRY_NUMBER_RAW(!!ba, !!bb);
		CMP_TRY_NUMBER_RAW(ba->line_type, bb->line_type);
		CMP_TRY_NUMBER_RAW(ba->color->go_color, bb->color->go_color);
		CMP_TRY_NUMBER_RAW(ba->begin_margin, bb->begin_margin);
		CMP_TRY_NUMBER_RAW(ba->end_margin, bb->end_margin);
		CMP_TRY_NUMBER_RAW(ba->width, bb->width);
	}
	CMP_TRY_NUMBER (MSTYLE_PATTERN, pattern);
	if (elem_is_set (a, MSTYLE_FONT_NAME)) {
		/* Plain strcmp, not utf-8.  We need to see diffs.  */
		int tmp = strcmp (a->font_detail.name->str,
				  b->font_detail.name->str);
		if (tmp)
			return tmp;
	}
	CMP_TRY_NUMBER (MSTYLE_FONT_BOLD, font_detail.bold);
	CMP_TRY_NUMBER (MSTYLE_FONT_ITALIC, font_detail.italic);
	CMP_TRY_NUMBER (MSTYLE_FONT_UNDERLINE, font_detail.underline);
	CMP_TRY_NUMBER (MSTYLE_FONT_STRIKETHROUGH, font_detail.strikethrough);
	CMP_TRY_NUMBER (MSTYLE_FONT_SCRIPT, font_detail.script);
	CMP_TRY_NUMBER (MSTYLE_FONT_SIZE, font_detail.size);
	if (elem_is_set (a, MSTYLE_FORMAT)) {
		/* Plain strcmp, not utf-8.  We need to see diffs.  */
		int tmp = strcmp (go_format_as_XL (a->format),
				  go_format_as_XL (b->format));
		if (tmp)
			return tmp;
	}
	CMP_TRY_NUMBER (MSTYLE_ALIGN_H, h_align);
	CMP_TRY_NUMBER (MSTYLE_ALIGN_V, v_align);
	CMP_TRY_NUMBER (MSTYLE_INDENT, indent);
	CMP_TRY_NUMBER (MSTYLE_ROTATION, rotation);
	CMP_TRY_NUMBER (MSTYLE_TEXT_DIR, text_dir);
	CMP_TRY_NUMBER (MSTYLE_WRAP_TEXT, wrap_text);
	CMP_TRY_NUMBER (MSTYLE_SHRINK_TO_FIT, shrink_to_fit);
	CMP_TRY_NUMBER (MSTYLE_CONTENTS_LOCKED, contents_locked);
	CMP_TRY_NUMBER (MSTYLE_CONTENTS_HIDDEN, contents_hidden);
	/* FIXME: validation */
	/* FIXME: hlink */
	/* FIXME: input_msg */
	/* FIXME: conditions */
	/* FIXME: cond_styles */

	/* Last resort: pointer comparison.  */
	return a < b ? -1 : +1;
}

#undef CMP_TRY_NUMBER_RAW
#undef CMP_TRY_NUMBER
#undef CMP_TRY_COLOR


/**
 * gnm_style_equal_header:
 * @a: #GnmStyle
 * @b: #GnmStyle
 * @top: is this a header vertically or horizontally
 *
 * Check to see if @a is different enough from @b to make us think that @a is
 * from a header.
 **/
gboolean
gnm_style_equal_header (GnmStyle const *a, GnmStyle const *b, gboolean top)
{
	int i = top ? MSTYLE_BORDER_BOTTOM : MSTYLE_BORDER_RIGHT;

	if (!elem_is_eq (a, b, i))
		return FALSE;
	for (i = MSTYLE_COLOR_BACK; i <= MSTYLE_COLOR_PATTERN ; i++)
		if (!elem_is_eq (a, b, i))
			return FALSE;
	for (i = MSTYLE_FONT_COLOR; i <= MSTYLE_SHRINK_TO_FIT ; i++)
		if (!elem_is_eq (a, b, i))
			return FALSE;
	return TRUE;
}


gboolean
gnm_style_is_element_set (GnmStyle const *style, GnmStyleElement elem)
{
	g_return_val_if_fail (style != NULL, FALSE);
	g_return_val_if_fail (MSTYLE_COLOR_BACK <= elem && elem < MSTYLE_ELEMENT_MAX, FALSE);
	return elem_is_set (style, elem);
}

/**
 * gnm_style_is_complete:
 * @style: #GnmStyle to query
 *
 * Returns: %TRUE if all elements are set.
 **/
gboolean
gnm_style_is_complete (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, FALSE);

	return style->set == ((1u << MSTYLE_ELEMENT_MAX) - 1);
}

void
gnm_style_unset_element (GnmStyle *style, GnmStyleElement elem)
{
	g_return_if_fail (style != NULL);
	g_return_if_fail (MSTYLE_COLOR_BACK <= elem && elem < MSTYLE_ELEMENT_MAX);

	if (elem_is_set (style, elem)) {
		elem_clear_contents (style, elem);
		elem_unset (style, elem);
	}
}

/**
 * gnm_style_merge:
 * @base: #GnmStyle
 * @overlay: #GnmStyle
 *
 * Applies all active elements of @overlay onto @base.
 **/
void
gnm_style_merge (GnmStyle *base, GnmStyle const *overlay)
{
	unsigned i;
	if (base == overlay)
		return;
	for (i = 0; i < MSTYLE_ELEMENT_MAX; i++)
		if (elem_is_set (overlay, i)) {
			elem_clear_contents (base, i);
			elem_assign_contents (base, overlay, i);
			elem_set (base, i);
			elem_changed (base, i);
		}
}

/**
 * gnm_style_merge_element:
 * @dst: Destination style
 * @src: Source style
 * @elem: Element to replace
 *
 * This function replaces element @elem in style @dst with element @elem
 * in style @src. (If element @elem was already set in style @dst then
 * the element will first be unset)
 **/
void
gnm_style_merge_element (GnmStyle *dst, GnmStyle const *src, GnmStyleElement elem)
{
	g_return_if_fail (src != NULL);
	g_return_if_fail (dst != NULL);
	g_return_if_fail (src != dst);

	if (elem_is_set (src, elem)) {
		elem_clear_contents (dst, elem);
		elem_assign_contents (dst, src, elem);
		elem_set (dst, elem);
		elem_changed (dst, elem);
	}
}

/**
 * gnm_style_set_font_color:
 * @style: #GnmStyle to change
 * @col: (transfer full): #GnmColor
 *
 * Set the color used for fonts.
 */
void
gnm_style_set_font_color (GnmStyle *style, GnmColor *col)
{
	g_return_if_fail (style != NULL);
	g_return_if_fail (col != NULL);

	elem_changed (style, MSTYLE_FONT_COLOR);
	if (elem_is_set (style, MSTYLE_FONT_COLOR))
		style_color_unref (style->color.font);
	else
		elem_set (style, MSTYLE_FONT_COLOR);
	elem_changed (style, MSTYLE_FONT_COLOR);
	style->color.font = col;
	gnm_style_clear_pango (style);
}

/**
 * gnm_style_set_back_color:
 * @style: #GnmStyle to change
 * @col: (transfer full): #GnmColor
 *
 * Assigns @col as the background of @style.
 *
 * NOTE: the background colour is only visible if GnmStyle::pattern > 0
 **/
void
gnm_style_set_back_color (GnmStyle *style, GnmColor *col)
{
	g_return_if_fail (style != NULL);
	g_return_if_fail (col != NULL);

	elem_changed (style, MSTYLE_COLOR_BACK);
	if (elem_is_set (style, MSTYLE_COLOR_BACK))
		style_color_unref (style->color.back);
	else
		elem_set (style, MSTYLE_COLOR_BACK);
	style->color.back = col;
	gnm_style_clear_pango (style);
}

/**
 * gnm_style_set_pattern_color:
 * @style: #GnmStyle to change
 * @col: (transfer full): #GnmColor
 *
 * Set the color used for pattern.
 */
void
gnm_style_set_pattern_color (GnmStyle *style, GnmColor *col)
{
	g_return_if_fail (style != NULL);
	g_return_if_fail (col != NULL);

	elem_changed (style, MSTYLE_COLOR_PATTERN);
	if (elem_is_set (style, MSTYLE_COLOR_PATTERN))
		style_color_unref (style->color.pattern);
	else
		elem_set (style, MSTYLE_COLOR_PATTERN);
	style->color.pattern = col;
	gnm_style_clear_pango (style);
}

/**
 * gnm_style_get_font_color:
 * @style: #GnmStyle to query
 *
 * Returns: (transfer none) (nullable): #GnmColor used for font.
 */
GnmColor *
gnm_style_get_font_color (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, NULL);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_FONT_COLOR), NULL);
	return style->color.font;
}

/**
 * gnm_style_get_back_color:
 * @style: #GnmStyle to query
 *
 * Returns: (transfer none) (nullable): #GnmColor used for background.
 */
GnmColor *
gnm_style_get_back_color (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, NULL);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_COLOR_BACK), NULL);
	return style->color.back;
}

/**
 * gnm_style_get_pattern_color:
 * @style: #GnmStyle to query
 *
 * Returns: (transfer none) (nullable): #GnmColor used for pattern.
 */
GnmColor *
gnm_style_get_pattern_color (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, NULL);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_COLOR_PATTERN), NULL);
	return style->color.pattern;
}

/**
 * gnm_style_set_border:
 * @style: #GnmStyle to change
 * @elem: Border element
 * @border: (transfer full) (nullable): new #GnmBorder for @style.
 */
void
gnm_style_set_border (GnmStyle *style, GnmStyleElement elem,
		      GnmBorder *border)
{
	g_return_if_fail (style != NULL);

	/* NOTE : It is legal for border to be NULL */
	switch (elem) {
	case MSTYLE_ANY_BORDER:
		elem_changed (style, elem);
		elem_set (style, elem);
		elem -= MSTYLE_BORDER_TOP;
		gnm_style_border_unref (style->borders[elem]);
		style->borders[elem] = border;
		break;
	default:
		g_warning ("Not a border element");
		break;
	}
}

/**
 * gnm_style_get_border:
 * @style: #GnmStyle to query
 * @elem: Border element
 *
 * Returns: (transfer none) (nullable): The #GnmBorder for a single
 * border element.
 */
GnmBorder *
gnm_style_get_border (GnmStyle const *style, GnmStyleElement elem)
{
	g_return_val_if_fail (style != NULL, NULL);

	switch (elem) {
	case MSTYLE_ANY_BORDER:
		return style->borders[elem - MSTYLE_BORDER_TOP ];

	default:
		g_warning ("Not a border element");
		return NULL;
	}
}

/**
 * gnm_style_set_pattern:
 * @style: #GnmStyle to change
 * @pattern: pattern code
 **/
void
gnm_style_set_pattern (GnmStyle *style, int pattern)
{
	g_return_if_fail (style != NULL);
	g_return_if_fail (pattern >= 0);
	g_return_if_fail (pattern < GNM_PATTERNS_MAX);

	elem_changed (style, MSTYLE_PATTERN);
	elem_set (style, MSTYLE_PATTERN);
	style->pattern = pattern;
}

int
gnm_style_get_pattern (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, 0);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_PATTERN), 0);

	return style->pattern;
}

/**
 * gnm_style_get_font:
 * @style: #GnmStyle to query
 * @context: #PangoContext
 *
 * Returns: (transfer none): GnmFont implied by @style.
 **/
GnmFont *
gnm_style_get_font (GnmStyle const *style, PangoContext *context)
{
	g_return_val_if_fail (style != NULL, NULL);

	if (!style->font || style->font_context != context) {
		char const *name;
		gboolean bold, italic;
		double size;

		gnm_style_clear_font ((GnmStyle *)style);

		if (elem_is_set (style, MSTYLE_FONT_NAME))
			name = gnm_style_get_font_name (style);
		else
			name = DEFAULT_FONT;

		if (elem_is_set (style, MSTYLE_FONT_BOLD))
			bold = gnm_style_get_font_bold (style);
		else
			bold = FALSE;

		if (elem_is_set (style, MSTYLE_FONT_ITALIC))
			italic = gnm_style_get_font_italic (style);
		else
			italic = FALSE;

		if (elem_is_set (style, MSTYLE_FONT_SIZE))
			size = gnm_style_get_font_size (style);
		else
			size = DEFAULT_SIZE;

		((GnmStyle *)style)->font =
			gnm_font_new (context, name, size, bold, italic);
		((GnmStyle *)style)->font_context = g_object_ref (context);
	}

	return style->font;
}

/**
 * gnm_style_set_font_name:
 * @style: #GnmStyle to change
 * @name: the font name as a string
 */
void
gnm_style_set_font_name (GnmStyle *style, char const *name)
{
	g_return_if_fail (name != NULL);
	g_return_if_fail (style != NULL);

	elem_changed (style, MSTYLE_FONT_NAME);
	if (elem_is_set (style, MSTYLE_FONT_NAME))
		go_string_unref (style->font_detail.name);
	else
		elem_set (style, MSTYLE_FONT_NAME);
	style->font_detail.name = go_string_new (name);
	gnm_style_clear_font (style);
	gnm_style_clear_pango (style);
}

/**
 * gnm_style_get_font_name:
 * @style: the style to query
 *
 * Returns: (transfer none): the currently set font name
 */
char const *
gnm_style_get_font_name (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, NULL);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_FONT_NAME), NULL);

	return style->font_detail.name->str;
}

/**
 * gnm_style_set_font_bold:
 * @style: #GnmStyle to change
 * @bold: %TRUE for bold, %FALSE for regular
 */
void
gnm_style_set_font_bold (GnmStyle *style, gboolean bold)
{
	g_return_if_fail (style != NULL);

	elem_changed (style, MSTYLE_FONT_BOLD);
	elem_set (style, MSTYLE_FONT_BOLD);
	style->font_detail.bold = !!bold;
	gnm_style_clear_font (style);
	gnm_style_clear_pango (style);
}

/**
 * gnm_style_get_font_bold:
 * @style: #GnmStyle to query
 *
 * Returns: %TRUE if the style has a bold font.
 */
gboolean
gnm_style_get_font_bold (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, FALSE);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_FONT_BOLD), FALSE);

	return style->font_detail.bold;
}

/**
 * gnm_style_set_font_italic:
 * @style: #GnmStyle to change
 * @italic: %TRUE for italic, %FALSE for regular
 */
void
gnm_style_set_font_italic (GnmStyle *style, gboolean italic)
{
	g_return_if_fail (style != NULL);

	elem_changed (style, MSTYLE_FONT_ITALIC);
	elem_set (style, MSTYLE_FONT_ITALIC);
	style->font_detail.italic = !!italic;
	gnm_style_clear_font (style);
	gnm_style_clear_pango (style);
}

/**
 * gnm_style_get_font_italic:
 * @style: #GnmStyle to query
 *
 * Returns: %TRUE if the style has an italic font.
 */
gboolean
gnm_style_get_font_italic (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, FALSE);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_FONT_ITALIC), FALSE);

	return style->font_detail.italic;
}

/**
 * gnm_style_set_font_uline:
 * @style: #GnmStyle to change
 * @ul: #GnmUnderline specifying type of underlining
 **/
void
gnm_style_set_font_uline (GnmStyle *style, GnmUnderline const underline)
{
	g_return_if_fail (style != NULL);
	g_return_if_fail (underline >= UNDERLINE_NONE && underline <= UNDERLINE_DOUBLE_LOW);

	elem_changed (style, MSTYLE_FONT_UNDERLINE);
	elem_set (style, MSTYLE_FONT_UNDERLINE);
	style->font_detail.underline = underline;
	gnm_style_clear_pango (style);
}

/**
 * gnm_style_get_font_uline:
 * @style: #GnmStyle to query
 *
 * Returns: #GnmUnderline specifying type of underlining
 **/
GnmUnderline
gnm_style_get_font_uline (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, UNDERLINE_NONE);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_FONT_UNDERLINE), UNDERLINE_NONE);

	return style->font_detail.underline;
}

/**
 * gnm_style_set_font_strike:
 * @style: #GnmStyle to change
 * @strike: %TRUE for strikethrough, %FALSE for regular
 */
void
gnm_style_set_font_strike (GnmStyle *style, gboolean strikethrough)
{
	g_return_if_fail (style != NULL);

	elem_changed (style, MSTYLE_FONT_STRIKETHROUGH);
	elem_set (style, MSTYLE_FONT_STRIKETHROUGH);
	style->font_detail.strikethrough = !!strikethrough;
	gnm_style_clear_pango (style);
}

/**
 * gnm_style_get_font_strike:
 * @style: #GnmStyle to query
 *
 * Returns: %TRUE for strikethrough, %FALSE for regular
 */
gboolean
gnm_style_get_font_strike (GnmStyle const *style)
{
	g_return_val_if_fail (elem_is_set (style, MSTYLE_FONT_STRIKETHROUGH), FALSE);

	return style->font_detail.strikethrough;
}

/**
 * gnm_style_set_font_script:
 * @style: #GnmStyle to change
 * @script: #GOFontScript specifying super or subscript
 **/
void
gnm_style_set_font_script (GnmStyle *style, GOFontScript script)
{
	g_return_if_fail (style != NULL);
	elem_changed (style, MSTYLE_FONT_SCRIPT);
	elem_set (style, MSTYLE_FONT_SCRIPT);
	style->font_detail.script = script;
	gnm_style_clear_pango (style);
}

/**
 * gnm_style_get_font_script:
 * @style: #GnmStyle to query
 *
 * Returns: #GOFontScript specifying super or subscript
 **/
GOFontScript
gnm_style_get_font_script (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, GO_FONT_SCRIPT_STANDARD);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_FONT_SCRIPT), GO_FONT_SCRIPT_STANDARD);

	return style->font_detail.script;
}

/**
 * gnm_style_set_font_size:
 * @style: #GnmStyle to change
 * @size: Font size in points
 **/
void
gnm_style_set_font_size (GnmStyle *style, double size)
{
	g_return_if_fail (style != NULL);
	g_return_if_fail (size >= 1.);
	elem_changed (style, MSTYLE_FONT_SIZE);
	elem_set (style, MSTYLE_FONT_SIZE);
	style->font_detail.size = size;
	gnm_style_clear_font (style);
	gnm_style_clear_pango (style);
}

/**
 * gnm_style_get_font_size:
 * @style: #GnmStyle to query
 *
 * Returns: Font size in points
 **/
double
gnm_style_get_font_size (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, 12.0);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_FONT_SIZE), 12.0);

	return style->font_detail.size;
}

/**
 * gnm_style_set_format:
 * @style: #GnmStyle to change
 * @fmt: #GOFormat
 */
void
gnm_style_set_format (GnmStyle *style, GOFormat const *fmt)
{
	g_return_if_fail (style != NULL);
	g_return_if_fail (fmt != NULL);

	elem_changed (style, MSTYLE_FORMAT);
	go_format_ref (fmt);
	elem_clear_contents (style, MSTYLE_FORMAT);
	elem_set (style, MSTYLE_FORMAT);
	style->format = fmt;
}

/*
 * gnm_style_set_format_text:
 * @style: mstyle to change.
 * @format: An *untranslated* format string.
 */
void
gnm_style_set_format_text (GnmStyle *style, char const *format)
{
	GOFormat *sf;

	g_return_if_fail (style != NULL);
	g_return_if_fail (format != NULL);

	sf = go_format_new_from_XL (format);
	gnm_style_set_format (style, sf);
	go_format_unref (sf);
}

/**
 * gnm_style_get_format:
 * @style: #GnmStyle to query
 *
 * Returns: (transfer none): #GOFormat
 */
const GOFormat *
gnm_style_get_format (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, NULL);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_FORMAT), NULL);

	return style->format;
}

/**
 * gnm_style_set_align_h:
 * @style: #GnmStyle to change
 * @a: A #GnmHAlign
 **/
void
gnm_style_set_align_h (GnmStyle *style, GnmHAlign a)
{
	g_return_if_fail (style != NULL);

	elem_changed (style, MSTYLE_ALIGN_H);
	elem_set (style, MSTYLE_ALIGN_H);
	style->h_align = a;
}

/**
 * gnm_style_get_align_h:
 * @style: #GnmStyle to query
 *
 * Returns: A #GnmHAlign
 **/
GnmHAlign
gnm_style_get_align_h (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, GNM_HALIGN_LEFT);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_ALIGN_H), GNM_HALIGN_LEFT);

	return style->h_align;
}

/**
 * gnm_style_set_align_v:
 * @style: #GnmStyle to change
 * @a: A #GnmVAlign
 **/
void
gnm_style_set_align_v (GnmStyle *style, GnmVAlign a)
{
	g_return_if_fail (style != NULL);

	elem_changed (style, MSTYLE_ALIGN_V);
	elem_set (style, MSTYLE_ALIGN_V);
	style->v_align = a;
}

/**
 * gnm_style_get_align_v:
 * @style: #GnmStyle to query
 *
 * Returns: A #GnmVAlign
 **/
GnmVAlign
gnm_style_get_align_v (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, GNM_VALIGN_TOP);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_ALIGN_V), GNM_VALIGN_TOP);

	return style->v_align;
}

/**
 * gnm_style_set_indent:
 * @style: #GnmStyle to change
 * @i: Indentation amount
 **/
void
gnm_style_set_indent (GnmStyle *style, int i)
{
	g_return_if_fail (style != NULL);

	elem_changed (style, MSTYLE_INDENT);
	elem_set (style, MSTYLE_INDENT);
	style->indent = i;
}

/**
 * gnm_style_get_indent:
 * @style: #GnmStyle to query
 *
 * Returns: Indentation amount
 **/
int
gnm_style_get_indent (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, 0);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_INDENT), 0);

	return style->indent;
}

/**
 * gnm_style_set_rotation:
 * @style: #GnmStyle to change
 * @r: Rotation in degrees relative to horizontal
 **/
void
gnm_style_set_rotation (GnmStyle *style, int rot_deg)
{
	g_return_if_fail (style != NULL);

	elem_changed (style, MSTYLE_ROTATION);
	elem_set (style, MSTYLE_ROTATION);
	style->rotation = rot_deg;
}

/**
 * gnm_style_get_rotation:
 * @style: #GnmStyle to query
 *
 * Returns: Rotation in degrees relative to horizontal
 **/
int
gnm_style_get_rotation (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, 0);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_ROTATION), 0);

	return style->rotation;
}

/**
 * gnm_style_set_text_dir:
 * @style: #GnmStyle to change
 * @text_dir: A #GnmTextDir
 **/
void
gnm_style_set_text_dir (GnmStyle *style, GnmTextDir text_dir)
{
	g_return_if_fail (style != NULL);

	elem_changed (style, MSTYLE_TEXT_DIR);
	elem_set (style, MSTYLE_TEXT_DIR);
	style->text_dir = text_dir;
}

/**
 * gnm_style_get_text_dir:
 * @style: #GnmStyle to query
 *
 * Returns: A #GnmTextDir
 **/
GnmTextDir
gnm_style_get_text_dir (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, GNM_TEXT_DIR_CONTEXT);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_TEXT_DIR), GNM_TEXT_DIR_CONTEXT);

	return style->text_dir;
}

/**
 * gnm_style_set_wrap_text:
 * @style: #GnmStyle to change
 * @f: %TRUE for wrapping, %FALSE for not
 **/
void
gnm_style_set_wrap_text (GnmStyle *style, gboolean f)
{
	g_return_if_fail (style != NULL);

	elem_changed (style, MSTYLE_WRAP_TEXT);
	elem_set (style, MSTYLE_WRAP_TEXT);
	style->wrap_text = !!f;
}

/**
 * gnm_style_get_wrap_text:
 * @style: #GnmStyle to query
 *
 * Returns: %TRUE for wrapping, %FALSE for not.  See also
 * gnm_style_get_effective_wrap_text.
 **/
gboolean
gnm_style_get_wrap_text (GnmStyle const *style)
{
	g_return_val_if_fail (elem_is_set (style, MSTYLE_WRAP_TEXT), FALSE);

	return style->wrap_text;
}

/**
 * gnm_style_get_effective_wrap_text:
 * @style: #GnmStyle to query
 *
 * Returns: %TRUE for wrapping, %FALSE for not.  This will be %TRUE also
 * when either alignment is JUSTIFY.
 **/
gboolean
gnm_style_get_effective_wrap_text (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, FALSE);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_WRAP_TEXT), FALSE);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_ALIGN_V), FALSE);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_ALIGN_H), FALSE);

	/* Note: GNM_HALIGN_GENERAL never expands to GNM_HALIGN_JUSTIFY.  */
	return (style->wrap_text ||
		style->v_align == GNM_VALIGN_JUSTIFY ||
		style->v_align == GNM_VALIGN_DISTRIBUTED ||
		style->h_align == GNM_HALIGN_JUSTIFY);
}

/**
 * gnm_style_set_shrink_to_fit:
 * @style: #GnmStyle to change
 * @f: %TRUE for shrink-to-fit, %FALSE for not
 **/
void
gnm_style_set_shrink_to_fit (GnmStyle *style, gboolean f)
{
	g_return_if_fail (style != NULL);

	elem_changed (style, MSTYLE_SHRINK_TO_FIT);
	elem_set (style, MSTYLE_SHRINK_TO_FIT);
	style->shrink_to_fit = !!f;
}

/**
 * gnm_style_get_shrink_to_fit:
 * @style: #GnmStyle to query
 *
 * Returns: %TRUE for shrink-to-fit, %FALSE for not
 **/
gboolean
gnm_style_get_shrink_to_fit (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, FALSE);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_SHRINK_TO_FIT), FALSE);

	return style->shrink_to_fit;
}

/**
 * gnm_style_set_contents_locked:
 * @style: #GnmStyle to change
 * @f: %TRUE for locked, %FALSE for not
 **/
void
gnm_style_set_contents_locked (GnmStyle *style, gboolean f)
{
	g_return_if_fail (style != NULL);

	elem_changed (style, MSTYLE_CONTENTS_LOCKED);
	elem_set (style, MSTYLE_CONTENTS_LOCKED);
	style->contents_locked = !!f;
}

/**
 * gnm_style_get_contents_locked:
 * @style: #GnmStyle to query
 *
 * Returns: %TRUE for locked, %FALSE for not
 **/
gboolean
gnm_style_get_contents_locked (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, FALSE);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_CONTENTS_LOCKED), FALSE);

	return style->contents_locked;
}

/**
 * gnm_style_set_contents_hidden:
 * @style: #GnmStyle to change
 * @f: %TRUE for hidden, %FALSE for not
 **/
void
gnm_style_set_contents_hidden (GnmStyle *style, gboolean f)
{
	g_return_if_fail (style != NULL);

	elem_changed (style, MSTYLE_CONTENTS_HIDDEN);
	elem_set (style, MSTYLE_CONTENTS_HIDDEN);
	style->contents_hidden = !!f;
}

/**
 * gnm_style_get_contents_hidden:
 * @style: #GnmStyle to query
 *
 * Return: %TRUE for hidden, %FALSE for not
 **/
gboolean
gnm_style_get_contents_hidden (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, FALSE);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_CONTENTS_HIDDEN), FALSE);

	return style->contents_hidden;
}

/**
 * gnm_style_set_validation:
 * @style: #GnmStyle to change
 * @v: (transfer full) (nullable): #GnmValidation
 **/
void
gnm_style_set_validation (GnmStyle *style, GnmValidation *v)
{
	g_return_if_fail (style != NULL);

	elem_clear_contents (style, MSTYLE_VALIDATION);
	elem_changed (style, MSTYLE_VALIDATION);
	elem_set (style, MSTYLE_VALIDATION);
	style->validation = v;
}

/**
 * gnm_style_get_validation:
 * @style: #GnmStyle to query
 *
 * Returns: (transfer none) (nullable):
 **/
GnmValidation const *
gnm_style_get_validation (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, NULL);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_VALIDATION), NULL);

	return style->validation;
}

/**
 * gnm_style_set_hlink:
 * @style: #GnmStyle to change
 * @lnk: (transfer full) (nullable): #GnmHLink
 *
 * This sets a link for @style.
 **/
void
gnm_style_set_hlink (GnmStyle *style, GnmHLink *lnk)
{
	g_return_if_fail (style != NULL);

	elem_clear_contents (style, MSTYLE_HLINK);
	elem_changed (style, MSTYLE_HLINK);
	elem_set (style, MSTYLE_HLINK);
	style->hlink = lnk;
}

/**
 * gnm_style_get_hlink:
 * @style: #GnmStyle to query
 *
 * Returns: (transfer none) (nullable): the associated #GnmHLink.
 **/
GnmHLink *
gnm_style_get_hlink (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, NULL);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_HLINK), NULL);

	return style->hlink;
}

/**
 * gnm_style_set_input_msg:
 * @style: #GnmStyle to change
 * @msg: (transfer full) (nullable): #GnmInputMsg
 *
 * This sets an input message for @style.
 **/
void
gnm_style_set_input_msg (GnmStyle *style, GnmInputMsg *msg)
{
	g_return_if_fail (style != NULL);

	elem_clear_contents (style, MSTYLE_INPUT_MSG);
	elem_changed (style, MSTYLE_INPUT_MSG);
	elem_set (style, MSTYLE_INPUT_MSG);
	style->input_msg = msg;
}

/**
 * gnm_style_get_input_msg:
 * @style: #GnmStyle to query
 *
 * Returns: (transfer none) (nullable): the currently set input message.
 **/
GnmInputMsg *
gnm_style_get_input_msg (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, NULL);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_INPUT_MSG), NULL);

	return style->input_msg;
}

/**
 * gnm_style_set_conditions:
 * @style: #GnmStyle to change
 * @sc: (transfer full) (nullable): #GnmStyleConditions
 *
 * This sets conditional style for @style.
 **/
void
gnm_style_set_conditions (GnmStyle *style, GnmStyleConditions *sc)
{
	g_return_if_fail (style != NULL);

	elem_clear_contents (style, MSTYLE_CONDITIONS);
	elem_changed (style, MSTYLE_CONDITIONS);
	elem_set (style, MSTYLE_CONDITIONS);
	style->conditions = sc;
}

/**
 * gnm_style_get_conditions:
 * @style: #GnmStyle to query
 *
 * Returns: (transfer none) (nullable): the currently set conditional style.
 **/
GnmStyleConditions *
gnm_style_get_conditions (GnmStyle const *style)
{
	g_return_val_if_fail (style != NULL, NULL);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_CONDITIONS), NULL);
	return style->conditions;
}

/**
 * gnm_style_get_cond_style:
 * @style: #GnmStyle to query
 * @ix: The index of the condition for which style is desired
 *
 * Returns: (transfer none): the resulting style from applying the condition's
 * style overlay onto @style.
 **/
GnmStyle const *
gnm_style_get_cond_style (GnmStyle const *style, int ix)
{
	g_return_val_if_fail (style != NULL, NULL);
	g_return_val_if_fail (elem_is_set (style, MSTYLE_CONDITIONS), NULL);
	g_return_val_if_fail (style->conditions != NULL, NULL);
	g_return_val_if_fail (ix >= 0 && (unsigned)ix < gnm_style_conditions_details (style->conditions)->len, NULL);

	if (style->changed)
		gnm_style_update ((GnmStyle *)style);

	return g_ptr_array_index (style->cond_styles, ix);
}

void
gnm_style_link_dependents (GnmStyle *style, GnmRange const *r)
{
	GnmStyleConditions *sc;
	Sheet *sheet;

	g_return_if_fail (style != NULL);
	g_return_if_fail (r != NULL);

	sheet = style->linked_sheet;

	// ----------------------------------------

	// Conditional formatting.
	//
	// We need to trigger a reformatting of the cell if a cell referenced
	// by the condition changes.
	sc = elem_is_set (style, MSTYLE_CONDITIONS)
		? gnm_style_get_conditions (style)
		: NULL;
	if (sc) {
		sheet_conditions_add (sheet, r, style);
	}

	// ----------------------------------------
	// Validations.
	//
	// We can probably ignore those.  If a dependent cell changes such
	// that a validation condition is no longer satisfied, it is
	// grandfathered in as valid.
}

void
gnm_style_unlink_dependents (GnmStyle *style, GnmRange const *r)
{
	GnmStyleConditions *sc;
	Sheet *sheet;

	g_return_if_fail (style != NULL);
	g_return_if_fail (r != NULL);

	sheet = style->linked_sheet;

	sc = elem_is_set (style, MSTYLE_CONDITIONS)
		? gnm_style_get_conditions (style)
		: NULL;
	if (sc) {
		sheet_conditions_remove (sheet, r, style);
	}

	// Validation -- see gnm_style_link_dependents
}


/**
 * gnm_style_visible_in_blank:
 * @style: style to query
 *
 * Returns: %TRUE if the style is visible, i.e., not transparent.  Specifically
 * that means if it has a background or a visible border.
 */
gboolean
gnm_style_visible_in_blank (GnmStyle const *style)
{
	GnmStyleElement i;

	g_return_val_if_fail (style != NULL, FALSE);

	if (elem_is_set (style, MSTYLE_PATTERN) &&
	    gnm_style_get_pattern (style) > 0)
		return TRUE;

	for (i = MSTYLE_BORDER_TOP; i <= MSTYLE_BORDER_DIAGONAL; ++i)
		if (elem_is_set (style, i) &&
		    gnm_style_border_visible_in_blank (gnm_style_get_border (style, i)))
			return TRUE;

	return FALSE;
}

static void
add_attr (PangoAttrList *attrs, PangoAttribute *attr)
{
	attr->start_index = 0;
	attr->end_index = G_MAXINT;
	pango_attr_list_insert (attrs, attr);
}

/**
 * gnm_style_generate_attrs:
 * @style: style to query
 * @context: the context for the attributes
 * @zoom: zoom level
 *
 * Returns: (transfer full): a #PangoAttrList with attributes matching
 * @style.  Attributes where the default will serve are not included.
 * The foreground color is not included.
 */
PangoAttrList *
gnm_style_get_pango_attrs (GnmStyle const *style,
			   PangoContext *context,
			   double zoom)
{
	PangoAttrList *l;
	GnmUnderline ul;
	GnmFont *font = gnm_style_get_font (style, context);

	if (style->pango_attrs) {
		if (zoom == style->pango_attrs_zoom) {
			pango_attr_list_ref (style->pango_attrs);
			return style->pango_attrs;
		}
		pango_attr_list_unref (((GnmStyle *)style)->pango_attrs);
	}

	((GnmStyle *)style)->pango_attrs = l = pango_attr_list_new ();
	((GnmStyle *)style)->pango_attrs_zoom = zoom;
	((GnmStyle *)style)->pango_attrs_height = -1;

	/* Foreground colour.  */
	/* See http://bugzilla.gnome.org/show_bug.cgi?id=105322 */
	if (0) {
		GnmColor const *fore = style->color.font;
		add_attr (l, go_color_to_pango (fore->go_color, TRUE));
	}

	/* Handle underlining.  */
	ul = gnm_style_get_font_uline (style);
	if (ul != UNDERLINE_NONE)
		add_attr (l,
			  pango_attr_underline_new (gnm_translate_underline_to_pango (ul)));

	/* Handle strikethrough. */
	if (gnm_style_get_font_strike (style))
		add_attr (l, pango_attr_strikethrough_new (TRUE));

	/* Handle script. */
	switch (gnm_style_get_font_script (style)) {
	default:
	case GO_FONT_SCRIPT_STANDARD:
		break;
	case GO_FONT_SCRIPT_SUB:
		add_attr (l, go_pango_attr_subscript_new (TRUE));
		break;
	case GO_FONT_SCRIPT_SUPER:
		add_attr (l, go_pango_attr_superscript_new (TRUE));
		break;
	}

	add_attr (l, pango_attr_font_desc_new (font->go.font->desc));

	if (zoom != 1)
		add_attr (l, pango_attr_scale_new (zoom));

	pango_attr_list_ref (l);
	return l;
}

/**
 * gnm_style_generate_attrs_full:
 * @style: style to query
 *
 * Returns: (transfer full): a #PangoAttrList with attributes matching
 * @style, even attributes where the default would have served.
 */
PangoAttrList *
gnm_style_generate_attrs_full (GnmStyle const *style)
{
	GnmColor const *fore = style->color.font;
	PangoAttrList *l = pango_attr_list_new ();

	add_attr (l, pango_attr_family_new (gnm_style_get_font_name (style)));
	add_attr (l, pango_attr_size_new (gnm_style_get_font_size (style) * PANGO_SCALE));
	add_attr (l, pango_attr_style_new (gnm_style_get_font_italic (style)
		? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL));
	add_attr (l, pango_attr_weight_new (gnm_style_get_font_bold (style)
		? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL));
	add_attr (l, go_color_to_pango (fore->go_color, TRUE));
	add_attr (l, pango_attr_strikethrough_new
		  (gnm_style_get_font_strike (style)));
	add_attr (l, pango_attr_underline_new
		  (gnm_translate_underline_to_pango
		   (gnm_style_get_font_uline (style))));
	return l;
}

int
gnm_style_get_pango_height (GnmStyle const *style,
			    PangoContext *context,
			    double zoom)
{
	PangoAttrList *attrs = gnm_style_get_pango_attrs (style, context, zoom);

	if (style->pango_attrs_height == -1) {
		int h;
		PangoLayout *layout = pango_layout_new (context);
		GOFormat const *fmt;
		gboolean requires_translation = FALSE;

		fmt = gnm_style_get_format (style);
		if (!go_format_is_general (fmt)) {
			GOFormatDetails details;
			go_format_get_details (fmt, &details, NULL);
			if (details.family == GO_FORMAT_SCIENTIFIC &&
			    details.use_markup) {
				PangoAttribute *a
					= go_pango_attr_superscript_new (TRUE);
				/* We want to superscript the "-01" in the */
				/* string "+1.23456789E-01" */
				a->start_index = 12;
				a->end_index = 15;
				pango_attr_list_insert (attrs, a);
				requires_translation = TRUE;
			}
		}
		pango_layout_set_attributes (layout, attrs);
		pango_layout_set_text (layout, "+1.23456789E-01", -1);
		if (requires_translation)
			go_pango_translate_layout (layout);
		pango_layout_get_pixel_size (layout, NULL, &h);
		g_object_unref (layout);
		((GnmStyle *)style)->pango_attrs_height = h;
	}

	pango_attr_list_unref (attrs);
	return style->pango_attrs_height;
}


void
gnm_style_set_from_pango_attribute (GnmStyle *style, PangoAttribute const *attr)
{
	switch (attr->klass->type) {
	case PANGO_ATTR_FAMILY:
		gnm_style_set_font_name (style, ((PangoAttrString *)attr)->value);
		break;
	case PANGO_ATTR_SIZE:
		gnm_style_set_font_size (style,
					 ((PangoAttrInt *)attr)->value / (double)PANGO_SCALE);
		break;
	case PANGO_ATTR_STYLE:
		gnm_style_set_font_italic (style,
			((PangoAttrInt *)attr)->value == PANGO_STYLE_ITALIC);
		break;
	case PANGO_ATTR_WEIGHT:
		gnm_style_set_font_bold (style,
			((PangoAttrInt *)attr)->value >= PANGO_WEIGHT_BOLD);
		break;
	case PANGO_ATTR_FOREGROUND:
		gnm_style_set_font_color (style, gnm_color_new_pango (
			&((PangoAttrColor *)attr)->color));
		break;
	case PANGO_ATTR_UNDERLINE:
		gnm_style_set_font_uline
			(style, gnm_translate_underline_from_pango
			 (((PangoAttrInt *)attr)->value));
		break;
	case PANGO_ATTR_STRIKETHROUGH:
		gnm_style_set_font_strike (style,
			((PangoAttrInt *)attr)->value != 0);
		break;
	default : {
		gboolean script_seen = FALSE, script_set = FALSE;
		if (attr->klass->type == go_pango_attr_superscript_get_attr_type ()) {
			script_seen = TRUE;
			if (((GOPangoAttrSuperscript *)attr)->val == 1) {
				script_set = TRUE;
				gnm_style_set_font_script
					(style, GO_FONT_SCRIPT_SUPER);
			}
		} else if (attr->klass->type == go_pango_attr_subscript_get_attr_type ()) {
			script_seen = TRUE;
			if (((GOPangoAttrSubscript *)attr)->val == 1) {
				script_set = TRUE;
				gnm_style_set_font_script
					(style, GO_FONT_SCRIPT_SUB);
			}
		}
		if (script_seen && !script_set)
			gnm_style_set_font_script
				(style, GO_FONT_SCRIPT_STANDARD);
		break; /* ignored */
	}
	}
}

/* ------------------------------------------------------------------------- */

static void
gnm_style_dump_color (GnmColor *color, GnmStyleElement elem)
{
	if (color)
		g_printerr ("\t%s: %x:%x:%x%s\n",
			    gnm_style_element_name [elem],
			    GO_COLOR_UINT_R (color->go_color),
			    GO_COLOR_UINT_G (color->go_color),
			    GO_COLOR_UINT_B (color->go_color),
			    color->is_auto ? " auto" : "");
	else
		g_printerr ("\t%s: (NULL)\n", gnm_style_element_name [elem]);
}

static void
gnm_style_dump_border (GnmBorder *border, GnmStyleElement elem)
{
	g_printerr ("\t%s: ", gnm_style_element_name[elem]);
	if (border)
		g_printerr ("%d\n", border->line_type);
	else
		g_printerr ("blank\n");
}

/**
 * gnm_style_dump:
 * @style: style to dump
 *
 * This function dumps the given style's contents to stderr.  This is meant
 * for debug purposes only and doesn't do a very good job for, for example,
 * conditional style settings.
 */
void
gnm_style_dump (GnmStyle const *style)
{
	int i;

	g_printerr ("Style Refs %d\n", style->ref_count);
	if (elem_is_set (style, MSTYLE_COLOR_BACK))
		gnm_style_dump_color (style->color.back, MSTYLE_COLOR_BACK);
	if (elem_is_set (style, MSTYLE_COLOR_PATTERN))
		gnm_style_dump_color (style->color.pattern, MSTYLE_COLOR_PATTERN);

	for (i = MSTYLE_BORDER_TOP; i <= MSTYLE_BORDER_DIAGONAL; ++i)
		if (elem_is_set (style, i))
			gnm_style_dump_border (style->borders[i-MSTYLE_BORDER_TOP], i);

	if (elem_is_set (style, MSTYLE_PATTERN))
		g_printerr ("\tpattern %d\n", style->pattern);
	if (elem_is_set (style, MSTYLE_FONT_COLOR))
		gnm_style_dump_color (style->color.font, MSTYLE_FONT_COLOR);
	if (elem_is_set (style, MSTYLE_FONT_NAME))
		g_printerr ("\tname '%s'\n", style->font_detail.name->str);
	if (elem_is_set (style, MSTYLE_FONT_BOLD))
		g_printerr (style->font_detail.bold ? "\tbold\n" : "\tnot bold\n");
	if (elem_is_set (style, MSTYLE_FONT_ITALIC))
		g_printerr (style->font_detail.italic ? "\titalic\n" : "\tnot italic\n");
	if (elem_is_set (style, MSTYLE_FONT_UNDERLINE))
		switch (style->font_detail.underline) {
		default:
		case UNDERLINE_NONE:
			g_printerr ("\tno underline\n"); break;
		case UNDERLINE_SINGLE:
			g_printerr ("\tsingle underline\n"); break;
		case UNDERLINE_DOUBLE:
			g_printerr ("\tdouble underline\n"); break;
		}
	if (elem_is_set (style, MSTYLE_FONT_STRIKETHROUGH))
		g_printerr (style->font_detail.strikethrough ? "\tstrikethrough\n" : "\tno strikethrough\n");
	if (elem_is_set (style, MSTYLE_FONT_SCRIPT))
		switch (style->font_detail.script) {
		case GO_FONT_SCRIPT_SUB:
			g_printerr ("\tsubscript\n"); break;
		default:
		case GO_FONT_SCRIPT_STANDARD:
			g_printerr ("\tno super or sub\n"); break;
		case GO_FONT_SCRIPT_SUPER:
			g_printerr ("\tsuperscript\n"); break;
		}
	if (elem_is_set (style, MSTYLE_FONT_SIZE))
		g_printerr ("\tsize %f\n", style->font_detail.size);
	if (elem_is_set (style, MSTYLE_FORMAT)) {
		const char *fmt = go_format_as_XL (style->format);
		g_printerr ("\tformat '%s'\n", fmt);
	}
	if (elem_is_set (style, MSTYLE_ALIGN_V))
		g_printerr ("\tvalign %hd\n", (short)style->v_align);
	if (elem_is_set (style, MSTYLE_ALIGN_H))
		g_printerr ("\thalign %hd\n", (short)style->h_align);
	if (elem_is_set (style, MSTYLE_INDENT))
		g_printerr ("\tindent %d\n", style->indent);
	if (elem_is_set (style, MSTYLE_ROTATION))
		g_printerr ("\trotation %d\n", style->rotation);
	if (elem_is_set (style, MSTYLE_TEXT_DIR))
		g_printerr ("\ttext dir %d\n", style->text_dir);
	if (elem_is_set (style, MSTYLE_WRAP_TEXT))
		g_printerr ("\twrap text %d\n", style->wrap_text);
	if (elem_is_set (style, MSTYLE_SHRINK_TO_FIT))
		g_printerr ("\tshrink to fit %d\n", style->shrink_to_fit);
	if (elem_is_set (style, MSTYLE_CONTENTS_LOCKED))
		g_printerr ("\tlocked %d\n", style->contents_locked);
	if (elem_is_set (style, MSTYLE_CONTENTS_HIDDEN))
		g_printerr ("\thidden %d\n", style->contents_hidden);
	if (elem_is_set (style, MSTYLE_VALIDATION))
		g_printerr ("\tvalidation %p\n", (void *)style->validation);
	if (elem_is_set (style, MSTYLE_HLINK))
		g_printerr ("\thlink %p\n", (void *)style->hlink);
	if (elem_is_set (style, MSTYLE_INPUT_MSG))
		g_printerr ("\tinput msg %p\n", (void *)style->input_msg);
	if (elem_is_set (style, MSTYLE_CONDITIONS))
		g_printerr ("\tconditions %p\n", (void *)style->conditions);
}

/* ------------------------------------------------------------------------- */

/**
 * gnm_style_init: (skip)
 */
void
gnm_style_init (void)
{
	debug_style_deps = gnm_debug_flag ("style-deps");

#if USE_MSTYLE_POOL
	gnm_style_pool =
		go_mem_chunk_new ("style pool",
				   sizeof (GnmStyle),
				   16 * 1024 - 128);
#endif
}

#if USE_MSTYLE_POOL
static void
cb_gnm_style_pool_leak (gpointer data, G_GNUC_UNUSED gpointer user)
{
	GnmStyle *style = data;
	g_printerr ("Leaking style at %p.\n", (void *)style);
	gnm_style_dump (style);
}
#endif

/**
 * gnm_style_shutdown: (skip)
 */
void
gnm_style_shutdown (void)
{
#if USE_MSTYLE_POOL
	go_mem_chunk_foreach_leak (gnm_style_pool, cb_gnm_style_pool_leak, NULL);
	go_mem_chunk_destroy (gnm_style_pool, FALSE);
	gnm_style_pool = NULL;
#endif
}
