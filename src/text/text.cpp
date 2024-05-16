#include "rive/text/text.hpp"
using namespace rive;
#ifdef WITH_RIVE_TEXT
#include "rive/text_engine.hpp"
#include "rive/component_dirt.hpp"
#include "rive/text/utf.hpp"
#include "rive/text/text_style.hpp"
#include "rive/text/text_value_run.hpp"
#include "rive/shapes/paint/shape_paint.hpp"
#include "rive/artboard.hpp"
#include "rive/factory.hpp"

GlyphItr& GlyphItr::operator++()
{
    auto run = *m_run;
    m_glyphIndex += run->dir == TextDirection::ltr ? 1 : -1;

    // Did we reach the end of the run?
    if (m_glyphIndex == m_line->endGlyphIndex(run) && run != m_line->lastRun())
    {
        m_run++;
        m_glyphIndex = m_line->startGlyphIndex(*m_run);
    }
    return *this;
}

OrderedLine::OrderedLine(const Paragraph& paragraph,
                         const GlyphLine& line,
                         float lineWidth,
                         bool wantEllipsis,
                         bool isEllipsisLineLast,
                         GlyphRun* ellipsisRun) :
    m_startGlyphIndex(line.startGlyphIndex), m_endGlyphIndex(line.endGlyphIndex)
{
    std::vector<const GlyphRun*> logicalRuns;
    const SimpleArray<GlyphRun>& glyphRuns = paragraph.runs;

    if (!wantEllipsis || !buildEllipsisRuns(logicalRuns,
                                            paragraph,
                                            line,
                                            lineWidth,
                                            isEllipsisLineLast,
                                            ellipsisRun))
    {
        for (uint32_t i = line.startRunIndex; i < line.endRunIndex + 1; i++)
        {
            logicalRuns.push_back(&glyphRuns[i]);
        }

        if (!logicalRuns.empty())
        {
            m_startLogical = logicalRuns.front();
            m_endLogical = logicalRuns.back();
        }
    }

    // Now sort the runs visually.
    if (paragraph.baseDirection == TextDirection::ltr || logicalRuns.empty())
    {
        m_runs = std::move(logicalRuns);
    }
    else
    {
        std::vector<const GlyphRun*> visualRuns;
        visualRuns.reserve(logicalRuns.size());

        auto itr = logicalRuns.rbegin();
        auto end = logicalRuns.rend();
        const GlyphRun* first = *itr;
        visualRuns.push_back(first);
        size_t ltrIndex = 0;
        TextDirection previousDirection = first->dir;
        while (++itr != end)
        {
            const GlyphRun* run = *itr;
            if (run->dir == TextDirection::ltr && previousDirection == run->dir)
            {
                visualRuns.insert(visualRuns.begin() + ltrIndex, run);
            }
            else
            {
                if (run->dir == TextDirection::ltr)
                {
                    ltrIndex = visualRuns.size();
                }
                visualRuns.push_back(run);
            }
            previousDirection = run->dir;
        }
        m_runs = std::move(visualRuns);
    }
}

static void appendUnicode(std::vector<rive::Unichar>& unichars, const char text[])
{
    const uint8_t* ptr = (const uint8_t*)text;
    while (*ptr)
    {
        unichars.push_back(rive::UTF::NextUTF8(&ptr));
    }
}

