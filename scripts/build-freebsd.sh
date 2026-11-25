#!/bin/sh
#
# Build and install Mulberry on FreeBSD (amd64) with GCC from packages.
#
# Usage: scripts/build-freebsd.sh [options] [build|install|uninstall|clean]
#
#   build      (default) configure and build everything
#   install    install a previous build in the layout chosen at build
#              time: run as root for the system layout, as the user for
#              a per-user one.  The list of installed files is kept in
#              <mulberry dir>/.install-manifest
#   uninstall  remove an installation, using its install manifest: only
#              the installed files are removed (user data in ~/.mulberry is
#              kept); the build tree is not needed.  The installation is
#              found automatically; use -s or -u if there are both a system
#              and a per-user one, -d for one in a staging directory
#   clean      remove all build products
#
# Options (before the action):
#   -g VERSION  GCC version from packages (default: 14, i.e. gcc14/g++14)
#   -j JOBS     parallel make jobs (default: number of CPUs)
#   -s          system layout: /usr/local/bin/mulberry and
#               /usr/local/lib/mulberry (default for "build")
#   -u          per-user layout: ~/bin/mulberry and ~/.mulberry
#   -d DESTDIR  staging directory for "install" and "uninstall"
#               (e.g. for packaging)
#   -c DIR      Unicode CLDR directory (the one containing
#               common/annotations/en.xml) used to generate the emoji text
#               table; without it the table is empty
#   -n          build without the HTML message viewer (litehtml, Cairo,
#               HarfBuzz); meson, ninja and cmake are then not needed
#   -h          show this help
#
# The build needs network access: timezone data is downloaded from IANA.

set -eu

die() {
	printf 'build-freebsd: ERROR: %s\n' "$*" >&2
	exit 1
}

note() {
	printf 'build-freebsd: %s\n' "$*"
}

# Print the header comment (from line 3 up to the first non-comment line)
usage() {
	awk 'NR < 3 { next } /^#/ { sub(/^# ?/, ""); print; next } { exit }' "$0"
}

GCC_VERSION=14
JOBS=$(sysctl -n hw.ncpu 2>/dev/null || echo 1)
LAYOUT=		# system, user, or empty: not given
STAGE_DIR=
CLDR=
HTML_VIEWER=yes

set_layout() {
	[ -z "$LAYOUT" ] || [ "$LAYOUT" = "$1" ] ||
		die "options -s and -u are mutually exclusive"
	LAYOUT=$1
}

while getopts g:j:sud:c:nh opt; do
	case $opt in
	g) GCC_VERSION=$OPTARG ;;
	j) JOBS=$OPTARG ;;
	s) set_layout system ;;
	u) set_layout user ;;
	d) STAGE_DIR=$OPTARG ;;
	c) CLDR=$OPTARG ;;
	n) HTML_VIEWER= ;;
	h) usage; exit 0 ;;
	*) usage >&2; exit 2 ;;
	esac
done
shift $((OPTIND - 1))
ACTION=${1:-build}

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

[ "$(uname -s)" = FreeBSD ] || die "this script is for FreeBSD"
[ "$(uname -m)" = amd64 ] || die "only amd64 is supported for now"

# ---------------------------------------------------------------------------
# Environment
# ---------------------------------------------------------------------------

# Start from a reproducible environment: these variables inject include or
# library paths and flags behind configure's back (e.g. making headers of
# ports libraries shadow the base system ones).
unset CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH LIBRARY_PATH \
	CFLAGS CXXFLAGS CPPFLAGS LDFLAGS 2>/dev/null || true

# One compiler, hence one C++ runtime, for JX/ACE and Mulberry
CC=gcc$GCC_VERSION
CXX=g++$GCC_VERSION
JX_CC=$CC
JX_CXX=$CXX
export CC CXX JX_CC JX_CXX

# JX build tools (makemake, jxlayout, ...) go inside the source tree
# instead of ~/bin (or /usr/bin when building as root)
JXDIST=$ROOT/Libraries/JX/dist
JX_INSTALL_ROOT=$JXDIST/bin
JX_LIB_ROOT=$JXDIST/lib
ROOT_DATA_FILE_DIR=$JXDIST/data
export JX_INSTALL_ROOT JX_LIB_ROOT ROOT_DATA_FILE_DIR
PATH=$JX_INSTALL_ROOT:$PATH
export PATH

MAKE_CMD=gmake

# ---------------------------------------------------------------------------
# Checks
# ---------------------------------------------------------------------------

