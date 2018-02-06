Name:		globus-gss-assist
%global soname 3
%if %{?suse_version}%{!?suse_version:0} >= 1315
%global apache_license Apache-2.0
%else
%global apache_license ASL 2.0
%endif
%global _name %(tr - _ <<< %{name})
Version:	11.1
Release:	1%{?dist}
Vendor:	Globus Support
Summary:	Globus Toolkit - GSSAPI Assist library

Group:		System Environment/Libraries
License:	%{apache_license}
URL:		http://toolkit.globus.org/
Source:	http://toolkit.globus.org/ftppub/gt6/packages/%{_name}-%{version}.tar.gz
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:	globus-gsi-cert-utils-devel >= 8
BuildRequires:	globus-gsi-sysconfig-devel >= 7
BuildRequires:	globus-common-devel >= 14
BuildRequires:	globus-callout-devel >= 2
BuildRequires:	globus-gssapi-gsi-devel >= 13
BuildRequires:	globus-gsi-credential-devel >= 6
BuildRequires:	globus-common-progs >= 14
BuildRequires:	doxygen
BuildRequires:	graphviz
%if "%{?rhel}" == "5"
BuildRequires:	graphviz-gd
%endif
%if %{?fedora}%{!?fedora:0} >= 19 || %{?rhel}%{!?rhel:0} >= 7 || %{?suse_version}%{!?suse_version:0} >= 1315
BuildRequires:  automake >= 1.11
BuildRequires:  autoconf >= 2.60
BuildRequires:  libtool >= 2.2
%endif
BuildRequires:  pkgconfig
%if %{?fedora}%{!?fedora:0} >= 18 || %{?rhel}%{!?rhel:0} >= 6
BuildRequires:  perl-Test-Simple
%endif

%if %{?rhel}%{!?rhel:0} == 5
BuildRequires:  openssl101e
%else
BuildRequires:  openssl
%endif

%if %{?suse_version}%{!?suse_version:0} >= 1315
%global mainpkg lib%{_name}%{soname}
%global nmainpkg -n %{mainpkg}
%else
%global mainpkg %{name}
%endif

%if %{?nmainpkg:1}%{!?nmainpkg:0} != 0
%package %{?nmainpkg}
Summary:	Globus Toolkit - GSSAPI Assist library Programs
Group:		System Environment/Libraries
%endif

%package progs
Summary:	Globus Toolkit - GSSAPI Assist library Programs
Group:		Applications/Internet
Requires:	%{mainpkg}%{?_isa} = %{version}-%{release}
Requires:	globus-common-progs >= 14

%package devel
Summary:	Globus Toolkit - GSSAPI Assist library Development Files
Group:		Development/Libraries
Requires:	%{mainpkg}%{?_isa} = %{version}-%{release}
Requires:	globus-gsi-cert-utils-devel%{?_isa} >= 8
Requires:	globus-gsi-credential-devel%{?_isa} >= 6
Requires:	globus-gsi-sysconfig-devel%{?_isa} >= 7
Requires:	globus-common-devel%{?_isa} >= 14
Requires:	globus-callout-devel%{?_isa} >= 2
Requires:	globus-gssapi-gsi-devel%{?_isa} >= 13

%package doc
Summary:	Globus Toolkit - GSSAPI Assist library Documentation Files
Group:		Documentation
%if %{?fedora}%{!?fedora:0} >= 10 || %{?rhel}%{!?rhel:0} >= 6
BuildArch:	noarch
%endif
Requires:	%{mainpkg} = %{version}-%{release}

%if %{?suse_version}%{!?suse_version:0} >= 1315
%description %{?nmainpkg}
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{mainpkg} package contains:
GSSAPI Assist library
%endif

%description
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{name} package contains:
GSSAPI Assist library

%description progs
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{name}-progs package contains:
GSSAPI Assist library Programs

%description devel
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{name}-devel package contains:
GSSAPI Assist library Development Files

%description doc
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{name}-doc package contains:
GSSAPI Assist library Documentation Files

%prep
%setup -q -n %{_name}-%{version}

%build
%if %{?fedora}%{!?fedora:0} >= 19 || %{?rhel}%{!?rhel:0} >= 7 || %{?suse_version}%{!?suse_version:0} >= 1315
# Remove files that should be replaced during bootstrap
rm -rf autom4te.cache

autoreconf -if
%endif

%if %{?rhel}%{!?rhel:0} == 5
export OPENSSL="$(which openssl101e)"
%endif

%configure \
           --disable-static \
           --docdir=%{_docdir}/%{name}-%{version} \
           --includedir=%{_includedir}/globus \
           --libexecdir=%{_datadir}/globus

