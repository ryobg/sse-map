/**
 * @file map.hpp
 * @brief Shared interface between files in SSE-Map
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

#ifndef SSEMAP_HPP
#define SSEMAP_HPP

#include <sse-imgui/sse-imgui.h>
#include <utils/winutils.hpp>

#include <d3d11.h>

#include <array>
#include <fstream>
#include <string>

//--------------------------------------------------------------------------------------------------

// skse.cpp

void map_version (int* maj, int* min, int* patch, const char** timestamp);

extern std::ofstream& log ();
extern std::string logfile_path;

extern imgui_api imgui;
extern sseimgui_api sseimgui;

//--------------------------------------------------------------------------------------------------

// fileio.cpp

bool save_settings ();
bool load_settings ();

extern std::string ssemap_directory;

//--------------------------------------------------------------------------------------------------

// variables.cpp

bool setup_variables ();

float game_time ();
std::array<float, 3> player_location ();

std::string format_game_time (std::string);
std::string format_player_location (std::string);

//--------------------------------------------------------------------------------------------------

// render.cpp

struct image_t
{
    std::string file;
    std::uint32_t tint = IM_COL32_WHITE;
    std::array<float, 4> uv = {{ 0, 0, 1, 1 }};
    ID3D11ShaderResourceView* ref;
};

struct font_t
{
    std::string name;
    float scale;
    float size;
    std::uint32_t color; ///< Only this is tuned by the UI, rest are default init only
    std::string file;
    const char* default_data;
    ImFont* imfont; ///< Actual font with its settings (apart from #color)
};

//--------------------------------------------------------------------------------------------------

/// Most important stuff for the current running instance
struct ssemap_t
{
    image_t map;
    font_t font;
};

extern ssemap_t ssemap;

//--------------------------------------------------------------------------------------------------

#endif

