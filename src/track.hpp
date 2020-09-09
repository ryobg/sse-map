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
        os.write (reinterpret_cast<const char*> (values.data ()), size * sizeof (track_point));
    }

    /// Counterpart of #save_binary()
    template<class IStream>
    void load_binary (IStream& is, std::array<int, 3> const& ver)
    {
        std::uint32_t size = 0;
        is.read (reinterpret_cast<char*> (&size), sizeof (size));
        values.resize (size);
        if (ver[0] <= 1 && ver[1] < 5) // Before 1.5 distance was not serialized
        {
            glm::vec3 g {0};
            for (std::uint32_t i = 0; i < size; ++i)
            {
                glm::vec4 p;
                is.read (reinterpret_cast<char*> (&p), sizeof (glm::vec4));
                values[i] = track_point { p.xyz (), p.w, glm::distance (p.xyz (), g) };
                g = p.xyz ();
            }
            if (size) values.front ().d = 0;
        }
        else
            is.read (reinterpret_cast<char*> (values.data ()), size * sizeof (track_point));
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
        } else values.push_back (track_point {});

        values.back () = track_point { p, t, d };
        update_lohi (p);
        invalidate_time_range ();
    }

    /// Quick access into subrange of tracked points between a given time range
    std::pair<const_iterator, const_iterator>
    time_range (float t_start, float t_end, bool& updated)
    {
        Expects (std::isfinite (t_start) && std::isfinite (t_end) && t_start <= t_end);
        updated = false;

        if (time_start != t_start)
        {
            updated = true;
            time_start = t_start;
            time_start_it = std::lower_bound (
                    values.cbegin (), values.cend (), time_start,
                    [] (auto const& p, float t) { return p.t < t; });
        }

        if (time_end != t_end)
        {
            updated = true;
            time_end = t_end;
            time_end_it = std::upper_bound (
                    values.cbegin (), values.cend (), time_end,
                    [] (float t, auto const& p) { return t < p.t; });
        }

        return std::make_pair (time_start_it, time_end_it);
    }

    static inline double compute_length (const_iterator first, const_iterator last)
    {
        return std::accumulate (first, last, .0, [] (double acc, auto const& v)
        {
            return acc + v.d;
        });
    }

private:

    static constexpr float max_float =  16'777'216.f,   ///< Some sane limits, because:
                           min_float = -16'777'216.f;   ///< This turns to zero if min limit values
    static constexpr float nan_float = std::numeric_limits<float>::quiet_NaN ();

    std::vector<track_point> values;
    float min_distance;
    float time_start, time_end;
    const_iterator time_start_it, time_end_it, lenfirst_it, lensecond_it;
    glm::vec3 lo, hi;

    inline void invalidate_time_range ()
    {
        time_start    = time_end    = nan_float;
        time_start_it = time_end_it = values.cend ();
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

