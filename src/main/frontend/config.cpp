/***************************************************************************
    XML Configuration File Handling.

    Load Settings.
    Load & Save Hi-Scores.

    Copyright Chris White.
    See license.txt for more details.
***************************************************************************/

#include <cstdlib>
#include <cstdio>
#include <iostream>

#ifndef __DREAMCAST__
// see: http://www.boost.org/doc/libs/1_52_0/doc/html/boost_propertytree/tutorial.html
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
// Boost string prediction
#include <boost/algorithm/string/predicate.hpp>
#include <boost/version.hpp>
#endif

#ifdef __DREAMCAST__
#include <kos/dbglog.h>
#endif

#include "main.hpp"
#include "config.hpp"
#include "globals.hpp"
#include "../utils.hpp"

#include "engine/ohiscore.hpp"
#include "engine/outils.hpp"
#include "engine/audio/osoundint.hpp"

#ifndef __DREAMCAST__
// api change in boost 1.56
#if (BOOST_VERSION >= 105600)
typedef boost::property_tree::xml_writer_settings<std::string> xml_writer_settings;
#else
typedef boost::property_tree::xml_writer_settings<char> xml_writer_settings;
#endif
#endif

Config config;

#ifdef __DREAMCAST__
#define DC_TRACE(...) dbglog(DBG_INFO, __VA_ARGS__)
#else
#define DC_TRACE(...) do {} while (0)
#endif

#ifdef __DREAMCAST__
namespace
{
    bool dc_read_file(const std::string& filename, std::string& out)
    {
        FILE* file = fopen(filename.c_str(), "rb");
        if (!file)
            return false;

        fseek(file, 0, SEEK_END);
        const long size = ftell(file);
        fseek(file, 0, SEEK_SET);
        if (size <= 0)
        {
            fclose(file);
            return false;
        }

        out.resize(static_cast<size_t>(size));
        const size_t read = fread(&out[0], 1, out.size(), file);
        fclose(file);
        out.resize(read);
        return read > 0;
    }

    bool dc_find_node(const std::string& xml, size_t begin, size_t end,
                      const std::string& tag, size_t& content_begin,
                      size_t& content_end, size_t& open_begin, size_t& open_end)
    {
        std::string open = "<" + tag;
        std::string close = "</" + tag + ">";
        open_begin = xml.find(open, begin);
        while (open_begin != std::string::npos && open_begin < end)
        {
            const char after = open_begin + open.size() < xml.size() ? xml[open_begin + open.size()] : '\0';
            if (after == '>' || after == ' ' || after == '\t' || after == '\r' || after == '\n')
                break;
            open_begin = xml.find(open, open_begin + 1);
        }
        if (open_begin == std::string::npos || open_begin >= end)
            return false;

        open_end = xml.find('>', open_begin);
        if (open_end == std::string::npos || open_end >= end)
            return false;

        content_begin = open_end + 1;
        content_end = xml.find(close, content_begin);
        return content_end != std::string::npos && content_end <= end;
    }

    bool dc_find_path(const std::string& xml, const char* path,
                      size_t& content_begin, size_t& content_end,
                      size_t& open_begin, size_t& open_end)
    {
        size_t begin = 0;
        size_t end = xml.size();
        std::string current;

        for (const char* p = path;; ++p)
        {
            if (*p == '.' || *p == '\0')
            {
                if (!dc_find_node(xml, begin, end, current, content_begin, content_end, open_begin, open_end))
                    return false;
                begin = content_begin;
                end = content_end;
                current.clear();
                if (*p == '\0')
                    return true;
            }
            else
            {
                current += *p;
            }
        }
    }

    std::string dc_trim(std::string value)
    {
        const char* whitespace = " \t\r\n";
        const size_t first = value.find_first_not_of(whitespace);
        if (first == std::string::npos)
            return "";
        const size_t last = value.find_last_not_of(whitespace);
        return value.substr(first, last - first + 1);
    }

    std::string dc_xml_string(const std::string& xml, const char* path, const char* default_value)
    {
        size_t content_begin, content_end, open_begin, open_end;
        if (!dc_find_path(xml, path, content_begin, content_end, open_begin, open_end))
            return default_value;
        return dc_trim(xml.substr(content_begin, content_end - content_begin));
    }

    int dc_xml_int(const std::string& xml, const char* path, int default_value)
    {
        std::string value = dc_xml_string(xml, path, "");
        return value.empty() ? default_value : std::atoi(value.c_str());
    }

    float dc_xml_float(const std::string& xml, const char* path, float default_value)
    {
        std::string value = dc_xml_string(xml, path, "");
        return value.empty() ? default_value : static_cast<float>(std::atof(value.c_str()));
    }

    bool dc_replace_prefix(std::string& value, const char* from, const char* to)
    {
        const std::string from_prefix(from);
        if (value.compare(0, from_prefix.size(), from_prefix) != 0)
            return false;
        value.replace(0, from_prefix.size(), to);
        return true;
    }

