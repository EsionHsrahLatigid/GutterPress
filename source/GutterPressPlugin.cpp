#include "GutterPressPlugin.h"

#include "ProductState.h"

#if ! GUTTERPRESS_HEADLESS_TEST
#include "ParameterGridEditor.h"
#endif

#include <algorithm>
#include <array>
#include <cmath>

namespace gutterpress::plugin
{
namespace
{
constexpr std::array<char, 4> stateMagic {{ 'G', 'T', 'P', '1' }};
constexpr int stateVersion = 1;
constexpr std::size_t presetParameterCount = 6;

constexpr std::array<std::array<float, presetParameterCount>, 4> presetValues {{
    {{ 1.00f, 0.58f, 0.42f, 0.55f, 1.00f, 0.82f }},
    {{ 1.18f, 0.78f, 0.28f, 0.72f, 0.92f, 0.70f }},
    {{ 0.92f, 0.46f, 0.74f, 0.36f, 1.00f, 0.86f }},
    {{ 1.34f, 0.91f, 0.61f, 0.82f, 0.84f, 0.64f }}
}};

yup::AudioParameter::Ptr makeParameter (const char* id,
                                        const char* name,
                                        int hostID,
                                        float minValue,
                                        float maxValue,
                                        float defaultValue,
                                        yup::AudioParameter::ParameterUnit unit,
                                        float smoothingMs)
{
    return yup::AudioParameterBuilder()
        .withID (id)
        .withName (name)
        .withHostID (static_cast<yup::uint32> (hostID))
        .withRange (minValue, maxValue)
        .withDefault (defaultValue)
        .withSmoothing (smoothingMs)
        .withModulatable (true)
        .withUnit (unit)
        .build();
}
} // namespace

GutterPressPlugin::GutterPressPlugin()
    : yup::AudioProcessor ("GutterPress",
                           yup::AudioBusLayout ({
                                                    yup::AudioBus ("main", yup::AudioBus::Audio, yup::AudioBus::Input, 2),
                                                },
                                                {
                                                    yup::AudioBus ("main", yup::AudioBus::Audio, yup::AudioBus::Output, 2),
                                                }))
{
    parameters[input] = makeParameter ("input", "Input", input, 0.0f, 2.0f, presetValues[0][input], yup::AudioParameter::ParameterUnit::LinearGain, 18.0f);
    parameters[crush] = makeParameter ("crush", "Crush", crush, 0.0f, 1.0f, presetValues[0][crush], yup::AudioParameter::ParameterUnit::Percent, 18.0f);
    parameters[gutter] = makeParameter ("gutter", "Gutter/Gate", gutter, 0.0f, 1.0f, presetValues[0][gutter], yup::AudioParameter::ParameterUnit::Percent, 18.0f);
    parameters[tone] = makeParameter ("tone", "Tone", tone, 0.0f, 1.0f, presetValues[0][tone], yup::AudioParameter::ParameterUnit::Percent, 28.0f);
    parameters[mix] = makeParameter ("mix", "Mix", mix, 0.0f, 1.0f, presetValues[0][mix], yup::AudioParameter::ParameterUnit::Percent, 20.0f);
    parameters[output] = makeParameter ("output", "Output", output, 0.0f, 2.0f, presetValues[0][output], yup::AudioParameter::ParameterUnit::LinearGain, 20.0f);

    for (const auto& parameter : parameters)
        addParameter (parameter);

    syncParameterValuesFromParameters();
    updateEngineParameters();
}

void GutterPressPlugin::prepareToPlay (const yup::AudioSpec& spec)
{
    engine.prepare (spec.sampleRate);
    engine.reset();

    for (std::size_t i = 0; i < parameterHandles.size(); ++i)
        parameterHandles[i] = yup::AudioParameterHandle (*parameters[i], spec.sampleRate);

    syncParameterValuesFromParameters();
    updateEngineParameters();
    controlUpdateCountdown = 0;
    inputPeakMilli.store (0, std::memory_order_relaxed);
    outputPeakMilli.store (0, std::memory_order_relaxed);

#if defined(YUP_AUDIO_PLUGIN_ENABLE_STANDALONE)
    auditionSampleRate = std::isfinite (spec.sampleRate) && spec.sampleRate > 1.0 ? spec.sampleRate : 44100.0;
    auditionPhase = 0.0f;
    auditionNoise = 0x6d2b79f5u;
#endif
}

void GutterPressPlugin::releaseResources()
{
}

void GutterPressPlugin::processBlock (yup::AudioProcessContext<float>& context)
{
    auto& audio = context.audio;
    const auto numSamples = audio.getNumSamples();
    const auto numChannels = audio.getNumChannels();

    for (std::size_t i = 0; i < parameterHandles.size(); ++i)
        parameterHandles[i].prepareBlock (context.params, parameters[i]->getIndexInContainer());

    auto* left = numChannels > 0 ? audio.getWritePointer (0) : nullptr;
    auto* right = numChannels > 1 ? audio.getWritePointer (1) : nullptr;
    float blockInputPeak = 0.0f;
    float blockOutputPeak = 0.0f;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        advanceParameterHandles (sample);
        if (controlUpdateCountdown <= 0)
        {
            updateEngineParameters();
            controlUpdateCountdown = parameterUpdateCadenceSamples;
        }
        --controlUpdateCountdown;

        auto inputLeft = left != nullptr ? left[sample] : 0.0f;
        auto inputRight = right != nullptr ? right[sample] : inputLeft;

#if defined(YUP_AUDIO_PLUGIN_ENABLE_STANDALONE)
        const auto audition = renderAuditionFrame();
        inputLeft += audition.left;
        inputRight += audition.right;
#endif

        blockInputPeak = std::max (blockInputPeak, std::max (std::fabs (inputLeft), std::fabs (inputRight)));

        const auto frame = engine.processSample (inputLeft, inputRight);
        if (left != nullptr)
            left[sample] = frame.left;
        if (right != nullptr)
            right[sample] = frame.right;
        blockOutputPeak = std::max (blockOutputPeak, std::max (std::fabs (frame.left), std::fabs (frame.right)));

        for (int channel = 2; channel < numChannels; ++channel)
            audio.getWritePointer (channel)[sample] = 0.0f;
    }

