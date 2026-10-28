class RoadVehicleSetup extends AIController {
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

		local report = TestLib("road_vehicle_setup", null, 424242);

		try {
			AICompany.SetLoanAmount(AICompany.GetMaxLoanAmount());

			local area = this.FindBuildableRectangle(16, 4);
			report.check_true("build_area_found", area != null, "Expected a clear rectangle for road vehicle setup.");

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
				report.check_true("station_a_valid", AIStation.IsValidStation(station_a), "Station A should resolve to a valid id.");
				report.check_true("station_b_valid", AIStation.IsValidStation(station_b), "Station B should resolve to a valid id.");

				local vehicle = AIVehicle.BuildVehicle(depot_tile, engine);
				report.check_true("vehicle_built", vehicle != AIVehicle.VEHICLE_INVALID, "Vehicle should be built inside the depot.");

				if (vehicle != AIVehicle.VEHICLE_INVALID) {
					report.check_true("order_1_added", AIOrder.AppendOrder(vehicle, station_a, AIOrder.OF_NONE), "First order should be added.");
					report.check_true("order_2_added", AIOrder.AppendOrder(vehicle, station_b, AIOrder.OF_NONE), "Second order should be added.");
					report.check_eq("order_count", 2, AIOrder.GetOrderCount(vehicle), "Vehicle should have two orders.");
					report.check_true("vehicle_in_depot", AIVehicle.IsInDepot(vehicle), "Newly built vehicle should still be in depot.");

					report.metric("engine", engine);
					report.metric("vehicle", vehicle);
					report.metric("station_a", station_a);
					report.metric("station_b", station_b);
				}
			}
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.finish();
	}
}
