#ifndef OPENFALLOUT_COMPONENTS_FALLOUTSCRIPT_PROCESSLEVELS_H
#define OPENFALLOUT_COMPONENTS_FALLOUTSCRIPT_PROCESSLEVELS_H

#include <cstdint>
#include <string_view>

namespace FalloutScript
{
    /// The process tier of an actor (the level byte at process+0x28 in both executables, saved as a signed byte): what
    /// the world keeps of the actor's life. High is updated every frame, has a character controller and a ragdoll when
    /// its 3D is loaded; Middle High can have them too; Middle Low and Low keep only what an actor that is not near needs.
    enum class ProcessLevel : std::int8_t
    {
        None = -1,
        High = 0,
        MiddleHigh = 1,
        MiddleLow = 2,
        Low = 3,
    };

    /// The names the executables print for the levels ("High", "Middle High", "Middle Low", "Low"); "None" for none
    std::string_view processLevelName(ProcessLevel level);

    /// What the world has made of the cell an actor is in: the cell state byte (cell+0x26) the executables read. A cell
    /// that is attached is one of the scene, around the player (states 5 and 6); one that is loaded is in memory but not
    /// in the scene (states 2 to 4).
    enum class CellLoad : std::uint8_t
    {
        None, ///< no cell, or a cell that is not loaded
        Loaded,
        Attached,
    };

    /// What the choice of the level reads of an actor
    struct ProcessInputs
    {
        CellLoad mCell = CellLoad::None;
        /// The cell has the flag that keeps its actors in Middle Low with no 3D (bit 9, FalloutNV.exe 0x551620)
        bool mCellKeepsActors = false;
        /// The actor's 3D is loaded
        bool mHas3D = false;
        /// The actor is a follower of the player (the third test of FalloutNV.exe 0x9334b0, 0x445750)
        bool mTeammate = false;
        /// The world is attaching cells in bulk: every actor is taken to be interesting
        bool mBulkLoading = false;
    };

    /// The level an actor should have (FalloutNV.exe 0x9334b0, Fallout3.exe 0x766d80): it is about cells, not
    /// distance. An interesting actor (its 3D loaded, a follower, or a bulk load) is High in an attached cell and
    /// Middle High in a loaded one; any other actor in a loaded or attached cell, or in a cell with the keep flag, is
    /// Middle Low; everything else, an actor with no cell among them, is Low (the cell-state helper 0x4511e0 says no for
    /// a null cell).
    ProcessLevel desiredProcessLevel(const ProcessInputs& inputs);

    /// The life state of an actor (Actor+0x108 in New Vegas, +0xFC in Fallout 3), with the names of the executables'
    /// table; 6 is the down state of an essential actor that the dead test counts with the others
    enum class LifeState : std::uint8_t
    {
        Alive = 0,
        Dying = 1,
        Dead = 2,
        Unconscious = 3,
        Reanimate = 4,
        Restrained = 5,
        EssentialDown = 6,
    };

    /// IsDead (FalloutNV.exe 0x8844f0): dying or dead, and the essential down state too when `strict` is false
    bool isDeadState(LifeState state, bool strict);

    /// The knock state of an actor with a Middle High or High process (MiddleHighProcess+0x13C), named as the
    /// executables' table names it
    enum class KnockState : std::uint8_t
    {
        Normal = 0,
        Explode = 1,
        ExplodeLeadIn = 2,
        KnockedOut = 3,
        KnockOutLeadIn = 4,
        Queued = 5,
        GettingUp = 6,
    };
}

#endif
