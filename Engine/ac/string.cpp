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
#include <algorithm>
#include <cstdio>
#include "ac/string.h"
#include "ac/common.h"
#include "ac/display.h"
#include "ac/gamesetupstruct.h"
#include "ac/gamestate.h"
#include "ac/global_translation.h"
#include "ac/runtime_defines.h"
#include "ac/dynobj/scriptstring.h"
#include "font/fonts.h"
#include "debug/debug_log.h"
#include "script/runtimescriptvalue.h"
#include "util/string_compat.h"
#include "util/utf8.h"

using namespace AGS::Common;

extern GameSetupStruct game;
extern GameState play;
extern int longestline;
extern ScriptString myScriptStringImpl;

int String_IsNullOrEmpty(const char *thisString) 
{
    if ((thisString == nullptr) || (thisString[0] == 0))
        return 1;

    return 0;
}

const char* String_Copy(const char *srcString) {
    return CreateNewScriptString(srcString);
}

const char* String_Append(const char *thisString, const char *extrabit) {
    char *buffer = (char*)malloc(strlen(thisString) + strlen(extrabit) + 1);
    strcpy(buffer, thisString);
    strcat(buffer, extrabit);
    return CreateNewScriptString(buffer, false);
}

const char* String_AppendChar(const char *thisString, int extraOne) {
    size_t chw = 1;
    char chr[Utf8::UtfSz + 1]{};
    if (get_uformat() == U_UTF8)
        chw = Utf8::SetChar(extraOne, chr, sizeof(chr));
    else
        chr[0] = extraOne;
    char *buffer = (char*)malloc(strlen(thisString) + chw + 1);
    sprintf(buffer, "%s%s", thisString, chr);
    return CreateNewScriptString(buffer, false);
}

const char* String_ReplaceCharAt(const char *thisString, int index, int newChar) {
    size_t len = ustrlen(thisString);
    if ((index < 0) || ((size_t)index >= len))
        quit("!String.ReplaceCharAt: index outside range of string");

    size_t off = uoffset(thisString, index);
    int uchar = ugetc(thisString + off);
    size_t remain_sz = strlen(thisString + off);
    size_t old_sz = ucwidth(uchar);
    size_t new_chw = 1;
    char new_chr[Utf8::UtfSz + 1]{};
    if (get_uformat() == U_UTF8)
        new_chw = Utf8::SetChar(newChar, new_chr, sizeof(new_chr));
    else
        new_chr[0] = newChar;
    size_t total_sz = off + remain_sz + new_chw - old_sz + 1;
    char *buffer = (char*)malloc(total_sz);
    memcpy(buffer, thisString, off);
    memcpy(buffer + off, new_chr, new_chw);
    memcpy(buffer + off + new_chw, thisString + off + old_sz, remain_sz - old_sz + 1);
    return CreateNewScriptString(buffer, false);
}

const char* String_Truncate(const char *thisString, int length) {
    if (length < 0)
        quit("!String.Truncate: invalid length");
    size_t strlen = ustrlen(thisString);
    if ((size_t)length >= strlen)
        return thisString;

    size_t sz = uoffset(thisString, length);
    char *buffer = (char*)malloc(sz + 1);
    memcpy(buffer, thisString, sz);
    buffer[sz] = 0;
    return CreateNewScriptString(buffer, false);
}

const char* String_Substring(const char *thisString, int index, int length) {
    if (length < 0)
        quit("!String.Substring: invalid length");
    size_t strlen = ustrlen(thisString);
    if ((index < 0) || ((size_t)index > strlen))
        quit("!String.Substring: invalid index");
    size_t sublen = std::min((size_t)length, strlen - index);
    size_t start = uoffset(thisString, index);
    size_t end = uoffset(thisString + start, sublen) + start;
    size_t copysz = end - start;

    char *buffer = (char*)malloc(copysz + 1);
    memcpy(buffer, thisString + start, copysz);
    buffer[copysz] = 0;
    return CreateNewScriptString(buffer, false);
}

int String_CompareTo(const char *thisString, const char *otherString, bool caseSensitive) {

    if (caseSensitive) {
        return strcmp(thisString, otherString);
    }
    else {
        return ustricmp(thisString, otherString);
    }
}

