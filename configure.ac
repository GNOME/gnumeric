-*- mode: m4 -*-
dnl We require Automake 1.7.2, which requires Autoconf 2.54.
dnl (It needs _AC_AM_CONFIG_HEADER_HOOK, for example.)
AC_PREREQ([2.54])
AC_CONFIG_MACRO_DIR([m4])
AX_IS_RELEASE([git-directory])

m4_define([gnumeric_version_epoch], [1])
m4_define([gnumeric_version_major], [12])
m4_define([gnumeric_version_minor], [60])
m4_define([gnumeric_version_extra], [])
m4_define([gnumeric_version_full],
    [gnumeric_version_epoch.gnumeric_version_major.gnumeric_version_minor[]gnumeric_version_extra])

dnl Emphasize some of the checks.
m4_define([BIG_CHECKING], [AC_MSG_CHECKING([
======== $1])])

AC_INIT([gnumeric],[gnumeric_version_full],[https://gitlab.gnome.org/GNOME/gnumeric/issues],[],[http://www.gnumeric.org/])

AC_CONFIG_SRCDIR(src/sheet.h)
AM_INIT_AUTOMAKE([dist-bzip2])

dnl Version info for libraries = CURRENT:REVISION:AGE
dnl
dnl Within each x.y.*, ABI is maintained backward and _forward_ compatible.
dnl (As a consequence, no exported function may be added.)
dnl So it's enough to have one interface number per each x.y.* branch.
dnl
dnl OTOH, we are not able to keep ABI strictly backward compatible throughout
dnl the whole x.*.*.
dnl The easiest way is to declare no ABI compatibility, ie. AGE is always 0.
dnl
m4_define([version_iface],
	m4_eval(100 * gnumeric_version_epoch + gnumeric_version_major))

AC_SUBST([VERSION_INFO], [version_iface:gnumeric_version_minor:0])
AC_SUBST([VERSION_IFACE], [version_iface])

if test `expr gnumeric_version_major % 2` -eq 1; then
    AC_MSG_NOTICE([NOTE: This is a development release])
    gnumeric_devel=yes
    gnumeric_api_ver=gnumeric_version_epoch.`expr gnumeric_version_major + 1`
    dnl An explicit version number should be used in place of the above
    dnl if the epoch is to change.
    dnl gnumeric_api_ver=2.0
else
    gnumeric_devel=no
    gnumeric_api_ver=gnumeric_version_epoch.gnumeric_version_major
fi
gnumeric_api_ver_=`echo $gnumeric_api_ver | sed -e 's/\./_/g'`

dnl Almost like epoch.major but development versions look forward to the next
dnl stable release.
AC_SUBST([GNUMERIC_API_VER], [$gnumeric_api_ver])
AC_SUBST([GNUMERIC_API_VER_], [$gnumeric_api_ver_])

dnl This one is created by autoheader, ...
AC_CONFIG_HEADERS(gnumeric-config.h)

dnl ... and this one is a small subset, maintained manually,
dnl which will be installed.
AC_CONFIG_HEADERS(gnumeric-features.h)
dnl
dnl Make sure these two won't clash.  Put the following to gnumeric-config.h:
AH_BOTTOM([/* Don't include gnumeric-features.h, it's a subset of gnumeric-config.h. */
#define GNUMERIC_FEATURES_H])

AM_MAINTAINER_MODE([enable])

m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

dnl AX_REQUIRE_DEFINED([GTK_DOC_CHECK])
GTK_DOC_CHECK

dnl We use a LINGUAS file, so we need intltool >= 0.35:
IT_PROG_INTLTOOL([0.35.0])
IT_PO_SUBDIR(po-functions)

dnl We use $host and $host_os:
AC_CANONICAL_HOST

AC_PROG_CC
AC_PROG_CC_C99
if test "$ac_cv_prog_cc_c99" = no; then
	AC_MSG_ERROR([*** c99 is required])
fi

AC_PROG_YACC
if test "x$ac_cv_prog_YACC" = x; then
	AC_MSG_ERROR([*** bison or equivalent is required])
fi

AC_PROG_LN_S

LT_PREREQ([2.2.6])
# Make --disable-static the default
LT_INIT([disable-static])

dnl Propagate Gnome-specific variable ACLOCAL_FLAGS to Makefile.
AC_SUBST(ACLOCAL_AMFLAGS, $ACLOCAL_FLAGS)

dnl *****************************
# Check for zlib.
dnl
dnl zlib is needed for the excel plugin; see plugins/excel/Makefile.am
dnl
_cppflags="${CPPFLAGS}"
_ldflags="${LDFLAGS}"

AC_ARG_WITH(zlib,
	AS_HELP_STRING([--with-zlib=DIR], [use libz in DIR]),
	[case $withval in
	 yes|no) ;;
	 *)	Z_DIR=$withval
		CPPFLAGS="${CPPFLAGS} -I$withval/include"
		LDFLAGS="${LDFLAGS} -L$withval/lib"
		;;
	 esac])

if test "x$with_zlib" != xno; then
	with_zlib=no
	AC_CHECK_HEADER(zlib.h, [AC_CHECK_LIB(z, gzread, [with_zlib=yes])])
fi

if test "$with_zlib" = no; then
	AC_MSG_ERROR([*** zlib is required])
fi

if test "x${Z_DIR}" != "x"; then
	Z_CPPFLAGS="-I${Z_DIR}/include"
	case ${host} in
	*-*-solaris*)	Z_LIBS="-L${Z_DIR}/lib -R${Z_DIR}/lib -lz" ;;
	*)		Z_LIBS="-L${Z_DIR}/lib -lz" ;;
	esac
else
	Z_LIBS="-lz"
fi
AC_SUBST(Z_CPPFLAGS)
AC_SUBST(Z_LIBS)

CPPFLAGS=${_cppflags}
LDFLAGS=${_ldflags}

dnl **********
dnl * Goffice
dnl **********
# Goffice releases are parallel installable, which do we depend on
GOFFICE_API_VER="0.10"
AC_SUBST(GOFFICE_API_VER)
# Components
GOFFICE_PLUGINS_DIR=`pkg-config --variable=PluginDir libgoffice-${GOFFICE_API_VER}`
AC_SUBST(GOFFICE_PLUGINS_DIR)
GOFFICE_VERSION=`pkg-config --modversion libgoffice-${GOFFICE_API_VER}`
AC_SUBST(GOFFICE_VERSION)


