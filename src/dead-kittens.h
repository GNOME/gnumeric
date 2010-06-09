#ifndef GNUMERIC_GTK_DEAD_KITTENS_H
#define GNUMERIC_GTK_DEAD_KITTENS_H

/* To be included only from C files, not headers.  */

#ifndef HAVE_GTK_ENTRY_GET_TEXT_LENGTH
#define gtk_entry_get_text_length(x) g_utf8_strlen (gtk_entry_get_text (x), -1) 
#endif

#ifndef HAVE_GTK_LAYOUT_GET_BIN_WINDOW
#define gtk_layout_get_bin_window(x) (x)->bin_window
#endif

#ifndef HAVE_GTK_WIDGET_SET_VISIBLE
#define gtk_widget_set_visible(_w_,_v_) do { if (_v_) gtk_widget_show (_w_); else gtk_widget_hide (_w_); } while (0)
#endif

#ifndef HAVE_GTK_WIDGET_IS_TOPLEVEL
#define gtk_widget_is_toplevel(w_) (GTK_WIDGET_FLAGS ((w_)) & GTK_TOPLEVEL)
#endif

#ifndef HAVE_GTK_WIDGET_GET_STATE
#define gtk_widget_get_state(_w) GTK_WIDGET_STATE((_w))
#endif

#ifndef HAVE_GTK_WIDGET_GET_WINDOW
#define gtk_widget_get_window(w) ((w)->window)
#endif

#endif
