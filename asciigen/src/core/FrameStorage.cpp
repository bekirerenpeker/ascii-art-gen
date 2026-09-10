#include "core/FrameStorage.hpp"

void FrameStorage::allocate(int cols, int rows, int planeW, int planeH)
{
    // buffer.setSize() resizing to the size it's already at costs nothing (the backing
    // vector doesn't reallocate). plane needs its own guard for the same case -- Image's
    // constructor always allocates unconditionally, so calling this again on an
    // already-sized instance (a reused video slot, every frame after the first) would
    // otherwise free and reallocate for no reason.
    buffer.setSize(cols, rows);
    if (plane.width != planeW || plane.height != planeH || plane.depth != 3)
        plane = Image(planeW, planeH, 3);
    if (ditheredPlane.width != planeW || ditheredPlane.height != planeH || ditheredPlane.depth != 3)
        ditheredPlane = Image(planeW, planeH, 3);
}
