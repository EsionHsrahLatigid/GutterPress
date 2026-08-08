#include "gutterpress/GutterPressEngine.h"

#include <algorithm>
#include <cmath>

namespace gutterpress
{
namespace
{
constexpr float ceiling = 0.98f;

[[nodiscard]] float sanitizeAudio (float value) noexcept
{
    return clampFinite (value, -8.0f, 8.0f, 0.0f);
}
} // namespace

GutterPressEngine::GutterPressEngine()
{
    prepare (44100.0);
    reset();
}

void GutterPressEngine::prepare (double newSampleRate) noexcept
{
    sampleRate = std::isfinite (newSampleRate) && newSampleRate > 1.0 ? newSampleRate : 44100.0;
    updateToneFilters();
    dcLeft.prepare (sampleRate, 7.0f);
    dcRight.prepare (sampleRate, 7.0f);
    reset();
}

void GutterPressEngine::reset() noexcept
{
    holdCountdown = 0;
    heldLeft = 0.0f;
    heldRight = 0.0f;
    toneLeft.reset();
    toneRight.reset();
    dcLeft.reset();
    dcRight.reset();
}

void GutterPressEngine::setParameters (const GutterPressParameters& parameters) noexcept
{
    params.inputGain = clampFinite (parameters.inputGain, 0.0f, 2.0f, GutterPressParameters {}.inputGain);
    params.crush = clampFinite (parameters.crush, 0.0f, 1.0f, GutterPressParameters {}.crush);
    params.gutter = clampFinite (parameters.gutter, 0.0f, 1.0f, GutterPressParameters {}.gutter);
    params.tone = clampFinite (parameters.tone, 0.0f, 1.0f, GutterPressParameters {}.tone);
    params.mix = clampFinite (parameters.mix, 0.0f, 1.0f, GutterPressParameters {}.mix);
    params.outputGain = clampFinite (parameters.outputGain, 0.0f, 2.0f, GutterPressParameters {}.outputGain);

    holdSamples = 1 + static_cast<int> (std::round (params.crush * params.crush * 47.0f));
    const auto effectiveBits = 16.0f - params.crush * 13.0f;
    quantizationSteps = std::max (3.0f, std::exp2 (effectiveBits) - 1.0f);
    updateToneFilters();
}

StereoFrame GutterPressEngine::processSample (float inputLeft, float inputRight) noexcept
{
    const auto dryLeft = sanitizeAudio (inputLeft);
    const auto dryRight = sanitizeAudio (inputRight);
    const auto drivenLeft = dryLeft * params.inputGain;
    const auto drivenRight = dryRight * params.inputGain;

    const auto crushed = crushFrame (drivenLeft, drivenRight);
    const auto gutteredLeft = applyGutter (crushed.left);
    const auto gutteredRight = applyGutter (crushed.right);
    const auto wetLeft = params.tone >= 0.999f ? gutteredLeft : dcLeft.process (toneLeft.process (gutteredLeft));
    const auto wetRight = params.tone >= 0.999f ? gutteredRight : dcRight.process (toneRight.process (gutteredRight));

    const auto dry = 1.0f - params.mix;
    return sanitizeFrame ((dryLeft * dry + wetLeft * params.mix) * params.outputGain,
                          (dryRight * dry + wetRight * params.mix) * params.outputGain);
}

void GutterPressEngine::process (float* left, float* right, int numSamples) noexcept
{
    if (left == nullptr || right == nullptr || numSamples <= 0)
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto frame = processSample (left[i], right[i]);
        left[i] = frame.left;
        right[i] = frame.right;
    }
}

void GutterPressEngine::updateToneFilters() noexcept
{
    const auto cutoff = 420.0f * std::pow (32.0f, params.tone);
    const auto harshness = 0.68f + params.crush * 0.34f;
    toneLeft.setLowPass (sampleRate, cutoff, harshness);
    toneRight.setLowPass (sampleRate, cutoff * (1.0f + 0.025f * params.gutter), harshness);
}

StereoFrame GutterPressEngine::crushFrame (float left, float right) noexcept
{
    if (holdCountdown <= 0)
    {
        heldLeft = quantize (left);
        heldRight = quantize (right);
        holdCountdown = holdSamples;
    }

    --holdCountdown;
    return { heldLeft, heldRight };
}

float GutterPressEngine::quantize (float input) const noexcept
{
    const auto clipped = clampFinite (input, -1.35f, 1.35f, 0.0f);
    const auto normalized = clipped / 1.35f;
    const auto stepped = std::round (normalized * quantizationSteps) / quantizationSteps;
    return sanitizeAudio (stepped * 1.35f);
}

float GutterPressEngine::applyGutter (float input) const noexcept
{
    if (params.gutter <= 0.0001f)
        return sanitizeAudio (input);

    const auto magnitude = std::fabs (input);
    const auto threshold = params.gutter * 0.18f;
    if (magnitude < threshold)
        return 0.0f;

    const auto hardLimit = 1.0f - params.gutter * 0.42f;
    auto output = input;
    if (magnitude > hardLimit)
    {
        const auto sign = input < 0.0f ? -1.0f : 1.0f;
        const auto overflow = magnitude - hardLimit;
        output = sign * (hardLimit - std::fmod (overflow, 0.19f + 0.2f * (1.0f - params.gutter)));
    }

    const auto serration = std::round (output * (11.0f + params.gutter * 53.0f)) / (11.0f + params.gutter * 53.0f);
    return sanitizeAudio (serration);
}

StereoFrame GutterPressEngine::sanitizeFrame (float left, float right) const noexcept
{
    auto safeLeft = boundedDrive (left, 1.12f + params.crush * 0.88f);
    auto safeRight = boundedDrive (right, 1.12f + params.crush * 0.88f);
    if (std::fabs (safeLeft) < 1.0e-20f)
        safeLeft = 0.0f;
    if (std::fabs (safeRight) < 1.0e-20f)
        safeRight = 0.0f;
    return { std::clamp (safeLeft, -ceiling, ceiling),
             std::clamp (safeRight, -ceiling, ceiling) };
}

} // namespace gutterpress
