EXTRA_DIST = gnumeric.nsi.in

sandbox		= ${HOME}/win32/debug

export WINEPREFIX	= ${sandbox}/wine
export WINEDIR		= ${WINEPREFIX}/drive_c
export py_pkg		= "http://www.python.org/ftp/python/2.6.3/python-2.6.3.msi"

setup :
	cd ${sandbox} \
	wine msiexec ~/Desktop/python-2.6.3.msi	\
	if [ ! -L Python26 ]; then		ln -s ${WINEDIR}/Python26 ; fi	\
	if [ ! -d bin ]; then			mkdir bin; fi	\
	if [ ! -d bin/libpython26.dll ]; then (	cd bin ; ln -s ${WINEDIR}/windows/system32/python26.dll libpython26.dll ) ; fi	\
	if [ ! -d lib ]; then			mkdir lib; fi	\
	if [ ! -d lib/libpython26.a ]; then (	cd lib ; ln -s ${WINEDIR}/Python26/libs/python26.lib libpython26.dll.a ) ; fi
