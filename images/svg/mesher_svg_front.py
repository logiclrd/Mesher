#!/usr/bin/env python3
"""2-D adaptation of Mesher's M22B/M23B advancing-front cap generator.

Usage:
    pip install 'shapely>=2.1'
    python mesher_svg_front.py input.svg output.svg --max-edge 12

SVG path commands are treated as STRAIGHT segments connecting their endpoints.
The original `d` attributes are also linearized in the output so curves cannot
silently disagree with the triangulated boundary. All inherited SVG transforms
remain in effect. Supports compound paths, holes, fill-rule, relative paths,
C/S/Q/T/A commands, and nested transforms. Does not triangulate text, strokes
without an area, or SVG primitives other than <path>.

Simple loops use the advancing front. Compound shapes with holes, or a simple
front that cannot advance legally, use GEOS constrained triangulation to close
all remaining area without leaks or overlaps. This is an intentional 2-D-only
safety fallback, not a port of Mesher's 3-D repair fallback.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
from math import acos, ceil, cos, degrees, hypot, isfinite, pi
from pathlib import Path
import re
import statistics
import sys
import xml.etree.ElementTree as ET

try:
    from shapely import constrained_delaunay_triangles
    from shapely.geometry import Polygon, GeometryCollection
    from shapely.geometry.polygon import orient
    from shapely.ops import unary_union
except ImportError as exc:
    raise SystemExit("Requires Shapely >=2.1: pip install 'shapely>=2.1'") from exc

SVG = "http://www.w3.org/2000/svg"
ET.register_namespace("", SVG)
NUM = re.compile(r"[+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?")
ARG_COUNT = {"M": 2, "L": 2, "H": 1, "V": 1, "C": 6,
             "S": 4, "Q": 4, "T": 2, "A": 7}
Point = tuple[float, float]
Triangle = tuple[int, int, int]


def tag_local(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


@dataclass
class Subpath:
    points: list[Point]
    closed: bool


class PathReader:
    """Read SVG's grammar (including adjacent arc flags), retaining only endpoints."""
    def __init__(self, d: str):
        self.d, self.at = d, 0

    def skip(self) -> None:
        while self.at < len(self.d) and self.d[self.at] in " \t\r\n,":
            self.at += 1

    def next_is_command(self) -> bool:
        self.skip()
        return self.at < len(self.d) and self.d[self.at].isalpha()

    def number(self) -> float:
        self.skip()
        m = NUM.match(self.d, self.at)
        if m is None:
            raise ValueError(f"expected number at character {self.at}: {self.d[self.at:self.at+22]!r}")
        self.at = m.end()
        result = float(m.group())
        if not isfinite(result):
            raise ValueError("SVG path has a nonfinite coordinate")
        return result

    def flag(self) -> int:
        self.skip()
        if self.at == len(self.d) or self.d[self.at] not in "01":
            raise ValueError(f"expected an SVG arc flag (0 or 1) at character {self.at}")
        value = int(self.d[self.at])
        self.at += 1
        return value

    def args(self, op: str) -> list[float]:
        if op == "A":
            return [self.number(), self.number(), self.number(),
                    self.flag(), self.flag(), self.number(), self.number()]
        return [self.number() for _ in range(ARG_COUNT[op])]


def read_subpaths(d: str) -> list[Subpath]:
    r = PathReader(d)
    paths: list[Subpath] = []
    pts: list[Point] = []
    current: Point = (0.0, 0.0)
    start: Point = (0.0, 0.0)
    command: str | None = None

    def finish(closed: bool) -> None:
        nonlocal pts
        if pts:
            if closed and len(pts) > 1 and pts[0] == pts[-1]:
                pts.pop()
            paths.append(Subpath(pts, closed))
        pts = []

    while True:
        r.skip()
        if r.at >= len(d):
            break
        if r.next_is_command():
            command = d[r.at]
            r.at += 1
            if command not in "MmLlHhVvCcSsQqTtAaZz":
                raise ValueError(f"unsupported path command {command!r}")
            if command.upper() == "Z":
                finish(True)
                current = start
                command = None
                continue
        if command is None:
            raise ValueError(f"path is missing a drawing command at character {r.at}")
        op = command.upper()
        if op != "M" and not pts:
            raise ValueError(f"path must begin with M/m at character {r.at}")
        values = r.args(op)
        x, y = current
        if op in "MLT":
            nx, ny = values[-2:]
            end = (x + nx, y + ny) if command.islower() else (nx, ny)
        elif op == "H":
            end = (x + values[0] if command.islower() else values[0], y)
        elif op == "V":
            end = (x, y + values[0] if command.islower() else values[0])
        else:  # C, S, Q, A: the endpoint is the last pair
            nx, ny = values[-2:]
            end = (x + nx, y + ny) if command.islower() else (nx, ny)
        if op == "M":
            finish(False)
            start = end
            pts.append(end)
            command = "l" if command.islower() else "L"
        elif not pts or pts[-1] != end:
            pts.append(end)
        current = end
    finish(False)
    return paths


