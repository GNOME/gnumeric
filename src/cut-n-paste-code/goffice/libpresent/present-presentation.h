/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- *//* vim: set sw=8: */
#ifndef PRESENT_PRESENTATION_H
#define PRESENT_PRESENTATION_H

/**
 * present-presentation.h: MS Office Graphic Object support
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
#include <libpresent/present-slide.h>
#include <drawing/god-drawing-group.h>

G_BEGIN_DECLS

#define PRESENT_PRESENTATION_TYPE		(present_presentation_get_type ())
#define PRESENT_PRESENTATION(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PRESENT_PRESENTATION_TYPE, PresentPresentation))
#define PRESENT_PRESENTATION_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST ((k), PRESENT_PRESENTATION_TYPE, PresentPresentationClass))
#define IS_PRESENT_PRESENTATION(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PRESENT_PRESENTATION_TYPE))
#define IS_PRESENT_PRESENTATION_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PRESENT_PRESENTATION_TYPE))

typedef struct PresentPresentationPrivate_ PresentPresentationPrivate;

typedef struct {
	GObject parent;
	PresentPresentationPrivate *priv;
} PresentPresentation;

typedef struct {
	GObjectClass parent_class;
} PresentPresentationClass;

GType                present_presentation_get_type           (void);
PresentPresentation *present_presentation_new                (void);

void                 present_presentation_append_slide       (PresentPresentation *presentation,
							      PresentSlide        *slide);
void                 present_presentation_insert_slide       (PresentPresentation *presentation,
							      PresentSlide        *slide,
							      int                  pos);
void                 present_presentation_delete_slide       (PresentPresentation *presentation,
							      int                  pos);
void                 present_presentation_reorder_slide      (PresentPresentation *presentation,
							      int                  old_pos,
							      int                  new_pos);
int                  present_presentation_get_slide_count    (PresentPresentation *presentation);
/* Return value is reffed. */
PresentSlide        *present_presentation_get_slide          (PresentPresentation *presentation,
							      int                  pos);

/* Return value is reffed. */
GodDrawingGroup     *present_presentation_get_drawing_group  (PresentPresentation *presentation);
void                 present_presentation_set_drawing_group  (PresentPresentation *presentation,
							      GodDrawingGroup     *drawing_group);

G_END_DECLS

#endif /* PRESENT_PRESENTATION_H */
