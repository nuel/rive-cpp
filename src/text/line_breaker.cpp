/*
 * Copyright 2022 Rive
 */

#include "rive/text.hpp"
#include <limits>
#include <algorithm>
using namespace rive;

static bool autowidth(float width) { return width < 0.0f; }

float GlyphLine::ComputeMaxWidth(Span<GlyphLine> lines, Span<const GlyphRun> runs)
{
    float maxLineWidth = 0.0f;
    for (auto& line : lines)
    {
        maxLineWidth = std::max(maxLineWidth,
                                runs[line.endRunIndex].xpos[line.endGlyphIndex] -
                                    runs[line.startRunIndex].xpos[line.startGlyphIndex]);
    }
    return maxLineWidth;
}

void GlyphLine::ComputeLineSpacing(Span<GlyphLine> lines,
                                   Span<const GlyphRun> runs,
                                   float width,
                                   TextAlign align)
{
    float Y = 0; // top of our frame
    for (auto& line : lines)
    {
        float asc = 0;
        float des = 0;
        for (int i = line.startRunIndex; i <= line.endRunIndex; ++i)
        {
            const auto& run = runs[i];

            asc = std::min(asc, run.font->lineMetrics().ascent * run.size);
            des = std::max(des, run.font->lineMetrics().descent * run.size);
        }
        line.top = Y;
        Y -= asc;
        line.baseline = Y;
        Y += des;
        line.bottom = Y;
        auto lineWidth = runs[line.endRunIndex].xpos[line.endGlyphIndex] -
                         runs[line.startRunIndex].xpos[line.startGlyphIndex];
        switch (align)
        {
            case TextAlign::right:
                line.startX = width - lineWidth;
                break;
            case TextAlign::left:
                line.startX = 0;
                break;
            case TextAlign::center:
                line.startX = width / 2.0f - lineWidth / 2.0f;
                break;
        }
    }
}

struct WordMarker
{
    const GlyphRun* run;
    uint32_t index;

    bool next(Span<const GlyphRun> runs)
    {
        index += 2;
        while (index >= run->breaks.size())
        {
            index -= run->breaks.size();
            run++;
            if (run == runs.end())
            {
                return false;
            }
        }
        return true;
    }
};

class RunIterator
{
    Span<const GlyphRun> m_runs;
    const GlyphRun* m_run;
    uint32_t m_index;

public:
    RunIterator(Span<const GlyphRun> runs, const GlyphRun* run, uint32_t index) :
        m_runs(runs), m_run(run), m_index(index)
    {}

    bool back()
    {
        if (m_index == 0)
        {
            if (m_run == m_runs.begin())
            {
                return false;
            }
            m_run--;
            if (m_run->glyphs.size() == 0)
            {
                m_index = 0;
                return back();
            }
            else
            {
                m_index = m_run->glyphs.size() == 0 ? 0 : (uint32_t)m_run->glyphs.size() - 1;
            }
        }
        else
        {
            m_index--;
        }
        return true;
    }

    bool forward()
    {
        if (m_index == m_run->glyphs.size())
        {
            if (m_run == m_runs.end())
            {
                return false;
            }
            m_run++;
            m_index = 0;
            if (m_index == m_run->glyphs.size())
            {
                return forward();
            }
        }
        else
        {
            m_index++;
        }
        return true;
    }

    float x() const { return m_run->xpos[m_index]; }

    const GlyphRun* run() const { return m_run; }
    uint32_t index() const { return m_index; }

    bool operator==(const RunIterator& o) const { return m_run == o.m_run && m_index == o.m_index; }
};

