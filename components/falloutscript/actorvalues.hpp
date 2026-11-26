#ifndef OPENFALLOUT_COMPONENTS_FALLOUTSCRIPT_ACTORVALUES_H
#define OPENFALLOUT_COMPONENTS_FALLOUTSCRIPT_ACTORVALUES_H

#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "saveaccess.hpp"
#include "script.hpp"

namespace FalloutScript
{
    class ActorState;
    class Interpreter;
    class PlayerProgress;

    /// The numbers scripts give to the actor values (the first argument of GetActorValue and the other commands about
    /// them), as the games number them: the ones that the rest of this component has to know by name
    namespace ActorValue
    {
        constexpr int strength = 5;
        constexpr int perception = 6;
        constexpr int endurance = 7;
        constexpr int charisma = 8;
        constexpr int intelligence = 9;
        constexpr int agility = 10;
        constexpr int luck = 11;
        constexpr int actionPoints = 12;
        constexpr int carryWeight = 13;
        constexpr int criticalChance = 14;
        constexpr int healRate = 15;
        constexpr int health = 16;
        constexpr int meleeDamage = 17;
        /// The resistances that the endurance gives (part E.1.7 of fallout-rules.md)
        constexpr int poisonResist = 19;
        constexpr int radResist = 20;
        constexpr int speedMult = 21;
        constexpr int fatigue = 22;
        constexpr int karma = 23;
        constexpr int experience = 24;
        /// The conditions of the head and the limbs (perception, endurance, both arms, both legs and the brain)
        constexpr int firstCondition = 25;
        constexpr int lastCondition = 31;
        constexpr int firstSkill = 32;
        /// The skill that the prices of the barter menu go by
        constexpr int barter = firstSkill;
        /// The skill that repairs go by (0x27 in the executable): the repair menu, and the cost of a vendor's repair
        constexpr int repair = 39;
        constexpr int sneak = 42;
        constexpr int unarmed = 45;
        /// Barter first, unarmed last: the 14 skills of the records (the one in the middle of the end is survival in
        /// New Vegas and the unused throwing in Fallout 3)
        constexpr int lastSkill = 45;
        /// Above 0 the actor cannot move or act (the Paralysis effect raises it; rule E of the docs)
        constexpr int paralysis = 47;
        /// What hides a character from V.A.T.S. (it halves the chance to hit it): invisibility and chameleon
        constexpr int invisibility = 48;
        constexpr int chameleon = 49;
        /// What a blow with the hands adds to its damage, from the Unarmed skill (part E.1.6 of fallout-rules.md)
        constexpr int unarmedDamage = 56;
        /// The numbers from here are not actor values
        constexpr int count = 128;
    }

    /// The health below which an actor is out of it: the games test "below 1.0" after every change of the health
    /// (FalloutNV.exe 0x898650; Fallout 3 the same; fallout-rules.md D.D1), so 0.4 health left by a fall kills
    constexpr double deathHealth = 1.0;

    /// Whether an actor with this much health is out of it: dead, or down if it is one that falls (an essential one)
    constexpr bool outOfHealth(double health)
    {
        return health < deathHealth;
    }

    /// What the records of the game say about the actor values of a character or creature before a script changes
    /// anything
    class ActorValueRecords
    {
    public:
        virtual ~ActorValueRecords() = default;

        /// The actor values that the record of the reference gives it, by their numbers. False when the host can not
        /// tell what the reference is: it is asked again by the next command that needs it. True with none when the
        /// reference has no record of an actor.
        virtual bool initialValues(FormId actor, std::vector<std::pair<int, double>>& values) = 0;

        /// The level that the record of the reference gives it for a player of that level (a record can take a share
        /// of it). False when the host can not tell what the reference is or it is not a character or creature.
        virtual bool levelOf(FormId /*actor*/, int /*playerLevel*/, int& /*level*/) { return false; }

        /// The value that the games work out for the actor from its other actor values (the health from its
        /// endurance and level, the action points from its agility, ...), when the actor value is one of those and the
        /// host can tell. `permanent` gives the value of another actor value without its damage (the health goes by it:
        /// the endurance of the level-up), `current` the value with it (the action points, the carry weight, the
        /// critical chance, the melee and unarmed damage and the resistances go by it, FalloutNV.exe 0x643CE3).
        virtual std::optional<double> derivedValue(FormId /*actor*/, int /*value*/,
            const std::function<double(int)>& /*permanent*/, const std::function<double(int)>& /*current*/)
        {
            return std::nullopt;
        }
    };

