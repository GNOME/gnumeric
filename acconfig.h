#undef PACKAGE
#undef VERSION
#undef HAVE_LIBSM
#undef ENABLE_NLS
#undef HAVE_CATGETS
#undef HAVE_GETTEXT
#undef HAVE_LC_MESSAGES
#undef HAVE_STPCPY
#undef HAVE_ORBIT
#undef HAVE_GTK_SELECTION_ADD_TARGET
#undef HAVE_GUILE
#undef ENABLE_BONOBO
#undef HAVE_IEEEFP_H
#undef ENABLE_GNOME

@BOTTOM@
/* This is from libglade */

#ifdef DEBUG
#  define debug(stmnt) stmnt
#else
#  define debug(stmnt) /* nothing */
#endif