check_requirements() {
	missing=
	need() {
		[ -e "$1" ] || missing="$missing $2"
	}
	need /usr/local/bin/gmake gmake
	need /usr/local/bin/autoconf autoconf
	need /usr/local/bin/pkg-config pkgconf
	need /usr/local/bin/$CXX gcc$GCC_VERSION
	need /usr/local/bin/curl curl
	# libcurl: OAuth2 (Libraries/OAuth2) and the HTML viewer's remote
	# images; the curl package provides both the tool and the library
	need /usr/local/include/curl/curl.h curl
	need /usr/local/include/ldap.h openldap26-client
	need /usr/local/include/sodium.h libsodium
	need /usr/local/include/uninorm.h libunistring
	need /usr/local/include/pcre2.h pcre2
	need /usr/local/include/X11/Xlib.h libX11
	need /usr/local/include/X11/extensions/shape.h libXext
	need /usr/local/include/X11/xpm.h libXpm
	need /usr/local/include/X11/Xft/Xft.h libXft
	need /usr/local/include/fontconfig/fontconfig.h fontconfig
	need /usr/local/include/freetype2/ft2build.h freetype2
	need /usr/local/bin/xml2-config libxml2
	need /usr/local/libdata/pkgconfig/glib-2.0.pc glib
	if [ -n "$CLDR" ]; then
		need /usr/local/bin/python3 python3
	fi
	if [ -n "$HTML_VIEWER" ]; then
		# HTML viewer: pixman, Cairo and HarfBuzz are built with Meson,
		# litehtml with CMake (static, inside the source tree)
		need /usr/local/bin/meson meson
		need /usr/local/bin/ninja ninja
		need /usr/local/bin/cmake cmake-core
		need /usr/local/include/png.h png
		need /usr/local/include/X11/extensions/Xrender.h libXrender
		# Image decoders (PNG through Cairo)
		need /usr/local/include/jpeglib.h jpeg-turbo
		need /usr/local/include/gif_lib.h giflib
	fi
	if [ -n "$missing" ]; then
		die "missing packages, install them with:
    pkg install$missing"
	fi

	# Optional features
	[ -e /usr/local/include/aspell.h ] ||
		note "aspell not installed: no ASpell spell checker plugin (pkg install aspell)"
	[ -e /usr/local/include/brotli/decode.h ] ||
		note "brotli not installed: no HTTP br encoding (pkg install brotli)"
	[ -e /usr/local/include/zstd.h ] ||
		note "zstd not installed: no HTTP zstd encoding (pkg install zstd)"
	if [ -n "$HTML_VIEWER" ] && [ ! -e /usr/local/include/webp/decode.h ]; then
		note "libwebp not installed: no WebP images in the HTML viewer (pkg install webp)"
	fi

	# OpenSSL from ports/packages installs headers into /usr/local/include,
	# where they shadow the base system OpenSSL.  Packaged libraries (e.g.
	# OpenLDAP) use the base OpenSSL, so Mulberry would end up with two
	# different OpenSSL versions in one process.
	if [ -e /usr/local/include/openssl/ssl.h ]; then
		die "OpenSSL headers found in /usr/local/include ($(pkg which -q /usr/local/include/openssl/ssl.h 2>/dev/null || echo 'unknown package')).
They shadow the base system OpenSSL used by the packaged libraries.
Remove that package and retry."
	fi

	if [ -n "$CLDR" ] && [ ! -f "$CLDR/common/annotations/en.xml" ]; then
		die "$CLDR/common/annotations/en.xml not found (option -c)"
	fi
}

check_submodules() {
	missing=
	set -- JX CICalendar vCard XMLLib vzic
	[ -z "$HTML_VIEWER" ] || set -- "$@" litehtml pixman cairo harfbuzz
	for d in "$@"; do
		[ -e "Libraries/$d/.git" ] || missing="$missing Libraries/$d"
	done
	[ -z "$missing" ] && return 0
	[ -e .git ] || die "submodules missing:$missing (not a git checkout)"
	note "initializing submodules:$missing"
	git submodule update --init $missing
}

# ---------------------------------------------------------------------------
# Actions
# ---------------------------------------------------------------------------

