/*
 * Layout (single row y):
 *
 *   [portal A]--UG(-1) x3--[portal B]      [plain]
 *
 * Neither the surface rail tools nor the underground rail tool may add a track
 * to a portal tile beyond the portal's own axis track; the axis track stays.
 * Plain tiles are unaffected.
 */
class MetroPortalRules extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);

		local report = MetroTest("metro_portal_rules", null, 510077);
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();
		local metro = MetroTest.MetroLib(build);

		try {
			local area = build.find_buildable_rectangle(20, 6);
			report.check_true("build_area_found", area != null, "Expected a clear area for the test layout.");

			local company = companies.ensure_company();
			local company_mode = company == GSCompany.COMPANY_INVALID ? null : GSCompanyMode(company);

			if (area != null && company_mode != null) {
				local x = build.tile_x(area) + 2;
				local y = build.tile_y(area) + 2;
				local depth = -1;

				local portal_a = build.make_tile(x, y);
				local portal_b = build.make_tile(x + 4, y);
				local plain_tile = build.make_tile(x + 9, y);

				report.check_true("portal_a_built", metro.build_portal(portal_a, GSMetro.DIAGDIR_SW, depth), "Portal A should be built.");
				report.check_true("portal_b_built", metro.build_portal(portal_b, GSMetro.DIAGDIR_NE, depth), "Portal B should be built.");
				report.check_true("underground_built", metro.build_underground_x(x + 1, x + 3, y, depth), "Underground track should be built.");
				report.check_true("portal_link", GSMetro.HasPortalConnection(portal_a, portal_b, depth), "Portals should be connected underground.");
				report.check_eq("axis_track_built", GSRail.RAILTRACK_NE_SW, GSRail.GetRailTracks(portal_a), "Portal A should carry its axis track only.");

				/* Surface: perpendicular and diagonal pieces are refused. */
				report.check_true("surface_perpendicular_refused", !GSRail.BuildRailTrack(portal_a, GSRail.RAILTRACK_NW_SE), "A perpendicular track on a portal tile must fail.");
				report.check_true("surface_diagonal_refused", !GSRail.BuildRailTrack(portal_a, GSRail.RAILTRACK_NW_NE), "A diagonal track on a portal tile must fail.");
				report.check_true("surface_diagonal2_refused", !GSRail.BuildRailTrack(portal_a, GSRail.RAILTRACK_SW_SE), "A diagonal track on a portal tile must fail.");
				/* A drag across the portal tile perpendicular to its axis does not lay track on it. */
				GSRail.BuildRail(build.make_tile(x, y - 2), build.make_tile(x, y - 1), build.make_tile(x, y + 2));
				report.check_eq("axis_track_kept", GSRail.RAILTRACK_NE_SW, GSRail.GetRailTracks(portal_a), "The portal tile must keep only its axis track.");

				/* Underground, at the portal's level: only the axis is accepted (as a no-op). */
				report.check_true("underground_perpendicular_refused", !GSMetro.BuildUndergroundRail(portal_a, GSRail.RAILTRACK_NW_SE, depth), "A perpendicular underground track on the portal must fail.");
				report.check_true("underground_diagonal_refused", !GSMetro.BuildUndergroundRail(portal_a, GSRail.RAILTRACK_NE_SE, depth), "A diagonal underground track on the portal must fail.");
				report.check_eq("portal_slice_kept", GSMetro.TK_PORTAL, GSMetro.GetUndergroundTileKind(portal_a, depth), "The portal slice must remain.");
				report.check_true("link_kept", GSMetro.HasPortalConnection(portal_a, portal_b, depth), "The portal link must survive the refused builds.");

				/* Plain tiles are unaffected. */
				report.check_true("plain_x_built", GSRail.BuildRailTrack(plain_tile, GSRail.RAILTRACK_NE_SW), "Plain rail should be built.");
				report.check_true("plain_y_built", GSRail.BuildRailTrack(plain_tile, GSRail.RAILTRACK_NW_SE), "A crossing track on a plain tile should be built.");
			}
			company_mode = null;
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.finish();
	}
}