    int dc_xml_attr_int(const std::string& xml, const char* path, const char* attr, int default_value)
    {
        size_t content_begin, content_end, open_begin, open_end;
        if (!dc_find_path(xml, path, content_begin, content_end, open_begin, open_end))
            return default_value;

        std::string open_tag = xml.substr(open_begin, open_end - open_begin + 1);
        std::string key = std::string(attr) + "=\"";
        size_t value_begin = open_tag.find(key);
        if (value_begin == std::string::npos)
            return default_value;
        value_begin += key.size();
        const size_t value_end = open_tag.find('"', value_begin);
        if (value_end == std::string::npos)
            return default_value;
        return std::atoi(open_tag.substr(value_begin, value_end - value_begin).c_str());
    }
}
#endif

Config::Config(void)
{
    data.cfg_file = "/cd/config.xml";
    
    // Setup default sounds
    music_t magical, breeze, splash;
    magical.title = "MAGICAL SOUND SHOWER";
    breeze.title  = "PASSING BREEZE";
    splash.title  = "SPLASH WAVE";
    magical.type  = music_t::IS_YM_INT;
    breeze.type   = music_t::IS_YM_INT;
    splash.type   = music_t::IS_YM_INT;
    magical.cmd   = sound::MUSIC_MAGICAL;
    breeze.cmd    = sound::MUSIC_BREEZE;
    splash.cmd    = sound::MUSIC_SPLASH;
    sound.music.push_back(magical);
    sound.music.push_back(breeze);
    sound.music.push_back(splash);
}


Config::~Config(void)
{
}


// Set Path to load and save config to
void Config::set_config_file(const std::string& file)
{
    data.cfg_file = file;
}

#ifndef __DREAMCAST__
using boost::property_tree::ptree;
ptree pt_config;
#endif

