/**
 * @file render.cpp
 * @brief User interface management
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
 * @ingroup Core
 *
 * @details
 */

#include "map.hpp"
#include <cstring>
#include <cctype>

//--------------------------------------------------------------------------------------------------

ssemap_t ssemap = {};

//--------------------------------------------------------------------------------------------------

bool
setup ()
{
    bool ret = true;
    ret &= load_settings ();
    ret &= setup_variables ();
    return ret;
}

//--------------------------------------------------------------------------------------------------

void SSEIMGUI_CCONV
render (int active)
{
    if (!active)
        return;

    imgui.igSetNextWindowSize (ImVec2 { 800, 600 }, ImGuiCond_FirstUseEver);
    imgui.igPushFont (ssemap.font.imfont);
    if (imgui.igBegin ("SSE Map", nullptr, 0))
    {
    }
    imgui.igEnd ();
    imgui.igPopFont ();
}

//--------------------------------------------------------------------------------------------------

