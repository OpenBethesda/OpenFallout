#include <apps/openfallout/mwrender/clipcullnode.hpp>

#include <components/sceneutil/gldialect.hpp>
#include <components/shader/glsles.hpp>

#include <osg/ClipPlane>
#include <osg/FrameStamp>
#include <osg/Geometry>
#include <osg/Group>
#include <osg/StateSet>
#include <osg/Uniform>
#include <osg/Viewport>
#include <osgUtil/CullVisitor>
#include <osgUtil/RenderLeaf>
#include <osgUtil/RenderStage>
#include <osgUtil/StateGraph>

#include <gtest/gtest.h>

#include <optional>
#include <vector>

namespace
{
    using namespace OFRender;

    constexpr double sWaterHeight = 100.0;

    // A box that reaches from below the water to above it, like a pier or a steep bank
    osg::ref_ptr<osg::Geometry> boxThroughTheWater()
    {
        osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array;
        for (float x : { -20.f, 20.f })
            for (float y : { -20.f, 20.f })
                for (float z : { 50.f, 150.f })
                    vertices->push_back(osg::Vec3f(x, y, z));
        osg::ref_ptr<osg::Geometry> box = new osg::Geometry;
        box->setVertexArray(vertices);
        box->addPrimitiveSet(new osg::DrawArrays(GL_POINTS, 0, vertices->size()));
        return box;
    }

    // What a frame's cull traversal of a camera gives the draw traversal: the leaves to draw, each under its state
    // graph, and the positioned state of the render stage
    struct CulledFrame
    {
        // The leaves belong to the cull visitor: it resets them when it goes
        osg::ref_ptr<osgUtil::CullVisitor> mCullVisitor = new osgUtil::CullVisitor;
        osg::ref_ptr<osgUtil::StateGraph> mStateGraph = new osgUtil::StateGraph;
        osg::ref_ptr<osgUtil::RenderStage> mRenderStage = new osgUtil::RenderStage;
        std::vector<osgUtil::RenderLeaf*> mLeaves;
    };

    void collectLeaves(osgUtil::StateGraph& graph, std::vector<osgUtil::RenderLeaf*>& leaves)
    {
        for (const osg::ref_ptr<osgUtil::RenderLeaf>& leaf : graph._leaves)
            leaves.push_back(leaf.get());
        for (auto& [stateSet, child] : graph._children)
            collectLeaves(*child, leaves);
    }

    // Culls the scene as a camera whose eye stands below the water and looks along the y axis at the box. That is
    // where the reflection camera's eye is: its view is the camera's view mirrored at the water.
    void cull(osg::Node& scene, CulledFrame& frame)
    {
        osgUtil::CullVisitor* cv = frame.mCullVisitor.get();
        osg::ref_ptr<osg::FrameStamp> frameStamp = new osg::FrameStamp;
        frameStamp->setFrameNumber(1);
        cv->setFrameStamp(frameStamp);
        cv->setTraversalNumber(1);
        cv->setStateGraph(frame.mStateGraph);
        cv->setRenderStage(frame.mRenderStage);

        const osg::Matrixd view
            = osg::Matrixd::lookAt(osg::Vec3d(0, -500, 50), osg::Vec3d(0, 0, 50), osg::Vec3d(0, 0, 1));
        frame.mRenderStage->setInitialViewMatrix(new osg::RefMatrix(view));
        osg::ref_ptr<osg::Viewport> viewport = new osg::Viewport(0, 0, 256, 256);
        frame.mRenderStage->setViewport(viewport);

        cv->pushViewport(viewport);
        cv->pushProjectionMatrix(new osg::RefMatrix(osg::Matrixd::perspective(60.0, 1.0, 1.0, 10000.0)));
        cv->pushModelViewMatrix(new osg::RefMatrix(view), osg::Transform::ABSOLUTE_RF);
        scene.accept(*cv);
        cv->popModelViewMatrix();
        cv->popProjectionMatrix();
        cv->popViewport();

        collectLeaves(*frame.mStateGraph, frame.mLeaves);
    }

    // The state sets a leaf is drawn with, from the root of the state graph to the leaf
    std::vector<const osg::StateSet*> stateSetsOf(const osgUtil::RenderLeaf& leaf)
    {
        std::vector<const osg::StateSet*> stateSets;
        for (const osgUtil::StateGraph* graph = leaf._parent; graph != nullptr; graph = graph->_parent)
            if (graph->getStateSet() != nullptr)
                stateSets.insert(stateSets.begin(), graph->getStateSet());
        return stateSets;
    }

