#!/usr/bin/env bash
# Build libjs8.dylib (the JS8 decoder of JS8Call) and test_js8 for macOS on Apple silicon.
# Run it on a Mac (arm64) with:
#   Xcode command line tools:  xcode-select --install       (clang 15+, C++20)
#   boost and Eigen headers:   brew install boost eigen       (headers only are used)
# The result: arm64 only, macOS 14.4 or newer (MACOS_MIN), install_name @rpath/libjs8.dylib,
# only js8_* exported, FFT by pocketfft (fft/), depends only on /usr/lib/libSystem.B.dylib and
# /usr/lib/libc++.1.dylib, signed ad hoc. The checks at the end fail the build otherwise.
#   BOOST_INC=/path/boost EIGEN_INC=/path/eigen ./build_macos.sh    headers elsewhere
# Output in this folder: libjs8.dylib, test_js8. Log: build_macos.log.

cd "$(dirname "$0")" || exit 1
exec > >(tee build_macos.log) 2>&1

MACOS_MIN=14.4
export MACOSX_DEPLOYMENT_TARGET=$MACOS_MIN
B=build_macos
CXX=${CXX:-clang++}
ARCH="-arch arm64 -mmacosx-version-min=$MACOS_MIN"
fail() { echo; echo "*** FAILED: $1"; echo "*** send build_macos.log"; exit 1; }
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
    local ex; ex=$(nm -gU "$f" | awk '{print $3}' | grep -vE "^_($rx)" || true)
    [ -z "$ex" ] || { echo "!! other exported symbols:"; echo "$ex" | head -20; bad=1; }
    codesign -v "$f" 2>/dev/null || { echo "!! not signed (codesign -s - -f $f)"; bad=1; }
    otool -L "$f" | sed 's/^/   /'
    echo "   exports: $(nm -gU "$f" | awk '{print $3}' | tr '\n' ' ')"
    [ $bad = 0 ] && echo "   OK: arm64, macOS $MACOS_MIN, $id, signed" || fail "$f does not meet the requirements (see !! above)"
}
echo "== $(date)"
echo "== $(uname -a)"
[ "$(uname -s)" = Darwin ] || fail "this script is for macOS (Linux: build_linux.sh)"
command -v $CXX >/dev/null || fail "no clang++: xcode-select --install"
echo "== $($CXX --version | head -1)"

BREW=$(command -v brew >/dev/null && brew --prefix || echo /opt/homebrew)
BOOST_INC=${BOOST_INC:-$BREW/include}
EIGEN_INC=${EIGEN_INC:-$BREW/include/eigen3}
[ -f "$BOOST_INC/boost/multi_index_container.hpp" ] || fail "no boost headers in $BOOST_INC: brew install boost"
[ -f "$EIGEN_INC/Eigen/Dense" ] || fail "no Eigen headers in $EIGEN_INC: brew install eigen"
# only the include dirs of boost / Eigen: nothing else of Homebrew is seen (no FFTW from there)
rm -rf "$B"; mkdir -p "$B/inc"
ln -s "$BOOST_INC/boost" "$B/inc/boost"
INC="-I$B/inc -I$EIGEN_INC"

CXXFLAGS="-std=gnu++20 -O2 -fPIC $ARCH -DJS8_BUILD -DNDEBUG -I. -Ifft $INC -w"
echo "CXX fft/fftwf_pocketfft.cpp"
$CXX -std=gnu++17 -O3 -fPIC $ARCH -fvisibility=hidden -Ifft -c fft/fftwf_pocketfft.cpp -o $B/fftwf_pocketfft.o || fail "fftwf_pocketfft.cpp"
echo "CXX JS8core.cpp"
$CXX $CXXFLAGS -c JS8core.cpp -o $B/JS8core.o || fail "JS8core.cpp"

printf '_js8_version\n_js8_period_samples\n_js8_decode\n_js8_encode\n' > $B/exports.txt
echo "LD  libjs8.dylib"
$CXX -dynamiclib $ARCH -o libjs8.dylib $B/JS8core.o $B/fftwf_pocketfft.o \
    -install_name @rpath/libjs8.dylib -Wl,-exported_symbols_list,$B/exports.txt -Wl,-dead_strip \
    || fail "libjs8.dylib"
strip -x libjs8.dylib
install_name_tool -id @rpath/libjs8.dylib libjs8.dylib
codesign -s - -f libjs8.dylib || fail "codesign libjs8.dylib"
check_dylib libjs8.dylib @rpath/libjs8.dylib "js8_"

echo "CXX test_js8"
$CXX -std=gnu++17 -O2 $ARCH -o test_js8 test/test_js8.cpp -L. -ljs8 -Wl,-rpath,@loader_path || fail "test_js8"
codesign -s - -f test_js8

echo
echo "== OK: libjs8.dylib $(stat -f %z libjs8.dylib) bytes (arm64, macOS $MACOS_MIN+)"
echo "== test:"
./test_js8
