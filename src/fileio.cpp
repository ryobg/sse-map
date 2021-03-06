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
std::string map_settings_location = maptrack_directory + "settings_map.json";
std::string icons_settings_location = maptrack_directory + "settings_icons.json";
std::string tracks_directory = maptrack_directory + "tracks\\";
std::string default_track_file = tracks_directory + "default_track.bin";
std::string icons_directory = maptrack_directory + "icons\\";
std::string default_icons_file = icons_directory + "default_icons.json";

//--------------------------------------------------------------------------------------------------

static glm::uvec2
texture_size (ID3D11ShaderResourceView* srv)
{
    ID3D11Resource* res = nullptr;
    srv->GetResource (&res);

    ID3D11Texture2D* tex = nullptr;
    HRESULT hr = res->QueryInterface (&tex);

    glm::uvec2 sz {0,0};
    if (SUCCEEDED (hr))
    {
        D3D11_TEXTURE2D_DESC d;
	    tex->GetDesc (&d);
	    sz.x = d.Width;
	    sz.y = d.Height;
    }

    if (tex) tex->Release ();
    if (res) res->Release ();
    return sz;
}

//--------------------------------------------------------------------------------------------------

void
save_json (nlohmann::json& json, std::string const& file)
{
    int maj, min, patch;
    const char* timestamp;
    maptrack_version (&maj, &min, &patch, &timestamp);

    try
    {
        auto& j = json["version"];
        j["major"] = maj;
        j["minor"] = min;
        j["patch"] = patch;
        j["timestamp"] = timestamp;

        std::ofstream of (file);
        if (!of.is_open ())
            log () << "Unable to open " << file << " for writting." << std::endl;
        else
            of << json.dump (4);
    }
    catch (std::exception const& ex)
    {
        log () << "Unable to save " << file << " as JSON: " << ex.what () << std::endl;
        throw ex;
    }
}

//--------------------------------------------------------------------------------------------------

static nlohmann::json
load_json (std::string const& file)
{
    try
    {
        nlohmann::json json = nlohmann::json::object ();
        std::ifstream fi (file);
        if (!fi.is_open ())
            log () << "Unable to open " << file << " for reading." << std::endl;
        else
            fi >> json;
        return json;
    }
    catch (std::exception const& ex)
    {
        log () << "Unable to parse " << file << " as JSON: " << ex.what () << std::endl;
        throw ex;
    }
}

//--------------------------------------------------------------------------------------------------

bool
save_icons (std::string const& filename)
{
    try
    {
        nlohmann::json json;

        int i = 0;
        for (auto const& ico: maptrack.icons)
        {
            glm::vec2 tl = maptrack.map_to_game (ico.tl);
            glm::vec2 br = maptrack.map_to_game (ico.br);

            json["icons"][std::to_string (i++)] = {
                { "index", ico.index },
                { "tint", hex_string (ico.tint) },
                { "text", ico.text.c_str () },
                { "aabb", { tl.x, tl.y, br.x, br.y }},
                { "atlas", ico.atlas }
            };
        }

        save_json (json, filename);
    }
    catch (std::exception const& ex)
    {
        log () << "Unable to save icons file: " << ex.what () << std::endl;
        return false;
    }
    return true;
}

//--------------------------------------------------------------------------------------------------

static void
fix_older_icons (nlohmann::json const& json, icon_t& ico)
{
    bool fix = false;
    if (json.contains ("version"))
    {
        auto const& j = json["version"];
        int maj = j.value ("major", 1);
        int min = j.value ("minor", 3);
        int pat = j.value ("patch", 2);
        fix = maj <= 1 && min <= 3 && pat < 2;
    }
    if (!fix)
        return;
    ico.tl = maptrack.map_to_game (ico.tl);
    ico.br = maptrack.map_to_game (ico.br);
}

//--------------------------------------------------------------------------------------------------

