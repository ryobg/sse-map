/**
 * @file variables.cpp
 * @brief Methods for obtaining the so called Map Variables
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
#include <sse-hooks/sse-hooks.h>

#include <array>
#include <vector>
#include <string>
#include <functional>
#include <ctime>
#include <cmath>
#include <algorithm>

#include <windows.h>

//--------------------------------------------------------------------------------------------------

/// Defined in skse.cpp
extern sseh_api sseh;

/// To turn relative addresses into absolute so that the Skyrim watch points can be set.
static std::uintptr_t skyrim_base = 0;

/// Obtains an address to a relative object, to a relative object, to a relative object, to a...
template<class T, unsigned N = 1>
struct relocation
{
    std::array<std::uintptr_t, 1+N> offsets;
    T obtain () const
    {
        std::uintptr_t that = skyrim_base;
        for (unsigned i = 0; i < N; ++i)
        {
            that = *reinterpret_cast<std::uintptr_t*> (that + offsets[i]);
            if (!that) return nullptr;
        }
        return reinterpret_cast<T> (that + offsets[N]);
    }
};

/**
 * Current in-game time since...
 *
 * Integer part: Day (starting from zero)
 * Floating part: Hours as % of 24,
 *                Minutes as % of 60
 *                Seconds as % of 60
 *                and so on...
 * In the main menu, the number may vary. 1 at start, 1.333 after "Quit to Main Menu" and maybe
 * other values, depending on the situation. At start of the game, the pointer reference is null,
 * hence no way to obtain the value.
 *
 * The game starts at Sundas, the 17th of Last Seed, 4E201, near 09:30. At that time the value is
 * something like 0.45 or so
 *
 * Found five consecitive pointers with offsets which seems to reside somewhere in the Papyrus
 * virtual machine object (0x1ec3b78) according to SKSE. Weirdly, it is inside the eventSink array
 * as specified there. No clue what is it, but on this machine and runtime it seems stable
 * reference:
 *
 * *0x1ec3ba8 + 0x114
 * *0x1ec3bb0 +  0xdc
 * *0x1ec3bb8 +  0xa4
 * *0x1ec3bc0 +  0x6c
 * *0x1ec3bc8 +  0x34
 */

struct relocation<float*> game_epoch { 0x1ec3bc8, 0x34 };

/**
 * Player position as 3 xyz floats.
 *
 * This field can be seen in as static offset SkyrimSE.exe + 0x3233490, but the Z coordinate seems
 * off, compared to the Console "player.getpos z" calls. There is also what seems to be the camera
 * position in SkyrimSE.exe + 0x2F3B854, but its Z coord is also a bit weird. Instead, here it is
 * used the global player reference. As seen from SKSE, this is PlayerCharacter -> Actor ->
 * TESObjectRERF -> pos as NiPoint3.
 */

struct relocation<float*> player_pos { 0x2f26ef8, 0x54 };

/// Used to differentiate between exterior (unnamed) and interior (named) cell in Skyrim worldspace

struct relocation<const char*, 3> player_cell { 0x2f26ef8, 0x60, 0x28, 0 };

/**
 * Current worldspace pointer from the PlayerCharacter class accroding to SKSE.
 *
 * PlayerCharacter -> CurrentWorldspace -> Fullname -> String data. The worldspace does not exist
 * during Main Menu, and likely in some locations like the Alternate Start room.
 */

struct relocation<const char*, 3> worldspace_name { 0x2f26ef8, 0x628, 0x28, 0x00 };

//--------------------------------------------------------------------------------------------------

/// Small utility function
static void
replace_all (std::string& data, std::string const& search, std::string const& replace)
{
    std::size_t n = data.find (search);
    while (n != std::string::npos)
    {
        data.replace (n, search.size (), replace);
        n = data.find (search, n + replace.size ());
    }
}

//--------------------------------------------------------------------------------------------------

std::array<float, 3>
obtain_player_location ()
{
    constexpr float nan = std::numeric_limits<float>::quiet_NaN ();
    float* pos = player_pos.obtain ();
    if (!pos || !std::isfinite (pos[0]) || !std::isfinite (pos[1]) || !std::isfinite (pos[2]))
        return { nan, nan, nan };
    return { pos[0], pos[1], pos[2] };
}

//--------------------------------------------------------------------------------------------------

/// It is too easy to crash, of the format is freely adjusted by the user

void
format_player_location (std::string& out, const char* format, std::array<float, 3> const& pos)
{
    if (std::isnan (pos[0]))
    {
        out = "(n/a)";
        return;
    }
    out = format;

    std::array<std::string, 3> sp;
    for (int i = 0; i < 3; ++i)
    {
        sp[i].resize (15);
        sp[i].resize (std::snprintf (&sp[i][0], sp[i].size (), "%.0f", pos[i]));
    }

    replace_all (out, "%x", sp[0]);
    replace_all (out, "%y", sp[1]);
    replace_all (out, "%z", sp[2]);
}

//--------------------------------------------------------------------------------------------------

std::string
obtain_current_worldspace ()
{
    if (auto name = worldspace_name.obtain ())
        return name;
    return "";
}