void Config::load()
{
    DC_TRACE("cannonball: Config::load cfg=%s\n", data.cfg_file.c_str());
#ifdef __DREAMCAST__
    std::string dc_xml;
    bool dc_loaded_from_pc = false;
    if (!dc_read_file(data.cfg_file, dc_xml))
    {
        DC_TRACE("cannonball: Config::load failed to read %s\n", data.cfg_file.c_str());
        if (data.cfg_file == "/cd/config.xml" && dc_read_file("/pc/config.xml", dc_xml))
        {
            data.cfg_file = "/pc/config.xml";
            dc_loaded_from_pc = true;
            DC_TRACE("cannonball: Config::load using /pc/config.xml fallback\n");
        }
        else
        {
            DC_TRACE("cannonball: Config::load using defaults\n");
        }
    }
    else
    {
        DC_TRACE("cannonball: Config::load read config bytes=%u\n", static_cast<unsigned>(dc_xml.size()));
    }

    data.rom_path         = dc_xml_string(dc_xml, "data.rompath", "/cd/roms/");
    data.res_path         = dc_xml_string(dc_xml, "data.respath", "/cd/res/");
    data.save_path        = dc_xml_string(dc_xml, "data.savepath", "/cd/");
    data.crc32            = dc_xml_int(dc_xml, "data.crc32", 1);
    data.crc32            = 0;

    if (dc_loaded_from_pc)
    {
        dc_replace_prefix(data.rom_path, "/cd/", "/pc/");
        dc_replace_prefix(data.res_path, "/cd/", "/pc/");
        dc_replace_prefix(data.save_path, "/cd/", "/pc/");
    }

    data.file_scores      = data.save_path + "hiscores.xml";
    data.file_scores_jap  = data.save_path + "hiscores_jap.xml";
    data.file_ttrial      = data.save_path + "hiscores_timetrial.xml";
    data.file_ttrial_jap  = data.save_path + "hiscores_timetrial_jap.xml";
    data.file_cont        = data.save_path + "hiscores_continuous.xml";
    data.file_cont_jap    = data.save_path + "hiscores_continuous_jap.xml";

    menu.enabled           = dc_xml_int(dc_xml, "menu.enabled", 1);
    menu.road_scroll_speed = dc_xml_int(dc_xml, "menu.roadspeed", 50);

    video.mode       = dc_xml_int(dc_xml, "video.mode", 2);
    video.scale      = dc_xml_int(dc_xml, "video.window.scale", 1);
    video.scanlines  = dc_xml_int(dc_xml, "video.scanlines", 0);
    video.fps        = dc_xml_int(dc_xml, "video.fps", 2);
    video.fps_count  = dc_xml_int(dc_xml, "video.fps_counter", 0);
    video.widescreen = dc_xml_int(dc_xml, "video.widescreen", 1);
    video.hires      = dc_xml_int(dc_xml, "video.hires", 0);
    video.filtering  = dc_xml_int(dc_xml, "video.filtering", 0);
    video.vsync      = dc_xml_int(dc_xml, "video.vsync", 1);
    video.shadow     = dc_xml_int(dc_xml, "video.shadow", 0);

    sound.enabled     = dc_xml_int(dc_xml, "sound.enable", 1);
    sound.rate        = dc_xml_int(dc_xml, "sound.rate", 44100);
    sound.advertise   = dc_xml_int(dc_xml, "sound.advertise", 1);
    sound.preview     = dc_xml_int(dc_xml, "sound.preview", 1);
    sound.fix_samples = dc_xml_int(dc_xml, "sound.fix_samples", 1);
    sound.music_timer = dc_xml_int(dc_xml, "sound.music_timer", 0);
    if (sound.rate > 8000)
        sound.rate = 8000;

    if (!sound.music_timer)
        sound.music_timer = MUSIC_TIMER;
    else
    {
        if (sound.music_timer > 99)
            sound.music_timer = 99;
        sound.music_timer = outils::DEC_TO_HEX[sound.music_timer];
    }

    smartypi.enabled = dc_xml_attr_int(dc_xml, "smartypi", "enabled", 0);
    smartypi.ouputs  = dc_xml_int(dc_xml, "smartypi.outputs", 1);
    smartypi.cabinet = dc_xml_int(dc_xml, "smartypi.cabinet", 1);

    controls.gear          = dc_xml_int(dc_xml, "controls.gear", 0);
    controls.steer_speed   = dc_xml_int(dc_xml, "controls.steerspeed", 3);
    controls.pedal_speed   = dc_xml_int(dc_xml, "controls.pedalspeed", 4);
    controls.rumble        = dc_xml_float(dc_xml, "controls.rumble", 1.0f);
#ifdef __DREAMCAST__
    controls.rumble        = 0.0f;
#endif
    controls.keyconfig[0]  = dc_xml_int(dc_xml, "controls.keyconfig.up",      273);
    controls.keyconfig[1]  = dc_xml_int(dc_xml, "controls.keyconfig.down",    274);
    controls.keyconfig[2]  = dc_xml_int(dc_xml, "controls.keyconfig.left",    276);
    controls.keyconfig[3]  = dc_xml_int(dc_xml, "controls.keyconfig.right",   275);
    controls.keyconfig[4]  = dc_xml_int(dc_xml, "controls.keyconfig.acc",     122);
    controls.keyconfig[5]  = dc_xml_int(dc_xml, "controls.keyconfig.brake",   120);
    controls.keyconfig[6]  = dc_xml_int(dc_xml, "controls.keyconfig.gear1",   32);
    controls.keyconfig[7]  = dc_xml_int(dc_xml, "controls.keyconfig.gear2",   32);
    controls.keyconfig[8]  = dc_xml_int(dc_xml, "controls.keyconfig.start",   49);
    controls.keyconfig[9]  = dc_xml_int(dc_xml, "controls.keyconfig.coin",    53);
    controls.keyconfig[10] = dc_xml_int(dc_xml, "controls.keyconfig.menu",    286);
    controls.keyconfig[11] = dc_xml_int(dc_xml, "controls.keyconfig.view",    304);
    controls.padconfig[0]  = dc_xml_int(dc_xml, "controls.padconfig.acc",     -1);
    controls.padconfig[1]  = dc_xml_int(dc_xml, "controls.padconfig.brake",   -1);
    controls.padconfig[2]  = dc_xml_int(dc_xml, "controls.padconfig.gear1",   -1);
    controls.padconfig[3]  = dc_xml_int(dc_xml, "controls.padconfig.gear2",   -1);
    controls.padconfig[4]  = dc_xml_int(dc_xml, "controls.padconfig.start",   -1);
    controls.padconfig[5]  = dc_xml_int(dc_xml, "controls.padconfig.coin",    -1);
    controls.padconfig[6]  = dc_xml_int(dc_xml, "controls.padconfig.menu",    -1);
    controls.padconfig[7]  = dc_xml_int(dc_xml, "controls.padconfig.view",    -1);
    controls.padconfig[8]  = dc_xml_int(dc_xml, "controls.padconfig.up",      -1);
    controls.padconfig[9]  = dc_xml_int(dc_xml, "controls.padconfig.down",    -1);
    controls.padconfig[10] = dc_xml_int(dc_xml, "controls.padconfig.left",    -1);
    controls.padconfig[11] = dc_xml_int(dc_xml, "controls.padconfig.right",   -1);
    controls.padconfig[12] = dc_xml_int(dc_xml, "controls.padconfig.limit_l", -1);
    controls.padconfig[13] = dc_xml_int(dc_xml, "controls.padconfig.limit_c", -1);
    controls.padconfig[14] = dc_xml_int(dc_xml, "controls.padconfig.limit_r", -1);
    controls.analog        = dc_xml_attr_int(dc_xml, "controls.analog", "enabled", 1);
    controls.pad_id        = dc_xml_int(dc_xml, "controls.pad_id", 0);
    controls.axis[0]       = dc_xml_int(dc_xml, "controls.analog.axis.wheel", -1);
    controls.axis[1]       = dc_xml_int(dc_xml, "controls.analog.axis.accel", -1);
    controls.axis[2]       = dc_xml_int(dc_xml, "controls.analog.axis.brake", -1);
    controls.axis[3]       = dc_xml_int(dc_xml, "controls.analog.axis.motor", -1);
    controls.invert[1]     = dc_xml_attr_int(dc_xml, "controls.analog.axis.accel", "invert", 0);
    controls.invert[2]     = dc_xml_attr_int(dc_xml, "controls.analog.axis.brake", "invert", 0);
    controls.asettings[0]  = dc_xml_int(dc_xml, "controls.analog.wheel.zone", 75);
    controls.asettings[1]  = dc_xml_int(dc_xml, "controls.analog.wheel.dead", 0);
    controls.haptic        = dc_xml_attr_int(dc_xml, "controls.analog.haptic", "enabled", 0);
    controls.max_force     = dc_xml_int(dc_xml, "controls.analog.haptic.max_force", 9000);
    controls.min_force     = dc_xml_int(dc_xml, "controls.analog.haptic.min_force", 8500);
    controls.force_duration= dc_xml_int(dc_xml, "controls.analog.haptic.force_duration", 20);

    engine.dip_time      = dc_xml_int(dc_xml, "engine.time",    0);
    engine.dip_traffic   = dc_xml_int(dc_xml, "engine.traffic", 1);
    engine.freeze_timer    = engine.dip_time == 4;
    engine.disable_traffic = engine.dip_traffic == 4;
    engine.dip_time    &= 3;
    engine.dip_traffic &= 3;

    engine.freeplay      = dc_xml_int(dc_xml, "engine.freeplay",        0) != 0;
    engine.jap           = dc_xml_int(dc_xml, "engine.japanese_tracks", 0);
    engine.prototype     = dc_xml_int(dc_xml, "engine.prototype",       0);
    engine.level_objects   = dc_xml_int(dc_xml, "engine.levelobjects", 1);
    engine.randomgen       = dc_xml_int(dc_xml, "engine.randomgen",    1);
    engine.fix_bugs_backup =
    engine.fix_bugs        = dc_xml_int(dc_xml, "engine.fix_bugs",     1) != 0;
    engine.fix_timer       = dc_xml_int(dc_xml, "engine.fix_timer",    0) != 0;
    engine.layout_debug    = dc_xml_int(dc_xml, "engine.layout_debug", 0) != 0;
    engine.hiscore_delete  = dc_xml_int(dc_xml, "scores.delete_last_entry", 1);
    engine.hiscore_timer   = dc_xml_int(dc_xml, "scores.hiscore_timer", 0);
    engine.new_attract     = dc_xml_int(dc_xml, "engine.new_attract", 1) != 0;
    engine.offroad         = dc_xml_int(dc_xml, "engine.offroad", 0);
    engine.grippy_tyres    = dc_xml_int(dc_xml, "engine.grippy_tyres", 0);
    engine.bumper          = dc_xml_int(dc_xml, "engine.bumper", 0);
    engine.turbo           = dc_xml_int(dc_xml, "engine.turbo", 0);
    engine.car_pal         = dc_xml_int(dc_xml, "engine.car_color", 0);

    if (!engine.hiscore_timer)
        engine.hiscore_timer = HIGHSCORE_TIMER;
    else
    {
        if (engine.hiscore_timer > 99)
            engine.hiscore_timer = 99;
        engine.hiscore_timer = outils::DEC_TO_HEX[engine.hiscore_timer];
    }

    ttrial.laps    = dc_xml_int(dc_xml, "time_trial.laps",    5);
    ttrial.traffic = dc_xml_int(dc_xml, "time_trial.traffic", 3);
    cont_traffic   = dc_xml_int(dc_xml, "continuous.traffic", 3);
    DC_TRACE("cannonball: Config::load dreamcast parser done\n");
    return;
#else
    // Load XML file and put its contents in property tree. 
    // No namespace qualification is needed, because of Koenig 
    // lookup on the second argument. If reading fails, exception
    // is thrown.
    try
    {
        DC_TRACE("cannonball: Config::load read_xml begin\n");
        read_xml(data.cfg_file, pt_config, boost::property_tree::xml_parser::trim_whitespace);
        DC_TRACE("cannonball: Config::load read_xml done\n");
    }
    catch (std::exception &e)
    {
        DC_TRACE("cannonball: Config::load read_xml exception: %s\n", e.what());
        std::cout << "Error: " << e.what() << "\n";
    }

    // ------------------------------------------------------------------------
    // Data Settings
    // ------------------------------------------------------------------------
    DC_TRACE("cannonball: Config::load data get begin\n");
    data.rom_path         = pt_config.get("data.rompath", "/cd/roms/");  // Path to ROMs
    data.res_path         = pt_config.get("data.respath", "/cd/res/");   // Path to ROMs
    data.save_path        = pt_config.get("data.savepath", "/cd/");    // Path to Save Data
    data.crc32            = pt_config.get("data.crc32", 1);
    DC_TRACE("cannonball: Config::load data get done\n");

    data.file_scores      = data.save_path + "hiscores.xml";
    data.file_scores_jap  = data.save_path + "hiscores_jap.xml";
    data.file_ttrial      = data.save_path + "hiscores_timetrial.xml";
    data.file_ttrial_jap  = data.save_path + "hiscores_timetrial_jap.xml";
    data.file_cont        = data.save_path + "hiscores_continuous.xml";
    data.file_cont_jap    = data.save_path + "hiscores_continuous_jap.xml";

    // ------------------------------------------------------------------------
    // Menu Settings
    // ------------------------------------------------------------------------

    menu.enabled           = pt_config.get("menu.enabled",   1);
    menu.road_scroll_speed = pt_config.get("menu.roadspeed", 50);

    // ------------------------------------------------------------------------
    // Video Settings
    // ------------------------------------------------------------------------
   
    video.mode       = pt_config.get("video.mode",               2); // Video Mode: Default is Full Screen 
    video.scale      = pt_config.get("video.window.scale",       1); // Video Scale: Default is 2x    
    video.scanlines  = pt_config.get("video.scanlines",          0); // Scanlines
    video.fps        = pt_config.get("video.fps",                2); // Default is 60 fps
    video.fps_count  = pt_config.get("video.fps_counter",        0); // FPS Counter
    video.widescreen = pt_config.get("video.widescreen",         1); // Enable Widescreen Mode
    video.hires      = pt_config.get("video.hires",              0); // Hi-Resolution Mode
    video.filtering  = pt_config.get("video.filtering",          0); // Open GL Filtering Mode
    video.vsync      = pt_config.get("video.vsync",              1); // Use V-Sync where available (e.g. Open GL)
    video.shadow     = pt_config.get("video.shadow",             0); // Shadow Settings

    // ------------------------------------------------------------------------
    // Sound Settings
    // ------------------------------------------------------------------------
    sound.enabled     = pt_config.get("sound.enable",      1);
    sound.rate        = pt_config.get("sound.rate",        44100);
    sound.advertise   = pt_config.get("sound.advertise",   1);
    sound.preview     = pt_config.get("sound.preview",     1);
    sound.fix_samples = pt_config.get("sound.fix_samples", 1);
    sound.music_timer = pt_config.get("sound.music_timer", 0);

    // Custom Music. Search for enabled custom tracks
    for (int i = 0;; i++)
    {
        std::string xmltag = "sound.custom_music.track" + Utils::to_string(i + 1);
        boost::optional<int> tag = pt_config.get_optional<int>(xmltag  + ".<xmlattr>.enabled");
        if (!tag.is_initialized()) break;
        if (tag.value() == 1)
        {
            music_t music;
            music.filename = pt_config.get(xmltag + ".filename", "track"+Utils::to_string(i+1)+".wav");
            music.title    = pt_config.get(xmltag + ".title", "TRACK " +Utils::to_string(i+1));
            std::transform(music.title.begin(), music.title.end(), music.title.begin(), ::toupper); // Convert title to uppercase
            music.type     = boost::ends_with(music.filename, ".wav") ? music_t::IS_WAV : music_t::IS_YM_EXT;
            music.cmd      = sound::MUSIC_CUSTOM;
            sound.music.push_back(music);
        }
    }

    if (!sound.music_timer)
        sound.music_timer = MUSIC_TIMER;
    else
    {
        if (sound.music_timer > 99)
            sound.music_timer = 99;
        sound.music_timer = outils::DEC_TO_HEX[sound.music_timer]; // convert to hexadecimal
    }

    // ------------------------------------------------------------------------
    // SMARTYPI Settings
    // ------------------------------------------------------------------------
    smartypi.enabled = pt_config.get("smartypi.<xmlattr>.enabled", 0);
    smartypi.ouputs  = pt_config.get("smartypi.outputs", 1);
    smartypi.cabinet = pt_config.get("smartypi.cabinet", 1);

    // ------------------------------------------------------------------------
    // Controls
    // ------------------------------------------------------------------------
    controls.gear          = pt_config.get("controls.gear", 0);
    controls.steer_speed   = pt_config.get("controls.steerspeed", 3);
    controls.pedal_speed   = pt_config.get("controls.pedalspeed", 4);
    controls.rumble        = pt_config.get("controls.rumble", 1.0f);
    controls.keyconfig[0]  = pt_config.get("controls.keyconfig.up",      273);
    controls.keyconfig[1]  = pt_config.get("controls.keyconfig.down",    274);
    controls.keyconfig[2]  = pt_config.get("controls.keyconfig.left",    276);
    controls.keyconfig[3]  = pt_config.get("controls.keyconfig.right",   275);
    controls.keyconfig[4]  = pt_config.get("controls.keyconfig.acc",     122);
    controls.keyconfig[5]  = pt_config.get("controls.keyconfig.brake",   120);
    controls.keyconfig[6]  = pt_config.get("controls.keyconfig.gear1",   32);
    controls.keyconfig[7]  = pt_config.get("controls.keyconfig.gear2",   32);
    controls.keyconfig[8]  = pt_config.get("controls.keyconfig.start",   49);
    controls.keyconfig[9]  = pt_config.get("controls.keyconfig.coin",    53);
    controls.keyconfig[10] = pt_config.get("controls.keyconfig.menu",    286);
    controls.keyconfig[11] = pt_config.get("controls.keyconfig.view",    304);
    controls.padconfig[0]  = pt_config.get("controls.padconfig.acc",     -1);
    controls.padconfig[1]  = pt_config.get("controls.padconfig.brake",   -1);
    controls.padconfig[2]  = pt_config.get("controls.padconfig.gear1",   -1);
    controls.padconfig[3]  = pt_config.get("controls.padconfig.gear2",   -1);
    controls.padconfig[4]  = pt_config.get("controls.padconfig.start",   -1);
    controls.padconfig[5]  = pt_config.get("controls.padconfig.coin",    -1);
    controls.padconfig[6]  = pt_config.get("controls.padconfig.menu",    -1);
    controls.padconfig[7]  = pt_config.get("controls.padconfig.view",    -1);
    controls.padconfig[8]  = pt_config.get("controls.padconfig.up",      -1);
    controls.padconfig[9]  = pt_config.get("controls.padconfig.down",    -1);
    controls.padconfig[10] = pt_config.get("controls.padconfig.left",    -1);
    controls.padconfig[11] = pt_config.get("controls.padconfig.right",   -1);
    controls.padconfig[12] = pt_config.get("controls.padconfig.limit_l", -1);
    controls.padconfig[13] = pt_config.get("controls.padconfig.limit_c", -1);
    controls.padconfig[14] = pt_config.get("controls.padconfig.limit_r", -1);
    controls.analog        = pt_config.get("controls.analog.<xmlattr>.enabled", 1);
    controls.pad_id        = pt_config.get("controls.pad_id", 0);
    controls.axis[0]       = pt_config.get("controls.analog.axis.wheel", -1);
    controls.axis[1]       = pt_config.get("controls.analog.axis.accel", -1);
    controls.axis[2]       = pt_config.get("controls.analog.axis.brake", -1);
    controls.axis[3]       = pt_config.get("controls.analog.axis.motor", -1);
    controls.invert[1]     = pt_config.get("controls.analog.axis.accel.<xmlattr>.invert", 0);
    controls.invert[2]     = pt_config.get("controls.analog.axis.brake.<xmlattr>.invert", 0);
    controls.asettings[0]  = pt_config.get("controls.analog.wheel.zone", 75);
    controls.asettings[1]  = pt_config.get("controls.analog.wheel.dead", 0);
    
    controls.haptic        = pt_config.get("controls.analog.haptic.<xmlattr>.enabled", 0);
    controls.max_force     = pt_config.get("controls.analog.haptic.max_force", 9000);
    controls.min_force     = pt_config.get("controls.analog.haptic.min_force", 8500);
    controls.force_duration= pt_config.get("controls.analog.haptic.force_duration", 20);

    // ------------------------------------------------------------------------
    // Engine Settings
    // ------------------------------------------------------------------------

    engine.dip_time      = pt_config.get("engine.time",    0);
    engine.dip_traffic   = pt_config.get("engine.traffic", 1);
    
    engine.freeze_timer    = engine.dip_time == 4;
    engine.disable_traffic = engine.dip_traffic == 4;
    engine.dip_time    &= 3;
    engine.dip_traffic &= 3;

    engine.freeplay      = pt_config.get("engine.freeplay",        0) != 0;
    engine.jap           = pt_config.get("engine.japanese_tracks", 0);
    engine.prototype     = pt_config.get("engine.prototype",       0);
    
    // Additional Level Objects
    engine.level_objects   = pt_config.get("engine.levelobjects", 1);
    engine.randomgen       = pt_config.get("engine.randomgen",    1);
    engine.fix_bugs_backup = 
    engine.fix_bugs        = pt_config.get("engine.fix_bugs",     1) != 0;
    engine.fix_timer       = pt_config.get("engine.fix_timer",    0) != 0;
    engine.layout_debug    = pt_config.get("engine.layout_debug", 0) != 0;
    engine.hiscore_delete  = pt_config.get("scores.delete_last_entry", 1);
    engine.hiscore_timer   = pt_config.get("scores.hiscore_timer", 0);
    engine.new_attract     = pt_config.get("engine.new_attract", 1) != 0;
    engine.offroad         = pt_config.get("engine.offroad", 0);
    engine.grippy_tyres    = pt_config.get("engine.grippy_tyres", 0);
    engine.bumper          = pt_config.get("engine.bumper", 0);
    engine.turbo           = pt_config.get("engine.turbo", 0);
    engine.car_pal         = pt_config.get("engine.car_color", 0);

    if (!engine.hiscore_timer)
        engine.hiscore_timer = HIGHSCORE_TIMER;
    else
    {
        if (engine.hiscore_timer > 99)
            engine.hiscore_timer = 99;
        engine.hiscore_timer = outils::DEC_TO_HEX[engine.hiscore_timer]; // convert to hexadecimal
    }

    // ------------------------------------------------------------------------
    // Time Trial Mode
    // ------------------------------------------------------------------------

    ttrial.laps    = pt_config.get("time_trial.laps",    5);
    ttrial.traffic = pt_config.get("time_trial.traffic", 3);

    cont_traffic   = pt_config.get("continuous.traffic", 3);
#endif
}