bool OrderedLine::buildEllipsisRuns(std::vector<const GlyphRun*>& logicalRuns,
                                    const Paragraph& paragraph,
                                    const GlyphLine& line,
                                    float lineWidth,
                                    bool isEllipsisLineLast,
                                    GlyphRun* storedEllipsisRun)
{
    float x = 0.0f;
    const SimpleArray<GlyphRun>& glyphRuns = paragraph.runs;
    uint32_t startGIndex = line.startGlyphIndex;
    // If it's the last line we can actually early out if the whole things fits,
    // so check that first with no extra shaping.
    if (isEllipsisLineLast)
    {
        bool fits = true;

        for (uint32_t i = line.startRunIndex; i < line.endRunIndex + 1; i++)
        {
            const GlyphRun& run = glyphRuns[i];
            uint32_t endGIndex =
                i == line.endRunIndex ? line.endGlyphIndex : (uint32_t)run.glyphs.size();

            for (uint32_t j = startGIndex; j != endGIndex; j++)
            {
                x += run.advances[j];
                if (x > lineWidth)
                {
                    fits = false;
                    goto measured;
                }
            }
            startGIndex = 0;
        }
    measured:
        if (fits)
        {
            // It fits, just get the regular glyphs.
            return false;
        }
    }

    std::vector<Unichar> ellipsisCodePoints;
    appendUnicode(ellipsisCodePoints, "...");

    rcp<Font> ellipsisFont = nullptr;
    float ellipsisFontSize = 0.0f;

    GlyphRun ellipsisRun = {};
    float ellipsisWidth = 0.0f;

    bool ellipsisOverflowed = false;
    startGIndex = line.startGlyphIndex;

    for (uint32_t i = line.startRunIndex; i < line.endRunIndex + 1; i++)
    {
        const GlyphRun& run = glyphRuns[i];
        if (run.font != ellipsisFont && run.size != ellipsisFontSize)
        {
            // Track the latest we've checked (even if we discard it so we don't try
            // to do this again for this ellipsis).
            ellipsisFont = run.font;
            ellipsisFontSize = run.size;

            // Get the next shape so we can check if it fits, otherwise keep using
            // the last one.
            TextRun ellipsisRuns[] = {
                {ellipsisFont, ellipsisFontSize, (uint32_t)ellipsisCodePoints.size()}};
            auto nextEllipsisShape =
                ellipsisFont->shapeText(ellipsisCodePoints, Span<TextRun>(ellipsisRuns, 1));

            // Hard assumption one run and para
            const Paragraph& para = nextEllipsisShape[0];
            const GlyphRun& nextEllipsisRun = para.runs.front();

            float nextEllipsisWidth = 0;
            for (size_t j = 0; j < nextEllipsisRun.glyphs.size(); j++)
            {
                nextEllipsisWidth += nextEllipsisRun.advances[j];
            }

            if (ellipsisRun.font == nullptr || x + nextEllipsisWidth <= lineWidth)
            {
                // This ellipsis still fits, go ahead and use it. Otherwise stick with
                // the old one.
                ellipsisWidth = nextEllipsisWidth;
                ellipsisRun = std::move(para.runs.front());
            }
        }

        uint32_t endGIndex =
            i == line.endRunIndex ? line.endGlyphIndex : (uint32_t)run.glyphs.size();
        for (uint32_t j = startGIndex; j != endGIndex; j++)
        {
            float advance = run.advances[j];
            if (x + advance + ellipsisWidth > lineWidth)
            {
                m_endGlyphIndex = j;
                ellipsisOverflowed = true;
                break;
            }
            x += advance;
        }
        startGIndex = 0;
        logicalRuns.push_back(&run);
        m_endLogical = &run;

        if (ellipsisOverflowed && ellipsisRun.font != nullptr)
        {
            *storedEllipsisRun = std::move(ellipsisRun);
            logicalRuns.push_back(storedEllipsisRun);
            break;
        }
    }

    // There was enough space for it, so let's add the ellipsis (if we didn't
    // already). Note that we already checked if this is the last line and found
    // that the whole text didn't fit.
    if (!ellipsisOverflowed && ellipsisRun.font != nullptr)
    {
        *storedEllipsisRun = std::move(ellipsisRun);
        logicalRuns.push_back(storedEllipsisRun);
    }
    m_startLogical = storedEllipsisRun == logicalRuns.front() ? nullptr : logicalRuns.front();
    return true;
}

