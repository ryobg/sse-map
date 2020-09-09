/**
 * @file skse.cpp
 * @brief Implements SKSE plugin for SSE Map
 * @internal
 *
 * This file is part of Skyrim SE Map mod (aka Map).
 *
 *   Map is free software: you can redistribute it and/or modify it
 *   under the terms of the GNU Lesser General Public License as published
 *   by the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   Map is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *   GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public
 *   License along with Map. If not, see <http://www.gnu.org/licenses/>.
 *
 * @endinternal
 *
 * @ingroup Public API
 *
 * @details
 */

#include "maptrack.hpp"

//--------------------------------------------------------------------------------------------------

std::string const&
plugin_name ()
{
    static std::string v = "sse-maptrack";
    return v;
}

//--------------------------------------------------------------------------------------------------

static inline bool
segments_intersection (
        glm::vec2 const& pa1, glm::vec2 const& pb1,
        glm::vec2 const& pa2, glm::vec2 const& pb2,
        glm::vec2& out)
{
    float a1 = pb1.y - pa1.y;   // pa1 points to pb1
    float b1 = pa1.x - pb1.x;
    float a2 = pb2.y - pa2.y;   // pa2 points to pb2
    float b2 = pa2.x - pb2.x;

    float delta = a1*b2 - a2*b1;
    if (std::fabs (delta) <= std::numeric_limits<float>::epsilon ()) // in parallel
        return false;

    float c1 = a1*pa1.x + b1*pa1.y;
    float c2 = a2*pa2.x + b2*pa2.y;

    out.x = b2*c1 - b1*c2;
    out.y = a1*c2 - a2*c1;
    out /= delta;

    return true;
}

//--------------------------------------------------------------------------------------------------

static inline bool
rectangle_contains_point (glm::vec2 const& tl, glm::vec2 const& br, glm::vec2 const& p)
{
    return (tl.x <= p.x) && (tl.y <= p.y) && (br.x >= p.x) && (br.y >= p.y);
}

//--------------------------------------------------------------------------------------------------

static bool
rectange_contained_segment (
        glm::vec2 const& tl, glm::vec2 const& tr, glm::vec2 const& bl, glm::vec2 const& br,
        glm::vec2 const& a, glm::vec2 const& b,
        glm::vec2& out_a, glm::vec2& out_b)
{
    int i = 0;
    glm::vec2* out[2] = { &out_a, &out_b };

    // For MapTrack likely a segment will lie within the rect or at least cross it
    i += rectangle_contains_point (tl, br, a);
    if (i  > 0) *out[i-1] = a;
    i += rectangle_contains_point (tl, br, b);
    if (i  > 0) *out[i-1] = b;
    if (i == 2)
        return true;

    i += segments_intersection (tl, tr, a, b, *out[i]);
    if (i < 2) i += segments_intersection (tr, br, a, b, *out[i]);
    if (i < 2) i += segments_intersection (br, bl, a, b, *out[i]);
    if (i < 2) i += segments_intersection (bl, tl, a, b, *out[i]);

    return i == 2; // else all outside
}

//--------------------------------------------------------------------------------------------------

