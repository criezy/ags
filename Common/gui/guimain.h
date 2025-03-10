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
#ifndef __AC_GUIMAIN_H
#define __AC_GUIMAIN_H

#include <vector>
#include "ac/common_defines.h" // TODO: split out gui drawing helpers
#include "gfx/gfx_def.h" // TODO: split out gui drawing helpers
#include "gui/guidefines.h"
#include "util/error.h"
#include "util/geometry.h"
#include "util/string.h"

// Forward declaration
namespace AGS { namespace Common { class Stream; } }
using namespace AGS; // FIXME later

// There were issues when including header caused conflicts
struct GameSetupStruct;

#define LEGACY_MAX_OBJS_ON_GUI             30

#define GUIMAIN_LEGACY_RESERVED_INTS       5
#define GUIMAIN_LEGACY_NAME_LENGTH         16
#define GUIMAIN_LEGACY_EVENTHANDLER_LENGTH 20
#define GUIMAIN_LEGACY_TW_FLAGS_SIZE       4

namespace AGS
{
namespace Common
{

// Legacy GUIMain visibility state, which combined Visible property and override factor
enum LegacyGUIVisState
{
    kGUIVisibility_LockedOff = -1, // locked hidden (used by PopupMouseY guis)
    kGUIVisibility_Off       =  0, // hidden
    kGUIVisibility_On        =  1  // shown
};

class Bitmap;
class GUIObject;


class GUIMain
{
public:
    static String FixupGUIName(const String &name);

public:
    GUIMain();

    void        InitDefaults();

    // Tells if the gui background supports alpha channel
    bool        HasAlphaChannel() const;
    // Tells if GUI will react on clicking on it
    bool        IsClickable() const;
    // Tells if GUI's visibility is overridden and it won't be displayed on
    // screen regardless of Visible property (until concealed mode is off).
    bool        IsConcealed() const;
    // Tells if gui is actually meant to be displayed on screen.
    // Normally Visible property determines whether GUI is allowed to be seen,
    // but there may be other settings that override it.
    bool        IsDisplayed() const;
    // Tells if given coordinates are within interactable area of gui
    // NOTE: this currently tests for actual visibility and Clickable property
    bool        IsInteractableAt(int x, int y) const;
    // Tells if gui is a text window
    bool        IsTextWindow() const;
    // Tells if GUI is *allowed* to be displayed and interacted with.
    // This does not necessarily mean that it is displayed right now, because
    // GUI may be hidden for other reasons, including overriding behavior.
    // For example GUI with kGUIPopupMouseY style will not be shown unless
    // mouse cursor is at certain position on screen.
    bool        IsVisible() const;

    // Tells if GUI has graphically changed recently
    bool        HasChanged() const;
    bool        HasControlsChanged() const;
    // Manually marks GUI as graphically changed
    // NOTE: this only matters if GUI's own graphic changes (content, size etc),
    // but not its state (visible) or texture drawing mode (transparency, etc).
    void        MarkChanged();
    void        MarkControlsChanged();
    // Clears changed flag
    void        ClearChanged();

    // Finds a control under given screen coordinates, returns control's child ID.
    // Optionally allows extra leeway (offset in all directions) to let the user grab tiny controls.
    // Optionally only allows clickable controls, ignoring non-clickable ones.
    int32_t FindControlAt(int atx, int aty, int leeway = 0, bool must_be_clickable = true) const;
    // Gets the number of the GUI child controls
    int32_t GetControlCount() const;
    // Gets control by its child's index
    GUIObject *GetControl(int index) const;
    // Gets child control's type, looks up with child's index
    GUIControlType GetControlType(int index) const;
    // Gets child control's global ID, looks up with child's index
    int32_t GetControlID(int index) const;
    // Gets an array of child control indexes in the z-order, from bottom to top
    const std::vector<int> &GetControlsDrawOrder() const;

    // Child control management
    // Note that currently GUIMain does not own controls (should not delete them)
    void    AddControl(GUIControlType type, int id, GUIObject *control);
    void    RemoveAllControls();

    // Operations
    bool    BringControlToFront(int index);
    void    DrawSelf(Bitmap *ds);
    void    DrawWithControls(Bitmap *ds);
    // Polls GUI state, providing current cursor (mouse) coordinates
    void    Poll(int mx, int my);
    HError  RebuildArray();
    void    ResortZOrder();
    bool    SendControlToBack(int index);
    // Sets whether GUI should react to player clicking on it
    void    SetClickable(bool on);
    // Override GUI visibility; when in concealed mode GUI won't show up
    // even if Visible = true
    void    SetConceal(bool on);
    // Attempts to change control's zorder; returns if zorder changed
    bool    SetControlZOrder(int index, int zorder);
    // Changes GUI style to the text window or back
    void    SetTextWindow(bool on);
    // Sets GUI transparency as a percentage (0 - 100) where 100 = invisible
    void    SetTransparencyAsPercentage(int percent);
    // Sets whether GUI is allowed to be displayed on screen
    void    SetVisible(bool on);

    // Events
    void    OnMouseButtonDown(int mx, int my);
    void    OnMouseButtonUp();
  
    // Serialization
    void    ReadFromFile(Stream *in, GuiVersion gui_version);
    void    WriteToFile(Stream *out) const;
    // TODO: move to engine, into gui savegame component unit
    // (should read/write GUI properties accessing them by interface)
    void    ReadFromSavegame(Stream *in, GuiSvgVersion svg_version);
    void    WriteToSavegame(Stream *out) const;

private:
    void    DrawBlob(Bitmap *ds, int x, int y, color_t draw_color);
    // Same as FindControlAt but expects local space coordinates
    int32_t FindControlAtLocal(int atx, int aty, int leeway, bool must_be_clickable) const;

