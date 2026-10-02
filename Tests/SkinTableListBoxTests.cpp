#include "skin/PlayerComponentCatalog.h"
#include "skin/SkinBuiltInPackages.h"
#include "skin/SkinCompiler.h"
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

void configureQueue (SkinTableListBox& table, QueueRows& rows, bool giveBounds = true)
{
    table.setModel (&rows);
    table.setOutlineThickness (0);
    table.setRowHeight (32);
    table.setHeaderHeight (24);
    table.getHeader().addColumn ("Title", 1, 240, 80, -1);
    table.getHeader().addColumn ("Artist / album", 2, 360, 100, -1);
    table.getHeader().addColumn ("Duration", 3, 90, 40, 120);
    if (giveBounds) table.setBounds (0, 0, 640, 280);
}

void setQueuePresentation (SkinTableListBox& table, const juce::String& presentation)
{
    table.getProperties().set (JuceSkinAdapter::styleProperty,
                              presentation == "list" ? "table.listening" : "table.studio");
    table.setPresentation (presentation);
}

class SkinTableListBoxTests final : public juce::UnitTest
{
public:
    SkinTableListBoxTests() : UnitTest ("SkinTableListBox", "skin") {}

    void runTest() override
    {
        beginTest ("skin fixtures compile with table geometry");
        SkinValidationReport report;
        const auto foundationPackage = SkinBuiltInPackages::create (
            SkinBuiltInPackages::Package::foundation, report);
        const auto foundation = foundationPackage
            ? SkinCompiler::compile (*foundationPackage, {}, report) : nullptr;
        const auto package = SkinBuiltInPackages::create (
            SkinBuiltInPackages::Package::defaultSkin, report);
        const auto skin = package && foundation ? SkinCompiler::compile (*package, foundation, report) : nullptr;
        expect (skin != nullptr, report.toText());
        if (! skin) return;
        SkinRenderer renderer;
        JuceSkinAdapter adapter ([&] (const juce::Component&, SkinRenderTarget target, const juce::String& style)
        {
            return JuceSkinAdapter::RuntimeBinding { skin->resolveStyle (target, style), nullptr, nullptr, false };
        }, renderer);

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
            table.setLookAndFeel (&adapter);
            table.selectRow (2);
            setQueuePresentation (table, "list");
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
            table.setLookAndFeel (nullptr);
        }

        beginTest ("table rows expose three contiguous columns and retain the header");
        {
            QueueRows rows;
            SkinTableListBox table;
            configureQueue (table, rows);
            table.setLookAndFeel (&adapter);
            setQueuePresentation (table, "table");
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
            table.setLookAndFeel (nullptr);
        }

        beginTest ("table profile initializes after empty bounds and survives uncustomized restores");
        {
            QueueRows rows;
            SkinTableListBox table;
            configureQueue (table, rows, false);
            table.setLookAndFeel (&adapter);
            setQueuePresentation (table, "table");
            for (const auto width : { 640, 520, 720 })
            {
                table.setSize (width, 280);
                expectEquals (table.getHeader().getColumnWidth (3), 64);
                table.resetPresentation();
                table.setBounds ({});
                setQueuePresentation (table, "table");
            }
            table.setLookAndFeel (nullptr);
        }

        beginTest ("explicit narrow duration width survives zero bounds and list transitions");
        {
            QueueRows rows;
            SkinTableListBox table;
            configureQueue (table, rows);
            table.setLookAndFeel (&adapter);
            setQueuePresentation (table, "table");
            table.getHeader().setColumnWidth (3, 40);
            table.resetPresentation();
            table.setBounds ({});
            setQueuePresentation (table, "table");
            table.setSize (520, 280);
            expectEquals (table.getHeader().getColumnWidth (3), 40);
            setQueuePresentation (table, "list");
            expectEquals (table.getHeader().getColumnWidth (3), 56);
            setQueuePresentation (table, "table");
            expectEquals (table.getHeader().getColumnWidth (3), 40);
            table.setLookAndFeel (nullptr);
        }

        beginTest ("switching back to table preserves user column order and widths");
        {
            QueueRows rows;
            SkinTableListBox table;
            configureQueue (table, rows);
            table.setLookAndFeel (&adapter);
            setQueuePresentation (table, "table");
            auto& header = table.getHeader();
            header.setColumnWidth (1, 220);
            header.setColumnWidth (3, 88);
            header.setColumnWidth (2, table.getVisibleContentWidth() - 220 - 88);
            header.moveColumn (3, 0);
            header.setSortColumnId (3, false);
            const auto customised = header.toString();
            setQueuePresentation (table, "list");
            setQueuePresentation (table, "list");
            setQueuePresentation (table, "table");
            expectEquals (header.toString(), customised);
            setQueuePresentation (table, "table");
            expectEquals (header.toString(), customised);
            table.setLookAndFeel (nullptr);
        }

        beginTest ("skin row metrics determine native row stride and header height");
        {
            auto style = *skin->resolveStyle (SkinRenderTarget::table, "table.listening");
            expect (style.renderer.rowLayout.has_value());
            if (style.renderer.rowLayout)
            {
                style.renderer.rowLayout->base.rowHeight->value = SkinCompiledRenderValue (72.0);
                style.renderer.rowLayout->base.rowGap->value = SkinCompiledRenderValue (4.0);
                style.renderer.rowLayout->base.headerHeight->value = SkinCompiledRenderValue (18.0);
                JuceSkinAdapter customAdapter ([&] (const juce::Component&, SkinRenderTarget, const juce::String&)
                {
                    return JuceSkinAdapter::RuntimeBinding { &style, nullptr, nullptr, false };
                }, renderer);
                QueueRows rows;
                SkinTableListBox table;
                configureQueue (table, rows);
                table.setLookAndFeel (&customAdapter);
                setQueuePresentation (table, "list");
                expectEquals (table.getRowHeight(), 76);
                expectEquals (table.getHeaderHeight(), 18);
                const auto geometry = table.rowGeometry (table.getRowHeight());
                expectEquals (geometry.title.getHeight(), 72.0f);
                expectEquals (table.getRowPosition (1, true).getY()
                              - table.getRowPosition (0, true).getY(), 76);
                table.setLookAndFeel (nullptr);
            }
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
