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

/// Wraps common ops and caches with regard a tracked route

class track_t
{
public:

    typedef std::vector<glm::vec4>::const_iterator const_iterator;

    track_t () : merge_distance2 (0)
    {
        clear ();
    }

    void merge_distance (float d)
    {
        Expects (std::isfinite (d) && d >= 0);
        merge_distance2 = d * d;
    }

    float last_time () const {
        return values.empty () ? 0.f : values.back ().w;
    }
    std::size_t size () const {
        return values.size ();
    }
    auto bounding_box () const {
        return std::make_pair (lo, hi);
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
        os.write (reinterpret_cast<const char*> (values.data ()), size * sizeof (glm::vec4));
    }

    /// Counterpart of #save_binary()
    template<class IStream>
    void load_binary (IStream& is)
    {
        std::uint32_t size = 0;
        is.read (reinterpret_cast<char*> (&size), sizeof (size));
        values.resize (size);
        is.read (reinterpret_cast<char*> (values.data ()), size * sizeof (glm::vec4));
    }

    /// Adds new point, eventually overriding the history (for example when a game is loaded)
    void add_point (glm::vec4 const& p)
    {
        Expects (std::isfinite (p.x) && std::isfinite (p.y)
              && std::isfinite (p.z) && std::isfinite (p.w));

        if (!values.empty ())
        {
            if (values.back ().w > p.w)
            {
                values.erase (std::upper_bound (
                        values.begin (), values.end (), p.w,
                        [] (float t, auto const& p) { return t < p.w; }),
                        values.end ());
                reset_lohi ();
                for (auto const& g: values)
                    update_lohi (g);
            }
            if (merge_distance2 < glm::distance2 (p.xy (), values.back ().xy ()))
            {
                values.push_back (glm::vec4 {});
            }
        }

        values.back () = p;
        update_lohi (p);
        invalidate_time_range ();
    }

    /// Quick access into subrange of tracked points between a given time range
    auto time_range (float t_start, float t_end)
    {
        Expects (std::isfinite (t_start) && std::isfinite (t_end) && t_start <= t_end);

        if (time_start != t_start)
        {
            time_start = t_start;
            time_start_it = std::lower_bound (
                    values.cbegin (), values.cend (), time_start,
                    [] (auto const& p, float t) { return p.w < t; });
        }

        if (time_end != t_end)
        {
            time_end = t_end;
            time_end_it = std::upper_bound (
                    values.cbegin (), values.cend (), time_end,
                    [] (float t, auto const& p) { return t < p.w; });
        }

        return std::make_pair (time_start_it, time_end_it);
    }

    static float compute_length (const_iterator first, const_iterator last)
    {
        if (first == last)
            return 0.f;
        glm::vec3 p = first->xyz ();
        return std::accumulate (++first, last, 0.f, [&p] (auto const& acc, auto const& v)
        {
            auto vp = v.xyz ();
            return acc + glm::distance (std::exchange (p, vp), vp);
        });
    }

private:

    static constexpr float max_float = std::numeric_limits<float>::max ();
    static constexpr float min_float = std::numeric_limits<float>::min ();
    static constexpr float nan_float = std::numeric_limits<float>::quiet_NaN ();

    std::vector<glm::vec4> values;
    float merge_distance2;
    float time_start, time_end;
    const_iterator time_start_it, time_end_it, lenfirst_it, lensecond_it;
    glm::vec4 lo, hi;

    inline void invalidate_time_range ()
    {
        time_start    = time_end    = nan_float;
        time_start_it = time_end_it = values.cend ();
    }

    inline void reset_lohi ()
    {
        lo = glm::vec4 { max_float };
        hi = glm::vec4 { min_float };
    }

    inline void update_lohi (glm::vec4 const& p)
    {
        lo = glm::min (lo, p);
        hi = glm::max (hi, p);
    }
};

//--------------------------------------------------------------------------------------------------

#endif

