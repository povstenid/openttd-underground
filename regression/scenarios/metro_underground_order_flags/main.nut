/**
 * metro_underground_order_flags
 *
 * Order flags must be settable on orders to underground stations. Same
 * single-train loop as metro_station_stop_persists. The underground order is
 * created without flags (GSMetro.AppendOrderByStationID), then
 * GSOrder.SetOrderFlags switches it to non-stop + full load. There is no
 * cargo on the map, so the train must wait at the underground station. After
 * the flags are set back to non-stop only, the train must leave again and
 * reach the surface station.
 *
 * Regression: ScriptOrder::SetOrderFlags validated the flags against
 * GetOrderDestination(), which returns INVALID_TILE for a station that has no
 * surface rail station tile, so every call on an underground order failed.
 */
class MetroUndergroundOrderFlags extends GSController {
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

		local report = MetroTest("metro_underground_order_flags", null, 510902);
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
				report.check_true("orders_added",
					trains.append_surface_order(t, loop.st_surf_tile) && trains.append_underground_order(t, loop.sid_ug),
					"Surface + underground orders should be added.");

				/* Control: flags on the surface order. */
				report.check_true("surface_flags_set", GSOrder.SetOrderFlags(t, 0, GSOrder.OF_NON_STOP_INTERMEDIATE), "SetOrderFlags on the surface order should succeed.");

				local wanted = GSOrder.OF_NON_STOP_INTERMEDIATE | GSOrder.OF_FULL_LOAD;
				local ok = GSOrder.SetOrderFlags(t, 1, wanted);
				if (!ok) GSLog.Info("SetOrderFlags failed: " + GSError.GetLastErrorString());
				report.check_true("ug_flags_set", ok, "SetOrderFlags(non-stop + full load) on the underground order should succeed.");
				report.check_eq("ug_flags_read_back", wanted, GSOrder.GetOrderFlags(t, 1), "Underground order flags should read back as set.");

				/* Unload flags too. */
				local unload = GSOrder.OF_NON_STOP_INTERMEDIATE | GSOrder.OF_UNLOAD | GSOrder.OF_NO_LOAD;
				report.check_true("ug_unload_flags_set", GSOrder.SetOrderFlags(t, 1, unload), "SetOrderFlags(unload + no load) on the underground order should succeed.");
				report.check_eq("ug_unload_flags_read_back", unload, GSOrder.GetOrderFlags(t, 1), "Unload flags should read back as set.");
				report.check_true("ug_flags_restored", GSOrder.SetOrderFlags(t, 1, wanted), "Switching back to non-stop + full load should succeed.");

				GSVehicle.StartStopVehicle(t);
				report.check_true("departed", trains.wait_for_departure(t, 6000) != null, "Train must leave the depot.");

				local arrive = trains.wait_for_underground_station(t, loop.sid_ug, 30000);
				report.check_true("reached_ug_station", arrive != null, "Train must reach the underground station.");

				if (arrive != null) {
					/* Full load with no cargo on the map: it must keep waiting. */
					local left_at = -1;
					for (local i = 0; i < 20 * 74; i += 5) {
						if (GSVehicle.GetState(t) != GSVehicle.VS_AT_STATION) {
							left_at = i;
							this.LogTrain("left", t);
							break;
						}
						GSController.Sleep(5);
					}
					report.check_eq("waits_for_full_load", -1, left_at, "A full-load underground order must keep the train at the station.");

					/* Drop full load while it waits: the train must leave and go on. */
					report.check_true("full_load_cleared", GSOrder.SetOrderFlags(t, 1, GSOrder.OF_NON_STOP_INTERMEDIATE), "Clearing full load on the underground order should succeed.");
					local surf = trains.wait_for_surface_station(t, loop.st_surf_tile, 20000);
					this.LogTrain("after_clear", t);
					report.check_true("leaves_after_clear", surf != null, "Without full load the train must leave and reach the surface station.");

					/* And it still calls at the underground station on the next lap. */
					local again = trains.wait_for_underground_station(t, loop.sid_ug, 20000);
					report.check_true("calls_again", again != null, "The train must call at the underground station again.");
				}
			}
			company_mode = null;
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.check_true("final_save_written", GSTest.SaveGame("metro_underground_order_flags_final.sav"), "Scenario should save final state for manual inspection.");
		report.finish();
	}
}
