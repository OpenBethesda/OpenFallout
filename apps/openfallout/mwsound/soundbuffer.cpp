#include "soundbuffer.hpp"

#include "../mwbase/environment.hpp"
#include "../mwworld/esmstore.hpp"

#include <components/debug/debuglog.hpp>
#include <components/esm3/loadsoun.hpp>
#include <components/esm4/loadsndr.hpp>
#include <components/esm4/loadsoun.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/settings/values.hpp>
#include <components/vfs/pathutil.hpp>

#include <algorithm>
#include <cmath>

namespace OFSound
{
    namespace
    {
        constexpr VFS::Path::NormalizedView soundDir("sound");
        constexpr VFS::Path::ExtensionView mp3("mp3");

        // Fallout has no fAudio* settings: each SOUN record carries its own attenuation, which FalloutSounds applies
        // when the sound plays (fallout-rules.md D.S4). A sound played by its file name alone, or one of the Morrowind
        // sound records, has no range of its own. It keeps the distances the engine gave it before the Morrowind
        // settings went (5 and 40, scaled by 20 and 50: 100 and 2000 units), so nothing that plays changes.
        constexpr float sDefaultMinDistance = 100.f;
        constexpr float sDefaultMaxDistance = 2000.f;
        constexpr float sRecordMinDistanceMult = 20.f;
        constexpr float sRecordMaxDistanceMult = 50.f;
    }

    SoundBufferPool::SoundBufferPool(SoundOutput& output)
        : mOutput(&output)
        , mBufferCacheMax(Settings::sound().mBufferCacheMax * 1024 * 1024)
        , mBufferCacheMin(
              std::min(static_cast<std::size_t>(Settings::sound().mBufferCacheMin) * 1024 * 1024, mBufferCacheMax))
    {
    }

    SoundBufferPool::~SoundBufferPool()
    {
        clear();
    }

    SoundBuffer* SoundBufferPool::lookup(const ESM::RefId& soundId) const
    {
        const auto it = mBufferNameMap.find(soundId);
        if (it != mBufferNameMap.end())
        {
            SoundBuffer* sfx = it->second;
            if (sfx->getHandle() != nullptr)
                return sfx;
        }
        return nullptr;
    }

    SoundBuffer* SoundBufferPool::lookup(VFS::Path::NormalizedView fileName) const
    {
        const auto it = mBufferFileNameMap.find(fileName);
        if (it != mBufferFileNameMap.end())
        {
            SoundBuffer* sfx = it->second;
            if (sfx->getHandle() != nullptr)
                return sfx;
        }
        return nullptr;
    }

    SoundBuffer* SoundBufferPool::loadSfx(SoundBuffer* sfx)
    {
        if (sfx->getHandle() != nullptr)
            return sfx;

        auto [handle, size] = mOutput->loadSound(sfx->getResourceName());
        if (handle == nullptr)
            return {};

        sfx->mHandle = handle;

        mBufferCacheSize += size;
        if (mBufferCacheSize > mBufferCacheMax)
        {
            unloadUnused();
            if (!mUnusedBuffers.empty() && mBufferCacheSize > mBufferCacheMax)
                Log(Debug::Warning) << "No unused sound buffers to free, using " << mBufferCacheSize << " bytes!";
        }
        mUnusedBuffers.push_front(sfx);

        return sfx;
    }

    SoundBuffer* SoundBufferPool::load(const ESM::RefId& soundId)
    {
        if (mBufferNameMap.empty())
        {
            const OFWorld::ESMStore* esmstore = OFBase::Environment::get().getESMStore();
            for (const ESM::Sound& sound : esmstore->get<ESM::Sound>())
                insertSound(sound.mId, sound);
            for (const ESM4::Sound& sound : esmstore->get<ESM4::Sound>())
                insertSound(sound.mId, sound);
            for (const ESM4::SoundReference& sound : esmstore->get<ESM4::SoundReference>())
                insertSound(sound.mId, sound);
        }

        SoundBuffer* sfx;
        const auto it = mBufferNameMap.find(soundId);
        if (it != mBufferNameMap.end())
            sfx = it->second;
        else
        {
            const ESM::Sound* sound = OFBase::Environment::get().getESMStore()->get<ESM::Sound>().search(soundId);
            if (sound == nullptr)
                return {};
            sfx = insertSound(soundId, *sound);
        }

        return loadSfx(sfx);
    }

