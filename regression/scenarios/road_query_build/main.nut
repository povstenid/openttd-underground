class RoadQueryBuild extends AIController {
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

	function BuildDryRun(func)
	{
		local test_mode = ScriptTestMode();
		local result = func();
		test_mode = null;
		return result;
	}

	function Start()
	{
		import("test.TestLib", "TestLib", 1);

		local report = TestLib("road_query_build", null, 424242);

		try {
			AICompany.SetLoanAmount(AICompany.GetMaxLoanAmount());

			local area = this.FindBuildableRectangle(14, 4);
			report.check_true("build_area_found", area != null, "Expected a clear rectangle for the smoke build.");

			if (area != null) {
				local base_x = AIMap.GetTileX(area);
				local base_y = AIMap.GetTileY(area);

				local road_start = this.MakeTile(base_x + 1, base_y + 1);
				local road_end = this.MakeTile(base_x + 10, base_y + 1);
				local depot_tile = this.MakeTile(base_x + 5, base_y + 2);
				local depot_front = this.MakeTile(base_x + 5, base_y + 1);
				local station_tile = this.MakeTile(base_x + 2, base_y + 1);
				local station_front = this.MakeTile(base_x + 3, base_y + 1);

				report.check_true("road_dry_run", this.BuildDryRun(function() { return AIRoad.BuildRoadFull(road_start, road_end); }), "Dry-run road build should succeed.");
				report.check_true("depot_dry_run", this.BuildDryRun(function() { return AIRoad.BuildRoadDepot(depot_tile, depot_front); }), "Dry-run depot build should succeed.");
				report.check_true("station_dry_run", this.BuildDryRun(function() { return AIRoad.BuildDriveThroughRoadStation(station_tile, station_front, AIRoad.ROADVEHTYPE_BUS, AIStation.STATION_NEW); }), "Dry-run station build should succeed.");

				report.check_true("road_built", AIRoad.BuildRoadFull(road_start, road_end), "Road segment should be built.");
				report.check_true("depot_built", AIRoad.BuildRoadDepot(depot_tile, depot_front), "Road depot should be built.");
				report.check_true("station_built", AIRoad.BuildDriveThroughRoadStation(station_tile, station_front, AIRoad.ROADVEHTYPE_BUS, AIStation.STATION_NEW), "Drive-through station should be built.");

				report.check_true("road_tile_start", AIRoad.IsRoadTile(road_start), "Start tile should contain road.");
				report.check_true("road_tile_end", AIRoad.IsRoadTile(road_end), "End tile should contain road.");
				report.check_true("depot_tile", AIRoad.IsRoadDepotTile(depot_tile), "Depot tile should be present.");
				report.check_true("station_tile", AIRoad.IsDriveThroughRoadStationTile(station_tile), "Station tile should be present.");
				report.check_true("station_id_valid", AIStation.IsValidStation(AIStation.GetStationID(station_tile)), "Station id should resolve from the built tile.");

				report.metric("bank_balance", AICompany.GetBankBalance(AICompany.COMPANY_SELF));
				report.metric("station_id", AIStation.GetStationID(station_tile));
			}
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.finish();
	}
}
