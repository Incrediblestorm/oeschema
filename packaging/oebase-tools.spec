Name:           oebase-tools
Version:        %{_version}
Release:        %{_release}%{?dist}
Summary:        OpenEdge database schema management tools
License:        MIT
BuildArch:      x86_64
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  glibc-static

%description
Command-line tools for managing OpenEdge database schemas.
Provides schema dumping, comparison, delta generation, and
end-to-end schema synchronization for OpenEdge 12.2.13+.

%prep
%setup -q

%build
gcc -O2 -static -o schema_dump      schema_dump.c
gcc -O2 -static -o compare_schemas  compare_schemas.c
gcc -O2 -static -o apply_schema     apply_schema.c
gcc -O2 -static -o schema_sync      schema_sync_final.c

%install
install -d %{buildroot}%{_bindir}
install -m 0755 schema_dump     %{buildroot}%{_bindir}/schema_dump
install -m 0755 compare_schemas %{buildroot}%{_bindir}/compare_schemas
install -m 0755 apply_schema    %{buildroot}%{_bindir}/apply_schema
install -m 0755 schema_sync     %{buildroot}%{_bindir}/schema_sync

%files
%{_bindir}/schema_dump
%{_bindir}/compare_schemas
%{_bindir}/apply_schema
%{_bindir}/schema_sync

%changelog
* Mon Jun 02 2026 Incrediblestorm <noreply@github.com> - %{_version}-%{_release}
- Initial package release
