class MetroLoopTwoPortalsDualUnderground extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);

		local report = MetroTest("metro_loop_two_portals_dual_underground", null, 510009);
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();
		local metro = MetroTest.MetroLib(build);
		local trains = MetroTest.MetroTrainLib();

		try {
			local area = build.find_buildable_rectangle(48, 18);
			report.check_true("build_area_found", area != null, "Expected a clear area for the two-portal underground loop scenario.");

			local company = companies.ensure_company();
			local company_mode = company == GSCompany.COMPANY_INVALID ? null : GSCompanyMode(company);
			local engine = company_mode == null ? null : trains.find_rail_engine();
			local wagon_engine = company_mode == null ? null : trains.find_rail_wagon();
			report.check_true("rail_engine_found", engine != null, "Expected one buildable rail engine for the loop scenario.");
			report.check_true("rail_wagon_found", wagon_engine != null, "Expected one buildable wagon for the loop scenario.");

			if (area != null && engine != null && wagon_engine != null && company != GSCompany.COMPANY_INVALID) {
				local x = build.tile_x(area);
				local y = build.tile_y(area);
				local depth = -1;

				local top_y = y + 6;
				local bottom_y = y + 10;
				local portal_x = x + 10;
				local right_x = x + 28;
				local underground_left_x = x + 4;

				local top_portal = build.make_tile(portal_x, top_y);
				local bottom_portal = build.make_tile(portal_x, bottom_y);
				local surface_station_tile = build.make_tile(x + 17, top_y);
				local depot_junction_tile = build.make_tile(x + 24, top_y);
				local depot_front = build.make_tile(x + 24, top_y + 1);
				local depot_tile = build.make_tile(x + 23, top_y + 1);

				local top_right_corner = build.make_tile(right_x, top_y);
				local bottom_right_corner = build.make_tile(right_x, bottom_y);

				local station_a_signal_tile = build.make_tile(x + 6, top_y);
				local station_a_tile = build.make_tile(x + 7, top_y);
				local top_underground_corner = build.make_tile(underground_left_x, top_y);

				local bottom_underground_corner = build.make_tile(underground_left_x, bottom_y);
				local station_b_tile = build.make_tile(x + 7, bottom_y);
				local station_b_signal_tile = build.make_tile(x + 8, bottom_y);

				report.check_true("top_portal_built", metro.build_portal(top_portal, GSMetro.DIAGDIR_NE, depth), "Top portal should return the train from underground to the surface.");
				/* A portal's direction is the way a train moves when it dives: here the tunnel lies west of the mouth. */
				report.check_true("bottom_portal_built", metro.build_portal(bottom_portal, GSMetro.DIAGDIR_NE, depth), "Bottom portal should send the train from the surface into the underground.");

				report.check_true("surface_station_built", build.build_surface_station_x(surface_station_tile), "Surface station should be built on the upper branch.");
				report.check_true("surface_top_row_left_built", build.build_surface_x(portal_x + 1, x + 16, top_y), "Upper branch should connect the top portal to the surface station.");
				report.check_true("surface_top_row_mid_built", build.build_surface_x(x + 19, x + 23, top_y), "Upper branch should continue from the surface station to the depot throat.");
				report.check_true("surface_top_row_junction_built", build.build_surface_x(x + 24, right_x - 1, top_y), "Upper branch should stay continuous past the depot junction.");
				report.check_true("surface_bottom_row_built", build.build_surface_x(portal_x + 1, right_x - 1, bottom_y), "Lower branch should connect the bottom portal to the right turn.");
				report.check_true("surface_right_column_built", build.build_surface_y(right_x, top_y + 1, bottom_y - 1), "Right column should close the surface U-turn.");
				report.check_true("surface_top_right_corner_built", GSRail.BuildRailTrack(top_right_corner, GSRail.RAILTRACK_NE_SE), "Top-right corner should connect the upper branch to the right column.");
				report.check_true("surface_bottom_right_corner_built", GSRail.BuildRailTrack(bottom_right_corner, GSRail.RAILTRACK_NW_NE), "Bottom-right corner should connect the right column back to the lower branch.");
				report.check_true("depot_junction_built", GSRail.BuildRail(build.make_tile(x + 23, top_y), depot_junction_tile, depot_front), "Depot junction should branch off the upper loop.");
				report.check_true("depot_siding_built", GSRail.BuildRail(depot_tile, depot_front, depot_junction_tile), "Depot siding should connect diagonally back to the loop.");
				report.check_true("depot_right_turn_built", GSRail.BuildRailTrack(depot_junction_tile, GSRail.RAILTRACK_SW_SE), "Depot junction should also connect the depot front toward the right side of the loop.");
				report.check_true("depot_built", GSRail.BuildRailDepot(depot_tile, depot_front), "Depot should be attached to the upper branch.");
				report.check_true("depot_front_connected", GSRail.AreTilesConnected(depot_tile, depot_front, depot_junction_tile), "Depot front should connect the depot to the upper-branch junction.");
				report.check_true("depot_junction_connected_left", GSRail.AreTilesConnected(build.make_tile(x + 23, top_y), depot_junction_tile, depot_front), "Depot junction should connect the upper branch to the depot front.");
				report.check_true("depot_junction_connected_right", GSRail.AreTilesConnected(build.make_tile(x + 25, top_y), depot_junction_tile, depot_front), "Depot junction should also let the train continue toward the right side of the loop.");

				report.check_true("bottom_underground_entry_built", metro.build_underground_x(underground_left_x + 1, portal_x - 1, bottom_y, depth), "Lower underground branch should connect the bottom portal to station B.");
				report.check_true("underground_station_b_built", metro.build_station_x(station_b_tile, depth), "Lower underground station should be built on its own branch.");
				report.check_true("bottom_underground_corner_built", GSMetro.BuildUndergroundRail(bottom_underground_corner, GSRail.RAILTRACK_NW_SW, depth), "Lower underground corner should turn the train toward the vertical connector.");
				report.check_true("underground_vertical_link_built", metro.build_underground_y(underground_left_x, top_y + 1, bottom_y - 1, depth), "Vertical underground connector should link both underground branches.");
				report.check_true("top_underground_corner_built", GSMetro.BuildUndergroundRail(top_underground_corner, GSRail.RAILTRACK_SW_SE, depth), "Upper underground corner should turn the train back toward the top portal.");
				report.check_true("top_underground_exit_built", metro.build_underground_x(underground_left_x + 1, portal_x - 1, top_y, depth), "Upper underground branch should connect station A to the top portal.");
				report.check_true("underground_station_a_built", metro.build_station_x(station_a_tile, depth), "Upper underground station should be built on its own branch.");

				report.check_true("underground_signal_a_built", GSMetro.BuildUndergroundSignal(station_a_signal_tile, GSRail.RAILTRACK_NE_SW, depth), "Upper underground signal should control station A.");
				/* The default direction is for westbound traffic; the upper branch runs east: turn it. */
				report.check_true("underground_signal_a_turned", GSMetro.BuildUndergroundSignal(station_a_signal_tile, GSRail.RAILTRACK_NE_SW, depth), "Upper underground signal should face the eastbound train.");
				report.check_true("underground_signal_b_built", GSMetro.BuildUndergroundSignal(station_b_signal_tile, GSRail.RAILTRACK_NE_SW, depth), "Lower underground signal should control station B.");

				local station_a_id = GSMetro.GetUndergroundStationID(station_a_tile, depth);
				local station_b_id = GSMetro.GetUndergroundStationID(station_b_tile, depth);
				local surface_station_id = GSStation.GetStationID(surface_station_tile);

				report.check_true("surface_station_valid", GSStation.IsValidStation(surface_station_id), "Surface station should resolve to a station id.");
				report.check_true("underground_station_a_valid", GSStation.IsValidStation(station_a_id), "Upper underground station should resolve to a station id.");
				report.check_true("underground_station_b_valid", GSStation.IsValidStation(station_b_id), "Lower underground station should resolve to a station id.");
				report.check_true("surface_station_named", GSStation.SetName(surface_station_id, "Metro Surface"), "Surface station should receive a deterministic name.");
				report.check_true("underground_station_a_named", GSStation.SetName(station_a_id, "Metro Underground A"), "Upper underground station should be renamed.");
				report.check_true("underground_station_b_named", GSStation.SetName(station_b_id, "Metro Underground B"), "Lower underground station should be renamed.");
				report.check_true("top_portal_visible", GSMetro.IsPortalTile(top_portal), "Top portal tile should contain a portal.");
				report.check_true("bottom_portal_visible", GSMetro.IsPortalTile(bottom_portal), "Bottom portal tile should contain a portal.");

				local vehicle = trains.build_train(depot_tile, engine);
				report.check_true("train_bought", vehicle != GSVehicle.VEHICLE_INVALID, "Train should be bought in the depot.");

				if (vehicle != GSVehicle.VEHICLE_INVALID) {
					local wagon_attach = trains.attach_wagon(vehicle, depot_tile, wagon_engine);
					report.check_true("wagon_bought", wagon_attach.wagon != GSVehicle.VEHICLE_INVALID, "A wagon should be built for the train.");
					report.check_true("wagon_attached", wagon_attach.attached, "The wagon should be attached to the train.");
					report.check_eq("train_unit_count", 2, GSVehicle.GetNumWagons(vehicle), "The train should consist of one engine and one wagon.");

					report.check_true("order_station_b_added", trains.append_underground_order(vehicle, station_b_id), "Order for underground station B should be added.");
					report.check_true("order_station_a_added", trains.append_underground_order(vehicle, station_a_id), "Order for underground station A should be added.");
					report.check_true("order_surface_added", trains.append_surface_order(vehicle, surface_station_tile), "Order for the surface station should be added.");
					report.check_eq("order_count", 3, GSOrder.GetOrderCount(vehicle), "Loop train should have all three stations in its orders.");
					report.check_true("pre_start_save_written", GSTest.SaveGame("metro_loop_two_portals_dual_underground_final.sav"), "Scenario should save the built metro loop before the train starts.");
					report.check_true("start_vehicle", GSVehicle.StartStopVehicle(vehicle), "Loop train should be started.");

					local left_depot_ticks = trains.wait_for_departure(vehicle, 6000);
					local bottom_portal_phase = left_depot_ticks == null ? null : trains.wait_for_portal_surface_or_entry(vehicle, bottom_portal, 12000);
					local bottom_portal_surface_ticks = bottom_portal_phase == null ? null : bottom_portal_phase.ticks;
					local underground_entry = bottom_portal_phase == null ? null :
						(bottom_portal_phase.seen_surface
							? trains.wait_for_underground_entry(vehicle, 12000)
							: { ticks = bottom_portal_phase.ticks, tile = GSMetro.GetVehicleUndergroundTile(vehicle), depth = GSMetro.GetVehicleUndergroundDepth(vehicle) });
					local station_b_ticks = underground_entry == null ? null : trains.wait_for_underground_station(vehicle, station_b_id, 18000);
					local station_a_ticks = station_b_ticks == null ? null : trains.wait_for_underground_station(vehicle, station_a_id, 18000);
					local surface_station_ticks = station_a_ticks == null ? null : trains.wait_for_surface_station(vehicle, surface_station_tile, 22000);
					local station_b_second_ticks = surface_station_ticks == null ? null : trains.wait_for_underground_station(vehicle, station_b_id, 22000);

					report.check_true("train_left_depot", left_depot_ticks != null, "Train should leave the depot.");
					report.check_true("train_reached_bottom_portal_surface", bottom_portal_surface_ticks != null, "Train should reach the lower portal tile on the surface or transition into it immediately.");
					report.check_true("train_entered_underground", underground_entry != null, "Train should enter the underground loop through the lower portal.");
					report.check_true("train_serviced_station_b", station_b_ticks != null, "Train should service underground station B.");
					report.check_true("train_serviced_station_a", station_a_ticks != null, "Train should service underground station A.");
					report.check_true("train_serviced_surface_station", surface_station_ticks != null, "Train should service the surface station.");
					report.check_true("loop_closed", station_b_second_ticks != null, "After servicing all stations, the train should re-enter the loop and reach station B again.");

					report.metric("vehicle", vehicle);
					report.metric("wagon_vehicle", wagon_attach.wagon);
					report.metric("surface_station_id", surface_station_id);
					report.metric("station_a_id", station_a_id);
					report.metric("station_b_id", station_b_id);
					report.metric("ticks_to_leave_depot", left_depot_ticks == null ? -1 : left_depot_ticks);
					report.metric("ticks_to_bottom_portal_surface", bottom_portal_surface_ticks == null ? -1 : bottom_portal_surface_ticks);
					report.metric("ticks_to_underground_entry", underground_entry == null ? -1 : underground_entry.ticks);
					report.metric("ticks_to_station_b", station_b_ticks == null ? -1 : station_b_ticks);
					report.metric("ticks_to_station_a", station_a_ticks == null ? -1 : station_a_ticks);
					report.metric("ticks_to_surface_station", surface_station_ticks == null ? -1 : surface_station_ticks);
					report.metric("ticks_to_second_station_b", station_b_second_ticks == null ? -1 : station_b_second_ticks);
					report.metric("final_vehicle_tile", GSVehicle.GetLocation(vehicle));
					report.metric("final_vehicle_state", GSVehicle.GetState(vehicle));
					report.metric("final_vehicle_speed", GSVehicle.GetCurrentSpeed(vehicle));
					report.metric("final_vehicle_underground", GSMetro.IsVehicleUnderground(vehicle));
					report.metric("final_vehicle_underground_tile", GSMetro.GetVehicleUndergroundTile(vehicle));
					report.metric("final_vehicle_underground_depth", GSMetro.GetVehicleUndergroundDepth(vehicle));
					report.metric("current_order_is_part_of_list", GSOrder.IsCurrentOrderPartOfOrderList(vehicle));
					report.metric("current_order_is_station_order", GSOrder.IsGotoStationOrder(vehicle, GSOrder.ORDER_CURRENT));
					report.metric("current_order_destination", GSOrder.GetOrderDestination(vehicle, GSOrder.ORDER_CURRENT));
				}
			}

			company_mode = null;
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.check_true("final_save_written", GSTest.SaveGame("metro_loop_two_portals_dual_underground_final.sav"), "Scenario should save the final world for manual inspection.");
		report.finish();
	}
}