def fmt(v: float) -> str:
    return f"{v:.12g}"


def linear_d(subpaths: list[Subpath]) -> str:
    pieces = []
    for sub in subpaths:
        if not sub.points:
            continue
        x, y = sub.points[0]
        pieces.append(f"M {fmt(x)} {fmt(y)}")
        pieces.extend(f"L {fmt(x)} {fmt(y)}" for x, y in sub.points[1:])
        if sub.closed:
            pieces.append("Z")
    return " ".join(pieces)


def signed_area(coords: list[Point]) -> float:
    return sum(a[0]*b[1]-a[1]*b[0]
               for a, b in zip(coords, coords[1:] + coords[:1])) / 2.0


def split_long_edges(pts: list[Point], max_edge: float | None,
                     max_vertices: int) -> list[Point]:
    """Insert boundary vertices on original straight chords; never move corners."""
    if not max_edge:
        return list(pts)
    result = []
    for a, b in zip(pts, pts[1:] + pts[:1]):
        dist = hypot(b[0]-a[0], b[1]-a[1])
        count = max(1, ceil(dist/max_edge))
        for j in range(count):
            t = j/count
            result.append((a[0]*(1-t)+b[0]*t, a[1]*(1-t)+b[1]*t))
            if len(result) > max_vertices:
                raise ValueError(f"boundary exceeds --max-vertices={max_vertices}")
    return result


def polygonal_fill(subpaths: list[Subpath], fill_rule: str,
                   max_edge: float | None, max_vertices: int):
    """Honor SVG evenodd and nonzero for NONINTERSECTING nested contours."""
    rings = []
    for sp in subpaths:
        pts = list(sp.points)
        if len(pts) > 1 and pts[0] == pts[-1]:
            pts.pop()
        if len(set(pts)) < 3:
            continue
        pts = split_long_edges(pts, max_edge, max_vertices)
        ring = Polygon(pts)
        if not ring.is_valid or ring.area <= 0:
            raise ValueError("self-intersecting or degenerate path contour (cannot guarantee coverage)")
        rings.append((ring, 1 if signed_area(pts) > 0 else -1))
    rings.sort(key=lambda item: -item[0].area)
    # A nested set of rings is unambiguous; crossing contours are not.
    for i, (a, _) in enumerate(rings):
        for b, _ in rings[i+1:]:
            if a.intersects(b) and not a.contains(b):
                raise ValueError("intersecting/touching contours within one path are unsupported")
    geometry = GeometryCollection()
    previous = []
    for ring, sign in rings:
        inside = ring.representative_point()
        enclosing = [(p, s) for p, s in previous if p.contains(inside)]
        if fill_rule == "evenodd":
            filled_outside = len(enclosing) % 2 != 0
            filled_inside = not filled_outside
        else:
            winding_outside = sum(s for _, s in enclosing)
            filled_outside = winding_outside != 0
            filled_inside = winding_outside + sign != 0
        if not filled_outside and filled_inside:
            geometry = geometry.union(ring)
        elif filled_outside and not filled_inside:
            geometry = geometry.difference(ring)
        previous.append((ring, sign))
    if not geometry.is_valid:
        raise ValueError("SVG path generated invalid filled geometry")
    return geometry


def polygons_in(geometry):
    if geometry.geom_type == "Polygon":
        if geometry.area > 0:
            yield geometry
    elif hasattr(geometry, "geoms"):
        for g in geometry.geoms:
            yield from polygons_in(g)


class VertexPool:
    def __init__(self):
        self.coords: list[Point] = []
        self.ids: dict[Point, int] = {}

    def add(self, p: Point) -> int:
        p = (float(p[0]), float(p[1]))
        if p in self.ids:
            return self.ids[p]
        i = len(self.coords)
        self.ids[p] = i
        self.coords.append(p)
        return i


