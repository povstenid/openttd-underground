/**
 * metro_station_stop_persists
 *
 * A player's Stop command must hold for a train standing at an underground
 * station. Single-train clockwise loop (north row surface with depot and a
 * surface station, south row underground between two portals with one
 * underground station). The train waits at the underground station on a
 * full-load order, is stopped (StartStopVehicle), and then the full-load order
 * is removed, so only the Stop holds it: it must not move for 30 days. After
 * the order is re-added and StartStopVehicle is called again it must resume,
 * reach the surface station and call at the underground station again.
 *
 * Regression: TrainEnterStation / Vehicle::LeaveStation used to clear
 * VehState::Stopped for underground trains and economy.cpp kept loading
 * stopped underground trains, so the train finished loading and drove off.
 */
class MetroStationStopPersists extends GSController {
	function BuildLoop(report, build, metro, trains)
	{
		local area = build.find_buildable_rectangle(22, 10);
		report.check_true("build_area_found", area != null, "Expected a clear 22x10 area for the loop.");
		local engine = trains.find_rail_engine();
		report.check_true("rail_engine_found", engine != null, "Expected at least one buildable rail engine.");
		if (area == null || engine == null) return null;

		local x0 = build.tile_x(area) + 1;
		local ys = build.tile_y(area) + 2;
		local yu = ys + 2;
		local xe = x0 + 15;
		local depth = -1;
		local T = function (tx, ty) { return GSMap.GetTileIndex(tx, ty); };

		local st_surf_tile = T(x0 + 6, ys);
		report.check_true("surface_line_built", build.build_surface_x(x0 + 1, xe - 1, ys), "North surface line should be built.");
		report.check_true("surf_station_built", GSRail.BuildRailStation(st_surf_tile, GSRail.RAILTRACK_NE_SW, 1, 2, GSStation.STATION_NEW), "Drive-through surface station should be built.");

		local depot_tile = T(x0 + 2, ys - 1);
		report.check_true("depot_built",
			GSRail.BuildRail(T(x0 + 1, ys), T(x0 + 2, ys), depot_tile) &&
			GSRail.BuildRail(depot_tile, T(x0 + 2, ys), T(x0 + 3, ys)) &&
			GSRail.BuildRailDepot(depot_tile, T(x0 + 2, ys)),
			"Depot north of the line should be built and connected.");

		report.check_true("east_side_built",
			GSRail.BuildRail(T(xe - 1, ys), T(xe, ys), T(xe, ys + 1)) &&
			GSRail.BuildRail(T(xe, ys), T(xe, ys + 1), T(xe, yu)) &&
			GSRail.BuildRail(T(xe, ys + 1), T(xe, yu), T(xe - 1, yu)),
			"East surface curve should be built.");
		report.check_true("west_side_built",
			GSRail.BuildRail(T(x0 + 1, yu), T(x0, yu), T(x0, ys + 1)) &&
			GSRail.BuildRail(T(x0, yu), T(x0, ys + 1), T(x0, ys)) &&
			GSRail.BuildRail(T(x0, ys + 1), T(x0, ys), T(x0 + 1, ys)),
			"West surface curve should be built.");

		report.check_true("portal_e_built", metro.build_portal(T(xe - 1, yu), GSMetro.DIAGDIR_NE, depth), "East portal (underground entry) should be built.");
		report.check_true("portal_w_built", metro.build_portal(T(x0 + 1, yu), GSMetro.DIAGDIR_SW, depth), "West portal (underground exit) should be built.");
		report.check_true("ug_line_built", metro.build_underground_x(x0 + 2, xe - 2, yu, depth), "Underground line between the portals should be built.");

		local st_ug_tile = T(x0 + 8, yu);
		report.check_true("ug_station_built", metro.build_station_x(st_ug_tile, depth), "Underground station should be built.");
		local sid_ug = GSMetro.GetUndergroundStationID(st_ug_tile, depth);
		report.check_true("ug_station_valid", GSStation.IsValidStation(sid_ug), "Underground station id should be valid.");
		local sid_surf = GSStation.GetStationID(st_surf_tile);
		report.check_true("surf_station_valid", GSStation.IsValidStation(sid_surf), "Surface station id should be valid.");

		/* One-way PBS signals enforcing clockwise traffic (front = west neighbour). */
		report.check_true("surf_signal_1", GSRail.BuildSignal(T(x0 + 9, ys), T(x0 + 8, ys), GSRail.SIGNALTYPE_PBS_ONEWAY), "Surface signal east of the station should be built.");
		report.check_true("surf_signal_2", GSRail.BuildSignal(T(x0 + 4, ys), T(x0 + 3, ys), GSRail.SIGNALTYPE_PBS_ONEWAY), "Surface signal west of the station should be built.");
		report.check_true("ug_signal_1", GSMetro.BuildUndergroundSignal(T(xe - 4, yu), GSRail.RAILTRACK_NE_SW, depth), "Underground signal east of the UG station should be built.");
		report.check_true("ug_signal_2", GSMetro.BuildUndergroundSignal(T(x0 + 5, yu), GSRail.RAILTRACK_NE_SW, depth), "Underground signal west of the UG station should be built.");

		return { depot_tile = depot_tile, st_surf_tile = st_surf_tile, sid_surf = sid_surf, sid_ug = sid_ug, engine = engine, x0 = x0, yu = yu, depth = depth };
	}

