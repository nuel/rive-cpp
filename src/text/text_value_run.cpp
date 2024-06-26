#include "rive/core_context.hpp"
#include "rive/text/text.hpp"
#include "rive/text/text_style.hpp"
#include "rive/text/text_value_run.hpp"
#include "rive/container_component.hpp"

using namespace rive;

void TextValueRun::textChanged() { parent()->as<Text>()->markShapeDirty(); }

StatusCode TextValueRun::onAddedClean(CoreContext* context)
{
    StatusCode code = Super::onAddedClean(context);
    if (code != StatusCode::Ok)
    {
        return code;
    }

    if (parent() != nullptr && parent()->is<Text>())
    {
        parent()->as<Text>()->addRun(this);
        return StatusCode::Ok;
    }

    return StatusCode::MissingObject;
}

StatusCode TextValueRun::onAddedDirty(CoreContext* context)
{
    StatusCode code = Super::onAddedDirty(context);
    if (code != StatusCode::Ok)
    {
        return code;
    }
    auto coreObject = context->resolve(styleId());
    if (coreObject == nullptr || !coreObject->is<TextStyle>())
    {
        return StatusCode::MissingObject;
    }

    m_style = static_cast<TextStyle*>(coreObject);

    return StatusCode::Ok;
}