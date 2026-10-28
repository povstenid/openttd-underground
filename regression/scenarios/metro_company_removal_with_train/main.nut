class MetroCompanyRemovalWithTrain extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);

		local report = MetroTest("metro_company_removal_with_train", null, 510021);
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();
		local metro = MetroTest.MetroLib(build);
		local trains = MetroTest.MetroTrainLib();

		try {
			local area = build.find_buildable_rectangle(40, 10);
			report.check_true("build_area_found", area != null, "Expected a clear area for the underground train layout.");

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
					report.check_true("start_vehicle", GSVehicle.StartStopVehicle(vehicle), "Train should be started.");
					company_mode = null;

					/* --- Merger while the train is underground: the train keeps running for the buyer. --- */
					local first_entry = trains.wait_for_underground_entry(vehicle, 16000);
					report.check_true("train_underground_before_merge", first_entry != null, "Train should enter the underground.");

					local buyer = GSMetro.CreateCompany(GSCompany.COMPANY_INVALID);
					report.check_true("buyer_created", buyer != GSCompany.COMPANY_INVALID, "A second company should be created.");

					if (first_entry != null && buyer != GSCompany.COMPANY_INVALID) {
						report.check_true("merged_with_train_underground", GSMetro.MergeCompany(company, buyer), "Merger should succeed while the train is underground.");
						report.check_true("train_transferred", GSVehicle.IsValidVehicle(vehicle) && GSVehicle.GetOwner(vehicle) == buyer, "Train should now belong to the buyer.");
						report.check_true("underground_station_survives_merge", GSMetro.GetUndergroundStationID(underground_station_tile, depth) == underground_station_id, "Underground platform should survive the merger.");

						local surface_ticks = trains.wait_for_surface_station(vehicle, station_b_tile, 16000);
						report.check_true("train_continues_after_merge", surface_ticks != null, "Inherited train should keep using the inherited metro.");

						/* --- Removal while the train is underground again: train and metro disappear. --- */
						local second_entry = trains.wait_for_underground_entry(vehicle, 20000);
						report.check_true("train_underground_before_removal", second_entry != null, "Train should go underground again.");

						local other = GSMetro.CreateCompany(GSCompany.COMPANY_INVALID);
						report.check_true("third_company_created", other != GSCompany.COMPANY_INVALID, "A third company should be created so the buyer can be removed.");

						report.check_true("removed_with_train_underground", GSMetro.RemoveCompany(buyer), "Company removal should succeed while its train is underground.");
						report.check_true("train_deleted", !GSVehicle.IsValidVehicle(vehicle), "The removed company's train should be gone.");
						report.check_true("portals_removed", !GSMetro.IsPortalTile(portal_a) && !GSMetro.IsPortalTile(portal_b), "Portals should be removed.");
						report.check_true("underground_removed",
							!GSMetro.HasUnderground(build.make_tile(x + 9, y)) &&
							!GSMetro.HasUnderground(build.make_tile(x + 10, y)) &&
							!GSMetro.HasUnderground(underground_station_tile) &&
							!GSMetro.HasUnderground(build.make_tile(x + 12, y)) &&
							!GSMetro.HasUnderground(build.make_tile(x + 13, y)),
							"All underground slices of the removed company should be freed.");
						report.check_eq("complex_platforms_gone", 0, GSMetro.GetStationComplexPlatformCount(underground_station_id), "Station complex should be empty.");

						/* Keep the game running for a while: nothing may touch the freed slices. */
						local waited = 0;
						while (waited < 6000 && GSStation.IsValidStation(underground_station_id)) {
							GSController.Sleep(100);
							waited += 100;
						}
						report.check_true("underground_station_deleted", !GSStation.IsValidStation(underground_station_id), "Emptied underground station should be deleted by the unused-station timeout.");
						report.metric("ticks_until_station_deleted", waited);
						report.metric("first_entry_ticks", first_entry.ticks);
						report.metric("second_entry_ticks", second_entry == null ? -1 : second_entry.ticks);
					}
				}
			}
			company_mode = null;
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.finish();
	}
}
