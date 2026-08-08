#include "gutterpress/GutterPressEngine.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

using gutterpress::GutterPressEngine;
using gutterpress::GutterPressParameters;

namespace
{

std::vector<float> renderRamp (GutterPressParameters params, int samples)
{
    GutterPressEngine engine;
    engine.prepare (48000.0);
    engine.setParameters (params);
    engine.reset();

    std::vector<float> output;
    output.reserve (static_cast<std::size_t> (samples));
    for (int i = 0; i < samples; ++i)
    {
        const auto input = -0.85f + 1.7f * static_cast<float> (i) / static_cast<float> (samples - 1);
        output.push_back (engine.processSample (input, input * 0.7f).left);
    }
    return output;
}

float averageStep (const std::vector<float>& samples)
{
    float total = 0.0f;
    for (std::size_t i = 1; i < samples.size(); ++i)
        total += std::fabs (samples[i] - samples[i - 1]);
    return total / static_cast<float> (std::max<std::size_t> (1, samples.size() - 1));
}

std::vector<float> renderAlternating (GutterPressParameters params, int samples)
{
    GutterPressEngine engine;
    engine.prepare (48000.0);
    engine.setParameters (params);
    engine.reset();

    std::vector<float> output;
    output.reserve (static_cast<std::size_t> (samples));
    for (int i = 0; i < samples; ++i)
    {
        const auto input = (i & 1) == 0 ? 0.45f : -0.45f;
        output.push_back (engine.processSample (input, input).left);
    }
    return output;
}

int transitionCount (const std::vector<float>& samples)
{
    int transitions = 0;
    for (std::size_t i = 1; i < samples.size(); ++i)
        transitions += std::fabs (samples[i] - samples[i - 1]) > 1.0e-6f ? 1 : 0;
    return transitions;
}

int distinctLevels (const std::vector<float>& samples)
{
    std::vector<int> levels;
    levels.reserve (samples.size());
    for (const auto sample : samples)
        levels.push_back (static_cast<int> (std::round (sample * 10000.0f)));
    std::sort (levels.begin(), levels.end());
    levels.erase (std::unique (levels.begin(), levels.end()), levels.end());
    return static_cast<int> (levels.size());
}

void testSilenceStaysSilent()
{
    GutterPressEngine engine;
    engine.prepare (48000.0);
    engine.reset();

    for (int i = 0; i < 8192; ++i)
    {
        const auto frame = engine.processSample (0.0f, 0.0f);
        assert (std::fabs (frame.left) <= 1.0e-7f);
        assert (std::fabs (frame.right) <= 1.0e-7f);
    }
}

void testCrushReducesTransitionsAndDistinctLevels()
{
    GutterPressParameters clean;
    clean.crush = 0.0f;
    clean.gutter = 0.0f;
    clean.tone = 1.0f;
    clean.mix = 1.0f;

    GutterPressParameters crushed = clean;
    crushed.crush = 1.0f;

    const auto cleanOutput = renderRamp (clean, 4096);
    const auto crushedOutput = renderRamp (crushed, 4096);

    assert (transitionCount (crushedOutput) < transitionCount (cleanOutput) / 5);
    assert (distinctLevels (crushedOutput) < distinctLevels (cleanOutput) / 8);
}

void testGutterGatesSmallSignals()
{
    GutterPressParameters params;
    params.crush = 0.0f;
    params.gutter = 1.0f;
    params.tone = 1.0f;
    params.mix = 1.0f;
    params.outputGain = 1.0f;

    GutterPressEngine engine;
    engine.prepare (48000.0);
    engine.setParameters (params);
    engine.reset();

    for (int i = 0; i < 512; ++i)
    {
        const auto frame = engine.processSample (0.02f, -0.02f);
        assert (std::fabs (frame.left) <= 1.0e-6f);
        assert (std::fabs (frame.right) <= 1.0e-6f);
    }
}

void testToneChangesBrightness()
{
    GutterPressParameters dark;
    dark.crush = 0.2f;
    dark.gutter = 0.0f;
    dark.tone = 0.0f;

    GutterPressParameters bright = dark;
    bright.tone = 1.0f;

    auto darkOutput = renderAlternating (dark, 4096);
    auto brightOutput = renderAlternating (bright, 4096);

    assert (averageStep (brightOutput) > averageStep (darkOutput) * 1.35f);
}

void testDeterministic()
{
    GutterPressParameters params;
    params.crush = 0.73f;
    params.gutter = 0.48f;

    const auto a = renderRamp (params, 4096);
    const auto b = renderRamp (params, 4096);
    assert (a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
        assert (std::fabs (a[i] - b[i]) <= 1.0e-6f);
}

void testFiniteBoundedExtremeParameters()
{
    GutterPressParameters params;
    params.inputGain = 1000.0f;
    params.crush = 1000.0f;
    params.gutter = 1000.0f;
    params.tone = std::numeric_limits<float>::infinity();
    params.mix = 1000.0f;
    params.outputGain = 1000.0f;

    GutterPressEngine engine;
    engine.prepare (0.0);
    engine.setParameters (params);
    engine.reset();

    for (int i = 0; i < 8192; ++i)
    {
        const auto frame = engine.processSample (1000.0f, -1000.0f);
        assert (std::isfinite (frame.left));
        assert (std::isfinite (frame.right));
        assert (frame.left >= -0.9801f && frame.left <= 0.9801f);
        assert (frame.right >= -0.9801f && frame.right <= 0.9801f);
    }
}

void testDenormalInputDoesNotLeak()
{
    GutterPressEngine engine;
    engine.prepare (48000.0);
    engine.reset();

    for (int i = 0; i < 1024; ++i)
    {
        const auto frame = engine.processSample (1.0e-30f, -1.0e-30f);
        assert (std::fabs (frame.left) <= 1.0e-7f);
        assert (std::fabs (frame.right) <= 1.0e-7f);
    }
}

} // namespace

int main()
{
    testSilenceStaysSilent();
    testCrushReducesTransitionsAndDistinctLevels();
    testGutterGatesSmallSignals();
    testToneChangesBrightness();
    testDeterministic();
    testFiniteBoundedExtremeParameters();
    testDenormalInputDoesNotLeak();

    std::cout << "GutterPressEngineTests passed\n";
    return 0;
}