dnl PKG_PROG_PKG_CONFIG is expanded just before the first occurrence of
dnl PKG_CHECK_MODULES.  Since this is inside an `if,' it can escape
dnl execution.  Thus we need an explicit call:
PKG_PROG_PKG_CONFIG(0.18)

dnl *****************************
libspreadsheet_reqs="
	libgoffice-${GOFFICE_API_VER}	>= 0.10.57
	libgsf-1		>= 1.14.33
	libxml-2.0		>= 2.4.12
"
gnumeric_reqs="$libspreadsheet_reqs
	glib-2.0		>= 2.40.0
	gobject-2.0		>= 2.40.0
	gmodule-2.0		>= 2.40.0
	gthread-2.0		>= 2.40.0
	pango			>= 1.46.0
	pangocairo		>= 1.46.0
"
libspreadsheet_gtk_reqs="
	gtk+-3.0		>= 3.8.7
"

gnumeric_gtk_reqs="$libspreadsheet_gtk_reqs"

ui_msg=

with_win32=no
case $host_os in
mingw* | pw32* | cygwin*)
    with_win32=yes
    ;;
esac

dnl *******************
dnl Should we use gtk ?
dnl *******************
gnumeric_with_gtk=true
AC_ARG_WITH(gtk, AS_HELP_STRING([--without-gtk], [Build without UI]))
if test "x$with_gtk" = xno; then
	ui_msg="None (Gtk disabled by request)"
	gnumeric_with_gtk=false
	AC_MSG_ERROR([configuration without gtk+ currently does not work])
else
	dnl We shouldn't silently default to --without-gtk.
	dnl If the requirements are not met, fail.
	ui_msg="Gtk"
	PKG_CHECK_MODULES(GTK, [$libspreadsheet_gtk_reqs])
	PKG_CHECK_MODULES(GTK, [$gnumeric_gtk_reqs])
fi

if test "x$gnumeric_with_gtk" = xtrue ; then
	AC_DEFINE(GNM_WITH_GTK, 1, [Define if UI is built])
	libspreadsheet_reqs="$libspreadsheet_reqs $libspreadsheet_gtk_reqs"
	gnumeric_reqs="$gnumeric_reqs $gnumeric_gtk_reqs"
fi

AM_CONDITIONAL(WITH_GTK,   $gnumeric_with_gtk)

dnl ****************************
dnl now that we have selected out libraries the whole collection in one
dnl shot so that we can have a nice neat compile/link line
dnl ****************************

PKG_CHECK_MODULES(LIBSPREADSHEET, $libspreadsheet_reqs)
AC_SUBST(LIBSPREADSHEET_LIBS)
AC_SUBST(LIBSPREADSHEET_CFLAGS)

PKG_CHECK_MODULES(GNUMERIC, $gnumeric_reqs)
AC_SUBST(GNUMERIC_LIBS)
AC_SUBST(GNUMERIC_CFLAGS)

# We don't attempt to track gtk+4, so disable the very noise warnings
CFLAGS="$CFLAGS -DGDK_DISABLE_DEPRECATION_WARNINGS -DGTK_DISABLE_DEPRECATION_WARNINGS"

with_native_win32=no
case $host_os in
mingw* | pw32*)
    with_native_win32=yes
    AC_ARG_VAR(WINDRES, [The windres executable (used by win32 builds only).])
    AC_CHECK_TOOL(WINDRES, windres, :)
    ;;
esac
AM_CONDITIONAL(WITH_WIN32, test $with_win32 = yes)
AM_CONDITIONAL(WITH_NATIVE_WIN32, test $with_native_win32 = yes)
AM_CONDITIONAL(CROSS_COMPILING, test x"$cross_compiling" != xno)

if test "x$with_win32" = "xyes"; then
    GNUMERIC_PLUGIN_LDFLAGS="-avoid-version -no-undefined $GNUMERIC_LIBS -Wl,--enable-runtime-pseudo-reloc,--export-all-symbols \$(top_builddir)/src/libspreadsheet.la -s"
else
    GNUMERIC_PLUGIN_LDFLAGS="-avoid-version"
fi
AC_SUBST(GNUMERIC_PLUGIN_LDFLAGS)

dnl **************************************************
dnl This needs to be done before we add aggressive -Werror=foo flags
dnl below.
AC_SUBST(GETTEXT_PACKAGE, gnumeric-${VERSION})
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, "$GETTEXT_PACKAGE",
	[The package name, for gettext])
AM_GNU_GETTEXT_VERSION([0.13])
AM_GNU_GETTEXT([external])

dnl ****************************
dnl prep the pixmap generator
dnl ****************************

AC_ARG_VAR(GLIB_GENMARSHAL, [The glib-genmarshal executable.])
AC_CHECK_PROG(GLIB_GENMARSHAL, glib-genmarshal$ac_exeext, glib-genmarshal$ac_exeext)
GLIB_COMPILE_RESOURCES=`$PKG_CONFIG --variable=glib_compile_resources gio-2.0`
AC_SUBST(GLIB_COMPILE_RESOURCES)

AC_ARG_VAR(GDK_PIXBUF_CSOURCE, [The gdk-pixbuf-csource executable.])
AC_CHECK_PROG(GDK_PIXBUF_CSOURCE, gdk-pixbuf-csource$ac_exeext, gdk-pixbuf-csource$ac_exeext)

AC_ARG_ENABLE(component,
  AS_HELP_STRING([--disable-component], [Do not build the goffice component]),
  [], [enable_component=yes])
component_dir=""
if test x"$enable_component" = xyes; then
	component_dir="component"
else
	component_dir=""
fi
AC_SUBST(COMPONENT_DIR, $component_dir)

dnl ****************************
dnl GDA Plugin
dnl ****************************
try_gda=true
gda_msg=yes
gnomedb_msg=no
AC_ARG_WITH(gda,
	AS_HELP_STRING([--with-gda], [Compile the Gnome Database Access plugin]),
	if test "x$withval" != xyes; then
		try_gda=false
		gda_msg="Disabled by request"
	fi
)
if test "$try_gda" = true; then
	PKG_CHECK_MODULES(GDA, [libgda-6.0 >= 6.0.0, libgda-ui-6.0 >= 6.0.0],
		[gda_msg=yes],
		[gda_msg="NO.  libgda problem"])
	if test "$gda_msg" = yes; then
		PKG_CHECK_MODULES(GNOMEDB, [libgnomedb-4.0 >= 3.99.6],
			[gnomedb_msg="yes"],
			[gnomedb_msg="NO. libgnomedb problem"])
		if test "$gnomedb_msg" = yes; then
			AC_DEFINE(HAVE_LIBGNOMEDB, 1, [ Define if libgnomedb support is compiled in])
		fi
	fi
fi
AM_CONDITIONAL(WITH_GDA, test x"$gda_msg" = xyes)
AC_SUBST(GDA_CFLAGS)
AC_SUBST(GDA_LIBS)

AM_CONDITIONAL(WITH_GNOMEDB, test x"$gnomedb_msg" = xyes)
AC_SUBST(GNOMEDB_CFLAGS)
AC_SUBST(GNOMEDB_LIBS)

dnl ****************************

AC_CHECK_DECL(fdopen, fdopen_works=yes, fdopen_works=no)

if test $fdopen_works = no ; then
	unset ac_cv_have_decl_fdopen
	CFLAGS="$CFLAGS -D_POSIX_SOURCE"
	AC_MSG_NOTICE([adding -D_POSIX_SOURCE to CFLAGS])
	AC_CHECK_DECL(fdopen, fdopen_works=yes, fdopen_works=no)
	if test $fdopen_works = no ; then
		AC_MSG_ERROR([fdopen is not available])
	fi
fi

# Unfortunately, -D_POSIX_SOURCE turns off struct timeval on Solaris
AC_MSG_CHECKING([whether struct timeval is available])
AC_COMPILE_IFELSE(
	[AC_LANG_PROGRAM([[#include <sys/time.h>]], [[struct timeval tv;]])],
	[struct_timeval_works=yes],
	[struct_timeval_works=no])
AC_MSG_RESULT($struct_timeval_works)

if test $struct_timeval_works = no ; then
	CFLAGS="$CFLAGS -D__EXTENSIONS__"
	AC_MSG_CHECKING([whether struct timeval is available with -D__EXTENSIONS__])
	AC_COMPILE_IFELSE(
		[AC_LANG_PROGRAM([[#include <sys/time.h>]], [[struct timeval tv;]])],
		[struct_timeval_works=yes],
		[struct_timeval_works=no])
	AC_MSG_RESULT($struct_timeval_works)
	if test $struct_timeval_works = no ; then
		AC_MSG_ERROR([struct timeval is not available])
	fi
fi

dnl src/functions/fn-math.c uses M_PI

AC_MSG_CHECKING([whether M_PI is available])
AC_COMPILE_IFELSE(
	[AC_LANG_PROGRAM([[#include <math.h>]], [[double f = M_PI]])],
	[works_without_bsd_source=yes],
	[works_without_bsd_source=no])
AC_MSG_RESULT($works_without_bsd_source)

if test $works_without_bsd_source = no ; then
	CFLAGS="$CFLAGS -D_BSD_SOURCE"
	AC_MSG_CHECKING([whether M_PI is available with -D_BSD_SOURCE])
	AC_COMPILE_IFELSE(
		[AC_LANG_PROGRAM([[#include <math.h>]], [[double f = M_PI]])],
		[m_pi_works=yes],
		[m_pi_works=no])
	AC_MSG_RESULT($m_pi_works)
	if test $m_pi_works = no ; then
		AC_MSG_ERROR([M_PI is not available])
	fi
fi


AC_SUBST(GUILE_LIBS)
AC_SUBST(GUILE_INCS)

dnl Comment out the guile block:
ifelse([
dnl check for guile 1.5
try_guile=false
guile_msg="disabled pending some guile developer interest"
enable_guile=false
AC_ARG_WITH(guile,
	AS_HELP_STRING([--with-guile], [Compile with Guile support or without it]),
	if test "x$withval" = xyes; then
		try_guile=true
	fi
)
if $try_guile; then
	AC_MSG_CHECKING(for guile >= 1.5)
	if test "`guile -c '(display (string>=? (version) "1.4.1"))'`" != "#t"; then
  		AC_MSG_RESULT([Your Guile is too old.  You need Guile 1.5 or later.])
	else
	        AC_MSG_RESULT(found)
		guile_msg="yes"
		enable_guile=true
	fi
fi
if $enable_guile; then
	GNOME_CHECK_GUILE
fi
AM_CONDITIONAL(WITH_GUILE, $enable_guile)
])

dnl **************************************************
dnl * psiconv support
dnl **************************************************

AC_ARG_WITH(psiconv,
	AS_HELP_STRING([--without-psiconv], [Compile without Psiconv support]))
AC_ARG_VAR(PSICONV_CONFIG, [The psiconv-config executable.])
if test "x$with_psiconv" != xno; then
    AC_CHECK_PROG(PSICONV_CONFIG,psiconv-config,psiconv-config)
    if test x"$PSICONV_CONFIG" = x; then
	psiconv_msg="Missing dependencies"
    else
	AC_MSG_CHECKING(for psiconv >= 0.9.3)
	if $PSICONV_CONFIG --version | awk -F. \
		'{exit !($1 > 0 || $2 > 9 || ($2 == 9 && $3 >= 3))}'
	then
	    PSICONV_CFLAGS=`$PSICONV_CONFIG --cflags`
            PSICONV_LIBS=`$PSICONV_CONFIG --libs`
	    psiconv_msg=yes
        else
	    psiconv_msg=no
        fi
	AC_MSG_RESULT($psiconv_msg)
    fi
else
    psiconv_msg=no
fi
AM_CONDITIONAL(WITH_PSICONV, test "x$psiconv_msg" = xyes)
AC_SUBST(PSICONV_LIBS)
AC_SUBST(PSICONV_CFLAGS)

try_paradox=true
enable_paradox=false
AC_ARG_WITH(paradox,
	AS_HELP_STRING([--without-paradox], [Compile without Paradox support]),
	if test "x$withval" = xno; then
		try_paradox=false
	fi
)

if $try_paradox; then
	PKG_CHECK_MODULES(PARADOX,
		[pxlib >= 0.4.0],
		[paradox_msg="yes"],
		[paradox_msg="missing dependencies"])
fi

if test "x$paradox_msg" = xyes; then
	SAVE_LIBS="$LIBS"
	LIBS="$LIBS $PARADOX_LIBS"
	AC_CHECK_FUNC(PX_open_gsf,[ enable_paradox=true ],)
	LIBS="$SAVE_LIBS"
	unset SAVE_LIBS
fi
AM_CONDITIONAL(WITH_PARADOX, $enable_paradox)
AC_SUBST(PARADOX_LIBS)
AC_SUBST(PARADOX_CFLAGS)

plugin_list_given=false
PLUGIN_LIST=""
AC_ARG_ENABLE(plugins,
	AS_HELP_STRING([--enable-plugins="text html"], [Compile only the listed plugins]),
	[plugin_list_given=true
	 test "x$enableval" != xno && PLUGIN_LIST=$enableval
])
AM_CONDITIONAL(PLUGIN_LIST_GIVEN, $plugin_list_given)
AC_SUBST(PLUGIN_LIST)

POFILES_FULL=
for lang in $ALL_LINGUAS; do
    POFILES_FULL="$POFILES_FULL \$(top_srcdir)/po/$lang.po"
done
AC_SUBST(POFILES_FULL)

dnl
dnl Code to handle po-functions/Makefile* and po-functions/POTFILES*
dnl
dnl First, let me document how the po/ directory is dealt with.
dnl Dark mysteries here, so skip the next paragraph if you don't have enough
dnl holy water at your hand.  You have been warned.
dnl
dnl AM_GLIB_GNU_GETTEXT creates an incorrect incarnation of po/POTFILES, as
dnl glib-gettext.m4 doesn't know about intltool tags.  Later on, just before
dnl config.status is created, a code originating from intltool.m4 creates
dnl POTFILES again, this time correctly removing the intltool tags.
dnl config.status then creates po/Makefile, again in two steps.  First,
dnl po/Makefile.in is created from po/Makefile.in.in the usual way, and then
dnl a code registered via the obsolete macro AC_CONFIG_COMMANDS inserts the
dnl contents of file POTFILES to po/Makefile.
dnl
dnl But both glib-gettext and intltool have the dirname "po/" hardwired, so
dnl we are on our own with po-functions.
dnl
dnl We use a more straightforward approach for po-functions:
dnl 1) We create POTFILES immediately here.
dnl 2) We let config.status to create Makefile.in.
dnl 3) We use AC_CONFIG_COMMANDS to create Makefile.
dnl
[# Create po-functions/POTFILES:
if test -d po-functions; then
	rm -f po-functions/POTFILES
else
	mkdir po-functions
fi
case "$srcdir" in
	.)  top_srcdir=.. ;;
	[\\/]* | ?:[\\/]* )  # Absolute name.
		top_srcdir="$srcdir" ;;
	*)  top_srcdir="../$srcdir" ;;
esac
sed <$srcdir/po-functions/POTFILES.in -e '/^#/d' -e 's/^[[].*[]] *//' \
	 -e '/^[   ]*$/d' -e "s,^,       $top_srcdir/," | \
        sed -e '$!s/$/ \\/' >po-functions/POTFILES
]
dnl Finally, register for creation of po-functions/Makefile:
dnl We do not currently do anything here.
AC_CONFIG_COMMANDS(po-functions/Makefile,
  [cp po-functions/Makefile.in po-functions/Makefile])

SAVE_LIBS="$LIBS"
LIBS="$LIBS $GNUMERIC_LIBS"
AC_CHECK_FUNCS(mkfifo)
LIBS="$SAVE_LIBS"
unset SAVE_LIBS

AC_HEADER_SYS_WAIT

dnl Check for some functions
AC_CHECK_FUNCS(uname setenv putenv)

dnl FIXME: Does this really belong here?
AC_CHECK_FUNC(bind_textdomain_codeset,,[AC_CHECK_LIB(intl,bind_textdomain_codeset)])

dnl check for complete locale implementation
AC_CHECK_HEADERS(langinfo.h)

dnl check for rlimit
AC_CHECK_HEADERS(sys/resource.h)

dnl we need to check fpu mode
AC_CHECK_HEADERS(fpu_control.h)

SAVE_CFLAGS=$CFLAGS
SAVE_LIBS=$LIBS
CFLAGS="$CFLAGS $GNUMERIC_CFLAGS"
LIBS="$GNUMERIC_LIBS $LIBS"

AC_CHECK_FUNCS(gtk_cell_renderer_text_get_background_set gtk_cell_renderer_text_get_foreground_set gtk_cell_renderer_text_get_editable)
AC_CHECK_FUNCS(gtk_entry_set_editing_cancelled gtk_icon_theme_add_resource_path gtk_widget_class_set_css_name)

AC_MSG_CHECKING([for OO_NS_LOCALC_EXT])
AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <gsf/gsf.h>]],
				[[(void)(OO_NS_LOCALC_EXT == OO_NS_LOCALC_EXT);]])],
               [AC_DEFINE(HAVE_OO_NS_LOCALC_EXT, 1, [Define if OO_NS_LOCALC_EXT is available])
		AC_MSG_RESULT(yes)],
	       [AC_MSG_RESULT(no)])

CFLAGS=$SAVE_CFLAGS
LIBS=$SAVE_LIBS

dnl We supply our own lgamma and lgamma_r when missing.
supply_lgamma_r=yes
AC_CHECK_FUNCS(lgamma_r)
if test $ac_cv_func_lgamma_r = no; then
  AC_CHECK_LIB(m, lgamma_r,
        [AC_DEFINE(HAVE_LGAMMA_R, 1,
		[Define if the lgamma_r function is available]
	 )
	 supply_lgamma_r=no
         LIBS="$LIBS -lm"])
else
  supply_lgamma_r=no
fi
if test $supply_lgamma_r = yes; then
   AC_DEFINE(GNM_SUPPLIES_LGAMMA_R, 1,
	[Define if Gnumeric supplies the lgamma_r function]
	)
fi
AC_CHECK_FUNCS(lgamma)
if test $ac_cv_func_lgamma = no; then
   AC_DEFINE(GNM_SUPPLIES_LGAMMA, 1,
	[Define if Gnumeric supplies the lgamma function]
	)
fi

cos_works=yes
AC_MSG_CHECKING([cos is reliable])
AC_RUN_IFELSE(
	[AC_LANG_PROGRAM([[
			#include <stdlib.h>
			#include <math.h>
		]],
		[[
			const char *s = "1.5689793435451356";
			double x = atof (s);
			double c = cos (x);
			int ok = (c > 0 && c < 0.05);
			return ok ? 0 : 1;
		]])],
	[AC_MSG_RESULT(yes)
	 cos_works=yes],
	[AC_MSG_RESULT(no)
	 cos_works=no],
	[AC_MSG_RESULT(assuming it is)])

if test "x$with_win32" = "xyes" -o "x$cos_works" = "xno"; then
   AC_DEFINE(GNM_REDUCES_TRIG_RANGE, 1,
	[Define if Gnumeric reduces the argument range of sin/cos/tan]
	)
fi

float_msg=double
float_msg_default=yes
AC_ARG_WITH(long_double,
	AS_HELP_STRING([--with-long-double], [Use long double for floating point]),
	[if test "x$withval" = xyes; then
		if test $float_msg_default = no; then
		   AC_MSG_ERROR([Incompatible number system options supplied])
		fi

		SAVE_CFLAGS="$CFLAGS"
		SAVE_LIBS="$LIBS"
		CFLAGS="$GNUMERIC_CFLAGS"
		LIBS="$GNUMERIC_LIBS -lm"

		AC_CHECK_HEADERS(sunmath.h)

		AC_CHECK_FUNCS(go_render_generall,
			       ,
			       AC_MSG_ERROR([libgoffice needs to be compiled with long double support.]))

		AC_CHECK_FUNCS(fabsl sqrtl expl expm1l logl log2l log10l log1pl ceill floorl powl hypotl \
				sinl cosl tanl asinl acosl atanl atan2l fmodl lgammal lgammal_r \
				sinhl coshl tanhl asinhl acoshl atanhl \
				isnanl finitel,
				,
				AC_MSG_ERROR([Long doubles require the $ldfunc function.]))

		AC_MSG_CHECKING([checking for working erfl and erfcl])
		have_erfl=no
		have_erfcl=no
		AC_RUN_IFELSE(
			[AC_LANG_PROGRAM([[
				#include <math.h>
				#ifdef HAVE_SUNMATH_H
				#include <sunmath.h>
				#endif
				]],
				[[
					long double l1 = erfl (1.2L);
					long double l2 = erfcl (1.2L);
					return !(l1 >= 0.91 && l1 <= 0.92 &&
						 l2 >= 0.08 && l2 <= 0.09);
				]])],
			[AC_DEFINE(HAVE_ERFL)
			 have_erfl=yes
			 AC_DEFINE(HAVE_ERFCL)
			 have_erfcl=yes
			AC_MSG_RESULT(yes)],
			AC_MSG_RESULT(no),
			[AC_CHECK_FUNCS(erfl erfcl)
			have_erfl=$ac_cv_func_erfl
			have_erfcl=$ac_cv_func_erfcl
			])

		if test $have_erfl = no; then
			AC_DEFINE(GNM_SUPPLIES_ERFL, 1,
				 [Define if Gnumeric supplies the erfl function]
				 )
		fi
		if test $have_erfcl = no; then
			AC_DEFINE(GNM_SUPPLIES_ERFCL, 1,
				 [Define if Gnumeric supplies the erfcl function]
				 )
		fi

		CFLAGS="$SAVE_CFLAGS"
		LIBS="$SAVE_LIBS"

		float_msg="long double (EXPERIMENTAL)"
		float_msg_default=no
		AC_MSG_WARN([Long double support is experimental -- expect problems])
		AC_DEFINE(GNM_WITH_LONG_DOUBLE, 1,
			[Define if the long double type is to be used]
		)
	fi]
)

gnumeric_with_decimal64=false
AC_ARG_WITH(decimal64,
	AS_HELP_STRING([--with-decimal], [Use Decimal64 for floating point]),
	[if test "x$withval" = xyes; then
		if test $float_msg_default = no; then
		   AC_MSG_ERROR([Incompatible number system options supplied])
		fi
		SAVE_CFLAGS="$CFLAGS"
		SAVE_LIBS="$LIBS"
		CFLAGS="$GNUMERIC_CFLAGS"
		LIBS="$GNUMERIC_LIBS -lm"

		AC_CHECK_FUNCS(go_finiteD,
			       ,
			       AC_MSG_ERROR([libgoffice needs to be compiled with Decimal64 support.]))
		CFLAGS="$SAVE_CFLAGS"
		LIBS="$SAVE_LIBS"
		AC_DEFINE(GNM_WITH_DECIMAL64, 1,
			[Define if the Decimal64 type is to be used]
		)
		gnumeric_with_decimal64=true
		float_msg="Decimal64 (EXPERIMENTAL)"
		float_msg_default=no
		AC_MSG_WARN([Decimal64 support is experimental -- expect problems])
	fi]
)
AM_CONDITIONAL(WITH_DECIMAL64, $gnumeric_with_decimal64)



## this should come after `AC_PROG_CC'
set_more_warnings=yes
if test "$GCC" = yes -a "x$set_more_warnings" != xno; then
	dnl Clang needs this option, or else it will appear to support any
	dnl warning option, only to spew warnings about them later.
	uwoption="-Werror=unknown-warning-option"
	SAVE_CFLAGS="$CFLAGS"
	CFLAGS="$CFLAGS $uwoption"
	AC_MSG_CHECKING([whether gcc understands $uwoption])
	AC_COMPILE_IFELSE(
		[AC_LANG_PROGRAM([], [])],
		[has_unknown_warning_option=yes],
		[has_unknown_warning_option=no; uwoption=""])
	AC_MSG_RESULT($has_unknown_warning_option)
	CFLAGS="$SAVE_CFLAGS"

	warning_options="-Wall -Werror=init-self -Werror=missing-include-dirs \
			 -Wsign-compare -Werror=pointer-arith \
			 -Wchar-subscripts -Wwrite-strings \
			 -Wnested-externs \
			 -Wmissing-noreturn \
			 -Werror=missing-prototypes -Werror=nested-externs \
			 -Werror=implicit-function-declaration \
			 -Wmissing-declarations -Wno-pointer-sign \
			 -Werror=format-security -Wbitwise -Wcast-to-as \
			 -Wdefault-bitfield-sign -Wdo-while -Wparen-string \
			 -Wptr-subtraction-blows -Wreturn-void -Wtypesign \
			 -Wstrict-prototypes -Wno-error=format-nonliteral "
	for option in $warning_options ; do
		SAVE_CFLAGS="$CFLAGS"
		CFLAGS="$CFLAGS $uwoption $option"
		AC_MSG_CHECKING([whether gcc understands $option])
		dnl Include a system header so we ignore Werror=... flags
		dnl that cause trouble.
		AC_COMPILE_IFELSE(
			[AC_LANG_PROGRAM([[#include <stdio.h>]], [])],
			[has_option=yes],
			[has_option=no])
		CFLAGS="$SAVE_CFLAGS"
		if test $has_option = yes; then
			CFLAGS="$CFLAGS $option"
		fi
		AC_MSG_RESULT($has_option)
		unset has_option
		unset SAVE_CFLAGS
	done
	unset option
fi
AC_SUBST(WARN_CFLAGS)

if test "$ac_cv_func_lgamma_r" = yes -o "$ac_cv_lib_m_lgamma_r" = yes; then
	AC_MSG_CHECKING([if we must prototype lgamma_r ourselves])
	AC_RUN_IFELSE(
		[AC_LANG_PROGRAM([[#include <math.h>
#include <stdlib.h>]],
			[[
				const char *s = "-3.1415e+0";
				int sign;
				double res = lgamma_r (atof(s), &sign);
				return !(res >= 0.01630610 && res <= 0.01630611 && sign == +1);
			]])],
		[AC_MSG_RESULT(no)],
		[AC_MSG_RESULT(yes)
		 AC_DEFINE([NEEDS_LGAMMA_R_PROTOTYPE], 1,
			   [Define if we must prototype lgamma_r.])
		],
		[AC_MSG_RESULT(assuming not)])
fi

dnl **************************************************
dnl * Check for GSettings
dnl **************************************************
have_gsettings=no
AC_CHECK_LIB(gio-2.0,[g_settings_new],
	[AC_DEFINE(HAVE_G_SETTINGS_NEW, 1,
		[Define if the g_settings_new function is available])
        GLIB_GSETTINGS
        have_gsettings=yes],
	[GSETTINGS_RULES=""
	AC_SUBST(GSETTINGS_RULES)])
AM_CONDITIONAL(HAVE_GSETTINGS, test $have_gsettings=yes)

dnl **************************************************
dnl * Check for Perl
dnl **************************************************

AC_ARG_VAR(PERL, [The Perl executable.])
AC_CHECK_PROG(PERL, perl, perl)

want_perl=auto
have_perl=no
perl_reason=""
AC_ARG_WITH(perl, AS_HELP_STRING([--without-perl], [Do not build the Perl plugin loader]),
	[case $withval in
	yes) want_perl=yes;;
	no) want_perl=no;;
	esac])
if test "x$want_perl" = xno ; then
    perl_reason="disabled by request"
else
    if test "x$PERL" != x; then
	BIG_CHECKING([for perl ExtUtils::Embed module])
	if $PERL -e 'eval { require ExtUtils::Embed }; if ($@) { exit(1); } else { exit(0); }'
	then
	    AC_MSG_RESULT(yes)

	    dnl Use ExtUtils::Embed to figure out the other options.
	    PERL_CCCDLFLAGS=`$PERL -MConfig -e 'print $Config{cccdlflags};'`
	    PERL_LDDLFLAGS=`$PERL -MConfig -e 'print $Config{lddlflags};'`
	    PERL_CC=`$PERL -MConfig -e 'print $Config{cc};'`
	    PERL_LD=`$PERL -MConfig -e 'print $Config{ld};'`
	    perlinc=`$PERL -MConfig -e 'print "$Config{archlib}/CORE";'`
	    PERL_CCOPTS="`$PERL -MExtUtils::Embed -e ccopts` -I$perlinc"
	    PERL_LDOPTS=`$PERL -MExtUtils::Embed -e ldopts`

	    AC_MSG_CHECKING([Checking for perl compiler, linker, libraries and headers])
	    rm -f testperl$ac_exeext testperl.$ac_objext testperl.c testperl.err
	    cat > testperl.c <<EOF
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"
int main () { (void)&perl_construct; return 0; }
EOF
	    $PERL_CC $PERL_CCOPTS -c testperl.c >testperl.err 2>&1 &&
	    $PERL_LD -o testperl testperl.$ac_objext $PERL_LDOPTS >testperl.err 2>&1 &&
	    test -x testperl &&
	    have_perl=yes
	    AC_MSG_RESULT($have_perl)

	    if test "x$have_perl" != xyes; then
		perl_reason="failed to compile test program"
		cat testperl.err testperl.c >&AS_MESSAGE_LOG_FD
	    fi

	    rm -f testperl$ac_exeext testperl.$ac_objext testperl.c testperl.err

	else
	    AC_MSG_RESULT(no)
	    perl_reason="missing parts of perl"
	fi
    else
	perl_reason="missing perl"
    fi
fi

if test "$want_perl" = xyes -a "x$have_perl" != xyes; then
    AC_MSG_ERROR([Perl requested, but not available: $perl_reason])
fi

AM_CONDITIONAL(WITH_PERL, test "x$have_perl" = xyes)
if test "x$have_perl" = xyes ; then
    perl_msg="yes (using $PERL)"
else
    perl_msg="No. ($perl_reason)"
fi
AC_SUBST(PERL_CCCDLFLAGS)
AC_SUBST(PERL_LDDLFLAGS)
AC_SUBST(PERL_CC)
AC_SUBST(PERL_LD)
AC_SUBST(PERL_CCOPTS)
AC_SUBST(PERL_LDOPTS)

dnl **************************************************
dnl * Check for Python
dnl **************************************************
python_msg="yes"
GNM_PY_CFLAGS=
GNM_PY_LDFLAGS=
GNM_PY_LIBADD=

AC_ARG_WITH(python, AS_HELP_STRING([--without-python], [Do not build the Python plugin loader]))
AC_ARG_VAR(PYTHON, [The Python executable.])
case $with_python in
yes|no) ;;
?*) AC_MSG_ERROR([You cannot use --with-python with an argument.
If you want to specify a path to your Python executable, use:
	$srcdir/configure PYTHON=/path/to/your/python ...]);;
esac
if test "x$with_python" != xno; then
    AC_PATH_PROGS(PYTHON, python3 python)
    if test "x$PYTHON" = x; then
	python_msg="could not find python"
    fi
else
    python_msg="Disabled by request"
fi

# A bit of a mess.  We really ought to separate the Python interpreter
# and the Python plugin
have_python=no
have_python_gi=no
introspection_py_msg=no
PYTHON_GIOVERRIDESDIR=
if test "x$PYTHON" != x; then
    AC_DEFINE_UNQUOTED(PYTHON_INTERPRETER, ["$PYTHON"],
		       [The Python interpreter to use])
    have_python=yes

    AC_MSG_CHECKING([for python module gi])
    if $PYTHON -c 'import gi' 2>/dev/null; then
        have_python_gi=yes
	introspection_py_msg="Yes."
	[PYTHON_GIOVERRIDESDIR=`$PYTHON -c 'import gi; import os.path; print(os.path.dirname(gi.__file__)+"/overrides")'`]
    fi
    AC_MSG_RESULT($have_python_gi)
fi
AM_CONDITIONAL(HAVE_PYTHON_GI, test "x$have_python_gi" = "xyes")


if test "x$python_msg" = xyes; then
    BIG_CHECKING([for python >= 3.x])
    [major_ver=`$PYTHON -c 'import sys ; sys.stdout.write(str(sys.version_info[0]))'`]
    if test "$major_ver" -ge 3; then
	AC_MSG_RESULT(yes)
    else
        pyversion=`$PYTHON --version`
	AC_MSG_RESULT([no. $PYTHON is $pyversion])
    fi
fi

if test "x$python_msg" = xyes; then
    if test "x$PY_PREFIX" = x; then
	PY_PREFIX=`$PYTHON -c 'import sys ; sys.stdout.write(sys.prefix)'`
    fi
    if test "x$PY_INCLUDE_DIR" = x; then
	if test "x$with_native_win32" = xyes ; then
	    PY_INCLUDES="-I$PY_PREFIX/include"
	else
	    PY_INCLUDES=`$PYTHON-config --includes`
	fi
    else
	PY_INCLUDES="-I$PY_INCLUDE_DIR"
    fi

    python_msg="unable to find Python.h"
    for py_include in $PY_INCLUDES; do
	py_include_dir="${py_include#-I}"
	BIG_CHECKING(Look for $py_include_dir/Python.h)
	if test -f $py_include_dir/Python.h; then
	    AC_MSG_RESULT(yes)
	    python_msg="yes"
	    break
	else
	    AC_MSG_RESULT(no)
	fi
    done
fi

if test "x$python_msg" = xyes; then
    m4_define([pygobject_required_version], [3.0.0])
    PKG_CHECK_MODULES(PYGOBJECT, pygobject-3.0 >= pygobject_required_version,,
      [python_msg="Missing pygobject"])
fi

AM_CONDITIONAL(WITH_PYTHON, test "x$python_msg" = xyes)
if test "x$python_msg" = xyes; then
    if test "x$PY_LIB_DIR" = x; then
	if test "x$with_native_win32" = xyes ; then
	    PY_LIB_DIR="$PY_PREFIX/libs"
	    PY_LIBS="-L$PY_LIB_DIR"
	    PY_DASHLPYTHON=FIXME
	else
	    PY_LIBS=`$PYTHON-config --ldflags`
	    PY_DASHLPYTHON=`$PYTHON -c 'import sysconfig; print(sysconfig.get_config_var("BLDLIBRARY"), end="");'`
	fi
    else
	PY_LIBS="-L$PY_LIB_DIR"
    fi

    GNM_PY_CFLAGS="$PY_INCLUDES $PYGOBJECT_CFLAGS"
    GNM_PY_LDFLAGS="$PY_LIBS $PY_DASHLPYTHON $PYGOBJECT_LIBS"
    python_msg="yes (using $PYTHON)"
elif test "x$with_python" = "xyes"; then
		AC_MSG_ERROR([$python_msg])
fi

AC_SUBST(GNM_PY_CFLAGS)
AC_SUBST(GNM_PY_LDFLAGS)
AC_SUBST(GNM_PY_LIBADD)

YELP_HELP_INIT(no-lc-dist)

# Support for PDF docs
AC_ARG_ENABLE(pdfdocs,
  AS_HELP_STRING([--enable-pdfdocs],[Generate documentation in Portable Document Format]),
  [enable_pdfdocs=$enableval],
  [enable_pdfdocs=no])
pdf_msg="No, not requested.";
if test x"$enable_pdfdocs" = xyes; then
	pdfroute=""
	AC_CHECK_PROG(DBCONTEXT, [dbcontext], [dbcontext], [])
	AC_CHECK_PROG(DBLATEX, [dblatex], [dblatex], [])
	if test x"$DBCONTEXT" = x"dbcontext"; then
		pdfroute=dbcontext
	else
		if test x"$DBLATEX" = x"dblatex"; then
			pdfroute=dblatex
		fi
	fi
	if test x"$pdfroute" != x""; then
		pdf_msg="Yes, through $pdfroute."
	else
		AC_MSG_ERROR([
Did not find a suitable tool for generating Portable Document Format from
DocBook XML - aborting.

Both the DocBook to ConTeXt and the DocBook to LaTeX conversion tool can be
obtained from
	http://dblatex.sourceforge.net/
])
		exit 1
	fi
fi
AM_CONDITIONAL(ENABLE_PDFDOCS, test x"$enable_pdfdocs" = xyes)
AM_CONDITIONAL(ENABLE_PDF_VIA_DBCONTEXT, test x"$pdfroute" = x"dbcontext")
AM_CONDITIONAL(ENABLE_PDF_VIA_DBLATEX, test x"$pdfroute" = x"dblatex")


dnl A tricky way to comment out in m4:
ifelse([
dnl **************************************************
dnl * Check for mono
dnl **************************************************
with_mono=no
mono_msg="disabled, still experimental"

AC_ARG_WITH(mono, AS_HELP_STRING([--with-mono], [Compile the mono scripting engine]))
case $with_mono in
no)	mono_msg="Disabled by request";;
*)
    PKG_CHECK_MODULES(MONO, mono,
	[with_mono=yes
	 AC_DEFINE(WITH_MONO, 1, [Define if mono .NET engine is available])],
	[with_mono=no])
    mono_msg=$with_mono
    ;;
esac
AM_CONDITIONAL(WITH_MONO, test "x$with_mono" = xyes)
AC_SUBST(MONO_CFLAGS)
AC_SUBST(MONO_LIBS)
])

