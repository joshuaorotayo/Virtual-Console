#include "ui/RoutingBoard.h"

#include <vector>

namespace vc
{
namespace
{
juce::Colour ink() { return juce::Colour(0xffc9d0d8); }
juce::Colour muted() { return juce::Colour(0xff8b949e); }
juce::Colour gridLine() { return juce::Colour(0xff2c333b); }
juce::Colour gridBack() { return juce::Colour(0xff101418); }
juce::Colour headerBack() { return juce::Colour(0xff1a1f26); }
juce::Colour orangeMark() { return juce::Colour(0xfff08a24); }
juce::Colour cyanMark() { return juce::Colour(0xff3ce0e0); }

juce::String twoDigits(int index)
{
    return juce::String(index + 1).paddedLeft('0', 2);
}

juce::String blockLabel(int offset)
{
    return juce::String(offset * 8 + 1) + "-" + juce::String(offset * 8 + 8);
}

int blockInputStart(int column)
{
    if (column < 0)
        return -1;

    if (column < 4)
        return column * 8;

    if (column < 10)
        return aes50AInputBase + (column - 4) * 8;

    if (column < 16)
        return aes50BInputBase + (column - 10) * 8;

    if (column < 20)
        return cardInputBase + (column - 16) * 8;

    return -1;
}

juce::Colour tapColour(int tap)
{
    switch (tap & 7)
    {
        case 0: return juce::Colour(0xff3dce4a);
        case 1: return juce::Colour(0xffffe14a);
        case 2: return juce::Colour(0xff3b82f6);
        case 4: return juce::Colour(0xffe23b3b);
        default: return juce::Colour(0xfff472b6);
    }
}

const char* tapLabel(int tap)
{
    switch (tap & 7)
    {
        case 0: return "Pre EQ";
        case 1: return "Post EQ";
        case 2: return "Pre Fader";
        case 4: return "Input";
        default: return "Post Fader";
    }
}

int nextTap(int tap)
{
    static const int order[] = { 4, 4 | 8, 0, 0 | 8, 1, 1 | 8, 2, 2 | 8, 3, 3 | 8 };

    for (int i = 0; i < 10; ++i)
        if (order[i] == tap)
            return order[(i + 1) % 10];

    return 3 | 8;
}

bool sameSignal(int stored, int columnId)
{
    if (stored == columnId)
        return true;

    if (stored >= outputBusBase && stored < outputMatrixBase)
        return columnId == outputBusMonoBase + (stored - outputBusBase) / 2;

    if (stored >= outputMatrixBase && stored < outputDirectBase)
        return columnId == outputMatrixMonoBase + (stored - outputMatrixBase) / 2;

    return false;
}

struct ColSpec
{
    juce::String label;
    int id = 0;
};

struct GroupSpec
{
    juce::String title;
    std::vector<ColSpec> columns;
};

void addBlocks(std::vector<GroupSpec>& groups, const juce::String& title, int count, int firstId)
{
    GroupSpec group;
    group.title = title;

    for (int i = 0; i < count; ++i)
        group.columns.push_back({ blockLabel(i), firstId + i });

    groups.push_back(std::move(group));
}

std::vector<GroupSpec> inputGroups()
{
    std::vector<GroupSpec> groups;
    addBlocks(groups, "Local", 4, 0);
    addBlocks(groups, "AES50-A", 6, 4);
    addBlocks(groups, "AES50-B", 6, 10);
    addBlocks(groups, "Card", 4, 16);
    addBlocks(groups, "User", 2, 20);
    return groups;
}

std::vector<GroupSpec> connectorGroups()
{
    std::vector<GroupSpec> groups;
    addBlocks(groups, "Local", 4, 0);
    addBlocks(groups, "AES50-A", 6, 4);
    addBlocks(groups, "AES50-B", 6, 10);
    addBlocks(groups, "Card", 4, 16);
    addBlocks(groups, "Out", 2, 20);
    addBlocks(groups, "Ultranet", 2, 22);
    GroupSpec aux;
    aux.title = "Aux";
    aux.columns.push_back({ "1-6/M", 24 });
    groups.push_back(std::move(aux));
    GroupSpec auxIn;
    auxIn.title = "Auxin";
    auxIn.columns.push_back({ "1-6/T", 25 });
    groups.push_back(std::move(auxIn));
    return groups;
}

std::vector<GroupSpec> signalGroups()
{
    std::vector<GroupSpec> groups;
    groups.push_back({ "Phase", { { {}, -2 } } });
    groups.push_back({ "Off", { { {}, outputOff } } });
    groups.push_back({ "Main", { { "L", outputMainL }, { "R", outputMainR }, { "MC", outputMainC } } });
    GroupSpec buses;
    buses.title = "Mix Bus";

    for (int i = 0; i < numBuses; ++i)
        buses.columns.push_back({ twoDigits(i), outputBusMonoBase + i });

    groups.push_back(std::move(buses));
    GroupSpec matrices;
    matrices.title = "Matrix";

    for (int i = 0; i < numMatrices; ++i)
        matrices.columns.push_back({ juce::String(i + 1), outputMatrixMonoBase + i });

    groups.push_back(std::move(matrices));
    GroupSpec direct;
    direct.title = "Direct";

    for (int i = 0; i < numChannels; ++i)
        direct.columns.push_back({ twoDigits(i), outputDirectBase + i });

    groups.push_back(std::move(direct));
    return groups;
}

std::vector<GroupSpec> userInputDetailGroups()
{
    std::vector<GroupSpec> groups;
    groups.push_back({ "Off", { { {}, -1 } } });
    GroupSpec local;
    local.title = "Local";

    for (int i = 0; i < numLocalInputs; ++i)
        local.columns.push_back({ juce::String(i + 1), i });

    groups.push_back(std::move(local));
    GroupSpec aesA;
    aesA.title = "AES50-A";

    for (int i = 0; i < numAes50Channels; ++i)
        aesA.columns.push_back({ juce::String(i + 1), aes50AInputBase + i });

    groups.push_back(std::move(aesA));
    GroupSpec aesB;
    aesB.title = "AES50-B";

    for (int i = 0; i < numAes50Channels; ++i)
        aesB.columns.push_back({ juce::String(i + 1), aes50BInputBase + i });

    groups.push_back(std::move(aesB));
    GroupSpec card;
    card.title = "Card";

    for (int i = 0; i < numCardChannels; ++i)
        card.columns.push_back({ juce::String(i + 1), cardInputBase + i });

    groups.push_back(std::move(card));
    return groups;
}
} // namespace

class RoutingBoard::RouteGrid : public juce::Component
{
public:
    void setContent(juce::String cornerName, std::vector<juce::String> rowNames, std::vector<GroupSpec> groupSpecs, int width)
    {
        corner = std::move(cornerName);
        rows = std::move(rowNames);
        groups = std::move(groupSpecs);
        columnWidth = width;
        layoutColumns();
        setSize(contentWidth(), contentHeight());
        repaint();
    }

