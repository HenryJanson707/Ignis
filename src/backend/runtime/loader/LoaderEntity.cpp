#include "LoaderEntity.h"
#include "Loader.h"
#include "Logger.h"
#include "bvh/SceneBVHAdapter.h"
#include "serialization/VectorSerializer.h"

#include <chrono>

namespace IG {
using namespace Parser;

template <typename Derived>
inline void writeMatrix(Serializer& serializer, const Eigen::MatrixBase<Derived>& m)
{
    for (int j = 0; j < m.cols(); ++j)
        for (int i = 0; i < m.rows(); ++i)
            serializer.write((float)m(i, j));
}

template <size_t N>
inline static void setup_bvh(std::vector<EntityObject>& input, LoaderResult& result)
{
    using Node = typename BvhNEnt<N>::Node;
    std::vector<Node> nodes;
    std::vector<EntityLeaf1> objs;
    build_scene_bvh<N>(nodes, objs, input);

    result.Database.SceneBVH.Nodes.resize(sizeof(Node) * nodes.size());
    std::memcpy(result.Database.SceneBVH.Nodes.data(), nodes.data(), result.Database.SceneBVH.Nodes.size());
    result.Database.SceneBVH.Leaves.resize(sizeof(EntityLeaf1) * objs.size());
    std::memcpy(result.Database.SceneBVH.Leaves.data(), objs.data(), result.Database.SceneBVH.Leaves.size());
}

bool LoaderEntity::load(LoaderContext& ctx, LoaderResult& result)
{
    // Fill entity list
    ctx.Environment.SceneBBox = BoundingBox::Empty();

    const auto start1 = std::chrono::high_resolution_clock::now();

    std::vector<EntityObject> in_objs;
    result.Database.EntityTable.reserve(ctx.Scene.entities().size() * 48);
    for (const auto& pair : ctx.Scene.entities()) {
        const auto child = pair.second;

        // Query shape
        const std::string shapeName = child->property("shape").getString();
        if (shapeName.empty()) {
            IG_LOG(L_ERROR) << "Entity " << pair.first << " has no shape" << std::endl;
            continue;
        } else if (ctx.Environment.ShapeIDs.count(shapeName) == 0) {
            IG_LOG(L_ERROR) << "Entity " << pair.first << " has unknown shape " << shapeName << std::endl;
            continue;
        }
        const uint32 shapeID = ctx.Environment.ShapeIDs.at(shapeName);

        // Query bsdf
        const std::string bsdfName = child->property("bsdf").getString();
        if (bsdfName.empty()) {
            IG_LOG(L_ERROR) << "Entity " << pair.first << " has no bsdf" << std::endl;
            continue;
        } else if (!ctx.Scene.bsdf(bsdfName)) {
            IG_LOG(L_ERROR) << "Entity " << pair.first << " has unknown bsdf " << bsdfName << std::endl;
            continue;
        }

        // Extract entity information
        Transformf transform = child->property("transform").getTransform();
        transform.makeAffine();

        const Transformf invTransform = transform.inverse();
        const BoundingBox& shapeBox   = ctx.Environment.Shapes[shapeID].BoundingBox;
        const BoundingBox entityBox   = shapeBox.transformed(transform);

        // Extend scene box
        ctx.Environment.SceneBBox.extend(entityBox);

        // Register name for lights to assosciate with
        ctx.Environment.EntityIDs[pair.first] = ctx.Environment.Entities.size();
        ctx.Environment.Entities.push_back({ transform, pair.first, shapeName, bsdfName });

        // Write data to dyntable
        auto& entityData = result.Database.EntityTable.addLookup(0, 0, DefaultAlignment); // We do not make use of the typeid
        VectorSerializer entitySerializer(entityData, false);
        writeMatrix(entitySerializer, invTransform.matrix().block<3, 4>(0, 0));                    // To Local
        writeMatrix(entitySerializer, transform.matrix().block<3, 4>(0, 0));                       // To Global
        writeMatrix(entitySerializer, transform.matrix().block<3, 3>(0, 0).transpose().inverse()); // To Global [Normal]
        entitySerializer.write((uint32)shapeID);

        // Extract information for BVH building
        EntityObject obj;
        obj.BBox    = entityBox;
        obj.Local   = invTransform.matrix();
        obj.ShapeID = shapeID;
        in_objs.emplace_back(obj);
    }

    IG_LOG(L_DEBUG) << "Storing Entities took " << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start1).count() / 1000.0f << " seconds" << std::endl;

    if (ctx.Environment.Entities.empty()) {
        ctx.Environment.SceneDiameter = 0;
        return true;
    }

    ctx.Environment.SceneDiameter = ctx.Environment.SceneBBox.diameter().norm();

    // Build bvh (keep in mind that this BVH has no pre-padding as in the case for shape BVHs)
    IG_LOG(L_DEBUG) << "Generating BVH for scene" << std::endl;
    const auto start2 = std::chrono::high_resolution_clock::now();
    if (ctx.Target == Target::NVVM || ctx.Target == Target::AMDGPU) {
        setup_bvh<2>(in_objs, result);
    } else if (ctx.Target == Target::GENERIC || ctx.Target == Target::ASIMD || ctx.Target == Target::SSE42) {
        setup_bvh<4>(in_objs, result);
    } else {
        setup_bvh<8>(in_objs, result);
    }
    IG_LOG(L_DEBUG) << "Building Scene BVH took " << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start2).count() / 1000.0f << " seconds" << std::endl;

    return true;
}

} // namespace IG