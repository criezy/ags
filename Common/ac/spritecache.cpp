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
#include "ac/spritecache.h"
#include "ac/gamestructdefines.h"
#include "debug/out.h"
#include "gfx/bitmap.h"

using namespace AGS::Common;

// [IKM] We have to forward-declare these because their implementations are in the Engine
extern void initialize_sprite(int);
extern void pre_save_sprite(Bitmap *image);
extern void get_new_size_for_sprite(int, int, int, int &, int &);

#define START_OF_LIST -1
#define END_OF_LIST   -1


SpriteInfo::SpriteInfo()
    : Flags(0)
    , Width(0)
    , Height(0)
{
}


namespace AGS
{
namespace Common
{

SpriteCache::SpriteCache(std::vector<SpriteInfo> &sprInfos)
    : _sprInfos(sprInfos)
    , _maxCacheSize(DEFAULTCACHESIZE_KB * 1024u)
    , _cacheSize(0u)
    , _lockedSize(0u)
{
}

SpriteCache::~SpriteCache()
{
    Reset();
}

size_t SpriteCache::GetCacheSize() const
{
    return _cacheSize;
}

size_t SpriteCache::GetLockedSize() const
{
    return _lockedSize;
}

size_t SpriteCache::GetMaxCacheSize() const
{
    return _maxCacheSize;
}

size_t SpriteCache::GetSpriteSlotCount() const
{
    return _spriteData.size();
}

void SpriteCache::SetMaxCacheSize(size_t size)
{
    _maxCacheSize = size;
    FreeMem(0); // makes sure it does not exceed max size
}

void SpriteCache::Reset()
{
    _file.Close();
    // TODO: find out if it's safe to simply always delete _spriteData.Image with array element
    for (size_t i = 0; i < _spriteData.size(); ++i)
    {
        if (_spriteData[i].Image)
        {
            delete _spriteData[i].Image;
            _spriteData[i].Image = nullptr;
        }
    }
    _spriteData.clear();
    _mru.clear();
    _cacheSize = 0;
    _lockedSize = 0;
}

void SpriteCache::SetSprite(sprkey_t index, Bitmap *sprite)
{
    if (index < 0 || EnlargeTo(index) != index)
    {
        Debug::Printf(kDbgGroup_SprCache, kDbgMsg_Error, "SetSprite: unable to use index %d", index);
        return;
    }
    if (!sprite)
    {
        Debug::Printf(kDbgGroup_SprCache, kDbgMsg_Error, "SetSprite: attempt to assign nullptr to index %d", index);
        return;
    }
    _spriteData[index].Image = sprite;
    _spriteData[index].Flags = SPRCACHEFLAG_LOCKED; // NOT from asset file
    _spriteData[index].Size = 0;
#ifdef DEBUG_SPRITECACHE
    Debug::Printf(kDbgGroup_SprCache, kDbgMsg_Debug, "SetSprite: (external) %d", index);
#endif
}

void SpriteCache::SetEmptySprite(sprkey_t index, bool as_asset)
{
    if (index < 0 || EnlargeTo(index) != index)
    {
        Debug::Printf(kDbgGroup_SprCache, kDbgMsg_Error, "SetEmptySprite: unable to use index %d", index);
        return;
    }
    if (as_asset)
        _spriteData[index].Flags = SPRCACHEFLAG_ISASSET;
    RemapSpriteToSprite0(index);
}

void SpriteCache::SubstituteBitmap(sprkey_t index, Bitmap *sprite)
{
    if (!DoesSpriteExist(index))
    {
        Debug::Printf(kDbgGroup_SprCache, kDbgMsg_Error, "SubstituteBitmap: attempt to set for non-existing sprite %d", index);
        return;
    }
    _spriteData[index].Image = sprite;
#ifdef DEBUG_SPRITECACHE
    Debug::Printf(kDbgGroup_SprCache, kDbgMsg_Debug, "SubstituteBitmap: %d", index);
#endif
}

void SpriteCache::RemoveSprite(sprkey_t index, bool freeMemory)
{
    if (freeMemory)
        delete _spriteData[index].Image;
    InitNullSpriteParams(index);
#ifdef DEBUG_SPRITECACHE
    Debug::Printf(kDbgGroup_SprCache, kDbgMsg_Debug, "RemoveSprite: %d", index);
#endif
}

sprkey_t SpriteCache::EnlargeTo(sprkey_t topmost)
{
    if (topmost < 0 || topmost > MAX_SPRITE_INDEX)
        return -1;
    if ((size_t)topmost < _spriteData.size())
        return topmost;

    size_t newsize = topmost + 1;
    _sprInfos.resize(newsize);
    _spriteData.resize(newsize);
    return topmost;
}

sprkey_t SpriteCache::GetFreeIndex()
{
    for (size_t i = MIN_SPRITE_INDEX; i < _spriteData.size(); ++i)
    {
        // slot empty
        if (!DoesSpriteExist(i))
        {
            _sprInfos[i] = SpriteInfo();
            _spriteData[i] = SpriteData();
            return i;
        }
    }
    // enlarge the sprite bank to find a free slot and return the first new free slot
    return EnlargeTo(_spriteData.size());
}

bool SpriteCache::SpriteData::DoesSpriteExist() const
{
    return (Image != nullptr) || // HAS loaded bitmap
        ((Flags & SPRCACHEFLAG_ISASSET) != 0); // OR found in the game resources
}

bool SpriteCache::SpriteData::IsAssetSprite() const
{
    return (Flags & SPRCACHEFLAG_ISASSET) != 0; // found in game resources
}

bool SpriteCache::SpriteData::IsExternalSprite() const
{
    return (Image != nullptr) &&  // HAS loaded bitmap
        ((Flags & SPRCACHEFLAG_ISASSET) == 0) && // AND NOT found in game resources
        ((Flags & SPRCACHEFLAG_REMAPPED) == 0); // AND was NOT remapped to another sprite
}

bool SpriteCache::SpriteData::IsLocked() const
{
    return (Flags & SPRCACHEFLAG_LOCKED) != 0;
}

bool SpriteCache::DoesSpriteExist(sprkey_t index) const
{
    return index >= 0 && (size_t)index < _spriteData.size() && _spriteData[index].DoesSpriteExist();
}

Bitmap *SpriteCache::operator [] (sprkey_t index)
{
    // invalid sprite slot
    if (index < 0 || (size_t)index >= _spriteData.size())
        return nullptr;

    // Externally added sprite or locked sprite, don't put it into MRU list
    if (_spriteData[index].IsExternalSprite() || _spriteData[index].IsLocked())
        return _spriteData[index].Image;

    if (_spriteData[index].Image)
    {
        // Move to the beginning of the MRU list
        _mru.splice(_mru.begin(), _mru, _spriteData[index].MruIt);
    }
    else
    {
        // Sprite exists in file but is not in mem, load it
        LoadSprite(index);
        _spriteData[index].MruIt = _mru.insert(_mru.begin(), index);
    }
    return _spriteData[index].Image;
}

void SpriteCache::FreeMem(size_t space)
{
    for (int tries = 0; (_mru.size() > 0) && (_cacheSize >= (_maxCacheSize - space)); ++tries)
    {
        DisposeOldest();
        if (tries > 1000) // ???
        {
            Debug::Printf(kDbgGroup_SprCache, kDbgMsg_Error, "RUNTIME CACHE ERROR: STUCK IN FREE_UP_MEM; RESETTING CACHE");
            DisposeAll();
        }
    }
}

void SpriteCache::DisposeOldest()
{
    assert(_mru.size() > 0);
    if (_mru.size() == 0) return;
    auto it = std::prev(_mru.end());
    const auto sprnum = *it;
    // Safety check: must be a sprite from resources
    assert(_spriteData[sprnum].IsAssetSprite());
    if (!_spriteData[sprnum].IsAssetSprite())
    {
        Debug::Printf(kDbgGroup_SprCache, kDbgMsg_Error,
            "SpriteCache::DisposeOldest: in MRU list sprite %d is external or does not exist", sprnum);
        _mru.erase(it);
        return;
    }
    // Delete the image, unless is locked
    // NOTE: locked sprites may still occur in MRU list
    if (!_spriteData[sprnum].IsLocked())
    {
        _cacheSize -= _spriteData[sprnum].Size;
        delete _spriteData[*it].Image;
        _spriteData[sprnum].Image = nullptr;
#ifdef DEBUG_SPRITECACHE
        Debug::Printf(kDbgGroup_SprCache, kDbgMsg_Debug, "DisposeOldest: disposed %d, size now %d KB", sprnum, _cacheSize / 1024);
#endif
    }
    // Remove from the mru list
    _mru.erase(it);
}

void SpriteCache::DisposeAll()
{
    for (size_t i = 0; i < _spriteData.size(); ++i)
    {
        if (!_spriteData[i].IsLocked() && // not locked
            _spriteData[i].IsAssetSprite()) // sprite from game resource
        {
            delete _spriteData[i].Image;
            _spriteData[i].Image = nullptr;
        }
    }
    _cacheSize = _lockedSize;
    _mru.clear();
}

void SpriteCache::Precache(sprkey_t index)
{
    if (index < 0 || (size_t)index >= _spriteData.size())
        return;
    if (!_spriteData[index].IsAssetSprite())
        return; // cannot precache a non-asset sprite

    soff_t sprSize = 0;

    if (_spriteData[index].Image == nullptr)
    {
        sprSize = LoadSprite(index);
    }
    else if (!_spriteData[index].IsLocked())
    {
        sprSize = _spriteData[index].Size;
        // Remove locked sprite from the MRU list
        _mru.erase(_spriteData[index].MruIt);
    }

    // make sure locked sprites can't fill the cache
    _maxCacheSize += sprSize;
    _lockedSize += sprSize;
    _spriteData[index].Flags |= SPRCACHEFLAG_LOCKED;
#ifdef DEBUG_SPRITECACHE
    Debug::Printf(kDbgGroup_SprCache, kDbgMsg_Debug, "Precached %d", index);
#endif
}

sprkey_t SpriteCache::GetDataIndex(sprkey_t index)
{
    return (_spriteData[index].Flags & SPRCACHEFLAG_REMAPPED) == 0 ? index : 0;
}

size_t SpriteCache::LoadSprite(sprkey_t index)
{
    assert((index >= 0) && ((size_t)index < _spriteData.size()));
    if (index < 0 || (size_t)index >= _spriteData.size())
        return 0;

    sprkey_t load_index = GetDataIndex(index);
    Bitmap *image;
    HError err = _file.LoadSprite(load_index, image);
    if (!image)
    {
        Debug::Printf(kDbgGroup_SprCache, kDbgMsg_Warn,
            "LoadSprite: failed to load sprite %d:\n%s\n - remapping to sprite 0.", index,
            err ? "Sprite does not exist." : err->FullMessage().GetCStr());
        RemapSpriteToSprite0(index);
        return 0;
    }

    // update the stored width/height
    _sprInfos[index].Width = image->GetWidth();
    _sprInfos[index].Height = image->GetHeight();
    _spriteData[index].Image = image;

    // Stop it adding the sprite to the used list just because it's loaded
    // TODO: this messy hack is required, because initialize_sprite calls operator[]
    // which puts the sprite to the MRU list.
    _spriteData[index].Flags |= SPRCACHEFLAG_LOCKED;

    // TODO: this is ugly: asks the engine to convert the sprite using its own knowledge.
    // And engine assigns new bitmap using SpriteCache::SubstituteBitmap().
    // Perhaps change to the callback function pointer?
    initialize_sprite(index);

    if (index != 0)  // leave sprite 0 locked
        _spriteData[index].Flags &= ~SPRCACHEFLAG_LOCKED;

    const size_t size = _sprInfos[index].Width * _sprInfos[index].Height *
        _spriteData[index].Image->GetBPP();
    // Clear up space before adding to cache
    FreeMem(size);
    _spriteData[index].Size = size;
    _cacheSize += size;

#ifdef DEBUG_SPRITECACHE
    Debug::Printf(kDbgGroup_SprCache, kDbgMsg_Debug, "Loaded %d, size now %zu KB", index, _cacheSize / 1024);
#endif

    return size;
}

void SpriteCache::RemapSpriteToSprite0(sprkey_t index)
{
    _sprInfos[index].Flags = _sprInfos[0].Flags;
    _sprInfos[index].Width = _sprInfos[0].Width;
    _sprInfos[index].Height = _sprInfos[0].Height;
    _spriteData[index].Image = nullptr;
    _spriteData[index].Size = _spriteData[0].Size;
    _spriteData[index].Flags |= SPRCACHEFLAG_REMAPPED;
#ifdef DEBUG_SPRITECACHE
    Debug::Printf(kDbgGroup_SprCache, kDbgMsg_Debug, "RemapSpriteToSprite0: %d", index);
#endif
}

int SpriteCache::SaveToFile(const String &filename, int store_flags, SpriteCompression compress, SpriteFileIndex &index)
{
    std::vector<std::pair<bool, Bitmap*>> sprites;
    for (size_t i = 0; i < _spriteData.size(); ++i)
    {
        // NOTE: this is a horrible hack:
        // because Editor expects slightly different RGB order, it swaps colors
        // when loading them (call to initialize_sprite), so here we basically
        // unfix that fix to save the data in a way that engine will expect.
        // TODO: perhaps adjust the editor to NOT need this?!
        pre_save_sprite(_spriteData[i].Image);
        sprites.push_back(std::make_pair(DoesSpriteExist(i), _spriteData[i].Image));
    }
    return SaveSpriteFile(filename, sprites, &_file, store_flags, compress, index);
}

HError SpriteCache::InitFile(const String &filename, const String &sprindex_filename)
{
    Reset();

    std::vector<Size> metrics;
    HError err = _file.OpenFile(filename, sprindex_filename, metrics);
    if (!err)
        return err;

    // Initialize sprite infos
    size_t newsize = metrics.size();
    _sprInfos.resize(newsize);
    _spriteData.resize(newsize);
    _mru.clear();
    for (size_t i = 0; i < metrics.size(); ++i)
    {
        if (!metrics[i].IsNull())
        {
            // Existing sprite
            _spriteData[i].Flags = SPRCACHEFLAG_ISASSET;
            get_new_size_for_sprite(i, metrics[i].Width, metrics[i].Height, _sprInfos[i].Width, _sprInfos[i].Height);
        }
        else
        {
            // Handle empty slot: remap to sprite 0
            if (i > 0) // FIXME: optimize
                InitNullSpriteParams(i);
        }
    }
    return HError::None();
}

void SpriteCache::DetachFile()
{
    _file.Close();
}

} // namespace Common
} // namespace AGS