make %{?_smp_mflags}

%install
rm -rf $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT

# Remove libtool archives (.la files)
find $RPM_BUILD_ROOT%{_libdir} -name 'lib*.la' -exec rm -v '{}' \;

%check
make %{_smp_mflags} check

%clean
rm -rf $RPM_BUILD_ROOT

%post %{?nmainpkg} -p /sbin/ldconfig

%postun %{?nmainpkg} -p /sbin/ldconfig

%files %{?nmainpkg}
%defattr(-,root,root,-)
%dir %{_docdir}/%{name}-%{version}
%doc %{_docdir}/%{name}-%{version}/GLOBUS_LICENSE
%{_libdir}/libglobus*.so.*

%files progs
%defattr(-,root,root,-)
%{_sbindir}/*
%{_mandir}/man8/*

%files devel
%defattr(-,root,root,-)
%{_includedir}/globus/*
%{_libdir}/libglobus*.so
%{_libdir}/pkgconfig/*.pc

%files doc
%defattr(-,root,root,-)
%dir %{_docdir}/%{name}-%{version}/html
%{_docdir}/%{name}-%{version}/html/*
%{_mandir}/man3/*

%changelog
* Tue Sep 12 2017 Globus Toolkit <support@globus.org> - 11.1-1
- race condition and dependency packaging fixes

* Tue Sep 05 2017 Globus Toolkit <support@globus.org> - 11.0-1
- Add new function gss_assist_read_vhost_cred_dir() for SNI server

* Tue Jan 10 2017 Globus Toolkit <support@globus.org> - 10.21-1
- Slow grid-mapfile-delete-entry (issue #84)

* Thu Sep 08 2016 Globus Toolkit <support@globus.org> - 10.20-1
- Update for el.5 openssl101e, replace docbook with asciidoc

* Fri Sep 02 2016 Globus Toolkit <support@globus.org> - 10.19-1
- Fix broken makefile

* Fri Sep 02 2016 Globus Toolkit <support@globus.org> - 10.18-1
- Fix grid-mapfile-add-entry is slow (issue #69)

* Thu Aug 25 2016 Globus Toolkit <support@globus.org> - 10.17-3
- Updates for SLES 12

* Thu Aug 18 2016 Globus Toolkit <support@globus.org> - 10.17-1
- Makefile fix

* Tue Aug 16 2016 Globus Toolkit <support@globus.org> - 10.16-1
- Updates for OpenSSL 1.1.0

* Thu Aug 06 2015 Globus Toolkit <support@globus.org> - 10.15-2
- Add vendor

* Wed Jul 15 2015 Globus Toolkit <support@globus.org> - 10.15-1
- Fix gridmap parsing error

* Wed Jul 01 2015 Globus Toolkit <support@globus.org> - 10.14-1
- fix uninitialized variable

* Mon Nov 03 2014 Globus Toolkit <support@globus.org> - 10.13-1
- doxygen fixes

* Thu Sep 25 2014 Globus Toolkit <support@globus.org> - 10.12-1
- Include more manpages for API
- Doxygen markup fixes
- Fix typos and clarify some documentation
- Quiet some autoconf/automake warnings
- GT-210: grid-mapfile-check-consistency doesn't work well

* Fri Aug 22 2014 Globus Toolkit <support@globus.org> - 10.11-1
- Merge fixes from ellert-globus_6_branch

* Wed Aug 20 2014 Globus Toolkit <support@globus.org> - 10.10-2
- Fix Source path

* Mon Jun 09 2014 Globus Toolkit <support@globus.org> - 10.10-1
- Merge changes from Mattias Ellert

* Fri May 23 2014 Globus Toolkit <support@globus.org> - 10.9-1
- Use globus_libc_[un]setenv

* Fri Apr 18 2014 Globus Toolkit <support@globus.org> - 10.8-1
- Version bump for consistency

* Tue Feb 25 2014 Globus Toolkit <support@globus.org> - 10.7-1
- Packaging fixes

* Tue Feb 25 2014 Globus Toolkit <support@globus.org> - 10.6-1
- Test fixes

* Thu Feb 20 2014 Globus Toolkit <support@globus.org> - 10.5-1
- Eliminate script initializer use

* Tue Feb 11 2014 Globus Toolkit <support@globus.org> - 10.4-1
- Test fixes

* Tue Feb 11 2014 Globus Toolkit <support@globus.org> - 10.3-1
- Test fixes

* Tue Feb 11 2014 Globus Toolkit <support@globus.org> - 10.2-1
- Test fixes

* Wed Jan 29 2014 Globus Toolkit <support@globus.org> - 10.1-1
- Fix test credential file permssions

* Tue Jan 21 2014 Globus Toolkit <support@globus.org> - 10.0-1
- Repackage for GT6 without GPT

* Mon Oct 28 2013 Globus Toolkit <support@globus.org> - 9.0-1
- Update Major version for globus_gss_assist_map_and_authorize_sharing

* Wed Jun 26 2013 Globus Toolkit <support@globus.org> - 8.9-2
- GT-424: New Fedora Packaging Guideline - no %%_isa in BuildRequires

* Tue Mar 19 2013 Globus Toolkit <support@globus.org> - 8.9-1
- Update sharing to support a full cert chain at logon

* Tue Mar 05 2013 Globus Toolkit <support@globus.org> - 8.8-1
- GT-365: Switch sharing user identification from DN to CERT

* Wed Feb 20 2013 Globus Toolkit <support@globus.org> - 8.7-2
- Workaround missing F18 doxygen/latex dependency

* Tue Feb 05 2013 Globus Toolkit <support@globus.org> - 8.7-1
- GT-302: Add initial sharing support to the GridFTP server
- GT-356: Add configuration and a command to make the sharing authorization file easier to manage

* Mon Nov 26 2012 Globus Toolkit <support@globus.org> - 8.6-2
- 5.2.3

* Tue Jul 17 2012 Joseph Bester <bester@mcs.anl.gov> - 8.6-1
- GT-255: gridmapdir support doesn't compile on non-POSIX systems

* Mon Jul 16 2012 Joseph Bester <bester@mcs.anl.gov> - 8.5-5
- GT 5.2.2 final

* Fri Jun 29 2012 Joseph Bester <bester@mcs.anl.gov> - 8.5-4
- GT 5.2.2 Release

* Wed May 09 2012 Joseph Bester <bester@mcs.anl.gov> - 8.5-3
- RHEL 4 patches

* Fri May 04 2012 Joseph Bester <bester@mcs.anl.gov> - 8.5-2
- SLES 11 patches

* Mon Apr 02 2012 Joseph Bester <bester@mcs.anl.gov> - 8.5-1
- RIC-239: GSSAPI Token inspection fails when using TLS 1.2

* Tue Feb 14 2012 Joseph Bester <bester@mcs.anl.gov> - 8.3-1
- RIC-224: Eliminate some doxygen warnings
- RIC-226: Some dependencies are missing in GPT metadata
- RIC-227: Potentially unsafe format strings in GSI

* Thu Dec 22 2011 Joseph Bester <bester@mcs.anl.gov> - 8.2-1
- Doxygen markup errors (bugzilla #7185)

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 8.1-4
- Update for 5.2.0 release

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 8.1-3
- Last sync prior to 5.2.0

* Tue Oct 11 2011 Joseph Bester <bester@mcs.anl.gov> - 8.1-2
- Add explicit dependencies on >= 5.2 libraries

* Thu Oct 06 2011 Joseph Bester <bester@mcs.anl.gov> - 8.1-1
- Add backward-compatibility aging

* Thu Sep 01 2011 Joseph Bester <bester@mcs.anl.gov> - 8.0-2
- Update for 5.1.2 release

* Sun Jul 18 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 5.9-2
- Move client man pages to progs package

* Sat Jul 17 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 5.9-1
- Update to Globus Toolkit 5.0.2

* Wed Apr 14 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 5.8-1
- Update to Globus Toolkit 5.0.1

* Fri Jan 22 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 5.5-1
- Update to Globus Toolkit 5.0.0

* Thu Jul 23 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 4.0-3
- Add instruction set architecture (isa) tags
- Make doc subpackage noarch

* Wed Jun 03 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 4.0-2
- Update to official Fedora Globus packaging guidelines

* Thu Apr 16 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 4.0-1
- Make comment about source retrieval more explicit
- Change defines to globals
- Remove explicit requires on library packages
- Put GLOBUS_LICENSE file in extracted source tarball

* Sun Mar 15 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 4.0-0.5
- Adapting to updated globus-core package

* Thu Feb 26 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 4.0-0.4
- Add s390x to the list of 64 bit platforms

* Thu Jan 01 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 4.0-0.3
- Adapt to updated GPT package

* Wed Oct 15 2008 Mattias Ellert <mattias.ellert@fysast.uu.se> - 4.0-0.2
- Update to Globus Toolkit 4.2.1

* Mon Jul 14 2008 Mattias Ellert <mattias.ellert@fysast.uu.se> - 3.25-0.1
- Autogenerated
