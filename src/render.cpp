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

#include <windows.h>

//--------------------------------------------------------------------------------------------------

maptrack_t maptrack = {};

static bool show_settings = false;

/// Current HWND, used for timer management
static HWND top_window = nullptr;
static std::string current_world;
static std::array<float, 3> current_location = {0,0,0};
static float current_time = 0;

//--------------------------------------------------------------------------------------------------

static VOID CALLBACK
timer_callback (HWND hwnd, UINT message, UINT_PTR idTimer, DWORD dwTime)
{
    if (!maptrack.enabled)
        return;
    current_world = current_worldspace ();
    current_location = player_location ();
    current_time = game_time ();
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
    return true;
}

//--------------------------------------------------------------------------------------------------

static void
draw_map (ImVec2 const& map_pos, ImVec2 const& map_size)
{
    auto const& oruv = maptrack.map.uv;
    static auto uvpos = *(ImVec2*) &oruv[0];
    static auto uvsz = *(ImVec2*) &oruv[2];
    static bool hovered = false;
    static float mouse_wheel = 0;
    static ImVec2 mouse_drag = {-1,-1};
    constexpr float zoom_factor = .01f;
    constexpr float move_factor = .01f;
    ImGuiIO* io = imgui.igGetIO ();

    if (hovered)
    {
        if (mouse_wheel)
        {
            auto wpos = imgui.igGetWindowPos ();
            ImVec2 zpos {
                (io->MousePos.x - map_pos.x - wpos.x) / map_size.x,
                (io->MousePos.y - map_pos.y - wpos.y) / map_size.y };
            auto dx = uvsz.x * mouse_wheel * zoom_factor;
            auto dy = uvsz.y * mouse_wheel * zoom_factor;
            uvpos.x += dx * zpos.x;
            uvpos.y += dy * zpos.y;
            uvsz.x -= dx;
            uvsz.y -= dy;
        }
        if (io->MouseDown[0])
        {
            if (mouse_drag.x == -1)
                mouse_drag = io->MousePos;
            float dx = (io->MousePos.x - mouse_drag.x) / map_size.x;
            float dy = (io->MousePos.y - mouse_drag.y) / map_size.y;
            uvsz.x -= dx * (uvsz.x / oruv[2]);
            uvsz.y -= dy * (uvsz.y / oruv[3]);
            mouse_drag = io->MousePos;
        }
        else mouse_drag.x = -1;
    }
    else mouse_drag.x = -1;

    uvpos.x = std::max (oruv[0], std::min (uvpos.x, oruv[2]));
    uvpos.y = std::max (oruv[1], std::min (uvpos.y, oruv[3]));
    uvsz.x  = std::max (.2f*oruv[2], std::min (uvsz.x, oruv[2]));
    uvsz.y  = std::max (.2f*oruv[3], std::min (uvsz.y, oruv[3]));

    imgui.igImage (maptrack.map.ref, map_size, uvpos, uvsz,
            imgui.igColorConvertU32ToFloat4 (maptrack.map.tint), ImVec4 {0,0,0,0});

    // One frame later we handle the input, so to allow the ImGui hover test. Otherwise, if
    // scrolling on other window which happens to be in front of the map, it will zoom in/out,
    // making awkward behaviour.
    if (hovered = imgui.igIsItemHovered (0))
    {
        mouse_wheel = io->MouseWheel ? (io->MouseWheel > 0 ? +1 : -1) : 0;
    }
}

//--------------------------------------------------------------------------------------------------

