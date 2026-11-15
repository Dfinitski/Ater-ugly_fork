/* fdv_c2hook.c - the text callbacks of a codec2 freedv object (its internals: freedv_api_internal.h
   of codec2), for fdv_engine.cpp: the reliable text (callsign) of FreeDV-GUI and the plain
   varicode text share the text channel - the bits are taken here and given to both. */
#include "freedv_api_internal.h"

void fdvc2_get_txt_callbacks(struct freedv *f, void (**rx)(void *, char), char (**tx)(void *), void **state)
{
    *rx = f->freedv_put_next_rx_char;
    *tx = f->freedv_get_next_tx_char;
    *state = f->callback_state;
}
