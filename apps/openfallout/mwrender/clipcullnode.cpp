#include "clipcullnode.hpp"

#include <array>
#include <cmath>
#include <map>
#include <mutex>

#include <osg/ClipNode>
#include <osgUtil/CullVisitor>

#include <components/sceneutil/clipplane.hpp>
#include <components/sceneutil/gldialect.hpp>
#include <components/sceneutil/nodecallback.hpp>
#include <components/settings/values.hpp>

namespace OFRender
{
    namespace
    {
        class PlaneCullCallback : public SceneUtil::NodeCallback<PlaneCullCallback, osg::Node*, osgUtil::CullVisitor*>
        {
        public:
            /// @param cullPlane The culling plane (in world space).
            PlaneCullCallback(const osg::Plane* cullPlane)
                : mCullPlane(cullPlane)
            {
            }

            void operator()(osg::Node* node, osgUtil::CullVisitor* cv)
            {
                osg::Polytope::PlaneList origPlaneList
                    = cv->getProjectionCullingStack().back().getFrustum().getPlaneList();

                osg::Plane plane = *mCullPlane;
                plane.transform(*cv->getCurrentRenderStage()->getInitialViewMatrix());

                osg::Vec3d eyePoint = cv->getEyePoint();
                if (mCullPlane->intersect(osg::BoundingSphere(osg::Vec3d(0, 0, eyePoint.z()), 0)) > 0)
                    plane.flip();

                cv->getProjectionCullingStack().back().getFrustum().add(plane);

                traverse(node, cv);

                // undo
                cv->getProjectionCullingStack().back().getFrustum().set(origPlaneList);
            }

        private:
            const osg::Plane* mCullPlane;
        };

        /// The model-view matrix that the clip plane (normal of the cull plane, 0) is positioned with, from the
        /// model-view matrix of the ClipCullNode.
        osg::ref_ptr<osg::RefMatrix> clipPlaneModelView(const osg::Plane& cullPlane, const osgUtil::CullVisitor& cv)
        {
            osg::Vec3d eyePoint = cv.getEyePoint();

            osg::ref_ptr<osg::RefMatrix> modelViewMatrix = new osg::RefMatrix(*cv.getModelViewMatrix());

            // apply the height of the plane
            // we can't apply this height in the addClipPlane() since the "flip the below graph" function would
            // otherwise flip the height as well
            modelViewMatrix->preMultTranslate(cullPlane.getNormal() * (cullPlane[3] * -1));

            // flip the below graph if the eye point is above the plane
            if (cullPlane.intersect(osg::BoundingSphere(osg::Vec3d(0, 0, eyePoint.z()), 0)) > 0)
            {
                modelViewMatrix->preMultScale(osg::Vec3(1, 1, -1));
            }

            // move the plane back along its normal a little bit to prevent bleeding at the water shore
            const float fov = Settings::camera().mFieldOfView;
            constexpr double clipFudgeMin = 2.5; // minimum offset of clip plane
            constexpr double clipFudgeScale = -15000.0;
            double clipFudge = std::abs(std::abs(cullPlane[3]) - eyePoint.z()) * fov / clipFudgeScale - clipFudgeMin;
            modelViewMatrix->preMultTranslate(cullPlane.getNormal() * clipFudge);

            return modelViewMatrix;
        }

        /// Desktop OpenGL: positions the clip node's plane, which then cuts all that the render stage draws
        class FlipCallback : public SceneUtil::NodeCallback<FlipCallback, osg::Node*, osgUtil::CullVisitor*>
        {
        public:
            FlipCallback(const osg::Plane* cullPlane)
                : mCullPlane(cullPlane)
            {
            }

            void operator()(osg::Node* node, osgUtil::CullVisitor* cv)
            {
                cv->pushModelViewMatrix(clipPlaneModelView(*mCullPlane, *cv), osg::Transform::RELATIVE_RF);
                traverse(node, cv);
                cv->popModelViewMatrix();
            }

        private:
            const osg::Plane* mCullPlane;
        };

        /// OpenGL ES has no clip node that positions a plane: the plane, in the eye space the clip node would put it
        /// in, reaches the shaders of everything below the ClipCullNode through a state set of the cull traversal.
        class ClipStateCallback : public SceneUtil::NodeCallback<ClipStateCallback, osg::Node*, osgUtil::CullVisitor*>
        {
        public:
            ClipStateCallback(const osg::Plane* cullPlane)
                : mCullPlane(cullPlane)
            {
            }

            void operator()(osg::Node* node, osgUtil::CullVisitor* cv)
            {
                osg::Plane eyePlane(mCullPlane->getNormal(), 0);
                eyePlane.transform(*clipPlaneModelView(*mCullPlane, *cv));
                osg::StateSet* stateSet = stateSetFor(*cv);
                SceneUtil::setEyeSpaceClipPlane(*stateSet, 0, eyePlane);
                cv->pushStateSet(stateSet);
                traverse(node, cv);
                cv->popStateSet();
            }

        private:
            // A state set that is being drawn is never changed: the cull of the next frame takes the other one of a
            // pair. Each cull visitor has its own pair, because a frame can cull the reflection more than once, each
            // time with its own plane, before any of it is drawn: stereo without multiview (the only stereo OpenGL ES
            // on Metal has) culls each eye with a cull visitor of its own.
            osg::StateSet* stateSetFor(const osgUtil::CullVisitor& cv)
            {
                const std::lock_guard lock(mMutex);
                osg::ref_ptr<osg::StateSet>& stateSet = mStateSets[&cv][cv.getTraversalNumber() % 2];
                if (stateSet == nullptr)
                    stateSet = new osg::StateSet;
                return stateSet.get();
            }

            const osg::Plane* mCullPlane;
            std::mutex mMutex;
            std::map<const osgUtil::CullVisitor*, std::array<osg::ref_ptr<osg::StateSet>, 2>> mStateSets;
        };
    }

    ClipCullNode::ClipCullNode()
    {
        addCullCallback(new PlaneCullCallback(&mPlane));

        if constexpr (SceneUtil::GLDialect::sHasUserClipPlanes)
        {
            mClipNodeTransform = new osg::Group;
            mClipNodeTransform->addCullCallback(new FlipCallback(&mPlane));
            osg::Group::addChild(mClipNodeTransform);

            mClipNode = new osg::ClipNode;

            mClipNodeTransform->addChild(mClipNode);
        }
        else
            addCullCallback(new ClipStateCallback(&mPlane));
    }

    void ClipCullNode::setPlane(const osg::Plane& plane)
    {
        if (plane == mPlane)
            return;
        mPlane = plane;

        if constexpr (SceneUtil::GLDialect::sHasUserClipPlanes)
        {
            mClipNode->getClipPlaneList().clear();
            mClipNode->addClipPlane(
                new osg::ClipPlane(0, osg::Plane(mPlane.getNormal(), 0))); // mPlane.d() applied in FlipCallback
            mClipNode->setStateSetModes(*getOrCreateStateSet(), osg::StateAttribute::ON);
            mClipNode->setCullingActive(false);
        }
    }
}
