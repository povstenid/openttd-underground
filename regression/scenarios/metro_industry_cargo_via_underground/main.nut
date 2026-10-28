class MetroIndustryCargoViaUnderground extends GSController {
	/**
	 * Build a pavilion exit within the default exit distance (2) of a single-tile
	 * underground platform, on the free tile closest to the industry.
	 */
	function build_exit_towards(build, platform_tile, station_id, built)
	{
		local px = build.tile_x(platform_tile);
		local py = build.tile_y(platform_tile);
		local target = built == null ? platform_tile : GSIndustry.GetLocation(built.industry);
		local candidates = [];
		for (local dy = -2; dy <= 2; dy++) {
			for (local dx = -2; dx <= 2; dx++) {
				if (dx == 0 && dy == 0) continue;
				local t = build.make_tile(px + dx, py + dy);
				candidates.push({ tile = t, dist = GSMap.DistanceManhattan(t, target) });
			}
		}
		candidates.sort(function(a, b) { return a.dist - b.dist; });
		foreach (c in candidates) {
			if (GSMetro.BuildExitSurface(c.tile, station_id, GSMetro.EXIT_PAVILION)) return true;
		}
		return false;
	}

	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);

		local report = MetroTest("metro_industry_cargo_via_underground", null, 510011);
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();
		local metro = MetroTest.MetroLib(build);
		local world = MetroTest.MetroWorldLib(build);
		local trains = MetroTest.MetroTrainLib();

		try {
			local area = build.find_buildable_rectangle(80, 30);
			report.check_true("build_area_found", area != null, "Expected a clear area for the metro industry cargo scenario.");

			local company = companies.ensure_company();
			local pair = world.find_buildable_industry_pair();
			report.check_true("buildable_industry_pair_found", pair != null, "Expected a buildable raw -> processing industry pair with a shared freight cargo.");
			local rail_engine = trains.find_rail_engine();
			report.check_true("rail_engine_found", rail_engine != null, "Expected a buildable rail engine for the freight metro scenario.");

			local company_mode = null;

			/* First the exits serve every cargo; the rule "passengers and mail" is checked after the run. */
			report.check_true("exit_cargo_all_set", GSGameSettings.SetValue("underground.exit_cargo", 1), "Setting underground.exit_cargo should switch to all cargo.");

			if (area != null && company != GSCompany.COMPANY_INVALID && pair != null && rail_engine != null) {
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
				local destination_station_tile = build.make_tile(x + 20, top_y);
				local destination_signal_tile = build.make_tile(x + 21, top_y);
				local source_station_tile = build.make_tile(x + 22, bottom_y);
				local source_signal_tile = build.make_tile(x + 23, bottom_y);
				local top_right_corner = build.make_tile(right_x, top_y);
				local bottom_right_corner = build.make_tile(right_x, bottom_y);
				local top_underground_corner = build.make_tile(underground_left_x, top_y);
				local bottom_underground_corner = build.make_tile(underground_left_x, bottom_y);
				/* Exits must be within construction.max_station_exit_distance (2) of the platforms. */
				local destination_exit_tile = build.make_tile(x + 20, top_y - 2);
				local source_exit_tile = build.make_tile(x + 22, bottom_y + 2);
				local depot_junction_tile = build.make_tile(x + 40, top_y);
				local depot_front = build.make_tile(x + 40, top_y + 1);
				local depot_tile = build.make_tile(x + 39, top_y + 1);

				company_mode = GSCompanyMode(company);

				report.check_true("top_portal_built", metro.build_portal(top_portal, GSMetro.DIAGDIR_NE, depth), "Top portal should return freight trains from underground to the surface.");
				/* A portal's direction is the way a train moves when it dives: here the tunnel lies west of the mouth. */
				report.check_true("bottom_portal_built", metro.build_portal(bottom_portal, GSMetro.DIAGDIR_NE, depth), "Bottom portal should send freight trains from the surface underground.");
				report.check_true("surface_top_row_built", build.build_surface_x(portal_x + 1, x + 39, top_y), "Upper surface row should connect the top portal to the depot throat.");
				report.check_true("surface_top_row_junction_built", build.build_surface_x(x + 40, right_x - 1, top_y), "Upper surface row should continue past the depot junction.");
				report.check_true("surface_bottom_row_built", build.build_surface_x(portal_x + 1, right_x - 1, bottom_y), "Lower surface row should connect the bottom portal to the right turn.");
				report.check_true("surface_right_column_built", build.build_surface_y(right_x, top_y + 1, bottom_y - 1), "Right column should close the surface U-turn.");
				report.check_true("surface_top_right_corner_built", GSRail.BuildRailTrack(top_right_corner, GSRail.RAILTRACK_NE_SE), "Top-right corner should connect the upper branch to the right column.");
				report.check_true("surface_bottom_right_corner_built", GSRail.BuildRailTrack(bottom_right_corner, GSRail.RAILTRACK_NW_NE), "Bottom-right corner should connect the right column back to the lower branch.");
				report.check_true("depot_junction_built", GSRail.BuildRail(build.make_tile(x + 39, top_y), depot_junction_tile, depot_front), "Depot junction should branch off the upper loop.");
				report.check_true("depot_siding_built", GSRail.BuildRail(depot_tile, depot_front, depot_junction_tile), "Depot siding should connect diagonally back to the loop.");
				report.check_true("depot_right_turn_built", GSRail.BuildRailTrack(depot_junction_tile, GSRail.RAILTRACK_SW_SE), "Depot junction should also connect the depot front toward the right side of the loop.");
				report.check_true("depot_built", GSRail.BuildRailDepot(depot_tile, depot_front), "Depot should be attached to the upper branch.");

				report.check_true("bottom_underground_branch_built", metro.build_underground_x(underground_left_x + 1, portal_x - 1, bottom_y, depth), "Lower underground branch should connect the bottom portal to the source station.");
				report.check_true("source_station_built", metro.build_station_x(source_station_tile, depth), "Underground source station should be built on the lower branch.");
				report.check_true("bottom_underground_corner_built", GSMetro.BuildUndergroundRail(bottom_underground_corner, GSRail.RAILTRACK_NW_SW, depth), "Lower underground corner should turn the train toward the vertical connector.");
				report.check_true("underground_vertical_link_built", metro.build_underground_y(underground_left_x, top_y + 1, bottom_y - 1, depth), "Vertical underground connector should link the freight branches.");
				report.check_true("top_underground_corner_built", GSMetro.BuildUndergroundRail(top_underground_corner, GSRail.RAILTRACK_SW_SE, depth), "Upper underground corner should turn the train toward the top portal.");
				report.check_true("top_underground_branch_built", metro.build_underground_x(underground_left_x + 1, portal_x - 1, top_y, depth), "Upper underground branch should connect the destination station to the top portal.");
				report.check_true("destination_station_built", metro.build_station_x(destination_station_tile, depth), "Underground destination station should be built on the upper branch.");
				report.check_true("source_signal_built", GSMetro.BuildUndergroundSignal(source_signal_tile, GSRail.RAILTRACK_NE_SW, depth), "Source underground station should be protected by a signal.");
				report.check_true("destination_signal_built", GSMetro.BuildUndergroundSignal(destination_signal_tile, GSRail.RAILTRACK_NE_SW, depth), "Destination underground station should be protected by a signal.");

				local source_station_id = GSMetro.GetUndergroundStationID(source_station_tile, depth);
				local destination_station_id = GSMetro.GetUndergroundStationID(destination_station_tile, depth);
				report.check_true("source_station_valid", GSStation.IsValidStation(source_station_id), "Underground source station should resolve to a station id.");
				report.check_true("destination_station_valid", GSStation.IsValidStation(destination_station_id), "Underground destination station should resolve to a station id.");
				company_mode = null;

				/* Industries near the stations. Scripts run their first steps while the world is
				 * still being generated, when an industry does not look for stations nearby; the
				 * exits are therefore built afterwards, which recomputes the station catchment. */
				local source_built = world.build_industry_near(pair.raw_type, source_exit_tile, 8, 0);
				local destination_built = world.build_industry_near(pair.processing_type, destination_exit_tile, 8, 0);
				report.check_true("source_industry_built", source_built != null && GSIndustry.IsValidIndustry(source_built.industry), "Raw source industry should be built near the source exit.");
				report.check_true("destination_industry_built", destination_built != null && GSIndustry.IsValidIndustry(destination_built.industry), "Processing destination industry should be built near the destination exit.");

				company_mode = GSCompanyMode(company);
				report.check_true("source_exit_built", GSStation.IsValidStation(source_station_id) && this.build_exit_towards(build, source_station_tile, source_station_id, source_built), "Source underground station should receive an explicit exit.");
				report.check_true("destination_exit_built", GSStation.IsValidStation(destination_station_id) && this.build_exit_towards(build, destination_station_tile, destination_station_id, destination_built), "Destination underground station should receive an explicit exit.");
				report.check_true("source_industry_served", source_built != null && GSIndustry.GetAmountOfStationsAround(source_built.industry) > 0, "Source industry should be in the catchment of the source station.");

				local vehicle = trains.build_train(depot_tile, rail_engine);
				report.check_true("train_bought", vehicle != GSVehicle.VEHICLE_INVALID, "Freight test should buy a train in the depot.");

				local wagon_attach = vehicle == GSVehicle.VEHICLE_INVALID ? null : trains.attach_cargo_wagon(vehicle, depot_tile, pair.cargo);
				report.check_true("cargo_wagon_found", wagon_attach != null && wagon_attach.spec != null, "Freight scenario should find a wagon with capacity for the selected cargo.");
				report.check_true("cargo_wagon_built", wagon_attach != null && wagon_attach.wagon != GSVehicle.VEHICLE_INVALID, "Freight wagon should be built.");
				report.check_true("cargo_wagon_attached", wagon_attach != null && wagon_attach.attached, "Freight wagon should be attached to the train.");
				report.check_true("source_order_added", vehicle != GSVehicle.VEHICLE_INVALID && trains.append_underground_order(vehicle, source_station_id), "Order for the underground source station should be added.");
				report.check_true("destination_order_added", vehicle != GSVehicle.VEHICLE_INVALID && trains.append_underground_order(vehicle, destination_station_id), "Order for the underground destination station should be added.");
				report.check_eq("order_count", 2, vehicle == GSVehicle.VEHICLE_INVALID ? -1 : GSOrder.GetOrderCount(vehicle), "Freight line should have two station orders.");

				local source_production = source_built == null ? null : trains.wait_for_industry_last_month_production(source_built.industry, pair.cargo, 100000, 8);
				local source_waiting = source_production == null ? null : trains.wait_for_station_waiting_cargo(source_station_id, pair.cargo, 70000, 8);
				local destination_accepts = destination_station_id != null && world.station_accepts_cargo(destination_station_id, pair.cargo);
				report.check_true("source_industry_produces", source_production != null, "Source industry should produce freight for the selected cargo.");
				report.check_true("destination_station_accepts_cargo", destination_accepts, "Destination underground station should accept the selected freight cargo.");
				if (source_built != null && destination_built != null) {
					GSCargoMonitor.GetIndustryPickupAmount(company, pair.cargo, source_built.industry, true);
					GSCargoMonitor.GetIndustryDeliveryAmount(company, pair.cargo, destination_built.industry, true);
				}
				report.check_true("pre_start_save_written", GSTest.SaveGame("metro_industry_cargo_via_underground_final.sav"), "Scenario should save the freight fixture before launching the train.");
				report.check_true("start_vehicle", vehicle != GSVehicle.VEHICLE_INVALID && GSVehicle.StartStopVehicle(vehicle), "Freight train should be started.");

				local left_depot_ticks = trains.wait_for_departure(vehicle, 8000);
				local underground_entry = left_depot_ticks == null ? null : trains.wait_for_underground_entry(vehicle, 20000);
				local source_station_ticks = underground_entry == null ? null : trains.wait_for_underground_station(vehicle, source_station_id, 24000);
				local cargo_loaded = source_station_ticks == null ? null : trains.wait_for_vehicle_cargo(vehicle, pair.cargo, 4000, 1);
				local destination_station_ticks = cargo_loaded == null ? null : trains.wait_for_underground_station(vehicle, destination_station_id, 26000);
				local cargo_unloaded = destination_station_ticks == null ? null : trains.wait_for_vehicle_cargo_at_most(vehicle, pair.cargo, 4000, 0);
				local cargo_after_destination = cargo_unloaded == null ? (destination_station_ticks == null ? -1 : GSVehicle.GetCargoLoad(vehicle, pair.cargo)) : cargo_unloaded.amount;
				local industry_pickup_result = (destination_station_ticks == null || source_built == null) ? null : trains.wait_for_industry_pickup(company, pair.cargo, source_built.industry, 4000, 1);
				local industry_delivery_result = (destination_station_ticks == null || destination_built == null) ? null : trains.wait_for_industry_delivery(company, pair.cargo, destination_built.industry, 4000, 1);
				local industry_pickup = industry_pickup_result == null ? -1 : industry_pickup_result.amount;
				local industry_delivery = industry_delivery_result == null ? -1 : industry_delivery_result.amount;
				local source_transported = source_built == null ? -1 : GSIndustry.GetLastMonthTransported(source_built.industry, pair.cargo);
				local destination_stockpiled = destination_built == null ? -1 : GSIndustry.GetStockpiledCargo(destination_built.industry, pair.cargo);

				report.check_true("source_station_waiting_cargo", source_waiting != null || cargo_loaded != null, "Source underground station should provide freight to the train via its explicit exit.");
				report.check_true("train_left_depot", left_depot_ticks != null, "Freight train should leave the depot.");
				report.check_true("train_entered_underground", underground_entry != null, "Freight train should enter the underground loop.");
				report.check_true("train_serviced_source_station", source_station_ticks != null, "Freight train should stop at the source underground station.");
				report.check_true("train_loaded_cargo", cargo_loaded != null, "Freight train should load cargo at the source underground station.");
				report.check_true("train_serviced_destination_station", destination_station_ticks != null, "Freight train should stop at the destination underground station.");
				report.check_true("industry_pickup_recorded", industry_pickup_result != null || source_transported > 0, "Source industry should report transported freight once the metro line starts moving cargo.");
				report.check_true("industry_delivery_recorded", cargo_unloaded != null || industry_delivery_result != null, "Freight train should unload its cargo at the destination underground station.");
				report.check_true("destination_stockpile_positive", industry_delivery_result != null || destination_stockpiled > 0 || cargo_unloaded != null, "Destination branch should consume or stockpile the delivered cargo after the train unloads.");

				/* The exits serve passengers and mail only: the freight industries lose the
				 * service through them, cargo unloaded for a transfer still moves. */
				local at_destination = vehicle == GSVehicle.VEHICLE_INVALID ? null : trains.wait_for_underground_station(vehicle, destination_station_id, 30000);
				report.check_true("pm_train_back_at_destination", at_destination != null, "Freight train should come back to the destination station.");
				if (at_destination != null) GSVehicle.StartStopVehicle(vehicle);
				local stopped = vehicle != GSVehicle.VEHICLE_INVALID && GSVehicle.GetState(vehicle) != GSVehicle.VS_RUNNING;
				report.metric("pm_train_state", vehicle == GSVehicle.VEHICLE_INVALID ? -1 : GSVehicle.GetState(vehicle));
				report.check_true("pm_train_stopped", stopped, "Freight train should wait at the destination station.");

				local w0 = GSStation.GetCargoWaiting(source_station_id, pair.cargo);
				GSController.Sleep(3000);
				local w1 = GSStation.GetCargoWaiting(source_station_id, pair.cargo);
				report.metric("all_cargo_waiting_before", w0);
				report.metric("all_cargo_waiting_after", w1);
				report.check_true("all_cargo_still_supplied", w1 > w0, "With all cargo the source industry should keep supplying the station through its exit.");

				report.check_true("exit_cargo_pm_set", GSGameSettings.SetValue("underground.exit_cargo", 0), "Setting underground.exit_cargo should switch to passengers and mail.");
				report.check_eq("exit_cargo_pm_value", 0, GSGameSettings.GetValue("underground.exit_cargo"), "Setting value should be passengers and mail.");
				report.check_true("pm_destination_rejects", !world.station_accepts_cargo(destination_station_id, pair.cargo), "With passengers and mail the destination station should not accept the freight through its exit.");
				report.check_true("pm_source_industry_still_near", GSIndustry.GetAmountOfStationsAround(source_built.industry) > 0, "The station still lies around the source industry.");

				local w2 = GSStation.GetCargoWaiting(source_station_id, pair.cargo);
				GSController.Sleep(3000);
				local w3 = GSStation.GetCargoWaiting(source_station_id, pair.cargo);
				report.metric("pm_waiting_before", w2);
				report.metric("pm_waiting_after", w3);
				report.check_true("pm_source_not_supplied", w3 <= w2, "With passengers and mail the source industry should not hand freight to the station through its exit.");

				/* Transfer: the freight still waiting at the source is unloaded underground at the destination. */
				GSCargoMonitor.GetIndustryDeliveryAmount(company, pair.cargo, destination_built.industry, true);
				local dest_waiting_before = GSStation.GetCargoWaiting(destination_station_id, pair.cargo);
				report.check_true("pm_transfer_order", GSOrder.SetOrderFlags(vehicle, 1, GSOrder.OF_TRANSFER | GSOrder.OF_NO_LOAD), "The destination order should become a transfer.");
				report.check_true("pm_train_restarted", GSVehicle.StartStopVehicle(vehicle), "Freight train should start again.");
				local pm_source = trains.wait_for_underground_station(vehicle, source_station_id, 30000);
				local pm_loaded = pm_source == null ? null : trains.wait_for_vehicle_cargo(vehicle, pair.cargo, 4000, 1);
				local pm_destination = pm_loaded == null ? null : trains.wait_for_underground_station(vehicle, destination_station_id, 30000);
				local pm_unloaded = pm_destination == null ? null : trains.wait_for_vehicle_cargo_at_most(vehicle, pair.cargo, 4000, 0);
				local dest_waiting_after = GSStation.GetCargoWaiting(destination_station_id, pair.cargo);
				local pm_delivered = GSCargoMonitor.GetIndustryDeliveryAmount(company, pair.cargo, destination_built.industry, false);
				report.metric("pm_transfer_loaded", pm_loaded == null ? -1 : pm_loaded.amount);
				report.metric("pm_destination_waiting_before", dest_waiting_before);
				report.metric("pm_destination_waiting_after", dest_waiting_after);
				report.metric("pm_delivered_to_industry", pm_delivered);
				report.check_true("pm_transfer_loaded", pm_loaded != null, "The freight left at the source station should still be loaded.");
				report.check_true("pm_transfer_unloaded", pm_unloaded != null, "The freight should be unloaded for the transfer underground.");
				report.check_true("pm_transfer_waiting", dest_waiting_after > dest_waiting_before, "The transferred freight should wait at the destination station.");
				report.check_eq("pm_not_delivered", 0, pm_delivered, "No freight should reach the destination industry through the exit.");

				report.metric("cargo", pair.cargo);
				report.metric("cargo_name", GSCargo.GetName(pair.cargo));
				report.metric("raw_industry_type", pair.raw_type);
				report.metric("processing_industry_type", pair.processing_type);
				report.metric("source_station_id", source_station_id);
				report.metric("destination_station_id", destination_station_id);
				report.metric("source_industry_id", source_built == null ? -1 : source_built.industry);
				report.metric("destination_industry_id", destination_built == null ? -1 : destination_built.industry);
				report.metric("source_station_waiting_amount", source_waiting == null ? -1 : source_waiting.amount);
				report.metric("source_last_month_production", source_built == null ? -1 : GSIndustry.GetLastMonthProduction(source_built.industry, pair.cargo));
				report.metric("source_last_month_transported", source_built == null ? -1 : GSIndustry.GetLastMonthTransported(source_built.industry, pair.cargo));
				report.metric("source_last_month_transported_pct", source_built == null ? -1 : GSIndustry.GetLastMonthTransportedPercentage(source_built.industry, pair.cargo));
				report.metric("destination_stockpiled_cargo", destination_stockpiled);
				report.metric("cargo_after_destination", cargo_after_destination);
				report.metric("industry_pickup_amount", industry_pickup);
				report.metric("industry_delivery_amount", industry_delivery);
				report.metric("vehicle_profit_this_year", vehicle == GSVehicle.VEHICLE_INVALID ? -1 : GSVehicle.GetProfitThisYear(vehicle));
			}

			company_mode = null;
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.check_true("final_save_written", GSTest.SaveGame("metro_industry_cargo_via_underground_final.sav"), "Scenario should save the final freight world for manual inspection.");
		report.finish();
	}
}