int String_StartsWith(const char *thisString, const char *checkForString, bool caseSensitive) {

    if (caseSensitive) {
        return (strncmp(thisString, checkForString, strlen(checkForString)) == 0) ? 1 : 0;
    }
    else {
        return (ustrnicmp(thisString, checkForString, ustrlen(checkForString)) == 0) ? 1 : 0;
    }
}

int String_EndsWith(const char *thisString, const char *checkForString, bool caseSensitive) {
    // NOTE: we need size in bytes here
    size_t thislen = strlen(thisString), checklen = strlen(checkForString);
    if (checklen > thislen)
        return 0;

    if (caseSensitive) 
    {
        return (strcmp(thisString + (thislen - checklen), checkForString) == 0) ? 1 : 0;
    }
    else 
    {
        return (ustricmp(thisString + (thislen - checklen), checkForString) == 0) ? 1 : 0;
    }
}

const char* String_Replace(const char *thisString, const char *lookForText, const char *replaceWithText, bool caseSensitive)
{
    char resultBuffer[STD_BUFFER_SIZE] = "";
    size_t outputSize = 0; // length in bytes
    if (caseSensitive)
    {
        size_t lookForLen = strlen(lookForText);
        size_t replaceLen = strlen(replaceWithText);
        for (const char *ptr = thisString; *ptr; ++ptr)
        {
            if (strncmp(ptr, lookForText, lookForLen) == 0)
            {
                memcpy(&resultBuffer[outputSize], replaceWithText, replaceLen);
                outputSize += replaceLen;
                ptr += lookForLen - 1;
            }
            else
            {
                resultBuffer[outputSize] = *ptr;
                outputSize++;
            }
        }
    }
    else
    {
        size_t lookForLen = ustrlen(lookForText);
        size_t lookForSz = strlen(lookForText); // length in bytes
        size_t replaceSz = strlen(replaceWithText); // length in bytes
        const char *p_cur = thisString;
        for (int c = ugetxc(&thisString); *p_cur; p_cur = thisString, c = ugetxc(&thisString))
        {
            if (ustrnicmp(p_cur, lookForText, lookForLen) == 0)
            {
                memcpy(&resultBuffer[outputSize], replaceWithText, replaceSz);
                outputSize += replaceSz;
                thisString = p_cur + lookForSz;
            }
            else
            {
                usetc(&resultBuffer[outputSize], c);
                outputSize += ucwidth(c);
            }
        }
    }

    resultBuffer[outputSize] = 0; // terminate
    return CreateNewScriptString(resultBuffer, true);
}

const char* String_LowerCase(const char *thisString) {
    char *buffer = ags_strdup(thisString);
    ustrlwr(buffer);
    return CreateNewScriptString(buffer, false);
}

const char* String_UpperCase(const char *thisString) {
    char *buffer = ags_strdup(thisString);
    ustrupr(buffer);
    return CreateNewScriptString(buffer, false);
}

int String_GetChars(const char *texx, int index) {
    if ((index < 0) || (index >= ustrlen(texx)))
        return 0;
    return ugetat(texx, index);
}

int StringToInt(const char*stino) {
    return atoi(stino);
}

int StrContains (const char *s1, const char *s2) {
    VALIDATE_STRING (s1);
    VALIDATE_STRING (s2);
    char *tempbuf1 = ags_strdup(s1);
    char *tempbuf2 = ags_strdup(s2);
    ustrlwr(tempbuf1);
    ustrlwr(tempbuf2);

    char *offs = ustrstr(tempbuf1, tempbuf2);

    if (offs == nullptr)
    {
        free(tempbuf1);
        free(tempbuf2);
        return -1;
    }

    *offs = 0;
    int at = ustrlen(tempbuf1);
    free(tempbuf1);
    free(tempbuf2);
    return at;
}

//=============================================================================

const char *CreateNewScriptString(const String &fromText) {
    return (const char*)CreateNewScriptStringObj(fromText.GetCStr(), true).second;
}

const char *CreateNewScriptString(const char *fromText, bool reAllocate) {
    return (const char*)CreateNewScriptStringObj(fromText, reAllocate).second;
}

DynObjectRef CreateNewScriptStringObj(const String &fromText) {
    return CreateNewScriptStringObj(fromText.GetCStr(), true);
}

