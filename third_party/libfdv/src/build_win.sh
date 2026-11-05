#!/bin/bash
# MicronSDR FreeDV library (libfdv.dll + libcodec2.dll), cross-built on Linux with llvm-mingw
# (x86_64-w64-mingw32-clang, msvcrt). Sources (git):
#   codec2          https://github.com/drowe67/codec2          310777b1c6f1af0bc7c72f5b32f80f6fd9136962  (LGPL-2.1)
#   rade_c          https://github.com/freedv/rade_c           c8a3dc156045cae2cd251e1a4be0c304c9ddf2f9  (BSD-2)
#   opus            https://github.com/xiph/opus               940d4e5af64351ca8ba8390df3f555484c567fbb  (BSD-3)
#                   + its DNN model opus_data-4ed9445b...tar.gz (media.xiph.org) unpacked in the tree
#                   + the patches of rade_c: src/opus-nnet.h.diff, src/opus-nnet.c.diff
#   rade_text / LDPC: https://github.com/tmiw/freedv-backend   e9d58299926fe438cba5ec5cb5a6870028c718ac  (BSD-2)
#                   (src/pipeline: rade_text.*, ldpc_*.*, HRA_56_56.h - copied here, logging stubbed)
set -e
W=$PWD; D=${1:-$W/deps}
# 1. opus (static, FARGAN / LPCNet / DRED / OSCE)
cd $D/opus && patch -N dnn/nnet.h < $D/rade_c/src/opus-nnet.h.diff || true
patch -N dnn/nnet.c < $D/rade_c/src/opus-nnet.c.diff || true
./autogen.sh && ./configure --host=x86_64-w64-mingw32 --with-pic --enable-osce --enable-dred --disable-shared \
    --disable-doc --disable-extra-programs CFLAGS=-O2 && make -j8
# 2. codec2 -> libcodec2.dll
mkdir -p $D/codec2/build_win && cd $D/codec2/build_win
cmake -DCMAKE_TOOLCHAIN_FILE=$W/mingw.cmake -DCMAKE_BUILD_TYPE=Release -DUNITTEST=OFF .. && make -j8 codec2
# 3. libfdv.dll: rade_c (the library sources) + opus + this wrapper
mkdir -p $W/obj && cd $W
R=$D/rade_c/src; O=$D/opus; C2=$D/codec2
INC="-I$R -I$O/dnn -I$O/celt -I$O/include -I$O -I$O/silk"
for f in rade_api rade_enc rade_dec rade_enc_data rade_dec_data rade_dsp rade_ofdm rade_bpf rade_acq rade_tx rade_rx \
         rade_enc_v2 rade_dec_v2 rade_sync rade_enc_v2_data rade_dec_v2_data rade_sync_data rade_v2_ofdm rade_tx_v2 rade_rx_v2; do
    x86_64-w64-mingw32-clang -O2 -w -DIS_BUILDING_RADE_API=1 $INC -c $R/$f.c -o obj/$f.o
done
for f in fdv_engine rade_text ldpc_encode ldpc_decode; do
    x86_64-w64-mingw32-clang++ -O2 -w -std=c++17 -DIS_BUILDING_RADE_API=1 -I$W $INC -I$C2/src -I$C2/build_win -I$C2/build_win/src -c $W/$f.cpp -o obj/$f.o
done
x86_64-w64-mingw32-clang -O2 -w -I$C2/src -I$C2/build_win -I$C2/build_win/src -c $W/fdv_c2hook.c -o obj/fdv_c2hook.o
x86_64-w64-mingw32-clang++ -shared -o libfdv.dll obj/*.o $O/.libs/libopus.a $C2/build_win/src/libcodec2.dll.a -static
cp $C2/build_win/src/libcodec2.dll .
echo "libfdv.dll, libcodec2.dll -> win64/"
