class MetroSurfaceStationBuild extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);

		local report = MetroTest("metro_surface_station_build", null, 510001);
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();

		try {
			local area = build.find_buildable_rectangle(24, 8);
			report.check_true("build_area_found", area != null, "Expected a clear area for surface metro construction.");

			local company = companies.ensure_company();
			if (area != null && company != GSCompany.COMPANY_INVALID) {
				local x = build.tile_x(area);
				local y = build.tile_y(area) + 2;

				local company_mode = GSCompanyMode(company);

				local station_tile = build.make_tile(x + 3, y);
				local depot_tile = build.make_tile(x + 8, y + 1);
				local depot_front = build.make_tile(x + 8, y);
				local signal_tile = build.make_tile(x + 10, y);
				local signal_front = build.make_tile(x + 11, y);

				report.check_true("surface_station_built", build.build_surface_station_x(station_tile), "Surface station should be built.");
				report.check_true("surface_tracks_built", build.build_surface_x(x + 2, x + 2, y) && build.build_surface_x(x + 5, x + 16, y), "Surface metro test line should be built.");
				report.check_true("surface_depot_built", GSRail.BuildRailDepot(depot_tile, depot_front), "Surface depot should be built.");
				report.check_true("surface_signal_built", GSRail.BuildSignal(signal_tile, signal_front, GSRail.SIGNALTYPE_PBS_ONEWAY), "Surface signal should be built.");

				local station_id = GSStation.GetStationID(station_tile);
				report.check_true("surface_station_valid", GSStation.IsValidStation(station_id), "Surface station tile should resolve to a station.");
				report.check_true("surface_station_tile", GSRail.IsRailStationTile(station_tile), "Surface station tile should be present.");
				report.check_true("surface_depot_tile", GSRail.IsRailDepotTile(depot_tile), "Depot tile should be present.");

				report.metric("station_id", station_id);
				report.metric("company_balance", GSCompany.GetBankBalance(company));

				company_mode = null;
			}
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.finish();
	}
}
