#pragma once
#include <vector>
#include <cmath>
#include <algorithm>

namespace chroma
{
// ---------------------------------------------------------------------------------------------
// Linear-phase half-band oversampling (2x / 4x / 8x as a cascade of 2x stages)
//
//  * Kaiser-windowed half-band FIR, ~100 dB stop-band, pass-band ripple of the same order
//    (a half-band filter's ripple is symmetric: H(w) + H(pi - w) = 1), i.e. flat to ~0.0001 dB.
//  * Symmetric FIR => exactly linear phase => no phase distortion, only a pure delay.
//  * Filter length is padded so the total latency is an exact whole number of samples at the
//    host rate (no fractional delay that would comb-filter against a dry/parallel path).
//  * Pass-band edge is 20 kHz (or 0.4535 * fs for fs < 44.1 kHz); everything that could alias
//    back into 0..20 kHz on decimation is removed.
// ---------------------------------------------------------------------------------------------
struct HalfbandDesign
{
    std::vector<float> c;   // c[j] = h[centre + 2j + 1], j = 0..q-1 (symmetric, nonzero odd taps only)
    int q = 0;              // number of tap pairs
    int d = 0;              // centre delay in input-rate samples (filter centre index = 2d)
};

inline double besselI0 (double x)
{
    double sum = 1.0, term = 1.0;
    const double h = 0.25 * x * x;
    for (int k = 1; k < 200; ++k)
    {
        term *= h / ((double) k * (double) k);
        sum += term;
        if (term < 1e-18 * sum) break;
    }
    return sum;
}

// passEdge: pass-band edge as a fraction of the stage INPUT rate (must be < 0.5)
// delayMultiple: d is rounded up to a multiple of this (keeps total latency integral)
inline HalfbandDesign designHalfband (double passEdge, double attenDb, int delayMultiple)
{
    const double pi = 3.14159265358979323846;
    const double dn = 0.5 - passEdge;                                   // transition width / output rate
    const double beta = attenDb > 50.0 ? 0.1102 * (attenDb - 8.7)
                                       : 0.5842 * std::pow (attenDb - 21.0, 0.4) + 0.07886 * (attenDb - 21.0);
    const double order = (attenDb - 7.95) / (2.285 * 2.0 * pi * dn);    // N - 1
    const double halfLen = std::max (3.0, 0.5 * order);                 // window half width, in high-rate samples
    int q = (int) std::ceil ((halfLen + 1.0) / 2.0);
    q = std::max (q, 2);

    HalfbandDesign hb;
    hb.q = q;
    hb.d = ((q + delayMultiple - 1) / delayMultiple) * delayMultiple;   // >= q, so the filter stays causal
    hb.c.resize ((size_t) q);
    const double Mw = 2.0 * q - 1.0, i0b = besselI0 (beta);
    for (int j = 0; j < q; ++j)
    {
        const double o = 2.0 * j + 1.0;
        const double r = o / Mw;
        const double w = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0b;
        hb.c[(size_t) j] = (float) (((j & 1) ? -1.0 : 1.0) / (pi * o) * w);   // sin(pi*o/2) / (pi*o)
    }
    return hb;
}

// Up-sample by 2. Even output = x delayed by d (centre tap, unity gain); odd output = interpolated.
class HalfbandUp
{
public:
    void init (const HalfbandDesign* design)
    {
        hb = design;  R = hb->d + hb->q;
        buf.assign ((size_t) (2 * R), 0.f);  wp = 0;
    }
    void reset() { std::fill (buf.begin(), buf.end(), 0.f); wp = 0; }