    std::function<bool(int, int)> isSelected;
    std::function<juce::Colour(int, int)> colourFor;
    std::function<bool(int, int)> filledFor;
    std::function<void(int, int, int)> onCell;

    int contentWidth() const { return std::max(1, usedWidth); }
    int contentHeight() const { return headerHeight + (int) rows.size() * rowHeight; }
    int columnCount() const { return (int) placed.size(); }

    void setNameWidth(int width)
    {
        rowHeader = juce::jmax(72, width);
        layoutColumns();
        setSize(std::max(1, contentWidth()), std::max(1, contentHeight()));
        repaint();
    }

    void fitToWidth(int available)
    {
        if (placed.empty() || available <= rowHeader + 30)
            return;

        const int fitted = juce::jlimit(32, 52, (available - rowHeader) / (int) placed.size());

        if (fitted == columnWidth)
            return;

        columnWidth = fitted;
        layoutColumns();
        setSize(std::max(1, contentWidth()), std::max(1, contentHeight()));
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(gridBack());
        g.setColour(headerBack());
        g.fillRect(0, 0, getWidth(), headerHeight);
        g.setColour(headerBack());
        g.fillRect(0, 0, rowHeader, getHeight());

        for (const auto& group : placedGroups)
        {
            g.setColour(juce::Colour(0xff242a32));
            g.fillRect(group.x, 0, group.w, 18);
            g.setColour(ink());
            g.setFont(juce::Font(juce::FontOptions(11.0f)));
            g.drawText(group.title, group.x, 0, group.w, 18, juce::Justification::centred);
        }

        g.setColour(muted());
        g.setFont(juce::Font(juce::FontOptions(10.0f)));

        for (const auto& column : placed)
            g.drawText(column.label, column.x, 18, column.w, 20, juce::Justification::centred);

        g.setColour(ink());
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawText(corner, 4, 0, rowHeader - 8, headerHeight, juce::Justification::centredLeft);

        for (int row = 0; row < (int) rows.size(); ++row)
        {
            const int y = headerHeight + row * rowHeight;
            g.setColour(row == hotRow ? juce::Colour(0xff1c2830) : gridBack());
            g.fillRect(rowHeader, y, getWidth() - rowHeader, rowHeight);
            g.setColour(ink());
            g.drawText(rows[(size_t) row], 6, y, rowHeader - 10, rowHeight, juce::Justification::centredLeft);

            for (int col = 0; col < (int) placed.size(); ++col)
            {
                if (isSelected == nullptr || ! isSelected(row, col))
                    continue;

                const auto colour = colourFor != nullptr ? colourFor(row, col) : orangeMark();
                const bool filled = filledFor == nullptr || filledFor(row, col);
                auto cell = juce::Rectangle<int>(placed[(size_t) col].x, y, placed[(size_t) col].w, rowHeight).reduced(6, 6);

                if (pills)
                {
                    g.setColour(colour);
                    g.fillRoundedRectangle(cell.withSizeKeepingCentre(18, 10).toFloat(), 5.0f);
                }
                else
                {
                    auto dot = cell.withSizeKeepingCentre(12, 12).toFloat();
                    g.setColour(colour);

                    if (filled)
                        g.fillEllipse(dot);
                    else
                        g.drawEllipse(dot.reduced(1.0f), 1.6f);
                }
            }
        }

        g.setColour(gridLine());
        g.drawRect(0, 0, rowHeader, getHeight());
        g.drawHorizontalLine(headerHeight - 1, 0.0f, (float) getWidth());

        for (const auto& column : placed)
            g.drawVerticalLine(column.x, 0.0f, (float) getHeight());

        if (! placed.empty())
            g.drawVerticalLine(placed.back().x + placed.back().w, 0.0f, (float) getHeight());

        for (int row = 0; row <= (int) rows.size(); ++row)
            g.drawHorizontalLine(headerHeight + row * rowHeight, 0.0f, (float) getWidth());
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        const int row = (event.y - headerHeight) / rowHeight;

        if (row < 0 || row >= (int) rows.size() || onCell == nullptr)
            return;

        hotRow = row;

        for (int col = 0; col < (int) placed.size(); ++col)
        {
            const auto& column = placed[(size_t) col];

            if (event.x >= column.x && event.x < column.x + column.w)
            {
                onCell(row, col, column.id);
                repaint();
                return;
            }
        }
    }