    osg::StateAttribute::GLModeValue modeOf(const osgUtil::RenderLeaf& leaf, osg::StateAttribute::GLMode mode)
    {
        osg::StateAttribute::GLModeValue value = osg::StateAttribute::INHERIT;
        for (const osg::StateSet* stateSet : stateSetsOf(leaf))
            if (stateSet->getMode(mode) != osg::StateAttribute::INHERIT)
                value = stateSet->getMode(mode);
        return value;
    }

    // The eye-space plane that clip plane 0 cuts the leaf with: in the OpenGL build the clip plane that the render
    // stage positions with its model-view matrix, in the Metal build the uniform the converted shaders read
    std::optional<osg::Plane> eyeSpaceClipPlane0(const osgUtil::RenderLeaf& leaf, const osgUtil::RenderStage& stage)
    {
        std::optional<osg::Plane> plane;
        if constexpr (SceneUtil::GLDialect::sHasUserClipPlanes)
        {
            osgUtil::PositionalStateContainer* positioned = stage.getPositionalStateContainer();
            if (positioned == nullptr)
                return plane;
            for (const auto& [attribute, matrix] : positioned->getAttrMatrixList())
            {
                const auto* clipPlane = dynamic_cast<const osg::ClipPlane*>(attribute.get());
                if (clipPlane == nullptr || clipPlane->getClipPlaneNum() != 0)
                    continue;
                osg::Plane eyePlane(clipPlane->getClipPlane());
                if (matrix != nullptr)
                    eyePlane.transform(*matrix);
                plane = eyePlane;
            }
        }
        else
        {
            const std::string name = Shader::clipPlaneUniformName(0);
            for (const osg::StateSet* stateSet : stateSetsOf(leaf))
            {
                const osg::Uniform* uniform = stateSet->getUniform(name);
                osg::Vec4f value;
                if (uniform != nullptr && uniform->get(value))
                    plane = osg::Plane(value);
            }
        }
        return plane;
    }

    TEST(OpenFalloutRenderClipCullNode, theSceneUnderItIsDrawnCutAtTheWater)
    {
        osg::ref_ptr<ClipCullNode> clipCullNode = new ClipCullNode;
        clipCullNode->setPlane(osg::Plane(osg::Vec3d(0, 0, 1), osg::Vec3d(0, 0, sWaterHeight)));
        // as the water's reflection adds its scene
        osg::ref_ptr<osg::Group> scene = new osg::Group;
        clipCullNode->addChild(scene);
        scene->addChild(boxThroughTheWater());

        CulledFrame frame;
        cull(*clipCullNode, frame);

        ASSERT_EQ(frame.mLeaves.size(), 1u);
        const osgUtil::RenderLeaf& leaf = *frame.mLeaves.front();

        EXPECT_EQ(modeOf(leaf, GL_CLIP_PLANE0) & osg::StateAttribute::ON, osg::StateAttribute::ON);

        const std::optional<osg::Plane> plane = eyeSpaceClipPlane0(leaf, *frame.mRenderStage);
        ASSERT_TRUE(plane.has_value());
        // What lies above the water is kept and what lies under it is cut away. The plane lies a little under the
        // water so that the shore does not bleed.
        const osg::Matrixd& modelView = *leaf._modelview;
        EXPECT_GT(plane->distance(osg::Vec3d(0, 0, sWaterHeight + 1) * modelView), 0.0);
        EXPECT_GT(plane->distance(osg::Vec3d(0, 0, sWaterHeight) * modelView), 0.0);
        EXPECT_LT(plane->distance(osg::Vec3d(0, 0, sWaterHeight - 20) * modelView), 0.0);
        EXPECT_LT(plane->distance(osg::Vec3d(15, 15, 50) * modelView), 0.0);
    }

    TEST(OpenFalloutRenderClipCullNode, thePlaneFollowsTheWaterHeight)
    {
        osg::ref_ptr<ClipCullNode> clipCullNode = new ClipCullNode;
        clipCullNode->setPlane(osg::Plane(osg::Vec3d(0, 0, 1), osg::Vec3d(0, 0, sWaterHeight + 30)));
        clipCullNode->addChild(boxThroughTheWater());

        CulledFrame frame;
        cull(*clipCullNode, frame);

        ASSERT_EQ(frame.mLeaves.size(), 1u);
        const osgUtil::RenderLeaf& leaf = *frame.mLeaves.front();
        const std::optional<osg::Plane> plane = eyeSpaceClipPlane0(leaf, *frame.mRenderStage);
        ASSERT_TRUE(plane.has_value());
        const osg::Matrixd& modelView = *leaf._modelview;
        EXPECT_GT(plane->distance(osg::Vec3d(0, 0, sWaterHeight + 31) * modelView), 0.0);
        EXPECT_LT(plane->distance(osg::Vec3d(0, 0, sWaterHeight + 10) * modelView), 0.0);
    }
}
