
INCLUDES = \
  $(GNUMERIC_CFLAGS) -I$(top_srcdir)/src \
  -DEGG_COMPILATION \
  -DGTK_DISABLE_DEPRECATED \
  -DGDK_DISABLE_DEPRECATED \
  -DG_DISABLE_DEPRECATED

noinst_LIBRARIES = libegg-toolbar.a

libegg_toolbar_a_LIBADD = 
libegg_toolbar_a_SOURCES = \
  eggtoolbar.c \
  eggtoolbutton.c \
  eggtoolitem.c

noinst_HEADERS = \
  eggtoolbar.h \
  eggtoolbutton.h \
  eggtoolitem.h	
