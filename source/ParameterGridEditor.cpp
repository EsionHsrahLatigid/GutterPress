#include "ParameterGridEditor.h"

#include "GutterPressPlugin.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace gutterpress::plugin
{
namespace
{
class CommandButton final : public yup::TextButton
{
public:
    using yup::TextButton::TextButton;

    std::function<void()> onPressed;

    void mouseUp (const yup::MouseEvent& event) override
    {
        yup::TextButton::mouseUp (event);
        if (onPressed)
            onPressed();
    }
};

class StripMeter final : public yup::Component
{
public:
    explicit StripMeter (std::uint32_t newColor)
        : color (newColor)
    {
    }

    void setLevel (float newLevel)
    {
        level = std::clamp (newLevel, 0.0f, 1.0f);
        repaint();
    }

    void paint (yup::Graphics& graphics) override
    {
        const auto bounds = getLocalBounds();
        graphics.setFillColor (0xff15171bu);
        graphics.fillRect (bounds.to<float>());

        graphics.setFillColor (0xff30342eu);
        graphics.fillRect (bounds.withTrimmedTop (bounds.getHeight() * 0.5f).to<float>());
        graphics.setFillColor (color);
        graphics.fillRect (0.0f, 0.0f, bounds.getWidth() * level, bounds.getHeight());
    }

private:
    std::uint32_t color = 0xffd9ff42u;
    float level = 0.0f;
};
} // namespace

ParameterGridEditor::ParameterGridEditor (yup::AudioProcessor& processor,
                                          yup::StringRef newTitle,
                                          yup::StringRef newWarning,
                                          std::uint32_t newAccentColor)
    : title (newTitle)
    , warning (newWarning)
    , accentColor (newAccentColor)
{
    gutterpressProcessor = dynamic_cast<GutterPressPlugin*> (&processor);

    const auto processorParameters = processor.getParameters();
    parameters.assign (processorParameters.begin(), processorParameters.end());

    titleLabel = std::make_unique<yup::Label>();
    titleLabel->setText (title, yup::dontSendNotification);
    titleLabel->setJustification (yup::Justification::centerLeft);
    addAndMakeVisible (*titleLabel);

    warningLabel = std::make_unique<yup::Label>();
    warningLabel->setText (warning, yup::dontSendNotification);
    warningLabel->setJustification (yup::Justification::centerLeft);
    addAndMakeVisible (*warningLabel);

    labels.reserve (parameters.size());
    sliders.reserve (parameters.size());
    valueLabels.reserve (parameters.size());

    for (const auto& parameter : parameters)
    {
        auto label = std::make_unique<yup::Label>();
        label->setText (parameter->getName(), yup::dontSendNotification);
        label->setJustification (yup::Justification::center);
        addAndMakeVisible (*label);
        labels.push_back (std::move (label));

        auto slider = std::make_unique<yup::Slider> (yup::Slider::RotaryVerticalDrag);
        slider->setRange (parameter->getMinimumValue(),
                          parameter->getMaximumValue(),
                          parameter->isStepped() ? 1.0 : 0.0);
        slider->setDefaultValue (parameter->getDefaultValue());
        slider->setValue (parameter->getValue(), yup::dontSendNotification);
        slider->setTextBoxStyle (yup::Slider::NoTextBox);
        slider->setPopupDisplayEnabled (false);
        slider->setMouseCursor (yup::MouseCursor::Hand);
        slider->setClickingGrabFocus (false);
        slider->onDragStart = [parameter] (const yup::MouseEvent&) { parameter->beginChangeGesture(); };
        slider->onValueChanged = [parameter] (double value)
        {
            parameter->setValueNotifyingHost (static_cast<float> (value));
        };
        slider->onDragEnd = [parameter] (const yup::MouseEvent&) { parameter->endChangeGesture(); };
        addAndMakeVisible (*slider);
        sliders.push_back (std::move (slider));

        auto valueLabel = std::make_unique<yup::Label>();
        valueLabel->setText (parameter->toString(), yup::dontSendNotification);
        valueLabel->setJustification (yup::Justification::center);
        addAndMakeVisible (*valueLabel);
        valueLabels.push_back (std::move (valueLabel));
    }

#if defined(YUP_AUDIO_PLUGIN_ENABLE_STANDALONE)
    if (gutterpressProcessor != nullptr)
    {
        auditionButton = std::make_unique<CommandButton>();
        auditionButton->setMouseCursor (yup::MouseCursor::Hand);
        auditionButton->setClickingGrabFocus (false);
        static_cast<CommandButton*> (auditionButton.get())->onPressed = [this]
        {
            gutterpressProcessor->setAuditionEnabled (! gutterpressProcessor->isAuditionEnabled());
            syncAuditionControls();
        };
        addAndMakeVisible (*auditionButton);

        auditionTypeButton = std::make_unique<CommandButton>();
        auditionTypeButton->setMouseCursor (yup::MouseCursor::Hand);
        auditionTypeButton->setClickingGrabFocus (false);
        static_cast<CommandButton*> (auditionTypeButton.get())->onPressed = [this]
        {
            gutterpressProcessor->setAuditionType (1 - gutterpressProcessor->getAuditionType());
            syncAuditionControls();
        };
        addAndMakeVisible (*auditionTypeButton);

        inputMeterLabel = std::make_unique<yup::Label>();
        inputMeterLabel->setText ("In", yup::dontSendNotification);
        inputMeterLabel->setJustification (yup::Justification::centerLeft);
        addAndMakeVisible (*inputMeterLabel);

        outputMeterLabel = std::make_unique<yup::Label>();
        outputMeterLabel->setText ("Out", yup::dontSendNotification);
        outputMeterLabel->setJustification (yup::Justification::centerLeft);
        addAndMakeVisible (*outputMeterLabel);

        inputMeter = std::make_unique<StripMeter> (0xffb8f23au);
        outputMeter = std::make_unique<StripMeter> (0xffff5a2eu);
        addAndMakeVisible (*inputMeter);
        addAndMakeVisible (*outputMeter);
        syncAuditionControls();
    }
#endif

    setSize (getPreferredSize().to<float>());
    startTimerHz (30);
}

ParameterGridEditor::~ParameterGridEditor()
{
}

bool ParameterGridEditor::isResizable() const
{
    return true;
}

bool ParameterGridEditor::shouldPreserveAspectRatio() const
{
    return true;
}

yup::Size<int> ParameterGridEditor::getPreferredSize() const
{
    return { 940, 520 };
}

void ParameterGridEditor::paint (yup::Graphics& graphics)
{
    graphics.setFillColor (0xff080907u);
    graphics.fillAll();

    graphics.setFillColor (0xff1b1e18u);
    for (float y = 14.0f; y < getHeight(); y += 18.0f)
        for (float x = 12.0f; x < getWidth(); x += 18.0f)
            graphics.fillEllipse (x, y, 4.0f, 4.0f);

    graphics.setFillColor (0xff2b2c24u);
    graphics.fillRect (0.0f, 70.0f, getWidth(), 54.0f);
    graphics.setFillColor (accentColor);
    graphics.fillRect (0.0f, 0.0f, getWidth(), 6.0f);
    graphics.fillRect (0.0f, 118.0f, getWidth(), 3.0f);
}

void ParameterGridEditor::resized()
{
    constexpr int columns = 5;
    constexpr float margin = 20.0f;
    constexpr float top = 124.0f;
    constexpr float gap = 12.0f;
    constexpr float labelHeight = 24.0f;
    constexpr float valueHeight = 24.0f;
    constexpr float controlGap = 4.0f;

    const auto bounds = getLocalBounds();
    const auto cellWidth = (bounds.getWidth() - 2.0f * margin - gap * (columns - 1)) / columns;
    const auto rows = std::max (1, static_cast<int> ((sliders.size() + columns - 1) / columns));
    const auto availableHeight = bounds.getHeight() - top - margin;
    const auto cellHeight = (availableHeight - gap * (rows - 1)) / rows;

    titleLabel->setBounds (24.0f, 12.0f, bounds.getWidth() - 48.0f, 30.0f);
    warningLabel->setBounds (24.0f, 43.0f, bounds.getWidth() - 48.0f, 24.0f);

#if defined(YUP_AUDIO_PLUGIN_ENABLE_STANDALONE)
    if (auditionButton != nullptr && auditionTypeButton != nullptr && inputMeter != nullptr && outputMeter != nullptr)
    {
        constexpr float buttonWidth = 118.0f;
        constexpr float typeWidth = 96.0f;
        constexpr float controlHeight = 32.0f;
        const auto meterX = margin + buttonWidth + typeWidth + gap * 2.0f;
        const auto meterWidth = std::max (90.0f, (bounds.getWidth() - margin - meterX - gap) * 0.5f);

        auditionButton->setBounds (margin, 80.0f, buttonWidth, controlHeight);
        auditionTypeButton->setBounds (margin + buttonWidth + gap, 80.0f, typeWidth, controlHeight);
        inputMeterLabel->setBounds (meterX, 75.0f, 40.0f, 20.0f);
        inputMeter->setBounds (meterX + 38.0f, 80.0f, meterWidth - 38.0f, 14.0f);
        outputMeterLabel->setBounds (meterX + meterWidth + gap, 75.0f, 40.0f, 20.0f);
        outputMeter->setBounds (meterX + meterWidth + gap + 42.0f, 80.0f, meterWidth - 42.0f, 14.0f);
    }
#endif

    for (std::size_t i = 0; i < sliders.size(); ++i)
    {
        const auto column = static_cast<int> (i) % columns;
        const auto row = static_cast<int> (i) / columns;
        const auto x = margin + column * (cellWidth + gap);
        const auto y = top + row * (cellHeight + gap);
        const auto controlHeight = cellHeight - labelHeight - valueHeight - 2.0f * controlGap;
        const auto controlSize = std::max (20.0f, std::min (cellWidth - 8.0f, controlHeight));
        const auto controlX = x + 0.5f * (cellWidth - controlSize);
        const auto controlY = y + labelHeight + controlGap;

        labels[i]->setBounds (x, y, cellWidth, labelHeight);
        sliders[i]->setBounds (controlX, controlY, controlSize, controlSize);
        valueLabels[i]->setBounds (x, y + cellHeight - valueHeight, cellWidth, valueHeight);
    }
}

void ParameterGridEditor::focusLost()
{
    yup::AudioProcessorEditor::focusLost();
}

void ParameterGridEditor::keyDown (const yup::KeyPress& key, const yup::Point<float>& position)
{
    yup::AudioProcessorEditor::keyDown (key, position);

}

void ParameterGridEditor::keyUp (const yup::KeyPress& key, const yup::Point<float>& position)
{
    yup::AudioProcessorEditor::keyUp (key, position);

}

void ParameterGridEditor::timerCallback()
{
    for (std::size_t i = 0; i < sliders.size(); ++i)
    {
        if (! sliders[i]->isCurrentlyBeingDragged())
            sliders[i]->setValue (parameters[i]->getValue(), yup::dontSendNotification);
        valueLabels[i]->setText (parameters[i]->toString(), yup::dontSendNotification);
    }

#if defined(YUP_AUDIO_PLUGIN_ENABLE_STANDALONE)
    if (gutterpressProcessor != nullptr && inputMeter != nullptr && outputMeter != nullptr)
    {
        const auto latestInputPeak = gutterpressProcessor->getInputPeakLevel();
        const auto latestPeak = gutterpressProcessor->getOutputPeakLevel();
        displayedInputPeak = std::max (latestInputPeak, displayedInputPeak * 0.82f);
        displayedPeak = std::max (latestPeak, displayedPeak * 0.82f);
        static_cast<StripMeter*> (inputMeter.get())->setLevel (displayedInputPeak);
        static_cast<StripMeter*> (outputMeter.get())->setLevel (displayedPeak);
    }
#endif
}

void ParameterGridEditor::syncAuditionControls()
{
#if defined(YUP_AUDIO_PLUGIN_ENABLE_STANDALONE)
    if (gutterpressProcessor == nullptr || auditionButton == nullptr || auditionTypeButton == nullptr)
        return;

    auditionButton->setButtonText (gutterpressProcessor->isAuditionEnabled() ? "Audition On" : "Audition Off");
    auditionTypeButton->setButtonText (gutterpressProcessor->getAuditionType() == 0 ? "Saw" : "Pulse");
#endif
}

} // namespace gutterpress::plugin
