/*
 * An underground station only catches passengers and cargo through the exits
 * the player builds (pavilions, underpasses); platforms catch nothing.
 *
 * A platform is built under the centre of a town. Without an exit:
 *  - the station exists (valid, with a location for its sign) for 60 days and
 *    is not removed as "unused";
 *  - its coverage and acceptance are empty;
 *  - no passengers appear at it (order.selectgoods is off, so cargo would be
 *    delivered to any station whose catchment reaches the houses).
 * Control: after building an exit next to the platform the coverage is no
 * longer empty and passengers do appear.
 */
class MetroNoExitNoCatchment extends GSController {
	report = null;
	build = null;
	town = null;

	function wait_days(days, station_id, cargo)
	{
		local start = GSDate.GetCurrentDate();
		local max_waiting = 0;
		while (GSDate.GetCurrentDate() - start < days) {
			GSController.Sleep(37);
			local waiting = GSStation.GetCargoWaiting(station_id, cargo);
			if (waiting > max_waiting) max_waiting = waiting;
			if (!GSStation.IsValidStation(station_id)) return { valid = false, max_waiting = max_waiting };
		}
		return { valid = true, max_waiting = max_waiting };
	}

	/* Try to build an exit within distance 2 of the platform: a pavilion on a free tile, else an underpass on a road. */
	function build_exit_near(platform, station_id)
	{
		local px = this.build.tile_x(platform);
		local py = this.build.tile_y(platform);
		foreach (exit_type in [GSMetro.EXIT_PAVILION, GSMetro.EXIT_UNDERPASS]) {
			for (local dist = 1; dist <= 2; dist++) {
				for (local dy = -dist; dy <= dist; dy++) {
					for (local dx = -dist; dx <= dist; dx++) {
						if (max(abs(dx), abs(dy)) != dist) continue;
						local tile = this.build.make_tile(px + dx, py + dy);
						if (GSMetro.BuildExitSurface(tile, station_id, exit_type)) return tile;					}
				}
			}
		}
		return null;
	}

	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);

		this.report = MetroTest("metro_no_exit_no_catchment", null, 510083);
		local report = this.report;
		local companies = MetroTest.MetroCompanyLib(report);
		this.build = MetroTest.MetroBuildLib();
		local build = this.build;
		local world = MetroTest.MetroWorldLib(build);

		try {
			local area = build.find_buildable_rectangle(24, 24);
			report.check_true("build_area_found", area != null, "Expected a clear area for the town.");

			local company = companies.ensure_company();
			local passenger_cargo = world.find_passenger_cargo();
			report.check_true("passenger_cargo_found", passenger_cargo != null, "Expected a passenger cargo.");
			report.check_eq("selectgoods_off", 0, GSGameSettings.GetValue("order.selectgoods"), "Cargo should be delivered without a vehicle visit.");

			if (area != null && company != GSCompany.COMPANY_INVALID && passenger_cargo != null) {
				local company_mode = GSCompanyMode(company);
				/* Above sea level, or the flat area floods and the town drowns. */
				report.check_true("area_levelled", build.level_area_min_height(area, 24, 24, 2), "Town area should be flat and above sea level.");
				company_mode = null;

				/* The town first, then the platform under its centre. */
				local centre = build.make_tile(build.tile_x(area) + 12, build.tile_y(area) + 12);
				local town = world.create_town(centre, "Metro No Exit Town", 60);
				if (town == null) town = world.create_town_near(centre, 3, "Metro No Exit Town", 60);
				report.check_true("town_created", town != null && GSTown.IsValidTown(town), "A town should be founded in the test area.");
				report.check_true("town_has_houses", town != null && GSTown.GetHouseCount(town) > 0, "The town should have houses.");

				this.town = town;
				report.metric("town_houses_initial", town == null ? -1 : GSTown.GetHouseCount(town));
				report.metric("town_population_initial", town == null ? -1 : GSTown.GetPopulation(town));
				local platform = town == null ? centre : GSTown.GetLocation(town);
				local depth = GSTile.GetCornerHeight(platform, GSTile.CORNER_N) - 1;
				company_mode = GSCompanyMode(company);
				report.check_true("station_built", GSMetro.BuildUndergroundStation(platform, GSRail.RAILTRACK_NE_SW, depth, GSStation.STATION_NEW), "Underground station should be built under the town centre (err=" + GSError.GetLastErrorString() + ").");
				company_mode = null;
				local station_id = GSMetro.GetUndergroundStationID(platform, depth);
				report.check_true("station_valid", GSStation.IsValidStation(station_id), "Platform should resolve to a station.");
				report.metric("town_houses_after_station", town == null ? -1 : GSTown.GetHouseCount(town));

				if (GSStation.IsValidStation(station_id)) {
					report.check_true("no_exit", !GSMetro.HasSurfaceExit(station_id) && GSMetro.GetStationComplexExitCount(station_id) == 0, "Platforms must not create exits.");
					report.check_eq("platform_count", 1, GSMetro.GetStationComplexPlatformCount(station_id), "Station should have its platform.");
					report.check_true("coverage_empty", GSTileList_StationCoverage(station_id).IsEmpty(), "Station without exits should have an empty coverage.");
					report.check_true("acceptance_empty", GSCargoList_StationAccepting(station_id).IsEmpty(), "Station without exits should accept nothing.");
					report.check_true("station_has_location", GSMap.IsValidTile(GSStation.GetLocation(station_id)), "Station sign should have a location.");

					local quiet = this.wait_days(60, station_id, passenger_cargo);
					report.check_true("station_survives_60_days", quiet.valid && GSStation.IsValidStation(station_id), "Station with platforms but no exit must not be deleted as unused.");
					report.check_eq("no_passengers_without_exit", 0, quiet.max_waiting, "No passengers should appear at a station without exits.");
					report.check_true("coverage_still_empty", GSTileList_StationCoverage(station_id).IsEmpty(), "Coverage should stay empty while there is no exit.");
					report.metric("town_population", town == null ? -1 : GSTown.GetPopulation(town));
					report.metric("town_houses", town == null ? -1 : GSTown.GetHouseCount(town));
					report.check_true("town_still_has_houses", town != null && GSTown.GetHouseCount(town) > 0, "The town should keep its houses (control for the passenger check).");
					report.metric("town_distance", town == null ? -1 : GSMap.DistanceManhattan(GSTown.GetLocation(town), platform));

					/* Control: an exit gives the station a catchment and passengers. */
					company_mode = GSCompanyMode(company);
					local exit_tile = this.build_exit_near(platform, station_id);
					company_mode = null;
					report.check_true("exit_built", exit_tile != null, "An exit within 2 tiles of the platform should be buildable.");
					if (exit_tile != null) {
						report.check_true("coverage_with_exit", !GSTileList_StationCoverage(station_id).IsEmpty(), "Station with an exit should have a coverage.");
						local busy = this.wait_days(60, station_id, passenger_cargo);
						report.check_true("passengers_with_exit", busy.max_waiting > 0, "Passengers should appear once the station has an exit.");
						report.metric("exit_type", GSMetro.GetExitType(exit_tile));
						report.metric("passengers_with_exit", busy.max_waiting);
					}
				}
			}
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.finish();
	}
}
