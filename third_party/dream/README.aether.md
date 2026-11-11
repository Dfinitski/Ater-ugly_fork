# Dream DRM receiver core (ANAN port)

Origin: https://github.com/Drm-tools/dream, commit 7b38ce59637a588960f7f379ca36f1851f68ce6e
(2026-08). Licence: GPL v2 or (at your option) any later version -- see COPYING.

## What is here

Only the receiver core: the 99 source files (and their headers) that the
receiver links, found with a link map of a receiver-only test program. Not
here: the Qt GUI, the sound card drivers, the transmitter, the simulation,
Dream's main(), the platform Pacer files.

Built by CMakeLists.txt as the static library `aether_dream`, WITHOUT Qt
(NO_QT, QT_CORE_LIB undefined). AetherSDR includes only `aether/DrmEngine.h`,
a plain C++ interface; Dream's headers change meaning with QT_CORE_LIB and must
never be compiled into Qt code.

## AetherSDR files

- `aether/DrmEngine.{h,cpp}` -- one receiver on its own thread: I/Q in, audio
  out, a status snapshot, slideshow pictures, Journaline pages.
- `aether/fdk_dyn.cpp` -- FDK-AAC loaded at run time (fdk-aac.dll /
  libfdk-aac.so.2 / libfdk-aac.2.dylib next to the program or on the library
  path). Not linked. Without it AAC / xHE-AAC audio is not decoded; everything
  else works.
- `aether/Pacer.cpp` -- portable CPacer (std::chrono).
- `../fdk-aac/include/fdk-aac/*.h` -- FDK-AAC API headers for the types only,
  with the FDK-AAC licence in `../fdk-aac/NOTICE`.

## Local changes to Dream (all marked "AetherSDR")

- `src/DrmReceiver.{h,cpp}`: SetExternalInput / SetExternalOutput. With no
  sound driver compiled in, LoadSettings binds the input to an RSCI source and
  the receiver never reads the host's samples.
- `src/sourcedecoders/fdk_aac_codec.cpp`: aacinfo() zeroes its result (library
  absent -> no capabilities); DecGetVersion() copes with a null title.
- `src/util/Pacer.h`: no <windows.h>; one portable implementation.
- `src/datadecoding/DABMOT.cpp`: without Qt, completed MOT objects are queued
  for the host (as the Qt build does) instead of written to files in the
  working directory; the queue is bounded.
- `src/datadecoding/DataDecoder.cpp`: EPG objects are not written to files.
- `src/sound/soundfactory.cpp`: no Windows MME driver under AETHER_DREAM (src/windows
  is not vendored; the host feeds the receiver).
