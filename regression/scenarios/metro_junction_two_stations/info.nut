class MetroJunctionTwoStations extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_junction_two_stations"; }
	function GetShortName()   { return "MJTS"; }
	function GetDescription() { return "GS metro acceptance: underground junction splits the tunnel into two branches with one station each; train 1 serves station A, train 2 serves station B."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-06-10"; }
	function CreateInstance() { return "MetroJunctionTwoStations"; }
}

RegisterGS(MetroJunctionTwoStations());
