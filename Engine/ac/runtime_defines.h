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
#ifndef __AC_RUNTIMEDEFINES_H
#define __AC_RUNTIMEDEFINES_H

#include "ac/common_defines.h"

// xalleg.h pulls in an Allegro-internal definition of MAX_TIMERS which
// conflicts with the definition in runtime_defines.h. Forget it.
#ifdef MAX_TIMERS
#undef MAX_TIMERS
#endif

// Max old-style script string length
#define MAX_MAXSTRLEN 200
#define MAXGLOBALVARS 50

#define INVALID_X  30000
#define MAXGSVALUES 500
#define MAXGLOBALSTRINGS 51
#define MAX_INVORDER 500
#define DIALOG_NONE      0
#define DIALOG_RUNNING   1
#define DIALOG_STOP      2
#define DIALOG_NEWROOM   100
#define DIALOG_NEWTOPIC  12000
#define MAX_TIMERS       21
#define MAX_PARSED_WORDS 15
// how many saves may be listed at once
#define MAXSAVEGAMES     50
// topmost save index to be listed with a FillSaveGameList command
// NOTE: changing this may theoretically affect older games which
// use slots > 99 for special purposes!
#define TOP_LISTEDSAVESLOT 99
#define MAX_QUEUED_MUSIC 10
#define GLED_INTERACTION 1
#define GLED_EFFECTS     2 
#define QUEUED_MUSIC_REPEAT 10000
#define PLAYMP3FILE_MAX_FILENAME_LEN 50
#define MAX_AUDIO_TYPES  30

// Legacy (pre 3.5.0) alignment types used in the script API
enum LegacyScriptAlignment
{
    kLegacyScAlignLeft      = 1,
    kLegacyScAlignCentre    = 2,
    kLegacyScAlignRight     = 3
};

const int LegacyMusicMasterVolumeAdjustment = 60;
const int LegacyRoomVolumeFactor            = 30;

// These numbers were chosen arbitrarily -- the idea is
// to make sure that the user gets the parameters the right way round
#define ANYWHERE       304
#define WALKABLE_AREAS 305
#define BLOCKING       919
#define IN_BACKGROUND  920
#define FORWARDS       1062
#define BACKWARDS      1063
#define STOP_MOVING    1
#define KEEP_MOVING    0

#define SCR_NO_VALUE   31998
#define SCR_COLOR_TRANSPARENT -1



#define NUM_DIGI_VOICES     16
#define NUM_MOD_DIGI_VOICES 12

#define DEBUG_CONSOLE_NUMLINES 6
#define TXT_SCOREBAR        29
#define MAXSCORE play.totalscore
#define CHANIM_REPEAT    2
#define CHANIM_BACKWARDS 4
#define ANIM_BACKWARDS 10
// Animates once and stops at the *last* frame
#define ANIM_ONCE      1
// Animates infinitely until stopped by command
#define ANIM_REPEAT    2
// Animates once and stops, resetting to the very first frame
#define ANIM_ONCERESET 3
#define FONT_STATUSBAR  0
#define FONT_NORMAL     play.normal_font
//#define FONT_SPEECHBACK 1
#define FONT_SPEECH     play.speech_font
#define MODE_WALK 0
#define MODE_LOOK 1
#define MODE_HAND 2
#define MODE_TALK 3
#define MODE_USE  4
#define MODE_PICKUP 5
#define CURS_ARROW  6
#define CURS_WAIT   7
#define MODE_CUSTOM1 8
#define MODE_CUSTOM2 9

#define OVER_TEXTMSG  1
#define OVER_COMPLETE 2
#define OVER_PICTURE  3
#define OVER_TEXTSPEECH 4
#define OVER_CUSTOM   100
#define OVR_AUTOPLACE 30000
#define FOR_ANIMATION 1
#define FOR_SCRIPT    2
#define FOR_EXITLOOP  3
// an actsps index offset for characters
#define ACTSP_OBJSOFF (MAX_ROOM_OBJECTS)
// a 1-based movelist index offset for characters
#define CHMLSOFFS (1 + MAX_ROOM_OBJECTS)
#define MAX_SCRIPT_AT_ONCE 10
#define EVENT_NONE       0
#define EVENT_INPROGRESS 1
#define EVENT_CLAIMED    2

// Internal skip style flags, for speech/display, wait
#define SKIP_NONE       0x00
#define SKIP_AUTOTIMER  0x01
#define SKIP_KEYPRESS   0x02
#define SKIP_MOUSECLICK 0x04

#define MANOBJNUM 99

#define STD_BUFFER_SIZE 3000

#define TURNING_AROUND     1000
#define TURNING_BACKWARDS 10000

#define MAX_PLUGIN_OBJECT_READERS 50

#define LOCTYPE_HOTSPOT 1
#define LOCTYPE_CHAR 2
#define LOCTYPE_OBJ  3

#define MAX_DYNAMIC_SURFACES 20

#define RESTART_POINT_SAVE_GAME_NUMBER 999

#define MAX_OPEN_SCRIPT_FILES 10

#define RETURN_CONTINUE 1

#endif // __AC_RUNTIMEDEFINES_H
