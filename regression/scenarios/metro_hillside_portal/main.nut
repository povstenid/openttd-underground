/*
 * A tunnel mouth on a hillside sits exactly at the foot of its incline, like a
 * vanilla tunnel: on an incline whose lowest corner is h the portal may be built
 * at depth h. A flat "dive" portal must lie strictly below the surface, so
 * depth h is refused on a flat tile at height h.
 *
 * Layout (row y, X axis):
 *
 *   [flat] ... [T: SLOPE_SW, h..h+1] [ridge h+1] [ridge h+1] [ridge h+1] [E: SLOPE_NE, h..h+1]
 *
 * plus a flat tile F three rows away for the flat portal checks.
 */
class MetroHillsidePortal extends GSController {
	report = null;

	function expect_error(id, ok, expected)
	{
		local err = GSError.GetLastError();
		this.report.check_true(id + "_refused", !ok, "Command should be refused.");
		this.report.check_eq(id + "_error", expected, err, "Unexpected error code (got " + GSError.GetLastErrorString() + ").");
	}

	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);

		this.report = MetroTest("metro_hillside_portal", null, 510071);
		local report = this.report;
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();

		try {
			local area = build.find_buildable_rectangle(12, 10);
			report.check_true("build_area_found", area != null, "Expected a clear area for the hillside portal regression.");

			local company = companies.ensure_company();
			if (area != null && company != GSCompany.COMPANY_INVALID) {
				local company_mode = GSCompanyMode(company);

				report.check_true("area_levelled", build.level_area_min_height(area, 12, 10, 1), "Test area should be flat at height >= 1.");

				local x0 = build.tile_x(area) + 3;
				local y = build.tile_y(area) + 3;
				local ridge = build.build_ridge_x(x0, y, 3);
				report.check_true("ridge_built", ridge != null, "A one-level ridge should be raised.");

				if (ridge != null) {
					local h = ridge.base_z;
					local incline = ridge.start;
					local far_incline = ridge.end;
					local flat = build.make_tile(x0, y + 4);
					report.metric("h", h);
					report.metric("incline", incline);

					report.check_true("h_at_least_1", h >= 1, "The foot of the ridge should lie at height >= 1.");
					report.check_eq("incline_slope", GSTile.SLOPE_SW, GSTile.GetSlope(incline), "The mouth tile should incline up into the hill (south-west).");
					report.check_eq("incline_lowest_corner", h, GSTile.GetMinHeight(incline), "The lowest corner of the incline should be h.");
					report.check_eq("incline_highest_corner", h + 1, GSTile.GetMaxHeight(incline), "The highest corner of the incline should be h + 1.");
					report.check_true("flat_tile_flat", GSTile.GetSlope(flat) == GSTile.SLOPE_FLAT && GSTile.GetMinHeight(flat) == h, "The flat probe tile should be flat at height h.");

					/* 1. The hillside mouth at the foot of its incline. */
					report.check_true("hillside_portal_built", GSMetro.BuildPortal(incline, GSMetro.DIAGDIR_SW, h), "A portal at depth h on the incline should be built (err=" + GSError.GetLastErrorString() + ").");
					report.check_true("hillside_is_portal", GSMetro.IsPortalTile(incline), "The incline should be a portal tile.");
					report.check_eq("hillside_portal_kind", GSMetro.TK_PORTAL, GSMetro.GetUndergroundTileKind(incline, h), "The portal slice should sit at depth h.");
					report.check_true("hillside_surface_rail", GSRail.IsRailTile(incline), "The portal should build surface rail on its incline.");
					report.check_eq("hillside_surface_track", GSRail.RAILTRACK_NE_SW, GSRail.GetRailTracks(incline), "The surface rail should run along the incline.");
					this.expect_error("hillside_portal_twice", GSMetro.BuildPortal(incline, GSMetro.DIAGDIR_SW, h), GSMetro.ERR_UNDERGROUND_PORTAL_ALREADY_BUILT);

					/* 2. A flat tile at height h: depth h is the surface itself. */
					this.expect_error("flat_portal_at_surface", GSMetro.BuildPortal(flat, GSMetro.DIAGDIR_SW, h), GSMetro.ERR_UNDERGROUND_DEPTH_INVALID);
					report.check_true("flat_no_portal", !GSMetro.IsPortalTile(flat), "No portal should exist on the flat tile after the refusal.");
					report.check_true("flat_no_rail", !GSRail.IsRailTile(flat), "No surface rail should exist on the flat tile after the refusal.");
					report.check_true("flat_dive_portal_built", GSMetro.BuildPortal(flat, GSMetro.DIAGDIR_SW, h - 1), "A flat dive portal at depth h - 1 should be built (err=" + GSError.GetLastErrorString() + ").");
					report.check_eq("flat_dive_portal_kind", GSMetro.TK_PORTAL, GSMetro.GetUndergroundTileKind(flat, h - 1), "The dive portal slice should sit at depth h - 1.");

					/* 3. The far incline: above the foot is refused, below it is a dive portal. */
					report.check_eq("far_incline_slope", GSTile.SLOPE_NE, GSTile.GetSlope(far_incline), "The far end of the ridge should incline up to the north-east.");
					this.expect_error("incline_portal_above_foot", GSMetro.BuildPortal(far_incline, GSMetro.DIAGDIR_NE, h + 1), GSMetro.ERR_UNDERGROUND_DEPTH_INVALID);
					report.check_true("incline_dive_portal_built", GSMetro.BuildPortal(far_incline, GSMetro.DIAGDIR_NE, h - 1), "A portal at depth h - 1 on an incline is a dive portal and should be built (err=" + GSError.GetLastErrorString() + ").");
					report.check_true("incline_dive_is_portal", GSMetro.IsPortalTile(far_incline), "The far incline should be a portal tile.");
					report.check_eq("incline_dive_portal_kind", GSMetro.TK_PORTAL, GSMetro.GetUndergroundTileKind(far_incline, h - 1), "The dive portal slice should sit at depth h - 1.");
					report.check_eq("incline_dive_nothing_at_foot", GSMetro.TK_NONE, GSMetro.GetUndergroundTileKind(far_incline, h), "Nothing should sit at depth h under the dive portal.");
					report.check_true("incline_dive_surface_rail", GSRail.IsRailTile(far_incline), "The dive portal should build surface rail on its incline.");

					/* 4. Removing the hillside portal also removes its surface rail. */
					report.check_true("hillside_portal_removed", GSMetro.RemovePortal(incline), "Removing the hillside portal should succeed (err=" + GSError.GetLastErrorString() + ").");
					report.check_true("hillside_portal_gone", !GSMetro.IsPortalTile(incline) && GSMetro.GetUndergroundTileKind(incline, h) == GSMetro.TK_NONE, "The portal slice should be gone.");
					report.check_true("hillside_surface_rail_gone", !GSRail.IsRailTile(incline), "The surface rail of the portal should be gone.");
				}

				company_mode = null;
			}
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.finish();
	}
}
