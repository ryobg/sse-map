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

    imgui.igPushFont (ssemap.font.imfont);
    imgui.igSetNextWindowSize (ImVec2 { 800, 600 }, ImGuiCond_FirstUseEver);
    if (imgui.igBegin ("SSE Map", nullptr, 0))
    {
        static bool since_day = true;
        static float time_point = 0, today = 10;
        static int last_xdays = 1, total_days = 10, since_dayx = 0;
        static std::string time_point_s, current_location_s, since_day_s, last_dayxs;

        ImVec2 avail = imgui.igGetContentRegionAvail ();
        avail.y -= imgui.igGetCursorPosY ();

        imgui.igBeginGroup ();
        imgui.igSetNextItemWidth (avail.x * .80f);
        imgui.igSliderFloat ("##Time", &time_point, 0, today, "", 1);
        imgui.igImage (ssemap.map.ref, ImVec2 { avail.x * .80f, avail.y },
                *(ImVec2*) &ssemap.map.uv[0], *(ImVec2*) &ssemap.map.uv[2],
                imgui.igColorConvertU32ToFloat4 (ssemap.map.tint), ImVec4 {0,0,0,0});
        imgui.igEndGroup ();
        imgui.igSameLine (0, -1);
        imgui.igBeginGroup ();

        format_game_time (time_point_s, "Day %md of %lm", time_point);
        imgui.igText (time_point_s.c_str ());
        format_player_location (current_location_s, "Location: %x %y %z", { 100, 5000, -100 });
        imgui.igText (current_location_s.c_str ());

        if (imgui.igRadioButtonBool ("Since day", since_day))
            since_day = true;
        imgui.igSameLine (0, -1);
        imgui.igSetNextItemWidth (imgui.igCalcTextSize ("12345", nullptr, false, -1.f).x);
        imgui.igDragInt ("##Since day X", &since_dayx, .25f, 0, total_days, "%d");
        imgui.igSameLine (0, -1);
        format_game_time (since_day_s, "Day %md of %lm", since_dayx);
        imgui.igText (since_day_s.c_str ());

        if (imgui.igRadioButtonBool ("Last##X days", !since_day))
            since_day = false;
        imgui.igSameLine (0, -1);
        imgui.igSetNextItemWidth (imgui.igCalcTextSize ("12345", nullptr, false, -1.f).x);
        imgui.igDragInt ("##Last X days", &last_xdays, .25f, 1, total_days, "%d");
        imgui.igSameLine (0, -1);
        format_game_time (last_dayxs, "days (%md of %lm)", std::max (0.f, time_point-last_xdays));
        imgui.igText (last_dayxs.c_str ());

        imgui.igEndGroup ();
    }
    imgui.igEnd ();

    extern void draw_settings ();
    if (ssemap.show_settings)
        draw_settings ();

    imgui.igPopFont ();
}

//--------------------------------------------------------------------------------------------------

void
draw_settings ()
{
    if (imgui.igBegin ("SSE Map: Settings", &ssemap.show_settings, 0))
    {
        constexpr int cflags = ImGuiColorEditFlags_Float | ImGuiColorEditFlags_DisplayHSV
            | ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_PickerHueBar
            | ImGuiColorEditFlags_AlphaBar;

        imgui.igText ("Default font:");
        ImVec4 font_c = imgui.igColorConvertU32ToFloat4 (ssemap.font.color);
        if (imgui.igColorEdit4 ("Color##Default", (float*) &font_c, cflags))
            ssemap.font.color = imgui.igGetColorU32Vec4 (font_c);
        imgui.igSliderFloat ("Scale##Default", &ssemap.font.imfont->Scale, .5f, 2.f, "%.2f", 1);

        if (imgui.igButton ("Save settings", ImVec2 {}))
            save_settings ();
        imgui.igSameLine (0, -1);
        if (imgui.igButton ("Load settings", ImVec2 {}))
            load_settings ();
    }
    imgui.igEnd ();
}

//--------------------------------------------------------------------------------------------------