    void process (float x, float& even, float& odd)
    {
        wp = (wp == 0 ? R : wp) - 1;
        buf[(size_t) wp] = x;  buf[(size_t) (wp + R)] = x;
        const float* w = &buf[(size_t) wp];                  // w[a] = x[n - a]
        const int d = hb->d, q = hb->q;
        const float* c = hb->c.data();
        float s0 = 0.f, s1 = 0.f;
        int j = 0;
        for (; j + 1 < q; j += 2)
        {
            s0 += c[j]     * (w[d + j]     + w[d - 1 - j]);
            s1 += c[j + 1] * (w[d + j + 1] + w[d - 2 - j]);
        }
        if (j < q) s0 += c[j] * (w[d + j] + w[d - 1 - j]);
        even = w[d];
        odd  = 2.f * (s0 + s1);
    }
private:
    const HalfbandDesign* hb = nullptr;
    int R = 1, wp = 0;
    std::vector<float> buf;
};

// Down-sample by 2 (anti-alias filter + decimate).
class HalfbandDown
{
public:
    void init (const HalfbandDesign* design)
    {
        hb = design;
        Re = hb->d + 1;  Ro = hb->d + hb->q + 1;
        be.assign ((size_t) (2 * Re), 0.f);  bo.assign ((size_t) (2 * Ro), 0.f);  we = wo = 0;
    }
    void reset()
    {
        std::fill (be.begin(), be.end(), 0.f);  std::fill (bo.begin(), bo.end(), 0.f);  we = wo = 0;
    }

