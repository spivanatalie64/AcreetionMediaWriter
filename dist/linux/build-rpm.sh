#!/bin/bash
# Build an RPM of AcreetionOS Media Writer.
#
# Run from the repository root after `cmake -B build -DCMAKE_INSTALL_PREFIX=/usr`
# (i.e. inside the release workflow's RPM job container).
#
# The resulting package is written to ./AcreetionOSMediaWriter-linux-x86_64-<tag>.rpm
set -euo pipefail

TAG="${TAG_NAME:?TAG_NAME is required (e.g. v5.4.1)}"
VERSION_STRIPPED="$(sed -e 's/^v//' -e 's/-.*//' <<< "$TAG")"
if [[ -z "$VERSION_STRIPPED" ]]; then
    VERSION_STRIPPED=5.3.50
fi

# Stage the install tree with DESTDIR (cmake installs to <prefix> inside it)
STAGING="$(pwd)/rpm-staging"
rm -rf "$STAGING"
DESTDIR="$STAGING" cmake --install build

mkdir -p ~/rpmbuild/{BUILD,RPMS,SOURCES,SPECS,SRPMS}

cat > ~/rpmbuild/SPECS/acreetionos-mediawriter.spec << RPMSEC
Name:           acreetionos-mediawriter
Version:        ${VERSION_STRIPPED}
Release:        1
Summary:        AcreetionOS Media Writer
License:        GPL-2.0+
URL:            https://github.com/spivanatalie64/AcreetionMediaWriter

%description
A tool to write images of AcreetionOS media to portable drives
like flash drives or memory cards.

%install
# rpmbuild changes cwd to the BUILD dir, so the staging dir must come
# from the environment, not \$(pwd)
cp -a \${RPM_STAGING}/* %{buildroot}/

%files
%{_bindir}/mediawriter
%{_libexecdir}/mediawriter/
%{_datadir}/applications/org.acreetionos.MediaWriter.desktop
%{_datadir}/metainfo/org.acreetionos.MediaWriter.metainfo.xml
%{_datadir}/icons/hicolor/*/apps/org.acreetionos.MediaWriter.png
%{_datadir}/polkit-1/actions/org.acreetionos.MediaWriter.policy
RPMSEC

RPM_STAGING="$STAGING" rpmbuild -bb ~/rpmbuild/SPECS/acreetionos-mediawriter.spec

cp ~/rpmbuild/RPMS/x86_64/*.rpm "AcreetionOSMediaWriter-linux-x86_64-${TAG}.rpm"
echo "Built AcreetionOSMediaWriter-linux-x86_64-${TAG}.rpm"
