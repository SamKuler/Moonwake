#include "skin/PlayerComponentCatalog.h"
#include "ui/SkinTableListBox.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace
{
class QueueRows final : public juce::TableListBoxModel
{
public:
    int getNumRows() override { return 12; }
    void paintRowBackground (juce::Graphics&, int, int, int, bool) override {}
    void paintCell (juce::Graphics&, int, int, int, int, bool) override {}
};

void configureQueue (SkinTableListBox& table, QueueRows& rows)
{
    table.setModel (&rows);
    table.setOutlineThickness (0);
    table.setRowHeight (32);
    table.setHeaderHeight (24);
    table.getHeader().addColumn ("Title", 1, 240, 80, -1);
    table.getHeader().addColumn ("Artist / album", 2, 360, 100, -1);
    table.getHeader().addColumn ("Duration", 3, 90, 40, 120);
    table.setBounds (0, 0, 640, 280);
}

class SkinTableListBoxTests final : public juce::UnitTest
{
public:
    SkinTableListBoxTests() : UnitTest ("SkinTableListBox", "skin") {}

    void runTest() override
    {
        beginTest ("queue catalog exposes table and list presentations");
        {
            const auto* descriptor = PlayerComponentCatalog::find ("queue.table");
            expect (descriptor != nullptr);
            if (descriptor != nullptr)
                expect (descriptor->presentations == juce::StringArray ({ "table", "list" }));
        }

        beginTest ("list rows reserve a fixed duration column and fill the remaining visible width");
        {
            QueueRows rows;
            SkinTableListBox table;
            configureQueue (table, rows);
            table.selectRow (2);
            table.setPresentation ("list");
            for (const auto width : { 320, 380, 440, 720 })
            {
                table.setSize (width, 280);
                const auto geometry = table.rowGeometry (table.getRowHeight());
                expectEquals (table.getRowHeight(), 56);
                expectEquals (table.getHeaderHeight(), 0);
                expectEquals (table.getHeader().getNumColumns (true), 2);
                expectEquals (table.getHeader().getColumnWidth (3), 56);
                expect (geometry.location.isEmpty());
                expectEquals (geometry.title.getX(), 0.0f);
                expectEquals (geometry.title.getRight(), geometry.duration.getX());
                expectEquals (geometry.duration.getRight(),
                              static_cast<float> (table.getVisibleContentWidth()));
                expectEquals (table.getRowPosition (2, true).getHeight(), 56);
                expectEquals (table.getSelectedRow(), 2);
            }
        }

        beginTest ("table rows expose three contiguous columns and retain the header");
        {
            QueueRows rows;
            SkinTableListBox table;
            configureQueue (table, rows);
            table.setPresentation ("table");
            for (const auto width : { 420, 640, 900 })
            {
                table.setSize (width, 280);
                const auto geometry = table.rowGeometry (table.getRowHeight());
                expectEquals (table.getRowHeight(), 40);
                expectEquals (table.getHeaderHeight(), 28);
                expectEquals (table.getHeader().getNumColumns (true), 3);
                expectEquals (table.getHeader().getColumnWidth (3), 64);
                expectEquals (geometry.title.getRight(), geometry.location.getX());
                expectEquals (geometry.location.getRight(), geometry.duration.getX());
                expectEquals (geometry.duration.getRight(),
                              static_cast<float> (table.getVisibleContentWidth()));
                expectEquals (table.getRowPosition (2, true).getHeight(), 40);
            }
        }

        beginTest ("switching back to table preserves user column order and widths");
        {
            QueueRows rows;
            SkinTableListBox table;
            configureQueue (table, rows);
            table.setPresentation ("table");
            auto& header = table.getHeader();
            header.setColumnWidth (1, 220);
            header.setColumnWidth (3, 88);
            header.setColumnWidth (2, table.getVisibleContentWidth() - 220 - 88);
            header.moveColumn (3, 0);
            header.setSortColumnId (3, false);
            const auto customised = header.toString();
            table.setPresentation ("list");
            table.setPresentation ("list");
            table.setPresentation ("table");
            expectEquals (header.toString(), customised);
            table.setPresentation ("table");
            expectEquals (header.toString(), customised);
        }

        beginTest ("reset restores original third-party geometry and column state");
        for (const auto stretch : { false, true })
        {
            QueueRows rows;
            SkinTableListBox table;
            configureQueue (table, rows);
            auto& header = table.getHeader();
            header.setStretchToFitActive (false);
            table.setRowHeight (35);
            table.setHeaderHeight (21);
            header.setColumnWidth (1, 197);
            header.setColumnWidth (2, 243);
            header.setColumnWidth (3, 91);
            header.setColumnVisible (2, false);
            header.moveColumn (3, 0);
            header.setSortColumnId (1, false);
            header.setStretchToFitActive (stretch);
            const auto original = header.toString();
            table.setPresentation ("table");
            table.setPresentation ("list");
            table.setSize (400, 300);
            table.setPresentation ({});
            expectEquals (header.toString(), original);
            expectEquals (table.getRowHeight(), 35);
            expectEquals (table.getHeaderHeight(), 21);
            expect (header.isStretchToFitActive() == stretch);
            header.setStretchToFitActive (false);
            header.setColumnWidth (1, 213);
            table.resetPresentation();
            expectEquals (header.getColumnWidth (1), 213);
        }
    }
};

SkinTableListBoxTests skinTableListBoxTests;
} // namespace