    // TODO: all members are currently public; hide them later
public:
    int32_t ID;             // GUI identifier
    String  Name;           // the name of the GUI

    int32_t X;
    int32_t Y;
    int32_t Width;
    int32_t Height;
    color_t BgColor;        // background color
    int32_t BgImage;        // background sprite index
    color_t FgColor;        // foreground color (used as border color in normal GUIs,
                            // and text color in text windows)
    int32_t Padding;        // padding surrounding a GUI text window
    GUIPopupStyle PopupStyle; // GUI popup behavior
    int32_t PopupAtMouseY;  // popup when mousey < this
    int32_t Transparency;   // "incorrect" alpha (in legacy 255-range units)
    int32_t ZOrder;

    int32_t FocusCtrl;      // which control has the focus
    int32_t HighlightCtrl;  // which control has the bounding selection rect
    int32_t MouseOverCtrl;  // which control has the mouse cursor over it
    int32_t MouseDownCtrl;  // which control has the mouse button pressed on it
    Point   MouseWasAt;     // last mouse cursor position

    String  OnClickHandler; // script function name

private:
    int32_t _flags;         // style and behavior flags
    bool    _hasChanged;    // flag tells whether GUI has graphically changed recently
    bool    _hasControlsChanged;

    // Array of types and control indexes in global GUI object arrays;
    // maps GUI child slots to actual controls and used for rebuilding Controls array
    typedef std::pair<GUIControlType, int32_t> ControlRef;
    std::vector<ControlRef> _ctrlRefs;
    // Array of child control references (not exclusively owned!)
    std::vector<GUIObject*> _controls;
    // Sorted array of controls in z-order.
    std::vector<int32_t>    _ctrlDrawOrder;
};


namespace GUI
{
    extern GuiVersion GameGuiVersion;
    extern GuiOptions Options;

    // Calculates the text's graphical position, given the alignment
    Rect CalcTextPosition(const char *text, int font, const Rect &frame, FrameAlignment align);
    // Calculates the text's graphical position, given the horizontal alignment
    Line CalcTextPositionHor(const char *text, int font, int x1, int x2, int y, FrameAlignment align);
    // Draw standart "shading" effect over rectangle
    void DrawDisabledEffect(Bitmap *ds, const Rect &rc);
    // Draw text aligned inside rectangle
    void DrawTextAligned(Bitmap *ds, const char *text, int font, color_t text_color, const Rect &frame, FrameAlignment align);
    // Draw text aligned horizontally inside given bounds
    void DrawTextAlignedHor(Bitmap *ds, const char *text, int font, color_t text_color, int x1, int x2, int y, FrameAlignment align);

    // Mark all existing GUI for redraw
    void MarkAllGUIForUpdate();
    // Mark all translatable GUI controls for redraw
    void MarkForTranslationUpdate();
    // Mark all GUI which use the given font for redraw
    void MarkForFontUpdate(int font);
    // Mark labels that acts as special text placeholders for redraw
    void MarkSpecialLabelsForUpdate(GUILabelMacro macro);
    // Mark inventory windows for redraw, optionally only ones linked to given character
    void MarkInventoryForUpdate(int char_id, bool is_player);

    // Parses the string and returns combination of label macro flags
    GUILabelMacro FindLabelMacros(const String &text);

    // Reads all GUIs and their controls.
    // WARNING: the data is read into the global arrays (guis, guibuts, and so on)
    // TODO: remove is_savegame param after dropping support for old saves
    // because only they use ReadGUI to read runtime GUI data
    HError ReadGUI(Stream *in, bool is_savegame = false);
    // Writes all GUIs and their controls.
    // WARNING: the data is written from the global arrays (guis, guibuts, and so on)
    void WriteGUI(Stream *out);
    // Converts legacy GUIVisibility into appropriate GUIMain properties
    void ApplyLegacyVisibility(GUIMain &gui, LegacyGUIVisState vis);
}

} // namespace Common
} // namespace AGS

extern std::vector<Common::GUIMain> guis;
// Tells if all controls are disabled
// TODO: investigate how this variable works, and if this is at all needed
extern int all_buttons_disabled;
extern int gui_inv_pic;

extern int get_adjusted_spritewidth(int spr);
extern int get_adjusted_spriteheight(int spr);
extern bool is_sprite_alpha(int spr);

// This function has distinct implementations in Engine and Editor
extern void draw_gui_sprite(Common::Bitmap *ds, int spr, int x, int y, bool use_alpha = true,
                            Common::BlendMode blend_mode = Common::kBlendMode_Alpha);
extern void draw_gui_sprite(Common::Bitmap *ds, bool use_alpha, int x, int y,
                            Common::Bitmap *image, bool src_has_alpha,
                            Common::BlendMode blend_mode, int alpha);

extern AGS_INLINE int game_to_data_coord(int coord);
extern AGS_INLINE int data_to_game_coord(int coord);
extern AGS_INLINE void data_to_game_coords(int *x, int *y);
extern AGS_INLINE int get_fixed_pixel_size(int pixels);

// Those function have distinct implementations in Engine and Editor
extern void wouttext_outline(Common::Bitmap *ds, int xxp, int yyp, int usingfont, color_t text_color, const char *texx);

extern void set_our_eip(int eip);
#define SET_EIP(x) set_our_eip(x);
extern void set_eip_guiobj(int eip);
extern int get_eip_guiobj();

#endif // __AC_GUIMAIN_H
