#ifndef MWGUI_COMPANION_ITEM_MODEL_H
#define MWGUI_COMPANION_ITEM_MODEL_H

#include "inventoryitemmodel.hpp"

namespace OFGui
{

    /// @brief The inventory of a companion. (The profit of a Morrowind companion was a variable of its Morrowind
    /// script, "minimumprofit", which OpenFallout no longer has.)
    class CompanionItemModel : public InventoryItemModel
    {
    public:
        CompanionItemModel(const OFWorld::Ptr& actor);
    };

}

#endif