    SoundBuffer* SoundBufferPool::load(VFS::Path::NormalizedView fileName)
    {
        SoundBuffer* sfx;
        const auto it = mBufferFileNameMap.find(fileName);
        if (it != mBufferFileNameMap.end())
            sfx = it->second;
        else
            sfx = insertSound(fileName);

        return loadSfx(sfx);
    }

    void SoundBufferPool::clear()
    {
        for (auto& sfx : mSoundBuffers)
        {
            if (sfx.mHandle)
                mOutput->unloadSound(sfx.mHandle);
            sfx.mHandle = nullptr;
        }

        mBufferFileNameMap.clear();
        mBufferNameMap.clear();
        mUnusedBuffers.clear();
    }

    SoundBuffer* SoundBufferPool::insertSound(VFS::Path::NormalizedView fileName)
    {
        const float volume = 1.f;
        const float min = sDefaultMinDistance;
        const float max = sDefaultMaxDistance;

        SoundBuffer& sfx = mSoundBuffers.emplace_back(fileName, volume, min, max);

        mBufferFileNameMap.emplace(fileName, &sfx);
        return &sfx;
    }

    SoundBuffer* SoundBufferPool::insertSound(const ESM::RefId& soundId, const ESM::Sound& sound)
    {
        float volume = static_cast<float>(std::pow(10.0, (sound.mData.mVolume / 255.0 * 3348.0 - 3348.0) / 2000.0));
        float min = sound.mData.mMinRange;
        float max = sound.mData.mMaxRange;
        if (min == 0 && max == 0)
        {
            min = sDefaultMinDistance;
            max = sDefaultMaxDistance;
        }
        else
        {
            min *= sRecordMinDistanceMult;
            max *= sRecordMaxDistanceMult;
        }
        min = std::max(min, 1.0f);
        max = std::max(min, max);

        SoundBuffer& sfx = mSoundBuffers.emplace_back(
            Misc::ResourceHelpers::correctSoundPath(VFS::Path::toNormalized(sound.mSound)), volume, min, max);

        mBufferNameMap.emplace(soundId, &sfx);
        return &sfx;
    }

    SoundBuffer* SoundBufferPool::insertSound(const ESM::RefId& soundId, const ESM4::Sound& sound)
    {
        VFS::Path::Normalized path = Misc::ResourceHelpers::correctResourcePath({ { soundDir } },
            VFS::Path::toNormalized(sound.mSoundFile), *OFBase::Environment::get().getResourceSystem()->getVFS(), mp3);
        float volume = 1, min = 1, max = 255; // TODO: needs research
        SoundBuffer& sfx = mSoundBuffers.emplace_back(std::move(path), volume, min, max);
        mBufferNameMap.emplace(soundId, &sfx);
        return &sfx;
    }

    SoundBuffer* SoundBufferPool::insertSound(const ESM::RefId& soundId, const ESM4::SoundReference& sound)
    {
        VFS::Path::Normalized path = Misc::ResourceHelpers::correctResourcePath({ { soundDir } },
            VFS::Path::toNormalized(sound.mSoundFile), *OFBase::Environment::get().getResourceSystem()->getVFS(), mp3);
        float volume = 1, min = 1, max = 255; // TODO: needs research
        // TODO: sound.mSoundId can link to another SoundReference, probably we will need to add additional lookups to
        // ESMStore.
        SoundBuffer& sfx = mSoundBuffers.emplace_back(std::move(path), volume, min, max);
        mBufferNameMap.emplace(soundId, &sfx);
        return &sfx;
    }

    void SoundBufferPool::unloadUnused()
    {
        while (!mUnusedBuffers.empty() && mBufferCacheSize > mBufferCacheMin)
        {
            SoundBuffer* const unused = mUnusedBuffers.back();

            mBufferCacheSize -= mOutput->unloadSound(unused->getHandle());
            unused->mHandle = nullptr;

            mUnusedBuffers.pop_back();
        }
    }
}