bool Config::save()
{
#ifdef __DREAMCAST__
    DC_TRACE("cannonball: Config::save skipped\n");
    return true;
#else
    // Save stuff
    pt_config.put("video.mode",               video.mode);
    pt_config.put("video.window.scale",       video.scale);
    pt_config.put("video.scanlines",          video.scanlines);
    pt_config.put("video.fps",                video.fps);
    pt_config.put("video.widescreen",         video.widescreen);
    pt_config.put("video.hires",              video.hires);

    pt_config.put("sound.enable",             sound.enabled);
    pt_config.put("sound.advertise",          sound.advertise);
    pt_config.put("sound.preview",            sound.preview);
    pt_config.put("sound.fix_samples",        sound.fix_samples);

    if (config.smartypi.enabled)
        pt_config.put("smartypi.cabinet",     config.smartypi.cabinet);

    pt_config.put("controls.gear",            controls.gear);
    pt_config.put("controls.rumble",          controls.rumble);
    pt_config.put("controls.steerspeed",      controls.steer_speed);
    pt_config.put("controls.pedalspeed",      controls.pedal_speed);
    pt_config.put("controls.keyconfig.up",    controls.keyconfig[0]);
    pt_config.put("controls.keyconfig.down",  controls.keyconfig[1]);
    pt_config.put("controls.keyconfig.left",  controls.keyconfig[2]);
    pt_config.put("controls.keyconfig.right", controls.keyconfig[3]);
    pt_config.put("controls.keyconfig.acc",   controls.keyconfig[4]);
    pt_config.put("controls.keyconfig.brake", controls.keyconfig[5]);
    pt_config.put("controls.keyconfig.gear1", controls.keyconfig[6]);
    pt_config.put("controls.keyconfig.gear2", controls.keyconfig[7]);
    pt_config.put("controls.keyconfig.start", controls.keyconfig[8]);
    pt_config.put("controls.keyconfig.coin",  controls.keyconfig[9]);
    pt_config.put("controls.keyconfig.menu",  controls.keyconfig[10]);
    pt_config.put("controls.keyconfig.view",  controls.keyconfig[11]);
    pt_config.put("controls.padconfig.acc",   controls.padconfig[0]);
    pt_config.put("controls.padconfig.brake", controls.padconfig[1]);
    pt_config.put("controls.padconfig.gear1", controls.padconfig[2]);
    pt_config.put("controls.padconfig.gear2", controls.padconfig[3]);
    pt_config.put("controls.padconfig.start", controls.padconfig[4]);
    pt_config.put("controls.padconfig.coin",  controls.padconfig[5]);
    pt_config.put("controls.padconfig.menu",  controls.padconfig[6]);
    pt_config.put("controls.padconfig.view",  controls.padconfig[7]);
    pt_config.put("controls.padconfig.up",    controls.padconfig[8]);
    pt_config.put("controls.padconfig.down",  controls.padconfig[9]);
    pt_config.put("controls.padconfig.left",  controls.padconfig[10]);
    pt_config.put("controls.padconfig.right", controls.padconfig[11]);
    pt_config.put("controls.analog.<xmlattr>.enabled", controls.analog);
    pt_config.put("controls.analog.axis.wheel", controls.axis[0]);
    pt_config.put("controls.analog.axis.accel", controls.axis[1]);
    pt_config.put("controls.analog.axis.brake", controls.axis[2]);

    pt_config.put("engine.freeplay",        (int) engine.freeplay);
    pt_config.put("engine.time",            engine.freeze_timer ? 4 : engine.dip_time);
    pt_config.put("engine.traffic",         engine.disable_traffic ? 4 : engine.dip_traffic);
    pt_config.put("engine.japanese_tracks", engine.jap);
    pt_config.put("engine.prototype",       engine.prototype);
    pt_config.put("engine.levelobjects",    engine.level_objects);
    pt_config.put("engine.fix_bugs",        (int) engine.fix_bugs);
    pt_config.put("engine.fix_timer",       (int) engine.fix_timer);
    pt_config.put("engine.new_attract",     engine.new_attract);
    pt_config.put("engine.offroad",         (int) engine.offroad);
    pt_config.put("engine.grippy_tyres",    (int) engine.grippy_tyres);
    pt_config.put("engine.bumper",          (int) engine.bumper);
    pt_config.put("engine.turbo",           (int) engine.turbo);
    pt_config.put("engine.car_color",       engine.car_pal);

    pt_config.put("time_trial.laps",    ttrial.laps);
    pt_config.put("time_trial.traffic", ttrial.traffic);
    pt_config.put("continuous.traffic", cont_traffic), 

    ttrial.laps    = pt_config.get("time_trial.laps",    5);
    ttrial.traffic = pt_config.get("time_trial.traffic", 3);
    cont_traffic   = pt_config.get("continuous.traffic", 3);

    try
    {
        write_xml(data.cfg_file, pt_config, std::locale(), xml_writer_settings('\t', 1)); // Tab space 1
    }
    catch (std::exception &e)
    {
        std::cout << e.what() << std::endl;
        return false;
    }
    return true;
#endif
}

