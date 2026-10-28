class MetroRegressionPortalLinkPersists extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);

		local report = MetroTest("metro_regression_portal_link_persists", null, 510008);
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();
		local metro = MetroTest.MetroLib(build);

		try {
			local area = build.find_buildable_rectangle(32, 8);
			report.check_true("build_area_found", area != null, "Expected a clear area for portal connectivity regression.");

			local company = companies.ensure_company();
			if (area != null && company != GSCompany.COMPANY_INVALID) {
				local x = build.tile_x(area);
				local y = build.tile_y(area) + 3;
				local depth = -1;

				local portal_a = build.make_tile(x + 8, y);
				local portal_b = build.make_tile(x + 14, y);
				local signal_tile = build.make_tile(x + 11, y);

				local company_mode = GSCompanyMode(company);

				report.check_true("left_surface_built", build.build_surface_x(x + 3, x + 7, y), "Left surface approach should be built.");
				report.check_true("right_surface_built", build.build_surface_x(x + 15, x + 19, y), "Right surface approach should be built.");
				report.check_true("portal_a_built", metro.build_portal(portal_a, GSMetro.DIAGDIR_SW, depth), "Entry portal should be built.");
				report.check_true("portal_b_built", metro.build_portal(portal_b, GSMetro.DIAGDIR_NE, depth), "Exit portal should be built.");
				report.check_true("underground_chain_built", metro.build_underground_x(x + 9, x + 13, y, depth), "Underground connecting chain should be built.");
				report.check_true("initial_connection", GSMetro.HasPortalConnection(portal_a, portal_b, depth), "Portals should be connected before signal changes.");

				/* The default signal faces westbound traffic (B -> A): a one-way signal
				 * against A -> B closes that way, the other stays open. Turned round
				 * (built again) it lets A -> B through and closes B -> A. */
				report.check_true("signal_built", GSMetro.BuildUndergroundSignal(signal_tile, GSRail.RAILTRACK_NE_SW, depth), "Underground signal should be built on the linked chain.");
				report.check_true("connection_against_signal_closed", !GSMetro.HasPortalConnection(portal_a, portal_b, depth), "A one-way signal against A -> B must close that connection.");
				report.check_true("connection_with_signal", GSMetro.HasPortalConnection(portal_b, portal_a, depth), "Portal connection B -> A should remain valid while the signal faces that way.");
				report.check_true("signal_turned", GSMetro.BuildUndergroundSignal(signal_tile, GSRail.RAILTRACK_NE_SW, depth), "Building again turns the signal round.");
				report.check_true("connection_along_signal", GSMetro.HasPortalConnection(portal_a, portal_b, depth) && !GSMetro.HasPortalConnection(portal_b, portal_a, depth), "Turned round, the signal lets A -> B through and closes B -> A.");
				report.check_true("signal_removed", GSMetro.RemoveUndergroundSignal(signal_tile, GSRail.RAILTRACK_NE_SW, depth), "Underground signal should be removable.");
				report.check_true("connection_after_remove", GSMetro.HasPortalConnection(portal_a, portal_b, depth) && GSMetro.HasPortalConnection(portal_b, portal_a, depth), "Both connections should be valid after removing the signal.");

				report.metric("portal_depth", GSMetro.GetMetroDepth(portal_a));
				report.metric("signal_tile", signal_tile);

				company_mode = null;
			}
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.finish();
	}
}