# GObject Introspection
GIR_REQ=1.0.0
AC_ARG_ENABLE(introspection,
          AS_HELP_STRING([--enable-introspection[=@<:@no/auto/yes@:>@]],
                         [Enable introspection for this build]),, 
                         [enable_introspection=no])

AC_MSG_CHECKING([for gobject-introspection])

dnl presence/version checking
AS_CASE([$enable_introspection],
[no], [
    found_introspection="no (disabled, use --enable-introspection to enable)"
    introspection_py_msg="No, introspection disabled"
],
[yes],[
    PKG_CHECK_EXISTS([gobject-introspection-1.0],,
                     AC_MSG_ERROR([gobject-introspection-1.0 is not installed]))
    PKG_CHECK_EXISTS([gobject-introspection-1.0 >= $GIR_REQ],
                     found_introspection=yes,
                     AC_MSG_ERROR([You need to have gobject-introspection >= $1 installed to build AC_PACKAGE_NAME]))
],
[auto],[
    PKG_CHECK_EXISTS([gobject-introspection-1.0 >= $GIR_REQ], found_introspection=yes, found_introspection=no)
dnl Canonicalize enable_introspection
enable_introspection=$found_introspection
],
[	
    AC_MSG_ERROR([invalid argument passed to --enable-introspection, should be one of @<:@no/auto/yes@:>@])
])

