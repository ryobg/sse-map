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
        ImVec2 avail = imgui.igGetContentRegionAvail ();
        avail.y -= imgui.igGetCursorPosY ();

        imgui.igBeginGroup ();
        imgui.igSetNextItemWidth (avail.x * .80f);
        imgui.igSliderFloat ("##Time", &maptrack.time_point, 0, 1, "", 1);
        imgui.igImage (maptrack.map.ref, ImVec2 { avail.x * .80f, avail.y },
                *(ImVec2*) &maptrack.map.uv[0], *(ImVec2*) &maptrack.map.uv[2],
                imgui.igColorConvertU32ToFloat4 (maptrack.map.tint), ImVec4 {0,0,0,0});
        imgui.igEndGroup ();
        imgui.igSameLine (0, -1);
        imgui.igBeginGroup ();

        format_game_time (track_start_s, "%md of %lm", track_start);
        format_game_time (track_end_s, "%md of %lm", track_end);
        imgui.igText ("From %s to %s", track_start_s.c_str (), track_end_s.c_str ());
        imgui.igText ("%s %d %d %d", current_world.c_str (),
                int (current_location[0]), int (current_location[1]), int (current_location[2]));

        auto dragday_size = imgui.igCalcTextSize ("12345", nullptr, false, -1.f);

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
        if (imgui.igDragInt ("##Last X days", &maptrack.last_xdays, .25f, 1, current_day, "%d"))
            maptrack.last_xdays = std::min (std::max (1, maptrack.last_xdays), current_day);
        imgui.igSameLine (0, -1);
        auto track_start2 = std::max (0.f, current_time - maptrack.last_xdays);
        format_game_time (last_xdays_s, "days i.e. %md of %lm, %Y", track_start2);
        imgui.igText (last_xdays_s.c_str ());

        track_start = since_day ? maptrack.since_dayx : track_start2;
        track_end = maptrack.time_point * (current_time - track_start) + track_start;

        imgui.igSeparator ();
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