    float process (float even, float odd)
    {
        we = (we == 0 ? Re : we) - 1;  be[(size_t) we] = even;  be[(size_t) (we + Re)] = even;
        wo = (wo == 0 ? Ro : wo) - 1;  bo[(size_t) wo] = odd;   bo[(size_t) (wo + Ro)] = odd;
        const float* e = &be[(size_t) we];
        const float* o = &bo[(size_t) wo];
        const int d = hb->d, q = hb->q;
        const float* c = hb->c.data();
        float s0 = 0.f, s1 = 0.f;
        int j = 0;
        for (; j + 1 < q; j += 2)
        {
            s0 += c[j]     * (o[d + j + 1] + o[d - j]);
            s1 += c[j + 1] * (o[d + j + 2] + o[d - j - 1]);
        }
        if (j < q) s0 += c[j] * (o[d + j + 1] + o[d - j]);
        return 0.5f * e[d] + (s0 + s1);
    }
private:
    const HalfbandDesign* hb = nullptr;
    int Re = 1, Ro = 1, we = 0, wo = 0;
    std::vector<float> be, bo;
};

// Knee widths, as a fraction of the ceiling. Both are constants: the knee has nothing to do with the
// Ceiling knob (it used to widen as the ceiling was lowered, which dragged the onset of distortion further
// and further below the ceiling). The only control over the knee is the "Soft to Hard" slider, which
// blends between the two curves below.
//   Soft: 0.20  -> a rounded dome instead of a flat top: exactly unity gain (zero processing) up to 80 % of the ceiling
//                  (about -1.9 dB), then a gradual roll-off that approaches the ceiling only asymptotically. Only peaks near
//                  or over the ceiling are touched. Raise it (up to 1.0) for a rounder, more audible dome that starts lower.
//   Hard: 0     -> a true brick wall: y = x up to the ceiling, then exactly the ceiling. No knee at all.
inline constexpr float softKneeFrac = 0.20f;
inline constexpr float hardKneeFrac = 0.0f;

// One transfer curve (per sample, one sqrt, C2-continuous, strictly monotonic, never exceeds the ceiling):
//   |x| <= k : y = x                                  (exactly unity gain, zero distortion below the knee)
//   |x| >  k : y = k + W * t / sqrt (1 + t^2),        t = (|x| - k) / W,   W = ceiling - k
//
// Why this shape ("sweet" rather than harsh):
//   * Below the ceiling it leaves the straight line with a cubic contact (value, slope AND curvature
//     are continuous at k), so the first thing you hear when you push is a gentle, low-order (3rd, 5th)
//     colouration that falls off quickly with harmonic order, not a spray of high-order harmonics.
//   * Above the knee it does not slam into a flat ceiling. It approaches the ceiling asymptotically
//     (1 - 1/(2 t^2)), so heavy pushing rounds the peaks progressively instead of squaring them off.
//     Squared-off peaks are what make a clipper sound harsh and generate the most aliasing.
//   * The ceiling is still a hard guarantee: the output is always strictly below it.
struct KneeCurve
{
    float k = .88f, W = .12f, invW = 1.f / .12f;
};

// Soft -> Hard blend. The output is the weighted sum of the Soft curve and the Hard curve:
//      y = (1 - h) * soft (x)  +  h * hard (x),        h = slider position, 0 (Soft) .. 1 (Hard)
// At h = 0.5 the two curves contribute exactly 1:1. Because the weights sum to 1 and both curves are
// unity gain below the Soft knee (and each is bounded by the ceiling), the blend is also unity gain there,
// monotonic and bounded by the ceiling for every h.
struct ClipParams
{
    float ceil = 1.f, hard = 0.f;       // hard: weight of the Hard curve (the Soft curve gets 1 - hard)
    KneeCurve soft, hardC;
};

inline KneeCurve makeKnee (float ceil, float knee)
{
    KneeCurve c;
    c.k = ceil * (1.f - knee);
    if (knee <= 0.f) { c.k = ceil;  c.W = 0.f;  c.invW = 0.f;  return c; }   // W = 0 marks the exact brick wall
    c.W = std::max (ceil - c.k, 1e-9f);              // headroom between knee start and ceiling = knee scale
    c.invW = 1.f / c.W;
    return c;
}

// ceil (linear gain) and hard (0..1) are smoothed per sample by the processor, so this is also called per
// sample while either one moves.
inline ClipParams makeClipParams (float ceil, float hard)
{
    ClipParams p;
    p.ceil = ceil;
    p.hard = hard < 0.f ? 0.f : (hard > 1.f ? 1.f : hard);
    p.soft  = makeKnee (ceil, softKneeFrac);
    p.hardC = makeKnee (ceil, hardKneeFrac);
    return p;
}

// magnitude of one curve for a >= 0
inline float clipMag (float a, const KneeCurve& c, float ceil)
{
    if (a <= c.k) return a;
    if (c.W <= 0.f) return ceil;                              // brick wall
    const float t = std::min ((a - c.k) * c.invW, 1.0e6f);    // bounded so t*t can never overflow
    const float y = c.k + c.W * t / std::sqrt (1.f + t * t);
    return y > ceil ? ceil : y;
}

inline float clip (float s, const ClipParams& p)
{
    const float a = std::fabs (s);
    float y = 0.f;
    if (p.hard < 1.f) y += (1.f - p.hard) * clipMag (a, p.soft,  p.ceil);
    if (p.hard > 0.f) y += p.hard         * clipMag (a, p.hardC, p.ceil);
    return s < 0.f ? -y : y;
}

// ---------------------------------------------------------------------------------------------
// First-order antiderivative anti-aliasing (ADAA)
//
// Instead of evaluating the clip curve at each sample, the average of the curve between two
// successive samples is used: y[n] = (F(x[n]) - F(x[n-1])) / (x[n] - x[n-1]), F = integral of clip.
// This suppresses the aliasing of the corner/knee on top of the oversampling. The catch: in the
// linear region it reduces to (x[n] + x[n-1]) / 2, a half-sample delay with a cos(w/2) droop
// (-2.4 dB at 20 kHz when running at 2x). To keep the response flat, the signal goes through
// AdaaPreFilter first, a short linear-phase FIR designed so that
//      (1 + z^-1)/2  *  AdaaPreFilter  =  pure delay  (flat to 20 kHz, exactly linear phase).
// Total extra delay is M/2 = 16 oversampled samples = 16/L host samples (a whole number for L = 2, 4, 8).
// ---------------------------------------------------------------------------------------------

// Antiderivative of one curve for x >= 0 (the integral of the odd curve is even, so use |x|).
//   a <= k : a^2 / 2
//   a >  k : k^2 / 2 + k (a - k) + W^2 (sqrt (1 + t^2) - 1)        (t = (a - k) / W)
// sqrt (1 + t^2) - 1 is evaluated as t^2 / (sqrt (1 + t^2) + 1) so that nothing cancels for small t.
inline double kneeIntegral (double a, const KneeCurve& c)
{
    const double k = c.k, W = c.W;
    if (a <= k) return 0.5 * a * a;
    if (W <= 0.0) return 0.5 * k * k + k * (a - k);          // brick wall: constant output k above the knee
    const double t = (a - k) / W, s = std::sqrt (1.0 + t * t);
    return 0.5 * k * k + k * (a - k) + W * W * (t * t / (s + 1.0));
}

// Integration is linear, so the antiderivative of the Soft/Hard blend is the same blend of the two
// antiderivatives. ADAA therefore runs ONCE on the blended curve, at every slider position, and gives
// exactly the same result as running two separate ADAA clippers and mixing them 1:1 at 50 %.
inline double clipIntegral (double a, const ClipParams& p)
{
    const double h = (double) p.hard;
    double f = 0.0;
    if (h < 1.0) f += (1.0 - h) * kneeIntegral (a, p.soft);
    if (h > 0.0) f += h         * kneeIntegral (a, p.hardC);
    return f;
}

inline double kneeValue (double a, const KneeCurve& c, double ceil)
{
    if (a <= c.k) return a;
    if (c.W <= 0.f) return ceil;                              // brick wall
    const double t = (a - c.k) / c.W;
    const double y = c.k + c.W * t / std::sqrt (1.0 + t * t);
    return y > ceil ? ceil : y;
}

inline double clipD (double x, const ClipParams& p)
{
    const double a = std::fabs (x), h = (double) p.hard, ceil = (double) p.ceil;
    double y = 0.0;
    if (h < 1.0) y += (1.0 - h) * kneeValue (a, p.soft,  ceil);
    if (h > 0.0) y += h         * kneeValue (a, p.hardC, ceil);
    return x < 0.0 ? -y : y;
}

class AdaaPreFilter
{
public:
    static constexpr int M = 32;                 // even length => half-sample centre, type II FIR
    static constexpr int K = M / 2;              // tap pairs