class FrontMesher:
    def __init__(self, max_faces: int):
        self.nodes = VertexPool()
        self.faces: list[Triangle] = []
        self.advanced_steps = 0
        self.fallback_faces = 0
        self.max_faces = max_faces

    def tri(self, a: int, b: int, c: int) -> Triangle:
        p, q, r = (self.nodes.coords[i] for i in (a, b, c))
        cross = (q[0]-p[0])*(r[1]-p[1]) - (q[1]-p[1])*(r[0]-p[0])
        return (a, b, c) if cross >= 0 else (a, c, b)

    def fallback(self, poly: Polygon) -> None:
        """Constrained triangulation respects EVERY original hole and boundary edge."""
        if poly.is_empty or poly.area <= 0:
            return
        for triangle in constrained_delaunay_triangles(poly).geoms:
            if triangle.area <= 0:
                continue
            coords = list(triangle.exterior.coords)[:3]
            a, b, c = (self.nodes.add(p) for p in coords)
            self.faces.append(self.tri(a, b, c))
            self.fallback_faces += 1
            if len(self.faces) > self.max_faces:
                raise ValueError(f"face count exceeds --max-faces={self.max_faces}")

    def _propose(self, front: list[int], at: int, mode: int, factor: float):
        n = len(front)
        a, b, c = (self.nodes.coords[front[j % n]] for j in (at-1, at, at+1))
        ax, ay = a[0]-b[0], a[1]-b[1]
        cx, cy = c[0]-b[0], c[1]-b[1]
        la, lc = hypot(ax, ay), hypot(cx, cy)
        if min(la,lc) <= 1e-12:
            return None
        ua, uc = (ax/la, ay/la), (cx/lc, cy/lc)
        proposals = []
        if mode == 2:
            proposals = [(ua[0]+uc[0], ua[1]+uc[1])]
        elif mode == 3:
            proposals = [(2*ua[0]+uc[0], 2*ua[1]+uc[1]),
                         (ua[0]+2*uc[0], ua[1]+2*uc[1])]
        qids = []
        for dx, dy in proposals:
            norm = hypot(dx,dy)
            if norm < 1e-10:
                return None
            h = factor*min(la, lc)
            qids.append(self.nodes.add((b[0]+h*dx/norm,b[1]+h*dy/norm)))
        aid, bid, cid = (front[j % n] for j in (at-1, at, at+1))
        if mode == 1:
            triangles = [(aid,bid,cid)]
        elif mode == 2:
            triangles = [(aid,bid,qids[0]),(bid,cid,qids[0])]
        else:
            triangles = [(aid,bid,qids[0]),(bid,qids[1],qids[0]),(bid,cid,qids[1])]
        newfront = front[:at] + qids + front[at+1:]
        return newfront, triangles

    def _valid(self, before: Polygon, after_ids: list[int], tris: list[Triangle],
               tolerance: float) -> bool:
        # Proposals are *not* accepted just because they look locally plausible.
        if len(after_ids) < 3 or len(set(after_ids)) != len(after_ids):
            return False
        after = Polygon([self.nodes.coords[i] for i in after_ids])
        if not after.is_valid or after.area <= tolerance or not after.exterior.is_ccw:
            return False
        pieces = []
        for ids in tris:
            p = Polygon([self.nodes.coords[i] for i in ids])
            if not p.is_valid or p.area <= tolerance:
                return False
            if p.difference(before).area > tolerance:
                return False
            pieces.append(p)
        joined = unary_union(pieces)
        if sum(p.area for p in pieces) - joined.area > tolerance:
            return False
        if joined.intersection(after).area > tolerance:
            return False
        if before.symmetric_difference(joined.union(after)).area > tolerance:
            return False
        return True

    def advance_simple(self, poly: Polygon) -> None:
        poly = orient(poly, 1.0)
        front = [self.nodes.add(p) for p in list(poly.exterior.coords)[:-1]]
        if len(front) < 3:
            return
        if len(front) == 3:
            self.faces.append(self.tri(*front))
            return
        size = max(poly.bounds[2]-poly.bounds[0], poly.bounds[3]-poly.bounds[1])
        tol = max(poly.area*1e-12, size*size*1e-15)
        require_reduction = False
        next_target = len(front)-1
        max_attempts = max(200, self.max_faces*8)
        attempts = 0
        while len(front) > 3:
            before = Polygon([self.nodes.coords[i] for i in front])
            if not before.is_valid or not before.exterior.is_ccw:
                raise ValueError("internal error: advancing front no longer simple CCW polygon")
            corners = []
            for i in range(len(front)):
                a, b, c = (self.nodes.coords[front[j % len(front)]]
                           for j in (i-1, i, i+1))
                u = (a[0]-b[0], a[1]-b[1])
                v = (c[0]-b[0], c[1]-b[1])
                l1, l2 = hypot(*u), hypot(*v)
                if min(l1, l2) <= 1e-12:
                    continue
                angle = acos(max(-1., min(1., (u[0]*v[0]+u[1]*v[1])/(l1*l2))))
                if u[0]*v[1]-u[1]*v[0] > 0:
                    angle = 2*pi-angle
                corners.append((angle,i))
            corners.sort()
            accepted = False
            for angle, i in corners:
                if accepted or attempts >= max_attempts:
                    break
                mode = 1 if angle <= 75*pi/180 else 2 if angle <= 135*pi/180 else 3
                if require_reduction and angle < pi:
                    mode = 1
                for factor in (0.7,) if mode == 1 else (0.7,1.0,0.45):
                    attempts += 1
                    if attempts > max_attempts:
                        break
                    # New vertices from rejected proposals may remain in the pool,
                    # but will never be rendered unless referenced by faces.
                    candidate = self._propose(front,i,mode,factor)
                    if candidate is None:
                        continue
                    newfront, triangles = candidate
                    if not self._valid(before,newfront,triangles,tol):
                        continue
                    if len(self.faces)+len(triangles) > self.max_faces:
                        raise ValueError(f"face count exceeds --max-faces={self.max_faces}")
                    self.faces.extend(self.tri(*t) for t in triangles)
                    front = newfront
                    if len(front) <= next_target:
                        require_reduction = False
                        next_target = len(front)-1
                    else:
                        require_reduction = True
                    self.advanced_steps += 1
                    accepted = True
                    break
            if not accepted:
                # Fill the still-open portion exactly. Do not accept holes or
                # lock in the current failure as a successful result.
                self.fallback(before)
                return
        self.faces.append(self.tri(*front))

    def mesh(self, region) -> None:
        for poly in polygons_in(region):
            if len(poly.interiors):
                # A front with one exterior and multiple holes would require
                # synchronized disjoint boundary loops / hole bridging.
                self.fallback(poly)
            else:
                self.advance_simple(poly)
        if not self.faces:
            return
        # Verify entire filled domain, not merely each proposed front step.
        tris = [Polygon([self.nodes.coords[i] for i in f]) for f in self.faces]
        union = unary_union(tris)
        size = max((region.bounds[2]-region.bounds[0]),
                   (region.bounds[3]-region.bounds[1]),1e-12)
        tolerance = max(region.area*1e-9, size*size*1e-12)
        missing = region.difference(union).area
        extra = union.difference(region).area
        overlap = sum(t.area for t in tris) - union.area
        if max(missing, extra, overlap) > tolerance:
            raise ValueError(f"mesh coverage failed: missing={missing:.6g}, "
                             f"extra={extra:.6g}, overlap={overlap:.6g}")