    /// The actor values of characters and creatures, as far as nothing else in the game keeps them yet. An actor value
    /// of a reference is its base value (what SetActorValue sets), a modifier that stays (ModActorValue) and the damage
    /// that scripts did to it (DamageActorValue, which RestoreActorValue takes back): the value the reference has now
    /// is the base plus the modifier minus the damage. A base that no script has set is the one its record gives,
    /// when the host knows one, and else a value that stands for a fresh character, which is what the conditions of
    /// the games ask for most (nothing crippled, nothing hurt). It can be used from any thread.
    class ActorValues
    {
        template <class T>
        friend struct SaveAccess;

    public:
        explicit ActorValues(ActorValueRecords* records = nullptr)
            : mRecords(records)
        {
        }

        static bool isValid(int value) { return value >= 0 && value < ActorValue::count; }

        /// Whether the games work the actor value out from others (see ActorValueRecords::derivedValue)
        static bool isDerived(int value);

        /// What a reference has of an actor value until a script sets it: 5 for each of the SPECIAL attributes, a full
        /// health, fatigue, speed and condition of the head and limbs, an average skill and nothing else
        static double startingValue(int value);

        /// Forgets everything, as for a new game
        void clear();

        /// The base value
        double base(FormId actor, int value) const;
        /// The base plus the modifier
        double permanent(FormId actor, int value) const;
        /// The permanent value minus the damage
        double current(FormId actor, int value) const;
        /// The modifier that stays (what ModActorValue, the points of a level-up and the skill books added), not
        /// clamped
        double modifier(FormId actor, int value) const;

        void setBase(FormId actor, int value, double amount);
        /// Whether a script or the game set the base (else it is the record's, the derived one or the starting one)
        bool hasBase(FormId actor, int value) const;
        /// Forgets the base that was set: the value is the record's, the derived one or the starting one again
        void resetBase(FormId actor, int value);
        /// Adds to the base (a negative amount takes away), to no less than the minimum; the reading and the writing
        /// are one step, so that changes made at the same time all count
        void addBase(FormId actor, int value, double amount, double minimum);
        void modify(FormId actor, int value, double amount);
        /// Changes the permanent modifier so that the current value is the amount, whatever the base and the damage are
        void setCurrent(FormId actor, int value, double amount);
        /// Hurts the actor value (a negative amount takes damage back, down to none)
        void damage(FormId actor, int value, double amount);
        /// Takes damage back, up to what there is
        void restore(FormId actor, int value, double amount);
        /// Takes all the damage back, and the modifier that left nothing of the value
        void heal(FormId actor, int value);
        /// The level of a character or creature that its record gives for a player of that level, none when the
        /// records do not tell
        std::optional<int> levelOf(FormId actor, int playerLevel) const;

    private:
        struct Entry
        {
            std::optional<double> mBase; // none until a script sets it
            double mModifier = 0;
            double mDamage = 0;
        };

        /// What the actor has of the value until a script sets it: the record's, or the one worked out from other
        /// values, or else the starting value. The caller holds the lock.
        double startOf(FormId actor, int value) const;
        /// The value with its modifier and damage, as current() gives it (the caller holds mMutex)
        double currentOf(FormId actor, int value) const;
        /// The base plus the modifier, for a caller that holds the lock
        double permanentOf(FormId actor, int value) const;
        /// An entry with its base filled in, for the actor value of that number
        double baseOf(FormId actor, const Entry& entry, int value) const;

        ActorValueRecords* mRecords;
        mutable std::mutex mMutex;
        std::map<std::pair<FormId, int>, Entry> mEntries;
        /// What the records said, once they could: by actor, then by actor value
        mutable std::map<FormId, std::map<int, double>> mFromRecords;
    };

    /// Gives the commands about actor values to the interpreter: GetActorValue, GetBaseActorValue,
    /// GetPermanentActorValue, SetActorValue, ModActorValue, ForceActorValue, DamageActorValue and RestoreActorValue,
    /// and those that depend on the health of a character: GetHealthPercentage, ResetHealth and ResurrectActor (a
    /// character that comes back has all its health), GetWeaponHealthPerc, and GetLevel. Karma and experience of the
    /// player are the ones that PlayerProgress keeps, and a character whose health is damaged to nothing is dead for
    /// ActorState. Those that the command table does not have are left out.
    void addActorValueCommands(
        Interpreter& interpreter, ActorValues& values, ActorState& actors, PlayerProgress& progress);
}

#endif
