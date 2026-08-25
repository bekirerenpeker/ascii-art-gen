#pragma once

#include "bitmap/Bitmask.hpp"
#include "bitmap/Structure.hpp"
#include "core/CellBuffer.hpp"
#include "core/Image.hpp"
#include "edges/EdgeField.hpp"
#include <string>

// Everything one frame's worth of processing writes into, held in one place so it can
// live for the whole run instead of being allocated fresh per frame -- one instance for a
// still image, several of them (one per in-flight frame) for video. Pure data: sizing and
// filling it is FrameProcessor's job (app/), since that needs App::Options and this
// doesn't -- staying a plain engine-layer type is what lets it sit next to Image and
// CellBuffer instead of needing to live in app/ alongside the thing that populates it.
//
// This does mean core/ pulls in bitmap/ and edges/ headers, which inverts the usual
// low-level/high-level direction of that folder split. Deliberate: the per-algorithm
// Scratch structs below only make sense held per in-flight frame (one independent copy
// per worker, later), and this struct is exactly "the thing held per in-flight frame" --
// splitting them out to dodge the folder convention would just mean threading three more
// objects through every call that touches a frame instead of the one it already needs.
// Not every member is always used. A text-only output run never touches `renderedImage`,
// which just stays at its default-constructed empty state (Image() doesn't allocate until
// it's given a size) instead of needing a separate code path for that case; only one of
// structureScratch/bitmaskScratch is ever touched, matching whichever algorithm is
// selected -- the other sits empty for the run.
struct FrameStorage
{
    Image input;            // decoded/loaded source frame -- sized by whatever loads it
    Image alphaSource;      // extracted alpha channel, only if the source has one and it's asked for
    Image plane;             // resampled + source-filtered working plane, at algorithm resolution
    Image ditheredPlane;      // dithered copy of `plane`, what the algorithm actually reads
    CellBuffer buffer;         // the character grid: glyphs + colours
    std::string text;           // ANSI-rendered output, always produced
    Image renderedImage;         // rendered pixel output, only if an image-format output was asked for

    Structure::Scratch structureScratch;   // only touched when --algo structure
    Bitmask::Scratch bitmaskScratch;       // only touched when --algo bitmask
    Edges::Scratch edgesScratch;           // only touched when --edge/--edge-alpha is on

    // Sizes every buffer here that CAN be known up front (buffer, plane, ditheredPlane),
    // so a call to FrameProcessor::run afterward allocates nothing new in the steady-state
    // case. Takes the already-resolved sizes rather than re-deriving them -- deriving them
    // needs the source's own dimensions (a loaded still, or a video's metadata), which is
    // whatever's driving this struct's job, not this struct's own.
    void allocate(int cols, int rows, int planeW, int planeH);
};