void Text::buildRenderStyles()
{
    // TODO: make this configurable.
    const float paragraphSpacing = 20.0f;

    // Build the clip path if we want it.
    if (overflow() == TextOverflow::clipped)
    {
        if (m_clipRenderPath == nullptr)
        {
            m_clipRenderPath = artboard()->factory()->makeEmptyRenderPath();
        }
        else
        {
            m_clipRenderPath->rewind();
        }
        m_clipRenderPath->addRect(0.0f, 0.0f, width(), height());
    }
    else
    {
        m_clipRenderPath = nullptr;
    }

    for (TextStyle* style : m_renderStyles)
    {
        style->rewindPath();
    }
    m_renderStyles.clear();

    // Build up ordered runs as we go.
    int paragraphIndex = 0;
    int lineIndex = 0;
    float y = 0.0f;

    int ellipsisLine = -1;
    bool isEllipsisLineLast = false;
    // Find the line to put the ellipsis on (line before the one that
    // overflows).
    if (overflow() == TextOverflow::ellipsis && sizing() == TextSizing::fixed)
    {
        int lastLineIndex = -1;
        for (const SimpleArray<GlyphLine>& paragraphLines : m_lines)
        {
            for (const GlyphLine& line : paragraphLines)
            {
                lastLineIndex++;
                if (y + line.bottom <= height())
                {
                    ellipsisLine++;
                }
            }

            if (!paragraphLines.empty())
            {
                y += paragraphLines.back().bottom;
            }
            y += paragraphSpacing;
        }
        if (ellipsisLine == -1)
        {
            // Nothing fits, just show the first line and ellipse it.
            ellipsisLine = 0;
        }
        isEllipsisLineLast = lastLineIndex == ellipsisLine;
        y = 0.0f;
    }

    for (const SimpleArray<GlyphLine>& paragraphLines : m_lines)
    {
        const Paragraph& paragraph = m_shape[paragraphIndex++];
        for (const GlyphLine& line : paragraphLines)
        {
            switch (overflow())
            {
                case TextOverflow::hidden:
                    if (sizing() == TextSizing::fixed && y + line.bottom > height())
                    {
                        return;
                    }
                    break;
                case TextOverflow::clipped:
                    if (sizing() == TextSizing::fixed && y + line.top > height())
                    {
                        return;
                    }
                    break;
                default:
                    break;
            }

            if (lineIndex >= m_orderedLines.size())
            {
                // We need to still compute this line's ordered runs.
                m_orderedLines.emplace_back(OrderedLine(paragraph,
                                                        line,
                                                        width(),
                                                        ellipsisLine == lineIndex,
                                                        isEllipsisLineLast,
                                                        &m_ellipsisRun));
            }
            const OrderedLine& orderedLine = m_orderedLines[lineIndex];
            float x = 0.0f;
            float renderY = y + line.baseline;
            for (auto glyphItr : orderedLine)
            {
                const GlyphRun* run = std::get<0>(glyphItr);
                size_t glyphIndex = std::get<1>(glyphItr);

                const Font* font = run->font.get();
                const Vec2D& offset = run->offsets[glyphIndex];

                GlyphID glyphId = run->glyphs[glyphIndex];

                // TODO: profile if this should be cached.
                RawPath path = font->getPath(glyphId);
                // If we do end up caching these, we'll want to not
                // transformInPlace and just transform instead.
                path.transformInPlace(
                    Mat2D(run->size, 0.0f, 0.0f, run->size, x + offset.x, renderY + offset.y));
                x += run->advances[glyphIndex];

                assert(run->styleId < m_runs.size());
                TextStyle* style = m_runs[run->styleId]->style();
                // TextValueRun::onAddedDirty botches loading if it cannot
                // resolve a style, so we're confident we have a style here.
                assert(style != nullptr);
                if (style->addPath(path))
                {
                    // This was the first path added to the style, so let's mark
                    // it in our draw list.
                    m_renderStyles.push_back(style);
                    style->propagateOpacity(renderOpacity());
                }
            }
            if (lineIndex == ellipsisLine)
            {
                return;
            }
            lineIndex++;
        }
        if (!paragraphLines.empty())
        {
            y += paragraphLines.back().bottom;
        }
        y += paragraphSpacing;
    }
}

void Text::draw(Renderer* renderer)
{
    if (!clip(renderer))
    {
        // We didn't clip, so make sure to save as we'll be doing some
        // transformations.
        renderer->save();
    }
    renderer->transform(worldTransform());
    if (overflow() == TextOverflow::clipped && m_clipRenderPath)
    {
        renderer->clipPath(m_clipRenderPath.get());
    }
    for (auto style : m_renderStyles)
    {
        style->draw(renderer);
    }
    renderer->restore();
}