void Config::load_scores(bool original_mode)
{
    std::string filename;

    if (original_mode)
        filename = engine.jap ? data.file_scores_jap : data.file_scores;
    else
        filename = engine.jap ? data.file_cont_jap : data.file_cont;

#ifdef __DREAMCAST__
    DC_TRACE("cannonball: Config::load_scores skipped filename=%s\n", filename.c_str());
    return;
#else
    // Create empty property tree object
    ptree pt;

    try
    {
        read_xml(filename , pt, boost::property_tree::xml_parser::trim_whitespace);
    }
    catch (std::exception &e)
    {
        std::cout << e.what() << std::endl;
        return;
    }
    
    // Game Scores
    for (int i = 0; i < ohiscore.NO_SCORES; i++)
    {
        score_entry* e = &ohiscore.scores[i];
        
        std::string xmltag = "score";
        xmltag += Utils::to_string(i);  
    
        e->score    = Utils::from_hex_string(pt.get<std::string>(xmltag + ".score",    "0"));
        e->initial1 = pt.get(xmltag + ".initial1", ".")[0];
        e->initial2 = pt.get(xmltag + ".initial2", ".")[0];
        e->initial3 = pt.get(xmltag + ".initial3", ".")[0];
        e->maptiles = Utils::from_hex_string(pt.get<std::string>(xmltag + ".maptiles", "20202020"));
        e->time     = Utils::from_hex_string(pt.get<std::string>(xmltag + ".time"    , "0")); 

        if (e->initial1 == '.') e->initial1 = 0x20;
        if (e->initial2 == '.') e->initial2 = 0x20;
        if (e->initial3 == '.') e->initial3 = 0x20;
    }
#endif
}

