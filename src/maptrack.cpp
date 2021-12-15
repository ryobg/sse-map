/**
 * @file maptrack.cpp
 * @brief Shared interface between files in SSE-MapTrack
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

#include "maptrack.hpp"

//--------------------------------------------------------------------------------------------------

maptrack_t maptrack = {};

//--------------------------------------------------------------------------------------------------

std::string const&
plugin_name ()
{
    static std::string v = PLUGIN_NAME;
    return v;
}

//--------------------------------------------------------------------------------------------------

bool
dispatch_journal (std::string msg)
{
    if (msg.empty ())
        return false;

    extern bool dispatch_skse_message (
            char const* receiver, int id, void const* data, std::size_t size);
    return dispatch_skse_message ("sse-journal", 1, msg.c_str (), std::strlen (msg.c_str ()));
}

//--------------------------------------------------------------------------------------------------

