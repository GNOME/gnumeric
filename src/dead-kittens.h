#ifndef GNUMERIC_GTK_DEAD_KITTENS_H
#define GNUMERIC_GTK_DEAD_KITTENS_H

#include <gutils.h>

/* To be included only from C files, not headers.  */

#ifndef HAVE_GTK_CELL_RENDERER_GET_ALIGNMENT
#define gtk_cell_renderer_get_alignment(_cr_,_px_,_py_) do {	\
  gfloat *px = (_px_);						\
  gfloat *py = (_py_);						\
  if (px) *px = (_cr_)->xalign;					\
  if (py) *py = (_cr_)->yalign;					\
} while (0)
#endif

#ifndef HAVE_GTK_CELL_RENDERER_GET_PADDING
#define gtk_cell_renderer_get_padding(_cr_,_px_,_py_) do {	\
  int *px = (_px_);						\
  int *py = (_py_);						\
  if (px) *px = (_cr_)->xpad;					\
  if (py) *py = (_cr_)->ypad;					\
} while (0)
#endif

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

#ifndef HAVE_GTK_DIALOG_GET_ACTION_AREA
#define gtk_dialog_get_action_area(x) ((x)->action_area)
#endif

#ifndef HAVE_GTK_DIALOG_GET_CONTENT_AREA
#define gtk_dialog_get_content_area(x) ((x)->vbox)
#endif

#ifndef HAVE_GTK_ENTRY_GET_TEXT_LENGTH
#define gtk_entry_get_text_length(x) g_utf8_strlen(gtk_entry_get_text((x)),-1)
#endif

#ifndef HAVE_GTK_ENTRY_GET_TEXT_AREA
#  ifdef HAVE_GTK_ENTRY_TEXT_AREA
#    define gtk_entry_get_text_area(x) ((x)->text_area)
#  else
#    define gtk_entry_get_text_area(x) ((x)->_g_sealed__text_area)
#  endif
#endif

#ifndef HAVE_GTK_ENTRY_GET_OVERWRITE_MODE
#define gtk_entry_get_overwrite_mode(_e_) ((_e_)->overwrite_mode)
#endif

/* This function does not exist in gtk+ yet.  634342.  */
#ifndef HAVE_GTK_ENTRY_SET_EDITING_CANCELLED
#define gtk_entry_set_editing_cancelled(_e_,_b_) \
  g_object_set ((_e_), "editing-canceled", (gboolean)(_b_), NULL)
#endif

#ifndef HAVE_GTK_LAYOUT_GET_BIN_WINDOW
#define gtk_layout_get_bin_window(x) ((x)->bin_window)
#endif

#ifndef HAVE_GTK_SELECTION_DATA_GET_DATA
#define gtk_selection_data_get_data(_s_) ((_s_)->data)
#endif

#ifndef HAVE_GTK_SELECTION_DATA_GET_LENGTH
#define gtk_selection_data_get_length(_s_) ((_s_)->length)
#endif

#ifndef HAVE_GTK_SELECTION_DATA_GET_TARGET
#define gtk_selection_data_get_target(_s_) ((_s_)->target)
#endif

#ifndef HAVE_GTK_WIDGET_SET_VISIBLE
#define gtk_widget_set_visible(_w_,_v_) do { if (_v_) gtk_widget_show (_w_); else gtk_widget_hide (_w_); } while (0)
#endif

#ifndef HAVE_GTK_WIDGET_GET_VISIBLE
#define gtk_widget_get_visible(_w_) GTK_WIDGET_VISIBLE((_w_))
#endif

#ifndef HAVE_GTK_WIDGET_IS_SENSITIVE
#define gtk_widget_is_sensitive(w) GTK_WIDGET_IS_SENSITIVE ((w))
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
#define gtk_widget_get_style(w) ((w)->style)
#endif

#ifndef HAVE_GTK_WIDGET_HAS_FOCUS
#define gtk_widget_has_focus(w) GTK_WIDGET_HAS_FOCUS (w)
#endif

#ifndef HAVE_GTK_WIDGET_SET_CAN_DEFAULT
#define gtk_widget_set_can_default(w,t)					\
	do {								\
	if (t) GTK_WIDGET_SET_FLAGS ((w), GTK_CAN_DEFAULT);	\
		else GTK_WIDGET_UNSET_FLAGS ((w), GTK_CAN_DEFAULT);	\
	} while (0)
#endif

#ifndef HAVE_GTK_WIDGET_GET_CAN_FOCUS
#define gtk_widget_get_can_focus(_w_) GTK_WIDGET_CAN_FOCUS((_w_))
#endif

