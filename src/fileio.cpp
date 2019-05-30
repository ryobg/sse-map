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

std::string maptrack_directory = "Data\\SKSE\\Plugins\\sse-maptrack\\";
std::string settings_location = maptrack_directory + "settings.json";
std::string tracks_directory = maptrack_directory + "tracks\\";
std::string default_track_file = tracks_directory + "default_track.bin";

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
                { "scale", { maptrack.scale[0], maptrack.scale[1] }},
                { "offset", { maptrack.offset[0], maptrack.offset[1] }}
            }},
            { "timeline", {
                { "enabled", maptrack.enabled },
                { "since dayx", maptrack.since_dayx },
                { "last xdays", maptrack.last_xdays },
                { "time point", maptrack.time_point }
            }},
            { "update period", maptrack.update_period }
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
    font.file = jf.value ("file", maptrack_directory + font.name + ".ttf");
    if (font.file.empty ())
        font.file = maptrack_directory + font.name + ".ttf";

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
        nlohmann::json json = nlohmann::json::object ();
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

        maptrack.enabled = true;
        maptrack.since_dayx = 0;
        maptrack.last_xdays = 1;
        maptrack.time_point = 1;
        if (json.contains ("timeline"))
        {
            auto const& jt = json.at ("timeline");
            maptrack.enabled = jt.at ("enabled");
            maptrack.since_dayx = jt.at ("since dayx");
            maptrack.last_xdays = jt.at ("last xdays");
            maptrack.time_point = jt.at ("time point");
        }

        maptrack.update_period = json.value ("update period", 5.f);

        maptrack.map = image_t {};
        maptrack.map.uv = { 0, 0, 1, .711f };
        maptrack.offset = { .4766f, .3760f };
        maptrack.scale = { 1.f/(2048*205), 1.f/(2048*205) };
        maptrack.map.file = maptrack_directory + "map.dds";
        if (json.contains ("map"))
        {
            auto const& jmap = json.at ("map");
            auto it = jmap.at ("uv").begin ();
            for (float& v: maptrack.map.uv) v = *it++;
            it = jmap.at ("scale").begin ();
            for (float& v: maptrack.scale) v = *it++;
            it = jmap.at ("offset").begin ();
            for (float& v: maptrack.offset) v = *it++;
            maptrack.map.tint = std::stoull (jmap.at ("tint").get<std::string> (), nullptr, 0);
            maptrack.map.file = jmap.value ("file", maptrack_directory + "map.dds");
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

template<class T, class Stream>
inline void
write_binary (Stream& os, T const& value)
{
    os.write (reinterpret_cast<const char*> (&value), sizeof (T));
}

template<class T, class Stream>
inline void
write_binary (Stream& os, T const* value, std::size_t size)
{
    os.write (reinterpret_cast<const char*> (value), size);
}

//--------------------------------------------------------------------------------------------------

bool
save_track (std::string const& file)
{
    std::int32_t maj, min, patch;
    maptrack_version (&maj, &min, &patch, nullptr);
    try
    {
        std::ofstream f (file, std::ios::binary | std::ios::out);
        if (!f.is_open ())
        {
            log () << "Unable to open " << file << " for writting." << std::endl;
            return false;
        }
        write_binary (f, maj);
        write_binary (f, min);
        write_binary (f, patch);
        write_binary (f, std::uint32_t (maptrack.track.size ()));
        write_binary (f, maptrack.track.data (), maptrack.track.size () * sizeof (trackpoint_t));
    }
    catch (std::exception const& ex)
    {
        log () << "Unable to save track file: " << ex.what () << std::endl;
        return false;
    }
    return true;
}

//--------------------------------------------------------------------------------------------------

template<class T, class Stream>
inline T
read_binary (Stream& is)
{
    T value;
    is.read (reinterpret_cast<char*> (&value), sizeof (T));
    return value;
}

template<class T, class Stream>
inline void
read_binary (Stream& is, T* value, std::size_t size)
{
    is.read (reinterpret_cast<char*> (value), size);
}

//--------------------------------------------------------------------------------------------------

bool
load_track (std::string const& file)
{
    try
    {
        std::ifstream f (file, std::ios::binary | std::ios::in);
        if (!f.is_open ())
        {
            log () << "Unable to open " << file << " for reading." << std::endl;
            return false;
        }
        read_binary<std::int32_t> (f);
        read_binary<std::int32_t> (f);
        read_binary<std::int32_t> (f);
        maptrack.track.resize (read_binary<std::uint32_t> (f));
        read_binary (f, maptrack.track.data (), maptrack.track.size () * sizeof (trackpoint_t));
    }
    catch (std::exception const& ex)
    {
        log () << "Unable to load track file: " << ex.what () << std::endl;
        return false;
    }
    return true;
}

//--------------------------------------------------------------------------------------------------

