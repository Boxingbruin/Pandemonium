#include <libdragon.h>
#include <t3d/t3d.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "collision_mesh.h"
#include "character.h"
#include "dev.h"
#include "dev/debug_draw.h"

// Collision mesh data
// Exported bossroom collision can easily exceed the old tiny limits.
// Current bossroom.collision is ~300 verts / ~500 tris.
#define MAX_COLLISION_VERTICES 1024
#define MAX_COLLISION_POLYS 2048
static CollisionVertex collisionVertices[MAX_COLLISION_VERTICES];
static ColliderPoly collisionPolys[MAX_COLLISION_POLYS];
static int collisionVertexCount = 0;
static int collisionPolyCount = 0;

// Collision vertex transform (to match how the map is rendered)
static float collisionScale = 1.0f;
static float collisionTx = 0.0f;
static float collisionTy = 0.0f;
static float collisionTz = 0.0f;

// Forward decl
static void compute_plane_equation(ColliderPoly* poly, const CollisionVertex* vertices);

static void finalize_collision_planes(void)
{
    if (collisionVertexCount <= 0 || collisionPolyCount <= 0) return;

    // Mesh centroid (world space, after transform)
    float cx = 0.0f, cy = 0.0f, cz = 0.0f;
    for (int i = 0; i < collisionVertexCount; i++) {
        cx += collisionVertices[i].x;
        cy += collisionVertices[i].y;
        cz += collisionVertices[i].z;
    }
    float inv = 1.0f / (float)collisionVertexCount;
    cx *= inv; cy *= inv; cz *= inv;

    // Recompute and orient planes so the centroid is on the INSIDE side.
    // We standardize: inside => signed distance <= 0 for ALL collider types.
    for (int i = 0; i < collisionPolyCount; i++) {
        ColliderPoly *p = &collisionPolys[i];
        compute_plane_equation(p, collisionVertices);
        float dist = p->planeA * cx + p->planeB * cy + p->planeC * cz + p->planeD;
        if (dist > 0.0f) {
            p->planeA = -p->planeA;
            p->planeB = -p->planeB;
            p->planeC = -p->planeC;
            p->planeD = -p->planeD;
        }
    }
}

// Compute plane equation from three points: ax + by + cz + d = 0
static void compute_plane_equation(ColliderPoly* poly, const CollisionVertex* vertices)
{
    const CollisionVertex* v0 = &vertices[poly->v0];
    const CollisionVertex* v1 = &vertices[poly->v1];
    const CollisionVertex* v2 = &vertices[poly->v2];
    
    // Compute two edge vectors
    float edge1x = v1->x - v0->x;
    float edge1y = v1->y - v0->y;
    float edge1z = v1->z - v0->z;
    
    float edge2x = v2->x - v0->x;
    float edge2y = v2->y - v0->y;
    float edge2z = v2->z - v0->z;
    
    // Compute normal via cross product
    float nx = edge1y * edge2z - edge1z * edge2y;
    float ny = edge1z * edge2x - edge1x * edge2z;
    float nz = edge1x * edge2y - edge1y * edge2x;
    
    // Normalize
    float len = sqrtf(nx * nx + ny * ny + nz * nz);
    if (len > 0.0001f) {
        poly->planeA = nx / len;
        poly->planeB = ny / len;
        poly->planeC = nz / len;
        poly->planeD = -(poly->planeA * v0->x + poly->planeB * v0->y + poly->planeC * v0->z);
    } else {
        // Degenerate triangle
        poly->planeA = 0.0f;
        poly->planeB = 1.0f;
        poly->planeC = 0.0f;
        poly->planeD = 0.0f;
    }
}

// Helper function to add a vertex to the collision mesh
int collision_mesh_add_vertex(float x, float y, float z)
{
    if (collisionVertexCount >= MAX_COLLISION_VERTICES) {
        return -1;  // Out of space
    }
    // Transform into world space so collision matches rendered map.
    collisionVertices[collisionVertexCount].x = x * collisionScale + collisionTx;
    collisionVertices[collisionVertexCount].y = y * collisionScale + collisionTy;
    collisionVertices[collisionVertexCount].z = z * collisionScale + collisionTz;
    return collisionVertexCount++;
}

// Helper function to add a polygon to the collision mesh
bool collision_mesh_add_poly(int v0, int v1, int v2, ColliderType type)
{
    if (collisionPolyCount >= MAX_COLLISION_POLYS) {
        return false;  // Out of space
    }
    if (v0 < 0 || v0 >= collisionVertexCount ||
        v1 < 0 || v1 >= collisionVertexCount ||
        v2 < 0 || v2 >= collisionVertexCount) {
        return false;  // Invalid vertex indices
    }
    
    collisionPolys[collisionPolyCount].v0 = v0;
    collisionPolys[collisionPolyCount].v1 = v1;
    collisionPolys[collisionPolyCount].v2 = v2;
    collisionPolys[collisionPolyCount].type = type;
    
    // Compute plane equation
    compute_plane_equation(&collisionPolys[collisionPolyCount], collisionVertices);
    
    collisionPolyCount++;
    return true;
}