AC_MSG_RESULT([$found_introspection])

if test "x$found_introspection" = "xyes"; then
   dnl You can override INTROSPECTION_GIRDIR and INTROSPECTION_TYPELIBDIR
   dnl if you wish.  If you override the latter, you might want to set
   dnl GI_TYPELIB_PATH to include the same directory.  For example
   dnl
   dnl GI_TYPELIB_PATH=$PREFIX/lib64/girepository-1.0
   dnl INTROSPECTION_TYPELIBDIR=$GI_TYPELIB_PATH
   dnl INTROSPECTION_GIRDIR=$PREFIX/share/gir-1.0
   dnl
   dnl Note, that unlike gnumeric's binaries, nothing tells python where
   dnl to find gnumeric libraries, so you might also need to set
   dnl LD_LIBRARY_PATH.

   INTROSPECTION_SCANNER=`$PKG_CONFIG --variable=g_ir_scanner gobject-introspection-1.0`
   INTROSPECTION_COMPILER=`$PKG_CONFIG --variable=g_ir_compiler gobject-introspection-1.0`
   INTROSPECTION_GENERATE=`$PKG_CONFIG --variable=g_ir_generate gobject-introspection-1.0`
   if test "x$INTROSPECTION_GIRDIR" = x; then
     INTROSPECTION_GIRDIR=`$PKG_CONFIG --variable=girdir gobject-introspection-1.0`
   fi
   if test "x$INTROSPECTION_TYPELIBDIR" = x; then
     INTROSPECTION_TYPELIBDIR="$($PKG_CONFIG --variable=typelibdir gobject-introspection-1.0)"
   fi
   INTROSPECTION_CFLAGS=`$PKG_CONFIG --cflags gobject-introspection-1.0`
   INTROSPECTION_LIBS=`$PKG_CONFIG --libs gobject-introspection-1.0`
   INTROSPECTION_MAKEFILE=`$PKG_CONFIG --variable=datadir gobject-introspection-1.0`/gobject-introspection-1.0/Makefile.introspection
