/**
 * metro_reload_underground_trains
 *
 * Regression for savegame loading with underground trains: the fixture is the
 * final save of metro_loop_two_underground_stations (two trains mid-route on
 * a half-underground loop with a forked tunnel). Multilayer slice IDs are
 * rebuilt from scratch on load, so each train's underground state must be
 * re-resolved by position; before that fix, loaded consists derailed within
 * seconds (split + chosen_track assert).
 *
 * Pass criteria: after loading, each train registers at least 2 surface and
 * 2 underground station stops within the budget — i.e. the loaded trains keep
 * doing their job.
 */
class MetroReloadUndergroundTrains extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);

		local report = MetroTest("metro_reload_underground_trains", "fixture.sav", null);
		local trains = MetroTest.MetroTrainLib();

		try {
			local vehicles = GSVehicleList();
			vehicles.Valuate(GSVehicle.GetVehicleType);
			vehicles.KeepValue(GSVehicle.VT_RAIL);

			local ids = [];
			for (local v = vehicles.Begin(); !vehicles.IsEnd(); v = vehicles.Next()) ids.append(v);
			report.check_eq("trains_found", 2, ids.len(), "The fixture should contain exactly two trains.");

			if (ids.len() == 2) {
				local stats = {};
				foreach (t in ids) {
					stats[t] <- { surf = 0, ug = 0, was_surf = false, was_ug = false };
					/* Make sure the loaded trains are running. */
					if (GSVehicle.GetState(t) == GSVehicle.VS_STOPPED) GSVehicle.StartStopVehicle(t);
				}

				local budget = 40000;
				local elapsed = 0;
				while (elapsed < budget) {
					if (elapsed % 5000 == 0) {
						foreach (t, s in stats) {
							local loc = GSVehicle.GetLocation(t);
							GSLog.Info("tick+" + elapsed + " t" + t +
								" pos=(" + GSMap.GetTileX(loc) + "," + GSMap.GetTileY(loc) + ")" +
								" state=" + GSVehicle.GetState(t) +
								" ug=" + (GSMetro.IsVehicleUnderground(t) ? 1 : 0) +
								" stops=" + s.surf + "/" + s.ug);
						}
					}
					local done = true;
					foreach (t, s in stats) {
						local at_station = GSVehicle.GetState(t) == GSVehicle.VS_AT_STATION;
						local under = GSMetro.IsVehicleUnderground(t);
						local at_surf = at_station && !under;
						local at_ug = at_station && under;
						if (at_surf && !s.was_surf) s.surf++;
						if (at_ug && !s.was_ug) s.ug++;
						s.was_surf = at_surf;
						s.was_ug = at_ug;
						if (s.surf < 2 || s.ug < 2) done = false;
					}
					if (done) break;
					GSController.Sleep(5);
					elapsed += 5;
				}

				local i = 1;
				foreach (t, s in stats) {
					report.check_true("train" + i + "_keeps_working", s.surf >= 2 && s.ug >= 2, "Loaded train " + t + " must keep serving surface and underground stations.");
					report.metric("train" + i + "_surf_stops", s.surf);
					report.metric("train" + i + "_ug_stops", s.ug);
					i++;
				}
				report.metric("ticks_used", elapsed);
			}
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.finish();
	}
}