    // omegaP: pass-band edge in rad/sample at the oversampled rate
    void design (double omegaP)
    {
        // amplitude A(w) = sum_k b_k cos((k + 1/2) w)  ->  least-squares fit of 1 / cos(w/2) on [0, omegaP*1.08].
        // Above that only content that the oversampler already removed (> 100 dB down) can exist, and the
        // cos((k+1/2)w) basis forces A(pi) = 0, so the response stays bounded.
        double A[K][K + 1] = {};
        const double wEdge = omegaP * 1.08;
        const int nP = 400;
        for (int i = 0; i <= nP; ++i) { const double w = wEdge * i / nP; accumulate (A, w, 1.0 / std::cos (0.5 * w), 1.0); }
        for (int i = 0; i < K; ++i) A[i][i] += 1e-13;                          // tiny ridge for stability
        for (int c = 0; c < K; ++c)                                              // Gauss-Jordan with pivoting
        {
            int piv = c;
            for (int r = c + 1; r < K; ++r) if (std::fabs (A[r][c]) > std::fabs (A[piv][c])) piv = r;
            for (int j = 0; j <= K; ++j) std::swap (A[c][j], A[piv][j]);
            const double d = A[c][c];
            for (int j = c; j <= K; ++j) A[c][j] /= d;
            for (int r = 0; r < K; ++r)
                if (r != c) { const double f = A[r][c]; for (int j = c; j <= K; ++j) A[r][j] -= f * A[c][j]; }
        }
        for (int k = 0; k < K; ++k) taps[K - 1 - k] = (float) (0.5 * A[k][K]);  // h[age i] = b_k / 2, i = K-1-k
        reset();
    }

    void reset() { std::fill (std::begin (buf), std::end (buf), 0.f); wp = 0; }

    float process (float x)
    {
        wp = (wp == 0 ? M : wp) - 1;
        buf[wp] = x;  buf[wp + M] = x;
        const float* w = &buf[wp];
        float s0 = 0.f, s1 = 0.f;
        for (int i = 0; i < M / 2; i += 2)
        {
            s0 += taps[i]     * (w[i]     + w[M - 1 - i]);
            s1 += taps[i + 1] * (w[i + 1] + w[M - 2 - i]);
        }
        return s0 + s1;
    }

private:
    // adds one weighted frequency sample to the least-squares normal equations
    static void accumulate (double (&A)[K][K + 1], double w, double target, double weight)
    {
        double phi[K];
        for (int k = 0; k < K; ++k) phi[k] = std::cos ((k + 0.5) * w);
        for (int i = 0; i < K; ++i)
        {
            for (int j = 0; j < K; ++j) A[i][j] += weight * phi[i] * phi[j];
            A[i][K] += weight * phi[i] * target;
        }
    }

