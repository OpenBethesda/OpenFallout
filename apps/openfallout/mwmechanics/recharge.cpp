#include "recharge.hpp"

#include <algorithm>

#include <components/esm3/loadgmst.hpp>

#include "../mwbase/environment.hpp"

#include "../mwworld/cellref.hpp"
#include "../mwworld/esmstore.hpp"

namespace OFMechanics
{

    bool rechargeItem(const OFWorld::Ptr& item, const float maxCharge, const float duration)
    {
        float charge = item.getCellRef().getEnchantmentCharge();
        if (charge == -1 || charge == maxCharge)
            return false;

        static const float fMagicItemRechargePerSecond = OFBase::Environment::get()
                                                             .getESMStore()
                                                             ->get<ESM::GameSetting>()
                                                             .find("fMagicItemRechargePerSecond")
                                                             ->mValue.getFloat();

        item.getCellRef().setEnchantmentCharge(std::min(charge + fMagicItemRechargePerSecond * duration, maxCharge));
        return true;
    }
}
