#ifndef GNUMERIC_MSTYLE_H
#define GNUMERIC_MSTYLE_H

#include <gdk/gdk.h>
#include <libgnomeprint/gnome-font.h>

typedef struct _MStyleBorder      MStyleBorder;
typedef struct _MStyleElement     MStyleElement;
typedef enum   _MStyleElementType MStyleElementType;

#include "sheet.h"

enum _MStyleElementType {
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
	        MSTYLE_BORDER_DIAGONAL,
	        MSTYLE_BORDER_REV_DIAGONAL,

		MSTYLE_PATTERN,
	/* Delimiter */
	MSTYLE_ELEMENT_MAX_BLANK,
	/* Normal types */
	        MSTYLE_COLOR_FORE,
		MSTYLE_FONT_NAME,
		MSTYLE_FONT_BOLD,
		MSTYLE_FONT_ITALIC,
	        MSTYLE_FONT_SIZE,

		MSTYLE_FORMAT,

	        MSTYLE_ALIGN_V,
	        MSTYLE_ALIGN_H,

		MSTYLE_ORIENTATION,

		MSTYLE_FIT_IN_CELL,
	/* Delimiter */
	MSTYLE_ELEMENT_MAX
};
#define MSTYLE_ANY_COLOR             MSTYLE_COLOR_FORE: \
				case MSTYLE_COLOR_BACK: \
				case MSTYLE_COLOR_PATTERN
#define MSTYLE_ANY_BORDER            MSTYLE_BORDER_TOP: \
				case MSTYLE_BORDER_BOTTOM: \
				case MSTYLE_BORDER_LEFT: \
				case MSTYLE_BORDER_RIGHT: \
				case MSTYLE_BORDER_DIAGONAL: \
				case MSTYLE_BORDER_REV_DIAGONAL

extern const char *mstyle_names[MSTYLE_ELEMENT_MAX];

struct _MStyleElement {
	MStyleElementType type;
	union {
		union {
			StyleColor *fore;
			StyleColor *back;
			StyleColor *pattern;
		}                color;
		union {
			MStyleBorder *top;
			MStyleBorder *bottom;
			MStyleBorder *left;
			MStyleBorder *right;
			MStyleBorder *diagonal;
			MStyleBorder *rev_diagonal;

			/* Used for loading */
			MStyleBorder *any;
		}                border;
		guint32          pattern;

		union {
			gchar    *name;
			gboolean  bold;
			gboolean  italic;
			gdouble   size;
		}                font;
		StyleFormat     *format;
		union {
			StyleVAlignFlags v;
			StyleHAlignFlags h;
		}                align;
		StyleOrientation orientation;
		gboolean         fit_in_cell;
	} u;
};

MStyleElement  mstyle_element_copy    (MStyleElement e);
void           mstyle_element_destroy (MStyleElement e);

MStyle     *mstyle_new         (const gchar *name);
MStyle     *mstyle_new_elem    (const gchar *name, MStyleElement e);
MStyle     *mstyle_new_elems   (const gchar *name, const MStyleElement *e);
void        mstyle_ref         (MStyle *st);
void        mstyle_unref       (MStyle *st);
void        mstyle_destroy     (MStyle *st);

/* No pre-existance checking */
void        mstyle_add         (MStyle *st, MStyleElement e);
/* Checks to see if it is alreqady in use */
void        mstyle_set         (MStyle *st, MStyleElement e);
const MStyleElement *mstyle_get_elements (MStyle *st);

/* commutative */
MStyle     *mstyle_merge       (const MStyle *sta, const MStyle *stb);
char       *mstyle_to_string   (const MStyle *st); /* Debug only ! leaks like a sieve */
void        mstyle_dump        (const MStyle *st);

void        mstyle_do_merge    (const GList *list, MStyleElementType max,
				MStyleElement *mash, gboolean blank_uniq);
Style      *render_merge       (const GList *mstyles);
Style      *render_merge_blank (const GList *mstyles);

#endif /* GNUMERIC_MSTYLE_H */