DynObjectRef CreateNewScriptStringObj(const char *fromText, bool reAllocate)
{
    ScriptString *str;
    if (reAllocate) {
        str = new ScriptString(fromText);
    }
    else { // TODO: refactor to avoid const casts!
        str = new ScriptString((char*)fromText, true);
    }
    void *obj_ptr = str->GetTextPtr();
    int32_t handle = ccRegisterManagedObject(obj_ptr, str);
    if (handle == 0)
    {
        delete str;
        return DynObjectRef(0, nullptr);
    }
    return DynObjectRef(handle, obj_ptr);
}

size_t break_up_text_into_lines(const char *todis, SplitLines &lines, int wii, int fonnt, size_t max_lines) {
    if (fonnt == -1)
        fonnt = play.normal_font;

    //  char sofar[100];
    if (todis[0]=='&') {
        while ((todis[0]!=' ') & (todis[0]!=0)) todis++;
        if (todis[0]==' ') todis++;
    }
    lines.Reset();
    longestline=0;

    // Don't attempt to display anything if the width is tiny
    if (wii < 3)
        return 0;

    int line_length;

    split_lines(todis, lines, wii, fonnt, max_lines);

    // Right-to-left just means reverse the text then
    // write it as normal
    if (game.options[OPT_RIGHTLEFTWRITE])
        for (size_t rr = 0; rr < lines.Count(); rr++) {
            (get_uformat() == U_UTF8) ?
                lines[rr].ReverseUTF8() :
                lines[rr].Reverse();
            line_length = get_text_width_outlined(lines[rr].GetCStr(), fonnt);
            if (line_length > longestline)
                longestline = line_length;
        }
    else
        for (size_t rr = 0; rr < lines.Count(); rr++) {
            line_length = get_text_width_outlined(lines[rr].GetCStr(), fonnt);
            if (line_length > longestline)
                longestline = line_length;
        }
    return lines.Count();
}

size_t MAXSTRLEN = MAX_MAXSTRLEN;
void check_strlen(char*ptt) {
    MAXSTRLEN = MAX_MAXSTRLEN;
    long charstart = (long)&game.chars[0];
    long charend = charstart + sizeof(CharacterInfo)*game.numcharacters;
    if (((long)&ptt[0] >= charstart) && ((long)&ptt[0] <= charend))
        MAXSTRLEN=30;
}

/*void GetLanguageString(int indxx,char*buffr) {
VALIDATE_STRING(buffr);
char*bptr=get_language_text(indxx);
if (bptr==NULL) strcpy(buffr,"[language string error]");
else strncpy(buffr,bptr,199);
buffr[199]=0;
}*/

void my_strncpy(char *dest, const char *src, int len) {
    // the normal strncpy pads out the string with zeros up to the
    // max length -- we don't want that
    if (strlen(src) >= (unsigned)len) {
        strncpy(dest, src, len);
        dest[len] = 0;
    }
    else
        strcpy(dest, src);
}

//=============================================================================
//
// Script API Functions
//
//=============================================================================

#include "debug/out.h"
#include "script/script_api.h"
#include "script/script_runtime.h"
#include "ac/math.h"

// int (const char *thisString)
RuntimeScriptValue Sc_String_IsNullOrEmpty(const RuntimeScriptValue *params, int32_t param_count)
{
    API_SCALL_INT_POBJ(String_IsNullOrEmpty, const char);
}

// const char* (const char *thisString, const char *extrabit)
RuntimeScriptValue Sc_String_Append(void *self, const RuntimeScriptValue *params, int32_t param_count)
{
    API_OBJCALL_OBJ_POBJ(const char, const char, myScriptStringImpl, String_Append, const char);
}

// const char* (const char *thisString, char extraOne)
RuntimeScriptValue Sc_String_AppendChar(void *self, const RuntimeScriptValue *params, int32_t param_count)
{
    API_OBJCALL_OBJ_PINT(const char, const char, myScriptStringImpl, String_AppendChar);
}

// int (const char *thisString, const char *otherString, bool caseSensitive)
RuntimeScriptValue Sc_String_CompareTo(void *self, const RuntimeScriptValue *params, int32_t param_count)
{
    API_OBJCALL_INT_POBJ_PBOOL(const char, String_CompareTo, const char);
}

