%{!?perl_vendorlib: %global perl_vendorlib %(eval "`perl -V:installvendorlib`"; echo $installvendorlib)}
Name:		globus-gram-job-manager-scripts
%global soname 0
%if %{?suse_version}%{!?suse_version:0} >= 1315
%global apache_license Apache-2.0
%else
%global apache_license ASL 2.0
%endif
%global _name %(tr - _ <<< %{name})
Version:	6.10
Release:	1%{?dist}
Vendor:	Globus Support
Summary:	Globus Toolkit - GRAM Job ManagerScripts

Group:		Applications/Internet
BuildArch:	noarch
License:	${apache_license}
URL:		http://toolkit.globus.org/
Source:	http://toolkit.globus.org/ftppub/gt6/packages/%{_name}-%{version}.tar.gz
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

Requires:	globus-common-progs >= 14
%if 0%{?suse_version} > 0
    %if %{suse_version} < 1140
Requires:     perl = %{perl_version}
    %else
%{perl_requires}
    %endif
%else
Requires:	perl(:MODULE_COMPAT_%(eval "`perl -V:version`"; echo $version))
%endif
%if %{?fedora}%{!?fedora:0} >= 19 || %{?rhel}%{!?rhel:0} >= 7 || %{?suse_version}%{!?suse_version:0} >= 1315
BuildRequires:  automake >= 1.11
BuildRequires:  autoconf >= 2.60
BuildRequires:  libtool >= 2.2
%endif

%package doc
Summary:	Globus Toolkit - GRAM Job ManagerScripts Documentation Files
Group:		Documentation
Requires:	%{name} = %{version}-%{release}

%description
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{name} package contains:
GRAM Job ManagerScripts

%description doc
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{name}-doc package contains:
GRAM Job ManagerScripts Documentation Files

%prep
%setup -q -n %{_name}-%{version}

%build
%if %{?fedora}%{!?fedora:0} >= 19 || %{?rhel}%{!?rhel:0} >= 7 || %{?suse_version}%{!?suse_version:0} >= 1315
# Remove files that should be replaced during bootstrap
rm -rf autom4te.cache

autoreconf -if
%endif


%configure \
           --disable-static \
           --docdir=%{_docdir}/%{name}-%{version} \
           --includedir=%{_includedir}/globus \
           --libexecdir=%{_datadir}/globus \
           --with-perlmoduledir=%{perl_vendorlib}

make %{?_smp_mflags}

