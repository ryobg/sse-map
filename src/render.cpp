/**
 * @file render.cpp
 * @brief User interface management
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
#include <cstring>
#include <cctype>
#include <algorithm>

#include <windows.h>

//--------------------------------------------------------------------------------------------------

maptrack_t maptrack = {};

static bool show_settings = false,
            show_menu = false,
            show_track_saveas = false,
            show_track_summary = false,
            show_track_load = false,
            show_icons_saveas = false,
            show_icons_load = false,
            show_icons_atlas = false;

/// Sets a Radio button below, should move to a persistent storate setting.
static bool menu_since_day = true;

/// Current HWND, used for timer management
static HWND top_window = nullptr;

/// Shared strings for rendering
static std::string current_location, current_time;
static glm::vec4 player_location { std::numeric_limits<float>::quiet_NaN () };

/// Current subrange of #maptrack.track selected for rendering, GUI controlled.
static struct {
    track_t::const_iterator first, second;
    bool draw_invalidated, length_invalidated;
}
track_range = {};

/// Easier than to add a lot of code, its also once per add/delete/load
static bool icons_invalidated = false;

//--------------------------------------------------------------------------------------------------

/// State based, projection of point in game or UV map coordinates into screen space
/// Better differentation between the both functionalities would be nice.

class map_project
{
    glm::vec2 wpos, uvtl, uvbr, mul;
public:
    map_project (glm::vec2 const& wpos, glm::vec2 const& wsz,
                 glm::vec2 const& uvtl, glm::vec2 const& uvbr)
        : wpos (wpos), uvtl (uvtl), uvbr (uvbr), mul (wsz / (uvbr - uvtl))
    {
    }
    /// Project a game point to a screen point
    inline glm::vec2 operator () (glm::vec4 const& p) const
    {
        return this->operator () (game_to_map (p));
    }
    /// Project a map point to a screen point
    inline glm::vec2 operator () (glm::vec2 const& p) const
    {
        return wpos + mul * (p - uvtl);
    }
    /// Project a game point to a map point
    inline glm::vec2 game_to_map (glm::vec4 const& p) const
    {
        return maptrack.offset + glm::vec2 { p.x * maptrack.scale.x, -p.y * maptrack.scale.y };
    }
};

//--------------------------------------------------------------------------------------------------

/// Basically adds meaningful points

static VOID CALLBACK
timer_callback (HWND hwnd, UINT message, UINT_PTR idTimer, DWORD dwTime)
{
    if (!maptrack.enabled && !maptrack.player.enabled)
        return;

    auto curr_world = obtain_current_worldspace ();
    auto curr_cell  = obtain_current_cell ();
    auto curr_loc   = obtain_player_location ();
    auto curr_time  = obtain_game_time ();

    // Use the time to reduce the stress of printf-like formatting in the rendering loop below
    // Though with the cache implementation, it may not make sense anymore... In any case, some
    // variables, whether strings or floats have to be shared across.

    current_location.clear ();
    if (!curr_world.empty ())
        current_location = curr_world;
    if (!curr_cell.empty ())
        current_location += ", " + curr_cell;
    for (auto const& l: curr_loc)
        current_location += " " + std::to_string (int (l));

    format_game_time (current_time, "Day %ri, %md of %lm, %Y [%h:%m]", curr_time);

    if (curr_world != "Skyrim" || !curr_cell.empty ())
    {
        player_location = glm::vec4 { std::numeric_limits<float>::quiet_NaN () };
        return;
    }
    player_location = glm::vec4 { curr_loc[0], curr_loc[1], curr_loc[2], curr_time };

    if (maptrack.enabled)
        maptrack.track.add_point (player_location);
}

//--------------------------------------------------------------------------------------------------

bool
update_timer ()
{
    if (!::SetTimer (top_window, (UINT_PTR) timer_callback,
                UINT (maptrack.update_period * 1000), timer_callback))
    {
        maptrack.enabled = false;
        log () << "Failed to create timer: " << format_utf8message (::GetLastError ()) << std::endl;
        return false;
    }
    return true;
}

//--------------------------------------------------------------------------------------------------

bool
setup ()
{
    if (!load_settings ())
        return false;
    if (!setup_variables ())
        return false;
    top_window = (HWND) imgui.igGetIO ()->ImeWindowHandle;
    if (!top_window)
        return false;
    if (!update_timer ())
        return false;
    load_track (default_track_file);
    load_icons (default_icons_file);
    return true;
}

//--------------------------------------------------------------------------------------------------

static void
draw_player (glm::vec2 const& wpos, glm::vec2 const& wsz,
            glm::vec2 const& uvtl, glm::vec2 const& uvbr)
{
    if (!maptrack.player.enabled
            || glm::isfinite (player_location) != glm::bvec4 (true))
        return;

    imgui.ImDrawList_PushClipRect (imgui.igGetWindowDrawList (),
            to_ImVec2 (wpos), to_ImVec2 (wpos+wsz), false);

    map_project proj (wpos, wsz, uvtl, uvbr);
    imgui.ImDrawList_AddCircleFilled (imgui.igGetWindowDrawList (),
            to_ImVec2 (proj (player_location)),
            maptrack.player.size * .5f, maptrack.player.color, 12);

    imgui.ImDrawList_PopClipRect (imgui.igGetWindowDrawList ());
}

//--------------------------------------------------------------------------------------------------

static void
draw_icons (glm::vec2 const& wpos, glm::vec2 const& wsz,
            glm::vec2 const& uvtl, glm::vec2 const& uvbr,
            bool hovered)
{
    constexpr float nan = std::numeric_limits<float>::quiet_NaN ();
    struct icon_image {
        ImVec2 tl, br, src; std::uint32_t tint, index;
    };
    static struct {
        glm::vec2 wpos {nan}, wsz {nan}, uvtl {nan}, uvbr {nan};
        std::vector<icon_image> drawlist;
        bool ico_updated = false;
    } cached;
    static decltype (maptrack.icons)::iterator ico = maptrack.icons.end ();

    map_project const mproj (wpos, wsz, uvtl, uvbr);
    auto make_image = [&] (icon_t const& ico, std::uint32_t index)
    {
        return icon_image {
            to_ImVec2 (mproj (ico.tl)),
            to_ImVec2 (mproj (ico.br)),
            to_ImVec2 (ico.src), ico.tint, index
        };
    };

    bool window_moved = cached.wpos != wpos;
    bool window_resized = (cached.wsz != wsz || cached.uvtl != uvtl || cached.uvbr != uvbr);

    if (window_resized || icons_invalidated)
    {
        if (icons_invalidated)
            ico = maptrack.icons.end ();
        icons_invalidated = false;
        cached.drawlist.clear ();
        for (std::size_t k = 0, n = maptrack.icons.size (); k < n; ++k)
        {
            auto const& i = maptrack.icons[k];
            if ((i.tl.x < uvbr.x) && (i.br.x > uvtl.x)
             && (i.tl.y < uvbr.y) && (i.br.y > uvtl.y))
            {
                cached.drawlist.push_back (make_image (i, std::uint32_t (k)));
            }
        }
    }
    else if (window_moved)
    {
        auto d = wpos - cached.wpos;
        for (auto& i: cached.drawlist)
        {
            i.tl.x += d.x, i.tl.y += d.y;
            i.br.x += d.x, i.br.y += d.y;
        }
    }
    else if (cached.ico_updated)
    {
        cached.ico_updated = false;
        auto index = std::uint32_t (std::distance (maptrack.icons.begin (), ico));
        for (auto& i: cached.drawlist)
            if (i.index == index)
            {
                i = make_image (*ico, index);
                break;
            }
    }
    cached.wpos = wpos;
    cached.wsz = wsz, cached.uvtl = uvtl, cached.uvbr = uvbr;

    // Draw here, before the GUI controls. Some sort of AABB tree would be nice.
    // The clip rects cant be on top of all map draw routines as the GUI popup below may get out of
    // the map bounds when the mouse is clicked near the edges.

    imgui.ImDrawList_PushClipRect (imgui.igGetWindowDrawList (),
            to_ImVec2 (wpos), to_ImVec2 (wpos + wsz), false);
    for (auto const& i: cached.drawlist)
    {
        imgui.ImDrawList_AddImage (imgui.igGetWindowDrawList (), maptrack.icon_atlas.ref,
                i.tl, i.br, i.src,
                ImVec2 { i.src.x + maptrack.icon_atlas.icon_uvsize,
                         i.src.y + maptrack.icon_atlas.icon_uvsize }, i.tint);
    }
    imgui.ImDrawList_PopClipRect (imgui.igGetWindowDrawList ());

    // Clicking on existing icon, sets it for modification. Otherwise, create new one by copying
    // the last one selected (if any) - this is for rapid multiplication across the map.

    constexpr int max_text = 63;

    ImGuiIO* io = imgui.igGetIO ();
    if (hovered && io->MouseDown[1])
    {
        auto tpos = uvtl + (uvbr - uvtl) * (to_vec2 (io->MousePos) - wpos) / wsz;

        auto it = std::find_if (maptrack.icons.begin (), maptrack.icons.end (),
                [tpos] (icon_t const& i)
                {
                    return (i.tl.x <= tpos.x) && (i.tl.y <= tpos.y)
                        && (i.br.x >= tpos.x) && (i.br.y >= tpos.y);
                });

        if (it != maptrack.icons.end ())
            ico = it;
        else
        {
            icon_t i;
            float half;
            if (ico != maptrack.icons.end ())
            {
                half = (ico->br - ico->tl).x * .5f;
                i.src = ico->src;
                i.tint = ico->tint;
                i.index = ico->index;
            }
            else
            {
                half = maptrack.icon_atlas.icon_uvsize * .5f;
                i.src = glm::vec2 (0);
                i.tint = IM_COL32_WHITE;
                i.index = 0;
            }
            i.tl = tpos - half;
            i.br = tpos + half;
            ico = maptrack.icons.insert (maptrack.icons.end (), i);
            cached.drawlist.push_back (make_image (i, maptrack.icons.size () - 1));
        }
        ico->text.resize (max_text);
        imgui.igOpenPopup ("Setup icon");
    }

    if (imgui.igBeginPopup ("Setup icon", 0))
    {
        static const std::string name = "out of " + std::to_string (maptrack.icon_atlas.icon_count);

        imgui.igInputText ("Small text##icon", &ico->text[0], max_text, 0, nullptr, nullptr);

        int user_index = ico->index + 1;
        if (imgui.igDragInt (name.c_str (), &user_index, 1, 1, maptrack.icon_atlas.icon_count,"%d"))
        {
            cached.ico_updated = true;
            ico->index = glm::clamp (1, user_index, int (maptrack.icon_atlas.icon_count)) - 1;
            ico->src = maptrack.icon_atlas.icon_uvsize * glm::vec2 {
                ico->index % maptrack.icon_atlas.stride,
                ico->index / maptrack.icon_atlas.stride };
        }

        constexpr int colflags = ImGuiColorEditFlags_Float | ImGuiColorEditFlags_DisplayHSV
            | ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_PickerHueBar
            | ImGuiColorEditFlags_AlphaBar;

        ImVec4 color = imgui.igColorConvertU32ToFloat4 (ico->tint);
        if (imgui.igColorEdit4 ("Tint##icon", (float*) &color, colflags))
        {
            cached.ico_updated = true;
            ico->tint = imgui.igGetColorU32Vec4 (color);
        }

        float scale = (ico->br - ico->tl).x * maptrack.icon_atlas.icon_size;
        if (imgui.igSliderFloat ("Scale##icon", &scale, .25f, 4.f, "%.2f", 1))
        {
            cached.ico_updated = true;
            float half = glm::clamp (.25f, scale, 4.f) * .5 * maptrack.icon_atlas.icon_uvsize;
            glm::vec2 c = .5f * (ico->tl + ico->br);
            ico->tl = c - half;
            ico->br = c + half;
        }

        if (imgui.igButton ("Open in Journal", ImVec2 {}))
        {
            if (dispatch_journal (ico->text))
                imgui.igCloseCurrentPopup ();
        }

        imgui.igSameLine (0, -1);
        if (imgui.igButton ("Delete", ImVec2 {}))
            imgui.igOpenPopup ("Delete icon?");
        if (imgui.igBeginPopup ("Delete icon?", 0))
        {
            if (imgui.igButton ("Confirm##icon", ImVec2 {}))
            {
                maptrack.icons.erase (ico);
                icons_invalidated = true;
                imgui.igCloseCurrentPopup ();
            }
            imgui.igEndPopup ();
        }
        // Have to close and the parent popup after deletion as it references ico all around
        if (ico == maptrack.icons.end ())
            imgui.igCloseCurrentPopup ();

        imgui.igEndPopup ();
    }
}

//--------------------------------------------------------------------------------------------------

static void
draw_track (glm::vec2 const& wpos, glm::vec2 const& wsz,
            glm::vec2 const& uvtl, glm::vec2 const& uvbr)
{
    constexpr float nan = std::numeric_limits<float>::quiet_NaN ();
    static struct
    {
        glm::vec2 wpos {nan}, wsz {nan}, uvtl {nan}, uvbr {nan};
        std::vector<glm::vec2> uvtrack;
    }
    cached;

    if (!maptrack.track_enabled || track_range.first == track_range.second)
        return;

    bool window_moved = (cached.wpos != wpos);
    bool window_resized = (cached.wsz != wsz || cached.uvtl != uvtl || cached.uvbr != uvbr);

    // Best case for update.
    if (window_moved && !window_resized && !track_range.draw_invalidated)
    {
        auto d = wpos - cached.wpos;
        for (auto& p: cached.uvtrack)
            p += d;
    }

    // Currently, no idea how to speed up on range change only, hence consider full recalc as it
    // is when the window is resized. Gladly, the range is done only once while the map is visible
    // if the player is not on auto-move.
    else if (window_resized || track_range.draw_invalidated)
    {
        cached.uvtrack.resize (std::distance (track_range.first, track_range.second));
        std::transform (
                track_range.first, track_range.second, cached.uvtrack.begin (),
                map_project (wpos, wsz, uvtl, uvbr));
    }

    imgui.ImDrawList_PushClipRect (imgui.igGetWindowDrawList (),
            to_ImVec2 (wpos), to_ImVec2 (wpos+wsz), false);

    int splits = 10000;
    auto div = std::div (int (cached.uvtrack.size ()), splits);
    for (int i = 0; i < div.quot; ++i)
    {
        imgui.ImDrawList_AddPolyline (imgui.igGetWindowDrawList (),
                reinterpret_cast<ImVec2 const*> (cached.uvtrack.data () + i*splits), splits,
                maptrack.track_color, false, maptrack.track_width);
    }
    imgui.ImDrawList_AddPolyline (imgui.igGetWindowDrawList (),
            reinterpret_cast<ImVec2 const*> (cached.uvtrack.data () + div.quot*splits), div.rem,
            maptrack.track_color, false, maptrack.track_width);
    imgui.ImDrawList_PopClipRect (imgui.igGetWindowDrawList ());

    cached.wpos = wpos, cached.wsz = wsz, cached.uvtl = uvtl, cached.uvbr = uvbr;
    track_range.draw_invalidated = false;
}

//--------------------------------------------------------------------------------------------------

static void
draw_fog (glm::vec2 const& wpos, glm::vec2 const& wsz,
          glm::vec2 const& uvtl, glm::vec2 const& uvbr)
{
    if (!maptrack.fow.enabled || track_range.first == track_range.second)
        return;

    static decltype (maptrack.fow) cached = maptrack.fow;
    bool const fow_invalidated =
               cached.resolution != maptrack.fow.resolution
            || cached.discover != maptrack.fow.discover
            || cached.default_alpha != maptrack.fow.default_alpha
            || cached.tracked_alpha != maptrack.fow.tracked_alpha;
    cached = maptrack.fow;

    glm::vec2 const step = 1.f / glm::vec2 (maptrack.fow.resolution, maptrack.fow.resolution);
    map_project const proj (wpos, wsz, uvtl, uvbr);

    static std::vector<char> cells;
    cells.resize (maptrack.fow.resolution * maptrack.fow.resolution);

    // Update the fog of war cells, reduces comparision time with the tracks down the rendering
    if (fow_invalidated || track_range.draw_invalidated)
    {
        auto update_cells = [&] (int x, int y, char alpha)
        {
            float r2 = maptrack.fow.discover * maptrack.fow.discover;
            for (int dy = -maptrack.fow.discover; dy <= +maptrack.fow.discover; ++dy)
                for (int dx = -maptrack.fow.discover; dx <= +maptrack.fow.discover; ++dx)
                {
                    if (x+dx >= 0 && x+dx < maptrack.fow.resolution
                     && y+dy >= 0 && y+dy < maptrack.fow.resolution)
                    {
                        if (glm::distance2 (glm::vec2 (x+dx, y+dy), glm::vec2 (x, y)) < r2)
                            cells[x+dx + (y+dy) * maptrack.fow.resolution] = alpha;
                    }
                }
        };

        char default_alpha = char (glm::clamp (maptrack.fow.default_alpha * 255, 0.f, 255.f));
        char tracked_alpha = char (glm::clamp (maptrack.fow.tracked_alpha * 255, 0.f, 255.f));
        char player_alpha = char (glm::clamp (maptrack.fow.player_alpha * 255, 0.f, 255.f));

        std::fill (cells.begin (), cells.end (), default_alpha);

        for (auto it = track_range.first; it != track_range.second; ++it)
        {
            glm::ivec2 cell (proj.game_to_map (*it) / step);
            update_cells (cell.x, cell.y, tracked_alpha);
        }

        glm::ivec2 cell (proj.game_to_map (player_location) / step);
        update_cells (cell.x, cell.y, player_alpha);
    }

    // Render

    auto wdl = imgui.igGetWindowDrawList ();
    imgui.ImDrawList_PushClipRect (wdl, to_ImVec2 (wpos), to_ImVec2 (wpos + wsz), false);
    auto old_flags = std::exchange (wdl->Flags, ImDrawListFlags_None);

    glm::ivec2 const ctl (glm::floor (uvtl / step));
    glm::vec2 const tl = glm::vec2 (ctl) * step;
    glm::vec2 const br = glm::vec2 (glm::ceil (uvbr / step)) * step;

    glm::ivec2 cell = ctl;
    for (float y = tl.y; y < br.y; y += step.y, ++cell.y)
    {
        cell.x = ctl.x;
        for (float x = tl.x; x < br.x; x += step.x, ++cell.x)
        {
            char alpha = 255;
            if (cell.x >= 0 && cell.x < maptrack.fow.resolution
             && cell.y >= 0 && cell.y < maptrack.fow.resolution)
            {
                alpha = cells[cell.x + cell.y * maptrack.fow.resolution];
            }
            imgui.ImDrawList_AddQuadFilled (wdl,
                    to_ImVec2 (proj (glm::vec2 (x       , y    ))),
                    to_ImVec2 (proj (glm::vec2 (x+step.x, y    ))),
                    to_ImVec2 (proj (glm::vec2 (x+step.x, y+step.y))),
                    to_ImVec2 (proj (glm::vec2 (x       , y+step.y))),
                    IM_COL32 (0, 0, 0, alpha));
        }
    }

    wdl->Flags = old_flags;
    imgui.ImDrawList_PopClipRect (wdl);
}

//--------------------------------------------------------------------------------------------------

/// Handles mostly map zoom and dragging but also and the actual drawing of course

static void
draw_map (glm::vec2 const& map_pos, glm::vec2 const& map_size)
{
    auto const& oruv = maptrack.map.uv;
    static glm::vec2 uvtl = oruv.xy ();
    static glm::vec2 uvbr = oruv.zw ();
    static const glm::vec2 max_zoom = oruv.zw () * .2f;
    static bool hovered = false;
    static float mouse_wheel = 0;
    static glm::vec2 last_mouse_pos = {-1,-1};
    constexpr float zoom_factor = .01f;
    ImGuiIO* io = imgui.igGetIO ();
    auto wpos = to_vec2 (imgui.igGetWindowPos ());

    glm::vec2 backup_uvtl = uvtl, backup_uvbr = uvbr;
    if (hovered)
    {
        if (mouse_wheel)
        {
            auto mouse_pos = to_vec2 (io->MousePos);
            auto z = (mouse_pos - map_pos - wpos) / map_size;
            auto d = (uvbr - uvtl) * mouse_wheel * zoom_factor;
            uvtl += d * z;
            uvbr -= d * (1.f - z);
        }
        if (io->MouseDown[0])
        {
            auto mouse_pos = to_vec2 (io->MousePos);
            if (last_mouse_pos.x == -1)
                last_mouse_pos = mouse_pos;
            auto d = (uvbr - uvtl) * (mouse_pos - last_mouse_pos) / map_size;
            uvtl -= d;
            uvbr -= d;
            last_mouse_pos = mouse_pos;
        }
        else last_mouse_pos.x = -1;
    }
    else last_mouse_pos.x = -1;

    // Fixup various deformation (that whole function is mess)
    if (hovered && (mouse_wheel || io->MouseDown[0]))
    {
        uvtl = glm::clamp (glm::vec2 (0), uvtl, oruv.zw () - max_zoom);
        uvbr = glm::clamp (max_zoom, uvbr, oruv.zw ());
        if (mouse_wheel)
        {
            if (std::abs (uvbr.x - uvtl.x) < max_zoom.x
             || std::abs (uvbr.y - uvtl.y) < max_zoom.y)
                uvtl = backup_uvtl, uvbr = backup_uvbr;
        }
        if (io->MouseDown[0])
        {
            if (uvbr.x - uvtl.x != backup_uvbr.x - backup_uvtl.x)
                uvbr.x = backup_uvbr.x, uvtl.x = backup_uvtl.x;
            if (uvbr.y - uvtl.y != backup_uvbr.y - backup_uvtl.y)
                uvbr.y = backup_uvbr.y, uvtl.y = backup_uvtl.y;
        }
    }

    imgui.igInvisibleButton ("Map", to_ImVec2 (map_size));
    imgui.ImDrawList_AddImage (imgui.igGetWindowDrawList (), maptrack.map.ref,
            to_ImVec2 (wpos + map_pos), to_ImVec2 (wpos + map_pos + map_size),
            to_ImVec2 (uvtl), to_ImVec2 (uvbr), IM_COL32_WHITE);

    // One frame later we handle the input, so to allow the ImGui hover test. Otherwise, if
    // scrolling on other window which happens to be in front of the map, it will zoom in/out,
    // making awkward behaviour.
    if (hovered = imgui.igIsItemHovered (0))
    {
        mouse_wheel = io->MouseWheel ? (io->MouseWheel > 0 ? +1 : -1) : 0;
    }

    draw_fog    (wpos + map_pos, map_size, uvtl, uvbr);
    draw_icons  (wpos + map_pos, map_size, uvtl, uvbr, hovered);
    draw_track  (wpos + map_pos, map_size, uvtl, uvbr);
    draw_player (wpos + map_pos, map_size, uvtl, uvbr);
}

//--------------------------------------------------------------------------------------------------

static void
update_track_range ()
{
    float last_recorded_time = maptrack.track.last_time ();
    auto track_start2 = std::max (0.f, last_recorded_time - maptrack.last_xdays);
    auto tstart = menu_since_day ? maptrack.since_dayx : track_start2;
    auto tend = maptrack.time_point * (last_recorded_time - tstart) + tstart;

    bool tupdated = false;
    std::tie (track_range.first, track_range.second)
        = maptrack.track.time_range (tstart, tend, tupdated);
    track_range.draw_invalidated |= tupdated;
    track_range.length_invalidated |= tupdated;
}

//--------------------------------------------------------------------------------------------------

void SSEIMGUI_CCONV
render (int active)
{
    if (!active)
        return;

    imgui.igPushStyleColor (ImGuiCol_FrameBg,       ImVec4 {0, 0, 0, 0});
    imgui.igPushStyleColor (ImGuiCol_Button,        ImVec4 {0, 0, 0, 0});
    imgui.igPushStyleColor (ImGuiCol_TitleBgActive, ImVec4 {0, 0, 0, 1.f});
    imgui.igPushStyleColor (ImGuiCol_CheckMark,     ImVec4 {.61f, .61f, .61f, 1.f});
    imgui.igPushStyleColor (ImGuiCol_SliderGrab,    ImVec4 {.61f, .61f, .61f, 1.f});
    imgui.igPushStyleColor (ImGuiCol_ResizeGrip,    ImVec4 {.61f, .61f, .61f, 1.f});
    imgui.igPushStyleColor (ImGuiCol_TextSelectedBg,ImVec4 {.61f, .61f, .61f, 1.f});
    imgui.igPushStyleColor (ImGuiCol_ButtonHovered ,ImVec4 {0.26f, 0.59f, 0.98f, 0.4f});
    imgui.igPushStyleColor (ImGuiCol_FrameBgHovered,ImVec4 {0.26f, 0.59f, 0.98f, 0.4f});

    imgui.igPushFont (maptrack.font.imfont);

    imgui.igPushStyleVarVec2 (ImGuiStyleVar_ItemSpacing, ImVec2 {5, 10});
    imgui.igPushStyleVarVec2 (ImGuiStyleVar_FramePadding, ImVec2 {5, 5});
    imgui.igPushStyleVarFloat (ImGuiStyleVar_FrameBorderSize, 1.f);
    imgui.igPushStyleVarFloat (ImGuiStyleVar_WindowBorderSize, 0.f);

    imgui.igSetNextWindowSize (ImVec2 { 800, 600 }, ImGuiCond_FirstUseEver);
    if (imgui.igBegin ("SSE MapTrack", nullptr, ImGuiWindowFlags_NoScrollbar))
    {
        update_track_range ();

        auto dragday_size = imgui.igCalcTextSize ("1345", nullptr, false, -1.f);
        auto mapsz = to_vec2 (imgui.igGetContentRegionAvail ());
        // ~48 chars based on max text content widgets below:
        mapsz.x -= dragday_size.x * (show_menu ? 12 : 1);

        imgui.igBeginGroup ();
        imgui.igSetNextItemWidth (mapsz.x);
        imgui.igSliderFloat ("##Time", &maptrack.time_point, 0, 1, "", 1);
        auto mappos = to_vec2 (imgui.igGetCursorPos ());
        mapsz.y -= mappos.y/2;
        draw_map (mappos, mapsz);
        imgui.igEndGroup ();

        imgui.igSameLine (0, -1);
        if (imgui.igButton (show_menu ? ">>##Menu" : "<<##Menu", ImVec2 {0, mapsz.y + mappos.y/2}))
            show_menu = !show_menu;
        if (show_menu)
        {
            imgui.igSameLine (0, -1);
            void draw_menu ();
            draw_menu ();
        }
    }
    imgui.igEnd ();

    void draw_settings ();
    if (show_settings)
        draw_settings ();

    void draw_track_saveas ();
    if (show_track_saveas)
        draw_track_saveas ();
    void draw_track_load ();
    if (show_track_load)
        draw_track_load ();
    void draw_track_summary ();
    if (show_track_summary)
        draw_track_summary ();

    void draw_icons_saveas ();
    if (show_icons_saveas)
        draw_icons_saveas ();
    void draw_icons_load ();
    if (show_icons_load)
        draw_icons_load ();
    void draw_icons_atlas ();
    if (show_icons_atlas)
        draw_icons_atlas ();

    imgui.igPopStyleVar (4);
    imgui.igPopFont ();
    imgui.igPopStyleColor (9);
}

//--------------------------------------------------------------------------------------------------

void
draw_menu ()
{
    static std::string track_start_s, track_end_s, since_dayx_s, last_xdays_s;

    float last_recorded_time = maptrack.track.last_time ();
    int last_recorded_day = int (last_recorded_time);

    ImVec2 dragday_size = imgui.igCalcTextSize ("1345", nullptr, false, -1.f);

    imgui.igBeginGroup ();

    imgui.igText (track_start_s.c_str ());
    imgui.igText (track_end_s.c_str ());
    imgui.igDummy (ImVec2 {0,1});
    imgui.igText (current_location.c_str ());
    imgui.igText (current_time.c_str ());

    imgui.igSeparator ();
    imgui.igCheckbox ("Tracking enabled", &maptrack.enabled);
    imgui.igSetNextItemWidth (dragday_size.x*2);
    if (imgui.igDragFloat ("seconds between updates",
                &maptrack.update_period, .1f, 1.f, 60.f, "%.1f", 1))
    {
        maptrack.update_period = std::max (1.f, maptrack.update_period);
        update_timer ();
    }
    imgui.igSetNextItemWidth (dragday_size.x*2);
    if (imgui.igDragFloat ("points merge distance", &maptrack.min_distance,
                1.f, 1, 1'000, "%1.0f", 1))
    {
        maptrack.min_distance = glm::clamp (1.f, maptrack.min_distance, 1'000.f);
        maptrack.track.merge_distance (maptrack.min_distance);
    }
    imgui.igSeparator ();

    if (imgui.igRadioButtonBool ("Since day", menu_since_day))
        menu_since_day = true;
    imgui.igSameLine (0, -1);
    imgui.igSetNextItemWidth (dragday_size.x);
    if (imgui.igDragInt ("##Since day", &maptrack.since_dayx, .25f, 0, last_recorded_day, "%d"))
        maptrack.since_dayx = glm::clamp (0, maptrack.since_dayx, last_recorded_day);
    imgui.igSameLine (0, -1);
    format_game_time_c<3> (since_dayx_s, "i.e. %md of %lm, %Y", maptrack.since_dayx);
    imgui.igText (since_dayx_s.c_str ());

    if (imgui.igRadioButtonBool ("Last##X days", !menu_since_day))
        menu_since_day = false;
    imgui.igSameLine (0, -1);
    imgui.igSetNextItemWidth (dragday_size.x);
    if (imgui.igDragInt ("##Last X days",
                &maptrack.last_xdays, .25f, 1, 1 +last_recorded_day, "%d"))
        maptrack.last_xdays = glm::clamp (1, maptrack.last_xdays, last_recorded_day);
    imgui.igSameLine (0, -1);
    auto track_start2 = std::max (0.f, last_recorded_time - maptrack.last_xdays);
    format_game_time_c<4> (last_xdays_s, "days i.e. %md of %lm, %Y", track_start2);
    imgui.igText (last_xdays_s.c_str ());

    auto tstart = menu_since_day ? maptrack.since_dayx : track_start2;
    auto tend = maptrack.time_point * (last_recorded_time - tstart) + tstart;
    format_game_time_c<1> (track_start_s, "From day %ri, %md of %lm", tstart);
    format_game_time_c<2> (track_end_s, "to day %ri, %md of %lm", tend);

    ImVec2 const button_size { dragday_size.x*3, 0 };
    imgui.igSeparator ();
    imgui.igText ("Track - %d point(s)", int (maptrack.track.size ()));
    if (imgui.igButton ("Save##track", button_size))
        save_track (default_track_file);
    imgui.igSameLine (0, -1);
    if (imgui.igButton ("Save As##track", button_size))
        show_track_saveas = !show_track_saveas;
    imgui.igSameLine (0, -1);
    if (imgui.igButton ("Report##track", button_size))
        show_track_summary = !show_track_summary;
    if (imgui.igButton ("Load##track", button_size))
        show_track_load = !show_track_load;
    imgui.igSameLine (0, -1);
    if (imgui.igButton ("Clear##track", button_size))
        imgui.igOpenPopup ("Clear track?");
    if (imgui.igBeginPopup ("Clear track?", 0))
    {
        if (imgui.igButton ("Confirm##clear track", ImVec2 {}))
        {
            maptrack.track.clear ();
            imgui.igCloseCurrentPopup ();
        }
        imgui.igEndPopup ();
    }

    imgui.igSeparator ();
    imgui.igText ("Icons - %d instance(s)", int (maptrack.icons.size ()));
    if (imgui.igButton ("Save##icons", button_size))
        save_icons (default_icons_file);
    imgui.igSameLine (0, -1);
    if (imgui.igButton ("Save As##icons", button_size))
        show_icons_saveas = !show_icons_saveas;
    imgui.igSameLine (0, -1);
    if (imgui.igButton ("View atlas##icons", button_size))
        show_icons_atlas = !show_icons_atlas;
    if (imgui.igButton ("Load##icons", button_size))
        show_icons_load = !show_icons_load;
    imgui.igSameLine (0, -1);
    if (imgui.igButton ("Clear##icons", button_size))
        imgui.igOpenPopup ("Clear icons?");
    if (imgui.igBeginPopup ("Clear icons?", 0))
    {
        if (imgui.igButton ("Confirm##clear icons", ImVec2 {}))
        {
            maptrack.icons.clear ();
            icons_invalidated = true;
            imgui.igCloseCurrentPopup ();
        }
        imgui.igEndPopup ();
    }

    imgui.igSeparator ();
    if (imgui.igButton ("Settings", button_size))
        show_settings = !show_settings;

    imgui.igEndGroup ();
}

//--------------------------------------------------------------------------------------------------

void
draw_settings ()
{
    if (imgui.igBegin ("SSE MapTrack: Settings", &show_settings, 0))
    {
        constexpr int cflags = ImGuiColorEditFlags_Float | ImGuiColorEditFlags_DisplayHSV
            | ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_PickerHueBar
            | ImGuiColorEditFlags_AlphaBar;

        imgui.igText ("Default font:");
        ImVec4 col = imgui.igColorConvertU32ToFloat4 (maptrack.font.color);
        if (imgui.igColorEdit4 ("Color##Default", (float*) &col, cflags))
            maptrack.font.color = imgui.igGetColorU32Vec4 (col);
        imgui.igSliderFloat ("Scale##Default", &maptrack.font.imfont->Scale, .5f, 2.f, "%.2f", 1);

        imgui.igText ("");
        imgui.igCheckbox ("Track", &maptrack.track_enabled);
        col = imgui.igColorConvertU32ToFloat4 (maptrack.track_color);
        if (imgui.igColorEdit4 ("Color##Track", (float*) &col, cflags))
            maptrack.track_color = imgui.igGetColorU32Vec4 (col);
        imgui.igSliderFloat ("Width##Track", &maptrack.track_width, 1.f, 20.f, "%.1f", 1);

        imgui.igText ("");
        imgui.igCheckbox ("Player circle", &maptrack.player.enabled);
        col = imgui.igColorConvertU32ToFloat4 (maptrack.player.color);
        if (imgui.igColorEdit4 ("Color##Player", (float*) &col, cflags))
            maptrack.player.color = imgui.igGetColorU32Vec4 (col);
        imgui.igSliderFloat ("Size##Player", &maptrack.player.size, 1.f, 20.f, "%.1f", 1);

        imgui.igText ("");
        imgui.igCheckbox ("Fog of War", &maptrack.fow.enabled);
        imgui.igSliderInt ("Resolution##FoW", &maptrack.fow.resolution, 32, 256, "%d");
        imgui.igSliderInt ("Discover radius##FoW", &maptrack.fow.discover, 1, 8, "%d");
        imgui.igSliderFloat ("Default alpha##FoW", &maptrack.fow.default_alpha, 0, 1, "%.2f", 1);
        imgui.igSliderFloat ("Tracked alpha##FoW", &maptrack.fow.tracked_alpha, 0, 1, "%.2f", 1);

        imgui.igText ("");
        if (imgui.igButton ("Save settings", ImVec2 {}))
            save_settings ();
        imgui.igSameLine (0, -1);
        if (imgui.igButton ("Load settings", ImVec2 {}))
            load_settings ();
    }
    imgui.igEnd ();
}

//--------------------------------------------------------------------------------------------------

/// Resizing one by one causes FPS stutters and CDTs, hence minimal SSO size + power of 2

static inline std::size_t
next_pow2 (std::size_t n)
{
    std::size_t p = 16;
    while (p < n) p <<= 1;
    return p;
};

static int
imgui_text_resize (ImGuiInputTextCallbackData* data)
{
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
    {
        auto str = reinterpret_cast<std::string*> (data->UserData);
        str->resize (next_pow2 (data->BufSize) - 1); // likely to avoid the internal pow2 of resize
        data->Buf = const_cast<char*> (str->c_str ());
    }
    return 0;
}

static bool
imgui_input_text (const char* label, std::string& text, ImGuiInputTextFlags flags = 0)
{
    return imgui.igInputText (
            label, const_cast<char*> (text.c_str ()), text.size () + 1,
            flags | ImGuiInputTextFlags_CallbackResize, imgui_text_resize, &text);
}

//--------------------------------------------------------------------------------------------------

void
draw_icons_saveas ()
{
    static std::string name;
    if (imgui.igBegin ("SSE MapTrack: Save Icons As", &show_icons_saveas, 0))
    {
        imgui.igText (icons_directory.c_str ());
        imgui_input_text ("Name", name);
        if (imgui.igButton ("Cancel", ImVec2 {}))
            show_icons_saveas = false;
        imgui.igSameLine (0, -1);
        if (imgui.igButton ("Save", ImVec2 {}))
        {
            auto file = icons_directory + name + ".json";
            if (file_exists (file))
                imgui.igOpenPopup ("Overwrite file?");
            else if (save_icons (file))
                show_icons_saveas = false;
        }
        if (imgui.igBeginPopup ("Overwrite file?", 0))
        {
            if (imgui.igButton ("Confirm##file", ImVec2 {}))
                if (save_icons (icons_directory + name + ".json"))
                    show_icons_saveas = false, imgui.igCloseCurrentPopup ();
            imgui.igEndPopup ();
        }
    }
    imgui.igEnd ();
}

//--------------------------------------------------------------------------------------------------

void
draw_track_saveas ()
{
    static std::string name;
    if (imgui.igBegin ("SSE MapTrack: Save Track As", &show_track_saveas, 0))
    {
        imgui.igText (tracks_directory.c_str ());
        imgui_input_text ("Name", name);
        if (imgui.igButton ("Cancel", ImVec2 {}))
            show_track_saveas = false;
        imgui.igSameLine (0, -1);
        if (imgui.igButton ("Save", ImVec2 {}))
        {
            auto file = tracks_directory + name + ".bin";
            if (file_exists (file))
                imgui.igOpenPopup ("Overwrite file?");
            else if (save_track (file))
                show_track_saveas = false;
        }
        if (imgui.igBeginPopup ("Overwrite file?", 0))
        {
            if (imgui.igButton ("Confirm##file", ImVec2 {}))
                if (save_track (tracks_directory + name + ".bin"))
                    show_track_saveas = false, imgui.igCloseCurrentPopup ();
            imgui.igEndPopup ();
        }
    }
    imgui.igEnd ();
}

//--------------------------------------------------------------------------------------------------

bool
extract_vector_string (void* data, int idx, const char** out_text)
{
    auto vars = reinterpret_cast<std::vector<std::string>*> (data);
    *out_text = vars->at (idx).c_str ();
    return true;
}

//--------------------------------------------------------------------------------------------------

void
draw_icons_load ()
{
    static std::vector<std::string> names;
    static int namesel = -1;
    static bool reload_names = false;
    static float items = -1;

    if (show_icons_load != reload_names)
    {
        reload_names = show_icons_load;
        enumerate_files (icons_directory + "*.json", names);
        for (auto& n: names)
            n.erase (n.find_last_of ('.'));
    }

    if (imgui.igBegin ("SSE MapTrack: Load icons", &show_icons_load, 0))
    {
        imgui.igText (icons_directory.c_str ());
        imgui.igListBoxFnPtr ("##Names",
                &namesel, extract_vector_string, &names, int (names.size ()), items);
        imgui.igSameLine (0, -1);
        imgui.igBeginGroup ();
        if (imgui.igButton ("Load", ImVec2 {-1, 0}) && unsigned (namesel) < names.size ())
            if (load_icons (icons_directory + names[namesel] + ".json"))
            	show_icons_load = false, icons_invalidated = true;
        if (imgui.igButton ("Cancel", ImVec2 {-1, 0}))
            show_icons_load = false;
        imgui.igEndGroup ();
        items = (imgui.igGetWindowHeight () / imgui.igGetTextLineHeightWithSpacing ()) - 4;
    }
    imgui.igEnd ();
}

//--------------------------------------------------------------------------------------------------

void
draw_track_load ()
{
    static std::vector<std::string> names;
    static int namesel = -1;
    static bool reload_names = false;
    static float items = -1;

    if (show_track_load != reload_names)
    {
        reload_names = show_track_load;
        enumerate_files (tracks_directory + "*.bin", names);
        for (auto& n: names)
            n.erase (n.find_last_of ('.'));
    }

    if (imgui.igBegin ("SSE MapTrack: Load track", &show_track_load, 0))
    {
        imgui.igText (tracks_directory.c_str ());
        imgui.igListBoxFnPtr ("##Names",
                &namesel, extract_vector_string, &names, int (names.size ()), items);
        imgui.igSameLine (0, -1);
        imgui.igBeginGroup ();
        if (imgui.igButton ("Load", ImVec2 {-1, 0}) && unsigned (namesel) < names.size ())
            if (load_track (tracks_directory + names[namesel] + ".bin"))
            	show_track_load = false;
        if (imgui.igButton ("Cancel", ImVec2 {-1, 0}))
            show_track_load = false;
        imgui.igEndGroup ();
        items = (imgui.igGetWindowHeight () / imgui.igGetTextLineHeightWithSpacing ()) - 4;
    }
    imgui.igEnd ();
}

//--------------------------------------------------------------------------------------------------

static float
trackpoint_height (void* data, int idx) {
    return std::next (*reinterpret_cast<track_t::const_iterator*> (data), idx)->z;
}

void
draw_track_summary ()
{
    if (imgui.igBegin ("SSE MapTrack: Track summary", &show_track_summary, 0))
    {
        static double len = 0;
        if (track_range.length_invalidated)
        {
            track_range.length_invalidated = false;
            len = track_t::compute_length (track_range.first, track_range.second);
        }
        imgui.igText ("Length: %.0f", len);

        auto bb = maptrack.track.bounding_box ();
        imgui.igText ("Bounding box min: %6.0f %6.0f %6.0f", bb.first.x, bb.first.y, bb.first.z);
        imgui.igText ("Bounding box max: %6.0f %6.0f %6.0f", bb.second.x, bb.second.y, bb.second.z);

        const char* name = "Altitude histogram";
        auto avail_sz = imgui.igGetContentRegionAvail ();
        avail_sz.x -= imgui.igCalcTextSize (name, nullptr, false, -1.f).x;
        imgui.igPlotLinesFnPtr (name, trackpoint_height, &track_range.first,
                int (std::distance (track_range.first, track_range.second)), 0, nullptr,
                bb.first.z, bb.second.z, avail_sz);
    }
    imgui.igEnd ();
}

//--------------------------------------------------------------------------------------------------

void
draw_icons_atlas ()
{
    if (imgui.igBegin ("SSE MapTrack: Icon atlas", &show_icons_atlas, 0))
    {
        const auto icons_size = std::min (maptrack.icon_atlas.stride, 16u);
        static glm::vec2 uvtl { 0 },
                         uvbr { maptrack.icon_atlas.icon_uvsize * icons_size };
        static glm::vec2 last_mouse_pos = {-1,-1};
        static bool hovered = false;
        static std::string index;

        imgui.igText ("Icon index: %s", index.c_str ());
        auto wsz = to_vec2 (imgui.igGetContentRegionAvail ());
        auto wpos = to_vec2 (imgui.igGetWindowPos ()) + to_vec2 (imgui.igGetCursorPos ());

        ImGuiIO* io = imgui.igGetIO ();
        glm::vec2 backup_uvtl = uvtl, backup_uvbr = uvbr;

        if (hovered)
        {
            auto mouse_pos = to_vec2 (io->MousePos);
            if (io->MouseDown[0])
            {
                if (last_mouse_pos.x == -1)
                    last_mouse_pos = mouse_pos;
                auto d = (uvbr - uvtl) * (mouse_pos - last_mouse_pos) / wsz;
                uvtl -= d;
                uvbr -= d;
                last_mouse_pos = mouse_pos;
            }
            else last_mouse_pos.x = -1;

            glm::ivec2 ndx {
                uvtl / maptrack.icon_atlas.icon_uvsize
                + float (icons_size) * (mouse_pos - wpos) / wsz };
            int i = ndx.x + ndx.y * maptrack.icon_atlas.stride;
            if (i < 0 || i >= int (maptrack.icon_atlas.icon_count))
                index.clear ();
            else index = std::to_string (i+1);
        }
        else last_mouse_pos.x = -1;

        if (hovered && io->MouseDown[0])
        {
            uvtl = glm::clamp (glm::vec2 (0), uvtl, glm::vec2 (1));
            uvbr = glm::clamp (glm::vec2 (0), uvbr, glm::vec2 (1));
            if (uvbr.x - uvtl.x != backup_uvbr.x - backup_uvtl.x)
                uvbr.x = backup_uvbr.x, uvtl.x = backup_uvtl.x;
            if (uvbr.y - uvtl.y != backup_uvbr.y - backup_uvtl.y)
                uvbr.y = backup_uvbr.y, uvtl.y = backup_uvtl.y;
        }

        imgui.igInvisibleButton ("Icon atlas", to_ImVec2 (wsz));
        imgui.ImDrawList_AddImage (imgui.igGetWindowDrawList (), maptrack.icon_atlas.ref,
                to_ImVec2 (wpos), to_ImVec2 (wpos + wsz),
                to_ImVec2 (uvtl), to_ImVec2 (uvbr), IM_COL32_WHITE);
        hovered = imgui.igIsItemHovered (0);
    }
    imgui.igEnd ();
}

//--------------------------------------------------------------------------------------------------
