#ifndef GNUMERIC_MSTYLE_H
#define GNUMERIC_MSTYLE_H

#include "gnumeric.h"
#include "style.h"

/*
 * Keep element_size up to date.
 */
typedef enum _MStyleElementType {
	/* Delimiter */
	MSTYLE_ELEMENT_UNSET = 0,
	/* When there is a conflict in a merge */
	MSTYLE_ELEMENT_CONFLICT,
	/* Types that are visible in blank cells */
		MSTYLE_COLOR_BACK,
		MSTYLE_COLOR_PATTERN,

	        MSTYLE_BORDER_TOP,
	        MSTYLE_BORDER_BOTTOM,
	        MSTYLE_BORDER_LEFT,
	        MSTYLE_BORDER_RIGHT,
	        MSTYLE_BORDER_REV_DIAGONAL,
	        MSTYLE_BORDER_DIAGONAL,

		MSTYLE_PATTERN,
	/* Delimiter */
	MSTYLE_ELEMENT_MAX_BLANK,
	/* Normal types */
	        MSTYLE_COLOR_FORE,
		MSTYLE_FONT_NAME,
		MSTYLE_FONT_BOLD,
		MSTYLE_FONT_ITALIC,
		MSTYLE_FONT_UNDERLINE,
		MSTYLE_FONT_STRIKETHROUGH,
	        MSTYLE_FONT_SIZE,

		MSTYLE_FORMAT,

	        MSTYLE_ALIGN_V,
	        MSTYLE_ALIGN_H,
	        MSTYLE_INDENT,
		MSTYLE_ROTATION,
		MSTYLE_WRAP_TEXT,
		MSTYLE_SHRINK_TO_FIT,

	        MSTYLE_CONTENT_LOCKED,
	        MSTYLE_CONTENT_HIDDEN,

	/* Things not in MS Excel's Style */
	        MSTYLE_VALIDATION,
	        MSTYLE_HLINK,		/* patch equal_XL if this is changed */
	        MSTYLE_INPUT_MSG,	/* patch equal_XL if this is changed */
	/* Delimiter */
	MSTYLE_ELEMENT_MAX
} MStyleElementType;

GnmMStyle     *mstyle_new           (void);
GnmMStyle     *mstyle_new_default   (void);
GnmMStyle     *mstyle_copy          (const GnmMStyle *st);
GnmMStyle	   *mstyle_copy_merge	 (const GnmMStyle *orig, const GnmMStyle *overlay);
void        mstyle_ref           (GnmMStyle *st);
void        mstyle_unref         (GnmMStyle *st);

GnmMStyle	   *mstyle_link_sheet    (GnmMStyle *st, Sheet *sheet);
void        mstyle_link          (GnmMStyle *st);
void        mstyle_link_multiple (GnmMStyle *st, int count);
void        mstyle_unlink        (GnmMStyle *st);

gboolean    mstyle_equal         (GnmMStyle const *a, GnmMStyle const *b);
gboolean    mstyle_equal_XL	 (GnmMStyle const *a, GnmMStyle const *b);
gboolean    mstyle_verify        (GnmMStyle const *st);
guint       mstyle_hash          (gconstpointer st);
guint       mstyle_hash_XL	 (gconstpointer st);
gboolean    mstyle_empty         (const GnmMStyle *st);

/*
 * Wafer thin element access functions.
 */
gboolean            mstyle_is_element_set  (const GnmMStyle *st, MStyleElementType t);
gboolean            mstyle_is_element_conflict (const GnmMStyle *st, MStyleElementType t);
void                mstyle_compare             (GnmMStyle *a, const GnmMStyle *b);
void                mstyle_unset_element   (GnmMStyle *st, MStyleElementType t);
void                mstyle_replace_element (GnmMStyle *src, GnmMStyle *dst, MStyleElementType t);
void                mstyle_set_color       (GnmMStyle *st, MStyleElementType t,
					    GnmStyleColor *col);
GnmStyleColor         *mstyle_get_color       (const GnmMStyle *st, MStyleElementType t);
void                mstyle_set_border      (GnmMStyle *st, MStyleElementType t,
					    GnmStyleBorder *border);
