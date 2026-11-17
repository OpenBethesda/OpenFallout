#include "clipcullnode.hpp"

#include <array>
#include <cmath>

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

        class FlipCallback : public SceneUtil::NodeCallback<FlipCallback, osg::Node*, osgUtil::CullVisitor*>
        {
        public:
            FlipCallback(const osg::Plane* cullPlane)
                : mCullPlane(cullPlane)
            {
                if constexpr (!SceneUtil::GLDialect::sHasUserClipPlanes)
                    for (osg::ref_ptr<osg::StateSet>& stateSet : mClipStateSets)
                        stateSet = new osg::StateSet;
            }

            void operator()(osg::Node* node, osgUtil::CullVisitor* cv)
            {
                osg::Vec3d eyePoint = cv->getEyePoint();

                osg::RefMatrix* modelViewMatrix = new osg::RefMatrix(*cv->getModelViewMatrix());

                // apply the height of the plane
                // we can't apply this height in the addClipPlane() since the "flip the below graph" function would
                // otherwise flip the height as well
                modelViewMatrix->preMultTranslate(mCullPlane->getNormal() * ((*mCullPlane)[3] * -1));

                // flip the below graph if the eye point is above the plane
                if (mCullPlane->intersect(osg::BoundingSphere(osg::Vec3d(0, 0, eyePoint.z()), 0)) > 0)
                {
                    modelViewMatrix->preMultScale(osg::Vec3(1, 1, -1));
                }

                // move the plane back along its normal a little bit to prevent bleeding at the water shore
                const float fov = Settings::camera().mFieldOfView;
                constexpr double clipFudgeMin = 2.5; // minimum offset of clip plane
                constexpr double clipFudgeScale = -15000.0;
                double clipFudge
                    = std::abs(std::abs((*mCullPlane)[3]) - eyePoint.z()) * fov / clipFudgeScale - clipFudgeMin;
                modelViewMatrix->preMultTranslate(mCullPlane->getNormal() * clipFudge);

                cv->pushModelViewMatrix(modelViewMatrix, osg::Transform::RELATIVE_RF);
                if constexpr (SceneUtil::GLDialect::sHasUserClipPlanes)
                    traverse(node, cv);
                else
                {
                    // The clip node below holds no plane here: its plane, in eye space where the clip node puts it,
                    // reaches the shaders through a state set of this frame
                    osg::Plane eyePlane(mCullPlane->getNormal(), 0);
                    eyePlane.transform(*modelViewMatrix);
                    osg::StateSet* stateSet = mClipStateSets[cv->getTraversalNumber() % 2];
                    SceneUtil::setEyeSpaceClipPlane(*stateSet, 0, eyePlane);
                    cv->pushStateSet(stateSet);
                    traverse(node, cv);
                    cv->popStateSet();
                }
                cv->popModelViewMatrix();
            }

        private:
            const osg::Plane* mCullPlane;
            std::array<osg::ref_ptr<osg::StateSet>, 2> mClipStateSets;
        };
    }

    ClipCullNode::ClipCullNode()
    {
        addCullCallback(new PlaneCullCallback(&mPlane));

        mClipNodeTransform = new osg::Group;
        mClipNodeTransform->addCullCallback(new FlipCallback(&mPlane));
        osg::Group::addChild(mClipNodeTransform);

        mClipNode = new osg::ClipNode;

        mClipNodeTransform->addChild(mClipNode);
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
        }
        mClipNode->setCullingActive(false);
    }
}
