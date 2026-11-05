Name:           crowsnest
Version:        0.6.0
Release:        1%{?dist}
Summary:        Compact always-on-top HUD for focused-process and infra status

License:        MIT
Obsoletes:      syspeek < %{version}-%{release}
Conflicts:      syspeek
URL:            https://github.com/codeafridi/Crowsnest
# Full URL so `spectool -g` / COPR can fetch it. The GitHub archive
# extracts to Crowsnest-%{version}/ (capital S), hence %autosetup -n below.
Source0:        https://github.com/codeafridi/Crowsnest/archive/refs/tags/v%{version}.tar.gz
BuildRequires:  gcc-c++
BuildRequires:  cmake
BuildRequires:  systemd-rpm-macros
BuildRequires:  desktop-file-utils
%if 0%{?suse_version}
BuildRequires:  qt6-base-devel
Requires:       qt6-base
%else
BuildRequires:  qt6-qtbase-devel
Requires:       qt6-qtbase
%endif
Recommends:     xdotool
Recommends:     docker
Recommends:     kubectl
# _userunitdir is defined by systemd macros on Fedora/openSUSE; fall back
# to the canonical path on older toolchains.
%{!?_userunitdir: %global _userunitdir %{_prefix}/lib/systemd/user}

%description
Crowsnest shows the focused application's PID, CPU, and RSS (aggregated
across its process group) plus optional Docker and Kubernetes health in a small
always-on-top overlay.

%prep
%autosetup -n Crowsnest-%{version} -p1

%build
%cmake -S cpp -B cpp/build
%cmake_build

%install
install -Dm0755 cpp/build/crowsnest %{buildroot}%{_bindir}/crowsnest
sed 's|@BINARY@|%{_bindir}/crowsnest|g' \
  cpp/packaging/crowsnest.service.in \
  > crowsnest.service
install -Dm0644 crowsnest.service %{buildroot}%{_userunitdir}/crowsnest.service
desktop-file-install \
  --dir=%{buildroot}%{_datadir}/applications \
  --set-key=Exec --set-value=%{_bindir}/crowsnest \
  --set-icon=crowsnest \
  cpp/packaging/crowsnest.desktop.in
# crowsnest.desktop.in has no Icon key; add it (desktop-file-install --set-icon
# only rewrites an existing key on some versions).
sed -i 's|^Exec=.*|Exec=%{_bindir}/crowsnest\nIcon=crowsnest|' \
  %{buildroot}%{_datadir}/applications/crowsnest.desktop
desktop-file-validate %{buildroot}%{_datadir}/applications/crowsnest.desktop
install -Dm0644 cpp/packaging/icons/crowsnest.svg \
  %{buildroot}%{_datadir}/icons/hicolor/scalable/apps/crowsnest.svg

%check
QT_QPA_PLATFORM=offscreen ctest --test-dir cpp/build --output-on-failure

%files
%license LICENSE
%{_bindir}/crowsnest
%{_userunitdir}/crowsnest.service
%{_datadir}/applications/crowsnest.desktop
%{_datadir}/icons/hicolor/scalable/apps/crowsnest.svg

%changelog
* Fri Oct 17 2026 Crowsnest <noreply@example.com> - 0.6.0-1
- Watchdog anomalies/forecast/insights; Profiles; TLS certificates;
  runtime/SSI; Kubernetes nodes; incident management
* Thu Oct 16 2026 Crowsnest <noreply@example.com> - 0.5.0-1
- Disks, TCP health, Network Path, SBOM, Traces, DogStatsD, Security,
  Logs pipeline, SLO, Kubernetes cost/deprecations, PostgreSQL DBM
* Sun Oct 11 2026 Crowsnest <noreply@example.com> - 0.3.2-1
- AppImage packaging
* Thu Jan 01 2026 Crowsnest <noreply@example.com> - 0.1.0-1
- Initial package
