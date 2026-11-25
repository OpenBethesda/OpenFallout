#ifndef OPENFALLOUT_COMPONENTS_FALLOUTSCRIPT_EXESETTINGS_H
#define OPENFALLOUT_COMPONENTS_FALLOUTSCRIPT_EXESETTINGS_H

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace ToUTF8
{
    class StatelessUtf8Encoder;
}

namespace FalloutScript::ExeSettings
{
    /// The default texts of the string settings, by the name of the setting in lower case. The plugins of the games
    /// hold only the settings that were changed from their defaults, and the defaults (every word of the screens, the
    /// texts of the terminals and of the game of hacking, the counters of the level-up screen) are in the executables,
    /// which build each setting with a static initialiser. Nothing of them is kept in the port: they are read from the
    /// player's own copy of the game.
    using Texts = std::map<std::string, std::string>;

    /// The texts in the image of a Windows executable (the file as it is on disk). Two layouts of the initialisers are
    /// known, and both are looked for: code that pushes the address of the default text and the address of the name
    /// and calls the constructor of the setting (`68 <text> 68 <name> B9 <object> E8` or `... 8B C8+n E8`), and a
    /// table of records of three addresses (the text, the name and the table of functions of the setting; Fallout3.exe
    /// builds most of its settings that way). The text is converted to UTF-8 with `encoder` (the code page that the
    /// player set for the data of the game: a localised executable has its words in the code page of its language),
    /// and from Windows-1252 when there is none. An image that is not one of the games' executables (too short,
    /// damaged, not a Windows executable) gives no texts.
    Texts scan(std::string_view image, const ToUTF8::StatelessUtf8Encoder* encoder = nullptr);

    /// The default values of the numeric settings (`f`, `i`, `u` and `b` before a capital: floats, integers, unsigned
    /// integers and booleans), by the name of the setting in lower case. The plugins hold only the settings that were
    /// changed, and the defaults of the others (fPlayerDeathReloadTime, iActivatePickLength, the knockdown and knockback
    /// settings...) are in the executables, built the same two ways as the texts:
    ///  - code that loads the value and pushes the name before the constructor: `D9 05 <float>` / `DD 05 <double>` /
    ///    `D9 EE` (0) / `D9 E8` (1), then `D9 1C 24` (with a `51` before or after the load), or `6A <8 bits>` or
    ///    `68 <32 bits>`, then `68 <name>` and `B9 <object> E8` or `8B C8+n E8` (FalloutNV.exe);
    ///  - a record of three 32-bit words in the data: the value, the address of the name, the address of the table of
    ///    functions of the setting (Fallout3.exe).
    /// The values in the code win over those of a record. The settings of the INI file (`fCreatureRadioMax:Audio`) are
    /// by their name without the section, under a setting of the game of the same name. Nothing of them is kept in the
    /// port: they are read from the player's own copy of the game.
    using Numbers = std::map<std::string, double>;

    /// The default texts and numbers of the settings of one executable
    struct Defaults
    {
        Texts mTexts;
        Numbers mNumbers;
    };

    /// The texts and numbers in the image of a Windows executable; none for an image that is not one of the games'
    /// executables
    Defaults scanDefaults(std::string_view image, const ToUTF8::StatelessUtf8Encoder* encoder = nullptr);

    /// The texts of the executable in `path`; none if it cannot be read.
    Texts read(const std::filesystem::path& path, const ToUTF8::StatelessUtf8Encoder* encoder = nullptr);

    /// The texts and numbers of the executable in `path`; none if it cannot be read.
    Defaults readDefaults(const std::filesystem::path& path, const ToUTF8::StatelessUtf8Encoder* encoder = nullptr);

    /// The file called `fileName` (not case sensitive) in one of the directories or in the directory above it: the
    /// executable of a game is next to its `Data` directory. An empty path if there is none.
    std::filesystem::path find(const std::vector<std::filesystem::path>& directories, std::string_view fileName);
}

#endif
