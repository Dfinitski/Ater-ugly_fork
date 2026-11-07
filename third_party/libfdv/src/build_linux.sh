#!/bin/bash
# libfdv.so + libcodec2.so for the Linux build (AppImage), from the same sources
# and revisions as build_win.sh, built natively with gcc. The wrapper was
# developed and tested on Linux (README.txt: test_fdv, off-air RADE and loopback
# in every mode); only the shipped DLLs were cross-built for Windows.
#
#   build_linux.sh [deps-dir] [out-dir]
#
# Out: <out-dir>/libfdv.so (opus static inside, RUNPATH $ORIGIN:$ORIGIN/../lib)
# and <out-dir>/libcodec2.so.* (its SONAME). FdvEngine loads "libfdv" from the
# directory of the executable first, as on Windows: the AppImage carries
# libfdv.so in usr/bin and libcodec2 in usr/lib.
set -euo pipefail
W=$(cd "$(dirname "$0")" && pwd)
D=${1:-$W/deps}
OUT=${2:-$W/linux64}
mkdir -p "$D" "$OUT"
D=$(cd "$D" && pwd)
OUT=$(cd "$OUT" && pwd)

fetch() {   # name url commit
    if [ ! -d "$D/$1/.git" ]; then
        git clone -q "$2" "$D/$1"
    fi
    git -C "$D/$1" checkout -q "$3"
}
fetch codec2 https://github.com/drowe67/codec2 310777b1c6f1af0bc7c72f5b32f80f6fd9136962
fetch rade_c https://github.com/freedv/rade_c c8a3dc156045cae2cd251e1a4be0c304c9ddf2f9
fetch opus   https://github.com/xiph/opus     940d4e5af64351ca8ba8390df3f555484c567fbb

# 1. opus (static, PIC; FARGAN / LPCNet / DRED / OSCE) with rade_c's patches.
#    autogen.sh downloads the DNN model data (media.xiph.org).
cd "$D/opus"
patch -N dnn/nnet.h < "$D/rade_c/src/opus-nnet.h.diff" || true
patch -N dnn/nnet.c < "$D/rade_c/src/opus-nnet.c.diff" || true
./autogen.sh
./configure --with-pic --enable-osce --enable-dred --disable-shared \
    --disable-doc --disable-extra-programs CFLAGS=-O2
make -j"$(nproc)"

# 2. codec2 -> libcodec2.so
cmake -S "$D/codec2" -B "$D/codec2/build_linux" -DCMAKE_BUILD_TYPE=Release -DUNITTEST=OFF
cmake --build "$D/codec2/build_linux" --target codec2 -j"$(nproc)"

# 3. libfdv.so: rade_c (the library sources) + opus + this wrapper
R=$D/rade_c/src; O=$D/opus; C2=$D/codec2; C2B=$C2/build_linux
OBJ=$W/obj_linux
rm -rf "$OBJ"; mkdir -p "$OBJ"
INC="-I$R -I$O/dnn -I$O/celt -I$O/include -I$O -I$O/silk"
for f in rade_api rade_enc rade_dec rade_enc_data rade_dec_data rade_dsp rade_ofdm rade_bpf rade_acq rade_tx rade_rx \
         rade_enc_v2 rade_dec_v2 rade_sync rade_enc_v2_data rade_dec_v2_data rade_sync_data rade_v2_ofdm rade_tx_v2 rade_rx_v2; do
    gcc -O2 -w -fPIC -DIS_BUILDING_RADE_API=1 $INC -c "$R/$f.c" -o "$OBJ/$f.o"
done
for f in fdv_engine rade_text ldpc_encode ldpc_decode; do
    g++ -O2 -w -fPIC -std=c++17 -DIS_BUILDING_RADE_API=1 -I"$W" $INC -I"$C2/src" -I"$C2B" -I"$C2B/src" \
        -c "$W/$f.cpp" -o "$OBJ/$f.o"
done
gcc -O2 -w -fPIC -I"$C2/src" -I"$C2B" -I"$C2B/src" -c "$W/fdv_c2hook.c" -o "$OBJ/fdv_c2hook.o"

C2LIB=$(readlink -f "$C2B/src/libcodec2.so")
C2SONAME=$(objdump -p "$C2LIB" | awk '/SONAME/ {print $2}')
g++ -shared -o "$OUT/libfdv.so" "$OBJ"/*.o "$O/.libs/libopus.a" \
    -L"$C2B/src" -lcodec2 -lm -lpthread \
    -static-libstdc++ -static-libgcc -Wl,-rpath,'$ORIGIN:$ORIGIN/../lib' -Wl,--no-undefined
cp "$C2LIB" "$OUT/$C2SONAME"
echo "libfdv.so, $C2SONAME -> $OUT"
