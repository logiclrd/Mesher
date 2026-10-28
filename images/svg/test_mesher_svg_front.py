"""Positive geometry invariants for the 2-D SVG advancing-front capper."""
import unittest
from shapely.geometry import Polygon
from shapely.ops import unary_union
from mesher_svg_front import (read_subpaths, polygonal_fill, FrontMesher,
                              split_long_edges, linear_d)


class FrontTests(unittest.TestCase):
    def check_meshing(self, d, rule='nonzero', max_edge=None):
        paths = read_subpaths(d)
        region = polygonal_fill(paths, rule, max_edge, 2000)
        mesh = FrontMesher(10000)
        mesh.mesh(region)  # Checks missing area, excess area, and overlapping faces.
        self.assertGreater(len(mesh.faces), 0)
        for face in mesh.faces:
            self.assertEqual(len(set(face)),3)
            self.assertGreater(Polygon([mesh.nodes.coords[i] for i in face]).area,0)
        return mesh, region

    def test_original_advancing_front_creates_interior_points(self):
        mesh, _ = self.check_meshing('M0 0 L100 0 100 100 0 100 Z')
        self.assertGreater(mesh.advanced_steps,0)
        self.assertEqual(mesh.fallback_faces,0)

    def test_strongly_concave_m_and_boundary_splits(self):
        d = 'M19 213 L19 27 60 27 120 104 180 27 221 27 221 213 179 213 179 91 120 169 61 91 61 213 Z'
        mesh, _ = self.check_meshing(d,max_edge=22)
        self.assertGreater(mesh.advanced_steps,40)
        self.assertEqual(mesh.fallback_faces,0)

    def test_evenodd_hole(self):
        mesh, region = self.check_meshing(
            'M0 0H100V100H0Z M20 20H80V80H20Z', 'evenodd', 13)
        self.assertAlmostEqual(region.area,6400)
        self.assertGreater(mesh.fallback_faces,0)

    def test_nonzero_nested_same_orientation_is_filled(self):
        _, region = self.check_meshing('M0 0H100V100H0Z M20 20H80V80H20Z')
        self.assertAlmostEqual(region.area,10000)

    def test_nonzero_nested_reversed_is_hole(self):
        _, region = self.check_meshing('M0 0H100V100H0Z M20 20V80H80V20Z')
        self.assertAlmostEqual(region.area,6400)

    def test_bezier_and_arc_endpoints_are_linearized(self):
        d = 'M 10 10 C 12 50 40 12 90 10 A 20 30 0 01 90 90 Q 50 99 10 90 Z'
        subpaths = read_subpaths(d)
        self.assertEqual(subpaths[0].points,[(10,10),(90,10),(90,90),(10,90)])
        self.assertNotIn('C',linear_d(subpaths))
        self.assertNotIn('A',linear_d(subpaths))
        self.check_meshing(d)

    def test_relative_smooth_commands(self):
        paths = read_subpaths('m 10 10 q 5 9 10 0 t 20 0 s 3 2 10 20 l -40 0 z')
        self.assertEqual(paths[0].points,[(10,10),(20,10),(40,10),(50,30),(10,30)])
        self.check_meshing('m 10 10 q 5 9 10 0 t 20 0 s 3 2 10 20 l -40 0 z')

    def test_multiple_separate_contours(self):
        mesh, region = self.check_meshing('M0 0H30V30H0Z M50 0H80V30H50Z')
        self.assertAlmostEqual(region.area,1800)
        self.assertGreater(len(mesh.faces),0)

if __name__ == '__main__':
    unittest.main()