void Text::addRun(TextValueRun* run) { m_runs.push_back(run); }

void Text::markShapeDirty() { addDirt(ComponentDirt::Path); }

void Text::alignValueChanged() { markShapeDirty(); }

void Text::sizingValueChanged() { markShapeDirty(); }

void Text::overflowValueChanged()
{
    if (sizing() != TextSizing::autoWidth)
    {
        markShapeDirty();
    }
}

void Text::widthChanged()
{
    if (sizing() != TextSizing::autoWidth)
    {
        markShapeDirty();
    }
}

void Text::heightChanged()
{
    if (sizing() == TextSizing::fixed)
    {
        markShapeDirty();
    }
}

static rive::TextRun append(std::vector<Unichar>& unichars,
                            rcp<Font> font,
                            float size,
                            const std::string& text,
                            uint16_t styleId)
{
    const uint8_t* ptr = (const uint8_t*)text.c_str();
    uint32_t n = 0;
    while (*ptr)
    {
        unichars.push_back(UTF::NextUTF8(&ptr));
        n += 1;
    }
    return {std::move(font), size, n, 0, styleId};
}

static SimpleArray<SimpleArray<GlyphLine>> breakLines(const SimpleArray<Paragraph>& paragraphs,
                                                      float width,
                                                      TextAlign align)
{
    bool autoWidth = width == -1.0f;
    float paragraphWidth = width;

    SimpleArray<SimpleArray<GlyphLine>> lines(paragraphs.size());

    size_t paragraphIndex = 0;
    for (auto& para : paragraphs)
    {
        lines[paragraphIndex] = GlyphLine::BreakLines(para.runs, autoWidth ? -1.0f : width);
        if (autoWidth)
        {
            paragraphWidth = std::max(paragraphWidth,
                                      GlyphLine::ComputeMaxWidth(lines[paragraphIndex], para.runs));
        }
        paragraphIndex++;
    }
    paragraphIndex = 0;
    for (auto& para : paragraphs)
    {
        GlyphLine::ComputeLineSpacing(lines[paragraphIndex++], para.runs, paragraphWidth, align);
    }
    return lines;
}

void Text::update(ComponentDirt value)
{
    Super::update(value);

    if (hasDirt(value, ComponentDirt::Path))
    {
        std::vector<Unichar> unichars;
        std::vector<TextRun> runs;
        uint16_t runIndex = 0;
        for (auto valueRun : m_runs)
        {
            auto style = valueRun->style();
            const std::string& text = valueRun->text();
            if (style == nullptr || style->font() == nullptr || text.empty())
            {
                runIndex++;
                continue;
            }
            runs.emplace_back(append(unichars, style->font(), style->fontSize(), text, runIndex++));
        }
        if (!runs.empty())
        {
            m_shape = runs[0].font->shapeText(unichars, runs);
            m_lines = breakLines(m_shape,
                                 sizing() == TextSizing::autoWidth ? -1.0f : width(),
                                 (TextAlign)alignValue());

            m_orderedLines.clear();
            m_ellipsisRun = {};
        }
        // This could later be flagged as dirty and called only when actually
        // rendered the first time, for now we do it on update cycle in tandem
        // with shaping.
        buildRenderStyles();
    }
    else if (hasDirt(value, ComponentDirt::RenderOpacity))
    {
        // Note that buildRenderStyles does this too, which is why we can get
        // away doing this in the else.
        for (TextStyle* style : m_renderStyles)
        {
            style->propagateOpacity(renderOpacity());
        }
    }
}

Core* Text::hitTest(HitInfo*, const Mat2D&)
{
    if (renderOpacity() == 0.0f)
    {
        return nullptr;
    }

    return nullptr;
}

#else
// Text disabled.
void Text::draw(Renderer* renderer) {}
Core* Text::hitTest(HitInfo*, const Mat2D&) { return nullptr; }
void Text::addRun(TextValueRun* run) {}
void Text::markShapeDirty() {}
void Text::update(ComponentDirt value) {}
void Text::alignValueChanged() {}
void Text::sizingValueChanged() {}
void Text::overflowValueChanged() {}
void Text::widthChanged() {}
void Text::heightChanged() {}
#endif