do_build() {
	check_requirements
	check_submodules

	if [ "$LAYOUT" = user ]; then
		set -- --prefix="$HOME" --with-program-dir=bin \
			--with-mulberry-dir=.mulberry --with-help-dir=.mulberry
	else
		set -- --prefix=/usr/local --with-program-dir=bin \
			--with-mulberry-dir=lib/mulberry \
			--with-help-dir=share/doc/mulberry
	fi
	[ -n "$HTML_VIEWER" ] || set -- "$@" --disable-html-viewer

	if [ ! -x configure ] || [ configure.in -nt configure ]; then
		note "running autoconf"
		autoconf
	fi

	note "configuring with $CC/$CXX"
	./configure "$@" CC="$CC" CXX="$CXX"

	mkdir -p "$JX_INSTALL_ROOT" "$JX_LIB_ROOT" "$ROOT_DATA_FILE_DIR"

	note "building with $JOBS jobs"
	if [ -n "$CLDR" ]; then
		$MAKE_CMD -j"$JOBS" CLDR_DIR="$CLDR" all
	else
		$MAKE_CMD -j"$JOBS" all
	fi

	if ! ls Sources_Common/Timezones/zoneinfo/*.ics >/dev/null 2>&1; then
		die "build finished but no timezone data was generated (network access needed); run scripts/update-timezones.sh and then $0 install"
	fi

	note "build complete: Linux/mulberry"
	if [ "$LAYOUT" = user ]; then
		note "next (as $(id -un), not root): $0 install"
	else
		note "next (as root): $0 install"
	fi
}

do_install() {
	[ -f Linux/Makefile ] && [ -x Linux/mulberry ] ||
		die "nothing built yet, run: $0 build"
	ls Sources_Common/Timezones/zoneinfo/*.ics >/dev/null 2>&1 ||
		die "no timezone data, run: scripts/update-timezones.sh"

	# Installation directories, as configured at build time
	prefix=$(sed -n 's/^prefix = //p' Build/Makefile)
	bindir=$prefix/$(sed -n 's/^BINDIR = \$(DESTDIR)\$(prefix)\///p' Linux/Makefile)
	libdir=$prefix/$(sed -n 's/^LIBDIR = \$(DESTDIR)\$(prefix)\///p' Build/Makefile)
	helpdir=$prefix/$(sed -n 's/^HELPDIR = \$(DESTDIR)\$(prefix)\///p' Build/Makefile)
	bindir=${bindir%/}
	libdir=${libdir%/}
	helpdir=${helpdir%/}

	# The layout is the one configured by "build"; -s/-u only double-check
	if [ "$prefix" = /usr/local ]; then
		built_layout=system
		layout_opt=
	else
		built_layout=user
		layout_opt="-u "
	fi
	[ -z "$LAYOUT" ] || [ "$LAYOUT" = "$built_layout" ] ||
		die "this build is configured for the $built_layout layout ($prefix); rebuild with: $0 ${layout_opt}build"

	if [ -z "$STAGE_DIR" ]; then
		# Root must not install into a per-user location: the files would
		# belong to root inside the user's home directory.
		owner=$(stat -f %Su "$(existing_dir "$libdir")")
		if [ "$(id -u)" -eq 0 ] && [ "$owner" != root ]; then
			die "this build is configured for a per-user installation in $prefix (owned by $owner): run '$0 install' as $owner, without doas/sudo"
		fi
		check_writable install "$bindir" "$libdir" "$helpdir"
	fi

	# List the installed files by installing into a temporary directory
	# first; the list becomes the manifest used by "uninstall".
	tmp=$(mktemp -d -t mulberry-install)
	trap 'rm -rf "$tmp"' EXIT
	note "collecting the list of installed files"
	run_install "$tmp" >/dev/null

	run_install "$STAGE_DIR"

	manifest=${STAGE_DIR}$libdir/.install-manifest
	{
		echo "# Mulberry install manifest, written by scripts/build-freebsd.sh"
		echo "# and used by its \"uninstall\" action"
		echo "bindir $bindir"
		echo "libdir $libdir"
		echo "helpdir $helpdir"
		(cd "$tmp" && find . ! -type d) | sed 's|^\.||; s|^|file |' | sort
	} > "$manifest"

	note "installed into ${STAGE_DIR}$prefix ($(grep -c '^file ' "$manifest") files, manifest: $manifest)"
}

# Print the nearest existing directory of a path (the path itself if it
# exists)
existing_dir() {
	d=$1
	while [ ! -d "$d" ]; do
		d=$(dirname "$d")
	done
	echo "$d"
}

# check_writable ACTION DIR...: each directory, or the nearest existing
# parent where it would be created, must be writable
check_writable() {
	action=$1
	shift
	for d in "$@"; do
		[ -w "$(existing_dir "$d")" ] ||
			die "$d is not writable, run this as root (doas/sudo $0 $action)"
	done
}

# Run the install targets with the given DESTDIR (empty: none).
# The parts are installed directly: the top-level "install" target depends
# on "all", which would regenerate files in the tree as root.
run_install() {
	for d in Linux Plug-ins Build Sources_Common; do
		$MAKE_CMD -C "$d" SKIPJX=yes DESTDIR="$1" install
	done
}

do_uninstall() {
	system_libdir=/usr/local/lib/mulberry
	user_libdir=$HOME/.mulberry

	case $LAYOUT in
	system)
		libdir=$system_libdir
		;;
	user)
		libdir=$user_libdir
		;;
	*)
		# Not given: use the only installation found
		found=
		for d in "$system_libdir" "$user_libdir"; do
			if [ -f "${STAGE_DIR}$d/.install-manifest" ]; then
				found="$found $d"
			fi
		done
		case $found in
		"")
			die "no installation found (no install manifest in ${STAGE_DIR}$system_libdir or ${STAGE_DIR}$user_libdir; use -d DESTDIR for a staging directory)"
			;;
		" $system_libdir $user_libdir")
			die "both a system (${STAGE_DIR}$system_libdir) and a per-user (${STAGE_DIR}$user_libdir) installation exist: choose one with -s or -u"
			;;
		esac
		libdir=${found# }
		note "found installation in ${STAGE_DIR}$libdir"
		;;
	esac
	manifest=${STAGE_DIR}$libdir/.install-manifest
	[ -f "$manifest" ] ||
		die "no install manifest $manifest (nothing installed there by this script?)"

	m_bindir=$(sed -n 's/^bindir //p' "$manifest")
	m_libdir=$(sed -n 's/^libdir //p' "$manifest")
	m_helpdir=$(sed -n 's/^helpdir //p' "$manifest")
	[ "$m_libdir" = "$libdir" ] ||
		die "$manifest is for $m_libdir, not $libdir"
	for d in "$m_bindir" "$m_helpdir"; do
		case $d in
		/*) ;;
		*) die "invalid directory '$d' in $manifest" ;;
		esac
	done
	if [ -z "$STAGE_DIR" ] && [ "$(id -u)" -eq 0 ]; then
		owner=$(stat -f %Su "$m_libdir")
		[ "$owner" = root ] ||
			die "$m_libdir is a per-user installation (owned by $owner): run '$0 uninstall' as $owner, without doas/sudo"
	fi
	check_writable uninstall "${STAGE_DIR}$m_bindir" "${STAGE_DIR}$m_libdir" \
		"${STAGE_DIR}$m_helpdir"

	# Directories holding installed files (up to the Mulberry library and
	# help directories), deepest first; they are removed only if empty.
	dirs=$(awk -v lib="$m_libdir" -v help="$m_helpdir" '
		$1 == "file" {
			p = substr($0, 6)
			while (sub(/\/[^\/]*$/, "", p)) {
				if (p == lib || p == help) { print p; break }
				if (index(p, lib "/") != 1 && index(p, help "/") != 1) break
				print p
			}
		}' "$manifest" | sort -u | awk '{ print length($0) "\t" $0 }' |
		sort -rn | cut -f2-)

	removed=0
	while read -r kind path; do
		[ "$kind" = file ] || continue
		# Only files in the Mulberry directories, whatever the manifest says
		case $path in
		*/../*|*/..)
			note "skipping $path (invalid path)"
			continue
			;;
		"$m_bindir"/mulberry|"$m_libdir"/*|"$m_helpdir"/*)
			;;
		*)
			note "skipping $path (outside the Mulberry directories)"
			continue
			;;
		esac
		if [ -e "${STAGE_DIR}$path" ] || [ -L "${STAGE_DIR}$path" ]; then
			rm -f -- "${STAGE_DIR}$path"
			removed=$((removed + 1))
		fi
	done < "$manifest"
	rm -f -- "$manifest"

	printf '%s\n' "$dirs" | while IFS= read -r d; do
		[ -n "$d" ] && rmdir -- "${STAGE_DIR}$d" 2>/dev/null || true
	done

	note "removed $removed files"
	for d in "$m_libdir" "$m_helpdir"; do
		[ -d "${STAGE_DIR}$d" ] &&
			note "kept ${STAGE_DIR}$d (not empty: files not installed by this script)"
		# In the per-user layout both are ~/.mulberry: report it once
		[ "$m_helpdir" != "$m_libdir" ] || break
	done
	return 0
}

do_clean() {
	if [ -f Makefile ]; then
		$MAKE_CMD distclean || true
	fi
	rm -rf "$JXDIST" Libraries/htmldeps
	note "clean complete"
}

case $ACTION in
build)   do_build ;;
install) do_install ;;
uninstall) do_uninstall ;;
clean)   do_clean ;;
*)       usage >&2; exit 2 ;;
esac
