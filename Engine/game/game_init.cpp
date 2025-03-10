//=============================================================================
//
// Adventure Game Studio (AGS)
//
// Copyright (C) 1999-2011 Chris Jones and 2011-20xx others
// The full list of copyright holders can be found in the Copyright.txt
// file, which is part of this source code distribution.
//
// The AGS source code is provided under the Artistic License 2.0.
// A copy of this license can be found in the file License.txt and at
// http://www.opensource.org/licenses/artistic-license-2.0.php
//
//=============================================================================
#include "ac/character.h"
#include "ac/dialog.h"
#include "ac/display.h"
#include "ac/draw.h"
#include "ac/file.h"
#include "ac/game.h"
#include "ac/gamesetup.h"
#include "ac/gamesetupstruct.h"
#include "ac/gamestate.h"
#include "ac/gui.h"
#include "ac/lipsync.h"
#include "ac/movelist.h"
#include "ac/view.h"
#include "ac/dynobj/all_dynamicclasses.h"
#include "ac/dynobj/all_scriptclasses.h"
#include "ac/statobj/agsstaticobject.h"
#include "ac/statobj/staticarray.h"
#include "core/assetmanager.h"
#include "debug/debug_log.h"
#include "debug/out.h"
#include "font/agsfontrenderer.h"
#include "font/fonts.h"
#include "game/game_init.h"
#include "gfx/bitmap.h"
#include "gfx/ddb.h"
#include "gui/guilabel.h"
#include "media/audio/audio_system.h"
#include "platform/base/agsplatformdriver.h"
#include "plugin/plugin_engine.h"
#include "script/cc_common.h"
#include "script/exports.h"
#include "script/script.h"
#include "script/script_runtime.h"
#include "util/string_compat.h"
#include "util/string_utils.h"

using namespace Common;
using namespace Engine;

extern std::vector<ViewStruct> views;

extern CCGUIObject ccDynamicGUIObject;
extern CCCharacter ccDynamicCharacter;
extern CCHotspot   ccDynamicHotspot;
extern CCRegion    ccDynamicRegion;
extern CCInventory ccDynamicInv;
extern CCGUI       ccDynamicGUI;
extern CCObject    ccDynamicObject;
extern CCDialog    ccDynamicDialog;
extern CCAudioChannel ccDynamicAudio;
extern CCAudioClip ccDynamicAudioClip;
extern ScriptString myScriptStringImpl;
extern ScriptObject scrObj[MAX_ROOM_OBJECTS];
extern ScriptGUI    *scrGui;
extern ScriptHotspot scrHotspot[MAX_ROOM_HOTSPOTS];
extern ScriptRegion scrRegion[MAX_ROOM_REGIONS];
extern ScriptInvItem scrInv[MAX_INV];
extern ScriptAudioChannel scrAudioChannel[MAX_GAME_CHANNELS];

extern ScriptDialogOptionsRendering ccDialogOptionsRendering;
extern ScriptDrawingSurface* dialogOptionsRenderingSurface;

extern AGSStaticObject GlobalStaticManager;

extern StaticArray StaticCharacterArray;
extern StaticArray StaticObjectArray;
extern StaticArray StaticGUIArray;
extern StaticArray StaticHotspotArray;
extern StaticArray StaticRegionArray;
extern StaticArray StaticInventoryArray;
extern StaticArray StaticDialogArray;

extern std::vector<ccInstance *> moduleInst;
extern std::vector<ccInstance *> moduleInstFork;
extern std::vector<RuntimeScriptValue> moduleRepExecAddr;

// Old dialog support (defined in ac/dialog)
extern std::vector< std::shared_ptr<unsigned char> > old_dialog_scripts;
extern std::vector<String> old_speech_lines;

// Lipsync
extern SpeechLipSyncLine *splipsync;
extern int numLipLines, curLipLine, curLipLinePhoneme;

StaticArray StaticCharacterArray;
StaticArray StaticObjectArray;
StaticArray StaticGUIArray;
StaticArray StaticHotspotArray;
StaticArray StaticRegionArray;
StaticArray StaticInventoryArray;
StaticArray StaticDialogArray;


