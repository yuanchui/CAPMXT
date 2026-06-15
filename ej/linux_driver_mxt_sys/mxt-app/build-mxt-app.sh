#!/bin/bash

set -e
set -x

if [ "$1" == "master" ]
then
  git clean -xfd
  git stash
  git fetch
  git submodule init
  git submodule update
  git checkout -B build origin/master
fi

VERSION=$(build-aux/version.sh)
OUTDIR=mxt-app-$VERSION

rm -rf "$OUTDIR"
mkdir "$OUTDIR"

# Build both normal and debug versions
for DEBUG in 0 1
do
  case "$DEBUG" in
    0)
      CFLAGS="-O2"
      AUTOGEN_OPTIONS=""
      LIBDIR="libs"
      TARGET="install-strip"
      BINARY_NAME="mxt-app"
      ;;
    1)
      CFLAGS="-O0 -g3"
      AUTOGEN_OPTIONS="--enable-debug"
      LIBDIR="obj/local"
      TARGET="install"
      BINARY_NAME="mxt-app-debug"
      ;;
  esac


  for ARCH in x86 32bit arm armhf aarch64
  do
    case "$ARCH" in
      32bit)
        AUTOGEN_OPTIONS+=" --build=i686-pc-linux-gnu"
        LDFLAGS="LDFLAGS=\"-m32\""
        EXTRA_CFLAGS=" -m32"
        ;;
      arm)
        AUTOGEN_OPTIONS+=" --host=arm-linux-gnueabi"
        EXTRA_CFLAGS=""
        LDFLAGS=""
        ;;
      armhf)
        AUTOGEN_OPTIONS+=" --host=arm-linux-gnueabihf"
        EXTRA_CFLAGS=""
        LDFLAGS=""
        ;;
      aarch64)
        AUTOGEN_OPTIONS+=" --host=aarch64-linux-gnu"
        EXTRA_CFLAGS=""
        LDFLAGS=""
        ;;
    esac

    # Build and copy binaries using autotool toolchain
    ./autogen.sh "$AUTOGEN_OPTIONS"
    make clean
    make DESTDIR="$(pwd)/out" CFLAGS="$CFLAGS $EXTRA_CFLAGS" $LDFLAGS $TARGET
    mkdir -p -v "$OUTDIR/gnueabi/$ARCH"
    cp -v "out/usr/local/bin/mxt-app" "$OUTDIR/gnueabi/$ARCH/$BINARY_NAME"
  done
done

# Generate HTML documentation
pandoc --smart \
  --css=github-pandoc.css \
  --self-contained \
  -t html -o "$OUTDIR/README.html" \
  -V title="mxt-app v$VERSION user manual" \
  -V date="$(date +"%Y-%m-%d")" \
  README.md

# Generate changelog from git commit log
gitlog-to-changelog > "$OUTDIR/CHANGELOG.txt"

zip -r "mxt-app-$VERSION.zip" "mxt-app-$VERSION"
