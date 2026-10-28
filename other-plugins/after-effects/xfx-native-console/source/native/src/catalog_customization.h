#pragma once

#include "search_core.h"
#include "settings.h"

namespace xfx {

std::wstring catalog_item_key(const CatalogItem& item);
// Merge settings aliases into a freshly scanned catalog. Build SearchEngine afterwards.
// Repeated calls do not duplicate aliases; edit/clear applies when the catalog is rescanned.
void apply_catalog_customizations(std::vector<CatalogItem>& items, const Settings& settings);

} // namespace xfx
