#!/usr/bin/env bash
# Build libftx.dylib (the FT8 / FT4 / WSPR decoders and encoders of WSJT-X) for macOS on Apple
# silicon, and the test programs. Run it on a Mac (arm64) with:
#   Xcode command line tools:  xcode-select --install       (clang, ld, otool, codesign)
#   Homebrew gfortran:         brew install gcc
# The result: arm64 only, macOS 14.4 or newer (MACOS_MIN), install_name @rpath/libftx.dylib,
# only ftx_* / wspr_* exported (and 4 helpers of the GCC runtime, see check_dylib below),
# libgfortran / libquadmath / libgcc linked in statically, FFT by
# pocketfft (c/fftwf_pocketfft.cpp): it depends only on /usr/lib/libSystem.B.dylib and
# /usr/lib/libc++.1.dylib, signed ad hoc. The checks at the end fail the build otherwise.
#   FC=/opt/homebrew/bin/gfortran-15 ./build_macos.sh    another gfortran
#   EXTRA_LDFLAGS=-Wl,-ld_classic ./build_macos.sh       if the new Xcode linker refuses GCC objects
# Output in this folder: libftx.dylib, test_ftx, wsprgen, ft8sim, ft4sim. Log: build_macos.log.

cd "$(dirname "$0")" || exit 1
exec > >(tee build_macos.log) 2>&1

MACOS_MIN=14.4
export MACOSX_DEPLOYMENT_TARGET=$MACOS_MIN
B=build_macos
FC=${FC:-gfortran}
CC=${CC:-clang}
CXX=${CXX:-clang++}
ARCH="-arch arm64 -mmacosx-version-min=$MACOS_MIN"
FFLAGS="-O2 -fPIC -mmacosx-version-min=$MACOS_MIN -funroll-all-loops -fno-f2c -fno-second-underscore -fallow-argument-mismatch -Wno-conversion -Wno-c-binding-type -w"
INC="-Ilib -Ilib/ft8 -Ilib/ft4 -Ilib/77bit -J$B/mod -I$B/mod"
FFTINC="-Ic/fftw"

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
[ "$(uname -m)" = arm64 ] || fail "run it on an Apple silicon Mac (arm64), not $(uname -m)"
command -v $FC >/dev/null || fail "no gfortran ($FC): brew install gcc"
command -v $CC >/dev/null || fail "no clang: xcode-select --install"
echo "== $($FC --version | head -1)"
echo "== $($CC --version | head -1)"
case "$($FC -dumpmachine)" in aarch64-*|arm64-*) ;; *) fail "$FC does not make arm64 code: $($FC -dumpmachine)";; esac
LIBGF=$($FC -print-file-name=libgfortran.a)
[ -f "$LIBGF" ] || fail "no libgfortran.a next to $FC (needed to link it statically)"
echo "== libgfortran.a: $LIBGF"

rm -rf "$B"
mkdir -p "$B/obj" "$B/mod" "$B/sim" "$B/fft"

# the Fortran sources: the modules first, in the order of their use (as build.sh)
SRC="\
    fftw3mod.f90 packjt.f90 ft8var/packjt77var.f90 77bit/packjt77.f90 crc.f90 timer_module.f90 \
    ft4_decode.f90 ft8/ft8_a7.f90 shmem.f90 ft8_decode.f90 bpdecode144.f90 chkcall.f90 db.f90 \
    deg2grid.f90 determ.f90 fmtmsg.f90 four2a.f90 gfsk_pulse.f90 \
    ft4/ft4_baseline.f90 ft4/ft4_downsample.f90 ft4/gen_ft4wave.f90 ft4/genft4.f90 \
    ft4/get_ft4_bitmetrics.f90 ft4/getcandidates4.f90 ft4/subtractft4.f90 ft4/sync4d.f90 \
    ft8/baseline.f90 ft8/decode174_91.f90 ft8/encode174_91.f90 ft8/encode174_91_nocrc.f90 \
    ft8/ft8_a8d.f90 ft8/ft8_downsample.f90 ft8/ft8apset.f90 ft8/ft8b.f90 ft8/gen_ft8wave.f90 \
    ft8/genft8.f90 ft8/get_crc14.f90 ft8/get_spectrum_baseline.f90 ft8/osd174_91.f90 \
    ft8/subtractft8.f90 ft8/sync8.f90 ft8/sync8d.f90 ft8/twkfreq1.f90 grid2deg.f90 indexx.f90 \
    nuttal_window.f90 pctile.f90 peakup.f90 polyfit.f90 shell.f90 smo121.f90 stdcall.f90 \
    wsprd/osdwspr.f90"

n=0
for f in $SRC; do
    o="$B/obj/$(echo "$f" | tr '/' '_' | sed 's/\.f90$/.o/')"
    echo "FC  lib/$f"
    $FC $FFLAGS $INC -c "lib/$f" -o "$o" || fail "lib/$f"
    n=$((n+1))
