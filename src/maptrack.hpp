/**
 * @file maptrack.hpp
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

#ifndef MAPTRACK_HPP
#define MAPTRACK_HPP

#include <sse-imgui/sse-imgui.h>
#include <utils/winutils.hpp>

#include <d3d11.h>

#include <array>
#include <fstream>
#include <string>

//--------------------------------------------------------------------------------------------------

// skse.cpp

void maptrack_version (int* maj, int* min, int* patch, const char** timestamp);

extern std::ofstream& log ();
extern std::string logfile_path;

extern imgui_api imgui;
extern sseimgui_api sseimgui;

//--------------------------------------------------------------------------------------------------

// fileio.cpp

bool save_settings ();
bool load_settings ();

//--------------------------------------------------------------------------------------------------

// variables.cpp

bool setup_variables ();

float game_time ();
std::array<float, 3> player_location ();
std::string current_worldspace ();

void format_game_time (std::string&, const char*, float);
void format_player_location (std::string&, const char*, std::array<float, 3> const&);

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
struct maptrack_t
{
    image_t map;
    font_t font;

    bool enabled = true;    ///< Is tracking, polling for data is, enabled or not
    int since_dayx = 0;     ///< Show a map track since day X, can't be less than zero actually.
    int last_xdays = 1;     ///< Map track for the last X days, also not less than zero
    float time_point = 1;   ///< Memorize where is the time line slider located
    float update_period;    ///< In seconds, how frequently to poll for data
};

extern maptrack_t maptrack;

//--------------------------------------------------------------------------------------------------

#endif

