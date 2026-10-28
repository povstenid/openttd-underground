/*
 * Layout (single row y, x grows to the right / south-west):
 *
 *   [stA]--surface--[junction]--[portal A]--UG(-1) x12--[portal B]--surface--[stB]
 *                       |
 *                    [depot]
 *
 * The train is stopped while running through the underground stretch.
 * Removing the underground track it stands on must be refused with
 * ERR_VEHICLE_IN_THE_WAY (the slice it holds would otherwise be freed and its
 * SliceID reused). Once the train has left the underground and is stopped at
 * the surface destination, the same removal must succeed.
 */
class MetroRemoveUnderTrainRefused extends GSController {
	function wait_for_underground_x(vehicle, min_x, max_ticks)
	{
		for (local ticks = 0; ticks <= max_ticks; ticks++) {
			if (GSMetro.IsVehicleUnderground(vehicle)) {
				local tile = GSMetro.GetVehicleUndergroundTile(vehicle);
				if (tile != GSMap.TILE_INVALID && GSMap.GetTileX(tile) >= min_x) return ticks;
			}
			GSController.Sleep(1);
		}
		return null;
	}

	function wait_for_standstill(vehicle, max_ticks)
	{
		for (local ticks = 0; ticks <= max_ticks; ticks++) {
			if (GSVehicle.GetCurrentSpeed(vehicle) == 0) {
				/* Make sure it stays put for a moment. */
				GSController.Sleep(20);
				if (GSVehicle.GetCurrentSpeed(vehicle) == 0) return ticks;
			}
			GSController.Sleep(1);
		}
		return null;
	}

	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);

		local report = MetroTest("metro_remove_under_train_refused", null, 510041);
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();
		local metro = MetroTest.MetroLib(build);
		local trains = MetroTest.MetroTrainLib();

		try {
			local area = build.find_buildable_rectangle(40, 10);
			report.check_true("build_area_found", area != null, "Expected a clear area for the test layout.");

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
				local ug_first_x = x + 9;
				local ug_last_x = x + 20;
				local portal_b = build.make_tile(x + 21, y);
				local station_b_tile = build.make_tile(x + 24, y);

				report.check_true("station_a_built", build.build_surface_station_x(station_a_tile), "Surface origin station should be built.");
				report.check_true("station_b_built", build.build_surface_station_x(station_b_tile), "Surface destination station should be built.");
				report.check_true("left_surface_built",
					build.build_surface_x(x + 5, x + 6, y) &&
					GSRail.BuildRail(build.make_tile(x + 6, y), depot_junction_tile, depot_front) &&
					GSRail.BuildRailTrack(depot_junction_tile, GSRail.RAILTRACK_SW_SE),
					"Left surface approach should be built.");
				report.check_true("right_surface_built", build.build_surface_x(x + 22, x + 23, y), "Right surface approach should be built.");
				report.check_true("depot_built",
					GSRail.BuildRail(depot_tile, depot_front, depot_junction_tile) &&
					GSRail.BuildRailDepot(depot_tile, depot_front),
					"Train depot should be built.");
				report.check_true("portal_a_built", metro.build_portal(portal_a, GSMetro.DIAGDIR_SW, depth), "Entry portal should be built.");
				report.check_true("portal_b_built", metro.build_portal(portal_b, GSMetro.DIAGDIR_NE, depth), "Exit portal should be built.");
				report.check_true("underground_chain_built", metro.build_underground_x(ug_first_x, ug_last_x, y, depth), "Underground track should be built.");
				report.check_true("portal_link", GSMetro.HasPortalConnection(portal_a, portal_b, depth), "Portals should be connected underground.");

				local vehicle = trains.build_train(depot_tile, engine);
				report.check_true("train_bought", vehicle != GSVehicle.VEHICLE_INVALID, "Train should be bought in the depot.");

				if (vehicle != GSVehicle.VEHICLE_INVALID) {
					report.check_true("surface_order_added", trains.append_surface_order(vehicle, station_b_tile), "Surface destination order should be added.");
					report.check_true("start_vehicle", GSVehicle.StartStopVehicle(vehicle), "Train should be started.");

					local reached = this.wait_for_underground_x(vehicle, x + 12, 20000);
					report.check_true("train_underground", reached != null, "Train should run into the underground stretch.");

					if (reached != null) {
						report.check_true("stop_vehicle", GSVehicle.StartStopVehicle(vehicle), "Train should be stopped underground.");
						local still = this.wait_for_standstill(vehicle, 4000);
						report.check_true("train_standstill", still != null, "Stopped train should come to a standstill.");

						local train_tile = GSMetro.GetVehicleUndergroundTile(vehicle);
						local train_depth = GSMetro.GetVehicleUndergroundDepth(vehicle);
						report.check_true("still_underground", GSMetro.IsVehicleUnderground(vehicle), "Stopped train should still be underground.");
						report.check_eq("train_depth", depth, train_depth, "Train should stand at the metro depth.");
						report.check_eq("train_on_track_slice", GSMetro.TK_TRACK, GSMetro.GetUndergroundTileKind(train_tile, depth), "Train should stand on plain underground track.");
						report.metric("train_tile_x", train_tile == GSMap.TILE_INVALID ? -1 : GSMap.GetTileX(train_tile) - x);

						/* The actual protection under test. */
						local removed = GSMetro.RemoveUndergroundRail(train_tile, GSRail.RAILTRACK_NE_SW, depth);
						local err = GSError.GetLastError();
						report.check_true("remove_under_train_refused", !removed, "Removing track under a stopped train must fail.");
						report.check_eq("remove_under_train_error", GSError.ERR_VEHICLE_IN_THE_WAY, err, "Refusal should report a vehicle in the way.");
						report.check_eq("track_still_present", GSMetro.TK_TRACK, GSMetro.GetUndergroundTileKind(train_tile, depth), "Track under the train must survive.");
						report.check_true("train_still_underground", GSMetro.IsVehicleUnderground(vehicle) && GSMetro.GetVehicleUndergroundTile(vehicle) == train_tile, "Train state must be untouched.");

						/* Let the train leave and park it at the surface destination. */
						report.check_true("restart_vehicle", GSVehicle.StartStopVehicle(vehicle), "Train should be restarted.");
						local arrival = trains.wait_for_surface_station(vehicle, station_b_tile, 20000);
						report.check_true("train_reached_surface_station", arrival != null, "Train should leave the underground and reach the destination.");
						if (arrival != null) {
							report.check_true("park_vehicle", GSVehicle.StartStopVehicle(vehicle), "Train should be stopped at the destination.");
							this.wait_for_standstill(vehicle, 2000);
						}
						report.check_true("train_left_underground", !GSMetro.IsVehicleUnderground(vehicle), "Train should be back on the surface.");

						local removed_after = GSMetro.RemoveUndergroundRail(train_tile, GSRail.RAILTRACK_NE_SW, depth);
						report.check_true("remove_after_leave_ok", removed_after, "Removing the track after the train left must succeed (err=" + GSError.GetLastErrorString() + ").");
						report.check_eq("track_gone", GSMetro.TK_NONE, GSMetro.GetUndergroundTileKind(train_tile, depth), "Track should be gone after removal.");
					}

					report.metric("vehicle", vehicle);
					report.metric("ticks_to_underground", reached == null ? -1 : reached);
				}
			}
			company_mode = null;
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.finish();
	}
}