else
   INTROSPECTION_SCANNER=
   INTROSPECTION_COMPILER=
   INTROSPECTION_GENERATE=
   INTROSPECTION_GIRDIR=
   INTROSPECTION_TYPELIBDIR=
   INTROSPECTION_CFLAGS=
   INTROSPECTION_LIBS=
   INTROSPECTION_MAKEFILE=
fi
AC_SUBST(INTROSPECTION_SCANNER)
AC_SUBST(INTROSPECTION_COMPILER)
AC_SUBST(INTROSPECTION_GENERATE)
AC_SUBST(INTROSPECTION_GIRDIR)
AC_SUBST(INTROSPECTION_TYPELIBDIR)
AC_SUBST(INTROSPECTION_CFLAGS)
AC_SUBST(INTROSPECTION_LIBS)
AC_SUBST(INTROSPECTION_MAKEFILE)

AM_CONDITIONAL(HAVE_INTROSPECTION, test "x$found_introspection" = "xyes")
dnl we need to change the install directories for distcheck
AC_ARG_WITH([gir-dir],
	AS_HELP_STRING(
		[--with-gir-dir],
		[
		 path to gir repository
		 (automatically detected via pkg-config)
		]
	),
	[GIRDIR=$withval],
	[GIRDIR=$INTROSPECTION_GIRDIR]
)
AC_SUBST(GIRDIR)
AC_ARG_WITH([typelib-dir],
	AS_HELP_STRING(
		[--with-typelib-dir],
		[
		 path to typelibs repository
		 (automatically detected via pkg-config)
		]
	),
	[TYPELIBDIR=$withval],
	[TYPELIBDIR=$INTROSPECTION_TYPELIBDIR]
)
AC_SUBST(TYPELIBDIR)
AC_ARG_WITH([gi-overrides-dir],
	AS_HELP_STRING(
		[--with-gi-overrides-dir],
		[
		 path to introspection overrides repository
		 (automatically detected via python)
		]
	),
	[GIOVERRIDESDIR=$withval],
	[GIOVERRIDESDIR=$PYTHON_GIOVERRIDESDIR]
)
AC_SUBST(GIOVERRIDESDIR)

