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

// Stable string identities for catalogs whose entries distinguish profile variants.
typedef struct WormUiStringDescriptorView {
    const char* id;
    const char* label;
} WormUiStringDescriptorView;
typedef struct WormUiStringCatalogView {
    const WormUiStringDescriptorView* entries;
    size_t count;
    const char* const* labels;
} WormUiStringCatalogView;