    int idAt(int column) const
    {
        if (column < 0 || column >= (int) placed.size())
            return -999;

        return placed[(size_t) column].id;
    }

    bool pills = true;

private:
    struct Placed
    {
        int x = 0;
        int w = 0;
        int id = 0;
        juce::String label;
        juce::String title;
    };

    struct PlacedGroup
    {
        juce::String title;
        int x = 0;
        int w = 0;
    };

    void layoutColumns()
    {
        placed.clear();
        placedGroups.clear();
        int x = rowHeader;

        for (const auto& group : groups)
        {
            PlacedGroup placedGroup;
            placedGroup.title = group.title;
            placedGroup.x = x;

            for (const auto& column : group.columns)
            {
                placed.push_back({ x, columnWidth, column.id, column.label, group.title });
                x += columnWidth;
            }

            placedGroup.w = x - placedGroup.x;
            placedGroups.push_back(placedGroup);
        }

        usedWidth = std::max(rowHeader + columnWidth, x);
    }

    juce::String corner;
    std::vector<juce::String> rows;
    std::vector<GroupSpec> groups;
    std::vector<Placed> placed;
    std::vector<PlacedGroup> placedGroups;
    int columnWidth = 36;
    int usedWidth = 1;
    int hotRow = -1;
    int rowHeader = 118;
    static constexpr int headerHeight = 40;
    static constexpr int rowHeight = 24;
};

class RoutingBoard::DelayBank : public juce::Component
{
public:
    explicit DelayBank(ConsoleModel& modelToUse)
        : model(modelToUse)
    {
        for (int i = 0; i < numLocalOutputs; ++i)
        {
            auto& slider = sliders[(size_t) i];
            slider.setSliderStyle(juce::Slider::LinearVertical);
            slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
            slider.setRange(0.0, 500.0, 0.1);
            slider.setScrollWheelEnabled(false);
            slider.setDoubleClickReturnValue(true, 0.0);
            const int index = i;
            slider.onValueChange = [this, index]
            {
                if (! applying)
                    model.setFloat("/outputs/main/" + twoDigits(index) + "/delay", (float) sliders[(size_t) index].getValue());
            };
            addAndMakeVisible(slider);
        }
    }

    void refresh()
    {
        applying = true;

        for (int i = 0; i < numLocalOutputs; ++i)
        {
            if (sliders[(size_t) i].isMouseButtonDown())
                continue;

            float milliseconds = 0.0f;
            model.getFloat("/outputs/main/" + twoDigits(i) + "/delay", milliseconds);
            sliders[(size_t) i].setValue(milliseconds, juce::dontSendNotification);
        }

        applying = false;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(gridBack());
        g.setFont(juce::Font(juce::FontOptions(12.0f)));

        for (int i = 0; i < numLocalOutputs; ++i)
        {
            const float milliseconds = (float) sliders[(size_t) i].getValue();
            const float metres = milliseconds * 0.343f;
            const float feet = metres * 3.28084f;
            auto column = columnBounds(i);
            g.setColour(muted());
            g.drawText(juce::String(feet, 1) + " ft", column.removeFromTop(16), juce::Justification::centred);
            g.drawText(juce::String(metres, 2) + " m", column.removeFromTop(16), juce::Justification::centred);
            g.setColour(cyanMark());
            g.drawText(juce::String(milliseconds, 1) + " ms", column.removeFromTop(16), juce::Justification::centred);
            g.setColour(ink());
            g.drawText("Output " + juce::String(i + 1), columnBounds(i).removeFromBottom(18), juce::Justification::centred);
        }
    }

    void resized() override
    {
        for (int i = 0; i < numLocalOutputs; ++i)
        {
            auto column = columnBounds(i);
            column.removeFromTop(48);
            column.removeFromBottom(20);
            sliders[(size_t) i].setBounds(column.withSizeKeepingCentre(28, column.getHeight()));
        }
    }

private:
    juce::Rectangle<int> columnBounds(int index) const
    {
        const int row = index / 8;
        const int col = index % 8;
        const int width = getWidth() / 8;
        const int height = getHeight() / 2;
        return juce::Rectangle<int>(col * width, row * height, width, height).reduced(4, 6);
    }

