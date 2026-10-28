class MetroLoopTwoPortalsDualUnderground extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_loop_two_portals_dual_underground"; }
	function GetShortName()   { return "MLTP"; }
	function GetDescription() { return "GS metro scenario: two portals, two underground stations on different paths, one surface station, signals, and a looping train with one wagon."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-03-19"; }
	function CreateInstance() { return "MetroLoopTwoPortalsDualUnderground"; }
}

RegisterGS(MetroLoopTwoPortalsDualUnderground());
