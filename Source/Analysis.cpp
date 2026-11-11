#include "Analysis.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>

namespace triad
{
namespace
{
    constexpr double kRate = 22050.0;
    constexpr double kPi = 3.14159265358979323846;

    std::vector<float> toMono22k (const juce::AudioBuffer<float>& in, double sr)
    {
        const int n = in.getNumSamples();
        const int nch = juce::jmax (1, in.getNumChannels());
        std::vector<float> mono ((size_t) n + 16, 0.0f);

        for (int c = 0; c < in.getNumChannels(); ++c)
        {
            const auto* p = in.getReadPointer (c);
            for (int i = 0; i < n; ++i)
                mono[(size_t) i] += p[i] / (float) nch;
        }

        if (std::abs (sr - kRate) < 1.0)
        {
            mono.resize ((size_t) n);
            return mono;
        }

        const double ratio = sr / kRate;
        const int outN = (int) std::floor ((double) n / ratio) - 2;
        if (outN <= 0)
            return {};

        std::vector<float> out ((size_t) outN);
        juce::LagrangeInterpolator interp;
        interp.process (ratio, mono.data(), out.data(), outN);
        return out;
    }

    template <typename Fn>
    void stft (const std::vector<float>& x, int order, int hop, Fn&& fn)
    {
        const int N = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> window ((size_t) N), buf ((size_t) N * 2);

        for (int i = 0; i < N; ++i)
            window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * (float) kPi * (float) i / (float) N);

        const int total = (int) x.size();
        const int frames = juce::jmax (1, (total - N) / hop + 1);

        for (int f = 0; f < frames; ++f)
        {
            std::fill (buf.begin(), buf.end(), 0.0f);
            const int s = f * hop;
            for (int i = 0; i < N; ++i)
            {
                const int idx = s + i;
                if (idx < total)
                    buf[(size_t) i] = x[(size_t) idx] * window[(size_t) i];
            }
            fft.performFrequencyOnlyForwardTransform (buf.data());
            fn (f, (const float*) buf.data());
        }
    }

    double pearson (const double* a, const double* b, int n)
    {
        double ma = 0, mb = 0;
        for (int i = 0; i < n; ++i) { ma += a[i]; mb += b[i]; }
        ma /= n; mb /= n;
        double num = 0, da = 0, db = 0;
        for (int i = 0; i < n; ++i)
        {
            num += (a[i] - ma) * (b[i] - mb);
            da += (a[i] - ma) * (a[i] - ma);
            db += (b[i] - mb) * (b[i] - mb);
        }
        return (da > 0 && db > 0) ? num / std::sqrt (da * db) : 0.0;
    }
}

const char* noteName (int pc)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names[((pc % 12) + 12) % 12];
}

juce::String Chord::name() const { return juce::String (noteName (root)) + (minor ? "m" : ""); }

std::array<int, 3> Chord::pitchClasses() const
{
    return { root % 12, (root + (minor ? 3 : 4)) % 12, (root + 7) % 12 };
}

std::array<int, 3> chordMidiNotes (const Chord& c)
{
    const int base = 60 + c.root - (c.root >= 6 ? 12 : 0);
    return { base, base + (c.minor ? 3 : 4), base + 7 };
}

int bassMidiNote (const Chord& c) { return 36 + c.root; }

