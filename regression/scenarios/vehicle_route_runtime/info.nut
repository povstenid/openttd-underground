class VehicleRouteRuntime extends AIInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "vehicle_route_runtime"; }
	function GetShortName()   { return "VRRT"; }
	function GetDescription() { return "Scenario smoke test for a road vehicle leaving depot and moving on its route."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-03-18"; }
	function CreateInstance() { return "VehicleRouteRuntime"; }
	function UseAsRandomAI()  { return false; }
}

RegisterAI(VehicleRouteRuntime());
