#ifndef OPENFALLOUT_ESM_FOGSTATE_H
#define OPENFALLOUT_ESM_FOGSTATE_H

namespace ESM
{
    class ESMReader;

    /// Reads past the fog of war of a cell, which follows the state of the cell in a record of a cell (CSTA) when its
    /// HFOW is set: the bounds (BOUN, an interior cell only), the angle of the north marker (ANGL) and the centre
    /// (CNTR) of an interior cell, and the images of the explored parts (FTEX, one for each). The engine wrote it for
    /// the map of Morrowind until that map was removed, so only saves of the engine from before that have it, and only
    /// to be skipped (format 0, saved games only).
    void skipFogState(ESMReader& esm);
}

#endif
