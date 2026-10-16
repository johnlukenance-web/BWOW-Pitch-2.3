// Bwow Pitch 2 - JUCE-free DSP core.
// Audio-triggered one-shot pitch bend: on each detected pick attack the whole signal is
// pitched up by `amount` cents and glides linearly (in cents) back to unity over `decay` ms.
// No MIDI, no gain envelope: the output is always the (pitch-shifted) input signal.
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace bwow
{
constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------- attack detector
// Two-band onset detector working on the incoming audio only:
//  - "body" band (HP 150 Hz): catches attacks that add broadband energy over a quiet/decaying ring
//  - "click" band (HP 2 kHz): the pick transient, which stands out even over a loud sustained note
// Each band compares a fast envelope to a slower smoothed copy of itself (ratio test) and to a
// level floor; either band firing triggers. A 25 ms refractory time prevents double triggers.
class AttackDetector
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        body.init (sr, 150.0, 0.0004, 0.012, 0.020);
        click.init (sr, 2000.0, 0.0002, 0.006, 0.015);
        noiseRise = std::pow (10.0, (3.0 / 20.0) / sr); // noise floor may rise 3 dB/s
        refractory = (int) std::lround (0.025 * sr);
        warmup = (int) std::lround (0.030 * sr);
        setSensitivity (0);
        reset();
    }

    void reset() { body.reset(); click.reset(); cool = 0; warm = warmup; }

    // 0 = Auto, 1..10 = manual (higher = more sensitive)
    void setSensitivity (int s)
    {
        if (s <= 0)
        {
            autoFloor = true;
            body.ratio = 2.0;  body.floorAbs = 0.0015;
            click.ratio = 2.4; click.floorAbs = 0.0006;
            return;
        }
        s = std::min (s, 10);
        autoFloor = false;
        const double u = (s - 1) / 9.0; // 0 (least sensitive) .. 1 (most sensitive)
        body.ratio  = 3.6 - u * 2.1;   body.floorAbs  = std::pow (10.0, (-36.0 - u * 30.0) / 20.0);
        click.ratio = 5.0 - u * 3.0;   click.floorAbs = std::pow (10.0, (-46.0 - u * 30.0) / 20.0);
    }

    bool process (float in)
    {
        const bool b1 = body.step (in, noiseRise, autoFloor);
        const bool b2 = click.step (in, noiseRise, autoFloor);
        if (warm > 0) { --warm; return false; } // let filters settle after start/reset
        if (cool > 0) { --cool; return false; }
        if (b1 || b2) { cool = refractory; return true; }
        return false;
    }

    int latencyEstimateSamples() const { return (int) std::lround (0.0008 * sr); }

private:
    struct Band
    {
        double hpCoef = 0, att = 0, rel = 0, refC = 0;
        double l1 = 0, l2 = 0, l3 = 0, l4 = 0, fast = 0, ref = 0, noise = 0.01, ratio = 2.2, floorAbs = 0.0015;
        void init (double sr, double fc, double a, double r, double rc)
        {
            hpCoef = 1.0 - std::exp (-2.0 * kPi * fc / sr);
            att = 1.0 - std::exp (-1.0 / (a * sr)); rel = 1.0 - std::exp (-1.0 / (r * sr)); refC = 1.0 - std::exp (-1.0 / (rc * sr));
        }
        void reset() { l1 = l2 = l3 = l4 = fast = ref = 0.0; noise = 0.01; }
        bool step (float in, double noiseRise, bool autoFloor)
        {
            const double x = in;
            l1 += hpCoef * (x - l1);  const double h1 = x - l1;
            l2 += hpCoef * (h1 - l2); const double a = std::fabs (h1 - l2);
            fast += (a > fast ? att : rel) * (a - fast);
            noise = std::min (noise * noiseRise, fast) + 1e-12;
            const double floorV = autoFloor ? std::max (floorAbs, 4.0 * noise) : floorAbs;
            const bool trig = fast > ratio * ref && fast > floorV;
            ref += refC * (fast - ref);
            return trig;
        }
    };

    double sr = 48000.0, noiseRise = 1.0;
    Band body, click;
    bool autoFloor = true;
    int refractory = 1680, cool = 0, warmup = 1440, warm = 1440;
};

// ---------------------------------------------------------------- core
class Core
{
public:
    void prepare (double sampleRate, int channels)
    {
        sr = sampleRate;
        nch = std::max (1, std::min (channels, 2));
        d0 = (int) std::lround (0.010 * sr);                 // reported latency
        xfLen = std::max (16, (int) std::lround (0.005 * sr));
        corrWin = (int) std::lround (0.008 * sr);
        searchHalf = (int) std::lround (0.0005 * sr);
        minDelay = 2.5 * 0.001 * sr;
        int need = (int) (0.25 * sr), size = 1;
        while (size < need) size <<= 1;
        for (int c = 0; c < 2; ++c) buf[c].assign ((size_t) size, 0.f);
        mask = size - 1;
        det.prepare (sr);
        det.setSensitivity (sens);
        reset();
    }

    void reset()
    {
        for (auto& b : buf) std::fill (b.begin(), b.end(), 0.f);
        n = 0; h0 = h1 = (double) d0; xfActive = false; xfPos = 0;
        pending = 0; active = false; rampPos = 0; rampLen = 1;
        det.reset();
    }