done
echo "FC  ftx_api.f90"
$FC $FFLAGS $INC -c ftx_api.f90 -o "$B/obj/ftx_api.o" || fail "ftx_api.f90"
for f in crc fftw_shim shmem_stub gran sgran wspr_api; do
    echo "CC  c/$f.c"
    $CC -O2 -fPIC $ARCH $FFTINC -c "c/$f.c" -o "$B/obj/c_$f.o" || fail "c/$f.c"
done
for f in wsprd wsprd_utils wsprsim_utils fano jelinek nhash tab; do
    echo "CC  lib/wsprd/$f.c"
    D=""; [ $f = wsprd ] && D="-Dmain=wsprd_main"
    $CC -O2 -fPIC $ARCH -w $D $FFTINC -Ilib/wsprd -c "lib/wsprd/$f.c" -o "$B/obj/wsprd_$f.o" || fail "lib/wsprd/$f.c"
done
echo "CXX c/fftwf_pocketfft.cpp"
$CXX -O3 -fPIC $ARCH -std=c++17 -fvisibility=hidden -c c/fftwf_pocketfft.cpp -o "$B/fft/fftwf_pocketfft.o" || fail "fftwf_pocketfft.cpp"
FFTLIB="$B/fft/fftwf_pocketfft.o"
echo "== $n Fortran files compiled"

# libquadmath inside too, where GCC has it (on arm64 macOS it usually has none: then not needed)
QUADMATH=""
if [ -f "$($FC -print-file-name=libquadmath.a)" ]; then QUADMATH="-static-libquadmath"; fi
echo "== libquadmath: ${QUADMATH:-none on this GCC (not needed)}"
# only the five functions of ftx_api.h exported
printf '_ftx_version\n_ftx_decode\n_wspr_decode\n_ftx_encode\n_wspr_encode\n' > $B/exports.txt
echo "LD  libftx.dylib"
$FC -dynamiclib -o libftx.dylib $B/obj/*.o $FFTLIB -mmacosx-version-min=$MACOS_MIN \
    -install_name @rpath/libftx.dylib -Wl,-exported_symbols_list,$B/exports.txt -Wl,-dead_strip \
    -static-libgfortran -static-libgcc $QUADMATH -lc++ $EXTRA_LDFLAGS || fail "libftx.dylib"
strip -x libftx.dylib
install_name_tool -id @rpath/libftx.dylib libftx.dylib
codesign -s - -f libftx.dylib || fail "codesign libftx.dylib"
# Besides the five API functions the GCC driver exports four helpers of the GCC runtime that it
# links in itself (emutls: ___emutls_get_address / ___emutls_register_common; trampolines of
# nested Fortran procedures: ___gcc_nested_func_ptr_created / _deleted); -exported_symbols_list
# does not hide them. They are allowed: a program looks up only ftx_* / wspr_*, and macOS'
# two-level namespace binds each symbol to the library it was linked against.
check_dylib libftx.dylib @rpath/libftx.dylib "ftx_|wspr_|__emutls_|__gcc_nested_func_ptr_"

echo "CC  test_ftx"
$CC -O2 $ARCH -o test_ftx test/test_ftx.c -L. -lftx -Wl,-rpath,@loader_path || fail "test_ftx"
echo "CC  wsprgen"
$CC -O2 $ARCH -w -Ilib/wsprd -o wsprgen test/wsprgen.c $B/obj/wsprd_wsprsim_utils.o $B/obj/wsprd_wsprd_utils.o \
    $B/obj/wsprd_nhash.o $B/obj/wsprd_fano.o $B/obj/wsprd_tab.o || fail "wsprgen"
echo "FC  simulators"
$FC $FFLAGS $INC -c lib/wavhdr.f90 -o $B/sim/wavhdr.o || fail "wavhdr.f90"
$FC $FFLAGS $INC -c lib/ft8/watterson.f90 -o $B/sim/watterson.o || fail "watterson.f90"
LIBOBJ=$(ls $B/obj/*.o | grep -v "ftx_api.o\|wsprd_\|wspr_api")
for s in ft8/ft8sim ft4/ft4sim; do
    e=$(basename $s)
    $FC $FFLAGS $INC -c lib/$s.f90 -o $B/sim/$e.o || fail "$s.f90"
    $FC -o $e $B/sim/$e.o $B/sim/wavhdr.o $B/sim/watterson.o $LIBOBJ $FFTLIB -mmacosx-version-min=$MACOS_MIN \
        -static-libgfortran -static-libgcc $QUADMATH -lc++ $EXTRA_LDFLAGS || fail "$e"
done
codesign -s - -f test_ftx wsprgen ft8sim ft4sim

echo
echo "== OK: libftx.dylib $(stat -f %z libftx.dylib) bytes (arm64, macOS $MACOS_MIN+)"
ls -l libftx.dylib test_ftx wsprgen ft8sim ft4sim
echo "== quick test:"
./ft8sim "CQ N7DDC CN87" 1500 0.0 0.1 1.0 1 -15 >/dev/null && ./test_ftx ft8 000000_000001.wav
