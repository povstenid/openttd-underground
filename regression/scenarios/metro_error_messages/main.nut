/*
 * Every refused metro command must report a specific error code instead of
 * GSError.ERR_UNKNOWN, so the player gets a real message and scripts can react.
 *
 * Layout (single row y, two empty tiles between features):
 *
 *   [probe]  .  .  [portal]  .  .  [station]  .  .  [ramp tile]
 *
 * plus a separate raised tile further down for the terraform check.
 */
class MetroErrorMessages extends GSController {
	report = null;

	function expect_error(id, ok, expected)
	{
		local err = GSError.GetLastError();
		this.report.check_true(id + "_refused", !ok, "Command should be refused.");
		this.report.check_eq(id + "_error", expected, err, "Unexpected error code (got " + GSError.GetLastErrorString() + ").");
		this.report.metric(id + "_error_string", GSError.GetLastErrorString());
	}

	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);

		this.report = MetroTest("metro_error_messages", null, 510061);
		local report = this.report;
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();
		local metro = MetroTest.MetroLib(build);

		try {
			local area = build.find_buildable_rectangle(16, 10);
			report.check_true("build_area_found", area != null, "Expected a clear area for the test layout.");

			local company = companies.ensure_company();
			local company_mode = company == GSCompany.COMPANY_INVALID ? null : GSCompanyMode(company);

			if (area != null && company_mode != null) {
				report.check_true("area_levelled", build.level_area(area, 16, 10), "Test area should be flat.");

				local x = build.tile_x(area) + 1;
				local y = build.tile_y(area) + 2;

				local probe = build.make_tile(x, y);
				local portal = build.make_tile(x + 3, y);
				local station = build.make_tile(x + 6, y);
				local ramp = build.make_tile(x + 9, y);

				/* Underground slices are absolute: the level right below a surface at height h is h - 1. */
				local h = GSTile.GetCornerHeight(probe, GSTile.CORNER_N);
				local depth = h - 1;
				report.metric("surface_height", h);

				/* 1. Depth at or above the surface. */
				this.expect_error("rail_at_surface", GSMetro.BuildUndergroundRail(probe, GSRail.RAILTRACK_NE_SW, h), GSMetro.ERR_UNDERGROUND_DEPTH_INVALID);
				this.expect_error("rail_above_surface", GSMetro.BuildUndergroundRail(probe, GSRail.RAILTRACK_NE_SW, h + 1), GSMetro.ERR_UNDERGROUND_DEPTH_INVALID);
				this.expect_error("station_at_surface", GSMetro.BuildUndergroundStation(probe, GSRail.RAILTRACK_NE_SW, h, GSStation.STATION_NEW), GSMetro.ERR_UNDERGROUND_DEPTH_INVALID);

				/* 2. Depth below the deepest allowed level. */
				this.expect_error("rail_too_deep", GSMetro.BuildUndergroundRail(probe, GSRail.RAILTRACK_NE_SW, -100), GSMetro.ERR_UNDERGROUND_TOO_DEEP);

				/* 3. Removing things that are not there. */
				this.expect_error("remove_missing_rail", GSMetro.RemoveUndergroundRail(probe, GSRail.RAILTRACK_NE_SW, depth), GSRail.ERR_UNSUITABLE_TRACK);
				this.expect_error("remove_missing_station", GSMetro.RemoveUndergroundStation(probe, depth), GSMetro.ERR_UNDERGROUND_NO_STATION);
				this.expect_error("remove_missing_portal", GSMetro.RemovePortal(probe), GSMetro.ERR_UNDERGROUND_NO_PORTAL);
				this.expect_error("remove_missing_ramp", GSMetro.RemoveRamp(probe, depth), GSMetro.ERR_UNDERGROUND_NO_RAMP);
				this.expect_error("remove_missing_signal", GSMetro.RemoveUndergroundSignal(probe, GSRail.RAILTRACK_NE_SW, depth), GSRail.ERR_UNSUITABLE_TRACK);
				this.expect_error("signal_without_track", GSMetro.BuildUndergroundSignal(probe, GSRail.RAILTRACK_NE_SW, depth), GSRail.ERR_UNSUITABLE_TRACK);

				/* 4. A second portal on a portal tile; vanilla rail removal on it. */
				report.check_true("portal_built", metro.build_portal(portal, GSMetro.DIAGDIR_SW, GSTile.GetCornerHeight(portal, GSTile.CORNER_N) - 1), "Portal should be built (err=" + GSError.GetLastErrorString() + ").");
				local portal_depth = GSTile.GetCornerHeight(portal, GSTile.CORNER_N) - 1;
				this.expect_error("portal_twice", GSMetro.BuildPortal(portal, GSMetro.DIAGDIR_SW, portal_depth), GSMetro.ERR_UNDERGROUND_PORTAL_ALREADY_BUILT);
				report.check_true("portal_surface_rail", GSRail.IsRailTile(portal), "Portal should have surface rail.");
				this.expect_error("remove_portal_rail", GSRail.RemoveRailTrack(portal, GSRail.RAILTRACK_NE_SW), GSMetro.ERR_MUST_REMOVE_PORTAL_FIRST);
				report.check_true("portal_survives", GSMetro.IsPortalTile(portal) && GSRail.IsRailTile(portal), "Portal and its rail must survive.");

				/* 5. Rail through a station platform of the other axis. */
				report.check_true("station_built", metro.build_station_x(station, depth), "Underground station should be built (err=" + GSError.GetLastErrorString() + ").");
				this.expect_error("rail_across_station", GSMetro.BuildUndergroundRail(station, GSRail.RAILTRACK_NW_SE, depth), GSMetro.ERR_UNDERGROUND_SOMETHING_IN_THE_WAY);
				this.expect_error("station_twice", metro.build_station_x(station, depth), GSError.ERR_ALREADY_BUILT);
				this.expect_error("remove_rail_on_station", GSMetro.RemoveUndergroundRail(station, GSRail.RAILTRACK_NE_SW, depth), GSRail.ERR_UNSUITABLE_TRACK);

				/* 6. Ramps must join adjacent levels. */
				this.expect_error("ramp_not_adjacent", GSMetro.BuildRamp(ramp, GSMetro.DIAGDIR_SW, depth, depth - 2), GSMetro.ERR_UNDERGROUND_RAMP_NOT_ADJACENT);
				this.expect_error("ramp_same_level", GSMetro.BuildRamp(ramp, GSMetro.DIAGDIR_SW, depth, depth), GSMetro.ERR_UNDERGROUND_RAMP_NOT_ADJACENT);
				report.check_true("ramp_rail_built", GSMetro.BuildUndergroundRail(ramp, GSRail.RAILTRACK_NE_SW, depth), "Rail on the ramp tile should be built.");
				this.expect_error("ramp_over_rail", GSMetro.BuildRamp(ramp, GSMetro.DIAGDIR_SW, depth, depth - 1), GSMetro.ERR_UNDERGROUND_SOMETHING_IN_THE_WAY);
				this.expect_error("remove_ramp_on_rail", GSMetro.RemoveRamp(ramp, depth), GSMetro.ERR_UNDERGROUND_NO_RAMP);

				/* 7. Lowering land right above underground rail. */
				local tt = build.make_tile(x + 12, y + 5);
				if (GSTile.GetMinHeight(tt) < 1) GSTile.RaiseTile(tt, GSTile.SLOPE_ELEVATED);
				local th = GSTile.GetMinHeight(tt);
				report.check_true("terraform_tile_flat", GSTile.GetSlope(tt) == GSTile.SLOPE_FLAT && th >= 1, "Terraform tile should be flat at height >= 1.");
				report.check_true("terraform_rail_built", GSMetro.BuildUndergroundRail(tt, GSRail.RAILTRACK_NE_SW, th - 1), "Rail directly under the surface should be built.");
				this.expect_error("lower_over_rail", GSTile.LowerTile(tt, GSTile.SLOPE_N), GSMetro.ERR_UNDERGROUND_IN_THE_WAY);
				report.check_eq("terraform_corner_unchanged", th, GSTile.GetCornerHeight(tt, GSTile.CORNER_N), "North corner must be unchanged.");
			}
			company_mode = null;
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.finish();
	}
}