void Config::save_scores(bool original_mode)
{
    std::string filename;

    if (original_mode)
        filename = engine.jap ? data.file_scores_jap : data.file_scores;
    else
        filename = engine.jap ? data.file_cont_jap : data.file_cont;

#ifdef __DREAMCAST__
    DC_TRACE("cannonball: Config::save_scores skipped filename=%s\n", filename.c_str());
    return;
#else
    // Create empty property tree object
    ptree pt;
        
    for (int i = 0; i < ohiscore.NO_SCORES; i++)
    {
        score_entry* e = &ohiscore.scores[i];
    
        std::string xmltag = "score";
        xmltag += Utils::to_string(i);    
        
        pt.put(xmltag + ".score",    Utils::to_hex_string(e->score));
        pt.put(xmltag + ".initial1", e->initial1 == 0x20 ? "." : Utils::to_string((char) e->initial1)); // use . to represent space
        pt.put(xmltag + ".initial2", e->initial2 == 0x20 ? "." : Utils::to_string((char) e->initial2));
        pt.put(xmltag + ".initial3", e->initial3 == 0x20 ? "." : Utils::to_string((char) e->initial3));
        pt.put(xmltag + ".maptiles", Utils::to_hex_string(e->maptiles));
        pt.put(xmltag + ".time",     Utils::to_hex_string(e->time));
    }
    
    try
    {
        write_xml(filename, pt, std::locale(), xml_writer_settings('\t', 1)); // Tab space 1
    }
    catch (std::exception &e)
    {
        std::cout << "Error saving hiscores: " << e.what() << "\n";
    }
#endif
}

