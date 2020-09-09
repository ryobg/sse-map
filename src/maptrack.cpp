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

#include <string>
#include <utils/plugin.hpp>

//--------------------------------------------------------------------------------------------------

std::string const&
plugin_name ()
{
    static std::string v = "sse-maptrack";
    return v;
}

//--------------------------------------------------------------------------------------------------