def fill_rule_for(path: ET.Element, parents: dict) -> str:
    node = path
    while node is not None:
        if node.get("fill-rule"):
            return node.get("fill-rule", "nonzero").strip().lower()
        style = dict(item.split(":", 1) for item in node.get("style", "").split(";")
                     if ":" in item)
        if "fill-rule" in style:
            return style["fill-rule"].strip().lower()
        node = parents.get(node)
    return "nonzero"


def triangulation_group(path: ET.Element, mesh: FrontMesher, name: str,
                        stroke: str, dot: str, line_width: float,
                        radius: float):
    group = ET.Element(f"{{{SVG}}}g", {
        "id": name, "data-mesher-generated": "true", "fill": "none",
        "pointer-events": "none"})
    for prop in ("transform", "clip-path"):
        if path.get(prop) is not None:
            group.set(prop,path.get(prop))
    edges_group = ET.SubElement(group, f"{{{SVG}}}g", {
        "stroke": stroke, "stroke-width": fmt(line_width),
        "stroke-linecap": "round", "stroke-linejoin": "round"})
    edges = set()
    used = set()
    for face in mesh.faces:
        used.update(face)
        for a, b in ((face[0],face[1]),(face[1],face[2]),(face[2],face[0])):
            edges.add(tuple(sorted((a,b))))
    for a, b in sorted(edges):
        p, q = mesh.nodes.coords[a], mesh.nodes.coords[b]
        ET.SubElement(edges_group,f"{{{SVG}}}path", {
            "d": f"M {fmt(p[0])} {fmt(p[1])} L {fmt(q[0])} {fmt(q[1])}"})
    dots_group = ET.SubElement(group,f"{{{SVG}}}g", {"fill":dot})
    for i in sorted(used):
        x, y = mesh.nodes.coords[i]
        ET.SubElement(dots_group, f"{{{SVG}}}circle", {
            "cx":fmt(x), "cy":fmt(y), "r":fmt(radius)})
    return group, len(used), len(edges)


