#ifndef GNUMERIC_MSTYLE_H
#define GNUMERIC_MSTYLE_H

#include <gdk/gdk.h>
#include <libgnomeprint/gnome-font.h>

typedef struct _MStyleElement     MStyleElement;
typedef enum   _MStyleElementType MStyleElementType;

#include "sheet.h"

enum _MStyleElementType {
	/* Delimiter */
	MSTYLE_ELEMENT_ZERO = 0,
	/* Types that are visible in blank cells */
	        MSTYLE_COLOR_FORE,
		MSTYLE_COLOR_BACK,
	/* Delimiter */
	MSTYLE_ELEMENT_MAX_BLANK,
	/* Normal types */
		MSTYLE_FONT_NAME,
		MSTYLE_FONT_BOLD,
		MSTYLE_FONT_ITALIC,
	        MSTYLE_FONT_SIZE,
	/* Delimiter */
	MSTYLE_ELEMENT_MAX
};

struct _MStyleElement {
	MStyleElementType type;
	union {
		union {
			StyleColor *fore;
			StyleColor *back;
		} color;
		union {
			gchar    *name;
			gboolean  bold;
			gboolean  italic;
			gdouble   size;
		} font;
	} u;
};

MStyle     *mstyle_new         (const gchar *name);
MStyle     *mstyle_new_elem    (const gchar *name, MStyleElement e);
MStyle     *mstyle_new_array   (const gchar *name, const GArray *elements);
/* No pre-existance checking */
void        mstyle_add         (MStyle *st, MStyleElement e);
void        mstyle_add_array   (MStyle *st, const GArray *elements);
/* Checks to see if it is alreqady in use */
void        mstyle_set         (MStyle *st, MStyleElement e);

/* commutative */
MStyle     *mstyle_merge       (const MStyle *sta, const MStyle *stb);
void        mstyle_destroy     (MStyle *st);
char       *mstyle_to_string   (const MStyle *st); /* Debug only ! leaks like a sieve */
void        mstyle_dump        (const MStyle *st);

Style      *render_merge       (const GList *mstyles);
Style      *render_merge_blank (const GList *mstyles);

#endif /* GNUMERIC_MSTYLE_H */
