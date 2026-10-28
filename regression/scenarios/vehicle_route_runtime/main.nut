class VehicleRouteRuntime extends AIController {
	function MakeTile(x, y)
	{
		return AIMap.GetTileIndex(x, y);
	}

	function FindBuildableRectangle(width, height)
	{
		local max_x = AIMap.GetMapSizeX() - width - 1;
		local max_y = AIMap.GetMapSizeY() - height - 1;

		for (local y = 1; y <= max_y; y++) {
			for (local x = 1; x <= max_x; x++) {
				local tile = this.MakeTile(x, y);
				if (AITile.IsBuildableRectangle(tile, width, height)) return tile;
			}
		}

		return null;
	}

	function FindPassengerRoadEngine()
	{
		local engines = AIEngineList(AIVehicle.VT_ROAD);
		for (local engine = engines.Begin(); !engines.IsEnd(); engine = engines.Next()) {
			if (!AIEngine.IsBuildable(engine)) continue;
			local cargo = AIEngine.GetCargoType(engine);
			if (AICargo.IsValidCargo(cargo) && AICargo.HasCargoClass(cargo, AICargo.CC_PASSENGERS)) return engine;
		}

		return null;
	}

	function Start()
	{
		import("test.TestLib", "TestLib", 1);

		local report = TestLib("vehicle_route_runtime", null, 424242);

		try {
			AICompany.SetLoanAmount(AICompany.GetMaxLoanAmount());

			local area = this.FindBuildableRectangle(16, 4);
			report.check_true("build_area_found", area != null, "Expected a clear rectangle for runtime route validation.");

			local engine = this.FindPassengerRoadEngine();
			report.check_true("passenger_engine_found", engine != null, "Expected at least one buildable passenger road engine.");

			if (area != null && engine != null) {
				local base_x = AIMap.GetTileX(area);
				local base_y = AIMap.GetTileY(area);

				local road_start = this.MakeTile(base_x + 1, base_y + 1);
				local road_end = this.MakeTile(base_x + 12, base_y + 1);
				local station_a_tile = this.MakeTile(base_x + 2, base_y + 1);
				local station_a_front = this.MakeTile(base_x + 3, base_y + 1);
				local station_b_tile = this.MakeTile(base_x + 10, base_y + 1);
				local station_b_front = this.MakeTile(base_x + 9, base_y + 1);
				local depot_tile = this.MakeTile(base_x + 6, base_y + 2);
				local depot_front = this.MakeTile(base_x + 6, base_y + 1);

				report.check_true("road_built", AIRoad.BuildRoadFull(road_start, road_end), "Road segment should be built.");
				report.check_true("station_a_built", AIRoad.BuildDriveThroughRoadStation(station_a_tile, station_a_front, AIRoad.ROADVEHTYPE_BUS, AIStation.STATION_NEW), "First station should be built.");
				report.check_true("station_b_built", AIRoad.BuildDriveThroughRoadStation(station_b_tile, station_b_front, AIRoad.ROADVEHTYPE_BUS, AIStation.STATION_NEW), "Second station should be built.");
				report.check_true("depot_built", AIRoad.BuildRoadDepot(depot_tile, depot_front), "Depot should be built.");

				local station_a = AIStation.GetStationID(station_a_tile);
				local station_b = AIStation.GetStationID(station_b_tile);
				local vehicle = AIVehicle.BuildVehicle(depot_tile, engine);
				report.check_true("vehicle_built", vehicle != AIVehicle.VEHICLE_INVALID, "Vehicle should be built inside the depot.");

				if (vehicle != AIVehicle.VEHICLE_INVALID) {
					report.check_true("order_1_added", AIOrder.AppendOrder(vehicle, station_a, AIOrder.OF_NONE), "First order should be added.");
					report.check_true("order_2_added", AIOrder.AppendOrder(vehicle, station_b, AIOrder.OF_NONE), "Second order should be added.");
					report.check_true("start_vehicle", AIVehicle.StartStopVehicle(vehicle), "Vehicle should leave the depot when started.");

					local initial_location = AIVehicle.GetLocation(vehicle);
					local moved = false;
					local left_depot = false;
					local peak_speed = 0;
					local ticks_waited = 0;

					for (local i = 0; i < 64; i++) {
						this.Sleep(15);
						ticks_waited += 15;

						left_depot = left_depot || !AIVehicle.IsInDepot(vehicle);
						moved = moved || AIVehicle.GetLocation(vehicle) != initial_location;

						local speed = AIVehicle.GetCurrentSpeed(vehicle);
						if (speed > peak_speed) peak_speed = speed;

						if (left_depot && moved && peak_speed > 0) break;
					}

					report.check_true("left_depot", left_depot, "Vehicle should leave the depot after being started.");
					report.check_true("vehicle_moved", moved, "Vehicle should change tile on the route.");
					report.check_true("speed_positive", peak_speed > 0, "Vehicle should report positive speed at runtime.");

					report.metric("vehicle", vehicle);
					report.metric("ticks_waited", ticks_waited);
					report.metric("peak_speed", peak_speed);
				}
			}
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.finish();
	}
}
