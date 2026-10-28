/**
 * metro_order_readd_while_loading
 *
 * Removing the order to an underground station while the train loads there
 * and adding it again must not make the train skip that station forever.
 * Same single-train loop as metro_station_stop_persists.
 *
 * Round 1: the underground order is full load (no cargo on the map, so the
 *          train is guaranteed to be loading). The order is removed and
 *          appended again (no flags). The train must leave, call at the
 *          surface station and call at the underground station again.
 * Round 2: while it loads there (normal order), the order is removed and
 *          appended again once more; it must still come back a third time.
 *          By now the train has called at a second underground station B
 *          that is not in its orders, so the list holds an implicit order:
 *          the re-added order must still be appended after the surface order
 *          (GSMetro.AppendOrderByStationID used to pass the manual-order
 *          count as the real insert index and put it in front instead).
 */
class MetroOrderReaddWhileLoading extends GSController {
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

	/** Wait until the train calls at the surface station and then at the underground one. */
	function WaitForLap(report, trains, loop, t, tag)
	{
		local surf = trains.wait_for_surface_station(t, loop.st_surf_tile, 20000);
		this.LogTrain(tag + "_surface", t);
		report.check_true(tag + "_surface_call", surf != null, "The train must call at the surface station.");
		if (surf == null) return false;
		local ug = trains.wait_for_underground_station(t, loop.sid_ug, 20000);
		this.LogTrain(tag + "_underground", t);
		report.check_true(tag + "_underground_call", ug != null, "The train must call at the re-added underground station again.");
		return ug != null;
	}

	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);

		local report = MetroTest("metro_order_readd_while_loading", null, 510903);
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
				/* A second underground station B, west of A, that is not in the
				 * order list: the train calls there as an intermediate stop,
				 * which adds an implicit order to its list. */
				local b_tile = GSMap.GetTileIndex(loop.x0 + 3, loop.yu);
				report.check_true("ug_station_b_built", metro.build_station_x(b_tile, loop.depth), "Intermediate underground station B should be built.");

				report.check_true("orders_added",
					trains.append_surface_order(t, loop.st_surf_tile) &&
					trains.append_underground_order(t, loop.sid_ug, GSOrder.OF_FULL_LOAD),
					"Surface + full-load underground orders should be added.");

				GSVehicle.StartStopVehicle(t);
				report.check_true("departed", trains.wait_for_departure(t, 6000) != null, "Train must leave the depot.");

				local arrive = trains.wait_for_underground_station(t, loop.sid_ug, 30000);
				report.check_true("reached_ug_station", arrive != null, "Train must reach the underground station.");

				if (arrive != null) {
					GSController.Sleep(100);
					report.check_true("loading_before_remove", GSVehicle.GetState(t) == GSVehicle.VS_AT_STATION, "Full-load train should still be loading.");

					/* Round 1: remove + re-add while (full-)loading. */
					report.check_true("r1_removed", GSOrder.RemoveOrder(t, 1), "Removing the underground order should succeed.");
					report.check_true("r1_readded", trains.append_underground_order(t, loop.sid_ug), "Re-adding the underground order should succeed.");
					this.LogTrain("r1_readded", t);
					report.check_eq("r1_order_count", 2, GSOrder.GetOrderCount(t), "Order list should be back to two orders.");

					if (this.WaitForLap(report, trains, loop, t, "r1")) {
						/* Round 2: remove + re-add during a normal stop. */
						local was_loading = GSVehicle.GetState(t) == GSVehicle.VS_AT_STATION;
						report.check_true("r2_removed", GSOrder.RemoveOrder(t, 1), "Removing the underground order again should succeed.");
						report.check_true("r2_readded", trains.append_underground_order(t, loop.sid_ug), "Re-adding the underground order again should succeed.");
						report.metric("r2_was_loading", was_loading ? 1 : 0);
						this.LogTrain("r2_readded", t);
						/* Appending must put the order after the surface order even
						 * though the list now holds an implicit order for B. */
						report.check_eq("r2_order_count", 2, GSOrder.GetOrderCount(t), "Order list should be back to two orders.");
						report.check_eq("r2_first_order_is_surface", loop.sid_surf, GSStation.GetStationID(GSOrder.GetOrderDestination(t, 0)), "The re-added order must be appended after the surface order.");
						report.check_true("r2_second_order_is_station", GSOrder.IsGotoStationOrder(t, 1), "The re-added underground order must be the last order.");
						this.WaitForLap(report, trains, loop, t, "r2");
					}
				}
			}
			company_mode = null;
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.check_true("final_save_written", GSTest.SaveGame("metro_order_readd_while_loading_final.sav"), "Scenario should save final state for manual inspection.");
		report.finish();
	}
}