%install
rm -rf $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root,-)
%dir %{_docdir}/%{name}-%{version}
%{_docdir}/%{name}-%{version}/GLOBUS_LICENSE
%{_datadir}/globus
%{_datadir}/globus/globus-job-manager-script.pl
%dir %{perl_vendorlib}/Globus
%dir %{perl_vendorlib}/Globus/GRAM
%{perl_vendorlib}/Globus/GRAM/*.pm
%dir %{_docdir}/%{name}-%{version}
%{_mandir}/man8/*
%{_sbindir}/globus-gatekeeper-admin

%files doc
%defattr(-,root,root,-)
%dir %{_docdir}/%{name}-%{version}/perl
%dir %{_docdir}/%{name}-%{version}/perl/Globus
%dir %{_docdir}/%{name}-%{version}/perl/Globus/GRAM
%{_docdir}/%{name}-%{version}/perl/Globus/GRAM/*.html


%changelog
* Thu Sep 28 2017 Globus Toolkit <support@globus.org> - 6.10-1
- Merge #110 from ellert: Fix regex for perl 5.26 compatibility

* Thu Sep 08 2016 Globus Toolkit <support@globus.org> - 6.9-1
- Update for el.5 openssl101e, replace docbook with asciidoc

* Mon Aug 29 2016 Globus Toolkit <support@globus.org> - 6.8-3
- Updates for SLES 12

* Sat Aug 20 2016 Globus Toolkit <support@globus.org> - 6.8-1
- Update bug report URL

* Thu Aug 06 2015 Globus Toolkit <support@globus.org> - 6.7-2
- Add vendor

* Thu Sep 18 2014 Globus Toolkit <support@globus.org> - 6.7-1
- GT-455: Incorporate OSG patches
- GT-463: OSG patch "osg-path.patch" for globus-gram-job-manager-scripts
- GT-467: OSG patch "gratia.patch" for globus-gram-job-manager-scripts
- GT-468: OSG patch "osg-environment.patch" for globus-gram-job-manager-scripts
- Don't modify environment values that are already set when merging OSG-specific values

* Mon Aug 25 2014 Globus Toolkit <support@globus.org> - 6.6-1
- Fix install rule when building from shadow dir

* Fri Aug 22 2014 Globus Toolkit <support@globus.org> - 6.5-1
- Merge fixes from ellert-globus_6_branch

* Wed Aug 20 2014 Globus Toolkit <support@globus.org> - 6.4-2
- Fix Source path

* Mon Jun 09 2014 Globus Toolkit <support@globus.org> - 6.4-1
- Merge changes from Mattias Ellert

* Fri Apr 25 2014 Globus Toolkit <support@globus.org> - 6.3-1
- Packaging fixes

* Fri Apr 18 2014 Globus Toolkit <support@globus.org> - 6.2-1
- Version bump for consistency

* Sat Feb 15 2014 Globus Toolkit <support@globus.org> - 6.1-1
- Packaging fixes

* Wed Jan 22 2014 Globus Toolkit <support@globus.org> - 6.0-1
- Repackage for GT6 without GPT

* Fri Sep 06 2013 Globus Toolkit <support@globus.org> - 5.0-1
- Add new features for slurm lrm implementation

* Wed Jun 26 2013 Globus Toolkit <support@globus.org> - 4.2-8
- GT-424: New Fedora Packaging Guideline - no %%_isa in BuildRequires

* Mon Nov 26 2012 Globus Toolkit <support@globus.org> - 4.2-7
- 5.2.3

* Mon Jul 16 2012 Joseph Bester <bester@mcs.anl.gov> - 4.2-6
- GT 5.2.2 final

* Fri Jun 29 2012 Joseph Bester <bester@mcs.anl.gov> - 4.2-5
- GT 5.2.2 Release

* Wed May 09 2012 Joseph Bester <bester@mcs.anl.gov> - 4.2-4
- RHEL 4 patches

* Fri May 04 2012 Joseph Bester <bester@mcs.anl.gov> - 4.2-3
- SLES 11 patches

* Tue Feb 14 2012 Joseph Bester <bester@mcs.anl.gov> - 4.2-2
- Updated version numbers

* Tue Dec 13 2011 Joseph Bester <bester@mcs.anl.gov> - 4.2-1
- Add manpage for globus-gatekeeper-admin.8

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 4.1-2
- Last sync prior to 5.2.0

* Mon Nov 28 2011 Joseph Bester <bester@mcs.anl.gov> - 4.1-1
- GRAM-278: GASS cache location not set in the perl environment

* Tue Oct 11 2011 Joseph Bester <bester@mcs.anl.gov> - 4.0-3
- Add explicit dependencies on >= 5.2 libraries

* Thu Sep 01 2011 Joseph Bester <bester@mcs.anl.gov> - 4.0-2
- Update for 5.1.2 release

* Sun Jun 05 2011 Mattias Ellert <mattias.ellert@fysast.uu.se> - 2.12-1
- Update to Globus Toolkit 5.0.4

* Mon Apr 25 2011 Mattias Ellert <mattias.ellert@fysast.uu.se> - 2.11-3
- Add README file

* Tue Feb 08 2011 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 2.11-2
- Rebuilt for https://fedoraproject.org/wiki/Fedora_15_Mass_Rebuild

* Sat Jul 17 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 2.11-1
- Update to Globus Toolkit 5.0.2

* Tue Jun 01 2010 Marcela Maslanova <mmaslano@redhat.com> - 2.5-2
- Mass rebuild with perl-5.12.0

* Wed Apr 14 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 2.5-1
- Update to Globus Toolkit 5.0.1

* Sat Jan 23 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 2.4-1
- Update to Globus Toolkit 5.0.0

* Thu Jul 30 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 0.7-1
- Autogenerated
