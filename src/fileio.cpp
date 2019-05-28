/**
 * @file fileio.cpp
 * @brief File saving and loading and etc. I/O related operations
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
#include <gsl/gsl_util>
#include <fstream>

// Warning come in a BSON parser, which is not used, and probably shouldn't be
#if defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wformat="
#  pragma GCC diagnostic ignored "-Wformat-extra-args"
#  include <nlohmann/json.hpp>
#  pragma GCC diagnostic pop
#endif

//--------------------------------------------------------------------------------------------------

std::string ssemap_directory = "Data\\SKSE\\Plugins\\sse-map\\";
std::string settings_location = ssemap_directory + "settings.json";

//--------------------------------------------------------------------------------------------------

static void
save_font (nlohmann::json& json, font_t const& font)
{
    auto& jf = json[font.name + " font"];
    jf["scale"] = font.imfont->Scale;
    jf["color"] = hex_string (font.color);
    jf["size"] = font.imfont->FontSize;
    jf["file"] = font.file;
}

//--------------------------------------------------------------------------------------------------

bool
save_settings ()
{
    int maj, min, patch;
    const char* timestamp;
    maptrack_version (&maj, &min, &patch, &timestamp);

    try
    {
        nlohmann::json json = {
            { "version", {
                { "major", maj },
                { "minor", min },
                { "patch", patch },
                { "timestamp", timestamp }
            }},
            { "map",  {
                { "file", maptrack.map.file.c_str () },
                { "tint", hex_string (maptrack.map.tint) },
                { "uv", { maptrack.map.uv[0], maptrack.map.uv[1],
                          maptrack.map.uv[2], maptrack.map.uv[3] }},
            }}
        };
        save_font (json, maptrack.font);

        std::ofstream of (settings_location);
        if (!of.is_open ())
        {
            log () << "Unable to open " << settings_location << " for writting." << std::endl;
            return false;
        }
        of << json.dump (4);
    }
    catch (std::exception const& ex)
    {
        log () << "Unable to save settings file: " << ex.what () << std::endl;
        return false;
    }
    return true;
}

//--------------------------------------------------------------------------------------------------

static void
load_font (nlohmann::json const& json, font_t& font)
{
    std::string section = font.name + " font";
    nlohmann::json jf;
    if (json.contains (section))
        jf = json[section];
    else jf = nlohmann::json::object ();

    font.color = std::stoull (jf.value ("color", hex_string (font.color)), nullptr, 0);
    font.scale = jf.value ("scale", font.scale);
    auto set_scale = gsl::finally ([&font] { font.imfont->Scale = font.scale; });

    // The UI load button, otherwise we have to recreate all fonts outside the rendering loop
    // and thats too much of hassle if not tuning all other params through the UI too.
    if (font.imfont)
        return;

    font.size = jf.value ("size", font.size);
    font.file = jf.value ("file", ssemap_directory + font.name + ".ttf");
    if (font.file.empty ())
        font.file = ssemap_directory + font.name + ".ttf";

    auto font_atlas = imgui.igGetIO ()->Fonts;

    if (file_exists (font.file))
        font.imfont = imgui.ImFontAtlas_AddFontFromFileTTF (
                font_atlas, font.file.c_str (), font.size, nullptr, nullptr);
    if (!font.imfont)
    {
        font.imfont = imgui.ImFontAtlas_AddFontFromMemoryCompressedBase85TTF (
            font_atlas, font.default_data, font.size, nullptr, nullptr);
        font.file.clear ();
    }
}

//--------------------------------------------------------------------------------------------------

bool
load_settings ()
{
    try
    {
        nlohmann::json json;
        std::ifstream fi (settings_location);
        if (!fi.is_open ())
            log () << "Unable to open " << settings_location << " for reading." << std::endl;
        else
            fi >> json;

        extern const char* font_inconsolata;
        maptrack.font.name = "default";
        maptrack.font.scale = 1.f;
        maptrack.font.size = 18.f;
        maptrack.font.color = IM_COL32_WHITE;
        maptrack.font.file = "";
        maptrack.font.default_data = font_inconsolata;
        load_font (json, maptrack.font);

        maptrack.map = image_t {};
        maptrack.map.uv[3] = .711f;
        maptrack.map.file = ssemap_directory + "map.dds";
        if (json.contains ("map"))
        {
            auto const& jmap = json["map"];
            auto it = jmap["uv"].begin ();
            for (float& uv: maptrack.map.uv) uv = *it++;
            maptrack.map.tint = std::stoull (jmap["tint"].get<std::string> (), nullptr, 0);
            maptrack.map.file = jmap.value ("file", ssemap_directory + "map.dds");
        }
        if (!sseimgui.ddsfile_texture (maptrack.map.file.c_str (), nullptr, &maptrack.map.ref))
            throw std::runtime_error ("Bad DDS file.");
    }
    catch (std::exception const& ex)
    {
        log () << "Unable to load settings file: " << ex.what () << std::endl;
        return false;
    }
    return true;
}

//--------------------------------------------------------------------------------------------------

