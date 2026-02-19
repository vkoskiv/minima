#!/bin/bash
# This script will download and build the cross-compiler and other required
# tools for working on minima.
# Originally (c) 2018-2020 Andreas Kling and the SerenityOS Developers,
# released under the 2-clause BSD license. Modified slightly for this project.

set -e
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
echo "Entering $DIR"

MAKE="make"
NPROC="nproc"

ARCH=${ARCH:-"i686"}
TARGET="$ARCH-pc-minima"
PREFIX="$DIR/local"
SYSROOT="$DIR/../root"
export CFLAGS="-O2"
export CXXFLAGS="-O2"

echo "PREFIX is $PREFIX"
echo "SYSROOT is $SYSROOT"

mkdir -p "$DIR/tarballs"

BINUTILS_VERSION="2.33.1"
BINUTILS_HASH="1a6b16bcc926e312633fcc3fae14ba0a"
BINUTILS_NAME="binutils-$BINUTILS_VERSION"
BINUTILS_PKG="${BINUTILS_NAME}.tar.gz"
BINUTILS_URL="http://ftp.gnu.org/gnu/binutils"

GCC_VERSION="9.2.0"
GCC_HASH="e03739b042a14376d727ddcfd05a9bc3"
GCC_NAME="gcc-$GCC_VERSION"
GCC_PKG="${GCC_NAME}.tar.gz"
GCC_URL="http://ftp.gnu.org/gnu/gcc"

pushd "$DIR/tarballs"
	md5="$(md5sum $BINUTILS_PKG | cut -f1 -d' ')"
	echo "binutils md5='$md5'"
	if [ ! -e $BINUTILS_PKG ] || [ "$md5" != ${BINUTILS_HASH} ] ; then
		rm -f $BINUTILS_PKG
		wget "$BINUTILS_URL/$BINUTILS_PKG"
	else
		echo "Skip downloading binutils"
	fi

	md5="$(md5sum ${GCC_PKG} | cut -f1 -d' ')"
	echo "gcc md5='$md5'"
	if [ ! -e $GCC_PKG ] || [ "$md5" != ${GCC_HASH} ] ; then
		rm -f $GCC_PKG
		wget "$GCC_URL/$GCC_NAME/$GCC_PKG"
	else
		echo "Skip downloading gcc"
	fi

	if [ ! -d ${BINUTILS_NAME} ]; then
		echo "Extracting binutils..."
		tar -xzf ${BINUTILS_PKG}
		pushd ${BINUTILS_NAME}
			echo "Patching binutils..."
			patch -p1 <"$DIR"/patches/binutils.patch
		popd
	else
		echo "Skipped extracting binutils"
	fi

	if [ ! -d $GCC_NAME ]; then
		echo "Extracting GCC..."
		tar -xzf $GCC_PKG
		pushd $GCC_NAME
			echo "Patching GCC"
			patch -p1 <"$DIR"/patches/gcc.patch
		popd
	else
		echo "Skipped extracting GCC"
	fi
popd

# Compile & Install

mkdir -p "$PREFIX"
mkdir -p "$DIR"/build/binutils
mkdir -p "$DIR"/build/gcc

if [ -z "$MAKEJOBS" ]; then
	MAKEJOBS=$($NPROC)
fi

pushd "$DIR/build/"
	unset PKG_CONFIG_LIBDIR
	pushd binutils
		"$DIR"/tarballs/binutils-2.33.1/configure --prefix="$PREFIX" \
												--target="$TARGET" \
												--with-sysroot="$SYSROOT" \
												--enable-shared \
												--enable-interwork \
												--enable-multilib \
												--disable-werror \
												--disable-nls || exit 1
		if [ "$(uname)" = "Darwin" ]; then
			"$MAKE" -j "$MAKEJOBS" || true
			pushd intl
			"$MAKE" all-yes
			popd
		fi
		"$MAKE" -j "$MAKEJOBS" || exit 1
		"$MAKE" install || exit 1
	popd
	
	pushd gcc
		"$DIR/tarballs/gcc-$GCC_VERSION/configure" --prefix="$PREFIX" \
												--target="$TARGET" \
												--with-sysroot="$SYSROOT" \
												--disable-nls \
												--with-newlib \
												--enable-shared \
												--enable-languages=c,lto \
												--without-headers \
												--enable-interwork || exit 1
		echo "Build gcc and libgcc"
		"$MAKE" -j "$MAKEJOBS" all-gcc all-target-libgcc || exit 1
		echo "Install gcc and libgcc"
		"$MAKE" install-gcc install-target-libgcc || exit 1
	popd
popd

echo "Tidying up..."
set -x
du -sh .
rm -rf "$DIR/build" "$DIR/tarballs/$GCC_NAME" "$DIR/tarballs/$BINUTILS_NAME"
du -sh .