def process_svg(src: Path, dst: Path, args) -> None:
    tree = ET.parse(src)
    root = tree.getroot()
    parents = {child: parent for parent in root.iter() for child in parent}
    # Idempotence: remove a previous run's output, never re-triangulate its edges.
    for e, parent in list(parents.items()):
        if e.get("data-mesher-generated") == "true":
            parent.remove(e)
    parents = {child: parent for parent in root.iter() for child in parent}
    skip_ancestors = {"defs","clipPath","mask","marker","pattern","symbol"}
    paths = []
    for p in root.iter():
        if tag_local(p.tag) != "path":
            continue
        a = parents.get(p)
        skip = False
        while a is not None:
            if tag_local(a.tag) in skip_ancestors:
                skip = True
                break
            a = parents.get(a)
        if not skip:
            paths.append(p)
    count = 0
    for index, path in enumerate(paths, 1):
        d = path.get("d", "")
        if not d.strip():
            continue
        title = path.get("id", f"path-{index}")
        try:
            subpaths = read_subpaths(d)
            rule = fill_rule_for(path,parents)
            if rule not in ("evenodd","nonzero"):
                raise ValueError(f"unsupported fill-rule: {rule}")
            region = polygonal_fill(subpaths,rule,args.max_edge,args.max_vertices)
            if region.is_empty or region.area == 0:
                print(f"{title}: no polygonal area, skipped",file=sys.stderr)
                continue
            mesher = FrontMesher(args.max_faces)
            mesher.mesh(region)
        except Exception as exc:
            raise ValueError(f"{src}: <path id={title!r}>: {exc}") from exc
        if not args.preserve_source:
            path.set("d",linear_d(subpaths))
        group, vertices, edges = triangulation_group(
            path,mesher,f"mesher-mesh-{index}",args.stroke,args.dot,
            args.line_width,args.dot_radius)
        parent = parents[path]
        parent.insert(list(parent).index(path)+1,group)
        count += 1
        print(f"{title}: {vertices} vertices, {edges} edges, {len(mesher.faces)} triangles, "
              f"{mesher.advanced_steps} advancing steps, "
              f"{mesher.fallback_faces} CDT fallback triangles")
    dst.parent.mkdir(parents=True,exist_ok=True)
    tree.write(dst,encoding="utf-8",xml_declaration=True)
    print(f"Wrote {dst} ({count} meshed paths)")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("input",type=Path,help="source SVG")
    parser.add_argument("output",type=Path,help="triangulated SVG (may be same file)")
    parser.add_argument("--max-edge",type=float,default=None,
                        help="subdivide straight boundary edges longer than this (SVG units)")
    parser.add_argument("--max-vertices",type=int,default=2000,
                        help="boundary vertex safety limit per contour (default: 2000)")
    parser.add_argument("--max-faces",type=int,default=10000,
                        help="face/operation safety limit per path (default: 10000)")
    parser.add_argument("--line-width",type=float,default=1.3)
    parser.add_argument("--dot-radius",type=float,default=1.8)
    parser.add_argument("--stroke",default="#49cbe3")
    parser.add_argument("--dot",default="#ffffff")
    parser.add_argument("--preserve-source",action="store_true",
                        help="keep original Bezier/arc commands (mesh still uses chords)")
    args = parser.parse_args()
    if args.max_edge is not None and args.max_edge <= 0:
        parser.error("--max-edge must be positive")
    if args.max_vertices < 3 or args.max_faces < 1 or args.line_width <= 0 or args.dot_radius <= 0:
        parser.error("limits, line width, and circle radius must be positive")
    try:
        process_svg(args.input,args.output,args)
    except (ValueError,ET.ParseError,OSError) as exc:
        parser.exit(1,f"error: {exc}\n")


if __name__ == "__main__":
    main()