    ConsoleModel& model;
    std::array<juce::Slider, numLocalOutputs> sliders;
    bool applying = false;
};

RoutingBoard::RoutingBoard(ConsoleModel& modelToUse)
    : model(modelToUse)
{
    static const char* names[] = {
        "Inputs", "AES50", "Card", "XLR", "Out 1-16", "Out Delay",
        "Aux Out", "Ultranet", "iQ Setup", "User In", "User Out"
    };

    for (int i = 0; i < 11; ++i)
    {
        tabs[(size_t) i].setButtonText(names[i]);
        tabs[(size_t) i].setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1a1f26));
        tabs[(size_t) i].setColour(juce::TextButton::textColourOffId, muted());
        const int index = i;
        tabs[(size_t) i].onClick = [this, index] { showPage(index); };
        addAndMakeVisible(tabs[(size_t) i]);
    }

    grid = std::make_unique<RouteGrid>();
    detail = std::make_unique<RouteGrid>();
    gridView.setViewedComponent(grid.get(), false);
    gridView.setScrollBarsShown(true, true);
    addAndMakeVisible(gridView);
    detailView.setViewedComponent(detail.get(), false);
    detailView.setScrollBarsShown(true, true);
    addChildComponent(detailView);

    delayBank = std::make_unique<DelayBank>(model);
    addChildComponent(*delayBank);

    static const char* boxes[] = { "None", "S16", "S32", "SD8", "SD16", "DL16", "DL32" };
    static const char* boxIds[] = { "none", "S16", "S32", "SD8", "SD16", "DL16", "DL32" };

    for (int i = 0; i < 7; ++i)
    {
        stageboxA.addItem(boxes[i], i + 1);
        stageboxB.addItem(boxes[i], i + 1);
    }

    stageboxA.onChange = [this]
    {
        if (! applying && stageboxA.getSelectedId() > 0)
            model.setString("/config/aes50/A/box", boxIds[stageboxA.getSelectedId() - 1]);
    };
    stageboxB.onChange = [this]
    {
        if (! applying && stageboxB.getSelectedId() > 0)
            model.setString("/config/aes50/B/box", boxIds[stageboxB.getSelectedId() - 1]);
    };
    addChildComponent(stageboxA);
    addChildComponent(stageboxB);
    stageboxAReadout.setColour(juce::Label::textColourId, muted());
    stageboxBReadout.setColour(juce::Label::textColourId, muted());
    addChildComponent(stageboxAReadout);
    addChildComponent(stageboxBReadout);

    for (auto* box : { &auxRemapA, &auxRemapB })
    {
        box->addItem("Aux Ins", 1);
        box->addItem("Local", 2);
        box->addItem("Card", 3);
        box->addItem("AES50 A", 4);
        addChildComponent(*box);
    }

    auxRemapA.onChange = [this]
    {
        if (! applying)
            model.setInt("/config/routing/auxremap/a", std::max(0, auxRemapA.getSelectedId() - 1));
    };
    auxRemapB.onChange = [this]
    {
        if (! applying)
            model.setInt("/config/routing/auxremap/b", std::max(0, auxRemapB.getSelectedId() - 1));
    };

    recordButton.setClickingTogglesState(true);
    playButton.setClickingTogglesState(true);
    recordButton.setRadioGroupId(1901);
    playButton.setRadioGroupId(1901);
    recordButton.onClick = [this]
    {
        if (! applying && recordButton.getToggleState())
            model.setInt("/config/routing/auxremap/mode", 0);
    };
    playButton.onClick = [this]
    {
        if (! applying && playButton.getToggleState())
            model.setInt("/config/routing/auxremap/mode", 1);
    };
    addChildComponent(recordButton);
    addChildComponent(playButton);

    tapBox.addItem("keep active tap", 1);
    tapBox.addItem("Input", 2);
    tapBox.addItem("Input + Mute", 3);
    tapBox.addItem("Pre EQ", 4);
    tapBox.addItem("Pre EQ + Mute", 5);
    tapBox.addItem("Post EQ", 6);
    tapBox.addItem("Post EQ + Mute", 7);
    tapBox.addItem("Pre Fader", 8);
    tapBox.addItem("Pre Fader + Mute", 9);
    tapBox.addItem("Post Fader", 10);
    tapBox.addItem("Post Fader + Mute", 11);
    tapBox.setSelectedId(1, juce::dontSendNotification);
    tapBox.onChange = [this]
    {
        static const int values[] = { -1, 4, 4 | 8, 0, 0 | 8, 1, 1 | 8, 2, 2 | 8, 3, 3 | 8 };
        const int id = tapBox.getSelectedId();
        defaultTap = id >= 1 && id <= 11 ? values[id - 1] : -1;
    };
    addChildComponent(tapBox);

    for (auto* button : { &iqOff, &iqA, &iqB })
    {
        button->setClickingTogglesState(true);
        button->setRadioGroupId(1902);
        addChildComponent(*button);
    }

    iqOff.onClick = [this] { if (! applying) model.setInt("/config/routing/iq/group", 0); };
    iqA.onClick = [this] { if (! applying) model.setInt("/config/routing/iq/group", 1); };
    iqB.onClick = [this] { if (! applying) model.setInt("/config/routing/iq/group", 2); };
    pageNote.setColour(juce::Label::textColourId, muted());
    pageNote.setFont(juce::Font(juce::FontOptions(12.0f)));
    addAndMakeVisible(pageNote);
    showPage(0);
}

void RoutingBoard::showPage(int next)
{
    page = juce::jlimit(0, 10, next);

    for (int i = 0; i < 11; ++i)
    {
        const bool on = i == page;
        tabs[(size_t) i].setColour(juce::TextButton::textColourOffId, on ? cyanMark() : muted());
        tabs[(size_t) i].setColour(juce::TextButton::buttonColourId, on ? juce::Colour(0xff14343a) : juce::Colour(0xff1a1f26));
    }

    configurePage();
    resized();
    refresh();
}

juce::String RoutingBoard::routingPath(const char* bank, int index) const
{
    return juce::String("/config/routing/") + bank + "/" + twoDigits(index);
}

void RoutingBoard::applyInputBlock(int row, int column)
{
    model.setInt(routingPath("in", row), column);
    const int start = blockInputStart(column);

    for (int i = 0; i < 8; ++i)
    {
        int source = -1;

        if (column >= 20)
            model.getInt(routingPath("userin", (column - 20) * 8 + i), source);
        else if (start >= 0)
            source = start + i;

        model.setChannelSource(row * 8 + i, source);
    }
}

