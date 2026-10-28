class MetroTrainLaunchSurface extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);

		local report = MetroTest("metro_train_launch_surface", null, 510004);
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();
		local trains = MetroTest.MetroTrainLib();

		try {
			local area = build.find_buildable_rectangle(32, 10);
			report.check_true("build_area_found", area != null, "Expected a clear area for the surface train runtime scenario.");

			local company = companies.ensure_company();
			local company_mode = company == GSCompany.COMPANY_INVALID ? null : GSCompanyMode(company);
			local engine = company_mode == null ? null : trains.find_rail_engine();
			report.check_true("rail_engine_found", engine != null, "Expected at least one buildable rail engine.");

			if (area != null && engine != null && company != GSCompany.COMPANY_INVALID) {
				local x = build.tile_x(area);
				local y = build.tile_y(area) + 3;

				local station_a_tile = build.make_tile(x + 3, y);
				local station_b_tile = build.make_tile(x + 15, y);
				local depot_junction_tile = build.make_tile(x + 7, y);
				local depot_front = build.make_tile(x + 7, y + 1);
				local depot_tile = build.make_tile(x + 7, y + 2);

				report.check_true("station_a_built", build.build_surface_station_x(station_a_tile), "First station should be built.");
				report.check_true("station_b_built", build.build_surface_station_x(station_b_tile), "Second station should be built.");
				report.check_true("surface_line_built",
					build.build_surface_x(x + 5, x + 6, y) &&
					build.build_surface_x(x + 8, x + 14, y) &&
					build.build_surface_x(x + 17, x + 18, y) &&
					GSRail.BuildRail(build.make_tile(x + 6, y), depot_junction_tile, depot_front) &&
					GSRail.BuildRailTrack(depot_junction_tile, GSRail.RAILTRACK_SW_SE),
					"Surface metro line should be built.");
				report.check_true("depot_built",
					GSRail.BuildRail(depot_tile, depot_front, depot_junction_tile) &&
					GSRail.BuildRailDepot(depot_tile, depot_front),
					"Depot should be built.");

				local vehicle = trains.build_train(depot_tile, engine);
				report.check_true("train_bought", vehicle != GSVehicle.VEHICLE_INVALID, "Train should be bought in the depot.");

				if (vehicle != GSVehicle.VEHICLE_INVALID) {
					report.check_true("order_1_added", trains.append_surface_order(vehicle, station_a_tile), "First station order should be added.");
					report.check_true("order_2_added", trains.append_surface_order(vehicle, station_b_tile), "Second station order should be added.");
					report.check_eq("order_count", 2, GSOrder.GetOrderCount(vehicle), "Train should have two orders.");
					report.check_true("start_vehicle", GSVehicle.StartStopVehicle(vehicle), "Train should leave the depot.");

					local left_depot_ticks = trains.wait_for_departure(vehicle, 4000);
					local move_result = trains.wait_for_movement(vehicle, 4000);

					report.check_true("train_left_depot", left_depot_ticks != null, "Train should leave the depot within the timeout.");
					report.check_true("train_started_moving", move_result.ticks != null, "Train should start moving on the surface line.");

					report.metric("vehicle", vehicle);
					report.metric("engine", engine);
					report.metric("ticks_to_leave_depot", left_depot_ticks == null ? -1 : left_depot_ticks);
					report.metric("ticks_to_move", move_result.ticks == null ? -1 : move_result.ticks);
					report.metric("max_speed", move_result.max_speed);
				}

			}
			company_mode = null;
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.finish();
	}
}
