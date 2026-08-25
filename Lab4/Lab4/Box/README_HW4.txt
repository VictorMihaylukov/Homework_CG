Homework 4: Frustum Culling + Octree

Implemented:
- 50 x 50 grid = 2500 scene objects.
- One world-space AABB per object.
- Linear frustum culling.
- Octree acceleration for frustum culling.
- Runtime switches and visible-object counter in the window title.

Controls:
- Left mouse: orbit camera.
- Right mouse: zoom camera.
- C: enable/disable frustum culling.
- O: enable/disable octree acceleration while frustum culling is enabled.

Modes:
- C OFF: all 2500 objects are rendered.
- C ON, O OFF: every object's AABB is tested against the camera frustum.
- C ON, O ON: the octree is traversed; whole spatial nodes outside the frustum are rejected at once.

If the machine is too slow when culling is disabled, reduce GridSize in BoxApp::BuildSceneObjects(),
for example from 50 to 20 (400 objects).
