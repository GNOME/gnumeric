WINEDIR=${HOME}/.wine/drive_c

setup :
	if [ ! -L Python25 ]; then		ln -s ${WINEDIR}/Python25 ; fi
	if [ ! -d bin ]; then			mkdir bin; fi
	if [ ! -d bin/libpython25.dll ]; then (	cd bin ; ln -s ${WINEDIR}/windows/system32/python25.dll libpython25.dll ) ; fi
	if [ ! -d lib ]; then			mkdir lib; fi
	if [ ! -d lib/libpython25.a ]; then (	cd lib ; ln -s ${WINEDIR}/Python25/libs/libpython25.a libpython25.a ) ; fi
