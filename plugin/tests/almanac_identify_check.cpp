#include "almanac_identify.h"

#include <cstdio>

int main() {
    using namespace D2RL::Items;
    int failed = 0;
    auto check = [&](bool ok) { if (!ok) ++failed; };
    ExistingItemOperation clicked {};
    clicked.item = 1;
    std::vector<ExistingItemOperation> ops {clicked};
    auto visit = [&](D2RL::ItemHandle handle, ItemContainer container, uint32_t flags = 0) {
        ItemInfo item {.structSize = ItemInfoSize};
        item.handle = handle;
        item.container = container;
        item.stateFlags = flags;
        check(hotkeys::CollectAlmanacTarget(nullptr, &item, &ops) == D2RL::Inventory::IterationAction::Continue);
    };
    visit(1, ItemContainer::Inventory);  // clicked item is not duplicated
    visit(2, ItemContainer::Inventory);
    visit(3, ItemContainer::Cube);
    visit(4, ItemContainer::Inventory, ItemStateIdentified);
    visit(5, ItemContainer::PersonalStash);
    visit(6, ItemContainer::SharedStash);
    visit(7, ItemContainer::Equipment);
    visit(8, ItemContainer::CustomPage);
    check(ops.size() == 3);
    if (ops.size() == 3) check(ops[0].item == 1 && ops[1].item == 2 && ops[2].item == 3);
    for (size_t i = 1; i < ops.size(); ++i) {
        check(ops[i].structSize == ExistingItemOperationSize);
        check(ops[i].kind == ExistingItemOperationKind::Edit);
        check(ops[i].edit.fields == EditFieldBit(EditField::Identified));
        check(ops[i].edit.stateFlags == ItemStateIdentified);
    }
    // Keep an explicitly clicked stash target without sweeping its neighbors.
    ops.resize(1);
    visit(1, ItemContainer::PersonalStash);
    visit(9, ItemContainer::PersonalStash);
    visit(2, ItemContainer::Inventory);
    check(ops.size() == 2);
    std::printf("Almanac identify targets: %s (%d failures)\n", failed == 0 ? "PASS" : "FAIL", failed);
    return failed != 0;
}
