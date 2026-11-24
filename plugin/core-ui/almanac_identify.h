#pragma once

#include <D2RLPlugin/inventory.h>
#include <vector>

namespace hotkeys {

// Keep the clicked target; also identify the inventory and Cube, as right-clicking the Almanac does.
inline auto CollectAlmanacTarget(const D2RL::PluginContext*, const D2RL::Items::ItemInfo* item, void* user) noexcept
    -> D2RL::Inventory::IterationAction {
    using namespace D2RL::Items;
    auto& ops = *static_cast<std::vector<ExistingItemOperation>*>(user);
    if ((item->container != ItemContainer::Inventory && item->container != ItemContainer::Cube) ||
        (item->stateFlags & ItemStateIdentified) != 0 || item->handle == ops.front().item) {
        return D2RL::Inventory::IterationAction::Continue;
    }
    ExistingItemOperation op {};
    op.structSize = ExistingItemOperationSize;
    op.kind = ExistingItemOperationKind::Edit;
    op.item = item->handle;
    op.edit = {.fields = EditFieldBit(EditField::Identified), .stateFlags = ItemStateIdentified};
    ops.push_back(op);
    return D2RL::Inventory::IterationAction::Continue;
}

}  // namespace hotkeys
