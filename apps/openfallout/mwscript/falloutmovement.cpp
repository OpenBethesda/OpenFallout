#include "falloutscripts.hpp"

#include <algorithm>
#include <cmath>

#include <components/esm4/loadarmo.hpp>
#include <components/esm4/loadcrea.hpp>
#include <components/esm4/loadrace.hpp>
#include <components/falloutscript/actortraits.hpp>
#include <components/falloutscript/actorvalues.hpp>
#include <components/falloutscript/perks.hpp>

#include "../mwworld/esmstore.hpp"
#include "../mwworld/ptr.hpp"

namespace OFScript
{
    namespace
    {
        namespace AV = FalloutScript::ActorValue;

        ESM::RefId refId(FalloutScript::FormId id)
        {
            const ESM::FormId form = ESM::FormId::fromUint32(id);
            return form.isZeroOrUnset() ? ESM::RefId() : ESM::RefId::formIdRefId(form);
        }

        /// The actor value that keeps crippled legs from slowing (IgnoreCrippledLimbs, rule B.M7)
        constexpr int ignoreCrippledLimbs = 72;

        /// The weight class of an armor by the general flags of its record (FalloutNV.exe 0x514410): heavy with 0x80,
        /// medium with 0x08
        FalloutScript::Movement::ArmorClass armorClassOf(const ESM4::Armor& armor)
        {
            if ((armor.mGeneralFlags & ESM4::Armor::FO3_HeavyArmor) != 0)
                return FalloutScript::Movement::ArmorClass::Heavy;
            if ((armor.mGeneralFlags & 0x08) != 0)
                return FalloutScript::Movement::ArmorClass::Medium;
            return FalloutScript::Movement::ArmorClass::LightOrNone;
        }
    }

    const FalloutScript::Movement::Settings& FalloutScripts::movementSettings()
    {
        if (!mMovementSettings)
            mMovementSettings = FalloutScript::Movement::Settings::fromGameSettings(
                [this](std::string_view name) -> std::optional<double> { return gameSetting(name); });
        return *mMovementSettings;
    }

    double FalloutScripts::carriedWeightOf(FalloutScript::FormId actor)
    {
        // What is carried weighs what its cards say (a perk can lighten the small things), less what the weight mods of
        // the weapons take off
        double weight = 0.0;
        std::vector<std::pair<FalloutScript::FormId, int>> held;
        if (actor == 0 || !mActors.items(actor, held))
            return weight;
        const bool packRat = isNewVegas() && mPerks.apply(FalloutScript::EntryPoint::packRat, actor, 0.0) > 0;
        for (const auto& [item, count] : held)
        {
            FalloutScript::ItemInfo info;
            if (!itemInfo(item, info))
                continue;
            weight += carriedWeight(info.mWeight, packRat) * count;
            // A mod of the effect 4 makes the weapon it is installed in lighter
            if (info.mKind != FalloutScript::ItemKind::Weapon)
                continue;
            FalloutScript::ItemMods::Slots slots;
            if (!modSlotsOf(item, slots))
                continue;
            for (const FalloutScript::ActorState::ConditionStack& stack : mActors.conditions(actor, item))
            {
                if (stack.mMods == 0)
                    continue;
                if (const std::optional<double> less
                    = FalloutScript::ItemMods::installedValue(stack.mMods, slots, FalloutScript::ItemMods::Weight))
                    weight -= (carriedWeight(info.mWeight, packRat)
                                  - carriedWeight(std::max(info.mWeight - *less, 0.0), packRat))
                        * stack.mCount;
            }
        }
        return weight;
    }

    bool FalloutScripts::overEncumbered(const OFWorld::Ptr& ptr)
    {
        const FalloutScript::FormId actor = scriptReferenceOf(ptr);
        if (actor == 0)
            return false;
        // PlayerCharacter::IsOverEncumbered (FalloutNV.exe 0x954CC0, Fallout3.exe 0x76D310): more than the carry
        // weight; a creature carries nothing that counts
        FalloutScript::ActorTraits traits;
        if (traitsOf(actor, traits) && traits.mCreature)
            return false;
        return carriedWeightOf(actor) > maxCarryWeight(actor);
    }

    bool FalloutScripts::heldStill(const OFWorld::Ptr& actor) const
    {
        const FalloutScript::FormId reference = scriptReferenceOf(actor);
        return reference != 0 && (mCombat.unconscious(reference) || mCombat.restrained(reference));
    }

    std::optional<osg::Vec3f> FalloutScripts::facingPoint(const OFWorld::Ptr& actor)
    {
        const FalloutScript::FormId target = mPresentation.facing(scriptReferenceOf(actor));
        if (target == 0)
            return std::nullopt;
        const OFWorld::Ptr found = findReference(target);
        if (found.isEmpty())
            return std::nullopt; // 0x901360: a target taken out of the world is let go
        return found.getRefData().getPosition().asVec3(); // vfunc 0x1F4
    }

