#pragma once

#include "model/ConsoleModel.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace vc
{

class RoutingBoard : public juce::Component
{
public:
    explicit RoutingBoard(ConsoleModel& modelToUse);
    ~RoutingBoard() override;

    void refresh();
    void resized() override;

private:
    class RouteGrid;
    class DelayBank;

    void showPage(int next);
    void configurePage();
    void configureDetail();
    void applyInputBlock(int row, int column);
    void assignUserInputs(int row, int column);
    void handleGrid(int row, int column, int id);
    void editSignal(int row, int id);
    void outputBankForRow(int row, int& bank, int& index) const;
    bool userInputUses(int row, int column) const;
    int gridColumnId(int column) const;
    juce::String routingPath(const char* bank, int index) const;
    juce::String outputPath(int bank, int index, const char* leaf) const;
    void paint(juce::Graphics& g) override;

    ConsoleModel& model;
    int page = 0;
    int userRow = 0;
    int defaultTap = -1;
    std::array<juce::TextButton, 11> tabs;
    juce::Viewport gridView;
    juce::Viewport detailView;
    std::unique_ptr<RouteGrid> grid;
    std::unique_ptr<RouteGrid> detail;
    std::unique_ptr<DelayBank> delayBank;
    juce::ComboBox stageboxA;
    juce::ComboBox stageboxB;
    juce::Label stageboxAReadout;
    juce::Label stageboxBReadout;
    juce::ComboBox auxRemapA;
    juce::ComboBox auxRemapB;
    juce::TextButton recordButton { "Record" };
    juce::TextButton playButton { "Play" };
    juce::ComboBox tapBox;
    juce::TextButton iqOff { "Off" };
    juce::TextButton iqA { "A" };
    juce::TextButton iqB { "B" };
    juce::Label pageNote;
    juce::Rectangle<int> lowerBounds;
    bool applying = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RoutingBoard)
};

} // namespace vc