    inputPeakMilli.store (static_cast<int> (std::clamp (blockInputPeak, 0.0f, 1.0f) * 1000.0f + 0.5f),
                          std::memory_order_relaxed);
    outputPeakMilli.store (static_cast<int> (std::clamp (blockOutputPeak, 0.0f, 1.0f) * 1000.0f + 0.5f),
                           std::memory_order_relaxed);
    context.midi.clear();
}

void GutterPressPlugin::flush()
{
    engine.reset();
    controlUpdateCountdown = 0;
    inputPeakMilli.store (0, std::memory_order_relaxed);
    outputPeakMilli.store (0, std::memory_order_relaxed);
#if defined(YUP_AUDIO_PLUGIN_ENABLE_STANDALONE)
    auditionPhase = 0.0f;
    auditionNoise = 0x6d2b79f5u;
#endif
}

bool GutterPressPlugin::acceptsMidi() const noexcept
{
    return false;
}

bool GutterPressPlugin::producesMidi() const noexcept
{
    return false;
}

int GutterPressPlugin::getCurrentPreset() const noexcept
{
    return currentPreset.load (std::memory_order_relaxed);
}

void GutterPressPlugin::setCurrentPreset (int index) noexcept
{
    if (! yup::isPositiveAndBelow (index, static_cast<int> (presetValues.size())))
        return;

    currentPreset.store (index, std::memory_order_relaxed);
    for (std::size_t i = 0; i < parameters.size(); ++i)
        parameters[i]->setValue (presetValues[static_cast<std::size_t> (index)][i]);
}

int GutterPressPlugin::getNumPresets() const
{
    return static_cast<int> (presetNames.size());
}

yup::String GutterPressPlugin::getPresetName (int index) const
{
    if (yup::isPositiveAndBelow (index, static_cast<int> (presetNames.size())))
        return presetNames[static_cast<std::size_t> (index)];
    return "Invalid Preset";
}

void GutterPressPlugin::setPresetName (int index, yup::StringRef newName)
{
    if (yup::isPositiveAndBelow (index, static_cast<int> (presetNames.size())))
        presetNames[static_cast<std::size_t> (index)] = newName;
}

yup::Result GutterPressPlugin::loadStateFromMemory (const yup::MemoryBlock& data)
{
    int loadedPreset = 0;
    const auto result = loadProductState (*this, data, stateMagic, stateVersion, getNumPresets(), loadedPreset);
    if (result.failed())
        return result;

    currentPreset.store (loadedPreset, std::memory_order_relaxed);
    return yup::Result::ok();
}

