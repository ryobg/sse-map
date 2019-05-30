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
            show_saveas = false,
            show_load = false;

/// Current HWND, used for timer management
static HWND top_window = nullptr;
static std::string current_location, current_time;

/// The current track as pairs of X/Y coordinates upon the texture map
static std::vector<glm::vec2> uvtrack;

//--------------------------------------------------------------------------------------------------

static inline decltype (uvtrack)::value_type
uvtrack_point (trackpoint_t const& t)
{
    return maptrack.offset + glm::vec2 { t.x * maptrack.scale.x, -t.y * maptrack.scale.y };
}

//--------------------------------------------------------------------------------------------------

static VOID CALLBACK
timer_callback (HWND hwnd, UINT message, UINT_PTR idTimer, DWORD dwTime)
{
    if (!maptrack.enabled)
        return;

    constexpr float nan = std::numeric_limits<float>::quiet_NaN ();
    static trackpoint_t last_newp { nan, nan, nan, nan };
    auto curr_world = current_worldspace ();
    auto curr_loc = player_location ();
    auto curr_time = game_time ();

    trackpoint_t newp { curr_loc[0], curr_loc[1], curr_loc[2], curr_time };
    if (last_newp[3] > curr_time) // Override history
    {
        auto it = std::upper_bound (maptrack.track.begin (), maptrack.track.end (),
                curr_time, [] (float t, auto const& p) { return t < p[3]; });
        if (it != maptrack.track.end ())
        {
            auto n = std::distance (it, maptrack.track.end ());
            uvtrack.erase (uvtrack.begin () + n, uvtrack.end ());
            maptrack.track.erase (it, maptrack.track.end ());
        }
    }

    // Match of X & Y just replaces. Don't care about vertical or time differences. In Game menu,
    // position stays the same as well as the time. This will handle conversation though not small
    // movements...
    if (last_newp[0] == newp[0] && last_newp[1] == newp[1] && !maptrack.track.empty ())
    {
        maptrack.track.back () = newp;
        uvtrack.back () = uvtrack_point (newp);
    }
    else
    {
        maptrack.track.push_back (newp);
        uvtrack.push_back (uvtrack_point (newp));
    }
    last_newp = newp;

    current_location = "Location:";
    if (!curr_world.empty ())
        current_location += " " + curr_world;
    for (auto const& l: curr_loc)
        current_location += " " + std::to_string (int (l));

    format_game_time (current_time, "Day %ri, %md of %lm, %Y", curr_time);
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

    imgui.igImage (maptrack.map.ref, to_ImVec2 (map_size), to_ImVec2 (uvtl), to_ImVec2 (uvbr),
            imgui.igColorConvertU32ToFloat4 (maptrack.map.tint), ImVec4 {0,0,0,0});

    if (!uvtrack.empty ())
    {
        auto lpp = uvtrack.back (); // last player position
        if (lpp.x > uvtl.x && lpp.x < uvbr.x && lpp.y > uvtl.y && lpp.y < uvbr.y)
        {
            imgui.ImDrawList_AddCircleFilled (imgui.igGetWindowDrawList (),
                to_ImVec2 (wpos + map_pos + map_size * (lpp - uvtl) / (uvbr - uvtl)),
                8, IM_COL32 (0,0,255,255), 12);
        }
    }

    // One frame later we handle the input, so to allow the ImGui hover test. Otherwise, if
    // scrolling on other window which happens to be in front of the map, it will zoom in/out,
    // making awkward behaviour.
    if (hovered = imgui.igIsItemHovered (0))
    {
        mouse_wheel = io->MouseWheel ? (io->MouseWheel > 0 ? +1 : -1) : 0;
    }

    // Not very wise, but ImGui makes it hard to disable window drag per widget. Other option
    // probably would be to use InvisibleButton or ImageButton, but they bring their own troubles.
    io->ConfigWindowsMoveFromTitleBarOnly = hovered;
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

        float last_recorded_time = maptrack.track.empty () ? 0.f : maptrack.track.back ()[3];
        int last_recorded_day = int (last_recorded_time);
        auto dragday_size = imgui.igCalcTextSize ("1345", nullptr, false, -1.f);
        auto mapsz = to_vec2 (imgui.igGetContentRegionAvail ());
        mapsz.x -= dragday_size.x * 12; // ~48 chars based on max text content widgets below

        imgui.igBeginGroup ();
        imgui.igSetNextItemWidth (mapsz.x);
        imgui.igSliderFloat ("##Time", &maptrack.time_point, 0, 1, "", 1);
        auto mappos = to_vec2 (imgui.igGetCursorPos ());
        mapsz.y -= mappos.y;
        draw_map (mappos, mapsz);
        imgui.igEndGroup ();
        imgui.igSameLine (0, -1);
        imgui.igBeginGroup ();

        format_game_time_c<1> (track_start_s, "From day %ri, %md of %lm", track_start);
        format_game_time_c<2> (track_end_s, "to day %ri, %md of %lm", track_end);
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
        imgui.igSeparator ();

        if (imgui.igRadioButtonBool ("Since day", since_day))
            since_day = true;
        imgui.igSameLine (0, -1);
        imgui.igSetNextItemWidth (dragday_size.x);
        if (imgui.igDragInt ("##Since day", &maptrack.since_dayx, .25f, 0, last_recorded_day, "%d"))
            maptrack.since_dayx = std::min (std::max (0, maptrack.since_dayx), last_recorded_day);
        imgui.igSameLine (0, -1);
        format_game_time_c<3> (since_dayx_s, "i.e. %md of %lm, %Y", maptrack.since_dayx);
        imgui.igText (since_dayx_s.c_str ());

        if (imgui.igRadioButtonBool ("Last##X days", !since_day))
            since_day = false;
        imgui.igSameLine (0, -1);
        imgui.igSetNextItemWidth (dragday_size.x);
        if (imgui.igDragInt ("##Last X days",
                    &maptrack.last_xdays, .25f, 1, 1 +last_recorded_day, "%d"))
            maptrack.last_xdays = std::min (std::max (1, maptrack.last_xdays), last_recorded_day);
        imgui.igSameLine (0, -1);
        auto track_start2 = std::max (0.f, last_recorded_time - maptrack.last_xdays);
        format_game_time_c<4> (last_xdays_s, "days i.e. %md of %lm, %Y", track_start2);
        imgui.igText (last_xdays_s.c_str ());

        track_start = since_day ? maptrack.since_dayx : track_start2;
        track_end = maptrack.time_point * (last_recorded_time - track_start) + track_start;

        imgui.igSeparator ();
        imgui.igText ("Track (%u points)", unsigned (maptrack.track.size ()));
        if (imgui.igButton ("Save", ImVec2 {-1, 0}))
            save_track (default_track_file);
        if (imgui.igButton ("Save As", ImVec2 {-1, 0}))
            show_saveas = !show_saveas;
        if (imgui.igButton ("Load", ImVec2 {-1, 0}))
            show_load = !show_load;
        if (imgui.igButton ("Clear", ImVec2 {-1,0}))
            imgui.igOpenPopup ("Clear track?");
        if (imgui.igBeginPopup ("Clear track?", 0))
        {
            if (imgui.igButton ("Are you sure?##Clear", ImVec2 {}))
            {
                maptrack.track.clear ();
                uvtrack.clear ();
                imgui.igCloseCurrentPopup ();
            }
            imgui.igEndPopup ();
        }

        imgui.igSeparator ();
        if (imgui.igButton ("Settings", ImVec2 {-1,0}))
            show_settings = !show_settings;

        imgui.igEndGroup ();
    }
    imgui.igEnd ();

    void draw_settings ();
    if (show_settings)
        draw_settings ();
    void draw_saveas ();
    if (show_saveas)
        draw_saveas ();
    void draw_load ();
    if (show_load)
        draw_load ();

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
draw_saveas ()
{
    static std::string name;
    if (imgui.igBegin ("SSE MapTrack: Save as", &show_saveas, 0))
    {
        imgui.igText (tracks_directory.c_str ());
        imgui_input_text ("Name", name);
        if (imgui.igButton ("Cancel", ImVec2 {}))
            show_saveas = false;
        imgui.igSameLine (0, -1);
        if (imgui.igButton ("Save", ImVec2 {}))
        {
            auto file = tracks_directory + name + ".bin";
            if (file_exists (file))
                imgui.igOpenPopup ("Overwrite track?");
            else if (save_track (file))
                show_saveas = false;
        }
        if (imgui.igBeginPopup ("Overwrite track?", 0))
        {
            if (imgui.igButton ("Overwrite track?##file", ImVec2 {}))
                if (save_track (tracks_directory + name + ".bin"))
                    show_saveas = false;
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
draw_load ()
{
    static std::vector<std::string> names;
    static int namesel = -1;
    static bool reload_names = false;
    static float items = -1;

    if (show_load != reload_names)
    {
        reload_names = show_load;
        enumerate_files (tracks_directory + "*.bin", names);
        for (auto& n: names)
            n.erase (n.find_last_of ('.'));
    }

    if (imgui.igBegin ("SSE MapTrack: Load", &show_load, 0))
    {
        imgui.igText (tracks_directory.c_str ());
        imgui.igListBoxFnPtr ("##Names",
                &namesel, extract_vector_string, &names, int (names.size ()), items);
        imgui.igSameLine (0, -1);
        imgui.igBeginGroup ();
        if (imgui.igButton ("Load", ImVec2 {-1, 0}) && unsigned (namesel) < names.size ())
            if (load_track (tracks_directory + names[namesel] + ".bin"))
            {
                uvtrack.resize (maptrack.track.size ());
                std::transform (maptrack.track.begin (), maptrack.track.end (), uvtrack.begin (),
                        uvtrack_point);
            	show_load = false;
            }
        if (imgui.igButton ("Cancel", ImVec2 {-1, 0}))
            show_load = false;
        imgui.igEndGroup ();
        items = (imgui.igGetWindowHeight () / imgui.igGetTextLineHeightWithSpacing ()) - 3;
    }
    imgui.igEnd ();
}

//--------------------------------------------------------------------------------------------------
