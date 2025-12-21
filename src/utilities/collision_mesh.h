#ifndef COLLISION_MESH_H
#define COLLISION_MESH_H

#include <stdbool.h>
#include <t3d/t3d.h>

// Collision mesh structures
typedef struct {
    float x, y, z;
} CollisionVertex;

typedef enum {
    COLLIDER_FLOOR,
    COLLIDER_WALL,
    COLLIDER_CEILING
} ColliderType;

typedef struct {
    int v0, v1, v2;   // indices into vertex array
    ColliderType type;
    // Plane equation: ax + by + cz + d = 0
    float planeA, planeB, planeC, planeD;
} ColliderPoly;

// Initialize collision mesh system
void collision_mesh_init(void);

// Cleanup collision mesh system
void collision_mesh_cleanup(void);

// Check if a position would collide with room boundaries
// Returns true if character would be outside room bounds (collision detected)
bool collision_mesh_check_bounds(float posX, float posY, float posZ);

// Capsule variant (for other entities like the boss).
// localCapA/B and radius are in the entity's local units; scale is the entity scale applied to those values.
bool collision_mesh_check_bounds_capsule(
    float posX, float posY, float posZ,
    float localAx, float localAy, float localAz,
    float localBx, float localBy, float localBz,
    float radius,
    float scale
);

// Debug rendering: draw collision mesh wireframe on screen
void collision_mesh_debug_draw(T3DViewport *vp);

// Apply a transform to collision vertices as they are loaded.
// Use this to match the world transform you render the level with (e.g. mapMatrix scale/translate).
void collision_mesh_set_transform(float scale, float tx, float ty, float tz);

// Manual population helpers (for defining collision geometry programmatically)
int collision_mesh_add_vertex(float x, float y, float z);
bool collision_mesh_add_poly(int v0, int v1, int v2, ColliderType type);

// Get collision mesh statistics
int collision_mesh_get_vertex_count(void);
int collision_mesh_get_poly_count(void);

#endif

