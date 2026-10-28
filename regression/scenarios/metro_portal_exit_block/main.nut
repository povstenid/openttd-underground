/**
 * metro_portal_exit_block
 *
 * One surface block without signals in front of a portal, with a depot
 * branching off right before the mouth:
 *
 *   [S S] -- x+4..x+7 -- J(x+8) -- P(x+9) ~~ x+10..x+12 ~~ [U U] |
 *                         |
 *                        depot (x+8, y+2)
 *
 * Train A shuttles S <-> U through the portal. Train B shuttles between the
 * depot and S, crossing the stretch in front of the mouth all the time. A
 * train below ground is not part of the surface block, so a train coming out
 * of the tunnel must wait while B is in the block; before the fix it drove
 * out into B.
 */
class MetroPortalExitBlock extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);
		local report = MetroTest("metro_portal_exit_block", null, 510094);
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();
		local metro = MetroTest.MetroLib(build);
		local trains = MetroTest.MetroTrainLib();
		try {
			local area = build.find_buildable_rectangle(30, 8);
			report.check_true("build_area_found", area != null, "area");
			local company = companies.ensure_company();
			local cm = company == GSCompany.COMPANY_INVALID ? null : GSCompanyMode(company);
			local engine = cm == null ? null : trains.find_rail_engine();
			report.check_true("engine", engine != null, "engine");
			if (area != null && engine != null && company != GSCompany.COMPANY_INVALID) {
				local x = build.tile_x(area);
				local y = build.tile_y(area) + 3;
				local d = -1;
				local T = function(ax, ay) : (build) { return build.make_tile(ax, ay); };
				local station = T(x + 2, y);
				local junction = T(x + 8, y);
				local depot_front = T(x + 8, y + 1);
				local depot = T(x + 8, y + 2);

				report.check_true("station", build.build_surface_station_x(station), "surface station (two tiles)");
				report.check_true("surface_row", build.build_surface_x(x + 4, x + 8, y), "surface row " + GSError.GetLastErrorString());
				report.check_true("surface_curve_west", GSRail.BuildRail(T(x + 7, y), junction, depot_front), "curve west " + GSError.GetLastErrorString());
				report.check_true("surface_curve_east", GSRail.BuildRailTrack(junction, GSRail.RAILTRACK_SW_SE), "curve east " + GSError.GetLastErrorString());
				report.check_true("depot", GSRail.BuildRail(depot, depot_front, junction) && GSRail.BuildRailDepot(depot, depot_front), "depot");
				report.check_true("portal", metro.build_portal(T(x + 9, y), GSMetro.DIAGDIR_SW, d), "portal");
				report.check_true("ug", metro.build_underground_x(x + 10, x + 12, y, d), "underground track");
				local su = null;
				for (local a = x + 13; a <= x + 14; a++) {
					if (!GSMetro.BuildUndergroundStation(T(a, y), GSRail.RAILTRACK_NE_SW, d, su == null ? GSStation.STATION_NEW : su)) report.check_true("platform", false, "platform");
					if (su == null) su = GSMetro.GetUndergroundStationID(T(a, y), d);
				}
				report.check_true("stations_valid", GSStation.IsValidStation(su), "underground station");

				local a_train = trains.build_train(depot, engine);
				local b_train = trains.build_train(depot, engine);
				report.check_true("trains", a_train != GSVehicle.VEHICLE_INVALID && b_train != GSVehicle.VEHICLE_INVALID, "trains");
				local flags = GSOrder.OF_NON_STOP_INTERMEDIATE;
				trains.append_underground_order(a_train, su, flags);
				trains.append_surface_order(a_train, station, flags);
				trains.append_surface_order(b_train, station, flags);
				GSOrder.AppendOrder(b_train, depot, flags);
				GSVehicle.StartStopVehicle(a_train);
				GSController.Sleep(150);
				GSVehicle.StartStopVehicle(b_train);

				local crashed = 0, a_ug_calls = 0, b_calls = 0, a_was = 0, b_was = false;
				for (local t = 0; t < 24000; t += 10) {
					foreach (v in [a_train, b_train]) {
						if (!GSVehicle.IsValidVehicle(v) || GSVehicle.GetState(v) == GSVehicle.VS_CRASHED) crashed = 1;
					}
					if (crashed) break;
					/* An engine without cargo calls only for a moment: count the order advancing past U. */
					local a_order = GSOrder.ResolveOrderPosition(a_train, GSOrder.ORDER_CURRENT);
					if (a_order == 1 && a_was == 0) a_ug_calls++;
					a_was = a_order;
					local b_at = GSVehicle.GetState(b_train) == GSVehicle.VS_AT_STATION;
					if (b_at && !b_was) b_calls++;
					b_was = b_at;
					GSController.Sleep(10);
				}
				report.metric("a_underground_calls", a_ug_calls);
				report.metric("b_station_calls", b_calls);
				report.check_eq("no_crash", 0, crashed, "A train coming out of the portal must not drive into the train in the surface block.");
				report.check_true("a_keeps_running", a_ug_calls >= 3, "Train A keeps shuttling through the portal (calls=" + a_ug_calls + ").");
				report.check_true("b_keeps_running", b_calls >= 3, "Train B keeps running between depot and station (calls=" + b_calls + ").");
				cm = null;
			}
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}
		report.finish();
	}
}