void RoutingBoard::assignUserInputs(int row, int column)
{
    const int start = blockInputStart(column);

    for (int i = 0; i < 8 && start >= 0; ++i)
        model.setInt(routingPath("userin", row * 8 + i), start + i);

    for (int block = 0; block < 4; ++block)
    {
        int chosen = -1;
        model.getInt(routingPath("in", block), chosen);

        if (chosen >= 20)
            applyInputBlock(block, chosen);
    }
}

void RoutingBoard::configurePage()
{
    const bool signal = page == 4 || page == 6 || page == 7;
    grid->pills = ! signal;

    if (page == 5 || page == 8)
        return;

    std::vector<juce::String> rows;
    std::vector<GroupSpec> groups = signal ? signalGroups() : page == 0 || page == 9 ? inputGroups() : connectorGroups();
    juce::String corner = "Output";

    if (page == 0)
    {
        corner = "Channel";

        for (int i = 0; i < 4; ++i)
            rows.push_back("Channel " + juce::String(i * 8 + 1) + "-" + juce::String(i * 8 + 8));
    }
    else if (page == 1)
    {
        corner = "Output";

        for (int side = 0; side < 2; ++side)
            for (int i = 0; i < 6; ++i)
                rows.push_back(juce::String(side == 0 ? "AES50-A " : "AES50-B ") + blockLabel(i));
    }
    else if (page == 2)
    {
        for (int i = 0; i < 4; ++i)
            rows.push_back("Outputs " + blockLabel(i));
    }
    else if (page == 3)
    {
        for (int i = 0; i < 4; ++i)
            rows.push_back("Outputs " + juce::String(i * 4 + 1) + "-" + juce::String(i * 4 + 4));
    }
    else if (page == 4)
    {
        for (int i = 0; i < numLocalOutputs; ++i)
            rows.push_back(juce::String(i + 1) + ": Output " + juce::String(i + 1));
    }
    else if (page == 6)
    {
        corner = "Aux Out";

        for (int i = 0; i < numAuxOutputs; ++i)
            rows.push_back(juce::String(i + 1));

        rows.push_back("AES/EBU L");
        rows.push_back("AES/EBU R");
    }
    else if (page == 7)
    {
        for (int i = 0; i < numP16Outputs; ++i)
            rows.push_back(juce::String(i + 1));
    }
    else if (page == 9)
    {
        corner = "Input";

        for (int i = 0; i < 4; ++i)
            rows.push_back("Inputs " + blockLabel(i));
    }
    else if (page == 10)
    {
        for (int i = 0; i < 6; ++i)
            rows.push_back("Outputs " + blockLabel(i));
    }

    grid->setContent(corner, rows, groups, signal ? 28 : 46);
    grid->isSelected = [this, signal](int row, int column)
    {
        if (page == 0)
        {
            int chosen = -1;
            model.getInt(routingPath("in", row), chosen);
            return gridColumnId(column) == chosen;
        }

        if (page == 9)
            return userInputUses(row, gridColumnId(column));

        if (page == 1 || page == 2 || page == 3 || page == 10)
        {
            int chosen = -1;
            const char* bank = page == 1 ? "aes50" : page == 2 ? "card" : page == 3 ? "xlr" : "userbank";
            model.getInt(routingPath(bank, row), chosen);
            return gridColumnId(column) == chosen;
        }

        if (! signal)
            return false;

        int bank = 0;
        int index = row;
        outputBankForRow(row, bank, index);
        const int id = gridColumnId(column);

        if (id == -2)
        {
            int phase = 0;
            model.getInt(outputPath(bank, index, "phase"), phase);
            return phase != 0;
        }

        int source = outputOff;
        model.getInt(outputPath(bank, index, "src"), source);
        return sameSignal(source, id);
    };
    grid->colourFor = [this, signal](int row, int column)
    {
        if (! signal || gridColumnId(column) == -2)
            return orangeMark();

        int bank = 0;
        int index = row;
        outputBankForRow(row, bank, index);
        int tap = 3 | 8;
        model.getInt(outputPath(bank, index, "tap"), tap);
        return tapColour(tap);
    };
    grid->filledFor = [this, signal](int row, int column)
    {
        if (! signal || gridColumnId(column) == -2)
            return true;

        int bank = 0;
        int index = row;
        outputBankForRow(row, bank, index);
        int tap = 3 | 8;
        model.getInt(outputPath(bank, index, "tap"), tap);
        return (tap & 8) != 0;
    };
    grid->onCell = [this](int row, int column, int id) { handleGrid(row, column, id); };

    if (page == 9 || page == 10)
        configureDetail();
}

int RoutingBoard::gridColumnId(int column) const
{
    return grid->idAt(column);
}

void RoutingBoard::outputBankForRow(int row, int& bank, int& index) const
{
    bank = 0;
    index = row;

    if (page == 6)
    {
        bank = row < numAuxOutputs ? 1 : 3;
        index = row < numAuxOutputs ? row : row - numAuxOutputs;
    }
    else if (page == 7)
    {
        bank = 2;
        index = row;
    }
}

juce::String RoutingBoard::outputPath(int bank, int index, const char* leaf) const
{
    const char* name = bank == 1 ? "aux" : bank == 2 ? "p16" : bank == 3 ? "aes" : "main";
    return "/outputs/" + juce::String(name) + "/" + twoDigits(index) + "/" + leaf;
}

bool RoutingBoard::userInputUses(int row, int column) const
{
    const int start = blockInputStart(column);

    if (start < 0)
        return false;

    for (int i = 0; i < 8; ++i)
    {
        int source = -1;
        model.getInt(routingPath("userin", row * 8 + i), source);

        if (source >= start && source < start + 8)
            return true;
    }

    return false;
}

