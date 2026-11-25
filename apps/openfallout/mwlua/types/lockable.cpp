#include "types.hpp"

#include <components/esm3/loadmisc.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/resource/resourcesystem.hpp>

#include "apps/openfallout/mwworld/esmstore.hpp"

namespace OFLua
{

    void addLockableBindings(sol::table lockable)
    {
        lockable["getLockLevel"]
            = [](const Object& object) { return std::abs(object.ptr().getCellRef().getLockLevel()); };
        lockable["isLocked"] = [](const Object& object) { return object.ptr().getCellRef().isLocked(); };
        lockable["getKeyRecord"] = [](const Object& object) -> sol::optional<const ESM::Miscellaneous*> {
            ESM::RefId key = object.ptr().getCellRef().getKey();
            if (key.empty())
                return sol::nullopt;
            return OFBase::Environment::get().getESMStore()->get<ESM::Miscellaneous>().find(key);
        };
        lockable["lock"] = [](const GObject& object, sol::optional<int> lockLevel) {
            object.ptr().getCellRef().setLocked(true);

            int level = 1;

            if (lockLevel)
                level = lockLevel.value();
            else if (object.ptr().getCellRef().getLockLevel() < 0)
                level = -object.ptr().getCellRef().getLockLevel();
            else if (object.ptr().getCellRef().getLockLevel() > 0)
                level = object.ptr().getCellRef().getLockLevel();

            object.ptr().getCellRef().setLockLevel(level);
        };
        lockable["unlock"] = [](const GObject& object) {
            if (!object.ptr().getCellRef().isLocked())
                return;
            object.ptr().getCellRef().setLocked(false);

            object.ptr().getCellRef().setLockLevel(-object.ptr().getCellRef().getLockLevel());
        };
        lockable["setKeyRecord"] = [](const GObject& object, const sol::object& itemOrRecordId) {
            if (itemOrRecordId == sol::nil)
            {
                object.ptr().getCellRef().setKey(ESM::RefId()); // remove the trap value
                return;
            }
            if (itemOrRecordId.is<ESM::Miscellaneous>())
                object.ptr().getCellRef().setKey(itemOrRecordId.as<const ESM::Miscellaneous*>()->mId);
            else
            {
                ESM::RefId miscId = ESM::RefId::deserializeText(LuaUtil::cast<std::string_view>(itemOrRecordId));
                const auto& keyStore = OFBase::Environment::get().getESMStore()->get<ESM::Miscellaneous>();
                const ESM::Miscellaneous* key = keyStore.find(miscId);
                object.ptr().getCellRef().setKey(key->mId);
            }
        };
    }
}
