/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- *//* vim: set sw=8: */
#ifndef GOD_DRAWING_MS_CLIENT_HANDLER_PPT_H
#define GOD_DRAWING_MS_CLIENT_HANDLER_PPT_H

/**
 * god-drawing-ms-client-handler-ppt.h: MS Office Graphic Object support
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
#include <ms-compat/god-drawing-ms-client-handler.h>

G_BEGIN_DECLS

#define GOD_DRAWING_MS_CLIENT_HANDLER_PPT_TYPE		(god_drawing_ms_client_handler_ppt_get_type ())
#define GOD_DRAWING_MS_CLIENT_HANDLER_PPT(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GOD_DRAWING_MS_CLIENT_HANDLER_PPT_TYPE, GodDrawingMsClientHandlerPpt))
#define GOD_DRAWING_MS_CLIENT_HANDLER_PPT_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST ((k), GOD_DRAWING_MS_CLIENT_HANDLER_PPT_TYPE, GodDrawingMsClientHandlerPptClass))
#define GOD_DRAWING_MS_CLIENT_HANDLER_PPT_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS((o), GOD_DRAWING_MS_CLIENT_HANDLER_PPT_TYPE, GodDrawingMsClientHandlerPptClass))
#define IS_GOD_DRAWING_MS_CLIENT_HANDLER_PPT(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), GOD_DRAWING_MS_CLIENT_HANDLER_PPT_TYPE))
#define IS_GOD_DRAWING_MS_CLIENT_HANDLER_PPT_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), GOD_DRAWING_MS_CLIENT_HANDLER_PPT_TYPE))

typedef struct GodDrawingMsClientHandlerPptPrivate_ GodDrawingMsClientHandlerPptPrivate;

typedef struct {
	GodDrawingMsClientHandler parent;
	GodDrawingMsClientHandlerPptPrivate *priv;
} GodDrawingMsClientHandlerPpt;

typedef struct {
	GodDrawingMsClientHandlerClass parent_class;
} GodDrawingMsClientHandlerPptClass;

GType                      god_drawing_ms_client_handler_ppt_get_type  (void);
GodDrawingMsClientHandler *god_drawing_ms_client_handler_ppt_new       (void);



G_END_DECLS

#endif /* GOD_DRAWING_MS_CLIENT_HANDLER_PPT_H */
