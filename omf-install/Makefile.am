omf_dest_dir=$(datadir)/omf/gnumeric
scrollkeeper_localstate_dir = $(localstatedir)/scrollkeeper

install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(omf_dest_dir)
	-for file in $(srcdir)/*.omf; do 			\
		$(INSTALL_DATA) $$file $(DESTDIR)$(omf_dest_dir); \
	done
	-if [ -z "$(DESTDIR)" -a -z "$(RPM_BUILD_ROOT)" ]; then		\
		scrollkeeper-update -p $(scrollkeeper_localstate_dir);	\
	fi

uninstall-local:
	-for file in $(srcdir)/*.omf; do 			\
		basefile=`basename $$file`; 			\
		rm -f $(omf_dest_dir)/$$basefile; 		\
        done
	-rmdir $(omf_dest_dir)
	-scrollkeeper-update -p $(scrollkeeper_localstate_dir) 
