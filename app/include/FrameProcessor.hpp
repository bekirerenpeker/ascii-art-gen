#pragma once

#include "Options.hpp"
#include "bitmap/Resample.hpp"
#include "core/Charset.hpp"
#include "core/FrameStorage.hpp"
#include "font/Font.hpp"
#include "font/GlyphAtlas.hpp"

namespace FrameProcessor {

// Everything FrameProcessor::run needs that does NOT change frame to frame -- built once
// (by Pipeline::run for a still image; once before spinning up video workers, later) and
// read-only across every call. Dithering::options/Edges::options/Edges::alphaOptions are
// configured once alongside this, the same way -- see Pipeline.cpp.
struct Context
{
    const Font* font = nullptr;
    // Not const: Ramp::generate and Edges::apply both append missing glyphs to the
    // charset (directional glyphs for edges, ramp characters for Ramp) rather than
    // failing on one that's short, so this has to stay mutable.
    Charset* charset = nullptr;
    const GlyphAtlas* matchAtlas = nullptr;
    const GlyphAtlas* renderAtlas = nullptr;   // null if no image-format output was asked for
    Resample::Filter resampleFilter = Resample::Filter::Auto;
    int planeW = 0, planeH = 0;

    // Built once (Pipeline::run today; once before spinning up video workers, later)
    // from `matchAtlas` and the relevant `opts.algo.*` fields, neither of which change
    // frame to frame -- generate() used to rebuild the equivalent of these on every
    // single call. Only the one matching opts.algo.name actually gets used; the other
    // is left default and ignored.
    Structure::GlyphModel structureModel;
    Bitmask::Model bitmaskModel;
};

// The whole per-frame pipeline: alpha extraction -> resample -> source filters ->
// algorithm select -> edges -> cell colour grading -> backdrop -> render. Reads
// `frame.input` (already populated by whatever loaded it -- a still image, or a
// decoded video frame) and writes everything else in `frame` in place -- except the
// rendered pixel output, which goes into the caller-owned `renderedImage` instead
// (left untouched if ctx.renderAtlas is null, i.e. no image-format output was asked
// for). Not a FrameStorage member on purpose: see FrameStorage.hpp's own note on why
// -- the short version is that the slot needs to be freeable the instant this
// function returns, and it can't be if the render result is still sitting inside it
// waiting for something else to collect it.
//
// Identical whether this is driven by a single still image or a worker thread inside
// a video run -- the only things that differ around it are what filled `frame.input`
// beforehand and what happens to `frame.text`/`renderedImage` afterward. This is the
// whole point: loading and saving stay in charge of their own formats, this never has
// to know which one is on either side of it.
void run(FrameStorage& frame, const App::Options& opts, const Context& ctx, Image& renderedImage);

}   // namespace FrameProcessor