// Parse simple text-based collision data file
// Format: 
//   v x y z        (vertex)
//   f v0 v1 v2 type (face: vertex indices and type: FLOOR=0, WALL=1, CEILING=2)
static bool parse_collision_text(const char* filename)
{
    // dfs_* APIs expect DFS paths like "bossroom.collision".
    // Accept common asset-style prefixes too (e.g. "rom:/bossroom.collision") for convenience.
    const char *dfs_path = filename;
    if (dfs_path && strncmp(dfs_path, "rom:/", 5) == 0) {
        dfs_path += 5;
    } else if (dfs_path && strncmp(dfs_path, "rom:", 4) == 0) {
        dfs_path += 4;
        if (*dfs_path == '/') dfs_path++;
    }

    int fd = dfs_open(dfs_path);
    if (fd < 0) {
        debugf("collision: dfs_open failed for %s (dfs_path=%s fd=%d)\n", filename, dfs_path, fd);
        return false;
    }
    
    int fileSize = dfs_size(fd);
    if (fileSize <= 0 || fileSize > (512 * 1024)) {  // Allow bigger collision files
        debugf("collision: dfs_size invalid for %s (size=%d)\n", filename, fileSize);
        dfs_close(fd);
        return false;
    }
    
    // Text parsing doesn't require uncached memory; use normal malloc to avoid
    // failing due to limited uncached RAM.
    char* buffer = malloc(fileSize + 1);
    if (!buffer) {
        debugf("collision: malloc failed for %s (%d bytes)\n", filename, fileSize + 1);
        dfs_close(fd);
        return false;
    }
    
    int bytesRead = dfs_read(buffer, 1, fileSize, fd);
    dfs_close(fd);
    
    if (bytesRead != fileSize) {
        debugf("collision: dfs_read mismatch for %s (expected=%d got=%d)\n", filename, fileSize, bytesRead);
        free(buffer);
        return false;
    }
    
    buffer[fileSize] = '\0';
    
    // Parse line by line
    char* line = buffer;
    char* end = buffer + fileSize;
    
    while (line < end) {
        // Skip whitespace
        while (line < end && (*line == ' ' || *line == '\t')) line++;
        if (line >= end || *line == '\n' || *line == '\r' || *line == '#') {
            // Skip to next line
            while (line < end && *line != '\n') line++;
            if (line < end) line++;
            continue;
        }
        
        // Parse vertex: "v x y z"
        if (*line == 'v' && (line[1] == ' ' || line[1] == '\t')) {
            float x, y, z;
            if (sscanf(line, "v %f %f %f", &x, &y, &z) == 3) {
                collision_mesh_add_vertex(x, y, z);
            }
        }
        // Parse face: "f v0 v1 v2 type"
        else if (*line == 'f' && (line[1] == ' ' || line[1] == '\t')) {
            int v0, v1, v2, type;
            if (sscanf(line, "f %d %d %d %d", &v0, &v1, &v2, &type) == 4) {
                collision_mesh_add_poly(v0, v1, v2, (ColliderType)type);
            }
        }
        
        // Skip to next line
        while (line < end && *line != '\n') line++;
        if (line < end) line++;
    }
    
    free(buffer);
    finalize_collision_planes();
    return collisionPolyCount > 0;
}

// Load collision mesh from simplified model
void collision_mesh_init(void)
{
    // Reset counts
    collisionVertexCount = 0;
    collisionPolyCount = 0;
    
    // Try multiple extraction methods:
    
    // 1. Try parsing a simple text collision file (recommended workflow)
    // Single-file workflow: assets/bossroom.glb contains Object named "COLLISION"
    // Build step exports filesystem/bossroom.collision, which we load here.
    if (parse_collision_text("bossroom.collision")) {
        debugf("Loaded collision mesh from bossroom.collision\n");
        return;
    }

    // Back-compat with older filename
    if (parse_collision_text("bossroom_simple.collision")) {
        debugf("Loaded collision mesh from bossroom_simple.collision\n");
        return;
    }
    
    // If all methods fail, collision system will be empty
    if (collisionPolyCount == 0) {
        debugf("NOTE: No collision mesh loaded (bossroom.collision missing/empty)\n");
        debugf("Collision disabled - character can move through walls.\n");
        debugf("Fix: Add an Object named \"COLLISION\" to assets/bossroom.glb and rebuild.\n");
    } else {
        debugf("Collision mesh loaded: %d vertices, %d polygons\n", collisionVertexCount, collisionPolyCount);
    }
}