AC_ARG_WITH([goffice-plugins-dir],
	AS_HELP_STRING(
		[--with-goffice-plugins-dir],
		[
		 path to goffice plugins
		 (automatically detected via pkg-config)
		]
	),
	[GOFFICE_PLUGINS_DIR=$withval],
	[:]
)


dnl **************************************************
dnl * Config defaults
dnl **************************************************
dnl
dnl These are changed in gutils.c for WIN32 packages
AC_SUBST(gnumeric_datadir, '${datadir}/gnumeric/${VERSION}')
AC_SUBST(gnumeric_libdir, '${libdir}/gnumeric/${VERSION}')
AC_SUBST(gnumeric_localedir, '${localedir}')
dnl
AC_SUBST(gnumeric_plugindir, '${gnumeric_libdir}/plugins')
AC_SUBST(gnumeric_externplugindir, '${libdir}/gnumeric/${GNUMERIC_API_VER}/plugins')

AC_SUBST(YYYYMMDD, `date +%Y%m%d`)

dnl Export to gnumeric-config.h
AC_DEFINE(GNM_VERSION_FULL, "gnumeric_version_full",
	[The version number of this release, with optional extra suffix])
AC_DEFINE(GNM_VERSION_EPOCH, gnumeric_version_epoch,
	[The Epoch of this release])