bool computeFeatures (const juce::AudioBuffer<float>& audio, double sampleRate,
                      Features& out, const std::function<bool()>& shouldExit)
{
    auto x = toMono22k (audio, sampleRate);
    if (x.size() < 4096)
        return false;

    out = {};
    out.durationSeconds = (double) x.size() / kRate;

    const int order = 13, N = 1 << order;
    const double binHz = kRate / N;

    // ---- tuning estimate (samples are often not exactly A=440) ----
    {
        const int kA = (int) std::ceil (110.0 / binHz), kB = (int) std::floor (1600.0 / binHz);
        double sx = 0.0, sy = 0.0;
        stft (x, order, 4096, [&] (int, const float* mag)
        {
            float mx = 1e-9f;
            for (int k = kA; k <= kB; ++k) mx = std::max (mx, mag[k]);
            for (int k = kA + 1; k < kB; ++k)
            {
                if (mag[k] > mag[k - 1] && mag[k] >= mag[k + 1] && mag[k] > 0.1f * mx)
                {
                    const double a = std::log (mag[k - 1] + 1e-9), b = std::log (mag[k] + 1e-9), c = std::log (mag[k + 1] + 1e-9);
                    const double den = a - 2.0 * b + c;
                    const double delta = std::abs (den) > 1e-12 ? juce::jlimit (-0.5, 0.5, 0.5 * (a - c) / den) : 0.0;
                    const double fr = ((double) k + delta) * binHz;
                    const double midi = 69.0 + 12.0 * std::log2 (fr / 440.0);
                    const double dev = midi - std::round (midi);
                    const double w = mag[k] / mx;
                    sx += w * std::cos (2.0 * kPi * dev);
                    sy += w * std::sin (2.0 * kPi * dev);
                }
            }
        });
        if (sx * sx + sy * sy > 1e-9)
            out.tuningSemitones = std::atan2 (sy, sx) / (2.0 * kPi);
    }

    if (shouldExit()) return false;

    // ---- chroma (8192 window, hop 2048) ----
    {
        const int hop = 2048;
        const int kLo = (int) std::ceil (55.0 / binHz);
        const int kHi = (int) std::floor (2000.0 / binHz);
        const int chromaLo = (int) std::ceil (70.0 / binHz);
        const int bassHi = (int) std::floor (220.0 / binHz);

        std::vector<int> pcOf ((size_t) kHi + 1, 0);
        std::vector<float> wOf ((size_t) kHi + 1, 0.0f);
        for (int k = kLo; k <= kHi; ++k)
        {
            const double fr = k * binHz;
            const int midi = (int) std::lround (69.0 + 12.0 * std::log2 (fr / 440.0) - out.tuningSemitones);
            pcOf[(size_t) k] = ((midi % 12) + 12) % 12;
            wOf[(size_t) k] = (float) (1.0 / (1.0 + fr / 1000.0));
        }

        out.frameSeconds = (double) hop / kRate;
        out.firstFrameCentre = (double) (N / 2) / kRate;

        stft (x, order, hop, [&] (int, const float* mag)
        {
            float mx = 1e-9f;
            for (int k = kLo; k <= kHi; ++k)
                mx = std::max (mx, mag[k]);

            // FIX: Use explicit size 12 for std::array to satisfy VS 2026 compiler
            std::array<float, 12> c {}, b {};
            for (int k = kLo; k <= kHi; ++k)
            {
                const float v = std::log1p (10.0f * mag[k] / mx);
                if (k >= chromaLo) c[(size_t) pcOf[(size_t) k]] += v * wOf[(size_t) k];
                if (k <= bassHi)   b[(size_t) pcOf[(size_t) k]] += v;
            }

            float l1 = 0.0f, l2 = 0.0f;
            for (auto v : c) { l1 += v; l2 += v * v; }
            l2 = std::sqrt (l2);
            if (l2 > 1e-9f)
                for (auto& v : c) v /= l2;

            float en = 0.0f;
            for (int k = kLo; k <= kHi; ++k) en += mag[k];
            out.energy.push_back (en);

            // peaky chroma = tonal frame, flat chroma = drums/noise
            const float peaky = l1 > 1e-9f ? l2 / l1 : 0.0f;
            out.weight.push_back (juce::jlimit (0.15f, 1.0f, (peaky - 0.28f) / 0.15f));
            out.chroma.push_back (c);
            out.bass.push_back (b);
        });
    }

    // length of the real music (samples often end with a quiet reverb tail or silence)
    {
        std::vector<float> sorted = out.energy;
        std::sort (sorted.begin(), sorted.end());
        const float p90 = sorted.empty() ? 0.0f : sorted[(size_t) ((double) (sorted.size() - 1) * 0.9)];
        int last = (int) out.energy.size() - 1;
        while (last > 0 && out.energy[(size_t) last] < 0.15f * p90)
            --last;
        out.activeSeconds = juce::jmin (out.durationSeconds,
                                        out.firstFrameCentre + (double) last * out.frameSeconds + 0.2);
    }

    if (shouldExit()) return false;

    // ---- key (Krumhansl-Schmuckler) ----
    {
        double total[12] = {};
        for (size_t i = 0; i < out.chroma.size(); ++i)
            for (size_t k = 0; k < 12; ++k)
                total[k] += out.chroma[i][k] * out.weight[i];

        static const double maj[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
        static const double mnr[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

        double best = -2.0;
        for (int t = 0; t < 12; ++t)
        {
            double pm[12], pn[12];
            for (int i = 0; i < 12; ++i)
            {
                pm[i] = maj[((i - t) % 12 + 12) % 12];
                pn[i] = mnr[((i - t) % 12 + 12) % 12];
            }
            const double cm = pearson (total, pm, 12);
            const double cn = pearson (total, pn, 12);
            out.keyCorr[(size_t) t] = cm;
            out.keyCorr[(size_t) t + 12] = cn;
            if (cm > best) { best = cm; out.keyRoot = t; out.keyMinor = false; }
            if (cn > best) { best = cn; out.keyRoot = t; out.keyMinor = true; }
        }
    }

    // ---- onset envelope (1024 window, hop 256) ----
    {
        const int oOrder = 10, oN = 1 << oOrder, oHop = 256;
        out.onsetRate = kRate / oHop;
        std::vector<float> prev ((size_t) oN / 2, 0.0f);
        const int kTop = 220;

        stft (x, oOrder, oHop, [&] (int f, const float* mag)
        {
            float flux = 0.0f;
            for (int k = 2; k < kTop; ++k)
            {
                const float amp = mag[k] / ((float) oN * 0.25f);
                const float l = std::log1p (100.0f * amp);
                if (f > 0) flux += std::max (0.0f, l - prev[(size_t) k]);
                prev[(size_t) k] = l;
            }
            out.onset.push_back (flux);
        });
    }

    return ! shouldExit();
}

double detectBpm (const Features& f)
{
    const int n = (int) f.onset.size();
    if (n < 128 || f.onsetRate <= 0.0)
        return 120.0;

    std::vector<double> e (f.onset.begin(), f.onset.end());
    double mean = 0;
    for (auto v : e) mean += v;
    mean /= n;
    double r0 = 0;
    for (auto& v : e) { v -= mean; r0 += v * v; }
    if (r0 < 1e-9)
        return 120.0;

    auto ac = [&] (int lag) -> double
    {
        if (lag <= 0 || lag >= n) return 0.0;
        double s = 0;
        for (int i = 0; i < n - lag; ++i) s += e[(size_t) i] * e[(size_t) (i + lag)];
        return (s / (n - lag)) / (r0 / n);
    };

    const double rate = f.onsetRate;
    const int lagMin = (int) std::floor (rate * 60.0 / 175.0);
    const int lagMax = (int) std::ceil (rate * 60.0 / 70.0);

    std::vector<double> score ((size_t) lagMax + 2, 0.0);
    int bestLag = lagMin;
    double bestScore = -1e9;
    for (int lag = lagMin; lag <= lagMax; ++lag)
    {
        const double bpm = 60.0 * rate / lag;
        const double z = std::log2 (bpm / 115.0) / 0.6;
        const double prior = std::exp (-0.5 * z * z);
        const double s = (ac (lag) + 0.5 * ac (2 * lag) + 0.25 * ac (4 * lag)) * prior;
        score[(size_t) lag] = s;
        if (s > bestScore) { bestScore = s; bestLag = lag; }
    }

    double lag = bestLag;
    if (bestLag > lagMin && bestLag < lagMax)
    {
        const double a = score[(size_t) bestLag - 1], b = score[(size_t) bestLag], c = score[(size_t) bestLag + 1];
        const double den = a - 2.0 * b + c;
        if (std::abs (den) > 1e-12)
            lag += juce::jlimit (-0.5, 0.5, 0.5 * (a - c) / den);
    }

    double bpm = 60.0 * rate / lag;

    // ---- fine tune: slide a beat comb over the onset envelope (+-3 %) ----
    {
        const double envSeconds = (double) n / rate;
        auto onsetAt = [&] (double t) -> double
        {
            const int i = (int) std::lround (t * rate);
            if (i < 1 || i >= n - 1) return 0.0;
            return std::max (0.0, std::max ({ e[(size_t) i - 1], e[(size_t) i], e[(size_t) i + 1] }));
        };
        auto combScore = [&] (double b)
        {
            const double per = 60.0 / b;
            double best = 0.0;
            for (int ph = 0; ph < 16; ++ph)
            {
                double s = 0.0;
                int cnt = 0;
                for (double t = ph * per / 16.0; t < envSeconds; t += per) { s += onsetAt (t); ++cnt; }
                if (cnt > 0) best = std::max (best, s / cnt);
            }
            return best;
        };

        const double base = combScore (bpm);
        double bestB = bpm, bestS = base;
        for (double r = 0.97; r <= 1.03; r += 0.0005)
        {
            const double s = combScore (bpm * r);
            if (s > bestS) { bestS = s; bestB = bpm * r; }
        }
        if (bestS > base * 1.05)
            bpm = bestB;
    }

    // Loops are cut to whole bars (2/4/8/16), maybe with a short reverb/ring-out tail.
    // Use that to (a) fix tempo-ratio mistakes (x2, /2, x1.5, x2/3 - e.g. a 153 BPM loop that
    // locked on 102) and (b) snap to the exact tempo when the loop is cut tight.
    {
        const double lens[2] = { f.activeSeconds > 1.0 ? f.activeSeconds : f.durationSeconds, f.durationSeconds };
        const int barOptions[4] = { 2, 4, 8, 16 };
        const double ratios[5]    = { 1.0, 2.0, 0.5, 1.5, 2.0 / 3.0 };
        const double penalties[5] = { 0.0, 0.012, 0.012, 0.02, 0.02 };

        double bestCost = 1e9, bestBpm = bpm;
        for (int ri = 0; ri < 5; ++ri)
        {
            const double b = bpm * ratios[ri];
            if (b < 68.0 || b > 185.0) continue;
            for (double T : lens)
            {
                if (T < 1.0) continue;
                for (int n : barOptions)
                {
                    const double Ln = n * 240.0 / b;
                    const double excess = T - Ln;                       // ring-out after the last bar
                    if (excess < -0.04 * Ln || excess > 2.2 || excess > 0.5 * Ln) continue;
                    const double cost = penalties[ri] + ((n == 4 || n == 8) ? 0.0 : 0.008)
                                        + 0.03 * std::max (0.0, excess) + 0.5 * std::max (0.0, -excess) / Ln;
                    if (cost < bestCost)
                    {
                        bestCost = cost;
                        bestBpm = (std::abs (excess) / Ln < 0.02) ? n * 240.0 / T : b;
                    }
                }
            }
        }
        if (bestCost < 1e8)
            bpm = bestBpm;
    }

    return bpm;
}

Analysis buildAnalysis (const Features& f, double bpm)
{
    Analysis a;
    a.bpm = bpm;
    a.durationSeconds = f.durationSeconds;
    a.keyRoot = f.keyRoot;
    a.keyMinor = f.keyMinor;

    const double barSec = 240.0 / bpm;
    const double est = (f.activeSeconds > 0.0 ? f.activeSeconds : f.durationSeconds) / barSec;
    a.bars = est < 6.0 ? 4 : 8;
    const int bars = a.bars;
    
    // FIX: Explicitly define Vec as array of size 12
    using Vec = std::array<float, 12>;

    double allWeight = 0.0;
    for (auto w : f.weight) allWeight += w;
    if (allWeight < 1e-9) allWeight = 1.0;

    // Bar windows starting at `off`; returns how well frames agree with their own bar's chroma.
    auto evaluate = [&] (double off, std::vector<Vec>& C, std::vector<Vec>& B, std::vector<double>& ws) -> double
    {
        C.assign ((size_t) bars, Vec {});
        B.assign ((size_t) bars, Vec {});
        ws.assign ((size_t) bars, 0.0);
        std::vector<int> barOf (f.chroma.size(), -1);
        std::vector<double> wOf (f.chroma.size(), 0.0);

        for (size_t i = 0; i < f.chroma.size(); ++i)
        {
            const double t = f.firstFrameCentre + (double) i * f.frameSeconds;
            if (t < off) continue;
            const double pos = (t - off) / barSec;
            const int b = (int) std::floor (pos);
            if (b >= bars) break;
            const double frac = pos - b;
            const double shape = 0.4 + 0.6 * std::min (1.0, std::min (frac, 1.0 - frac) / 0.15);
            const double w = f.weight[i] * shape;
            barOf[i] = b;
            wOf[i] = w;
            for (size_t k = 0; k < 12; ++k)
            {
                C[(size_t) b][k] += (float) (w * f.chroma[i][k]);
                B[(size_t) b][k] += (float) (w * f.bass[i][k]);
            }
            ws[(size_t) b] += w;
        }

        std::vector<Vec> unit = C;
        for (auto& v : unit)
        {
            float nrm = 0;
            for (auto z : v) nrm += z * z;
            nrm = std::sqrt (nrm);
            if (nrm > 1e-9f) for (auto& z : v) z /= nrm;
        }

        double coh = 0.0;
        for (size_t i = 0; i < f.chroma.size(); ++i)
        {
            if (barOf[i] < 0) continue;
            double d = 0;
            for (size_t k = 0; k < 12; ++k) d += f.chroma[i][k] * unit[(size_t) barOf[i]][k];
            coh += f.weight[i] * d;
        }
        double skipped = 0.0;   // audio before bar 1 is only allowed to be a short lead-in
        for (size_t i = 0; i < f.chroma.size(); ++i)
            if (f.firstFrameCentre + (double) i * f.frameSeconds < off)
                skipped += f.weight[i];
        return (coh - 0.3 * skipped) / allWeight;
    };

    std::vector<Vec> barC, barB;
    std::vector<double> ws;
    double bestOff = 0.0;
    {
        const double base = evaluate (0.0, barC, barB, ws);
        double bestScore = base;
        for (int k = 1; k <= 13; ++k)   // lead-in can be at most ~0.4 bar
        {
            const double off = barSec * k / 32.0;
            const double s = evaluate (off, barC, barB, ws);
            if (s > bestScore) { bestScore = s; bestOff = off; }
        }
        if (bestScore < base + 0.04)
            bestOff = 0.0;
    }
    evaluate (bestOff, barC, barB, ws);
    a.offsetSeconds = bestOff;

    for (int b = 1; b < bars; ++b)
        if (ws[(size_t) b] < 1e-9)
        {
            barC[(size_t) b] = barC[(size_t) b - 1];
            barB[(size_t) b] = barB[(size_t) b - 1];
        }

    // 60 states: 24 triads + maj7 / m7 / dom7 (they map back to a major or minor triad)
    constexpr int S = 60;
    auto mapped = [] (int s) { return s < 24 ? s : s < 36 ? s - 24 : s < 48 ? 12 + (s - 36) : s - 48; };

    // Each chord note also excites its own overtones (3rd harmonic = fifth above, 5th = major third ...).
    float noteProfile[12] = {};
    {
        const int harmonicSemis[] = { 0, 12, 19, 24, 28, 31, 34, 36 };
        for (int h = 0; h < 8; ++h)
            noteProfile[harmonicSemis[h] % 12] += 1.0f / std::pow ((float) (h + 1), 0.9f);
    }

    std::array<Vec, S> tmpl {};
    for (int s = 0; s < S; ++s)
    {
        const int root = s % 12;
        float weights[4] = { 1.0f, 0.85f, 0.9f, 0.0f };
        int ivals[4] = { 0, 0, 7, 0 };
        int count = 3;
        if (s < 24)
            ivals[1] = s >= 12 ? 3 : 4;
        else
        {
            const int type = (s - 24) / 12;               // 0 maj7, 1 m7, 2 dom7
            ivals[1] = type == 1 ? 3 : 4;
            ivals[3] = type == 0 ? 11 : 10;
            weights[3] = 0.75f;
            count = 4;
        }

        Vec& t = tmpl[(size_t) s];
        t.fill (0.0f);
        for (int n = 0; n < count; ++n)
            for (int pc = 0; pc < 12; ++pc)
                t[(size_t) ((root + ivals[n] + pc) % 12)] += weights[n] * noteProfile[pc];

        float nrm = 0;
        for (auto v : t) nrm += v * v;
        nrm = std::sqrt (nrm);
        for (auto& v : t) v /= nrm;
    }

    std::vector<std::array<double, S>> emit ((size_t) bars);
    for (int b = 0; b < bars; ++b)
    {
        float nrm = 0, bsum = 0;
        for (auto v : barC[(size_t) b]) nrm += v * v;
        nrm = std::sqrt (nrm);
        for (auto v : barB[(size_t) b]) bsum += v;

        for (int s = 0; s < S; ++s)
        {
            double dot = 0;
            if (nrm > 1e-9f)
                for (size_t k = 0; k < 12; ++k)
                    dot += (barC[(size_t) b][k] / nrm) * tmpl[(size_t) s][k];
            const double bassBonus = bsum > 1e-9f ? 0.2 * barB[(size_t) b][(size_t) (s % 12)] / bsum : 0.0;
            emit[(size_t) b][(size_t) s] = dot + bassBonus - (s >= 24 ? 0.04 : 0.0);
        }
    }

    // Scale lock: only triads that belong to the key (plus a few very common borrowed chords)
    // are allowed. Anything else is pushed far below so it can never be chosen.
    auto scaleBonus = [] (int t, bool minor) -> std::array<double, 24>
    {
        std::array<double, 24> d {};
        d.fill (-10.0);
        auto set = [&] (int off, bool mn, double v) { d[(size_t) (((t + off) % 12) + (mn ? 12 : 0))] = v; };
        if (! minor)
        {
            // borrowed: V/vi, V/V, bVI, bVII, iv
            set (4, false, 0.05); set (2, false, 0.04); set (8, false, 0.05); set (10, false, 0.06); set (5, true, 0.06);
            // diatonic
            set (0, false, 0.12); set (2, true, 0.12); set (4, true, 0.12); set (5, false, 0.12); set (7, false, 0.12); set (9, true, 0.12);
        }
        else
        {
            // borrowed: IV major (dorian), bII (phrygian)
            set (5, false, 0.06); set (1, false, 0.06);
            // natural minor triads + major V (harmonic minor)
            set (0, true, 0.12); set (3, false, 0.12); set (5, true, 0.12); set (7, true, 0.12); set (8, false, 0.12); set (10, false, 0.12);
            set (7, false, 0.12);
        }
        return d;
    };

    // Viterbi for one key hypothesis (small penalty for changing chord)
    const double changePenalty = 0.015;
    auto decode = [&] (const std::array<double, 24>& bonus, std::vector<int>& path) -> double
    {
        std::vector<std::array<double, S>> dp ((size_t) bars);
        std::vector<std::array<int, S>> from ((size_t) bars);
        for (int s = 0; s < S; ++s) dp[0][(size_t) s] = emit[0][(size_t) s] + bonus[(size_t) mapped (s)];
        for (int b = 1; b < bars; ++b)
            for (int s = 0; s < S; ++s)
            {
                double best = -1e9;
                int arg = s;
                for (int p = 0; p < S; ++p)
                {
                    const double v = dp[(size_t) b - 1][(size_t) p] - (mapped (p) == mapped (s) ? 0.0 : changePenalty);
                    if (v > best) { best = v; arg = p; }
                }
                dp[(size_t) b][(size_t) s] = best + emit[(size_t) b][(size_t) s] + bonus[(size_t) mapped (s)];
                from[(size_t) b][(size_t) s] = arg;
            }

        path.assign ((size_t) bars, 0);
        int cur = (int) (std::max_element (dp.back().begin(), dp.back().end()) - dp.back().begin());
        const double total = dp.back()[(size_t) cur];
        for (int b = bars - 1; b >= 0; --b)
        {
            path[(size_t) b] = cur;
            if (b > 0) cur = from[(size_t) b][(size_t) cur];
        }
        return total;
    };

    // Try every key; the best key = chords that fit one scale + agree with the overall pitch profile.
    double bestTotal = -1e9;
    std::vector<int> bestPath, path;
    for (int t = 0; t < 12; ++t)
        for (int m = 0; m < 2; ++m)
        {
            double total = decode (scaleBonus (t, m == 1), path) + 1.5 * f.keyCorr[(size_t) (t + 12 * m)];

            // the key's own chord should show up (this separates a minor key from its relative major)
            const int tonic = t + 12 * m;
            int tonics = 0;
            for (int b = 0; b < bars; ++b)
                if (mapped (path[(size_t) b]) == tonic) ++tonics;
            total += 0.5 * (double) tonics / (double) bars + (mapped (path[0]) == tonic ? 0.15 : 0.0);

            if (total > bestTotal) { bestTotal = total; bestPath = path; }
        }

    for (int b = 0; b < bars; ++b)
    {
        const int m = mapped (bestPath[(size_t) b]);
        a.chords.push_back ({ m % 12, m >= 12 });
    }

    // Name the key (relative major/minor share the same chords: pick by tonic evidence).
    {
        double bestScore = -1.0;
        for (int t = 0; t < 12; ++t)
            for (int m = 0; m < 2; ++m)
            {
                const auto d = scaleBonus (t, m == 1);
                double score = 0.0;
                for (int b = 0; b < bars; ++b)
                {
                    const auto& c = a.chords[(size_t) b];
                    if (d[(size_t) (c.root + (c.minor ? 12 : 0))] > 0.0) score += 1.0;
                    if (c.root == t && c.minor == (m == 1)) score += 0.8;
                }
                if (a.chords[0].root == t && a.chords[0].minor == (m == 1)) score += 1.2;
                score += 1.5 * f.keyCorr[(size_t) (t + 12 * m)];

                if (score > bestScore) { bestScore = score; a.keyRoot = t; a.keyMinor = (m == 1); }
            }
    }

    return a;
}

Analysis buildAnalysisAuto (const Features& f, double bpm)
{
    auto a = buildAnalysis (f, bpm);
    if (a.bars == 8 && bpm >= 125.0)
    {
        bool pairs = true;
        for (int b = 0; b < 8; b += 2)
            pairs = pairs && a.chords[(size_t) b].root == a.chords[(size_t) b + 1].root
                          && a.chords[(size_t) b].minor == a.chords[(size_t) b + 1].minor;
        if (pairs)
            return buildAnalysis (f, bpm * 0.5);
    }
    return a;
}
}