void RoutingBoard::handleGrid(int row, int column, int id)
{
    juce::ignoreUnused(column);

    if (page == 0)
    {
        applyInputBlock(row, id);
        return;
    }

    if (page == 9)
    {
        userRow = row;
        assignUserInputs(row, id);
        configureDetail();
        return;
    }

    if (page == 10)
    {
        userRow = row;
        model.setInt(routingPath("userbank", row), id);
        configureDetail();
        return;
    }

    if (page == 1 || page == 2 || page == 3)
    {
        const char* bank = page == 1 ? "aes50" : page == 2 ? "card" : "xlr";
        model.setInt(routingPath(bank, row), id);
        return;
    }

    if (page == 4 || page == 6 || page == 7)
        editSignal(row, id);
}

void RoutingBoard::editSignal(int row, int id)
{
    int bank = 0;
    int index = row;
    outputBankForRow(row, bank, index);
    const auto path = [this, bank, index](const char* leaf) { return outputPath(bank, index, leaf); };

    if (id == -2)
    {
        int phase = 0;
        model.getInt(path("phase"), phase);
        model.setInt(path("phase"), phase == 0 ? 1 : 0);
        return;
    }

    int source = outputOff;
    model.getInt(path("src"), source);

    if (sameSignal(source, id))
    {
        int tap = 3 | 8;
        model.getInt(path("tap"), tap);
        model.setInt(path("tap"), nextTap(tap));
        return;
    }

    model.setInt(path("src"), id);

    if (defaultTap >= 0)
        model.setInt(path("tap"), defaultTap);
}

void RoutingBoard::configureDetail()
{
    std::vector<juce::String> rows;
    const int origin = userRow * 8;

    if (page == 9)
    {
        for (int i = 0; i < 8; ++i)
        {
            int source = -1;
            model.getInt(routingPath("userin", origin + i), source);
            rows.push_back("Input " + juce::String(origin + i + 1) + "   " + model.inputName(source));
        }

        detail->pills = true;
        detail->setContent("Input", rows, userInputDetailGroups(), 28);
        detail->setNameWidth(220);
        detail->isSelected = [this, origin](int row, int column)
        {
            int source = -1;
            model.getInt(routingPath("userin", origin + row), source);
            return detail->idAt(column) == source;
        };
        detail->colourFor = [](int, int) { return orangeMark(); };
        detail->filledFor = [](int, int) { return true; };
        detail->onCell = [this, origin](int row, int, int id)
        {
            model.setInt(routingPath("userin", origin + row), id);

            for (int block = 0; block < 4; ++block)
            {
                int chosen = -1;
                model.getInt(routingPath("in", block), chosen);

                if (chosen >= 20)
                    applyInputBlock(block, chosen);
            }

            configureDetail();
            refresh();
        };
        resized();
        return;
    }

    for (int i = 0; i < 8; ++i)
    {
        int source = -1;
        model.getInt(routingPath("userout", origin + i), source);
        rows.push_back("Output " + juce::String(origin + i + 1) + "   " + model.outputSourceName(source));
    }

    detail->pills = false;
    detail->setContent("Output", rows, signalGroups(), 28);
    detail->setNameWidth(220);
    detail->isSelected = [this, origin](int row, int column)
    {
        int source = -1;
        model.getInt(routingPath("userout", origin + row), source);

        if (source < 0)
            source = outputOff;

        const int id = detail->idAt(column);
        return id == -2 ? false : sameSignal(source, id);
    };
    detail->colourFor = [this](int, int column)
    {
        const int id = detail->idAt(column);
        return id == outputOff || id == -2 ? orangeMark() : juce::Colour(0xfff472b6);
    };
    detail->filledFor = [](int, int) { return true; };
    detail->onCell = [this, origin](int row, int, int id)
    {
        if (id != -2)
            model.setInt(routingPath("userout", origin + row), id == outputOff ? -1 : id);

        configureDetail();
    };

    resized();
}

void RoutingBoard::refresh()
{
    applying = true;
    auto selectBox = [this](juce::ComboBox& box, const juce::String& path)
    {
        if (box.isPopupActive())
            return;

        juce::String name;
        model.getString(path + "/box", name);
        static const char* ids[] = { "none", "S16", "S32", "SD8", "SD16", "DL16", "DL32" };
        int selected = 1;

        for (int i = 0; i < 7; ++i)
            if (name.equalsIgnoreCase(ids[i]))
                selected = i + 1;

        box.setSelectedId(selected, juce::dontSendNotification);
        int inputs = 0;
        int outputs = 0;
        model.getInt(path + "/inputs", inputs);
        model.getInt(path + "/outputs", outputs);
        (path.endsWith("A") ? stageboxAReadout : stageboxBReadout)
            .setText(name + "   " + juce::String(inputs) + " in / " + juce::String(outputs) + " out",
                     juce::dontSendNotification);
    };
    selectBox(stageboxA, "/config/aes50/A");
    selectBox(stageboxB, "/config/aes50/B");

    if (! auxRemapA.isPopupActive())
    {
        int value = 0;
        model.getInt("/config/routing/auxremap/a", value);
        auxRemapA.setSelectedId(value + 1, juce::dontSendNotification);
    }

    if (! auxRemapB.isPopupActive())
    {
        int value = 0;
        model.getInt("/config/routing/auxremap/b", value);
        auxRemapB.setSelectedId(value + 1, juce::dontSendNotification);
    }

    int mode = 1;
    model.getInt("/config/routing/auxremap/mode", mode);
    recordButton.setToggleState(mode == 0, juce::dontSendNotification);
    playButton.setToggleState(mode != 0, juce::dontSendNotification);
    int group = 0;
    model.getInt("/config/routing/iq/group", group);
    iqOff.setToggleState(group == 0, juce::dontSendNotification);
    iqA.setToggleState(group == 1, juce::dontSendNotification);
    iqB.setToggleState(group == 2, juce::dontSendNotification);
    delayBank->refresh();
    grid->repaint();
    detail->repaint();
    applying = false;
    repaint();
}

