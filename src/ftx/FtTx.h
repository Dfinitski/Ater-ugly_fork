// ANAN fork build 381: this file is MicronSDR's (N7DDC), taken as it is for the
// FTX mode of this program; the changes for it are marked "AetherSDR".
/*
 * FtTx - the transmitter of the FT8 / FT4 / WSPR mode: the waveform of a message (the channel
 * symbols of libftx.dll: ftx_encode / wspr_encode) made at 48 kHz as WSJT-X makes it, and played
 * into the TX DSP of the program instead of the microphone (the audio thread: audio(), as the
 * TX audio of TCI) at its time:
 *   - FT8: 79 symbols, 8-GFSK, 6.25 baud, BT 2.0 (gen_ft8wave), 12.64 s from 0.5 s of the period;
 *   - FT4: 103 symbols + the ramp up / down ones, 4-GFSK, 20.833 baud, BT 1.0 (gen_ft4wave),
 *     5.04 s from 0.5 s of the period;
 *   - WSPR: 162 symbols, 4-FSK, 1.4648 baud and Hz (continuous phase), 110.6 s from 1 s of the
 *     even minute;
 *   - JS8 (the JS8 window, as the Modulator of JS8Call): 79 symbols, 8-FSK (continuous phase, no
 *     shaping), the baud = the tone spacing = 12000 / the samples of a symbol at 12 kHz (Normal
 *     1920: 6.25 Hz, Fast 1200: 10 Hz, Turbo 600: 20 Hz, Slow 3840: 3.125 Hz), from the start
 *     delay of the period (Normal 0.5 s, Fast 0.2 s, Turbo 0.1 s, Slow 0.5 s).
 * The level: full scale (as TONE), the power is the Drive. The time: the clock of this computer
 * (it should be synchronized: NTP); the start late by up to a second or two: the waveform from
 * the point of the time (as WSJT-X).
 */
#ifndef FTTX_H
#define FTTX_H

#include <vector>
#include <atomic>
#include <QtGlobal>

class FtTx
{
public:
    FtTx();

    // the waveforms, 48 kHz; f0: the audio frequency of tone 0 (FT8 / FT4: the lower edge of the
    // signal, as the TX frequency of WSJT-X; WSPR: the centre of the 4 tones - 2.2 Hz)
    static std::vector<float> ft8Wave(const int *tones, double f0);
    static std::vector<float> ft4Wave(const int *tones, double f0);
    static std::vector<float> wsprWave(const unsigned char *symbols, double fCentre);
    // JS8: nsps12 the samples of a symbol at 12 kHz; f0 the offset (tone 0, as JS8Call)
    static std::vector<float> js8Wave(const int *tones, double f0, int nsps12);
    static const int RATE = 48000;

    // GUI thread: a new waveform to start at startMs (ms since the epoch, UTC); a waveform
    // replaced at the same startMs goes on from the same point of time (a message changed just
    // after the start)
    void arm(std::vector<float> &&wave, qint64 startMs);
    void start();                   // the TX audio replaced by this (silence until the waveform starts)
    void stop();                    // the microphone again
    bool active() const { return on.load(); }
    bool done() const { return finished.load(); }     // the waveform played to its end
    qint64 startMs() const { return t0.load(); }
    int lengthMs() const;

    // the audio thread: the TX audio (both channels) replaced while active
    void audio(float *l, float *r, unsigned long n);

private:
    std::vector<float> buf[2];
    std::atomic<int> cur;           // the buffer in use (-1: none)
    std::atomic<qint64> t0;
    std::atomic<bool> on, finished, resync;
    long long pos;                  // audio thread: the sample of the waveform now (< 0: before it)
};

#endif // FTTX_H