yup::Result GutterPressPlugin::saveStateIntoMemory (yup::MemoryBlock& data)
{
    return saveProductState (*this, data, stateMagic, stateVersion, currentPreset.load (std::memory_order_relaxed));
}

bool GutterPressPlugin::hasEditor() const
{
#if GUTTERPRESS_HEADLESS_TEST
    return false;
#else
    return true;
#endif
}

yup::AudioProcessorEditor* GutterPressPlugin::createEditor()
{
#if GUTTERPRESS_HEADLESS_TEST
    return nullptr;
#else
    return new ParameterGridEditor (*this,
                                    "GutterPress",
                                    "Corroded stereo effect with standalone-only audition.",
                                    0xffd9ff42u);
#endif
}

float GutterPressPlugin::getInputPeakLevel() const noexcept
{
    return static_cast<float> (inputPeakMilli.load (std::memory_order_relaxed)) * 0.001f;
}

float GutterPressPlugin::getOutputPeakLevel() const noexcept
{
    return static_cast<float> (outputPeakMilli.load (std::memory_order_relaxed)) * 0.001f;
}

#if defined(YUP_AUDIO_PLUGIN_ENABLE_STANDALONE)
void GutterPressPlugin::setAuditionEnabled (bool shouldBeEnabled) noexcept
{
    auditionEnabled.store (shouldBeEnabled ? 1 : 0, std::memory_order_relaxed);
}

bool GutterPressPlugin::isAuditionEnabled() const noexcept
{
    return auditionEnabled.load (std::memory_order_relaxed) != 0;
}

void GutterPressPlugin::setAuditionType (int type) noexcept
{
    auditionType.store (std::clamp (type, 0, 1), std::memory_order_relaxed);
}

int GutterPressPlugin::getAuditionType() const noexcept
{
    return auditionType.load (std::memory_order_relaxed);
}
#endif

void GutterPressPlugin::advanceParameterHandles (int samplePosition) noexcept
{
    for (std::size_t i = 0; i < parameterHandles.size(); ++i)
    {
        parameterHandles[i].advanceToSample (samplePosition);
        currentParameterValues[i] = parameterHandles[i].getNextValue();
    }
}

void GutterPressPlugin::syncParameterValuesFromParameters() noexcept
{
    for (std::size_t i = 0; i < parameters.size(); ++i)
        currentParameterValues[i] = parameters[i]->getValue();
}

void GutterPressPlugin::updateEngineParameters() noexcept
{
    gutterpress::GutterPressParameters engineParameters;
    engineParameters.inputGain = currentParameterValues[input];
    engineParameters.crush = currentParameterValues[crush];
    engineParameters.gutter = currentParameterValues[gutter];
    engineParameters.tone = currentParameterValues[tone];
    engineParameters.mix = currentParameterValues[mix];
    engineParameters.outputGain = currentParameterValues[output];
    engine.setParameters (engineParameters);
}

#if defined(YUP_AUDIO_PLUGIN_ENABLE_STANDALONE)
StereoFrame GutterPressPlugin::renderAuditionFrame() noexcept
{
    if (auditionEnabled.load (std::memory_order_relaxed) == 0)
        return {};

    auditionPhase += 96.0f / static_cast<float> (auditionSampleRate);
    if (auditionPhase >= 1.0f)
        auditionPhase -= 1.0f;

    auditionNoise ^= auditionNoise << 13u;
    auditionNoise ^= auditionNoise >> 17u;
    auditionNoise ^= auditionNoise << 5u;
    if (auditionNoise == 0u)
        auditionNoise = 0x6d2b79f5u;

    const auto type = auditionType.load (std::memory_order_relaxed);
    const auto noise = static_cast<float> (static_cast<double> (auditionNoise) / 2147483648.0 - 1.0);
    const auto pulse = auditionPhase < 0.18f ? 1.0f : -0.55f;
    const auto saw = auditionPhase * 2.0f - 1.0f;
    const auto source = type == 0 ? saw * 0.22f + noise * 0.035f : pulse * 0.18f + noise * 0.055f;
    return { source, source * 0.93f };
}
#endif

} // namespace gutterpress::plugin

extern "C" yup::AudioProcessor* createPluginProcessor()
{
    return new gutterpress::plugin::GutterPressPlugin();
}
