#pragma once

// SPDX-License-Identifier: AGPL-3.0-or-later

#include "model/ConsoleModel.h"

#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

namespace vc
{

class FxEditor : public juce::Component
{
public:
    explicit FxEditor(ConsoleModel& modelToUse);
    ~FxEditor() override;

    void setReturn(int returnIndex);
    void refresh();
    void resized() override;

private:
    class Picture;

    ConsoleModel& model;
    int slot = 0;
    int fxReturn = 0;
    bool applying = false;
    juce::Label title;
    juce::Label sendLabel;
    juce::ComboBox typeBox;
    juce::TextButton enabled { "On" };
    std::unique_ptr<Picture> picture;
    std::array<juce::Label, 4> paramLabels;
    std::array<juce::Label, 4> readouts;
    std::array<juce::Slider, 4> params;

    void applyType(int type);
    void showParameters(int type);
    void pushPicture();
};

} // namespace vc
