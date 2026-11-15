#!/bin/bash
# MicronSDR FreeDV library for macOS on Apple silicon: libfdv.dylib + libcodec2.1.2.dylib (the same
# sources and versions as build_win.sh / build_linux.sh). Run it on a Mac (arm64) with:
#   Xcode command line tools:  xcode-select --install       (clang, ld, otool, codesign)
#   cmake, git:                brew install cmake git        (patch, curl: part of macOS)
#   ./build_macos.sh [deps dir]     default: ./deps - the sources are fetched there if missing
# The opus DNN model data (opus_data-4ed9445b....tar.gz, 183 MB) is needed once: put it into the
# deps dir (or it is downloaded from media.xiph.org).
# The result: arm64 only, macOS 14.4 or newer (MACOS_MIN), signed ad hoc;
#   libfdv.dylib        install_name @rpath/libfdv.dylib, only fdv_* exported, opus and rade_c
#                       inside, codec2 by @loader_path/libcodec2.1.2.dylib (next to itself)
#   libcodec2.1.2.dylib install_name @rpath/libcodec2.1.2.dylib (LGPL: a library of its own)
# both depend only on /usr/lib/libSystem.B.dylib and /usr/lib/libc++.1.dylib.
# The checks at the end fail the build otherwise. Log: build_macos.log.
set -e
W=$(cd "$(dirname "$0")" && pwd); D=${1:-$W/deps}
exec > >(tee $W/build_macos.log) 2>&1
MACOS_MIN=14.4
export MACOSX_DEPLOYMENT_TARGET=$MACOS_MIN
CC=${CC:-clang}; CXX=${CXX:-clang++}
ARCH="-arch arm64 -mmacosx-version-min=$MACOS_MIN"
CMAKE_MAC="-DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=$MACOS_MIN -DCMAKE_C_COMPILER=$CC -DCMAKE_CXX_COMPILER=$CXX"
fail() { echo; echo "*** FAILED: $1"; echo "*** send build_macos.log"; exit 1; }
trap 'fail "the command on line $LINENO"' ERR
# --- the checks of a built macOS library (the same block in all build_macos.sh) ---
# check_dylib <file> <id> <exported prefixes regex, ".*": any> [extra allowed dependency]
check_dylib() {
    local f=$1 id=$2 rx=$3 extra=$4 bad=0
    echo "== check $f"
    local a; a=$(lipo -archs "$f" 2>/dev/null || true)
    [ "$a" = "arm64" ] || { echo "!! architectures: '$a' (must be arm64 only)"; bad=1; }
    local minos; minos=$(otool -l "$f" | awk '/LC_BUILD_VERSION/{b=1} b&&/minos/{print $2; exit}' || true)
    [ "$minos" = "$MACOS_MIN" ] || { echo "!! minos $minos (must be $MACOS_MIN)"; bad=1; }
    local gid; gid=$(otool -D "$f" | tail -n +2)
    [ "$gid" = "$id" ] || { echo "!! install_name '$gid' (must be $id)"; bad=1; }
    # dependencies: only libSystem, libc++ (and codec2 next to libfdv)
    otool -L "$f" | tail -n +2 | awk '{print $1}' | while read -r d; do
        case "$d" in
            "$id"|/usr/lib/libSystem.B.dylib|/usr/lib/libc++.1.dylib) ;;
            *) if [ -n "$extra" ] && [ "$d" = "$extra" ]; then :; else echo "!! dependency $d"; echo x >> "$f.bad"; fi ;;
        esac
    done
    [ -f "$f.bad" ] && { bad=1; rm -f "$f.bad"; }
    # exports: only the C API functions
    # ALLOW_EXPORTS: the exact names of other symbols allowed (one regex, whole name)
    local ex; ex=$(nm -gU "$f" | awk '{print $3}' | grep -vE "^_($rx)" | grep -vxE "${ALLOW_EXPORTS:-^$}" || true)
    [ -z "$ex" ] || { echo "!! other exported symbols:"; echo "$ex" | head -20; bad=1; }
    codesign -v "$f" 2>/dev/null || { echo "!! not signed (codesign -s - -f $f)"; bad=1; }
    otool -L "$f" | sed 's/^/   /'
    echo "   exports: $(nm -gU "$f" | awk '{print $3}' | tr '\n' ' ')"
    [ $bad = 0 ] && echo "   OK: arm64, macOS $MACOS_MIN, $id, signed" || fail "$f does not meet the requirements (see !! above)"
}
echo "== $(date)"; echo "== $(uname -a)"; echo "== $($CC --version | head -1)"
[ "$(uname -s)" = Darwin ] || fail "this script is for macOS (Linux: build_linux.sh)"
command -v cmake >/dev/null || fail "no cmake: brew install cmake"
command -v git >/dev/null || fail "no git: brew install git (or xcode-select --install)"
OPUS_DATA=opus_data-4ed9445b96698bad25d852e912b41495ddfa30c8dbc8a55f9cde5826ed793453.tar.gz
NCPU=$(sysctl -n hw.ncpu)

# 0. the sources at the versions of build_win.sh
mkdir -p $D
get() {     # dir url commit
    if [ ! -d $D/$1 ]; then git clone -q $2 $D/$1; fi
    (cd $D/$1 && git checkout -q $3)
}
get codec2 https://github.com/drowe67/codec2 310777b1c6f1af0bc7c72f5b32f80f6fd9136962
get rade_c https://github.com/freedv/rade_c c8a3dc156045cae2cd251e1a4be0c304c9ddf2f9
get opus https://github.com/xiph/opus 940d4e5af64351ca8ba8390df3f555484c567fbb
if [ ! -f $D/opus/dnn/fargan_data.c ]; then
    [ -f $D/$OPUS_DATA ] || curl -fL -o $D/$OPUS_DATA https://media.xiph.org/opus/models/$OPUS_DATA
    (cd $D/opus && tar xzf $D/$OPUS_DATA)
