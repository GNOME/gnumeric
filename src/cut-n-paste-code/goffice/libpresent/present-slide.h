/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- *//* vim: set sw=8: */
#ifndef PRESENT_SLIDE_H
#define PRESENT_SLIDE_H

/**
 * present-slide.h: MS Office Graphic Object support
 *
 * Author:
 *    Michael Meeks (michael@ximian.com)
 *    Jody Goldberg (jody@gnome.org)
 *    Christopher James Lahey <clahey@ximian.com>
 *
 * (C) 1998-2003 Michael Meeks, Jody Goldberg, Chris Lahey
 **/

#include <glib-object.h>
#include <glib.h>
#include <libpresent/present-text.h>
#include <drawing/god-drawing.h>

G_BEGIN_DECLS

#define PRESENT_SLIDE_TYPE		(present_slide_get_type ())
#define PRESENT_SLIDE(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PRESENT_SLIDE_TYPE, PresentSlide))
#define PRESENT_SLIDE_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST ((k), PRESENT_SLIDE_TYPE, PresentSlideClass))
#define IS_PRESENT_SLIDE(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PRESENT_SLIDE_TYPE))
#define IS_PRESENT_SLIDE_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PRESENT_SLIDE_TYPE))

typedef struct PresentSlidePrivate_ PresentSlidePrivate;

typedef struct {
	GObject parent;
	PresentSlidePrivate *priv;
} PresentSlide;

typedef struct {
	GObjectClass parent_class;
} PresentSlideClass;

GType         present_slide_get_type        (void);
PresentSlide *present_slide_new             (void);

void          present_slide_append_text     (PresentSlide *parent,
					     PresentText  *text);
void          present_slide_insert_text     (PresentSlide *parent,
					     PresentText  *text,
					     int           pos);
void          present_slide_delete_text     (PresentSlide *parent,
					     int           pos);
void          present_slide_reorder_text    (PresentSlide *parent,
					     int           old_pos,
					     int           new_pos);
int           present_slide_get_text_count  (PresentSlide *parent);
/* Return value is reffed. */
PresentText  *present_slide_get_text        (PresentSlide *parent,
					     int           pos);

/* Return value is reffed. */
GodDrawing   *present_slide_get_drawing     (PresentSlide *slide);
void          present_slide_set_drawing     (PresentSlide *slide,
					     GodDrawing   *drawing);

G_END_DECLS

#endif /* PRESENT_SLIDE_H */
