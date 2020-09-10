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

maptrack_t maptrack = {};

//--------------------------------------------------------------------------------------------------

std::string const&
plugin_name ()
{
    static std::string v = "sse-maptrack";
    return v;
}

//--------------------------------------------------------------------------------------------------

static inline bool
rectangle_contains_point (glm::vec2 const& tl, glm::vec2 const& br, glm::vec2 const& p)
{
    return (tl.x <= p.x) && (tl.y <= p.y) && (br.x >= p.x) && (br.y >= p.y);
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

    // Exploit that the 2nd segment is the line we care about. The 1st is 1 of the 4 rect lines.
    return rectangle_contains_point (glm::min (pa2, pb2), glm::max (pa2, pb2), out);
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

static void
accumulate_time_map (track_point const& prev, track_point const& next)
{
    auto rsz = 1.f / maptrack.tmap.resolution;
    auto ga = maptrack.game_to_map (prev.p.xy ());
    auto gb = maptrack.game_to_map (next.p.xy ());
    auto id = 1.f / (next.d * maptrack.scale);
    auto dt = next.t - prev.t;


    // Bounding box, top-left and bottom-right 2d integers to index within the destination array
    glm::ivec2 bbtli (glm::floor (glm::min (ga, gb) / rsz));
    glm::ivec2 bbbri (glm::ceil  (glm::max (ga, gb) / rsz));

    bbtli = glm::max (bbtli, glm::ivec2 {0, 0});        ///< Clamp in case a point is outside map
    bbbri = glm::min (bbbri, maptrack.tmap.resolution);

    // Iterate over all rects inside the ab segment bounding box
    for (glm::ivec2 i = bbtli; i.y < bbbri.y; ++i.y)
        for (i.x = bbtli.x; i.x < bbbri.x; ++i.x)
    {
        glm::vec2 ia, ib;
        glm::vec2 tl = glm::vec2 (i) * rsz;
        float& v = maptrack.tmap.vals[i.x + i.y * maptrack.tmap.resolution];

        if (rectange_contained_segment (
                    tl, glm::vec2 (tl.x + rsz, tl.y), glm::vec2 (tl.x, tl.y + rsz), tl + rsz,
                    ga, gb, ia, ib))
        {
            // Intersection length is percentage of the time duration represented by the
            // checked segment.
            auto d = glm::distance (ia, ib);
            v += d * id * dt;
            maptrack.tmap.vlo = std::min (maptrack.tmap.vlo, v);
            maptrack.tmap.vhi = std::max (maptrack.tmap.vhi, v);
        }
    }
}

//--------------------------------------------------------------------------------------------------

void
reset_time_map (track_t::const_iterator begin, track_t::const_iterator end)
{
    maptrack.tmap.vlo = max_float;
    maptrack.tmap.vhi = min_float;

    maptrack.tmap.vals.resize (maptrack.tmap.resolution * maptrack.tmap.resolution);
    std::fill (maptrack.tmap.vals.begin (), maptrack.tmap.vals.end (), 0.f);

    std::adjacent_find (begin, end,
            [] (auto const& a, auto const& b) { accumulate_time_map (a, b); return false; });
}

//--------------------------------------------------------------------------------------------------