#ifndef HAVE_GTK_WIDGET_SET_CAN_FOCUS
#define gtk_widget_set_can_focus(w,t)					\
	do {								\
		if ((t)) GTK_WIDGET_SET_FLAGS ((w), GTK_CAN_FOCUS);	\
		else GTK_WIDGET_UNSET_FLAGS ((w), GTK_CAN_FOCUS);	\
	} while (0)		     
#endif

#ifndef HAVE_GTK_WIDGET_GET_REALIZED
#  ifdef HAVE_WORKING_GTK_WIDGET_REALIZED
#    define gtk_widget_get_realized(w) GTK_WIDGET_REALIZED ((w))
#  else
#    define gtk_widget_get_realized(wid) (((GTK_OBJECT (wid)->_g_sealed__flags) & GTK_REALIZED) != 0)
#  endif
#endif

#ifndef HAVE_GTK_ADJUSTMENT_CONFIGURE
#define gtk_adjustment_configure(_a,_v,_l,_u,_si,_pi,_ps)	\
  g_object_set ((_a),						\
                "lower", (double)(_l),				\
                "upper", (double)(_u),				\
                "step-increment", (double)(_si),		\
                "page-increment", (double)(_pi),		\
                "page-size", (double)(_ps),			\
		"value", (double)(_v),				\
                NULL)
#endif

#ifndef HAVE_GTK_ADJUSTMENT_GET_LOWER
#define gtk_adjustment_get_lower(_a) ((_a)->lower)
#endif

#ifndef HAVE_GTK_ADJUSTMENT_GET_UPPER
#define gtk_adjustment_get_upper(_a) ((_a)->upper)
#endif

#ifndef HAVE_GTK_ADJUSTMENT_GET_PAGE_SIZE
#define gtk_adjustment_get_page_size(_a) ((_a)->page_size)
#endif

#ifndef HAVE_GTK_ADJUSTMENT_GET_PAGE_INCREMENT
#define gtk_adjustment_get_page_increment(_a) ((_a)->page_increment)
#endif

#ifndef HAVE_GTK_ADJUSTMENT_GET_STEP_INCREMENT
#define gtk_adjustment_get_step_increment(_a) ((_a)->step_increment)
#endif

#ifndef HAVE_GTK_ADJUSTMENT_SET_LOWER
#define gtk_adjustment_set_lower(_a,_l) \
  g_object_set ((_a), "lower", (double)(_l), NULL)
#endif

#ifndef HAVE_GTK_ADJUSTMENT_SET_UPPER
#define gtk_adjustment_set_upper(_a,_u) \
  g_object_set ((_a), "upper", (double)(_u), NULL)
#endif

#ifndef HAVE_GTK_ADJUSTMENT_PAGE_INCREMENT
#define gtk_adjustment_set_page_increment(_a,_pi) \
  g_object_set ((_a), "page-increment", (double)(_pi), NULL)
#endif

#ifndef HAVE_GTK_ADJUSTMENT_STEP_INCREMENT
#define gtk_adjustment_set_step_increment(_a,_si) \
  g_object_set ((_a), "step-increment", (double)(_si), NULL)
#endif

#ifndef HAVE_GTK_TABLE_GET_SIZE
#  ifdef HAVE_GTK_TABLE_NROWS
#     define gtk_table_get_size(_t,_r,_c) do {	\
       int *_pr = (_r);				\
       int *_pc = (_c);				\
       GtkTable *_pt = (_t);			\
       if (_pr) *_pr = _pt->nrows;		\
       if (_pc) *_pc = _pt->ncols;		\
     } while (0)
#  else
     /* At first sealed with no accessors.  */
#     define gtk_table_get_size(_t,_r,_c) do {			\
       int *_pr = (_r);						\
       int *_pc = (_c);						\
       GtkTable *_pt = (_t);					\
       if (_pr) g_object_get (_pt, "n-rows", _pr, NULL);	\
       if (_pc) g_object_get (_pt, "n-columns", _pc, NULL);	\
     } while (0)
#  endif
#endif

/* This function does not exist in gtk+ yet.  634100.  */
#ifndef HAVE_GTK_TREE_VIEW_COLUMN_GET_BUTTON
#  ifdef HAVE_GTK_TREE_VIEW_COLUMN_BUTTON
#    define gtk_tree_view_column_get_button(_c) ((_c)->button)
#  else
#    define gtk_tree_view_column_get_button(_c) ((_c)->_g_sealed__button)
#  endif
#endif

#ifndef HAVE_GTK_WINDOW_GET_DEFAULT_WIDGET
#define gtk_window_get_default_widget(_w_) ((_w_)->default_widget)
#endif

#endif
