/**
 * metro_portal_slope_direction
 *
 * A portal on an incline faces the way the incline does. Asked for a
 * direction across the incline, the portal was built facing the incline all
 * the same and the command succeeded: the line it was meant to end never
 * reached it, trains dived into the tunnel and found no way out.
 *
 *   lane Y: dive portal (x, y) facing SE, tunnel (x, y+1 .. y+5), end (x, y+6)
 *   lane X: dive portal (x+4, y+9) facing SW, tunnel x+5 .. x+8, end (x+9, y+9)
 *
 * The end of lane Y is an incline rising to the NE: a portal facing NW there
 * (along the lane) is refused. A portal facing NW on an incline rising to the
 * NW is built and joins the lane. The end of lane X is an incline rising to the
 * NE too: a portal asked to face SW (same axis, the other way) is built facing
 * the incline, as before, and joins lane X.
 */
class MetroPortalSlopeDirection extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);
		local report = MetroTest("metro_portal_slope_direction", null, 510132);
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();
		local metro = MetroTest.MetroLib(build);
		try {
			local area = build.find_buildable_rectangle(16, 14);
			report.check_true("build_area_found", area != null, "area");
			local company = companies.ensure_company();
			local cm = company == GSCompany.COMPANY_INVALID ? null : GSCompanyMode(company);
			if (area != null && cm != null) report.check_true("area_level", build.level_area_min_height(area, 16, 14, 1), "level the area");
			if (area != null && company != GSCompany.COMPANY_INVALID) {
				local x = build.tile_x(area) + 2;
				local y = build.tile_y(area) + 1;
				local d = -1;
				local T = function(ax, ay) : (build) { return build.make_tile(ax, ay); };

				/* Lane Y. */
				local dive_y = T(x, y);
				local end_y = T(x, y + 6);
				report.check_true("lane_y_dive", GSMetro.BuildPortal(dive_y, GSMetro.DIAGDIR_SE, d), "dive portal: " + GSError.GetLastErrorString());
				report.check_true("lane_y_tunnel", metro.build_underground_y(x, y + 1, y + 5, d), "tunnel: " + GSError.GetLastErrorString());
				report.check_true("lane_y_incline_ne", metro.ensure_portal_slope(end_y, GSMetro.DIAGDIR_NE), "incline rising to the NE");

				local built = GSMetro.BuildPortal(end_y, GSMetro.DIAGDIR_NW, d);
				local err = GSError.GetLastError();
				report.check_true("across_incline_refused", !built, "A portal facing NW on an incline rising to the NE must be refused.");
				report.check_true("across_incline_error", built || err == GSError.ERR_LAND_SLOPED_WRONG, "Refused as land sloped in the wrong direction (" + GSError.GetLastErrorString() + ").");
				report.check_true("across_incline_no_portal", !GSMetro.IsPortalTile(end_y), "No portal was built across the incline.");
				report.check_true("across_incline_not_linked", !GSMetro.HasPortalConnection(dive_y, end_y, d), "The dive portal is not linked to a portal across the line.");
				built = GSMetro.BuildPortal(end_y, GSMetro.DIAGDIR_SE, d);
				report.check_true("across_incline_refused_se", !built && !GSMetro.IsPortalTile(end_y), "A portal facing SE on an incline rising to the NE must be refused too.");

				/* The same end on an incline along the lane. */
				report.check_true("lane_y_level", GSTile.LevelTiles(end_y, T(x + 1, y + 7)) || GSTile.GetSlope(end_y) == GSTile.SLOPE_FLAT, "level the end tile");
				report.check_true("lane_y_incline_nw", metro.ensure_portal_slope(end_y, GSMetro.DIAGDIR_NW), "incline rising to the NW (slope " + GSTile.GetSlope(end_y) + ")");
				report.check_true("along_incline_built", GSMetro.BuildPortal(end_y, GSMetro.DIAGDIR_NW, d), "A portal facing NW on an incline rising to the NW is built (" + GSError.GetLastErrorString() + ").");
				report.check_true("along_incline_linked", GSMetro.HasPortalConnection(dive_y, end_y, d), "The dive portal leads out of the portal on the incline.");

				/* Lane X: the other direction along the incline's axis. */
				local xy = y + 9;
				local dive_x = T(x + 4, xy);
				local end_x = T(x + 9, xy);
				report.check_true("lane_x_dive", GSMetro.BuildPortal(dive_x, GSMetro.DIAGDIR_SW, d), "dive portal: " + GSError.GetLastErrorString());
				report.check_true("lane_x_tunnel", metro.build_underground_x(x + 5, x + 8, xy, d), "tunnel: " + GSError.GetLastErrorString());
				report.check_true("lane_x_incline_ne", metro.ensure_portal_slope(end_x, GSMetro.DIAGDIR_NE), "incline rising to the NE");
				report.check_true("same_axis_built", GSMetro.BuildPortal(end_x, GSMetro.DIAGDIR_SW, d), "A portal asked to face SW on an incline rising to the NE is built facing the incline (" + GSError.GetLastErrorString() + ").");
				report.check_true("same_axis_linked", GSMetro.HasPortalConnection(dive_x, end_x, d), "The dive portal leads out of it.");
				cm = null;
			}
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}
		report.finish();
	}
}
