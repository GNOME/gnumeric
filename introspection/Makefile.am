EXTRA_DIST = README 			\
	gi/overrides/Gnm.py		\
	gi/overrides/__init__.py

if HAVE_INTROSPECTION
if HAVE_PYTHON_GI
install-data-local:
	install -D -m 0444 -t @GIOVERRIDESDIR@ gi/overrides/Gnm.py

uninstall-local:
	rm -f @GIOVERRIDESDIR@/Gnm.py
endif
endif