namespace AGS
{
namespace Engine
{

String GetGameInitErrorText(GameInitErrorType err)
{
    switch (err)
    {
    case kGameInitErr_NoError:
        return "No error.";
    case kGameInitErr_NoFonts:
        return "No fonts specified to be used in this game.";
    case kGameInitErr_TooManyAudioTypes:
        return "Too many audio types for this engine to handle.";
    case kGameInitErr_EntityInitFail:
        return "Failed to initialize game entities.";
    case kGameInitErr_TooManyPlugins:
        return "Too many plugins for this engine to handle.";
    case kGameInitErr_PluginNameInvalid:
        return "Plugin name is invalid.";
    case kGameInitErr_NoGlobalScript:
        return "No global script in game.";
    case kGameInitErr_ScriptLinkFailed:
        return "Script link failed.";
    }
    return "Unknown error.";
}

// Initializes audio channels and clips and registers them in the script system
void InitAndRegisterAudioObjects(GameSetupStruct &game)
{
    for (int i = 0; i < game.numCompatGameChannels; ++i)
    {
        scrAudioChannel[i].id = i;
        ccRegisterManagedObject(&scrAudioChannel[i], &ccDynamicAudio);
    }

    for (size_t i = 0; i < game.audioClips.size(); ++i)
    {
        // Note that as of 3.5.0 data format the clip IDs are still restricted
        // to actual item index in array, so we don't make any difference
        // between game versions, for now.
        game.audioClips[i].id = i;
        ccRegisterManagedObject(&game.audioClips[i], &ccDynamicAudioClip);
        ccAddExternalDynamicObject(game.audioClips[i].scriptName, &game.audioClips[i], &ccDynamicAudioClip);
    }
}

// Initializes characters and registers them in the script system
void InitAndRegisterCharacters(GameSetupStruct &game)
{
    for (int i = 0; i < game.numcharacters; ++i)
    {
        game.chars[i].walking = 0;
        game.chars[i].animating = 0;
        game.chars[i].pic_xoffs = 0;
        game.chars[i].pic_yoffs = 0;
        game.chars[i].blinkinterval = 140;
        game.chars[i].blinktimer = game.chars[i].blinkinterval;
        game.chars[i].index_id = i;
        game.chars[i].blocking_width = 0;
        game.chars[i].blocking_height = 0;
        game.chars[i].prevroom = -1;
        game.chars[i].loop = 0;
        game.chars[i].frame = 0;
        game.chars[i].walkwait = -1;
        ccRegisterManagedObject(&game.chars[i], &ccDynamicCharacter);

        // export the character's script object
        ccAddExternalDynamicObject(game.chars[i].scrname, &game.chars[i], &ccDynamicCharacter);
    }
}

// Initializes dialog and registers them in the script system
void InitAndRegisterDialogs(GameSetupStruct &game)
{
    scrDialog = new ScriptDialog[game.numdialog];
    for (int i = 0; i < game.numdialog; ++i)
    {
        scrDialog[i].id = i;
        scrDialog[i].reserved = 0;
        ccRegisterManagedObject(&scrDialog[i], &ccDynamicDialog);

        if (!game.dialogScriptNames[i].IsEmpty())
            ccAddExternalDynamicObject(game.dialogScriptNames[i], &scrDialog[i], &ccDynamicDialog);
    }
}

// Initializes dialog options rendering objects and registers them in the script system
void InitAndRegisterDialogOptions()
{
    ccRegisterManagedObject(&ccDialogOptionsRendering, &ccDialogOptionsRendering);

    dialogOptionsRenderingSurface = new ScriptDrawingSurface();
    dialogOptionsRenderingSurface->isLinkedBitmapOnly = true;
    long dorsHandle = ccRegisterManagedObject(dialogOptionsRenderingSurface, dialogOptionsRenderingSurface);
    ccAddObjectReference(dorsHandle);
}

// Initializes gui and registers them in the script system
HError InitAndRegisterGUI(GameSetupStruct &game)
{
    scrGui = (ScriptGUI*)malloc(sizeof(ScriptGUI) * game.numgui);
    for (int i = 0; i < game.numgui; ++i)
    {
        scrGui[i].id = -1;
    }

    for (int i = 0; i < game.numgui; ++i)
    {
        // link controls to their parent guis
        HError err = guis[i].RebuildArray();
        if (!err)
            return err;
        // export all the GUI's controls
        export_gui_controls(i);
        scrGui[i].id = i;
        ccAddExternalDynamicObject(guis[i].Name, &scrGui[i], &ccDynamicGUI);
        ccRegisterManagedObject(&scrGui[i], &ccDynamicGUI);
    }
    return HError::None();
}

// Initializes inventory items and registers them in the script system
void InitAndRegisterInvItems(GameSetupStruct &game)
{
    for (int i = 0; i < MAX_INV; ++i)
    {
        scrInv[i].id = i;
        scrInv[i].reserved = 0;
        ccRegisterManagedObject(&scrInv[i], &ccDynamicInv);

        if (!game.invScriptNames[i].IsEmpty())
            ccAddExternalDynamicObject(game.invScriptNames[i], &scrInv[i], &ccDynamicInv);
    }
}

// Initializes room hotspots and registers them in the script system
void InitAndRegisterHotspots()
{
    for (int i = 0; i < MAX_ROOM_HOTSPOTS; ++i)
    {
        scrHotspot[i].id = i;
        scrHotspot[i].reserved = 0;
        ccRegisterManagedObject(&scrHotspot[i], &ccDynamicHotspot);
    }
}

// Initializes room objects and registers them in the script system
void InitAndRegisterRoomObjects()
{
    for (int i = 0; i < MAX_ROOM_OBJECTS; ++i)
    {
        ccRegisterManagedObject(&scrObj[i], &ccDynamicObject);
    }
}

// Initializes room regions and registers them in the script system
void InitAndRegisterRegions()
{
    for (int i = 0; i < MAX_ROOM_REGIONS; ++i)
    {
        scrRegion[i].id = i;
        scrRegion[i].reserved = 0;
        ccRegisterManagedObject(&scrRegion[i], &ccDynamicRegion);
    }
}

// Registers static entity arrays in the script system
void RegisterStaticArrays(GameSetupStruct &game)
{
    StaticCharacterArray.Create(&ccDynamicCharacter, sizeof(CharacterInfo), sizeof(CharacterInfo));
    StaticObjectArray.Create(&ccDynamicObject, sizeof(ScriptObject), sizeof(ScriptObject));
    StaticGUIArray.Create(&ccDynamicGUI, sizeof(ScriptGUI), sizeof(ScriptGUI));
    StaticHotspotArray.Create(&ccDynamicHotspot, sizeof(ScriptHotspot), sizeof(ScriptHotspot));
    StaticRegionArray.Create(&ccDynamicRegion, sizeof(ScriptRegion), sizeof(ScriptRegion));
    StaticInventoryArray.Create(&ccDynamicInv, sizeof(ScriptInvItem), sizeof(ScriptInvItem));
    StaticDialogArray.Create(&ccDynamicDialog, sizeof(ScriptDialog), sizeof(ScriptDialog));

    ccAddExternalStaticArray("character",&game.chars[0], &StaticCharacterArray);
    ccAddExternalStaticArray("object",&scrObj[0], &StaticObjectArray);
    ccAddExternalStaticArray("gui",&scrGui[0], &StaticGUIArray);
    ccAddExternalStaticArray("hotspot",&scrHotspot[0], &StaticHotspotArray);
    ccAddExternalStaticArray("region",&scrRegion[0], &StaticRegionArray);
    ccAddExternalStaticArray("inventory",&scrInv[0], &StaticInventoryArray);
    ccAddExternalStaticArray("dialog", &scrDialog[0], &StaticDialogArray);
}

// Initializes various game entities and registers them in the script system
HError InitAndRegisterGameEntities(GameSetupStruct &game)
{
    InitAndRegisterAudioObjects(game);
    InitAndRegisterCharacters(game);
    InitAndRegisterDialogs(game);
    InitAndRegisterDialogOptions();
    HError err = InitAndRegisterGUI(game);
    if (!err)
        return err;
    InitAndRegisterInvItems(game);

    InitAndRegisterHotspots();
    InitAndRegisterRegions();
    InitAndRegisterRoomObjects();
    play.CreatePrimaryViewportAndCamera();

    RegisterStaticArrays(game);

    setup_player_character(game.playercharacter);
    if (loaded_game_file_version >= kGameVersion_270)
        ccAddExternalStaticObject("player", &_sc_PlayerCharPtr, &GlobalStaticManager);
    return HError::None();
}

void LoadFonts(GameSetupStruct &game, GameDataVersion data_ver)
{
    for (int i = 0; i < game.numfonts; ++i) 
    {
        FontInfo &finfo = game.fonts[i];
        if (!load_font_size(i, finfo))
            quitprintf("Unable to load font %d, no renderer could load a matching file", i);

        const bool is_wfn = is_bitmap_font(i);
        // Outline thickness corresponds to 1 game pixel by default;
        // but if it's a scaled up bitmap font, then it equals to scale
        if (data_ver < kGameVersion_360)
        {
            if (is_wfn && (finfo.Outline == FONT_OUTLINE_AUTO))
            {
                set_font_outline(i, FONT_OUTLINE_AUTO, FontInfo::kSquared, get_font_scaling_mul(i));
            }
        }
    }

    // Additional fixups - after all the fonts are registered
    for (int i = 0; i < game.numfonts; ++i)
    {
        if (!is_bitmap_font(i))
        {
            // Check for the LucasFan font since it comes with an outline font that
            // is drawn incorrectly with Freetype versions > 2.1.3.
            // A simple workaround is to disable outline fonts for it and use
            // automatic outline drawing.
            const int outline_font = get_font_outline(i);
            if (outline_font < 0) continue;
            const char *name = get_font_name(i);
            const char *outline_name = get_font_name(outline_font);
            if ((ags_stricmp(name, "LucasFan-Font") == 0) && (ags_stricmp(outline_name, "Arcade") == 0))
                set_font_outline(i, FONT_OUTLINE_AUTO);
        }
    }
}

void LoadLipsyncData()
{
    std::unique_ptr<Stream> speechsync( AssetMgr->OpenAsset("syncdata.dat", "voice") );
    if (!speechsync)
        return;
    // this game has voice lip sync
    int lipsync_fmt = speechsync->ReadInt32();
    if (lipsync_fmt != 4)
    {
        Debug::Printf(kDbgMsg_Info, "Unknown speech lip sync format (%d).\nLip sync disabled.", lipsync_fmt);
    }
    else {
        numLipLines = speechsync->ReadInt32();
        splipsync = (SpeechLipSyncLine*)malloc(sizeof(SpeechLipSyncLine) * numLipLines);
        for (int ee = 0; ee < numLipLines; ee++)
        {
            splipsync[ee].numPhonemes = speechsync->ReadInt16();
            speechsync->Read(splipsync[ee].filename, 14);
            splipsync[ee].endtimeoffs = (int*)malloc(splipsync[ee].numPhonemes * sizeof(int));
            speechsync->ReadArrayOfInt32(splipsync[ee].endtimeoffs, splipsync[ee].numPhonemes);
            splipsync[ee].frame = (short*)malloc(splipsync[ee].numPhonemes * sizeof(short));
            speechsync->ReadArrayOfInt16(splipsync[ee].frame, splipsync[ee].numPhonemes);
        }
    }
    Debug::Printf(kDbgMsg_Info, "Lipsync data found and loaded");
}

void AllocScriptModules()
{
    moduleInst.resize(numScriptModules, nullptr);
    moduleInstFork.resize(numScriptModules, nullptr);
    moduleRepExecAddr.resize(numScriptModules);
    repExecAlways.moduleHasFunction.resize(numScriptModules, true);
    lateRepExecAlways.moduleHasFunction.resize(numScriptModules, true);
    getDialogOptionsDimensionsFunc.moduleHasFunction.resize(numScriptModules, true);
    renderDialogOptionsFunc.moduleHasFunction.resize(numScriptModules, true);
    getDialogOptionUnderCursorFunc.moduleHasFunction.resize(numScriptModules, true);
    runDialogOptionMouseClickHandlerFunc.moduleHasFunction.resize(numScriptModules, true);
    runDialogOptionKeyPressHandlerFunc.moduleHasFunction.resize(numScriptModules, true);
    runDialogOptionTextInputHandlerFunc.moduleHasFunction.resize(numScriptModules, true);
    runDialogOptionRepExecFunc.moduleHasFunction.resize(numScriptModules, true);
    runDialogOptionCloseFunc.moduleHasFunction.resize(numScriptModules, true);
    for (auto &val : moduleRepExecAddr)
    {
        val.Invalidate();
    }
}

HGameInitError InitGameState(const LoadedGameEntities &ents, GameDataVersion data_ver)
{
    GameSetupStruct &game = ents.Game;
    const ScriptAPIVersion base_api = (ScriptAPIVersion)game.options[OPT_BASESCRIPTAPI];
    const ScriptAPIVersion compat_api = (ScriptAPIVersion)game.options[OPT_SCRIPTCOMPATLEV];
    if (data_ver >= kGameVersion_341)
    {
        const char *base_api_name = GetScriptAPIName(base_api);
        const char *compat_api_name = GetScriptAPIName(compat_api);
        Debug::Printf(kDbgMsg_Info, "Requested script API: %s (%d), compat level: %s (%d)",
                    base_api >= 0 && base_api <= kScriptAPI_Current ? base_api_name : "unknown", base_api,
                    compat_api >= 0 && compat_api <= kScriptAPI_Current ? compat_api_name : "unknown", compat_api);
    }
    // If the game was compiled using unsupported version of the script API,
    // we warn about potential incompatibilities but proceed further.
    if (game.options[OPT_BASESCRIPTAPI] > kScriptAPI_Current)
        platform->DisplayAlert("Warning: this game requests a higher version of AGS script API, it may not run correctly or run at all.");

    //
    // 1. Check that the loaded data is valid and compatible with the current
    // engine capabilities.
    //
    if (game.numfonts == 0)
        return new GameInitError(kGameInitErr_NoFonts);
    if (game.audioClipTypes.size() > MAX_AUDIO_TYPES)
        return new GameInitError(kGameInitErr_TooManyAudioTypes, String::FromFormat("Required: %zu, max: %zu", game.audioClipTypes.size(), (size_t)MAX_AUDIO_TYPES));

    //
    // 3. Allocate and init game objects
    //
    charextra.resize(game.numcharacters);
    mls.resize(game.numcharacters + MAX_ROOM_OBJECTS + 1);
    init_game_drawdata();
    views = std::move(ents.Views);
    play.charProps.resize(game.numcharacters);
    dialog = std::move(ents.Dialogs);
    old_dialog_scripts = std::move(ents.OldDialogScripts);
    old_speech_lines = std::move(ents.OldSpeechLines);
    // Set number of game channels corresponding to the loaded game version
    if (loaded_game_file_version < kGameVersion_360)
    {
        game.numGameChannels = MAX_GAME_CHANNELS_v320;
        game.numCompatGameChannels = TOTAL_AUDIO_CHANNELS_v320;
    }
    else
    {
        game.numGameChannels = MAX_GAME_CHANNELS;
        game.numCompatGameChannels = MAX_GAME_CHANNELS;
    }
    HError err = InitAndRegisterGameEntities(game);
    if (!err)
        return new GameInitError(kGameInitErr_EntityInitFail, err);
    LoadFonts(game, data_ver);
    LoadLipsyncData();

    //
    // 4. Initialize certain runtime variables
    //
    game_paused = 0;  // reset the game paused flag
    ifacepopped = -1;

    String svg_suffix;
    if (game.saveGameFileExtension[0] != 0)
        svg_suffix.Format(".%s", game.saveGameFileExtension);
    set_save_game_suffix(svg_suffix);

    play.score_sound = game.scoreClipID;
    play.fade_effect = game.options[OPT_FADETYPE];

    //
    // 5. Initialize runtime state of certain game objects
    //
    for (auto &label : guilabels)
    {
        // labels are not clickable by default
        label.SetClickable(false);
    }
    play.gui_draw_order.resize(game.numgui);
    for (int i = 0; i < game.numgui; ++i)
        play.gui_draw_order[i] = i;
    update_gui_zorder();
    calculate_reserved_channel_count();

    //
    // 6. Register engine API exports
    // NOTE: we must do this before plugin start, because some plugins may
    // require access to script API at initialization time.
    //
    ccSetScriptAliveTimer(10u, 1000u);
    ccSetStringClassImpl(&myScriptStringImpl);
    setup_script_exports(base_api, compat_api);

    //
    // 7. Start up plugins
    //
    pl_register_plugins(ents.PluginInfos);
    pl_startup_plugins();

    //
    // 8. Create script modules
    // NOTE: we must do this after plugins, because some plugins may export
    // script symbols too.
    //
    if (!ents.GlobalScript)
        return new GameInitError(kGameInitErr_NoGlobalScript);
    gamescript = ents.GlobalScript;
    dialogScriptsScript = ents.DialogScript;
    numScriptModules = ents.ScriptModules.size();
    scriptModules = ents.ScriptModules;
    AllocScriptModules();
    if (create_global_script())
        return new GameInitError(kGameInitErr_ScriptLinkFailed, cc_get_error().ErrorString);

    return HGameInitError::None();
}

} // namespace Engine
} // namespace AGS
