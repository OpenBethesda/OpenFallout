#ifndef OPENFALLOUT_MWPHYSICS_COLLISIONTYPE_H
#define OPENFALLOUT_MWPHYSICS_COLLISIONTYPE_H

namespace OFPhysics
{

    enum CollisionType
    {
        CollisionType_World = 1 << 0,
        CollisionType_Door = 1 << 1,
        CollisionType_Actor = 1 << 2,
        CollisionType_HeightMap = 1 << 3,
        CollisionType_Projectile = 1 << 4,
        CollisionType_Water = 1 << 5,
        CollisionType_Default
        = CollisionType_World | CollisionType_HeightMap | CollisionType_Actor | CollisionType_Door,
        CollisionType_AnyPhysical = CollisionType_World | CollisionType_HeightMap | CollisionType_Actor
            | CollisionType_Door | CollisionType_Projectile | CollisionType_Water,
        CollisionType_CameraOnly = 1 << 6,
        CollisionType_VisualOnly = 1 << 7,
        /// The bodies of a character that is down, moved by its ragdoll (the layer BIPED of the executables): the
        /// living, the projectiles and the lines of sight meet them, the camera does not (NV 0xC828F0)
        CollisionType_Ragdoll = 1 << 8
    };
}

#endif