void Config::load_tiletrial_scores()
{
    // Counter value that represents 1m 15s 0ms
    static const uint16_t COUNTER_1M_15 = 0x11D0;

#ifdef __DREAMCAST__
    for (int i = 0; i < 15; i++)
        ttrial.best_times[i] = COUNTER_1M_15;

    DC_TRACE("cannonball: Config::load_tiletrial_scores skipped\n");
    return;
#else
    // Create empty property tree object
    ptree pt;

    try
    {
        read_xml(engine.jap ? config.data.file_ttrial_jap : config.data.file_ttrial, pt, boost::property_tree::xml_parser::trim_whitespace);
    }
    catch (std::exception &e)
    {
        for (int i = 0; i < 15; i++)
            ttrial.best_times[i] = COUNTER_1M_15;

        std::cout << e.what();
        return;
    }

    // Time Trial Scores
    for (int i = 0; i < 15; i++)
    {
        ttrial.best_times[i] = pt.get("time_trial.score" + Utils::to_string(i), COUNTER_1M_15);
    }
#endif
}

void Config::save_tiletrial_scores()
{
#ifdef __DREAMCAST__
    DC_TRACE("cannonball: Config::save_tiletrial_scores skipped\n");
    return;
#else
    // Create empty property tree object
    ptree pt;

    // Time Trial Scores
    for (int i = 0; i < 15; i++)
    {
        pt.put("time_trial.score" + Utils::to_string(i), ttrial.best_times[i]);
    }

    try
    {
        write_xml(engine.jap ? config.data.file_ttrial_jap : config.data.file_ttrial, pt, std::locale(), xml_writer_settings('\t', 1)); // Tab space 1
    }
    catch (std::exception &e)
    {
        std::cout << "Error saving hiscores: " << e.what() << "\n";
    }
#endif
}

