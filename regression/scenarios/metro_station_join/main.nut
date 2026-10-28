/*
 * Underground platforms join stations the way vanilla rail stations do.
 *
 * Layout on row py (x grows to the right), level d = surface - 1, d2 = d - 1:
 *
 *   x     x+1   x+2   x+3          x+spread-1   x+spread
 *   A1    A2    ?     C            far ok       far refused     (level d)
 *   A4    A5                                                    (level d2)
 *   and A3 at (x, py + 2) on level d, N at (x + 1, py - 1) on level d.
 *
 *  - A2 next to A1 with STATION_JOIN_ADJACENT joins station A;
 *  - C with STATION_NEW is a separate station; the tile between A2 and C
 *    adjoins both, so STATION_JOIN_ADJACENT is refused and an explicit id decides;
 *  - A3, one tile away from A1, joins A by explicit id (distant join);
 *  - A4 on level d2 right under A1 joins A by explicit id, A5 next to A4 joins
 *    A by STATION_JOIN_ADJACENT on level d2;
 *  - joining A beyond the station spread is refused with ERR_STATION_TOO_SPREAD_OUT;
 *  - STATION_NEW next to A builds a separate station (vanilla "build separate").
 */
class MetroStationJoin extends GSController {
	report = null;
	ox = 0;
	oy = 0;

	function T(dx, dy)
	{
		return GSMap.GetTileIndex(this.ox + dx, this.oy + dy);
	}