fi

# 1. opus (static, PIC; FARGAN / LPCNet / DRED / OSCE) with the patches of rade_c
cd $D/opus
patch -N -s dnn/nnet.h < $D/rade_c/src/opus-nnet.h.diff || true
patch -N -s dnn/nnet.c < $D/rade_c/src/opus-nnet.c.diff || true
rm -rf build_mac && mkdir -p build_mac && cd build_mac
cmake .. -DCMAKE_BUILD_TYPE=Release -DOPUS_DRED=ON -DOPUS_OSCE=ON -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DBUILD_SHARED_LIBS=OFF -DOPUS_BUILD_PROGRAMS=OFF -DOPUS_BUILD_TESTING=OFF $CMAKE_MAC >/dev/null
make -j$NCPU opus

# 2. codec2 -> libcodec2.1.2.dylib
rm -rf $D/codec2/build_mac && mkdir -p $D/codec2/build_mac && cd $D/codec2/build_mac
cmake .. -DCMAKE_BUILD_TYPE=Release -DUNITTEST=OFF $CMAKE_MAC >/dev/null
make -j$NCPU codec2
C2LIB=""
for f in src/libcodec2*.dylib; do [ -L "$f" ] || C2LIB=$f; done
[ -n "$C2LIB" ] || fail "no libcodec2*.dylib in $D/codec2/build_mac/src"
rm -f $W/libcodec2*.dylib
cp "$C2LIB" $W/libcodec2.1.2.dylib
chmod u+w $W/libcodec2.1.2.dylib
strip -x $W/libcodec2.1.2.dylib
install_name_tool -id @rpath/libcodec2.1.2.dylib $W/libcodec2.1.2.dylib

# 3. libfdv.dylib: rade_c (the library sources) + opus + this wrapper; only fdv_* exported
rm -rf $W/obj_mac && mkdir -p $W/obj_mac && cd $W
R=$D/rade_c/src; O=$D/opus; C2=$D/codec2
INC="-I$R -I$O/dnn -I$O/celt -I$O/include -I$O -I$O/silk"
CFL="-O2 -fPIC $ARCH -fvisibility=hidden -w -DIS_BUILDING_RADE_API=1"
for f in rade_api rade_enc rade_dec rade_enc_data rade_dec_data rade_dsp rade_ofdm rade_bpf rade_acq rade_tx rade_rx \
         rade_enc_v2 rade_dec_v2 rade_sync rade_enc_v2_data rade_dec_v2_data rade_sync_data rade_v2_ofdm rade_tx_v2 rade_rx_v2; do
    echo "CC  rade_c/$f.c"
    $CC $CFL $INC -c $R/$f.c -o obj_mac/$f.o
done
for f in fdv_engine rade_text ldpc_encode ldpc_decode; do
    echo "CXX $f.cpp"
    $CXX $CFL -std=c++17 -DFDV_BUILDING -I$W $INC -I$C2/src -I$C2/build_mac -I$C2/build_mac/src -c $W/$f.cpp -o obj_mac/$f.o
done
echo "CC  fdv_c2hook.c"
$CC $CFL -I$C2/src -I$C2/build_mac -I$C2/build_mac/src -c $W/fdv_c2hook.c -o obj_mac/fdv_c2hook.o
echo "LD  libfdv.dylib"
echo '_fdv_*' > obj_mac/exports.txt
$CXX -dynamiclib $ARCH -o libfdv.dylib obj_mac/*.o $O/build_mac/libopus.a $W/libcodec2.1.2.dylib \
    -install_name @rpath/libfdv.dylib -Wl,-exported_symbols_list,obj_mac/exports.txt -Wl,-dead_strip
strip -x libfdv.dylib
install_name_tool -id @rpath/libfdv.dylib libfdv.dylib
# codec2 next to libfdv.dylib, whatever name the link recorded
C2REF=$(otool -L libfdv.dylib | awk 'NR>1 && /libcodec2/ {print $1}')
[ -n "$C2REF" ] || fail "libfdv.dylib has no reference to libcodec2"
install_name_tool -change "$C2REF" @loader_path/libcodec2.1.2.dylib libfdv.dylib
codesign -s - -f libcodec2.1.2.dylib libfdv.dylib
trap - ERR
check_dylib libcodec2.1.2.dylib @rpath/libcodec2.1.2.dylib ".*"
check_dylib libfdv.dylib @rpath/libfdv.dylib "fdv_" @loader_path/libcodec2.1.2.dylib

# 4. the offline test (needs the sources of rade_c: FDV_offair.wav, wav/peter.wav)
$CXX -O2 -std=c++17 $ARCH -I$W -o test_fdv test_fdv.cpp -L$W -lfdv -Wl,-rpath,@loader_path
codesign -s - -f test_fdv
echo
echo "== OK: libfdv.dylib $(stat -f %z libfdv.dylib) bytes, libcodec2.1.2.dylib $(stat -f %z libcodec2.1.2.dylib) bytes" \
     "(arm64, macOS $MACOS_MIN+)"
echo "== test:"
./test_fdv offair $D/rade_c
./test_fdv loop $D/rade_c