GnmStyleBorder	   *mstyle_get_border      (const GnmMStyle *st, MStyleElementType t);
void                mstyle_set_pattern     (GnmMStyle *st, int pattern);
int                 mstyle_get_pattern     (const GnmMStyle *st);
void                mstyle_set_font_name   (GnmMStyle *st, const char *name);
const char         *mstyle_get_font_name   (const GnmMStyle *st);
void                mstyle_set_font_bold   (GnmMStyle *st, gboolean bold);
gboolean            mstyle_get_font_bold   (const GnmMStyle *st);
void                mstyle_set_font_italic (GnmMStyle *st, gboolean italic);
gboolean            mstyle_get_font_italic (const GnmMStyle *st);
void                mstyle_set_font_uline  (GnmMStyle *st, StyleUnderlineType const t);
StyleUnderlineType  mstyle_get_font_uline  (const GnmMStyle *st);
void                mstyle_set_font_strike (GnmMStyle *st, gboolean strikethrough);
gboolean            mstyle_get_font_strike (const GnmMStyle *st);
void                mstyle_set_font_size   (GnmMStyle *st, double size);
double              mstyle_get_font_size   (const GnmMStyle *st);

/* this font must be unrefd after use */
GnmStyleFont          *mstyle_get_font        (const GnmMStyle *st,
					    PangoContext *context,
					    double zoom);
void                mstyle_set_format      (GnmMStyle *st, GnmStyleFormat *);
void                mstyle_set_format_text (GnmMStyle *st, const char *format);
GnmStyleFormat        *mstyle_get_format      (const GnmMStyle *st);
void                mstyle_set_align_h     (GnmMStyle *st, StyleHAlignFlags a);
StyleHAlignFlags    mstyle_get_align_h     (const GnmMStyle *st);
void                mstyle_set_align_v     (GnmMStyle *st, StyleVAlignFlags a);
StyleVAlignFlags    mstyle_get_align_v     (const GnmMStyle *st);
void                mstyle_set_indent	   (GnmMStyle *st, int i);
int		    mstyle_get_indent	   (const GnmMStyle *st);

void                mstyle_set_rotation	   (GnmMStyle *st, int r);
int            	    mstyle_get_rotation    (const GnmMStyle *st);

void                mstyle_set_wrap_text   (GnmMStyle *st, gboolean f);
gboolean            mstyle_get_wrap_text   (const GnmMStyle *st);
gboolean            mstyle_get_effective_wrap_text   (const GnmMStyle *st);
void                mstyle_set_shrink_to_fit (GnmMStyle *st, gboolean f);
gboolean            mstyle_get_shrink_to_fit (const GnmMStyle *st);

void                mstyle_set_content_locked (GnmMStyle *st, gboolean f);
gboolean            mstyle_get_content_locked (const GnmMStyle *st);
void                mstyle_set_content_hidden (GnmMStyle *st, gboolean f);
gboolean            mstyle_get_content_hidden (const GnmMStyle *st);

void                mstyle_set_validation	(GnmMStyle *st, GnmValidation *v);
GnmValidation      *mstyle_get_validation	(const GnmMStyle *st);

void                mstyle_set_hlink		(GnmMStyle *st, GnmHLink *link);
GnmHLink	   *mstyle_get_hlink		(const GnmMStyle *st);

void                mstyle_set_input_msg	(GnmMStyle *st, GnmInputMsg *msg);
GnmInputMsg   	   *mstyle_get_input_msg	(const GnmMStyle *st);

gboolean            mstyle_visible_in_blank (const GnmMStyle *st);

PangoAttrList      *mstyle_get_pango_attrs (const GnmMStyle *st,
					    PangoContext *context,
					    double zoom);

char       *mstyle_to_string   (const GnmMStyle *st); /* Debug only ! leaks like a sieve */
void        mstyle_dump        (const GnmMStyle *st);

void        mstyle_init (void);
void        mstyle_shutdown (void);

#endif /* GNUMERIC_MSTYLE_H */