	function BuildTrain(report, trains, loop)
	{
		local t = trains.build_train(loop.depot_tile, loop.engine);
		report.check_true("train_built", t != GSVehicle.VEHICLE_INVALID, "Train should be built.");
		if (t == GSVehicle.VEHICLE_INVALID) return t;
		local wagon_engine = trains.find_rail_wagon();
		report.check_true("wagon_attached", wagon_engine != null && trains.attach_wagon(t, loop.depot_tile, wagon_engine).attached, "Train should get one wagon.");
		return t;
	}

	function LogTrain(tag, t)
	{
		local ug = GSMetro.IsVehicleUnderground(t);
		local loc = ug ? GSMetro.GetVehicleUndergroundTile(t) : GSVehicle.GetLocation(t);
		GSLog.Info(tag + " t" + t + " pos=(" + GSMap.GetTileX(loc) + "," + GSMap.GetTileY(loc) + ")" +
			" state=" + GSVehicle.GetState(t) + " speed=" + GSVehicle.GetCurrentSpeed(t) + " ug=" + (ug ? 1 : 0) +
			" cur_order=" + GSOrder.ResolveOrderPosition(t, GSOrder.ORDER_CURRENT) + " orders=" + GSOrder.GetOrderCount(t));
	}

	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);

		local report = MetroTest("metro_station_stop_persists", null, 510901);
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();
		local metro = MetroTest.MetroLib(build);
		local trains = MetroTest.MetroTrainLib();

		try {
			local company = companies.ensure_company();
			local company_mode = company == GSCompany.COMPANY_INVALID ? null : GSCompanyMode(company);
			local loop = company_mode == null ? null : this.BuildLoop(report, build, metro, trains);
			local t = loop == null ? GSVehicle.VEHICLE_INVALID : this.BuildTrain(report, trains, loop);

			if (t != GSVehicle.VEHICLE_INVALID) {
				/* Full load (no cargo on the map) keeps the train at the platform
				 * until the order is removed, so the Stop below reliably lands
				 * while it is loading there (an empty stop lasts a single tick). */
				report.check_true("orders_added",
					trains.append_surface_order(t, loop.st_surf_tile) &&
					trains.append_underground_order(t, loop.sid_ug, GSOrder.OF_FULL_LOAD),
					"Surface + full-load underground orders should be added.");

				GSVehicle.StartStopVehicle(t);
				report.check_true("departed", trains.wait_for_departure(t, 6000) != null, "Train must leave the depot.");

				local arrive = trains.wait_for_underground_station(t, loop.sid_ug, 30000);
				report.check_true("reached_ug_station", arrive != null, "Train must reach the underground station.");

				if (arrive != null) {
					GSController.Sleep(20);
					report.check_true("loading_before_stop", GSVehicle.GetState(t) == GSVehicle.VS_AT_STATION, "Full-load train should be loading at the underground station.");

					/* Player presses Stop while the train is standing at the platform. */
					report.check_true("stop_accepted", GSVehicle.StartStopVehicle(t), "Stop command should be accepted.");
					local stop_tile = GSMetro.GetVehicleUndergroundTile(t);
					this.LogTrain("stopped", t);

					/* Drop the full-load order: nothing holds the train at the
					 * station any more except the player's Stop. */
					report.check_true("full_load_order_removed", GSOrder.RemoveOrder(t, 1), "Removing the full-load order should succeed.");

					/* 30 days: the train must stay on the same platform tile, at speed 0. */
					local hold_ticks = 30 * 74;
					local moved_at = -1;
					for (local i = 0; i < hold_ticks; i += 5) {
						if (!GSMetro.IsVehicleUnderground(t) ||
								GSMetro.GetVehicleUndergroundTile(t) != stop_tile ||
								GSVehicle.GetCurrentSpeed(t) != 0 ||
								GSVehicle.GetState(t) == GSVehicle.VS_RUNNING) {
							moved_at = i;
							this.LogTrain("moved", t);
							break;
						}
						if (i % 500 == 0) this.LogTrain("hold+" + i, t);
						GSController.Sleep(5);
					}
					report.check_eq("stays_stopped_30_days", -1, moved_at, "A stopped train must not restart by itself at the underground station.");
					report.metric("moved_after_ticks", moved_at);

					/* Player re-adds the order and presses Start: the train must
					 * leave, reach the surface station and call here again. */
					report.check_true("order_readded", trains.append_underground_order(t, loop.sid_ug), "Re-adding the underground order should succeed.");
					report.check_true("start_accepted", GSVehicle.StartStopVehicle(t), "Start command should be accepted.");
					local left = false;
					for (local i = 0; i < 6000 && !left; i += 5) {
						if (!GSMetro.IsVehicleUnderground(t) || GSMetro.GetVehicleUndergroundTile(t) != stop_tile) left = true;
						GSController.Sleep(5);
					}
					report.check_true("resumes_after_start", left, "After Start the train must leave the underground platform.");
					local surf = trains.wait_for_surface_station(t, loop.st_surf_tile, 20000);
					this.LogTrain("after_start", t);
					report.check_true("reaches_surface_after_start", surf != null, "After Start the train must continue to the surface station.");
					local again = trains.wait_for_underground_station(t, loop.sid_ug, 20000);
					report.check_true("calls_again_after_start", again != null, "The train must call at the underground station again.");
				}
			}
			company_mode = null;
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.check_true("final_save_written", GSTest.SaveGame("metro_station_stop_persists_final.sav"), "Scenario should save final state for manual inspection.");
		report.finish();
	}
}