bool
load_icons (std::string const& filename)
{
    try
    {
        nlohmann::json json = load_json (filename);

        std::vector<icon_t> icons;
        icons.reserve (json.at ("icons").size ());

        for (auto const& jico: json["icons"])
        {
            icon_t i;
            i.atlas = jico.value ("atlas", maptrack.icon_atlas.uid);
            if (i.atlas != maptrack.icon_atlas.uid)
            {
                log () << "Icon from different atlas (" << i.atlas <<
                    "), than the currently loaded one (" << maptrack.icon_atlas.uid
                    << "). Ignoring." << std::endl;
                continue;
            }
            i.tint = std::stoul (jico.at ("tint").get<std::string> (), nullptr, 0);
            i.text = jico.at ("text");
            auto it = jico.at ("aabb").begin ();
            i.tl.x = *it++; i.tl.y = *it++;
            i.br.x = *it++; i.br.y = *it;
            i.tl = maptrack.game_to_map (i.tl);
            i.br = maptrack.game_to_map (i.br);
            fix_older_icons (json, i);
            i.index = jico.at ("index");
            i.src = maptrack.icon_atlas.icon_uvsize * glm::vec2 {
                i.index % maptrack.icon_atlas.stride, i.index / maptrack.icon_atlas.stride };
            icons.push_back (i);
        }

        maptrack.icons = std::move (icons);
    }
    catch (std::exception const& ex)
    {
        log () << "Unable to load icons file: " << ex.what () << std::endl;
        return false;
    }
    return true;
}

//--------------------------------------------------------------------------------------------------

static void
save_map_settings ()
{
    nlohmann::json json = {
        { "map",  {
            { "file", maptrack.map.file.c_str () },
            { "tint", hex_string (maptrack.map.tint) },
            { "uv", { maptrack.map.uv[0], maptrack.map.uv[1],
                      maptrack.map.uv[2], maptrack.map.uv[3] }},
            { "scale", { maptrack.scale[0], maptrack.scale[1] }},
            { "offset", { maptrack.offset[0], maptrack.offset[1] }}
        }}
    };
    save_json (json, map_settings_location);
}

//--------------------------------------------------------------------------------------------------

static void
load_map_settings ()
{
    auto json = load_json (map_settings_location);

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
        throw std::runtime_error ("Bad map DDS file.");
}

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

void
save_icon_atlas ()
{
    nlohmann::json json = {
        { "icon atlas", {
            { "file", maptrack.icon_atlas.file },
            { "icon size", maptrack.icon_atlas.icon_size },
            { "icon count", maptrack.icon_atlas.icon_count },
            { "uid", maptrack.icon_atlas.uid }
        }}
    };
    save_json (json, icons_settings_location);
}

//--------------------------------------------------------------------------------------------------

bool
save_settings ()
{
    try
    {
        nlohmann::json json = {
            { "timeline", {
                { "enabled", maptrack.enabled },
                { "since dayx", maptrack.since_dayx },
                { "last xdays", maptrack.last_xdays },
                { "time point", maptrack.time_point }
            }},
            { "player", {
                { "enabled", maptrack.player.enabled },
                { "color", hex_string (maptrack.player.color) },
                { "size", maptrack.player.size }
            }},
            { "Fog of War", {
                { "enabled", maptrack.fow.enabled },
                { "resolution", maptrack.fow.resolution },
                { "discover", maptrack.fow.discover },
                { "default alpha", maptrack.fow.default_alpha },
                { "tracked alpha", maptrack.fow.tracked_alpha },
            }},
            { "update period", maptrack.update_period },
            { "min distance", maptrack.min_distance },
            { "track enabled", maptrack.track_enabled },
            { "track width", maptrack.track_width },
            { "track color", hex_string (maptrack.track_color) },
            { "Cursor info", {
                { "enabled", maptrack.cursor_info.enabled },
                { "deformation", maptrack.cursor_info.deformation },
                { "color", maptrack.cursor_info.color },
                { "scale", maptrack.cursor_info.scale }
            }}
        };

        save_font (json, maptrack.font);
        save_json (json, settings_location);
        save_icon_atlas ();
        save_map_settings ();
    }
    catch (std::exception const& ex)
    {
        log () << "Unable to save settings file: " << ex.what () << std::endl;
        return false;
    }
    return true;
}