// int  (const char *s1, const char *s2)
RuntimeScriptValue Sc_StrContains(void *self, const RuntimeScriptValue *params, int32_t param_count)
{
    API_OBJCALL_INT_POBJ(const char, StrContains, const char);
}

// const char* (const char *srcString)
RuntimeScriptValue Sc_String_Copy(void *self, const RuntimeScriptValue *params, int32_t param_count)
{
    API_OBJCALL_OBJ(const char, const char, myScriptStringImpl, String_Copy);
}

// int (const char *thisString, const char *checkForString, bool caseSensitive)
RuntimeScriptValue Sc_String_EndsWith(void *self, const RuntimeScriptValue *params, int32_t param_count)
{
    API_OBJCALL_INT_POBJ_PBOOL(const char, String_EndsWith, const char);
}

// const char* (const char *texx, ...)
RuntimeScriptValue Sc_String_Format(const RuntimeScriptValue *params, int32_t param_count)
{
    API_SCALL_SCRIPT_SPRINTF(String_Format, 1);
    return RuntimeScriptValue().SetDynamicObject((void*)CreateNewScriptString(scsf_buffer), &myScriptStringImpl);
}

// const char* (const char *thisString)
RuntimeScriptValue Sc_String_LowerCase(void *self, const RuntimeScriptValue *params, int32_t param_count)
{
    API_OBJCALL_OBJ(const char, const char, myScriptStringImpl, String_LowerCase);
}

// const char* (const char *thisString, const char *lookForText, const char *replaceWithText, bool caseSensitive)
RuntimeScriptValue Sc_String_Replace(void *self, const RuntimeScriptValue *params, int32_t param_count)
{
    API_OBJCALL_OBJ_POBJ2_PBOOL(const char, const char, myScriptStringImpl, String_Replace, const char, const char);
}

// const char* (const char *thisString, int index, char newChar)
RuntimeScriptValue Sc_String_ReplaceCharAt(void *self, const RuntimeScriptValue *params, int32_t param_count)
{
    API_OBJCALL_OBJ_PINT2(const char, const char, myScriptStringImpl, String_ReplaceCharAt);
}

// int (const char *thisString, const char *checkForString, bool caseSensitive)
RuntimeScriptValue Sc_String_StartsWith(void *self, const RuntimeScriptValue *params, int32_t param_count)
{
    API_OBJCALL_INT_POBJ_PBOOL(const char, String_StartsWith, const char);
}

// const char* (const char *thisString, int index, int length)
RuntimeScriptValue Sc_String_Substring(void *self, const RuntimeScriptValue *params, int32_t param_count)
{
    API_OBJCALL_OBJ_PINT2(const char, const char, myScriptStringImpl, String_Substring);
}

// const char* (const char *thisString, int length)
RuntimeScriptValue Sc_String_Truncate(void *self, const RuntimeScriptValue *params, int32_t param_count)
{
    API_OBJCALL_OBJ_PINT(const char, const char, myScriptStringImpl, String_Truncate);
}

// const char* (const char *thisString)
RuntimeScriptValue Sc_String_UpperCase(void *self, const RuntimeScriptValue *params, int32_t param_count)
{
    API_OBJCALL_OBJ(const char, const char, myScriptStringImpl, String_UpperCase);
}

// FLOAT_RETURN_TYPE (const char *theString);
RuntimeScriptValue Sc_StringToFloat(void *self, const RuntimeScriptValue *params, int32_t param_count)
{
    API_OBJCALL_FLOAT(const char, StringToFloat);
}

// int (char*stino)
RuntimeScriptValue Sc_StringToInt(void *self, const RuntimeScriptValue *params, int32_t param_count)
{
    API_OBJCALL_INT(const char, StringToInt);
}

// int (const char *texx, int index)
RuntimeScriptValue Sc_String_GetChars(void *self, const RuntimeScriptValue *params, int32_t param_count)
{
    API_OBJCALL_INT_PINT(const char, String_GetChars);
}

RuntimeScriptValue Sc_strlen(void *self, const RuntimeScriptValue *params, int32_t param_count)
{
    ASSERT_SELF(strlen);
    return RuntimeScriptValue().SetInt32(ustrlen((const char*)self));
}

//=============================================================================
//
// Exclusive API for Plugins
//
//=============================================================================

