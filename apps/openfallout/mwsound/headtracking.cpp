#include "headtracking.hpp"

#include <cmath>

#include <components/debug/debuglog.hpp>

namespace OFSound
{
    namespace
    {
        struct Vec
        {
            double mX;
            double mY;
            double mZ;
        };

        Vec cross(const Vec& a, const Vec& b)
        {
            return { a.mY * b.mZ - a.mZ * b.mY, a.mZ * b.mX - a.mX * b.mZ, a.mX * b.mY - a.mY * b.mX };
        }

        Vec normalized(const Vec& v)
        {
            const double length = std::sqrt(v.mX * v.mX + v.mY * v.mY + v.mZ * v.mZ);
            if (length == 0)
                return v;
            return { v.mX / length, v.mY / length, v.mZ / length };
        }

        Vec fromOsg(const osg::Vec3f& v)
        {
            return { v.x(), v.y(), v.z() };
        }

        HeadRotation normalized(const HeadRotation& q)
        {
            const double length = std::sqrt(q.mW * q.mW + q.mX * q.mX + q.mY * q.mY + q.mZ * q.mZ);
            if (length == 0)
                return HeadRotation{};
            return { q.mW / length, q.mX / length, q.mY / length, q.mZ / length };
        }

        HeadRotation conjugate(const HeadRotation& q)
        {
            return { q.mW, -q.mX, -q.mY, -q.mZ };
        }

        // q v q*, for a unit q
        Vec rotate(const HeadRotation& q, const Vec& v)
        {
            const Vec axis{ q.mX, q.mY, q.mZ };
            const Vec t = cross(axis, v);
            const Vec twice{ 2 * t.mX, 2 * t.mY, 2 * t.mZ };
            const Vec c = cross(axis, twice);
            return { v.mX + q.mW * twice.mX + c.mX, v.mY + q.mW * twice.mY + c.mY, v.mZ + q.mW * twice.mZ + c.mZ };
        }

        bool isIdentity(const HeadRotation& q)
        {
            return q.mW == 1 && q.mX == 0 && q.mY == 0 && q.mZ == 0;
        }
    }

    HeadRotation HeadRotation::fromAxisAngle(double x, double y, double z, double radians)
    {
        const Vec axis = normalized(Vec{ x, y, z });
        const double s = std::sin(radians / 2);
        return { std::cos(radians / 2), axis.mX * s, axis.mY * s, axis.mZ * s };
    }

    HeadRotation operator*(const HeadRotation& a, const HeadRotation& b)
    {
        return {
            a.mW * b.mW - a.mX * b.mX - a.mY * b.mY - a.mZ * b.mZ,
            a.mW * b.mX + a.mX * b.mW + a.mY * b.mZ - a.mZ * b.mY,
            a.mW * b.mY - a.mX * b.mZ + a.mY * b.mW + a.mZ * b.mX,
            a.mW * b.mZ + a.mX * b.mY - a.mY * b.mX + a.mZ * b.mW,
        };
    }

    HeadRotation relativeTo(const HeadRotation& reference, const HeadRotation& attitude)
    {
        return normalized(conjugate(normalized(reference)) * normalized(attitude));
    }

    ListenerAxes turnListener(const osg::Vec3f& forward, const osg::Vec3f& up, const HeadRotation& head)
    {
        // The camera's own axes in the world: right, forward and up (right-handed, as the world and OpenAL are)
        const Vec f = normalized(fromOsg(forward));
        const Vec r = normalized(cross(f, fromOsg(up)));
        const Vec u = cross(r, f);

        // The head's forward and up in the camera's frame, then in the world
        const HeadRotation q = normalized(head);
        const auto toWorld = [&](const Vec& local) {
            return osg::Vec3f(static_cast<float>(r.mX * local.mX + f.mX * local.mY + u.mX * local.mZ),
                static_cast<float>(r.mY * local.mX + f.mY * local.mY + u.mY * local.mZ),
                static_cast<float>(r.mZ * local.mX + f.mZ * local.mY + u.mZ * local.mZ));
        };
        return { toWorld(rotate(q, Vec{ 0, 1, 0 })), toWorld(rotate(q, Vec{ 0, 0, 1 })) };
    }

    void HeadSampleBox::connect()
    {
        std::lock_guard lock(mMutex);
        if (mConnected)
            return;
        mConnected = true;
        ++mConnection;
        mSample.reset();
    }

    void HeadSampleBox::disconnect()
    {
        std::lock_guard lock(mMutex);
        mConnected = false;
        mSample.reset();
    }

    void HeadSampleBox::publish(const HeadRotation& attitude, HeadClock::time_point time)
    {
        std::lock_guard lock(mMutex);
        if (!mConnected)
        {
            mConnected = true;
            ++mConnection;
        }
        mSample = HeadSample{ attitude, time, ++mCount, mConnection };
    }

    std::optional<HeadSample> HeadSampleBox::latest() const
    {
        std::lock_guard lock(mMutex);
        return mSample;
    }

    HeadTracker::HeadTracker(std::unique_ptr<HeadMotionSource> source)
        : mSource(std::move(source))
    {
    }

    HeadTracker::~HeadTracker()
    {
        setEnabled(false);
    }

    void HeadTracker::setEnabled(bool enabled)
    {
        if (enabled == mEnabled)
            return;
        mEnabled = enabled;
        mReference.reset();
        mRateStart.reset();
        if (enabled)
        {
            Log(Debug::Info) << "Head tracking on: waiting for headphones that report motion";
            mSource->start();
        }
        else
        {
            mSource->stop();
            mDelivering = false;
            Log(Debug::Info) << "Head tracking off";
        }
    }

    void HeadTracker::recentre()
    {
        mReference.reset();
    }

    void HeadTracker::setDelivering(bool delivering)
    {
        if (delivering == mDelivering)
            return;
        mDelivering = delivering;
        if (delivering)
            Log(Debug::Info) << "Head tracking: motion arriving, the listener turns with the head";
        else
            Log(Debug::Info) << "Head tracking: no motion from the headphones, the listener follows the camera";
    }

    void HeadTracker::logRate(const HeadSample& sample)
    {
        if (!mRateStart.has_value() || mRateStart->mConnection != sample.mConnection)
        {
            mRateStart = sample;
            return;
        }
        const std::chrono::duration<double> span = sample.mTime - mRateStart->mTime;
        if (span < std::chrono::seconds(10))
            return;
        Log(Debug::Debug) << "Head tracking: " << (sample.mCount - mRateStart->mCount) / span.count()
                          << " samples per second";
        mRateStart = sample;
    }

    std::optional<HeadRotation> HeadTracker::rotation(HeadClock::time_point now)
    {
        if (!mEnabled)
            return std::nullopt;
        const std::optional<HeadSample> sample = mSource->latest();
        if (!sample.has_value() || now - sample->mTime > sHeadSampleStale)
        {
            setDelivering(false);
            return std::nullopt;
        }
        setDelivering(true);
        if (!mReference.has_value() || mReference->mConnection != sample->mConnection)
        {
            mReference = sample;
            Log(Debug::Info) << "Head tracking: straight ahead set to the pose of the headphones now";
        }
        logRate(*sample);
        if (sample->mCount == mReference->mCount)
            return HeadRotation{};
        return relativeTo(mReference->mAttitude, sample->mAttitude);
    }

    ListenerAxes HeadTracker::listener(const osg::Vec3f& forward, const osg::Vec3f& up, HeadClock::time_point now)
    {
        const std::optional<HeadRotation> head = rotation(now);
        if (!head.has_value() || isIdentity(*head))
            return { forward, up };
        return turnListener(forward, up, *head);
    }
}