//--------------------------------------------------------------------------------------------------

void
load_icon_atlas ()
{
    auto json = load_json (icons_settings_location);
    auto& atlas = maptrack.icon_atlas;

    atlas.file = maptrack_directory + "icons.dds";
    atlas.icon_size = 64;
    atlas.icon_count = 3509;
    atlas.uid = "c70d7839c21dff225b61ec0f09395afbde4d222eed2d70fa6f2ce4ad50327ac2";
    if (json.contains ("icon atlas"))
    {
        auto const& j = json.at ("icon atlas");
        atlas.icon_size = j.at ("icon size");
        atlas.icon_count = j.at ("icon count");
        atlas.file = j.at ("file");
        atlas.uid = j.value ("uid", atlas.uid);
    }
    if (!sseimgui.ddsfile_texture (atlas.file.c_str (), nullptr, &atlas.ref))
        throw std::runtime_error ("Bad icons DDS file.");
    atlas.size = texture_size (atlas.ref).x;
    atlas.icon_uvsize = float (atlas.icon_size) / atlas.size;
    atlas.stride = maptrack.icon_atlas.size / maptrack.icon_atlas.icon_size;
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
        maptrack.min_distance = json.value ("min distance", 10.f); //1:205 map scale by 5x zoom
        maptrack.track_enabled = json.value ("track enabled", true);
        maptrack.track_width = json.value ("track width", 3.f);
        maptrack.track_color = std::stoul (json.value ("track color", "0xFF400000"), nullptr, 0);
        maptrack.track.merge_distance (maptrack.min_distance);

        maptrack.player.enabled = true;
        maptrack.player.color = 0xFF400000;
        maptrack.player.size = 6.f;
        if (json.contains ("player"))
        {
            auto const& jp = json.at ("player");
            maptrack.player.enabled = jp.at ("enabled");
            maptrack.player.color = std::stoul (jp.at ("color").get<std::string> (), nullptr, 0);
            maptrack.player.size = jp.at ("size");
        }

        maptrack.fow.enabled = true;
        maptrack.fow.resolution = 128;
        maptrack.fow.discover   = 4;
        maptrack.fow.player_alpha = 0.f;
        maptrack.fow.default_alpha = 1.f;
        maptrack.fow.tracked_alpha = .5f;
        if (json.contains ("Fog of War"))
        {
            auto const& j = json.at ("Fog of War");
            maptrack.fow.enabled = j.value ("enabled", maptrack.fow.enabled);
            maptrack.fow.resolution = j.value ("resolution", maptrack.fow.resolution);
            maptrack.fow.discover = j.value ("discover", maptrack.fow.discover);
            maptrack.fow.default_alpha = j.value ("default alpha", maptrack.fow.default_alpha);
            maptrack.fow.tracked_alpha = j.value ("tracked alpha", maptrack.fow.tracked_alpha);
        }

        maptrack.cursor_info.enabled = true;
        maptrack.cursor_info.color = IM_COL32_WHITE;
        maptrack.cursor_info.scale = 1.f;
        maptrack.cursor_info.deformation = false;
        if (json.contains ("Cursor info"))
        {
            auto const& j = json.at ("Cursor info");
            maptrack.cursor_info.enabled = j.value ("enabled", maptrack.cursor_info.enabled);
            maptrack.cursor_info.color = j.value ("color", maptrack.cursor_info.color);
            maptrack.cursor_info.scale = j.value ("scale", maptrack.cursor_info.scale);
            maptrack.cursor_info.deformation = j.value ("deformation",
                    maptrack.cursor_info.deformation);
        };

        load_icon_atlas ();
        load_map_settings ();
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
        maptrack.track.save_binary (f);
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
        maptrack.track.load_binary (f);
    }
    catch (std::exception const& ex)
    {
        log () << "Unable to load track file: " << ex.what () << std::endl;
        return false;
    }
    return true;
}

//--------------------------------------------------------------------------------------------------