	function station_at(tile, depth)
	{
		return GSMetro.GetUndergroundStationID(tile, depth);
	}

	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);

		this.report = MetroTest("metro_station_join", null, 510471);
		local report = this.report;
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();

		try {
			local spread = GSGameSettings.GetValue("station.station_spread");
			report.check_true("spread_sane", spread >= 4 && spread <= 40, "Station spread should be a usable value (got " + spread + ").");
			report.check_true("distant_join_default", GSGameSettings.GetValue("station.distant_join_stations") != 0, "Distant joins should be enabled by default.");

			local width = spread + 6;
			local area = build.find_buildable_rectangle(width, 10);
			report.check_true("build_area_found", area != null, "Expected a clear area for the test layout.");

			local company = companies.ensure_company();
			local company_mode = company == GSCompany.COMPANY_INVALID ? null : GSCompanyMode(company);

			if (area != null && company_mode != null) {
				report.check_true("area_levelled", build.level_area(area, width, 10), "Test area should be flat.");

				local x = build.tile_x(area) + 2;
				local py = build.tile_y(area) + 4;
				this.ox = x;
				this.oy = py;

				local depth = GSTile.GetCornerHeight(this.T(0, 0), GSTile.CORNER_N) - 1;
				local depth2 = depth - 1;
				local X = GSRail.RAILTRACK_NE_SW;

				/* 1. Side by side on the same level: one station with two platforms. */
				report.check_true("a1_built", GSMetro.BuildUndergroundStation(this.T(0, 0), X, depth, GSStation.STATION_NEW), "First platform should be built (err=" + GSError.GetLastErrorString() + ").");
				local station_a = this.station_at(this.T(0, 0), depth);
				report.check_true("a_valid", GSStation.IsValidStation(station_a), "First platform should form a station.");
				report.check_true("a2_built", GSMetro.BuildUndergroundStation(this.T(1, 0), X, depth, GSStation.STATION_JOIN_ADJACENT), "Adjacent platform should be built (err=" + GSError.GetLastErrorString() + ").");
				report.check_eq("a2_joined", station_a, this.station_at(this.T(1, 0), depth), "Adjacent platform should join the existing station.");
				report.check_eq("a_platforms_2", 2, GSMetro.GetStationComplexPlatformCount(station_a), "Station A should have two platform nodes.");

				/* 2. A separate station, then a tile adjoining both stations. */
				report.check_true("c_built", GSMetro.BuildUndergroundStation(this.T(3, 0), X, depth, GSStation.STATION_NEW), "Separate station C should be built (err=" + GSError.GetLastErrorString() + ").");
				local station_c = this.station_at(this.T(3, 0), depth);
				report.check_true("c_separate", GSStation.IsValidStation(station_c) && station_c != station_a, "STATION_NEW should build a separate station.");
				report.check_true("between_ambiguous_refused", !GSMetro.BuildUndergroundStation(this.T(2, 0), X, depth, GSStation.STATION_JOIN_ADJACENT), "Joining a tile adjoining two stations should be refused.");
				report.check_true("between_not_built", !GSStation.IsValidStation(this.station_at(this.T(2, 0), depth)), "Refused platform should not be built.");
				report.check_true("between_joined_c", GSMetro.BuildUndergroundStation(this.T(2, 0), X, depth, station_c), "Explicit id should resolve the ambiguity (err=" + GSError.GetLastErrorString() + ").");
				report.check_eq("between_station", station_c, this.station_at(this.T(2, 0), depth), "Platform between the stations should join C.");
				report.check_eq("c_platforms_2", 2, GSMetro.GetStationComplexPlatformCount(station_c), "Station C should have two platform nodes.");
				report.check_eq("a_platforms_still_2", 2, GSMetro.GetStationComplexPlatformCount(station_a), "Station A should be unchanged.");

				/* 3. Distant join: one tile gap, explicit station id. */
				report.check_true("a3_built", GSMetro.BuildUndergroundStation(this.T(0, 2), X, depth, station_a), "Distant platform should be built (err=" + GSError.GetLastErrorString() + ").");
				report.check_eq("a3_joined", station_a, this.station_at(this.T(0, 2), depth), "Platform one tile away should join A by explicit id.");
				report.check_eq("a_platforms_3", 3, GSMetro.GetStationComplexPlatformCount(station_a), "Station A should have three platform nodes.");

				/* 4. Another level right under A1, explicit id; then auto-join next to it on that level. */
				report.check_true("a4_built", GSMetro.BuildUndergroundStation(this.T(0, 0), X, depth2, station_a), "Lower level platform should be built (err=" + GSError.GetLastErrorString() + ").");
				report.check_eq("a4_joined", station_a, this.station_at(this.T(0, 0), depth2), "Platform on the lower level should join A.");
				report.check_eq("a_platforms_4", 4, GSMetro.GetStationComplexPlatformCount(station_a), "Station A should have four platform nodes.");
				report.check_eq("a_deepest_level", depth2, GSMetro.GetStationComplexDeepestLevel(station_a), "Station A should reach the lower level.");
				report.check_true("a5_built", GSMetro.BuildUndergroundStation(this.T(1, 0), X, depth2, GSStation.STATION_JOIN_ADJACENT), "Adjacent lower platform should be built (err=" + GSError.GetLastErrorString() + ").");
				report.check_eq("a5_joined", station_a, this.station_at(this.T(1, 0), depth2), "Adjacent platform on the lower level should join A.");
				report.check_eq("a_platforms_5", 5, GSMetro.GetStationComplexPlatformCount(station_a), "Station A should have five platform nodes.");

				/* 5. Station spread: A spans x..x+1, so x+spread-1 still fits, x+spread does not. */
				local far_bad = this.T(spread, 0);
				local far_ok = this.T(spread - 1, 0);
				local ok = GSMetro.BuildUndergroundStation(far_bad, X, depth, station_a);
				local err = GSError.GetLastError();
				report.check_true("spread_refused", !ok, "Joining beyond the station spread should be refused.");
				report.check_eq("spread_error", GSError.ERR_STATION_TOO_SPREAD_OUT, err, "Unexpected error (got " + GSError.GetLastErrorString() + ").");
				report.check_true("spread_not_built", !GSStation.IsValidStation(this.station_at(far_bad, depth)), "Refused platform should not be built.");
				report.check_true("spread_edge_built", GSMetro.BuildUndergroundStation(far_ok, X, depth, station_a), "Joining at the spread limit should work (err=" + GSError.GetLastErrorString() + ").");
				report.check_eq("spread_edge_joined", station_a, this.station_at(far_ok, depth), "Platform at the spread limit should join A.");

				/* 6. Build separate next to A. */
				report.check_true("n_built", GSMetro.BuildUndergroundStation(this.T(1, -1), X, depth, GSStation.STATION_NEW), "Separate platform next to A should be built (err=" + GSError.GetLastErrorString() + ").");
				local station_n = this.station_at(this.T(1, -1), depth);
				report.check_true("n_separate", GSStation.IsValidStation(station_n) && station_n != station_a && station_n != station_c, "STATION_NEW next to A should build a separate station.");

				/* 7. Without distant joins only same-level neighbours can be joined by id. */
				report.check_true("distant_off", GSGameSettings.SetValue("station.distant_join_stations", 0), "Distant join setting should be changeable.");
				report.check_true("distant_off_refused", !GSMetro.BuildUndergroundStation(this.T(0, -2), X, depth, station_a), "Distant join should be refused when disabled.");
				report.check_true("distant_off_adjacent_ok", GSMetro.BuildUndergroundStation(this.T(0, 3), X, depth, station_a), "Joining a same-level neighbour by id should still work (err=" + GSError.GetLastErrorString() + ").");
				report.check_eq("distant_off_adjacent_joined", station_a, this.station_at(this.T(0, 3), depth), "Neighbour platform should join A.");
				report.check_true("distant_on", GSGameSettings.SetValue("station.distant_join_stations", 1), "Distant join setting should be restorable.");

				report.metric("station_a", station_a);
				report.metric("station_c", station_c);
				report.metric("platforms_a", GSMetro.GetStationComplexPlatformCount(station_a));
			}

			company_mode = null;
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.finish();
	}
}
