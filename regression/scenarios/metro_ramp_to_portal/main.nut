/**
 * metro_ramp_to_portal
 *
 * A ramp up from the level just below flat ground would end on the surface:
 * it is built as a portal there, turned back into the tunnel, with its surface
 * rail, so the line leaves for the surface. A ramp up between two underground
 * levels stays a ramp, and on an incline the ramp is refused as before.
 */
class MetroRampToPortal extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);

		local report = MetroTest("metro_ramp_to_portal", null, 510035);
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();
		local metro = MetroTest.MetroLib(build);

		try {
			local area = build.find_buildable_rectangle(24, 10);
			report.check_true("build_area_found", area != null, "Expected a clear area.");
			local company = companies.ensure_company();
			if (area != null && company != GSCompany.COMPANY_INVALID) {
				local x = build.tile_x(area) + 2;
				local y = build.tile_y(area) + 2;
				local cm = GSCompanyMode(company);
				report.check_true("levelled", build.level_area_min_height(build.make_tile(x, y), 14, 5, 2), "Flat ground at height 2 or more.");
				local z = GSTile.GetMinHeight(build.make_tile(x, y));
				report.metric("surface", z);

				/* Line just below the surface; the ramp up would end at the surface. */
				local d = z - 1;
				local ramp_tile = build.make_tile(x + 4, y);
				report.check_true("line_built", metro.build_underground_x(x, x + 3, y, d), "Line one level below the surface.");
				report.check_true("ramp_up_built", GSMetro.BuildRamp(ramp_tile, GSMetro.DIAGDIR_SW, d, d + 1), "Ramp up to the surface: " + GSError.GetLastErrorString());
				report.check_eq("became_portal", GSMetro.TK_PORTAL, GSMetro.GetUndergroundTileKind(ramp_tile, d), "The ramp should be built as a portal.");
				report.check_true("portal_tile", GSMetro.IsPortalTile(ramp_tile), "The tile should be a portal tile.");
				report.check_true("portal_surface_rail", GSRail.IsRailTile(ramp_tile), "The portal should have its surface rail.");
				report.check_true("surface_continues", GSRail.BuildRailTrack(build.make_tile(x + 5, y), GSRail.RAILTRACK_NE_SW), "The line continues on the surface.");

				/* Between two underground levels the ramp stays a ramp. */
				local d2 = z - 3;
				local y2 = y + 2;
				local ramp2 = build.make_tile(x + 4, y2);
				{
					report.check_true("deep_line_built", metro.build_underground_x(x, x + 3, y2, d2), "Deeper line.");
					report.check_true("deep_ramp_built", GSMetro.BuildRamp(ramp2, GSMetro.DIAGDIR_SW, d2, d2 + 1), "Ramp between underground levels: " + GSError.GetLastErrorString());
					report.check_eq("deep_ramp_kind", GSMetro.TK_RAMP, GSMetro.GetUndergroundTileKind(ramp2, d2), "It should stay a ramp.");
				}

				/* On an incline: no portal, the ramp is refused as before. */
				local y3 = y + 4;
				local slope_tile = build.make_tile(x + 4, y3);
				report.check_true("line3_built", metro.build_underground_x(x, x + 3, y3, d), "Third line.");
				GSTile.RaiseTile(slope_tile, GSTile.SLOPE_S);
				report.check_true("ramp_on_slope_refused", !GSMetro.BuildRamp(slope_tile, GSMetro.DIAGDIR_SW, d, d + 1), "No portal on a sloped tile.");
				report.check_true("no_portal_on_slope", !GSMetro.IsPortalTile(slope_tile), "No portal on a sloped tile.");
				cm = null;
			}
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.finish();
	}
}
