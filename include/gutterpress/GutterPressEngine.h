#pragma once

#include "gutterpress/GutterPressDspPrimitives.h"

namespace gutterpress
{

/** Realtime-safe parameter set for the GutterPress stereo effect.

    Public values are sanitized by setParameters():
    inputGain [0, 2], crush [0, 1], gutter [0, 1], tone [0, 1],
    mix [0, 1], outputGain [0, 2].
*/
struct GutterPressParameters
{
    float inputGain = 1.0f;
    float crush = 0.58f;
    float gutter = 0.42f;
    float tone = 0.55f;
    float mix = 1.0f;
    float outputGain = 0.82f;
};

/** Stereo input effect with deliberate broken digital edges.

    The signal path is input gain -> sample-rate/bit-depth crush ->
    amplitude gutter/gate/clamp -> tone filter -> dry/wet -> bounded output.
    prepare(), reset(), processSample(), and process() allocate no memory.
*/
class GutterPressEngine
{
public:
    GutterPressEngine();

    /** Sets the sample rate and rebuilds coefficients; invalid rates fall back to 44.1 kHz. */
    void prepare (double sampleRate) noexcept;

    /** Clears hold, filter, and deterministic state. */
    void reset() noexcept;

    /** Clamps and applies all public parameters. */
    void setParameters (const GutterPressParameters& parameters) noexcept;

    /** Processes one stereo input frame and returns finite output bounded to +/-0.98. */
    [[nodiscard]] StereoFrame processSample (float inputLeft, float inputRight) noexcept;

    /** Processes stereo buffers in-place. Null buffers and non-positive sizes are ignored. */
    void process (float* left, float* right, int numSamples) noexcept;

private:
    struct ClampedParameters
    {
        float inputGain = 1.0f;
        float crush = 0.58f;
        float gutter = 0.42f;
        float tone = 0.55f;
        float mix = 1.0f;
        float outputGain = 0.82f;
    };

    void updateToneFilters() noexcept;
    [[nodiscard]] StereoFrame crushFrame (float left, float right) noexcept;
    [[nodiscard]] float quantize (float input) const noexcept;
    [[nodiscard]] float applyGutter (float input) const noexcept;
    [[nodiscard]] StereoFrame sanitizeFrame (float left, float right) const noexcept;

    ClampedParameters params;
    double sampleRate = 44100.0;
    int holdSamples = 1;
    int holdCountdown = 0;
    float quantizationSteps = 32767.0f;
    float heldLeft = 0.0f;
    float heldRight = 0.0f;
    Biquad toneLeft;
    Biquad toneRight;
    DcBlocker dcLeft;
    DcBlocker dcRight;
};

} // namespace gutterpress
