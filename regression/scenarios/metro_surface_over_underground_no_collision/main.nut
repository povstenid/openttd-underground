/**
 * metro_surface_over_underground_no_collision
 *
 * Two independent loops of the SAME company:
 *
 *  - loop B (metro): surface north side with depot + station, south side
 *    underground between two portals, with an underground station;
 *  - loop A (surface): a small rectangle whose two north-south columns run
 *    straight over loop B's underground line.
 *
 *   ys   ═[stB]═══════════════════════════  (loop B surface, eastbound)
 *   ys+1        ╔═[st2]══╗                  (loop A north row)
 *   yu   P2 ~~~~~~[U]~║~~~║~~~~~ P1         (loop B underground, westbound; loop A columns above)
 *   ...         ║        ║
 *   yu+4        ╚═[st1]══╝                  (loop A south row)
 *
 * The metro train is first parked at the underground station (a "full load"
 * order with no cargo around), which puts one of its wagons exactly under
 * loop A's west column. The surface train then laps loop A, driving straight
 * over it. Afterwards both trains run together, the metro dwelling at its
 * underground station on every lap. Trains on different layers must never
 * collide.
 */
function LayerIsCrashed(v)
{
	return !GSVehicle.IsValidVehicle(v) || GSVehicle.GetState(v) == GSVehicle.VS_CRASHED;
}

/** Station id the vehicle is currently stopped at (surface or underground), or -1. */
function LayerStoppedStation(v)
{
	if (!GSVehicle.IsValidVehicle(v) || GSVehicle.GetState(v) != GSVehicle.VS_AT_STATION) return -1;
	if (GSMetro.IsVehicleUnderground(v)) {
		return GSMetro.GetUndergroundStationID(GSMetro.GetVehicleUndergroundTile(v), GSMetro.GetVehicleUndergroundDepth(v));
	}
	return GSStation.GetStationID(GSVehicle.GetLocation(v));
}

/**
 * Metro dwell control. The metro train's underground-station order is "full
 * load" without any cargo around, so it parks there until released; releasing
 * means skipping to its surface-station order. While both trains run, every
 * underground stop is released after a fixed dwell, so each lap still parks
 * the metro under the surface line for a while.
 */
class MetroDweller {
	_mt = null;
	_sid_ug = -1;
	_wait = 0;
	releases = 0;

	constructor(mt, sid_ug)
	{
		this._mt = mt;
		this._sid_ug = sid_ug;
		this._wait = 0;
		this.releases = 0;
	}

	function release()
	{
		this._wait = 0;
		if (GSOrder.SkipToOrder(this._mt, 0)) this.releases++;
	}

	/** Call every tick step; releases the metro after `dwell` ticks at the underground station. */
	function poll(step, dwell)
	{
		if (LayerStoppedStation(this._mt) == this._sid_ug) {
			this._wait += step;
			if (this._wait >= dwell) this.release();
		} else {
			this._wait = 0;
		}
	}
}

class ArrivalCounter {
	_vehicle = null;
	_was = -1;
	arrivals = 0;
	per_station = null;

	constructor(vehicle)
	{
		this._vehicle = vehicle;
		this._was = -1;
		this.arrivals = 0;
		this.per_station = {};
	}

	function poll()
	{
		local st = LayerStoppedStation(this._vehicle);
		if (st != -1 && st != this._was) {
			this.arrivals++;
			if (!(st in this.per_station)) this.per_station[st] <- 0;
			this.per_station[st]++;
		}
		this._was = st;
	}

	function count_at(st) { return (st in this.per_station) ? this.per_station[st] : 0; }
}