// const char* (const char *texx, ...)
const char *ScPl_String_Format(const char *texx, ...)
{
    API_PLUGIN_SCRIPT_SPRINTF(texx);
    return CreateNewScriptString(scsf_buffer);
}


void RegisterStringAPI()
{
    ccAddExternalStaticFunction("String::IsNullOrEmpty^1",  Sc_String_IsNullOrEmpty);
    ccAddExternalObjectFunction("String::Append^1",         Sc_String_Append);
    ccAddExternalObjectFunction("String::AppendChar^1",     Sc_String_AppendChar);
    ccAddExternalObjectFunction("String::CompareTo^2",      Sc_String_CompareTo);
    ccAddExternalObjectFunction("String::Contains^1",       Sc_StrContains);
    ccAddExternalObjectFunction("String::Copy^0",           Sc_String_Copy);
    ccAddExternalObjectFunction("String::EndsWith^2",       Sc_String_EndsWith);
    ccAddExternalStaticFunction("String::Format^101",       Sc_String_Format);
    ccAddExternalObjectFunction("String::IndexOf^1",        Sc_StrContains);
    ccAddExternalObjectFunction("String::LowerCase^0",      Sc_String_LowerCase);
    ccAddExternalObjectFunction("String::Replace^3",        Sc_String_Replace);
    ccAddExternalObjectFunction("String::ReplaceCharAt^2",  Sc_String_ReplaceCharAt);
    ccAddExternalObjectFunction("String::StartsWith^2",     Sc_String_StartsWith);
    ccAddExternalObjectFunction("String::Substring^2",      Sc_String_Substring);
    ccAddExternalObjectFunction("String::Truncate^1",       Sc_String_Truncate);
    ccAddExternalObjectFunction("String::UpperCase^0",      Sc_String_UpperCase);
    ccAddExternalObjectFunction("String::get_AsFloat",      Sc_StringToFloat);
    ccAddExternalObjectFunction("String::get_AsInt",        Sc_StringToInt);
    ccAddExternalObjectFunction("String::geti_Chars",       Sc_String_GetChars);
    ccAddExternalObjectFunction("String::get_Length",       Sc_strlen);

    /* ----------------------- Registering unsafe exports for plugins -----------------------*/

    ccAddExternalFunctionForPlugin("String::IsNullOrEmpty^1",  (void*)String_IsNullOrEmpty);
    ccAddExternalFunctionForPlugin("String::Append^1",         (void*)String_Append);
    ccAddExternalFunctionForPlugin("String::AppendChar^1",     (void*)String_AppendChar);
    ccAddExternalFunctionForPlugin("String::CompareTo^2",      (void*)String_CompareTo);
    ccAddExternalFunctionForPlugin("String::Contains^1",       (void*)StrContains);
    ccAddExternalFunctionForPlugin("String::Copy^0",           (void*)String_Copy);
    ccAddExternalFunctionForPlugin("String::EndsWith^2",       (void*)String_EndsWith);
    ccAddExternalFunctionForPlugin("String::Format^101",       (void*)ScPl_String_Format);
    ccAddExternalFunctionForPlugin("String::IndexOf^1",        (void*)StrContains);
    ccAddExternalFunctionForPlugin("String::LowerCase^0",      (void*)String_LowerCase);
    ccAddExternalFunctionForPlugin("String::Replace^3",        (void*)String_Replace);
    ccAddExternalFunctionForPlugin("String::ReplaceCharAt^2",  (void*)String_ReplaceCharAt);
    ccAddExternalFunctionForPlugin("String::StartsWith^2",     (void*)String_StartsWith);
    ccAddExternalFunctionForPlugin("String::Substring^2",      (void*)String_Substring);
    ccAddExternalFunctionForPlugin("String::Truncate^1",       (void*)String_Truncate);
    ccAddExternalFunctionForPlugin("String::UpperCase^0",      (void*)String_UpperCase);
    ccAddExternalFunctionForPlugin("String::get_AsFloat",      (void*)StringToFloat);
    ccAddExternalFunctionForPlugin("String::get_AsInt",        (void*)StringToInt);
    ccAddExternalFunctionForPlugin("String::geti_Chars",       (void*)String_GetChars);
    ccAddExternalFunctionForPlugin("String::get_Length",       (void*)strlen);
}
