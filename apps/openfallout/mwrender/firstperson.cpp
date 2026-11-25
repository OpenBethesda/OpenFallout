#include "firstperson.hpp"

#include <osg/ColorMask>
#include <osg/Depth>

#include <osgUtil/CullVisitor>
#include <osgUtil/RenderBin>

#include <components/sceneutil/depth.hpp>

#include "postprocessor.hpp"
#include "renderbin.hpp"
#include "renderingmanager.hpp"

namespace OFRender
{
    namespace
    {
        /// @brief A RenderBin callback to clear the depth buffer before rendering.
        /// Switches depth attachments to a proxy renderbuffer, reattaches original depth then redraws first person root.
        /// This gives a complete depth buffer which can be used for postprocessing, buffer resolves as if depth was never
        /// cleared.
        class DepthClearCallback : public osgUtil::RenderBin::DrawCallback
        {
        public:
            DepthClearCallback()
            {
                mDepth = new SceneUtil::AutoDepth;
                mDepth->setWriteMask(true);

                mStateSet = new osg::StateSet;
                mStateSet->setAttributeAndModes(new osg::ColorMask(false, false, false, false), osg::StateAttribute::ON);
            }

            void drawImplementation(
                osgUtil::RenderBin* bin, osg::RenderInfo& renderInfo, osgUtil::RenderLeaf*& previous) override
            {
                osg::State* state = renderInfo.getState();

                PostProcessor* postProcessor = static_cast<PostProcessor*>(renderInfo.getCurrentCamera()->getUserData());

                state->applyAttribute(mDepth);

                unsigned int frameId = state->getFrameStamp()->getFrameNumber() % 2;

                postProcessor->getFbo(PostProcessor::FBO_FirstPerson, frameId)->apply(*state);
                glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
                // color accumulation pass
                bin->drawImplementation(renderInfo, previous);

                auto primaryFBO = postProcessor->getPrimaryFbo(frameId);
                primaryFBO->apply(*state);

                postProcessor->getFbo(PostProcessor::FBO_OpaqueDepth, frameId)->apply(*state);

                // depth accumulation pass
                osg::ref_ptr<osg::StateSet> restore = bin->getStateSet();
                bin->setStateSet(mStateSet);
                bin->drawImplementation(renderInfo, previous);
                bin->setStateSet(restore);

                primaryFBO->apply(*state);

                state->checkGLErrors("after DepthClearCallback::drawImplementation");
            }

            osg::ref_ptr<osg::Depth> mDepth;
            osg::ref_ptr<osg::StateSet> mStateSet;
        };

        /// Overrides Field of View to given value for rendering the subgraph.
        /// Must be added as cull callback.
        class OverrideFieldOfViewCallback : public osg::NodeCallback
        {
        public:
            OverrideFieldOfViewCallback(float fov, RenderingManager* renderingManager)
                : mFov(fov)
                , mRenderingManager(renderingManager)
            {
            }

            void operator()(osg::Node* node, osg::NodeVisitor* nv) override
            {
                osgUtil::CullVisitor* cv = static_cast<osgUtil::CullVisitor*>(nv);
                float fov, aspect, zNear, zFar;
                if (cv->getProjectionMatrix()->getPerspective(fov, aspect, zNear, zFar) && std::abs(fov - mFov) > 0.001)
                {
                    fov = mFov;
                    osg::ref_ptr<osg::RefMatrix> newProjectionMatrix = new osg::RefMatrix();
                    newProjectionMatrix->makePerspective(fov, aspect, zNear, zFar);

                    osg::Vec2f offset = mRenderingManager->getProjectionOffset();

                    double offsetX = (offset.x() / cv->getViewport()->width()) * 2.0;
                    double offsetY = (offset.y() / cv->getViewport()->height()) * 2.0;

                    const osg::Matrix translation = osg::Matrix::translate(offsetX, offsetY, 0.0);
                    newProjectionMatrix->postMult(translation);

                    osg::ref_ptr<osg::RefMatrix> invertedOldMatrix = cv->getProjectionMatrix();
                    invertedOldMatrix = new osg::RefMatrix(osg::RefMatrix::inverse(*invertedOldMatrix));
                    osg::ref_ptr<osg::RefMatrix> viewMatrix = new osg::RefMatrix(*cv->getModelViewMatrix());
                    viewMatrix->postMult(*newProjectionMatrix);
                    viewMatrix->postMult(*invertedOldMatrix);
                    cv->pushModelViewMatrix(viewMatrix, osg::Transform::ReferenceFrame::ABSOLUTE_RF);
                    traverse(node, nv);
                    cv->popModelViewMatrix();
                }
                else
                    traverse(node, nv);
            }

        private:
            float mFov;
            RenderingManager* mRenderingManager;
        };
    }

    void setFirstPersonRenderBin(osg::Node& root)
    {
        [[maybe_unused]] static const bool prototypeAdded = [&] {
            osg::ref_ptr<osgUtil::RenderBin> depthClearBin(new osgUtil::RenderBin);
            depthClearBin->setDrawCallback(new DepthClearCallback());
            osgUtil::RenderBin::addRenderBinPrototype("DepthClear", depthClearBin);
            return true;
        }();
        root.getOrCreateStateSet()->setRenderBinDetails(
            RenderBin_FirstPerson, "DepthClear", osg::StateSet::OVERRIDE_RENDERBIN_DETAILS);
    }

    void addFirstPersonFieldOfView(osg::Node& root, float fov, RenderingManager* renderingManager)
    {
        root.addCullCallback(new OverrideFieldOfViewCallback(fov, renderingManager));
    }
}