    float taps[M / 2] {};
    float buf[2 * M] {};
    int wp = 0;
};

// One instance per channel and oversampling factor.
class AdaaClipper
{
public:
    void design (double omegaP) { pre.design (omegaP); prev = 0.0; }
    void reset()                { pre.reset(); prev = 0.0; }

    float process (float xin, const ClipParams& p)
    {
        double x = (double) pre.process (xin);
        // Guard against NaN / Inf / absurd values from a misbehaving host. Anything beyond +-1e4 is far
        // into the flat part of the curve anyway, so this changes nothing for real audio.
        x = (x > 1.0e4) ? 1.0e4 : (x < -1.0e4 ? -1.0e4 : (x == x ? x : 0.0));
        const double dx = x - prev;
        double y;
        if (std::fabs (dx) > 1e-7)
            y = (clipIntegral (std::fabs (x), p) - clipIntegral (std::fabs (prev), p)) / dx;
        else
            y = clipD (0.5 * (x + prev), p);        // ill-conditioned: fall back to the midpoint value
        prev = x;
        y = y > (double) p.ceil ? (double) p.ceil : (y < -(double) p.ceil ? -(double) p.ceil : y);   // belt and braces
        return (float) y;
    }

    static constexpr int delayOversampled = AdaaPreFilter::M / 2;

private:
    AdaaPreFilter pre;
    double prev = 0.0;
};
// Cascade of half-band stages: factor = 2, 4 or 8. One instance per channel.
class Oversampler
{
public:
    Oversampler (int factor, double sampleRate) : L (factor)
    {
        for (int s = 0; (1 << (s + 1)) <= L; ++s) ++stages;
        // Pass-band edge at the host rate: 20 kHz, or 0.4535 fs on low sample rates
        const double fp = std::min (20000.0, 0.4535 * sampleRate);
        for (int s = 0; s < stages; ++s)
        {
            const double fi = sampleRate * (double) (1 << s);       // stage input rate
            const double edge = s == 0 ? fp : sampleRate - fp;     // later stages only need to keep what stage 0 let through
            design[s] = designHalfband (edge / fi, 100.0, s >= 2 ? (1 << (s - 1)) : 1);
            up[s].init (&design[s]);  down[s].init (&design[s]);
            latency_ += (2 * design[s].d) >> s;                      // exact: d is a multiple of 2^(s-1)
        }
        adaa.design (2.0 * 3.14159265358979323846 * fp / (sampleRate * (double) L));
        latency_ += AdaaClipper::delayOversampled / L;               // 16 / L: whole samples for L = 2, 4, 8
    }

    int getLatency() const { return latency_; }   // whole samples at the host rate
    int getFactor()  const { return L; }

    void reset()
    {
        for (int s = 0; s < stages; ++s) { up[s].reset(); down[s].reset(); }
        std::fill (std::begin (a), std::end (a), 0.f);
        std::fill (std::begin (b), std::end (b), 0.f);
        adaa.reset();
    }

    // Complete clip path for one host-rate sample: oversample -> ADAA clip -> filter and decimate.
    float process (float x, const ClipParams& p)
    {
        float* g = upsample (x);
        for (int i = 0; i < L; ++i) g[i] = adaa.process (g[i], p);
        return downsample (g);
    }

    // returns L oversampled samples (valid until the next call)
    float* upsample (float x)
    {
        a[0] = x;
        float* in = a; float* out = b;
        for (int s = 0; s < stages; ++s)
        {
            const int n = 1 << s;
            for (int i = 0; i < n; ++i) up[s].process (in[i], out[2 * i], out[2 * i + 1]);
            std::swap (in, out);
        }
        return in;
    }

    // g: L (processed) oversampled samples -> 1 output sample
    float downsample (float* g)
    {
        float* in = g; float* out = (g == a) ? b : a;
        for (int s = stages - 1; s >= 0; --s)
        {
            const int n = 1 << s;
            for (int i = 0; i < n; ++i) out[i] = down[s].process (in[2 * i], in[2 * i + 1]);
            std::swap (in, out);
        }
        return in[0];
    }

private:
    int L, stages = 0, latency_ = 0;
    HalfbandDesign design[3];
    HalfbandUp up[3];
    HalfbandDown down[3];
    float a[8] {}, b[8] {};
    AdaaClipper adaa;
};

} // namespace chroma