void RoutingBoard::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff14181d));

    if (lowerBounds.isEmpty())
        return;

    if (page == 0)
    {
        g.setColour(ink());
        g.setFont(juce::Font(juce::FontOptions(13.0f)));
        g.drawText("Aux In Remap", lowerBounds.getX(), lowerBounds.getY(), 110, 24, juce::Justification::centredLeft);
        g.drawText("Connected Devices", lowerBounds.getX(), lowerBounds.getY() + 36, 200, 20, juce::Justification::centredLeft);
        auto panels = lowerBounds.withTrimmedTop(60);
        const int gap = 16;
        const int panelWidth = juce::jmax(120, (panels.getWidth() - gap) / 2);
        g.setColour(juce::Colour(0xff2a3138));
        g.drawRoundedRectangle((float) panels.getX(), (float) panels.getY(), (float) panelWidth, (float) panels.getHeight(), 6.0f, 1.0f);
        g.drawRoundedRectangle((float) (panels.getX() + panelWidth + gap), (float) panels.getY(), (float) panelWidth, (float) panels.getHeight(), 6.0f, 1.0f);
        g.setColour(muted());
        g.drawText("AES50-A", panels.getX() + 12, panels.getY() + 6, 120, 18, juce::Justification::centredLeft);
        g.drawText("AES50-B", panels.getX() + panelWidth + gap + 12, panels.getY() + 6, 120, 18, juce::Justification::centredLeft);
    }
    else if (page == 4 || page == 6 || page == 7)
    {
        const char* labels[] = { "Input", "Pre EQ", "Post EQ", "Pre Fader", "Post Fader" };
        const int taps[] = { 4, 0, 1, 2, 3 };
        const int tapBlock = 230;
        const int legendWidth = juce::jmax(5 * 88, lowerBounds.getWidth() - tapBlock);
        const int itemWidth = legendWidth / 5;
        g.setFont(juce::Font(juce::FontOptions(11.0f)));

        for (int i = 0; i < 5; ++i)
        {
            const int x = lowerBounds.getX() + i * itemWidth;
            auto dot = juce::Rectangle<float>((float) x, (float) lowerBounds.getY() + 2.0f, 12.0f, 12.0f);
            g.setColour(tapColour(taps[i]));
            g.drawEllipse(dot, 1.5f);
            g.fillEllipse(dot.translated(0.0f, 18.0f));
            g.setColour(muted());
            g.drawText(labels[i], x + 16, lowerBounds.getY(), itemWidth - 20, 16, juce::Justification::centredLeft);
            g.drawText(itemWidth > 130 ? juce::String(labels[i]) + " + Mute" : "+ Mute",
                       x + 16, lowerBounds.getY() + 18, itemWidth - 20, 16, juce::Justification::centredLeft);
        }

        g.drawText("Default Signal Tap", lowerBounds.getRight() - tapBlock, lowerBounds.getY(), 140, 20, juce::Justification::centredLeft);

        if (page == 4)
        {
            g.drawText("Outputs 1 and 2 reach the device through XLR 1 and 2.",
                       lowerBounds.getX(), lowerBounds.getY() + 42, lowerBounds.getWidth(), 18, juce::Justification::centredLeft);
        }
    }
    else if (page == 3)
    {
        g.setColour(muted());
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawText("The device plays XLR 1 and 2. Point Outputs 1-4 at Out 1-8 to hear local outputs 1 and 2.",
                   lowerBounds.getX(), lowerBounds.getY(), lowerBounds.getWidth(), 22, juce::Justification::centredLeft);
    }
    else if (page == 8)
    {
        auto table = lowerBounds.withTrimmedRight(juce::jmax(180, lowerBounds.getWidth() / 2));
        g.setColour(ink());
        g.setFont(juce::Font(juce::FontOptions(15.0f)));
        g.drawText("Ultranet output", table.getX(), table.getY(), 240, 24, juce::Justification::centredLeft);
        g.drawText("Speaker group", table.getRight() + 24, table.getY(), 180, 24, juce::Justification::centredLeft);
        auto rows = table.withTrimmedTop(32);
        g.setColour(headerBack());
        g.fillRect(rows.getX(), rows.getY(), rows.getWidth(), 22);
        g.setColour(muted());
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawText("Ch", rows.getX() + 8, rows.getY(), 40, 22, juce::Justification::centredLeft);
        g.drawText("Source", rows.getX() + 56, rows.getY(), 220, 22, juce::Justification::centredLeft);
        g.drawText("Tap", rows.getRight() - 140, rows.getY(), 130, 22, juce::Justification::centredLeft);
        auto body = rows.withTrimmedTop(22);
        const int rowHeight = juce::jmax(18, body.getHeight() / numP16Outputs);

        for (int i = 0; i < numP16Outputs; ++i)
        {
            const int y = body.getY() + i * rowHeight;
            int source = outputOff;
            int tap = 0;
            model.getInt("/outputs/p16/" + twoDigits(i) + "/src", source);
            model.getInt("/outputs/p16/" + twoDigits(i) + "/tap", tap);
            g.setColour(i % 2 == 0 ? gridBack() : juce::Colour(0xff161b20));
            g.fillRect(body.getX(), y, body.getWidth(), rowHeight);
            g.setColour(ink());
            g.drawText(juce::String(i + 1), body.getX() + 8, y, 40, rowHeight, juce::Justification::centredLeft);
            g.drawText(model.outputSourceName(source), body.getX() + 56, y, body.getWidth() - 210, rowHeight, juce::Justification::centredLeft);
            g.setColour(tapColour(tap));
            g.drawText(tapLabel(tap), body.getRight() - 140, y, 130, rowHeight, juce::Justification::centredLeft);
        }
    }
}

