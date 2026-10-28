class MetroTrainLaunchUnderground extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);

		local report = MetroTest("metro_train_launch_underground", null, 510005);
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();
		local metro = MetroTest.MetroLib(build);
		local trains = MetroTest.MetroTrainLib();

		try {
			local area = build.find_buildable_rectangle(40, 10);
			report.check_true("build_area_found", area != null, "Expected a clear area for underground train runtime.");

			local company = companies.ensure_company();
			local company_mode = company == GSCompany.COMPANY_INVALID ? null : GSCompanyMode(company);
			local engine = company_mode == null ? null : trains.find_rail_engine();
			report.check_true("rail_engine_found", engine != null, "Expected at least one buildable rail engine.");

			if (area != null && engine != null && company != GSCompany.COMPANY_INVALID) {
				local x = build.tile_x(area);
				local y = build.tile_y(area) + 3;
				local depth = -1;

				local station_a_tile = build.make_tile(x + 3, y);
				local depot_junction_tile = build.make_tile(x + 7, y);
				local depot_front = build.make_tile(x + 7, y + 1);
				local depot_tile = build.make_tile(x + 7, y + 2);
				local portal_a = build.make_tile(x + 8, y);
				local underground_station_tile = build.make_tile(x + 11, y);
				local portal_b = build.make_tile(x + 14, y);
				local station_b_tile = build.make_tile(x + 17, y);

				report.check_true("station_a_built", build.build_surface_station_x(station_a_tile), "Surface origin station should be built.");
				report.check_true("station_b_built", build.build_surface_station_x(station_b_tile), "Surface destination station should be built.");
				report.check_true("left_surface_built",
					build.build_surface_x(x + 5, x + 6, y) &&
					GSRail.BuildRail(build.make_tile(x + 6, y), depot_junction_tile, depot_front) &&
					GSRail.BuildRailTrack(depot_junction_tile, GSRail.RAILTRACK_SW_SE),
					"Left surface approach should be built.");
				report.check_true("right_surface_built", build.build_surface_x(x + 15, x + 16, y), "Right surface approach should be built.");
				report.check_true("depot_built",
					GSRail.BuildRail(depot_tile, depot_front, depot_junction_tile) &&
					GSRail.BuildRailDepot(depot_tile, depot_front),
					"Train depot should be built.");
				report.check_true("portal_a_built", metro.build_portal(portal_a, GSMetro.DIAGDIR_SW, depth), "Entry portal should be built.");
				report.check_true("portal_b_built", metro.build_portal(portal_b, GSMetro.DIAGDIR_NE, depth), "Exit portal should be built.");
				report.check_true("underground_left_chain_built", metro.build_underground_x(x + 9, x + 10, y, depth), "Left underground chain should be built.");
				report.check_true("underground_right_chain_built", metro.build_underground_x(x + 12, x + 13, y, depth), "Right underground chain should be built.");
				report.check_true("underground_station_built", metro.build_station_x(underground_station_tile, depth), "Underground platform should be built.");

				local underground_station_id = GSMetro.GetUndergroundStationID(underground_station_tile, depth);
				report.check_true("underground_station_valid", GSStation.IsValidStation(underground_station_id), "Underground platform should resolve to a station id.");

				local vehicle = trains.build_train(depot_tile, engine);
				report.check_true("train_bought", vehicle != GSVehicle.VEHICLE_INVALID, "Train should be bought in the depot.");

				if (vehicle != GSVehicle.VEHICLE_INVALID) {
					report.check_true("underground_order_added", trains.append_underground_order(vehicle, underground_station_id), "Underground station order should be added.");
					report.check_true("surface_order_added", trains.append_surface_order(vehicle, station_b_tile), "Surface destination order should be added.");
					report.check_eq("order_count", 2, GSOrder.GetOrderCount(vehicle), "Train should have two orders.");
					report.check_true("start_vehicle", GSVehicle.StartStopVehicle(vehicle), "Train should be started.");

					local left_depot_ticks = trains.wait_for_departure(vehicle, 6000);
					local underground_entry = trains.wait_for_underground_entry(vehicle, 10000);
					local underground_station_ticks = trains.wait_for_underground_station(vehicle, underground_station_id, 12000);
					local surface_station_ticks = trains.wait_for_surface_station(vehicle, station_b_tile, 16000);

					report.check_true("train_left_depot", left_depot_ticks != null, "Train should leave the depot.");
					report.check_true("train_reached_underground_tile", underground_entry != null, "Train should enter underground infrastructure.");
					report.check_true("train_serviced_underground_station", underground_station_ticks != null, "Train should stop at the underground station.");
					report.check_true("train_returned_to_surface", surface_station_ticks != null, "Train should return to a surface station after underground travel.");

					report.metric("vehicle", vehicle);
					report.metric("engine", engine);
					report.metric("underground_station_id", underground_station_id);
					report.metric("ticks_to_leave_depot", left_depot_ticks == null ? -1 : left_depot_ticks);
					report.metric("ticks_to_first_underground_entry", underground_entry == null ? -1 : underground_entry.ticks);
					report.metric("ticks_to_first_station_service", underground_station_ticks == null ? -1 : underground_station_ticks);
					report.metric("ticks_to_surface_station", surface_station_ticks == null ? -1 : surface_station_ticks);
					report.metric("underground_entry_depth", underground_entry == null ? 0 : underground_entry.depth);
				}

			}
			company_mode = null;
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.finish();
	}
}
