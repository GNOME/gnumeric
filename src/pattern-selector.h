#ifndef GNUMERIC_PATTERN_SELECTOR_H
#define GNUMERIC_PATTERN_SELECTOR_H

#define PATTERN_SELECTOR_TYPE     (pattern_selector_get_type ())
#define PATTERN_SELECTOR(obj)     (GTK_CHECK_CAST((obj), PATTERN_SELECTOR_TYPE, PatternSelector))
#define PATTERN_SELECTOR_CLASS(k) (GTK_CHECK_CLASS_CAST((obj), PATTERN_SELECTOR_TYPE))
#define IS_PATTERN_SELECTOR(o)    (GTK_CHECK_TYPE((o), PATTERN_SELECTOR_TYPE))

typedef struct {
	GnomeCanvas     canvas;

	GdkPixmap       *patterns [GNUMERIC_SHEET_PATTERNS];
	int             selected_item;
	GnomeCanvasItem *selector;
} PatternSelector;

typedef struct {
	GnomeCanvasClass parent_class;
} PatternSelectorClass;

GtkType     pattern_selector_get_type   (void);
GtkWidget  *pattern_selector_new        (int pattern);
void        pattern_selector_select     (PatternSelector *ps, int pattern);

#endif /* GNUMERIC_PATTERN_SELECTOR_H */