bool Config::clear_scores()
{
    // Init Default Hiscores
    ohiscore.init_def_scores();

    int clear = 0;

    // Remove XML files if they exist
    clear += remove(data.file_scores.c_str());
    clear += remove(data.file_scores_jap.c_str());
    clear += remove(data.file_ttrial.c_str());
    clear += remove(data.file_ttrial_jap.c_str());
    clear += remove(data.file_cont.c_str());
    clear += remove(data.file_cont_jap.c_str());

    // remove returns 0 on success
    return clear == 6;
}

void Config::set_fps(int fps)
{
    video.fps = fps;
    // Set core FPS to 30fps or 60fps
    this->fps = video.fps == 0 ? 30 : 60;
    
    // Original game ticks sprites at 30fps but background scroll at 60fps
    tick_fps  = video.fps < 2 ? 30 : 60;

    cannonball::frame_ms = 1000.0 / this->fps;

    if (config.sound.enabled)
        cannonball::audio.stop_audio();
    osoundint.init();
    if (config.sound.enabled)
        cannonball::audio.start_audio();
}

// Inc time setting from menu
void Config::inc_time()
{
    if (engine.dip_time == 3)
    {
        if (!engine.freeze_timer)
            engine.freeze_timer = 1;
        else
        {
            engine.dip_time = 0;
            engine.freeze_timer = 0;
        }
    }
    else
        engine.dip_time++;
}

// Inc traffic setting from menu
void Config::inc_traffic()
{
    if (engine.dip_traffic == 3)
    {
        if (!engine.disable_traffic)
            engine.disable_traffic = 1;
        else
        {
            engine.dip_traffic = 0;
            engine.disable_traffic = 0;
        }
    }
    else
        engine.dip_traffic++;
}
