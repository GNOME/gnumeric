#ifndef GNUMERIC_GTK_DEAD_KITTENS_H
#define GNUMERIC_GTK_DEAD_KITTENS_H

/* To be included only from C files, not headers.  */

#ifndef HAVE_GTK_DIALOG_GET_ACTION_AREA
#define gtk_dialog_get_action_area(x) ((x)->action_area)
#endif

#ifndef HAVE_GTK_DIALOG_GET_CONTENT_AREA
#define gtk_dialog_get_content_area(x) ((x)->vbox)
#endif

#ifndef HAVE_GTK_ENTRY_GET_TEXT_LENGTH
#define gtk_entry_get_text_length(x) g_utf8_strlen (gtk_entry_get_text (x), -1)
#endif

#ifndef HAVE_GTK_LAYOUT_GET_BIN_WINDOW
#define gtk_layout_get_bin_window(x) (x)->bin_window
#endif

#ifndef HAVE_GTK_WIDGET_SET_VISIBLE
#define gtk_widget_set_visible(_w_,_v_) do { if (_v_) gtk_widget_show (_w_); else gtk_widget_hide (_w_); } while (0)
#endif

#ifndef HAVE_GTK_WIDGET_GET_VISIBLE
#define gtk_widget_get_visible(_w_) GTK_WIDGET_VISIBLE(_w_)
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

#ifndef HAVE_GTK_WIDGET_GET_ALLOCATION
#define gtk_widget_get_allocation(w,a) (*(a) = (w)->allocation)
#endif

#ifndef HAVE_GTK_WIDGET_GET_STYLE
#define gtk_widget_get_style(w) (w)->style
#endif

#ifndef HAVE_GTK_WIDGET_HAS_FOCUS
#define gtk_widget_has_focus(w) GTK_WIDGET_HAS_FOCUS (w)
#endif

#ifndef  HAVE_GTK_DIALOG_GET_CONTENT_AREA
#define gtk_dialog_get_content_area(w) ((w)->vbox)
#endif

#ifndef HAVE_GTK_CHECK_MENU_ITEM_GET_ACTIVE
#define gtk_check_menu_item_get_active(cm) ((cm)->active)
#endif

#ifndef HAVE_GTK_ADJUSTMENT_CONFIGURE
#define gtk_adjustment_configure(_a,_v,_l,_u,_si,_pi,_ps)	\
  g_object_set (_a,						\
                "lower", (double)(_l),				\
                "upper", (double)(_u),				\
                "step-increment", (double)(_si),		\
                "page-increment", (double)(_pi),		\
                "page-size", (double)(_ps),			\
		"value", (double)(_v),				\
                NULL)
#endif

#ifndef HAVE_GTK_ADJUSTMENT_GET_PAGE_SIZE
#define gtk_adjustment_get_page_size (_a) ((_a)->page_size)
#endif

#ifndef HAVE_GTK_ADJUSTMENT_SET_LOWER
#define gtk_adjustment_set_lower (_a,_l) \
  g_object_set (_a, "lower", (double)(_l), NULL)
#endif

#ifndef HAVE_GTK_ADJUSTMENT_SET_UPPER
#define gtk_adjustment_set_upper (_a,_u) \
  g_object_set (_a, "upper", (double)(_u), NULL)
#endif

#endif
