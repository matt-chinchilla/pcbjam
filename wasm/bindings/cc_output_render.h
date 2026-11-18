// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine run 15: the render job (pcb_render), called by cc_output::MergeSettings and read by
// cc_output::JobsWithDefaults. Included ONLY at the end of cc_output_common.h, which declares
// both functions before their use. Lane e11 owns the bodies after the seams commit (the render
// job compiled in, no dialog, a yield and a percent line per pass). The seam refuses every
// render and keeps pcb_render out of kicadOutputJobs.

#pragma once

#ifdef __EMSCRIPTEN__

namespace cc_output
{

/** The pcb_render fixes on aMerged; the seam refuses the run. */
inline void ccFixPcbRender( json& aMerged, const json& aDefaults, MERGE_RESULT& aOut )
{
    (void) aMerged;
    (void) aDefaults;
    aOut.refusal = "The render is not made here.";
}

/** Whether kicadOutputJobs lists pcb_render (false until the render job runs). */
inline bool ccPcbRenderListed()
{
    return false;
}

} // namespace cc_output

#endif // __EMSCRIPTEN__