    double FalloutScripts::sizeOf(const OFWorld::Ptr& ptr)
    {
        // The size factor of the executables (FalloutNV.exe 0x567400): the scale of the reference times the height of
        // the race for the sex of a character, or the scale of the record of a creature
        const double scale = ptr.isEmpty() ? 1.0 : ptr.getCellRef().getScale();
        double height = 1.0;
        FalloutScript::ActorTraits traits;
        const FalloutScript::FormId actor = scriptReferenceOf(ptr);
        if (actor != 0 && traitsOf(actor, traits))
        {
            if (traits.mCreature)
            {
                ESM::FormId base;
                if (baseOf(actor, base))
                    if (const ESM4::Creature* creature = mStore.get<ESM4::Creature>().search(base))
                        height = creature->mBaseScale;
            }
            else if (const ESM4::Race* race = mStore.get<ESM4::Race>().search(refId(traits.mRace)))
                height = traits.mFemale ? race->mHeightFemale : race->mHeightMale;
        }
        if (!std::isfinite(height) || height <= 0.0)
            height = 1.0;
        return scale * height;
    }

    FalloutScript::Movement::Mover FalloutScripts::moverOf(const OFWorld::Ptr& ptr, bool sneaking)
    {
        FalloutScript::Movement::Mover mover;
        const FalloutScript::FormId actor = scriptReferenceOf(ptr);
        mover.mSneaking = sneaking || actorForcesSneak(ptr);
        if (actor == 0)
            return mover;
        mover.mSpeedMult = mActorValues.current(actor, AV::speedMult);
        mover.mCrippledLegs = crippledLegs(ptr);
        mover.mIgnoreCrippledLimbs = mActorValues.current(actor, ignoreCrippledLimbs) != 0.0;
        FalloutScript::ActorTraits traits;
        mover.mHumanoid = !(traitsOf(actor, traits) && traits.mCreature);
        mover.mHolstered = !mEquipment.weaponDrawn(actor);
        mover.mWeaponAnimationType = heldWeapon(ptr).mAnimationType;
        if (mover.mHumanoid)
        {
            mover.mCarriedWeight = carriedWeightOf(actor);
            mover.mOverEncumbered = mover.mCarriedWeight > maxCarryWeight(actor);
            // The heaviest class of the armor that is worn (New Vegas reads the body armor; the heaviest of what is
            // worn is the same for the armor of the games, whose helmets have the class of their suit)
            for (const FalloutScript::FormId piece : mEquipment.armor(actor))
                if (const ESM4::Armor* armor = mStore.get<ESM4::Armor>().search(refId(piece)))
                    mover.mArmor = std::max(mover.mArmor, armorClassOf(*armor));
        }
        return mover;
    }

    double FalloutScripts::moveSpeed(const OFWorld::Ptr& ptr, bool running, bool sneaking, bool swimming)
    {
        const FalloutScript::Game game = isNewVegas() ? FalloutScript::Game::NewVegas : FalloutScript::Game::Fallout3;
        const FalloutScript::Movement::Settings& settings = movementSettings();
        const FalloutScript::Movement::Mover mover = moverOf(ptr, sneaking);
        // The run flag (or SetForceRun) counts only when the actor is not over the carry weight (rule B.M5c). Travel
        // Light (New Vegas, perk entry point 42) multiplies the run (Character::GetRunSpeed, FalloutNV.exe 0x885D60);
        // Fallout 3 has no entry there.
        const bool run = (running || actorForcesRun(ptr)) && FalloutScript::Movement::canRun(mover);
        double perk = 1.0;
        if (run && game == FalloutScript::Game::NewVegas)
            if (const FalloutScript::FormId actor = scriptReferenceOf(ptr); actor != 0)
                perk = mPerks.apply(FalloutScript::EntryPoint::runSpeed, actor, 1.0);
        if (swimming)
            return FalloutScript::Movement::swimSpeed(game, settings, mover, run, perk);
        return run ? FalloutScript::Movement::runSpeed(game, settings, mover, perk)
                   : FalloutScript::Movement::walkSpeed(game, settings, mover);
    }

    std::optional<double> FalloutScripts::characterMoveSpeed(const OFWorld::Ptr& ptr, bool running)
    {
        const FalloutScript::FormId actor = scriptReferenceOf(ptr);
        FalloutScript::ActorTraits traits;
        if (actor == 0 || (traitsOf(actor, traits) && traits.mCreature))
            return std::nullopt;
        return moveSpeed(ptr, running, false, false);
    }

    double FalloutScripts::creatureSpeedShare(const OFWorld::Ptr& ptr)
    {
        const FalloutScript::FormId actor = scriptReferenceOf(ptr);
        const double speedMult = actor != 0 ? mActorValues.current(actor, AV::speedMult) : 100.0;
        return std::max(speedMult, 0.0) / 100.0 * moveSpeedMultiplier(ptr);
    }

    double FalloutScripts::jumpHeight(const OFWorld::Ptr& ptr, bool swimming)
    {
        return FalloutScript::Movement::jumpHeight(movementSettings(), sizeOf(ptr), swimming);
    }

    bool FalloutScripts::isPlayerTeammate(const OFWorld::Ptr& ptr)
    {
        const FalloutScript::FormId actor = scriptReferenceOf(ptr);
        return actor != 0 && actor != mPlayer && isTeammate(actor);
    }

    bool FalloutScripts::isParalyzed(const OFWorld::Ptr& ptr)
    {
        const FalloutScript::FormId actor = scriptReferenceOf(ptr);
        return actor != 0 && mActorValues.current(actor, FalloutScript::ActorValue::paralysis) > 0;
    }
}