void RoutingBoard::resized()
{
    auto area = getLocalBounds();
    auto tabRow = area.removeFromTop(34).reduced(6, 4);
    const int tabWidth = std::max(64, tabRow.getWidth() / 11);

    for (int i = 0; i < 11; ++i)
        tabs[(size_t) i].setBounds(tabRow.removeFromLeft(std::min(tabWidth, tabRow.getWidth())).reduced(2, 0));

    const bool inputs = page == 0;
    const bool signal = page == 4 || page == 6 || page == 7;
    const bool user = page == 9 || page == 10;
    const bool delay = page == 5;
    const bool iq = page == 8;
    const bool blockGrid = page == 0 || page == 1 || page == 2 || page == 3 || page == 9 || page == 10;
    delayBank->setVisible(delay);
    gridView.setVisible(! delay && ! iq);
    detailView.setVisible(user);
    stageboxA.setVisible(inputs);
    stageboxB.setVisible(inputs);
    stageboxAReadout.setVisible(inputs);
    stageboxBReadout.setVisible(inputs);
    auxRemapA.setVisible(inputs);
    auxRemapB.setVisible(inputs);
    recordButton.setVisible(inputs);
    playButton.setVisible(inputs);
    tapBox.setVisible(signal);
    iqOff.setVisible(iq);
    iqA.setVisible(iq);
    iqB.setVisible(iq);
    pageNote.setVisible(false);
    lowerBounds = {};

    if (delay)
    {
        delayBank->setBounds(area.reduced(8));
        return;
    }

    area = area.reduced(8, 4);

    if (iq)
    {
        lowerBounds = area;
        const int tableRight = area.getX() + area.getWidth() - juce::jmax(180, area.getWidth() / 2);
        iqOff.setBounds(tableRight + 24, area.getY() + 36, 70, 24);
        iqA.setBounds(tableRight + 100, area.getY() + 36, 50, 24);
        iqB.setBounds(tableRight + 156, area.getY() + 36, 50, 24);
        return;
    }

    const int lowerReserve = inputs ? 220 : signal ? 70 : user ? 250 : page == 3 ? 28 : 0;
    const int bar = gridView.getScrollBarThickness();

    if (blockGrid)
        grid->fitToWidth(std::max(200, area.getWidth() - bar));

    int gridHeight = grid->contentHeight() + 2;

    if (grid->contentWidth() > area.getWidth())
        gridHeight += bar;

    gridHeight = juce::jmin(gridHeight, juce::jmax(96, area.getHeight() - lowerReserve));
    gridView.setBounds(area.removeFromTop(gridHeight));
    grid->setSize(std::max(grid->contentWidth(), gridView.getMaximumVisibleWidth()), grid->contentHeight());
    area.removeFromTop(8);
    lowerBounds = area;

    if (inputs)
    {
        auxRemapA.setBounds(lowerBounds.getX() + 118, lowerBounds.getY(), 150, 24);
        recordButton.setBounds(lowerBounds.getX() + 276, lowerBounds.getY(), 72, 24);
        playButton.setBounds(lowerBounds.getX() + 354, lowerBounds.getY(), 64, 24);
        auxRemapB.setBounds(lowerBounds.getX() + 428, lowerBounds.getY(), 150, 24);
        auto panels = lowerBounds.withTrimmedTop(60);
        const int gap = 16;
        const int panelWidth = juce::jmax(120, (panels.getWidth() - gap) / 2);
        auto left = juce::Rectangle<int>(panels.getX(), panels.getY(), panelWidth, panels.getHeight()).reduced(12, 8);
        auto right = juce::Rectangle<int>(panels.getX() + panelWidth + gap, panels.getY(), panelWidth, panels.getHeight()).reduced(12, 8);
        left.removeFromTop(22);
        right.removeFromTop(22);
        stageboxA.setBounds(left.removeFromTop(24).withWidth(160));
        stageboxAReadout.setBounds(left.removeFromTop(20).withWidth(juce::jmax(160, left.getWidth())));
        stageboxB.setBounds(right.removeFromTop(24).withWidth(160));
        stageboxBReadout.setBounds(right.removeFromTop(20).withWidth(juce::jmax(160, right.getWidth())));
    }
    else if (signal)
    {
        tapBox.setBounds(lowerBounds.getRight() - 200, lowerBounds.getY() + 16, 190, 24);
    }
    else if (user)
    {
        const int contentHeight = detail->contentHeight() + (detail->contentWidth() > lowerBounds.getWidth() ? bar : 0);
        auto detailArea = lowerBounds.withHeight(juce::jmin(lowerBounds.getHeight(), contentHeight + 4));
        detailView.setBounds(detailArea);
        detail->setSize(std::max(detail->contentWidth(), detailView.getMaximumVisibleWidth()), detail->contentHeight());
    }
}

RoutingBoard::~RoutingBoard() = default;

} // namespace vc
