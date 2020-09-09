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

#include "track.hpp"

#include <sse-imgui/sse-imgui.h>
#include <utils/winutils.hpp>
#include <utils/imgui.hpp>

#include <d3d11.h>

#ifndef GLM_FORCE_CXX14
#define GLM_FORCE_CXX14
#endif

#ifndef GLM_FORCE_SWIZZLE
#define GLM_FORCE_SWIZZLE
#endif

#include <glm/glm.hpp>
#include <glm/gtx/range.hpp>
#include <glm/gtx/norm.hpp>
#include <glm/gtx/io.hpp>
#include <glm/gtx/compatibility.hpp>

#include <array>
#include <vector>
#include <fstream>
#include <string>

//--------------------------------------------------------------------------------------------------

template<class T> static inline glm::vec2
to_vec2 (T const& v) { return glm::vec2 {v.x, v.y}; }
template<class T> static inline ImVec2
to_ImVec2 (T const& v) { return ImVec2 {v.x, v.y}; }

//--------------------------------------------------------------------------------------------------

extern imgui_api imgui;
extern sseimgui_api sseimgui;

//--------------------------------------------------------------------------------------------------

// fileio.cpp

bool save_settings ();
bool load_settings ();
bool save_track (std::filesystem::path const& file); ///< Modifies maptrack#track
bool load_track (std::filesystem::path const& file); ///< Modifies maptrack#track
bool save_icons (std::filesystem::path const& file); ///< Modifies maptrack#icons
bool load_icons (std::filesystem::path const& file); ///< Modifies maptrack#icons

extern const struct locations_type
{
    std::filesystem::path
        settings,
        map_settings,
        icons_settings,
        tracks_directory,
        default_track,
        icons_directory,
        default_icons;
}
locations;

//--------------------------------------------------------------------------------------------------

// variables.cpp

bool setup_variables ();

float obtain_game_time ();
std::array<float, 3> obtain_player_location ();
std::string obtain_current_worldspace ();
std::string obtain_current_cell ();

void format_game_time (std::string&, const char*, float);
void format_player_location (std::string&, const char*, std::array<float, 3> const&);

/// Simple speed up (is it really needed? or it is for fun only?) through caching

template<int Id> inline void
format_game_time_c (std::string& out, const char* format, float value)
{
    static std::string cached_out;
    static float cached_value = std::numeric_limits<float>::quiet_NaN ();
    if (cached_value != value)
        format_game_time (cached_out, format, cached_value = value);
    out = cached_out;
}

/// Extract the hour, minute and seconds from a game encoded value

template<typename T>
static inline void
game_time_hms (float source, T& h, T& m, T& s)
{
    float hms = source - int (source);
    h = int (hms *= 24);
    hms  -= int (hms);
    m = int (hms *= 60);
    hms  -= int (hms);
    s = int (hms * 60);
}

//--------------------------------------------------------------------------------------------------

// render.cpp

struct image_t
{
    std::string file;
    std::uint32_t tint = IM_COL32_WHITE;
    glm::vec4 uv = { 0, 0, 1, 1 };
    ID3D11ShaderResourceView* ref;
};

struct icon_atlas_t
{
    std::string file;
    ID3D11ShaderResourceView* ref;
    std::uint32_t size;       ///< Computed size of #ref (one as the texture is a square)
    std::uint32_t stride;     ///< Computed number of icons per row of icons (upon #ref)
    float icon_uvsize;        ///< Computed size of #icon_size (0..1] upon the texture #ref
    std::uint32_t icon_size;  ///< Sides size of each icon in pixels (e.g. 16, 32, 64)
    std::uint32_t icon_count; ///< Number of icons in loaded texture.
    std::string uid;          ///< Unique ID (sha256), helps when swapping atlases
     /// On a whim, same with the default icon atlas
     static constexpr float default_uvsize = 64.f / 4096.f;
};

/// This structure may be compressed a bit later
struct icon_t
{
    glm::vec2 src;      ///< Top-left UV from the source texture icon_atlas_t#ref
    glm::vec2 tl, br;   ///< Actual position on the map - in map UV coordinates
    std::uint32_t tint;
    std::uint32_t index;///< Z order index within #icon_atlas_t
    std::string text;   ///< Size constrained text for small tip info and for Journal interaction
    std::string atlas;  ///< The UID of the atlas to match to
};

//--------------------------------------------------------------------------------------------------

/// As reported by player position (xyz) and current game time (t)
typedef glm::vec4 trackpoint_t;

/// Most important stuff for the current running instance
struct maptrack_t
{
    image_t map;
    font_t font;
    glm::vec2 scale, offset; ///< For conversion between texture map and world coords

    inline glm::vec2 map_to_game (glm::vec2 const& p) const
    {
        auto g = p - offset;
        return glm::vec2 { g.x / scale.x, -g.y / scale.y };
    }
    inline glm::vec2 map_to_game (float xy) const
    {
        float mid = (scale.x + scale.y) * .5f;    /// Reduce deformations due to diferent XY scale
        return glm::vec2 { xy / mid, -xy / mid };
    }
    inline glm::vec2 game_to_map (glm::vec2 const& p) const
    {
        return offset + glm::vec2 { p.x * scale.x, -p.y * scale.y };
    }

    icon_atlas_t icon_atlas;
    std::vector<icon_t> icons;

    bool enabled = true;    ///< Is tracking, polling for data is, enabled or not
    int since_dayx = 0;     ///< Show a map track since day X, can't be less than zero actually.
    int last_xdays = 1;     ///< Map track for the last X days, also not less than zero
    float time_point = 1;   ///< Memorize where is the time line slider located
    float update_period;    ///< In seconds, how frequently to poll for data
    float min_distance;     ///< Minimum distance between points, to register a new one

    bool track_enabled = true;
    float track_width;
    std::uint32_t track_color;

    struct {
        bool enabled;
        float size;
        std::uint32_t color;
    } player;

    struct {
        bool enabled;
        int resolution;
        int discover;
        float player_alpha, default_alpha, tracked_alpha;
    } fow;                      ///< Fog of War

    struct {
        bool enabled;
        bool deformation;
        std::uint32_t color;
        float scale;
    } cursor_info;              ///< Stuff like world position under the cursor and map ratio

    /// Heavy scenario: 60 seconds by 60 minutes by 150 game hours = 540k elements
    track_t track;
};

extern maptrack_t maptrack;

//--------------------------------------------------------------------------------------------------

#endif