void SSEIMGUI_CCONV
render (int active)
{
    if (!active)
        return;

    imgui.igPushFont (maptrack.font.imfont);
    imgui.igSetNextWindowSize (ImVec2 { 800, 600 }, ImGuiCond_FirstUseEver);
    if (imgui.igBegin ("SSE MapTrack", nullptr, 0))
    {
        static bool since_day = true;
        static float track_end = 0, track_start = 0;
        static std::string track_start_s, track_end_s,
            current_location_s, since_dayx_s, last_xdays_s;

        int current_day = int (current_time);
        auto dragday_size = imgui.igCalcTextSize ("1345", nullptr, false, -1.f);
        ImVec2 mapsz = imgui.igGetContentRegionAvail ();
        mapsz.x -= dragday_size.x * 12; // ~48 chars based on max text content widgets below

        imgui.igBeginGroup ();
        imgui.igSetNextItemWidth (mapsz.x);
        imgui.igSliderFloat ("##Time", &maptrack.time_point, 0, 1, "", 1);
        draw_map (imgui.igGetCursorPos (), mapsz);
        imgui.igEndGroup ();
        imgui.igSameLine (0, -1);
        imgui.igBeginGroup ();

        format_game_time (track_start_s, "From day %ri, %md of %lm", track_start);
        format_game_time (track_end_s, "to day %ri, %md of %lm", track_end);
        imgui.igText (track_start_s.c_str ());
        imgui.igText (track_end_s.c_str ());
        imgui.igText ("%s %d %d %d", current_world.c_str (),
                int (current_location[0]), int (current_location[1]), int (current_location[2]));

        imgui.igSeparator ();
        imgui.igCheckbox ("Tracking enabled", &maptrack.enabled);
        imgui.igSeparator ();

        if (imgui.igRadioButtonBool ("Since day", since_day))
            since_day = true;
        imgui.igSameLine (0, -1);
        imgui.igSetNextItemWidth (dragday_size.x);
        if (imgui.igDragInt ("##Since day", &maptrack.since_dayx, .25f, 0, current_day, "%d"))
            maptrack.since_dayx = std::min (std::max (0, maptrack.since_dayx), current_day);
        imgui.igSameLine (0, -1);
        format_game_time (since_dayx_s, "i.e. %md of %lm, %Y", maptrack.since_dayx);
        imgui.igText (since_dayx_s.c_str ());

        if (imgui.igRadioButtonBool ("Last##X days", !since_day))
            since_day = false;
        imgui.igSameLine (0, -1);
        imgui.igSetNextItemWidth (dragday_size.x);
        if (imgui.igDragInt ("##Last X days", &maptrack.last_xdays, .25f, 1, 1+current_day, "%d"))
            maptrack.last_xdays = std::min (std::max (1, maptrack.last_xdays), current_day);
        imgui.igSameLine (0, -1);
        auto track_start2 = std::max (0.f, current_time - maptrack.last_xdays);
        format_game_time (last_xdays_s, "days i.e. %md of %lm, %Y", track_start2);
        imgui.igText (last_xdays_s.c_str ());

        track_start = since_day ? maptrack.since_dayx : track_start2;
        track_end = maptrack.time_point * (current_time - track_start) + track_start;

        imgui.igSeparator ();
        if (imgui.igButton ("Settings", ImVec2 {}))
            show_settings = !show_settings;

        imgui.igEndGroup ();
    }
    imgui.igEnd ();

    extern void draw_settings ();
    if (show_settings)
        draw_settings ();

    imgui.igPopFont ();
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
        ImVec4 font_c = imgui.igColorConvertU32ToFloat4 (maptrack.font.color);
        if (imgui.igColorEdit4 ("Color##Default", (float*) &font_c, cflags))
            maptrack.font.color = imgui.igGetColorU32Vec4 (font_c);
        imgui.igSliderFloat ("Scale##Default", &maptrack.font.imfont->Scale, .5f, 2.f, "%.2f", 1);

        if (imgui.igButton ("Save settings", ImVec2 {}))
            save_settings ();
        imgui.igSameLine (0, -1);
        if (imgui.igButton ("Load settings", ImVec2 {}))
            load_settings ();
    }
    imgui.igEnd ();
}

//--------------------------------------------------------------------------------------------------

