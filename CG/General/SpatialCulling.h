#pragma once

#include <DirectXCollision.h>
#include <DirectXMath.h>
#include <array>
#include <memory>
#include <vector>
#include <algorithm>

struct SceneObject
{
    DirectX::XMFLOAT4X4 World{};
    DirectX::BoundingBox Bounds;
};

class Octree
{
public:
    void Build(const std::vector<SceneObject>& objects, int maxDepth = 6, int maxObjectsPerNode = 16)
    {
        mObjects = &objects;
        mMaxDepth = maxDepth;
        mMaxObjectsPerNode = maxObjectsPerNode;

        if (objects.empty())
        {
            mRoot.reset();
            return;
        }

        DirectX::XMFLOAT3 minP = {
            objects[0].Bounds.Center.x - objects[0].Bounds.Extents.x,
            objects[0].Bounds.Center.y - objects[0].Bounds.Extents.y,
            objects[0].Bounds.Center.z - objects[0].Bounds.Extents.z
        };
        DirectX::XMFLOAT3 maxP = {
            objects[0].Bounds.Center.x + objects[0].Bounds.Extents.x,
            objects[0].Bounds.Center.y + objects[0].Bounds.Extents.y,
            objects[0].Bounds.Center.z + objects[0].Bounds.Extents.z
        };

        for (size_t i = 1; i < objects.size(); ++i)
        {
            const auto& b = objects[i].Bounds;
            minP.x = (std::min)(minP.x, b.Center.x - b.Extents.x);
            minP.y = (std::min)(minP.y, b.Center.y - b.Extents.y);
            minP.z = (std::min)(minP.z, b.Center.z - b.Extents.z);
            maxP.x = (std::max)(maxP.x, b.Center.x + b.Extents.x);
            maxP.y = (std::max)(maxP.y, b.Center.y + b.Extents.y);
            maxP.z = (std::max)(maxP.z, b.Center.z + b.Extents.z);
        }

        const DirectX::XMFLOAT3 center = {
            (minP.x + maxP.x) * 0.5f,
            (minP.y + maxP.y) * 0.5f,
            (minP.z + maxP.z) * 0.5f
        };
        const float halfSize = (std::max)({
            (maxP.x - minP.x) * 0.5f,
            (maxP.y - minP.y) * 0.5f,
            (maxP.z - minP.z) * 0.5f,
            1.0f
        });

        mRoot = std::make_unique<Node>();
        mRoot->Bounds.Center = center;
        mRoot->Bounds.Extents = { halfSize, halfSize, halfSize };
        mRoot->ObjectIndices.resize(objects.size());
        for (size_t i = 0; i < objects.size(); ++i)
            mRoot->ObjectIndices[i] = static_cast<int>(i);

        Subdivide(*mRoot, 0);
    }

    void Query(const DirectX::BoundingFrustum& frustum, std::vector<int>& visible) const
    {
        visible.clear();
        if (!mRoot || !mObjects)
            return;

        QueryNode(*mRoot, frustum, visible);
    }

private:
    struct Node
    {
        DirectX::BoundingBox Bounds;
        std::vector<int> ObjectIndices;
        std::array<std::unique_ptr<Node>, 8> Children;

        bool IsLeaf() const
        {
            for (const auto& child : Children)
                if (child)
                    return false;
            return true;
        }
    };

    std::array<DirectX::BoundingBox, 8> MakeChildBounds(const DirectX::BoundingBox& parent) const
    {
        std::array<DirectX::BoundingBox, 8> result;
        const DirectX::XMFLOAT3 childExtents = {
            parent.Extents.x * 0.5f,
            parent.Extents.y * 0.5f,
            parent.Extents.z * 0.5f
        };

        for (int i = 0; i < 8; ++i)
        {
            const float sx = (i & 1) ? 1.0f : -1.0f;
            const float sy = (i & 2) ? 1.0f : -1.0f;
            const float sz = (i & 4) ? 1.0f : -1.0f;

            result[i].Center = {
                parent.Center.x + sx * childExtents.x,
                parent.Center.y + sy * childExtents.y,
                parent.Center.z + sz * childExtents.z
            };
            result[i].Extents = childExtents;
        }
        return result;
    }

    void Subdivide(Node& node, int depth)
    {
        if (depth >= mMaxDepth || static_cast<int>(node.ObjectIndices.size()) <= mMaxObjectsPerNode)
            return;

        const auto childBounds = MakeChildBounds(node.Bounds);
        std::array<std::vector<int>, 8> childObjects;
        std::vector<int> stayHere;
        stayHere.reserve(node.ObjectIndices.size());

        for (int objectIndex : node.ObjectIndices)
        {
            int containingChild = -1;
            for (int childIndex = 0; childIndex < 8; ++childIndex)
            {
                if (childBounds[childIndex].Contains((*mObjects)[objectIndex].Bounds) == DirectX::CONTAINS)
                {
                    containingChild = childIndex;
                    break;
                }
            }

            if (containingChild >= 0)
                childObjects[containingChild].push_back(objectIndex);
            else
                stayHere.push_back(objectIndex);
        }

        node.ObjectIndices = std::move(stayHere);

        for (int i = 0; i < 8; ++i)
        {
            if (childObjects[i].empty())
                continue;

            node.Children[i] = std::make_unique<Node>();
            node.Children[i]->Bounds = childBounds[i];
            node.Children[i]->ObjectIndices = std::move(childObjects[i]);
            Subdivide(*node.Children[i], depth + 1);
        }
    }

    void AppendSubtree(const Node& node, std::vector<int>& visible) const
    {
        visible.insert(visible.end(), node.ObjectIndices.begin(), node.ObjectIndices.end());
        for (const auto& child : node.Children)
            if (child)
                AppendSubtree(*child, visible);
    }

    void QueryNode(const Node& node, const DirectX::BoundingFrustum& frustum, std::vector<int>& visible) const
    {
        const DirectX::ContainmentType nodeResult = frustum.Contains(node.Bounds);
        if (nodeResult == DirectX::DISJOINT)
            return;

        if (nodeResult == DirectX::CONTAINS)
        {
            AppendSubtree(node, visible);
            return;
        }

        for (int objectIndex : node.ObjectIndices)
        {
            if (frustum.Contains((*mObjects)[objectIndex].Bounds) != DirectX::DISJOINT)
                visible.push_back(objectIndex);
        }

        for (const auto& child : node.Children)
            if (child)
                QueryNode(*child, frustum, visible);
    }

private:
    const std::vector<SceneObject>* mObjects = nullptr;
    std::unique_ptr<Node> mRoot;
    int mMaxDepth = 6;
    int mMaxObjectsPerNode = 16;
};