class MetroSurfaceOverUndergroundNoCollision extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);

		local report = MetroTest("metro_surface_over_underground_no_collision", null, 610201);
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();
		local metro = MetroTest.MetroLib(build);
		local trains = MetroTest.MetroTrainLib();
		local T = function (tx, ty) { return GSMap.GetTileIndex(tx, ty); };

		try {
			local area = build.find_buildable_rectangle(24, 12);
			report.check_true("build_area_found", area != null, "Expected a clear 24x12 area.");

			local company = companies.ensure_company();
			local company_mode = company == GSCompany.COMPANY_INVALID ? null : GSCompanyMode(company);
			local engine = company_mode == null ? null : trains.find_rail_engine();
			local wagon_engine = company_mode == null ? null : trains.find_rail_wagon();
			report.check_true("rail_engine_found", engine != null, "Expected at least one buildable rail engine.");
			report.check_true("rail_wagon_found", wagon_engine != null, "Expected at least one buildable rail wagon.");

			if (area != null && engine != null && wagon_engine != null && company != GSCompany.COMPANY_INVALID) {
				local x0 = build.tile_x(area) + 1;
				local ys = build.tile_y(area) + 2;
				local yu = ys + 2;
				local xe = x0 + 19;
				local depth = -1;

				/* ---------------- loop B: the metro loop ---------------- */
				local stb_tile = T(x0 + 6, ys);
				report.check_true("b_surface_line", build.build_surface_x(x0 + 1, xe - 1, ys), "Loop B north surface line should be built.");
				report.check_true("b_station", GSRail.BuildRailStation(stb_tile, GSRail.RAILTRACK_NE_SW, 1, 2, GSStation.STATION_NEW), "Loop B surface station should be built.");
				local depot_b = T(x0 + 2, ys - 1);
				report.check_true("b_depot",
					GSRail.BuildRail(T(x0 + 1, ys), T(x0 + 2, ys), depot_b) &&
					GSRail.BuildRail(depot_b, T(x0 + 2, ys), T(x0 + 3, ys)) &&
					GSRail.BuildRailDepot(depot_b, T(x0 + 2, ys)),
					"Loop B depot should be built.");
				report.check_true("b_east_side",
					GSRail.BuildRail(T(xe - 1, ys), T(xe, ys), T(xe, ys + 1)) &&
					GSRail.BuildRail(T(xe, ys), T(xe, ys + 1), T(xe, yu)) &&
					GSRail.BuildRail(T(xe, ys + 1), T(xe, yu), T(xe - 1, yu)),
					"Loop B east curve should be built.");
				report.check_true("b_west_side",
					GSRail.BuildRail(T(x0 + 1, yu), T(x0, yu), T(x0, ys + 1)) &&
					GSRail.BuildRail(T(x0, yu), T(x0, ys + 1), T(x0, ys)) &&
					GSRail.BuildRail(T(x0, ys + 1), T(x0, ys), T(x0 + 1, ys)),
					"Loop B west curve should be built.");
				report.check_true("b_portal_e", metro.build_portal(T(xe - 1, yu), GSMetro.DIAGDIR_NE, depth), "Loop B east portal should be built.");
				report.check_true("b_portal_w", metro.build_portal(T(x0 + 1, yu), GSMetro.DIAGDIR_SW, depth), "Loop B west portal should be built.");
				report.check_true("b_ug_line", metro.build_underground_x(x0 + 2, xe - 2, yu, depth), "Loop B underground line should be built.");
				local ug_station_x = x0 + 8;
				report.check_true("b_ug_station", metro.build_station_x(T(ug_station_x, yu), depth), "Loop B underground station should be built.");
				report.check_true("b_signals",
					GSRail.BuildSignal(T(x0 + 9, ys), T(x0 + 8, ys), GSRail.SIGNALTYPE_PBS_ONEWAY) &&
					GSRail.BuildSignal(T(x0 + 4, ys), T(x0 + 3, ys), GSRail.SIGNALTYPE_PBS_ONEWAY) &&
					GSMetro.BuildUndergroundSignal(T(xe - 4, yu), GSRail.RAILTRACK_NE_SW, depth) &&
					GSMetro.BuildUndergroundSignal(T(x0 + 5, yu), GSRail.RAILTRACK_NE_SW, depth),
					"Loop B signals should be built.");
				local sid_b_surf = GSStation.GetStationID(stb_tile);
				local sid_b_ug = GSMetro.GetUndergroundStationID(T(ug_station_x, yu), depth);
				report.check_true("b_station_ids", GSStation.IsValidStation(sid_b_surf) && GSStation.IsValidStation(sid_b_ug), "Loop B station ids should be valid.");

				/* ---------------- loop A: the surface loop over the tunnel ---------------- */
				/* West column runs over the tile just east of the underground station:
				 * a westbound metro train stopped at that 1-tile platform has its first
				 * wagon centred under it. */
				local xa = ug_station_x + 1;
				local xb = xa + 4;
				local yn = yu - 1;
				local yss = yu + 4;
				local st2_tile = T(xa + 1, yn);
				local st1_tile = T(xa + 1, yss);
				report.check_true("a_north_row",
					GSRail.BuildRail(T(xa + 1, yn), T(xa, yn), T(xa, yn + 1)) &&
					GSRail.BuildRailStation(st2_tile, GSRail.RAILTRACK_NE_SW, 1, 2, GSStation.STATION_NEW) &&
					build.build_surface_x(xa + 3, xb - 1, yn) &&
					GSRail.BuildRail(T(xb - 1, yn), T(xb, yn), T(xb, yn + 1)),
					"Loop A north row should be built.");
				report.check_true("a_columns",
					build.build_surface_y(xa, yn + 1, yss - 1) &&
					build.build_surface_y(xb, yn + 1, yss - 1),
					"Loop A columns (over the tunnel) should be built.");
				report.check_true("a_south_row",
					GSRail.BuildRail(T(xa, yss - 1), T(xa, yss), T(xa + 1, yss)) &&
					GSRail.BuildRailStation(st1_tile, GSRail.RAILTRACK_NE_SW, 1, 2, GSStation.STATION_NEW) &&
					build.build_surface_x(xa + 3, xb - 1, yss) &&
					GSRail.BuildRail(T(xb, yss - 1), T(xb, yss), T(xb - 1, yss)),
					"Loop A south row should be built.");
				local depot_a = T(xa - 1, yu + 2);
				report.check_true("a_depot",
					GSRail.BuildRail(T(xa, yu + 1), T(xa, yu + 2), depot_a) &&
					GSRail.BuildRail(depot_a, T(xa, yu + 2), T(xa, yu + 3)) &&
					GSRail.BuildRailDepot(depot_a, T(xa, yu + 2)),
					"Loop A depot should be built.");
				/* Clockwise: north row eastbound, south row westbound. */
				report.check_true("a_signals",
					GSRail.BuildSignal(T(xa + 3, yn), T(xa + 2, yn), GSRail.SIGNALTYPE_PBS_ONEWAY) &&
					GSRail.BuildSignal(T(xa + 3, yss), T(xb, yss), GSRail.SIGNALTYPE_PBS_ONEWAY),
					"Loop A one-way signals should be built.");
				local sid_a1 = GSStation.GetStationID(st1_tile);
				local sid_a2 = GSStation.GetStationID(st2_tile);
				report.check_true("a_station_ids", GSStation.IsValidStation(sid_a1) && GSStation.IsValidStation(sid_a2), "Loop A station ids should be valid.");
				report.check_true("a_column_over_tunnel", GSMetro.HasUndergroundRail(T(xa, yu), depth) && GSRail.IsRailTile(T(xa, yu)),
					"Loop A west column must sit directly on top of the underground line.");

				/* ---------------- trains ---------------- */
				local mt = trains.build_train(depot_b, engine);
				local st = trains.build_train(depot_a, engine);
				report.check_true("trains_built", mt != GSVehicle.VEHICLE_INVALID && st != GSVehicle.VEHICLE_INVALID, "Both trains should be built.");
				if (mt != GSVehicle.VEHICLE_INVALID && st != GSVehicle.VEHICLE_INVALID) {
					local wagons = 0;
					if (trains.attach_wagon(mt, depot_b, wagon_engine).attached) wagons++;
					if (trains.attach_wagon(mt, depot_b, wagon_engine).attached) wagons++;
					if (trains.attach_wagon(st, depot_a, wagon_engine).attached) wagons++;
					report.check_eq("wagons_attached", 3, wagons, "Metro train gets two wagons, surface train one.");

					report.check_true("metro_orders", trains.append_surface_order(mt, stb_tile) && trains.append_underground_order(mt, sid_b_ug, GSOrder.OF_FULL_LOAD_ANY), "Metro orders should be added.");
					report.check_true("surface_orders", trains.append_surface_order(st, st1_tile) && trains.append_surface_order(st, st2_tile), "Surface train orders should be added.");

					local crashed = false;
					local crash_note = "";
					/* Phase 1: park the metro train at the underground station: its
					 * order there is "full load" and there is no cargo, so it waits. */
					GSVehicle.StartStopVehicle(mt);
					local ug_ticks = trains.wait_for_underground_station(mt, sid_b_ug, 20000);
					report.check_true("metro_reached_ug_station", ug_ticks != null, "Metro train must reach the underground station.");
					report.metric("metro_parked_x", GSMap.GetTileX(GSVehicle.GetLocation(mt)));
					report.metric("metro_parked_y", GSMap.GetTileY(GSVehicle.GetLocation(mt)));
					report.metric("column_x", xa);

					/* Phase 2: the surface train laps loop A, driving over the parked metro. */
					local sc = ArrivalCounter(st);
					GSVehicle.StartStopVehicle(st);
					local elapsed = 0;
					while (elapsed < 15000) {
						local c = LayerIsCrashed(mt) ? "metro" : (LayerIsCrashed(st) ? "surface" : null);
						if (c != null) { crashed = true; crash_note = "phase2:" + c; break; }
						sc.poll();
						if (sc.count_at(sid_a1) >= 3 && sc.count_at(sid_a2) >= 3) break;
						GSController.Sleep(2);
						elapsed += 2;
					}
					report.metric("phase2_ticks", elapsed);
					report.metric("phase2_surface_arrivals", sc.arrivals);
					report.check_true("phase2_surface_laps", !crashed && sc.count_at(sid_a1) >= 3 && sc.count_at(sid_a2) >= 3,
						"Surface train must complete laps over the parked metro train without crashing.");

					/* Phase 3: release the metro train, both run together. */
					local mc = ArrivalCounter(mt);
					local s_before = sc.arrivals;
					if (!crashed) {
						report.check_true("metro_still_parked", LayerStoppedStation(mt) == sid_b_ug, "Metro train must still be waiting under the surface loop.");
						local dweller = MetroDweller(mt, sid_b_ug);
						dweller.release();
						report.check_true("metro_released", dweller.releases == 1, "The parked metro train should be released.");
						elapsed = 0;
						while (elapsed < 25000) {
							local c = LayerIsCrashed(mt) ? "metro" : (LayerIsCrashed(st) ? "surface" : null);
							if (c != null) { crashed = true; crash_note = "phase3:" + c; break; }
							sc.poll();
							mc.poll();
							dweller.poll(2, 150);
							if (mc.count_at(sid_b_ug) >= 3 && mc.count_at(sid_b_surf) >= 3 && sc.arrivals - s_before >= 6) break;
							GSController.Sleep(2);
							elapsed += 2;
						}
						report.metric("phase3_ticks", elapsed);
					}
					report.metric("metro_arrivals", mc.arrivals);
					report.metric("surface_arrivals_phase3", sc.arrivals - s_before);
					report.metric("crash", crash_note);

					report.check_true("no_crash", !crashed, "No train may crash: surface and underground trains are on different layers (" + crash_note + ").");
					report.check_true("metro_keeps_arriving", mc.count_at(sid_b_ug) >= 3 && mc.count_at(sid_b_surf) >= 3, "Metro train must keep serving both of its stations.");
					report.check_true("surface_keeps_arriving", sc.arrivals - s_before >= 6, "Surface train must keep lapping while the metro runs underneath.");
				}
			}
			company_mode = null;
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.check_true("final_save_written", GSTest.SaveGame("metro_surface_over_underground_no_collision_final.sav"), "Scenario should save final state for manual inspection.");
		report.finish();
	}
}