AC_DEFINE(GNM_VERSION_MAJOR, gnumeric_version_major,
	[The Major version number of this release])
AC_DEFINE(GNM_VERSION_MINOR, gnumeric_version_minor,
	[The Minor version number of this release])
AC_DEFINE(GNM_VERSION_EXTRA, "gnumeric_version_extra",
	[Extra, possibly empty tag for this release])
AC_DEFINE(GNM_API_VERSION, "GNUMERIC_API_VER",
	[The API version number of this release])

AC_CONFIG_FILES([
Makefile
libspreadsheet.pc
icons/Makefile
icons/16x16/Makefile
icons/16x16/actions/Makefile
icons/16x16/apps/Makefile
icons/22x22/Makefile
icons/22x22/actions/Makefile
icons/22x22/apps/Makefile
icons/24x24/Makefile
icons/24x24/actions/Makefile
icons/24x24/apps/Makefile
icons/32x32/Makefile
icons/32x32/actions/Makefile
icons/32x32/apps/Makefile
icons/48x48/Makefile
icons/48x48/actions/Makefile
icons/48x48/apps/Makefile
icons/64x64/Makefile
icons/64x64/actions/Makefile
icons/256x256/Makefile
icons/256x256/apps/Makefile
introspection/Makefile
src/Makefile
doc/Makefile
plugins/Makefile
plugins/fn-christian-date/Makefile
plugins/fn-complex/Makefile
plugins/fn-database/Makefile
plugins/fn-date/Makefile
plugins/fn-derivatives/Makefile
plugins/fn-eng/Makefile
plugins/fn-erlang/Makefile
plugins/fn-financial/Makefile
plugins/fn-flt/Makefile
plugins/fn-hebrew-date/Makefile
plugins/fn-info/Makefile
plugins/fn-logical/Makefile
plugins/fn-lookup/Makefile
plugins/fn-math/Makefile
plugins/fn-numtheory/Makefile
plugins/fn-r/Makefile
plugins/fn-stat/Makefile
plugins/fn-string/Makefile
plugins/fn-random/Makefile
plugins/fn-tsa/Makefile
plugins/applix/Makefile
plugins/dif/Makefile
plugins/excel/Makefile
plugins/excelplugins/Makefile
plugins/gda/Makefile
plugins/gnome-db/Makefile
plugins/gnome-glossary/Makefile
plugins/html/Makefile
plugins/lotus-123/Makefile
plugins/lpsolve/Makefile
plugins/nlsolve/Makefile
plugins/glpk/Makefile
plugins/mps/Makefile
plugins/oleo/Makefile
plugins/openoffice/Makefile
plugins/paradox/Makefile
plugins/perl-func/Makefile
plugins/perl-loader/Makefile
plugins/plan-perfect/Makefile
plugins/psiconv/Makefile
plugins/py-func/Makefile
plugins/python-loader/Makefile
plugins/qpro/Makefile
plugins/sample_datasource/Makefile
plugins/sc/Makefile
plugins/sylk/Makefile
plugins/uihello/Makefile
plugins/xbase/Makefile
po/Makefile.in
po-functions/Makefile.in
schemas/Makefile
schemas/org.gnome.gnumeric.gschema.xml.in
schemas/org.gnome.gnumeric.dialogs.gschema.xml.in
schemas/org.gnome.gnumeric.plugin.gschema.xml.in
templates/Makefile
templates/english/Makefile
templates/autoformat/Makefile
test/Makefile
tools/Makefile
tools/win32/Makefile
tools/win32/moduleset
tools/win32/gnumeric.nsi
component/Makefile
])
AC_OUTPUT

echo "

Configuration:

	Source code location:		${srcdir}
	Compiler:			${CC}
	Compiler flags:			${CFLAGS}
	Floating point type:		${float_msg}

	UI:				${ui_msg}

	Perl plugin support:		${perl_msg}
	Python plugin support:		${python_msg}
	Introspection python support:	${introspection_py_msg}

	GDA support:			${gda_msg}
	GNOME-DB support:		${gnomedb_msg}
	Psiconv support:        	${psiconv_msg}

	PDF documentation:		${pdf_msg}
"
#	Mono support:			${mono_msg}
#	Guile Support:			${guile_msg}
