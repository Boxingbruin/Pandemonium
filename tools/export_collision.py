#!/usr/bin/env python3
"""
Export collision geometry from a .glb into a simple text format:

v x y z
f i0 i1 i2 type

Only nodes (scene objects) whose name is exactly "COLLISION" are included.
Faces are written as triangles.
"""

import argparse
import sys

import numpy as np
import trimesh

COLLISION_NODE_NAME = "COLLISION"


def write_collision(path_out: str, vertices: np.ndarray, faces: np.ndarray, face_types: np.ndarray) -> None:
    # vertices: (N, 3) float
    # faces: (M, 3) int
    with open(path_out, "w", encoding="utf-8") as f:
        f.write("# exported collision mesh\n")
        f.write("# v x y z\n")
        f.write("# f i0 i1 i2 type  (type: 0=floor 1=wall 2=ceiling)\n\n")

        for v in vertices:
            f.write(f"v {v[0]:.6f} {v[1]:.6f} {v[2]:.6f}\n")

        f.write("\n")
        for tri, t in zip(faces, face_types):
            f.write(f"f {int(tri[0])} {int(tri[1])} {int(tri[2])} {int(t)}\n")


def classify_faces(vertices: np.ndarray, faces: np.ndarray, threshold: float = 0.7) -> np.ndarray:
    """
    Classify triangles based on normal Y:
      ny > +threshold -> floor (0)
      ny < -threshold -> ceiling (2)
      else -> wall (1)
    """
    v0 = vertices[faces[:, 0]]
    v1 = vertices[faces[:, 1]]
    v2 = vertices[faces[:, 2]]
    e1 = v1 - v0
    e2 = v2 - v0
    n = np.cross(e1, e2)
    lens = np.linalg.norm(n, axis=1)
    # Avoid division by zero; degenerate -> wall
    ny = np.zeros(len(faces), dtype=np.float64)
    ok = lens > 1e-12
    ny[ok] = n[ok, 1] / lens[ok]

    out = np.full(len(faces), 1, dtype=np.int64)  # wall by default
    out[ny > threshold] = 0  # floor
    out[ny < -threshold] = 2  # ceiling
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("input_glb", help="Input .glb (can contain render + collision)")
    ap.add_argument("output_collision", help="Output .collision text file")
    ap.add_argument("--name", default=COLLISION_NODE_NAME, help='Node name to export (default: "COLLISION")')
    ap.add_argument("--weld-eps", type=float, default=1e-6, help="Vertex weld epsilon in model units")
    ap.add_argument(
        "--type",
        type=int,
        default=-1,
        help="Face type: 0=floor 1=wall 2=ceiling. Use -1 to auto-classify by normal (default).",
    )
    args = ap.parse_args()

    # Load as a scene so we can pick nodes by name
    scene_or_mesh = trimesh.load(args.input_glb, force="scene")
    if not isinstance(scene_or_mesh, trimesh.Scene):
        print(
            "Expected a Scene; got a single mesh. Put COLLISION as a named node/object in the glb.",
            file=sys.stderr,
        )
        return 2

    scene: trimesh.Scene = scene_or_mesh

    # Find geometries referenced by nodes named COLLISION.
    #
    # NOTE: In recent trimesh, `scene.graph.nodes_geometry` is a list of node names
    # (not a dict). The geometry name is returned by `scene.graph.get(node_name)`.
    wanted_instances = []
    for node_name in scene.graph.nodes_geometry:
        if node_name != args.name:
            continue
        T, geom_name = scene.graph.get(node_name)
        wanted_instances.append((node_name, geom_name, T))

    if not wanted_instances:
        print(f'No node named "{args.name}" found in {args.input_glb}', file=sys.stderr)
        print("Tip: In Blender, the *Object* name must be exactly COLLISION.", file=sys.stderr)
        return 3

    meshes_world = []
    for node_name, geom_name, T in wanted_instances:
        geom = scene.geometry.get(geom_name)
        if geom is None:
            continue

        m = geom.copy()
        m.apply_transform(T)

        # glTF meshes are typically already triangles. If they aren't, we fail loudly
        # rather than guessing a triangulation strategy.
        if not m.is_empty:
            if getattr(m, "faces", None) is None or len(m.faces) == 0:
                continue
            if getattr(m.faces, "shape", None) is not None and m.faces.shape[1] != 3:
                print(
                    f'Node "{node_name}" geometry "{geom_name}" is not triangulated (faces have shape {m.faces.shape}).',
                    file=sys.stderr,
                )
                print("Triangulate the COLLISION mesh before export (e.g. in Blender).", file=sys.stderr)
                return 5
            meshes_world.append(m)

    if not meshes_world:
        print("Found COLLISION node(s) but no triangle geometry to export.", file=sys.stderr)
        return 4

    combined = trimesh.util.concatenate(meshes_world)

    # Weld vertices so we don’t spam duplicate v lines.
    # (You can disable by setting weld-eps <= 0)
    V = np.asarray(combined.vertices, dtype=np.float64)
    F = np.asarray(combined.faces, dtype=np.int64)

    if args.weld_eps and args.weld_eps > 0:
        # Quantize to grid -> unique
        q = np.round(V / args.weld_eps).astype(np.int64)
        _, unique_idx, inverse = np.unique(q, axis=0, return_index=True, return_inverse=True)
        V2 = V[unique_idx]
        F2 = inverse[F]
        V, F = V2, F2

    if args.type in (0, 1, 2):
        types = np.full(len(F), int(args.type), dtype=np.int64)
    else:
        types = classify_faces(V, F)

    write_collision(args.output_collision, V, F, face_types=types)
    print(f"Wrote {len(V)} vertices, {len(F)} triangles -> {args.output_collision}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())