void collision_mesh_set_transform(float scale, float tx, float ty, float tz)
{
    collisionScale = scale;
    collisionTx = tx;
    collisionTy = ty;
    collisionTz = tz;
}

void collision_mesh_debug_draw(T3DViewport *vp)
{
    if (!vp || collisionPolyCount <= 0) return;

    // Draw as wireframe triangles in screen space via debug_draw helpers.
    // Colors: floor=green, wall=red, ceiling=magenta
    for (int i = 0; i < collisionPolyCount; i++) {
        const ColliderPoly *poly = &collisionPolys[i];

        const CollisionVertex *a = &collisionVertices[poly->v0];
        const CollisionVertex *b = &collisionVertices[poly->v1];
        const CollisionVertex *c = &collisionVertices[poly->v2];

        T3DVec3 p0 = {{ a->x, a->y, a->z }};
        T3DVec3 p1 = {{ b->x, b->y, b->z }};
        T3DVec3 p2 = {{ c->x, c->y, c->z }};

        uint16_t color = DEBUG_COLORS[0]; // default red
        if (poly->type == COLLIDER_FLOOR) color = DEBUG_COLORS[1];   // green
        else if (poly->type == COLLIDER_CEILING) color = DEBUG_COLORS[4]; // magenta

        debug_draw_tri_wire(vp, &p0, &p1, &p2, color);
    }
}

// Cleanup collision mesh system
void collision_mesh_cleanup(void)
{
    collisionVertexCount = 0;
    collisionPolyCount = 0;
}

// Check if a point is on the "inside" side of a plane (for walls, inside means negative distance)
// Check if a capsule violates a collider plane.
// Planes are oriented so interior is dist <= 0.
static bool capsule_violates_plane(
    float posX, float posY, float posZ,
    const ColliderPoly* poly,
    float localAx, float localAy, float localAz,
    float localBx, float localBy, float localBz,
    float radius,
    float scale
)
{
    // Get capsule endpoints in world space
    float ax = posX + localAx * scale;
    float ay = posY + localAy * scale;
    float az = posZ + localAz * scale;

    float bx = posX + localBx * scale;
    float by = posY + localBy * scale;
    float bz = posZ + localBz * scale;
    
    // Compute distance from capsule endpoints to plane
    float distA = poly->planeA * ax + poly->planeB * ay + poly->planeC * az + poly->planeD;
    float distB = poly->planeA * bx + poly->planeB * by + poly->planeC * bz + poly->planeD;

    // Distance along the plane normal varies linearly along the capsule segment,
    // so the segment's maximum signed distance is max(distA, distB).
    // If that max exceeds the capsule radius, the capsule is outside the allowed half-space.
    return fmaxf(distA, distB) > (radius * scale);
}

bool collision_mesh_check_bounds_capsule(
    float posX, float posY, float posZ,
    float localAx, float localAy, float localAz,
    float localBx, float localBy, float localBz,
    float radius,
    float scale
)
{
    if (collisionPolyCount == 0) return false;

    for (int i = 0; i < collisionPolyCount; i++) {
        if (collisionPolys[i].type != COLLIDER_WALL) continue;
        if (capsule_violates_plane(
                posX, posY, posZ, &collisionPolys[i],
                localAx, localAy, localAz,
                localBx, localBy, localBz,
                radius, scale
            )) {
            return true;
        }
    }
    return false;
}

// Check if character would collide with room boundaries at the given position
// Returns true if character would be outside room bounds (collision detected)
bool collision_mesh_check_bounds(float posX, float posY, float posZ)
{
    // If no collision mesh loaded, allow movement (fallback)
    if (collisionPolyCount == 0) {
        return false;
    }
    
    // Use the character capsule by default
    extern Character character;
    float sx = character.scale[0];
    return collision_mesh_check_bounds_capsule(
        posX, posY, posZ,
        character.capsuleCollider.localCapA.v[0], character.capsuleCollider.localCapA.v[1], character.capsuleCollider.localCapA.v[2],
        character.capsuleCollider.localCapB.v[0], character.capsuleCollider.localCapB.v[1], character.capsuleCollider.localCapB.v[2],
        character.capsuleCollider.radius,
        sx
    );
}

// Get collision mesh statistics
int collision_mesh_get_vertex_count(void)
{
    return collisionVertexCount;
}

int collision_mesh_get_poly_count(void)
{
    return collisionPolyCount;
}

