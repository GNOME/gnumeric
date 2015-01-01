#ifndef GNUMERIC_GTK_DEAD_KITTENS_H
#define GNUMERIC_GTK_DEAD_KITTENS_H

#include <gutils.h>
#include <gdk/gdkkeysyms.h>

/* To be included only from C files, not headers.  */

/* This function does not exist in gtk+ yet.  634344.  */
#ifndef HAVE_GTK_CELL_RENDERER_TEXT_GET_BACKGROUND_SET
#define gtk_cell_renderer_text_get_background_set(_cr_) \
  gnm_object_get_bool ((_cr_), "background-set")
#endif

/* This function does not exist in gtk+ yet.  634344.  */
#ifndef HAVE_GTK_CELL_RENDERER_TEXT_GET_FOREGROUND_SET
#define gtk_cell_renderer_text_get_foreground_set(_cr_) \
  gnm_object_get_bool ((_cr_), "foreground-set")
#endif

/* This function does not exist in gtk+ yet.  634344.  */
#ifndef HAVE_GTK_CELL_RENDERER_TEXT_GET_EDITABLE
#define gtk_cell_renderer_text_get_editable(_cr_) \
  gnm_object_get_bool ((_cr_), "editable")
#endif

/* This function does not exist in gtk+ yet.  634342.  */
#ifndef HAVE_GTK_ENTRY_SET_EDITING_CANCELLED
#define gtk_entry_set_editing_cancelled(_e_,_b_) \
  g_object_set ((_e_), "editing-canceled", (gboolean)(_b_), NULL)
#endif

#endif
