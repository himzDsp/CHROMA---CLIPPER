#pragma once
#include <cmath>
#include <algorithm>
#include <vector>
#include <juce_audio_basics/juce_audio_basics.h>

namespace chroma
{
// ---------------------------------------------------------------------------------------------
// Clip-band selector (replaces the old pre-clip high-pass / low-pass).
//
//   band = LP( HP( x ) )     the only part the clipper sees (4th-order Linkwitz-Riley slopes, 24 dB/oct)
//   rest = x - band          everything outside the band, untouched
//   out  = clip( band ) + rest
//
//  * rest is the exact complement of band, so with the clipper idle out == x for ANY cut-off setting:
//    no cancellation, no comb filtering, no level or phase change anywhere in the spectrum.
//  * IIR / causal: no linear-phase pre-ringing, and the splitter adds no latency of its own.
//  * Zero-delay-feedback (TPT) state-variable filters: stable and click-free while a cut-off is automated;
//    cut-offs and the on/off state are smoothed per sample.
//  * Only the band goes through the oversampler + ADAA clipper, so the bypassed spectrum can't alias at all.
// ---------------------------------------------------------------------------------------------
class BandSplitter
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        // Slow glides: a cut-off move is a gentle sweep (80 ms, in the log-frequency domain), and bringing a
        // filter in or out is a 200 ms crossfade shaped as a smoothstep (zero slope at both ends), so the
        // clipper never sees the band change abruptly.
        hpFreq.reset (sr, freqGlideSec);  lpFreq.reset (sr, freqGlideSec);
        hpMix.reset (sr, mixGlideSec);    lpMix.reset (sr, mixGlideSec);
        hpFreq.setCurrentAndTargetValue (20.f);  lpFreq.setCurrentAndTargetValue (20000.f);
        hpMix.setCurrentAndTargetValue (0.f);    lpMix.setCurrentAndTargetValue (0.f);
        reset();
        updateCoefs();
    }

    void reset()
    {
        for (auto& ch : st) for (auto& s : ch) s = {};
    }

    // hpHz <= 20.5 => lower boundary off, lpHz >= 19990 => upper boundary off
    void setCutoffs (float hpHz, float lpHz, bool snap = false)
    {
        const float maxF = (float) (0.45 * sr);
        const float hp = std::min (std::max (hpHz, 20.f), maxF), lp = std::min (std::max (lpHz, 20.f), maxF);
        const float hm = hpHz > 20.5f ? 1.f : 0.f, lm = lpHz < 19990.f ? 1.f : 0.f;
        if (snap)
        {
            hpFreq.setCurrentAndTargetValue (hp);  lpFreq.setCurrentAndTargetValue (lp);
            hpMix.setCurrentAndTargetValue (hm);   lpMix.setCurrentAndTargetValue (lm);
            hpM = hm;  lpM = lm;
            updateCoefs();
        }
        else
        {
            hpFreq.setTargetValue (hp);  lpFreq.setTargetValue (lp);
            hpMix.setTargetValue (hm);   lpMix.setTargetValue (lm);
        }
    }

    // once per sample (all channels), before process()
    void tick() noexcept
    {
        if (hpFreq.isSmoothing() || lpFreq.isSmoothing())
        {
            hpFreq.getNextValue();  lpFreq.getNextValue();
            updateCoefs();
        }
        hpM = ease (hpMix.isSmoothing() ? hpMix.getNextValue() : hpMix.getTargetValue());
        lpM = ease (lpMix.isSmoothing() ? lpMix.getNextValue() : lpMix.getTargetValue());
    }

    void process (int ch, float x, float& band, float& rest) noexcept
    {
        auto& s = st[(size_t) ch];
        const float hp1 = high (s[0], hpC, x);
        const float hp2 = high (s[1], hpC, hp1);
        const float a   = x + hpM * (hp2 - x);                 // lower boundary (or x when it is off)
        const float lp1 = low (s[2], lpC, a);
        const float lp2 = low (s[3], lpC, lp1);
        band = a + lpM * (lp2 - a);                             // upper boundary (or a when it is off)
        rest = x - band;
    }

private:
    struct Coef { float a1 = 0.f, a2 = 0.f, a3 = 0.f; };
    struct State { float ic1 = 0.f, ic2 = 0.f; };

    static constexpr double freqGlideSec = 0.080, mixGlideSec = 0.200;
    static float ease (float t) noexcept { t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);  return t * t * (3.f - 2.f * t); }   // smoothstep

    static constexpr float k = 1.41421356f;                     // 1 / Q, Q = 1/sqrt(2): Butterworth, squared = Linkwitz-Riley

    static Coef makeCoef (double fc, double sr)
    {
        const double g = std::tan (3.14159265358979323846 * fc / sr);
        Coef c;
        const double a1 = 1.0 / (1.0 + g * (g + (double) k));
        c.a1 = (float) a1;  c.a2 = (float) (g * a1);  c.a3 = (float) (g * g * a1);
        return c;
    }
    void updateCoefs()
    {
        hpC = makeCoef ((double) hpFreq.getCurrentValue(), sr);
        lpC = makeCoef ((double) lpFreq.getCurrentValue(), sr);
    }

    static float high (State& s, const Coef& c, float v0) noexcept
    {
        const float v3 = v0 - s.ic2;
        const float v1 = c.a1 * s.ic1 + c.a2 * v3;
        const float v2 = s.ic2 + c.a2 * s.ic1 + c.a3 * v3;
        s.ic1 = 2.f * v1 - s.ic1;  s.ic2 = 2.f * v2 - s.ic2;
        return v0 - k * v1 - v2;
    }
    static float low (State& s, const Coef& c, float v0) noexcept
    {
        const float v3 = v0 - s.ic2;
        const float v1 = c.a1 * s.ic1 + c.a2 * v3;
        const float v2 = s.ic2 + c.a2 * s.ic1 + c.a3 * v3;
        s.ic1 = 2.f * v1 - s.ic1;  s.ic2 = 2.f * v2 - s.ic2;
        return v2;
    }

    double sr = 44100.0;
    Coef hpC, lpC;
    float hpM = 0.f, lpM = 0.f;
    State st[2][4];
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> hpFreq, lpFreq;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> hpMix, lpMix;
};

// Plain integer delay: keeps the untouched spectrum time-aligned with the clipped band (oversampler latency).
class DelayLine
{
public:
    void prepare (int maxDelay)
    {
        int n = 16;  while (n < maxDelay + 2) n <<= 1;
        buf.assign ((size_t) n, 0.f);  mask = n - 1;  w = 0;
    }
    void reset() { std::fill (buf.begin(), buf.end(), 0.f);  w = 0; }
    float process (float x, int d) noexcept
    {
        buf[(size_t) w] = x;
        const float y = buf[(size_t) ((w - d) & mask)];
        w = (w + 1) & mask;
        return y;
    }
private:
    std::vector<float> buf;
    int mask = 15, w = 0;
};
} // namespace chroma
