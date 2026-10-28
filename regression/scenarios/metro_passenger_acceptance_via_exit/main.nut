class MetroPassengerAcceptanceViaExit extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);

		local report = MetroTest("metro_passenger_acceptance_via_exit", null, 510010);
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();
		local metro = MetroTest.MetroLib(build);
		local world = MetroTest.MetroWorldLib(build);
		local trains = MetroTest.MetroTrainLib();

		try {
			local area = build.find_buildable_rectangle(72, 28);
			report.check_true("build_area_found", area != null, "Expected a clear area for the metro passenger acceptance scenario.");

			local company = companies.ensure_company();
			local passenger_cargo = world.find_passenger_cargo();
			report.check_true("passenger_cargo_found", passenger_cargo != null, "Expected a passenger-class cargo in the current game.");
			local rail_engine = trains.find_rail_engine();
			report.check_true("rail_engine_found", rail_engine != null, "Expected a buildable rail engine for the passenger metro scenario.");

			local company_mode = null;

			if (area != null && company != GSCompany.COMPANY_INVALID && passenger_cargo != null && rail_engine != null) {
				local x = build.tile_x(area);
				local y = build.tile_y(area);
				local depth = -1;

				local top_y = y + 8;
				local bottom_y = y + 18;
				local portal_x = x + 30;
				local right_x = x + 46;
				local underground_left_x = x + 10;

				local top_portal = build.make_tile(portal_x, top_y);
				local bottom_portal = build.make_tile(portal_x, bottom_y);
				local source_station_tile = build.make_tile(x + 22, bottom_y);
				local source_station_signal_tile = build.make_tile(x + 23, bottom_y);
				local surface_station_tile = build.make_tile(x + 35, top_y);
				local top_right_corner = build.make_tile(right_x, top_y);
				local bottom_right_corner = build.make_tile(right_x, bottom_y);
				local top_underground_corner = build.make_tile(underground_left_x, top_y);
				local bottom_underground_corner = build.make_tile(underground_left_x, bottom_y);
				/* Within construction.max_station_exit_distance (2) of the platform. */
				local source_exit_tile = build.make_tile(x + 22, bottom_y + 2);
				local depot_junction_tile = build.make_tile(x + 40, top_y);
				local depot_front = build.make_tile(x + 40, top_y + 1);
				local depot_tile = build.make_tile(x + 39, top_y + 1);

				company_mode = GSCompanyMode(company);

				report.check_true("top_portal_built", metro.build_portal(top_portal, GSMetro.DIAGDIR_NE, depth), "Top portal should return the train from underground to the surface.");
				report.check_true("bottom_portal_built", metro.build_portal(bottom_portal, GSMetro.DIAGDIR_SW, depth), "Bottom portal should send the train from the surface into the underground.");
				report.check_true("surface_station_built", build.build_surface_station_x(surface_station_tile), "Surface destination station should be built on the upper branch.");
				report.check_true("surface_top_row_left_built", build.build_surface_x(portal_x + 1, x + 34, top_y), "Upper branch should connect the top portal to the surface station.");
				report.check_true("surface_top_row_mid_built", build.build_surface_x(x + 37, x + 39, top_y), "Upper branch should continue from the surface station to the depot throat.");
				report.check_true("surface_top_row_junction_built", build.build_surface_x(x + 40, right_x - 1, top_y), "Upper branch should stay continuous past the depot junction.");
				report.check_true("surface_bottom_row_built", build.build_surface_x(portal_x + 1, right_x - 1, bottom_y), "Lower branch should connect the bottom portal to the right turn.");
				report.check_true("surface_right_column_built", build.build_surface_y(right_x, top_y + 1, bottom_y - 1), "Right column should close the surface U-turn.");
				report.check_true("surface_top_right_corner_built", GSRail.BuildRailTrack(top_right_corner, GSRail.RAILTRACK_NE_SE), "Top-right corner should connect the upper branch to the right column.");
				report.check_true("surface_bottom_right_corner_built", GSRail.BuildRailTrack(bottom_right_corner, GSRail.RAILTRACK_NW_NE), "Bottom-right corner should connect the right column back to the lower branch.");
				report.check_true("depot_junction_built", GSRail.BuildRail(build.make_tile(x + 39, top_y), depot_junction_tile, depot_front), "Depot junction should branch off the upper loop.");
				report.check_true("depot_siding_built", GSRail.BuildRail(depot_tile, depot_front, depot_junction_tile), "Depot siding should connect diagonally back to the loop.");
				report.check_true("depot_right_turn_built", GSRail.BuildRailTrack(depot_junction_tile, GSRail.RAILTRACK_SW_SE), "Depot junction should also connect the depot front toward the right side of the loop.");
				report.check_true("depot_built", GSRail.BuildRailDepot(depot_tile, depot_front), "Depot should be attached to the upper branch.");

				report.check_true("bottom_underground_entry_built", metro.build_underground_x(underground_left_x + 1, portal_x - 1, bottom_y, depth), "Lower underground branch should connect the bottom portal to the underground station.");
				report.check_true("source_underground_station_built", metro.build_station_x(source_station_tile, depth), "Underground source station should be built on the lower branch.");
				report.check_true("bottom_underground_corner_built", GSMetro.BuildUndergroundRail(bottom_underground_corner, GSRail.RAILTRACK_NW_SW, depth), "Lower underground corner should turn the train toward the vertical connector.");
				report.check_true("underground_vertical_link_built", metro.build_underground_y(underground_left_x, top_y + 1, bottom_y - 1, depth), "Vertical underground connector should link both underground branches.");
				report.check_true("top_underground_corner_built", GSMetro.BuildUndergroundRail(top_underground_corner, GSRail.RAILTRACK_SW_SE, depth), "Upper underground corner should turn the train back toward the top portal.");
				report.check_true("top_underground_exit_built", metro.build_underground_x(underground_left_x + 1, portal_x - 1, top_y, depth), "Upper underground branch should connect the underground station back to the top portal.");
				report.check_true("source_station_signal_built", GSMetro.BuildUndergroundSignal(source_station_signal_tile, GSRail.RAILTRACK_NE_SW, depth), "Underground source station should be protected by a signal.");

				local source_station_id = GSMetro.GetUndergroundStationID(source_station_tile, depth);
				local surface_station_id = GSStation.GetStationID(surface_station_tile);
				report.check_true("source_station_valid", GSStation.IsValidStation(source_station_id), "Underground station should resolve to a station id.");
				report.check_true("surface_station_valid", GSStation.IsValidStation(surface_station_id), "Surface station should resolve to a station id.");
				report.check_true("explicit_exit_built", GSStation.IsValidStation(source_station_id) && GSMetro.BuildExitSurface(source_exit_tile, source_station_id, GSMetro.EXIT_PAVILION), "Explicit surface exit should be attached to the underground station.");
				report.check_true("source_station_has_surface_exit", GSMetro.HasSurfaceExit(source_station_id), "Underground station should report a surface exit.");

				company_mode = null;

				local source_town = world.create_town_near(source_exit_tile, 8, "Metro Source Town", 80);
				local destination_town = world.create_town_near(surface_station_tile, 10, "Metro Destination Town", 80);
				report.check_true("source_town_created", source_town != null && GSTown.IsValidTown(source_town), "Source town should be founded near the explicit exit.");
				report.check_true("destination_town_created", destination_town != null && GSTown.IsValidTown(destination_town), "Destination town should be founded near the surface station.");

				company_mode = GSCompanyMode(company);

				local vehicle = trains.build_train(depot_tile, rail_engine);
				report.check_true("train_bought", vehicle != GSVehicle.VEHICLE_INVALID, "Passenger test should buy a train in the depot.");

				local wagon_attach = vehicle == GSVehicle.VEHICLE_INVALID ? null : trains.attach_cargo_wagon(vehicle, depot_tile, passenger_cargo);
				report.check_true("passenger_wagon_found", wagon_attach != null && wagon_attach.spec != null, "Passenger scenario should find a wagon with passenger capacity.");
				report.check_true("passenger_wagon_built", wagon_attach != null && wagon_attach.wagon != GSVehicle.VEHICLE_INVALID, "Passenger wagon should be built.");
				report.check_true("passenger_wagon_attached", wagon_attach != null && wagon_attach.attached, "Passenger wagon should be attached to the train.");

				report.check_true("order_source_added", vehicle != GSVehicle.VEHICLE_INVALID && trains.append_underground_order(vehicle, source_station_id), "Underground source station order should be added.");
				report.check_true("order_surface_added", vehicle != GSVehicle.VEHICLE_INVALID && trains.append_surface_order(vehicle, surface_station_tile), "Surface destination station order should be added.");
				report.check_eq("order_count", 2, vehicle == GSVehicle.VEHICLE_INVALID ? -1 : GSOrder.GetOrderCount(vehicle), "Passenger line should have two orders.");

				local source_station_waiting = trains.wait_for_station_waiting_cargo(source_station_id, passenger_cargo, 120000, 8);
				report.check_true("surface_station_accepts_passengers", world.station_accepts_cargo(surface_station_id, passenger_cargo), "Surface destination station should accept passengers.");
				if (source_town != null && destination_town != null) {
					GSCargoMonitor.GetTownPickupAmount(company, passenger_cargo, source_town, true);
					GSCargoMonitor.GetTownDeliveryAmount(company, passenger_cargo, destination_town, true);
				}
				report.check_true("pre_start_save_written", GSTest.SaveGame("metro_passenger_acceptance_via_exit_final.sav"), "Scenario should save the passenger fixture before launching the train.");
				report.check_true("start_vehicle", vehicle != GSVehicle.VEHICLE_INVALID && GSVehicle.StartStopVehicle(vehicle), "Passenger train should be started.");

				local left_depot_ticks = trains.wait_for_departure(vehicle, 8000);
				local underground_entry = left_depot_ticks == null ? null : trains.wait_for_underground_entry(vehicle, 20000);
				local source_station_ticks = underground_entry == null ? null : trains.wait_for_underground_station(vehicle, source_station_id, 22000);
				local cargo_loaded = source_station_ticks == null ? null : trains.wait_for_vehicle_cargo(vehicle, passenger_cargo, 4000, 1);
				local destination_station_ticks = cargo_loaded == null ? null : trains.wait_for_surface_station(vehicle, surface_station_tile, 26000);
				local cargo_reduced = (destination_station_ticks == null || cargo_loaded == null || cargo_loaded.amount <= 0) ? null : trains.wait_for_vehicle_cargo_at_most(vehicle, passenger_cargo, 4000, cargo_loaded.amount - 1);
				local town_pickup_result = (destination_station_ticks == null || source_town == null) ? null : trains.wait_for_town_pickup(company, passenger_cargo, source_town, 4000, 1);
				local town_delivery_result = (destination_station_ticks == null || destination_town == null) ? null : trains.wait_for_town_delivery(company, passenger_cargo, destination_town, 4000, 1);
				local cargo_after_destination = cargo_reduced == null ? (destination_station_ticks == null ? -1 : GSVehicle.GetCargoLoad(vehicle, passenger_cargo)) : cargo_reduced.amount;
				local town_pickup = town_pickup_result == null ? -1 : town_pickup_result.amount;
				local town_delivery = town_delivery_result == null ? -1 : town_delivery_result.amount;

				report.check_true("source_station_waiting_passengers", source_station_waiting != null || cargo_loaded != null, "Underground station should provide passengers to the train via the explicit surface exit.");
				report.check_true("train_left_depot", left_depot_ticks != null, "Passenger train should leave the depot.");
				report.check_true("train_entered_underground", underground_entry != null, "Passenger train should enter the underground loop.");
				report.check_true("train_serviced_source_station", source_station_ticks != null, "Passenger train should stop at the underground source station.");
				report.check_true("train_loaded_passengers", cargo_loaded != null, "Passenger train should load passengers at the underground station.");
				report.check_true("train_serviced_destination_station", destination_station_ticks != null, "Passenger train should stop at the surface destination station.");
				report.check_true("source_town_pickup_recorded", town_pickup_result != null, "Source town should record passenger pickup through the underground station exit.");
				report.check_true("train_unloaded_passengers", town_delivery_result != null || cargo_reduced != null, "Passenger train should deliver passengers to the surface destination station catchment.");

				report.metric("passenger_cargo", passenger_cargo);
				report.metric("passenger_cargo_name", GSCargo.GetName(passenger_cargo));
				report.metric("source_station_id", source_station_id);
				report.metric("surface_station_id", surface_station_id);
				report.metric("source_town_id", source_town);
				report.metric("destination_town_id", destination_town);
				report.metric("source_station_waiting_amount", source_station_waiting == null ? -1 : source_station_waiting.amount);
				report.metric("cargo_after_destination", cargo_after_destination);
				report.metric("town_pickup_amount", town_pickup);
				report.metric("town_delivery_amount", town_delivery);
				report.metric("vehicle_profit_this_year", vehicle == GSVehicle.VEHICLE_INVALID ? -1 : GSVehicle.GetProfitThisYear(vehicle));
				report.metric("vehicle_running_cost", vehicle == GSVehicle.VEHICLE_INVALID ? -1 : GSVehicle.GetRunningCost(vehicle));
			}

			company_mode = null;
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.check_true("final_save_written", GSTest.SaveGame("metro_passenger_acceptance_via_exit_final.sav"), "Scenario should save the final passenger world for manual inspection.");
		report.finish();
	}
}
