#pragma once
#include "ui/frontend.h"
namespace wormedit::sys {
template <class FrontendRect = WormUiComposeRect, class Rect>
auto to_frontend_rect(const Rect& rect) -> FrontendRect {
    return {rect.left, rect.top, rect.right, rect.bottom};
}
} // namespace wormedit::sys
