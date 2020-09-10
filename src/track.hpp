/**
 * @file track.hpp
 * @brief Small (still) implementation of track/route on a map
 * @internal
 *
 * This file is part of Skyrim SE Map Tracker mod (aka MapTrack).
 *
 *   MapTrack is free software: you can redistribute it and/or modify it
 *   under the terms of the GNU Lesser General Public License as published
 *   by the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   MapTrack is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *   GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public
 *   License along with MapTrack. If not, see <http://www.gnu.org/licenses/>.
 *
 * @endinternal
 *
 * @ingroup Core
 *
 * @details
 */

#ifndef TRACK_HPP
#define TRACK_HPP

#ifndef GLM_FORCE_CXX14
#define GLM_FORCE_CXX14
#endif

#ifndef GLM_FORCE_SWIZZLE
#define GLM_FORCE_SWIZZLE
#endif

#include <glm/glm.hpp>
#include <glm/gtx/range.hpp>
#include <glm/gtx/norm.hpp>

#ifndef GSL_THROW_ON_CONTRACT_VIOLATION
#define GSL_THROW_ON_CONTRACT_VIOLATION
#endif

#include <gsl/gsl_assert>

#include <vector>
#include <limits>
#include <algorithm>
#include <numeric>

//--------------------------------------------------------------------------------------------------

inline constexpr float max_float =  16'777'216.f,   ///< Some sane limits, because:
                       min_float = -16'777'216.f;   ///< This turns to zero if min limit values
inline constexpr float nan_float = std::numeric_limits<float>::quiet_NaN ();

struct track_point
{
    glm::vec3 p;    ///< XYZ position in game space
    float t;        ///< Game time at which this position was recorded
    float d;        ///< Computed distance between this and the previous point
};

//--------------------------------------------------------------------------------------------------

/// Wraps common ops and caches with regard a tracked route

class track_t
{
public:

    typedef std::vector<track_point>::const_iterator const_iterator;

    track_t () : min_distance (0)
    {
        clear ();
    }

    void merge_distance (float d)
    {
        Expects (std::isfinite (d) && d >= 0);
        min_distance = d;
    }

    float last_time () const {
        return values.empty () ? 0.f : values.back ().t;
    }
    std::size_t size () const {
        return values.size ();
    }
    auto bounding_box () const {
        return values.empty () ? std::make_pair (glm::vec3 {0}, glm::vec3 {0})
                               : std::make_pair (lo, hi);
    }

    void clear ()
    {
        reset_lohi ();
        invalidate_time_range ();
        values.clear ();
    }

    /// Pretty generic way to write a binary blob into stream-like object
    template<class OStream>
    void save_binary (OStream& os)
    {
        auto size = static_cast<std::uint32_t> (values.size ());
        os.write (reinterpret_cast<const char*> (&size), sizeof (size));
        for (auto const& p: values)
        {
            glm::vec4 v (p.p.x, p.p.y, p.p.z, p.t);
            os.write (reinterpret_cast<const char*> (&v), sizeof (v));
        }
    }

    /// Counterpart of #save_binary()
    template<class IStream>
    void load_binary (IStream& is)
    {
        std::uint32_t size = 0;
        is.read (reinterpret_cast<char*> (&size), sizeof (size));
        values.resize (size);

        glm::vec3 g (0,0,0);
        for (auto& v: values)
        {
            glm::vec4 p;
            is.read (reinterpret_cast<char*> (&p), sizeof (glm::vec4));
            v = track_point { p.xyz (), p.w, glm::distance (p.xyz (), g) };
            g = p.xyz ();
        }
        if (size) values.front ().d = 0;

        update_lohi ();
        invalidate_time_range ();
    }

    /// Adds new point, eventually overriding the history (for example when a game is loaded)
    void add_point (glm::vec3 const& p, float t)
    {
        Expects (std::isfinite (p.x) && std::isfinite (p.y)
              && std::isfinite (p.z) && std::isfinite (  t));

        float d = 0;
        if (!values.empty ())
        {
            if (values.back ().t > t)
            {
                values.erase (std::upper_bound (
                        values.begin (), values.end (), t,
                        [] (float t, auto const& p) { return t < p.t; }),
                        values.end ());
                update_lohi ();
            }

            if (d = glm::distance (p, values.back ().p); min_distance < d)
               values.push_back (track_point {});
            else if (d = 0; values.size () > 1)
                d = glm::distance (p, values[values.size () - 2].p);

        } else values.push_back (track_point {});

        values.back () = track_point { p, t, d };
        update_lohi (p);
    }

    /// Quick access into subrange of tracked points between a given time range
    std::tuple<bool, bool, const_iterator, const_iterator, const_iterator>
    pull_track_range (float t_start, float t_end)
    {
        Expects (std::isfinite (t_start) && std::isfinite (t_end) && t_start <= t_end);

        bool changed = false;
        bool appends = true;
        std::size_t prev_end = time_end_ndx;

        if (time_start != t_start)
        {
            changed = true;
            appends = false;
            time_start = t_start;
            time_start_ndx = std::lower_bound (
                    values.cbegin (), values.cend (), time_start,
                    [] (auto const& p, float t) { return p.t < t; }) - values.cbegin ();
        }

        if (time_end != t_end)
        {
            changed = true;
            time_end = t_end;
            time_end_ndx = std::upper_bound (
                    values.cbegin (), values.cend (), time_end,
                    [] (float t, auto const& p) { return t < p.t; }) - values.cbegin ();
        }

        appends &= std::exchange (prev_end,
                std::clamp (prev_end, time_start_ndx, time_end_ndx)) == prev_end;

        return std::make_tuple (
                changed, appends,
                values.cbegin () + time_start_ndx,
                values.cbegin () + prev_end,
                values.cbegin () + time_end_ndx);
    }

    static inline double compute_length (const_iterator first, const_iterator last)
    {
        return std::accumulate (first, last, .0, [] (double acc, auto const& v)
        {
            return acc + v.d;
        });
    }

private:

    std::vector<track_point> values;
    float min_distance;
    float time_start, time_end;
    std::size_t time_start_ndx, time_end_ndx;
    glm::vec3 lo, hi;

    inline void invalidate_time_range ()
    {
        time_start     = time_end     = nan_float;
        time_start_ndx = time_end_ndx = 0;
    }

    inline void reset_lohi ()
    {
        lo = glm::vec3 { max_float };
        hi = glm::vec3 { min_float };
    }
    inline void update_lohi (glm::vec3 const& p)
    {
        lo = glm::min (lo, p);
        hi = glm::max (hi, p);
    }
    inline void update_lohi ()
    {
        reset_lohi ();
        for (auto const& g: values)
            update_lohi (g.p);
    }
};

//--------------------------------------------------------------------------------------------------

#endif

