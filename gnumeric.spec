%define  ver     0.3
%define  rel     1
%define  prefix  /usr

Summary: the GNOME spreadsheet
Name: gnumeric
Version: %ver
Release: %rel
Copyright: GPL
Group: Applications/Spreadsheets
Source: ftp://ftp.gnome.org/pub/GNOME/sources/gnumeric-%{ver}.tar.gz
Url:http://www.gnome.org/gnumeric
BuildRoot:/var/tmp/gnumeric-root
Docdir: %{prefix}/doc

Requires: gnome-libs >= 0.30

%description
GNOME based spreadsheet

%prep
%setup

%build
./configure --prefix=%prefix

if [ "$SMP" != "" ]; then
  (make "MAKE=make -k -j $SMP"; exit 0)
  make
else
  make
fi

%install
#rm -rf $RPM_BUILD_ROOT

make prefix=$RPM_BUILD_ROOT%{prefix} install

%clean
rm -rf $RPM_BUILD_ROOT



%changelog

* Thu Sep 24 1998 Michael Fulbright <msf@redhat.com>
- Version 0.2

%files
%defattr(-, root, root)

%doc AUTHORS ChangeLog NEWS README COPYING TODO

%{prefix}/bin/*
%{prefix}/lib/gnumeric/*
%{prefix}/share/*

