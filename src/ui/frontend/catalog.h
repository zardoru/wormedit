#pragma once
#include "common.h"

// Stable enum ids and process-lifetime labels projected from the core catalogs.
typedef struct WormUiKindDescriptorView {
    uint32_t id;
    const char* label;
} WormUiKindDescriptorView;

typedef struct WormUiKindCatalogView {
    const WormUiKindDescriptorView* entries;
    size_t count;
} WormUiKindCatalogView;
