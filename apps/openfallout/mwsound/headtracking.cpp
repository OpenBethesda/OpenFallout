#include "headtracking.hpp"

#include <algorithm>
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

    bool isUsableAttitude(const HeadRotation& attitude)
    {
        const double components[] = { attitude.mW, attitude.mX, attitude.mY, attitude.mZ };
        double lengthSquared = 0;
        for (const double c : components)
        {
            if (!std::isfinite(c))
                return false;
            lengthSquared += c * c;
        }
        // Core Motion's quaternions are unit to float precision; this allows far more and drops only nonsense
        return std::abs(lengthSquared - 1) <= 0.01;
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
        if (mLink == Link::Connected)
            return;
        mLink = Link::Connected;
        ++mConnection;
        mSample.reset();
        mClockDifference.reset();
    }

    void HeadSampleBox::disconnect()
    {
        std::lock_guard lock(mMutex);
        mLink = Link::Disconnected;
        mSample.reset();
        mClockDifference.reset();
    }

    void HeadSampleBox::reset()
    {
        std::lock_guard lock(mMutex);
        mLink = Link::Unknown;
        mSample.reset();
        mClockDifference.reset();
    }

    void HeadSampleBox::publishMeasured(
        const HeadRotation& attitude, double sourceSeconds, HeadClock::time_point arrival)
    {
        // Two clocks never run exactly alike. The learnt difference may grow by this much of the time that passed, so
        // that a source whose clock runs a little slow does not make every sample look older and older.
        constexpr double sDriftAllowed = 1e-3;
        // Below this an age is the jitter of the delivery, not an age
        constexpr double sSmallestAge = 1e-3;

        const double arrivalSeconds = std::chrono::duration<double>(arrival.time_since_epoch()).count();
        const double difference = arrivalSeconds - sourceSeconds;
        double age = 0;
        if (std::isfinite(difference))
        {
            std::lock_guard lock(mMutex);
            if (!mClockDifference.has_value())
                mClockDifference = difference;
            else
            {
                const double passed = std::max(0.0, arrivalSeconds - mClockDifferenceAt);
                mClockDifference = std::min(difference, *mClockDifference + sDriftAllowed * passed);
            }
            mClockDifferenceAt = arrivalSeconds;
            age = difference - *mClockDifference;
        }
        HeadClock::time_point measured = arrival;
        if (age >= sSmallestAge)
            measured -= std::chrono::duration_cast<HeadClock::duration>(std::chrono::duration<double>(age));
        publish(attitude, measured);
    }

    void HeadSampleBox::publish(const HeadRotation& attitude, HeadClock::time_point time)
    {
        if (!isUsableAttitude(attitude))
            return;
        std::lock_guard lock(mMutex);
        switch (mLink)
        {
            case Link::Disconnected:
                // A sample still on its way when the headphones went: only a connect starts a connection
                return;
            case Link::Unknown:
                mLink = Link::Connected;
                ++mConnection;
                break;
            case Link::Connected:
                // The headphones left the ears and came back without a disconnect: start afresh
                if (mSample.has_value() && time - mSample->mTime > sHeadSampleStale)
                    ++mConnection;
                break;
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
        mSource->update(now);
        const std::optional<HeadSample> sample = mSource->latest();
        if (!sample.has_value() || now - sample->mTime > sHeadSampleStale)
        {
            // The next sample that arrives is straight ahead
            mReference.reset();
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
        return turnedListener(forward, up, now).value_or(ListenerAxes{ forward, up });
    }

    std::optional<ListenerAxes> HeadTracker::turnedListener(
        const osg::Vec3f& forward, const osg::Vec3f& up, HeadClock::time_point now)
    {
        const std::optional<HeadRotation> head = rotation(now);
        if (!head.has_value())
            return std::nullopt;
        if (isIdentity(*head))
            return ListenerAxes{ forward, up };
        return turnListener(forward, up, *head);
    }
}
