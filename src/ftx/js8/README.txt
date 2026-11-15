JS8 text (the frames -> the messages): the files of JS8Call (https://github.com/js8call/js8call,
GPL v3, (C) 2018 Jordan Sherer KN4CRD and the JS8Call developers):

  varicode.cpp/.h     packing / unpacking of the JS8 frames (callsigns, commands, Huffman text)
  jsc.cpp/.h          JSC: the dictionary compression of the free text
  jsc_list.cpp, jsc_map.cpp   its dictionary (262144 words)
  decodedtext.cpp/.h  a decoded frame -> its type, callsigns, text
  commons.h           constants
  vendor/CRCpp/CRC.h  CRCpp (BSD)

Changed for MicronSDR (Qt 5.15, C++11):
  - decodedtext: the constructor from a libjs8 decode (struct Js8Decode) instead of JS8::Event;
    no C++17 (if with an initializer, the array of the unpacking strategies);
  - varicode: no C++17 (if with an initializer), QStringView::left instead of first (Qt 5);
  - varicode, jsc: qDebug off;
  - varicode buildMessageFrames: a character no frame can carry is dropped (it looped for ever).
The decoder itself: libjs8.dll (JS8_src, built with MSYS2).