    void setParameters (float amountCents, float decayMs, int sensitivity)
    {
        amount = std::max (0.f, amountCents);
        decay = std::max (1.f, decayMs);
        if (sensitivity != sens) { sens = sensitivity; det.setSensitivity (sens); }
    }

    int getLatencySamples() const { return d0; }
    int getTriggerCount() const { return triggerCount.load (std::memory_order_relaxed); }
    double getCurrentCents() const { return curCents; } // diagnostics

    // optional test hook
    std::vector<int64_t>* triggerLog = nullptr;

    void process (float* const* io, int numCh, int num)
    {
        const int chs = std::max (1, std::min (numCh, nch));
        for (int i = 0; i < num; ++i)
        {
            float mono = 0.f;
            for (int c = 0; c < chs; ++c) { buf[c][(size_t) (n & mask)] = io[c][i]; mono += io[c][i]; }
            mono /= (float) chs;

            if (det.process (mono)) onTrigger();

            if (pending > 0 && --pending == 0)
            {
                rampPos = 0;
                rampLen = std::max (1, (int) std::lround (decay * 0.001 * sr));
                rampAmt = amount;
                active = true;
            }

            double rm = 0.0;
            curCents = 0.0;
            if (active)
            {
                curCents = rampAmt * (1.0 - (double) rampPos / (double) rampLen);
                rm = std::exp2 (curCents / 1200.0) - 1.0;
                if (++rampPos >= rampLen) active = false;
            }

            if (! xfActive && h0 < minDelay) startRecenter();

            for (int c = 0; c < chs; ++c)
            {
                float y = read (c, h0);
                if (xfActive)
                {
                    const float w = (float) xfPos / (float) xfLen;
                    y += (read (c, h1) - y) * w;
                }
                io[c][i] = y;
            }

            h0 -= rm;
            if (xfActive)
            {
                h1 -= rm;
                if (++xfPos >= xfLen) { h0 = h1; xfActive = false; }
            }
            ++n;
        }
    }

private:
    float sampleAt (int c, int64_t idx) const { return buf[c][(size_t) (idx & mask)]; }

    float read (int c, double d) const
    {
        d = std::max (d, 2.0);
        const double pos = (double) n - d;
        const int64_t i0 = (int64_t) std::floor (pos);
        const float f = (float) (pos - (double) i0);
        const float y0 = sampleAt (c, i0 - 1), y1 = sampleAt (c, i0), y2 = sampleAt (c, i0 + 1), y3 = sampleAt (c, i0 + 2);
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }

    float monoAt (int64_t idx) const
    {
        float s = 0.f;
        for (int c = 0; c < nch; ++c) s += sampleAt (c, idx);
        return s;
    }

    // Move the read head back to the nominal latency (+-0.5 ms, chosen for best waveform
    // match) with a short crossfade, so every attack always reaches the output at ~d0.
    void startRecenter()
    {
        std::vector<float>& ref = tmp;
        ref.assign ((size_t) corrWin, 0.f);
        for (int i = 0; i < corrWin; ++i)
        {
            const double pos = (double) n - h0 - (double) (corrWin - i);
            const int64_t i0 = (int64_t) std::floor (pos);
            const double f = pos - (double) i0;
            ref[(size_t) i] = (float) ((1.0 - f) * monoAt (i0) + f * monoAt (i0 + 1));
        }
        int best = d0;
        double bestScore = -1e30;
        for (int d = d0 - searchHalf; d <= d0 + searchHalf; ++d)
        {
            double dot = 0, en = 1e-12;
            for (int i = 0; i < corrWin; ++i)
            {
                const double v = monoAt (n - d - (corrWin - i));
                dot += ref[(size_t) i] * v; en += v * v;
            }
            const double score = dot / std::sqrt (en) - 1e-6 * std::abs (d - d0);
            if (score > bestScore) { bestScore = score; best = d; }
        }
        if (bestScore <= 0.0) best = d0;
        h1 = (double) best;
        xfPos = 0;
        xfActive = true;
    }

    void onTrigger()
    {
        triggerCount.fetch_add (1, std::memory_order_relaxed);
        if (triggerLog) triggerLog->push_back (n);
        double target;
        if (! xfActive && std::abs (h0 - (double) d0) >= 0.5) { startRecenter(); target = h1; }
        else if (xfActive) target = h1;
        else target = h0;
        // Attack onset is ~detLat samples old; it reaches the output after `target` samples.
        pending = std::max (1, (int) std::lround (target) - det.latencyEstimateSamples());
    }

    double sr = 48000.0;
    int nch = 2, d0 = 480, xfLen = 240, corrWin = 384, searchHalf = 24, mask = 0;
    double minDelay = 120.0;
    std::vector<float> buf[2];
    std::vector<float> tmp;
    int64_t n = 0;
    double h0 = 480, h1 = 480;
    bool xfActive = false; int xfPos = 0;
    int pending = 0; bool active = false; int rampPos = 0, rampLen = 1; double rampAmt = 0, curCents = 0;
    float amount = 65.f, decay = 143.f; int sens = 0;
    AttackDetector det;
    std::atomic<int> triggerCount { 0 };
};
} // namespace bwow
