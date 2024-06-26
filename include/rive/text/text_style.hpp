#ifndef _RIVE_TEXT_STYLE_HPP_
#define _RIVE_TEXT_STYLE_HPP_
#include "rive/generated/text/text_style_base.hpp"
#include "rive/shapes/shape_paint_container.hpp"
#include "rive/assets/file_asset_referencer.hpp"
#include "rive/assets/file_asset.hpp"
#include "rive/assets/font_asset.hpp"

namespace rive
{
class FontAsset;
class Font;
class FileAsset;
class Renderer;
class RenderPath;

class TextVariationHelper;
class TextStyleAxis;
class TextStyle : public TextStyleBase, public ShapePaintContainer, public FileAssetReferencer
{
private:
    Artboard* getArtboard() override { return artboard(); }

public:
    TextStyle();
    void buildDependencies() override;
    const rcp<Font> font() const;
    void assets(const std::vector<FileAsset*>& assets) override;
    StatusCode import(ImportStack& importStack) override;

    bool addPath(const RawPath& rawPath);
    void rewindPath();
    void draw(Renderer* renderer);
    Core* clone() const override;
    void addVariation(TextStyleAxis* axis);
    void updateVariableFont();
    StatusCode onAddedClean(CoreContext* context) override;
    void onDirty(ComponentDirt dirt) override;
    // void update(ComponentDirt value) override;

protected:
    void fontSizeChanged() override;

private:
    std::unique_ptr<TextVariationHelper> m_variationHelper;
    rcp<Font> m_variableFont;
    FontAsset* m_fontAsset = nullptr;
    std::unique_ptr<RenderPath> m_path;
    bool m_hasContents = false;
    std::vector<Font::Coord> m_coords;
    std::vector<TextStyleAxis*> m_variations;
};
} // namespace rive

#endif