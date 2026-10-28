class MetroSurfaceToUndergroundLink extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);

		local report = MetroTest("metro_surface_to_underground_link", null, 510003);
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();
		local metro = MetroTest.MetroLib(build);

		try {
			local area = build.find_buildable_rectangle(32, 8);
			report.check_true("build_area_found", area != null, "Expected a clear area for surface-to-underground linking.");

			local company = companies.ensure_company();
			if (area != null && company != GSCompany.COMPANY_INVALID) {
				local x = build.tile_x(area);
				local y = build.tile_y(area) + 3;
				local depth = -1;

				local portal_a = build.make_tile(x + 8, y);
				local portal_b = build.make_tile(x + 14, y);

				local company_mode = GSCompanyMode(company);

				report.check_true("left_surface_built", build.build_surface_x(x + 3, x + 7, y), "Left surface approach should be built.");
				report.check_true("right_surface_built", build.build_surface_x(x + 15, x + 19, y), "Right surface approach should be built.");
				report.check_true("portal_a_built", metro.build_portal(portal_a, GSMetro.DIAGDIR_SW, depth), "Entry portal should be built.");
				report.check_true("portal_b_built", metro.build_portal(portal_b, GSMetro.DIAGDIR_NE, depth), "Exit portal should be built.");
				report.check_true("underground_chain_built", metro.build_underground_x(x + 9, x + 13, y, depth), "Underground connecting chain should be built.");

				report.check_true("portal_a_visible", GSMetro.IsPortalTile(portal_a), "Entry tile should contain a portal.");
				report.check_true("portal_b_visible", GSMetro.IsPortalTile(portal_b), "Exit tile should contain a portal.");
				report.check_true("surface_to_underground_connected", GSMetro.HasPortalConnection(portal_a, portal_b, depth), "Portals should be connected by underground track.");

				report.metric("portal_depth", GSMetro.GetMetroDepth(portal_a));

				company_mode = null;
			}
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.finish();
	}
}
