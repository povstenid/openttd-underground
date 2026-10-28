class MetroLoopTwoUndergroundStations extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_loop_two_underground_stations"; }
	function GetShortName()   { return "ML2U"; }
	function GetDescription() { return "GS metro acceptance: closed loop half surface / half underground; the tunnel forks into two parallel tracks, one underground station on each; train 1 serves station A, train 2 serves station B, two rounds each."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-06-10"; }
	function CreateInstance() { return "MetroLoopTwoUndergroundStations"; }
}

RegisterGS(MetroLoopTwoUndergroundStations());
