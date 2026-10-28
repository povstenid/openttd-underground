class MetroPortalLinkCurve extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);
		local report = MetroTest("metro_portal_link_curve", null, 510008);
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();
		local metro = MetroTest.MetroLib(build);
		try {
			local area = build.find_buildable_rectangle(32, 20);
			report.check_true("build_area_found", area != null, "area");
			local company = companies.ensure_company();
			if (area != null && company != GSCompany.COMPANY_INVALID) {
				local x = build.tile_x(area);
				local y = build.tile_y(area) + 3;
				local d = -1;
				local cm = GSCompanyMode(company);
				local T = function(ax, ay) : (build) { return build.make_tile(ax, ay); };

				/* Layout 1: real curve. A(x+8,y) SW -> straight -> corner NE_SE at (x+12,y)
				 * -> down (y+1..y+2) -> corner NW_SW at (x+12,y+3) -> straight -> B(x+16,y+3) NE. */
				report.check_true("s1", build.build_surface_x(x + 3, x + 7, y), "surf1");
				report.check_true("s2", build.build_surface_x(x + 17, x + 21, y + 3), "surf2");
				report.check_true("pa", metro.build_portal(T(x + 8, y), GSMetro.DIAGDIR_SW, d), "pa");
				report.check_true("pb", metro.build_portal(T(x + 16, y + 3), GSMetro.DIAGDIR_NE, d), "pb");
				report.check_true("u1", metro.build_underground_x(x + 9, x + 11, y, d), "u1");
				report.check_true("c1", GSMetro.BuildUndergroundRail(T(x + 12, y), GSRail.RAILTRACK_NE_SE, d), "c1");
				report.check_true("u2", metro.build_underground_y(x + 12, y + 1, y + 2, d), "u2");
				report.check_true("c2", GSMetro.BuildUndergroundRail(T(x + 12, y + 3), GSRail.RAILTRACK_NW_SW, d), "c2");
				report.check_true("u3", metro.build_underground_x(x + 13, x + 15, y + 3, d), "u3");
				local curve_ab = GSMetro.HasPortalConnection(T(x + 8, y), T(x + 16, y + 3), d);
				local curve_ba = GSMetro.HasPortalConnection(T(x + 16, y + 3), T(x + 8, y), d);
				report.metric("curve_connected_ab", curve_ab ? 1 : 0);
				report.metric("curve_connected_ba", curve_ba ? 1 : 0);

				/* Layout 2: phantom. A2(x+8,y2) SW -> X x+9..x+10 -> NE_SE piece at x+11
				 * (turns to +y into nothing) -> X x+12..x+15 -> B2(x+16,y2) NE. Not connected in reality. */
				local y2 = y + 9;
				report.check_true("s3", build.build_surface_x(x + 3, x + 7, y2), "surf3");
				report.check_true("s4", build.build_surface_x(x + 17, x + 21, y2), "surf4");
				report.check_true("pa2", metro.build_portal(T(x + 8, y2), GSMetro.DIAGDIR_SW, d), "pa2");
				report.check_true("pb2", metro.build_portal(T(x + 16, y2), GSMetro.DIAGDIR_NE, d), "pb2");
				report.check_true("u4", metro.build_underground_x(x + 9, x + 10, y2, d), "u4");
				report.check_true("k1", GSMetro.BuildUndergroundRail(T(x + 11, y2), GSRail.RAILTRACK_NE_SE, d), "k1");
				report.check_true("u5", metro.build_underground_x(x + 12, x + 15, y2, d), "u5");
				local phantom_ab = GSMetro.HasPortalConnection(T(x + 8, y2), T(x + 16, y2), d);
				local phantom_ba = GSMetro.HasPortalConnection(T(x + 16, y2), T(x + 8, y2), d);
				report.metric("phantom_connected_ab", phantom_ab ? 1 : 0);
				report.metric("phantom_connected_ba", phantom_ba ? 1 : 0);

				/* Control: straight pair. */
				local y3 = y + 14;
				report.check_true("s5", build.build_surface_x(x + 3, x + 7, y3), "surf5");
				report.check_true("s6", build.build_surface_x(x + 17, x + 21, y3), "surf6");
				report.check_true("pa3", metro.build_portal(T(x + 8, y3), GSMetro.DIAGDIR_SW, d), "pa3");
				report.check_true("pb3", metro.build_portal(T(x + 16, y3), GSMetro.DIAGDIR_NE, d), "pb3");
				report.check_true("u6", metro.build_underground_x(x + 9, x + 15, y3, d), "u6");
				report.metric("straight_connected_ab", GSMetro.HasPortalConnection(T(x + 8, y3), T(x + 16, y3), d) ? 1 : 0);
				report.check_true("curve_linked", curve_ab && curve_ba, "Portals joined by a curved tunnel must be registered as connected both ways.");
				report.check_true("no_phantom_link", !phantom_ab && !phantom_ba, "A curve piece turning away from the line must not count as a connection.");
				report.check_true("straight_linked", GSMetro.HasPortalConnection(T(x + 8, y3), T(x + 16, y3), d), "A straight tunnel is connected.");
				cm = null;
			}
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}
		report.finish();
	}
}