//--------------------------------------------------------------------------------------------------

std::string
obtain_current_cell ()
{
    if (auto name = player_cell.obtain ())
        return name;
    return "";
}

//--------------------------------------------------------------------------------------------------

float
obtain_game_time ()
{
    float* source = game_epoch.obtain ();
    if (!source || !std::isnormal (*source) || *source < 0)
        return std::numeric_limits<float>::quiet_NaN ();
    return *source;
}

//--------------------------------------------------------------------------------------------------

/**
 * Very simple custom formatted time printing for the Skyrim calendar.
 *
 * Preparses some stuff before calling back strftime()
 */

void
format_game_time (std::string& out, const char* format, float source)
{
    if (std::isnan (source))
    {
        out = "(n/a)";
        return;
    }
    out = format;

    // Compute the format input
    float hms = source - int (source);
    int h = int (hms *= 24);
    hms  -= int (hms);
    int m = int (hms *= 60);
    hms  -= int (hms);
    int s = int (hms * 60);

    // Adjusts for starting date: Sun, 17 Jul 201 (considering that the year starts Wed)
    int d = int (source) + 228;
    int y = d / 365 + 201;
    int yd = d % 365 + 1;
    int wd = (d+3) % 7;

    std::array<int, 12> months = { 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 };
    auto mit = std::lower_bound (months.cbegin (), months.cend (), yd);
    int mo = mit - months.cbegin ();
    int md = (mo ? yd-*(mit-1) : yd);

    // Replace years
    auto sy = std::to_string (y);
    auto sY = "4E" + sy;
    replace_all (out, "%y", sy);
    replace_all (out, "%Y", sY);

    // Replace months
    static std::array<std::string, 12> longmon = {
        "Morning Star", "Sun's Dawn", "First Seed", "Rain's Hand", "Second Seed", "Midyear",
        "Sun's Height", "Last Seed", "Hearthfire", "Frostfall", "Sun's Dusk", "Evening Star"
    };
    static std::array<std::string, 12> birtmon = {
        "The Ritual", "The Lover", "The Lord", "The Mage", "The Shadow", "The Steed",
        "The Apprentice", "The Warrior", "The Lady", "The Tower", "The Atronach", "The Thief"
    };
    static std::array<std::string, 12> argomon = {
        "Vakka (Sun)", "Xeech (Nut)", "Sisei (Sprout)", "Hist-Deek (Hist Sapling)",
        "Hist-Dooka (Mature Hist)", "Hist-Tsoko (Elder Hist)", "Thtithil-Gah (Egg-Basket)",
        "Thtithil (Egg)", "Nushmeeko (Lizard)", "Shaja-Nushmeeko (Semi-Humanoid Lizard)",
        "Saxhleel (Argonian)", "Xulomaht (The Deceased)"
    };
    replace_all (out, "%lm", longmon[mo]);
    replace_all (out, "%bm", birtmon[mo]);
    replace_all (out, "%am", argomon[mo]);
    replace_all (out, "%mo", std::to_string (mo+1));
    replace_all (out, "%md", std::to_string (md));

    // Weekdays
    static std::array<std::string, 7> longwday = {
        "Sundas", "Morndas", "Tirdas", "Middas", "Turdas", "Fredas", "Loredas"
    };
    static std::array<std::string, 7> shrtwday = {
        "Sun", "Mor", "Tir", "Mid", "Tur", "Fre", "Lor"
    };
    replace_all (out, "%sd", shrtwday[wd]);
    replace_all (out, "%ld", longwday[wd]);
    replace_all (out, "%wd", std::to_string (wd+1));

    // Time
    replace_all (out, "%h", std::to_string (h));
    replace_all (out, "%m", std::to_string (m));
    replace_all (out, "%s", std::to_string (s));

    // Raw
    replace_all (out, "%ri", std::to_string (d));
    replace_all (out, "%r", std::to_string (source));
}

//--------------------------------------------------------------------------------------------------

bool
setup_variables ()
{
    skyrim_base = reinterpret_cast<std::uintptr_t> (::GetModuleHandle (nullptr));
    if (!sseh.find_target)
        return false;

    sseh.find_target ("GameTime", &game_epoch.offsets[0]);
    sseh.find_target ("GameTime.Offset", &game_epoch.offsets[1]);
    sseh.find_target ("PlayerCharacter", &player_pos.offsets[0]);
    sseh.find_target ("PlayerCharacter.Position", &player_pos.offsets[1]);
    sseh.find_target ("PlayerCharacter.Cell", &player_cell.offsets[1]);
    sseh.find_target ("PlayerCharacter.Worldspace", &worldspace_name.offsets[1]);
    sseh.find_target ("Worldspace.Fullname", &worldspace_name.offsets[2]);
    sseh.find_target ("Cell.Fullname", &player_cell.offsets[2]);
    worldspace_name.offsets[0] = player_pos.offsets[0];
    player_cell.offsets[0] = player_pos.offsets[0];

    return true;
}

//--------------------------------------------------------------------------------------------------