SimpleArray<GlyphLine> GlyphLine::BreakLines(Span<const GlyphRun> runs, float width)
{
    float maxLineWidth = autowidth(width) ? std::numeric_limits<float>::max() : width;

    SimpleArrayBuilder<GlyphLine> lines;

    if (runs.empty())
    {
        return lines;
    }

    auto limit = maxLineWidth;

    bool advanceWord = false;
    WordMarker start = {runs.begin(), 0};
    WordMarker end = {runs.begin(), (uint32_t)-1};
    if (!end.next(runs))
    {
        return lines;
    }

    GlyphLine line = GlyphLine();

    uint32_t breakIndex = end.run->breaks[end.index];
    const GlyphRun* breakRun = end.run;
    uint32_t lastEndGlyphIndex = end.index;
    uint32_t startBreakIndex = start.run->breaks[start.index];
    const GlyphRun* startBreakRun = start.run;

    float x = end.run->xpos[breakIndex];
    while (true)
    {
        if (advanceWord)
        {
            lastEndGlyphIndex = end.index;

            if (!start.next(runs))
            {
                break;
            }
            if (!end.next(runs))
            {
                break;
            }

            advanceWord = false;

            breakIndex = end.run->breaks[end.index];
            breakRun = end.run;
            startBreakIndex = start.run->breaks[start.index];
            startBreakRun = start.run;
            x = end.run->xpos[breakIndex];
        }

        bool isForcedBreak = breakRun == startBreakRun && breakIndex == startBreakIndex;

        if (!isForcedBreak && x > limit)
        {
            uint32_t startRunIndex = (uint32_t)(start.run - runs.begin());

            // A whole word overflowed, break until we can no longer break (or
            // it fits).
            if (line.startRunIndex == startRunIndex && line.startGlyphIndex == startBreakIndex)
            {
                bool canBreakMore = true;
                while (canBreakMore && x > limit)
                {

                    RunIterator lineStart =
                        RunIterator(runs, runs.begin() + line.startRunIndex, line.startGlyphIndex);
                    RunIterator lineEnd = RunIterator(runs, end.run, end.run->breaks[end.index]);
                    // Look for the next character that doesn't overflow.
                    while (true)
                    {
                        if (!lineEnd.back())
                        {
                            // Hit the start of the text, can't go back.
                            canBreakMore = false;
                            break;
                        }
                        else if (lineEnd.x() <= limit)
                        {
                            if (lineStart == lineEnd && !lineEnd.forward())
                            {
                                // Hit the start of the line and could not
                                // go forward to consume a single character.
                                // We can't break any further.
                                canBreakMore = false;
                            }
                            else
                            {
                                line.endRunIndex = (uint32_t)(lineEnd.run() - runs.begin());
                                line.endGlyphIndex = lineEnd.index();
                            }
                            break;
                        }
                    }
                    if (canBreakMore)
                    {
                        // Add the line and push the limit out.
                        limit = lineEnd.x() + maxLineWidth;
                        if (!line.empty())
                        {
                            lines.add(line);
                        }
                        // Setup the next line.
                        line = GlyphLine((uint32_t)(lineEnd.run() - runs.begin()), lineEnd.index());
                    }
                }
            }
            else
            {
                // word overflowed, knock it to a new line
                auto startX = start.run->xpos[start.run->breaks[start.index]];
                limit = startX + maxLineWidth;

                if (!line.empty() || start.index - lastEndGlyphIndex > 1)
                {
                    lines.add(line);
                }

                line = GlyphLine(startRunIndex, startBreakIndex);
            }
        }
        else
        {
            line.endRunIndex = (uint32_t)(end.run - runs.begin());
            line.endGlyphIndex = end.run->breaks[end.index];
            advanceWord = true;
            // Forced BR.
            if (isForcedBreak)
            {
                lines.add(line);
                auto startX = start.run->xpos[start.run->breaks[start.index] + 1];
                limit = startX + maxLineWidth;
                line = GlyphLine((uint32_t)(start.run - runs.begin()), startBreakIndex + 1);
            }
        }
    }
    // Don't add a line that starts/ends at the same spot.
    if (!line.empty())
    {
        lines.add(line);
    }

    return lines;
}