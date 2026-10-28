/**
 * metro_reverse_at_portal
 *
 *   [stA]--surface--[depot]--[portal]--UG--UG--UG--[UG station]
 *
 * A train three tiles long shuttles between surface station A and the
 * underground station. Each time its front dives into the portal, and each
 * time it comes out again, the script waits k ticks (k = 0, 1, 2, ...) and
 * turns the train round, so that over the run the train is reversed with its
 * parts in every position across the portal tile: some on the surface heading
 * into the tunnel, some on the portal column, some below.
 *
 * Before the fix, a part left on the surface of the portal tile but heading
 * into the tunnel after the reversal was advanced by the surface code towards
 * the mouth, found no rails and the game stopped with "Disconnecting train".
 *
 * Pass: every reversal is accepted, the game never stops, and the train keeps
 * running (it still reaches both stations after the reversals).
 */
class MetroReverseAtPortal extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);

		local report = MetroTest("metro_reverse_at_portal", null, 510131);
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();
		local metro = MetroTest.MetroLib(build);
		local trains = MetroTest.MetroTrainLib();

		try {
			local area = build.find_buildable_rectangle(20, 8);
			report.check_true("build_area_found", area != null, "Expected a clear 20x8 area.");

			local company = companies.ensure_company();
			local company_mode = company == GSCompany.COMPANY_INVALID ? null : GSCompanyMode(company);
			/* A powered engine with passenger wagons of ANOTHER length (the xUSSR
			 * sets loaded by scenario.cfg have them, like the user's ChS4T with
			 * 61-425 coaches): only then does a reversal advance wagons, which is
			 * where the portal bug was. The whole line is built for its rail type. */
			local engine = null;
			if (company_mode != null) {
				local best = -1;
				local engines = GSEngineList(GSVehicle.VT_RAIL);
				for (local e = engines.Begin(); !engines.IsEnd(); e = engines.Next()) {
					if (!GSEngine.IsBuildable(e) || GSEngine.IsWagon(e) || GSEngine.GetPower(e) <= 0) continue;
					if (GSEngine.GetCapacity(e) > 0) continue; /* a locomotive, not a multiple unit */
					local k = GSEngine.GetMaxSpeed(e);
					if (k > best) { best = k; engine = e; }
				}
			}
			report.check_true("rail_engine_found", engine != null, "Expected a buildable locomotive.");
			local wagon = null;
			if (engine != null) {
				local rt = GSEngine.GetRailType(engine);
				GSRail.SetCurrentRailType(rt);
				report.metric("railtype", rt);
			}

			if (area != null && engine != null) {
				local x = build.tile_x(area);
				local y = build.tile_y(area) + 3;
				local depth = -1;

				local station_a_tile  = build.make_tile(x + 0,  y);
				local depot_junction  = build.make_tile(x + 6,  y);
				local depot_front     = build.make_tile(x + 6,  y + 1);
				local depot_tile      = build.make_tile(x + 6,  y + 2);
				local portal_tile     = build.make_tile(x + 7,  y);
				local ug_station_tile = build.make_tile(x + 13, y);

				report.check_true("station_a_built", build.build_surface_station_x(station_a_tile), "Surface station A should be built.");
				report.check_true("surface_built",
					build.build_surface_x(x + 2, x + 6, y) &&
					GSRail.BuildRail(build.make_tile(x + 5, y), depot_junction, depot_front) &&
					GSRail.BuildRailTrack(depot_junction, GSRail.RAILTRACK_SW_SE),
					"Surface track and depot junction should be built.");
				report.check_true("depot_built",
					GSRail.BuildRail(depot_tile, depot_front, depot_junction) &&
					GSRail.BuildRailDepot(depot_tile, depot_front),
					"Train depot should be built and connected.");
				report.check_true("portal_built", metro.build_portal(portal_tile, GSMetro.DIAGDIR_SW, depth), "Portal should be built.");
				report.check_true("ug_built", metro.build_underground_x(x + 8, x + 12, y, depth), "Underground line should be built.");
				report.check_true("ug_station_built", metro.build_station_x(ug_station_tile, depth), "Underground platform should be built.");

				local sid = GSMetro.GetUndergroundStationID(ug_station_tile, depth);
				/* Engine plus passenger wagons whose length differs from the engine's,
				 * about three tiles in total. */
				local train = GSVehicle.BuildVehicle(depot_tile, engine);
				local engine_len = GSVehicle.IsValidVehicle(train) ? GSVehicle.GetLength(train) : 0;
				local mixed = false;
				if (GSVehicle.IsValidVehicle(train)) {
					local rt = GSEngine.GetRailType(engine);
					local wagons = GSEngineList(GSVehicle.VT_RAIL);
					for (local w = wagons.Begin(); !wagons.IsEnd() && wagon == null; w = wagons.Next()) {
						if (!GSEngine.IsBuildable(w) || !GSEngine.IsWagon(w) || !GSEngine.CanRunOnRail(w, rt)) continue;
						if (GSEngine.GetCapacity(w) <= 0) continue;
						local probe = GSVehicle.BuildVehicle(depot_tile, w);
						if (!GSVehicle.IsValidVehicle(probe)) continue;
						local before = GSVehicle.GetLength(train);
						if (!GSVehicle.MoveWagon(probe, 0, train, 0)) { GSVehicle.SellVehicle(probe); continue; }
						local wl = GSVehicle.GetLength(train) - before;
						if (wl != engine_len) {
							wagon = w;
							mixed = true;
						} else {
							GSVehicle.SellWagon(train, GSVehicle.GetNumWagons(train) - 1);
						}
					}
					/* Fill up to about three tiles with that wagon. */
					while (wagon != null && GSVehicle.GetLength(train) < 48) {
						local more = GSVehicle.BuildVehicle(depot_tile, wagon);
						if (!GSVehicle.IsValidVehicle(more) || !GSVehicle.MoveWagon(more, 0, train, 0)) break;
					}
				}
				report.check_true("train_built", GSVehicle.IsValidVehicle(train), "Train should be built.");
				report.check_true("mixed_lengths", mixed, "Expected a wagon whose length differs from the engine's (xUSSR sets).");
				report.metric("engine_length", engine_len);
				report.metric("train_length", GSVehicle.IsValidVehicle(train) ? GSVehicle.GetLength(train) : 0);
				report.metric("train_parts", GSVehicle.IsValidVehicle(train) ? GSVehicle.GetNumWagons(train) : 0);
				if (!GSVehicle.IsValidVehicle(train)) train = GSVehicle.VEHICLE_INVALID;

				if (train != GSVehicle.VEHICLE_INVALID) {
					report.check_true("order_ug", trains.append_underground_order(train, sid), "Underground order should be added.");
					report.check_true("order_surf", trains.append_surface_order(train, station_a_tile), "Surface order should be added.");
					GSVehicle.StartStopVehicle(train);

					local reversals = 0;
					local refused = 0;
					local lost = false;
					/* Alternate: reverse k ticks after a dive, then k ticks after a surfacing. */
					for (local k = 0; k < 24 && !lost; k++) {
						foreach (want_underground in [true, false]) {
							/* Wait for the transition. */
							local seen = false;
							local was = GSMetro.IsVehicleUnderground(train);
							for (local t = 0; t < 6000; t++) {
								if (!GSVehicle.IsValidVehicle(train) || GSVehicle.GetState(train) == GSVehicle.VS_CRASHED) { lost = true; break; }
								local now = GSMetro.IsVehicleUnderground(train);
								if (now == want_underground && was != want_underground) { seen = true; break; }
								was = now;
								GSController.Sleep(1);
							}
							if (lost || !seen) continue;
							if (k > 0) GSController.Sleep(k);
							if (!GSVehicle.IsValidVehicle(train)) { lost = true; break; }
							if (GSVehicle.ReverseVehicle(train)) {
								reversals++;
							} else {
								refused++;
							}
							/* Let it settle and drive on before the next round. */
							GSController.Sleep(40);
						}
					}
					report.metric("reversals", reversals);
					report.metric("refused", refused);
					report.check_true("train_kept", !lost, "The train must survive all reversals (not crash or vanish).");
					report.check_true("enough_reversals", reversals >= 20, "Expected at least 20 reversals across the portal, got " + reversals + ".");

					/* After all that the train still serves both stations. */
					local ug_ticks = trains.wait_for_underground_station(train, sid, 30000);
					report.check_true("reached_ug_station_after", ug_ticks != null, "Train must still reach the underground station.");
					local surf_ticks = trains.wait_for_surface_station(train, station_a_tile, 30000);
					report.check_true("reached_surface_station_after", surf_ticks != null, "Train must still reach surface station A.");
				}
			}
			company_mode = null;
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.finish();
	}
}
