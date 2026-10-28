class MetroPoweredRailOnly extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_powered_rail_only"; }
	function GetShortName()   { return "MPRO"; }
	function GetDescription() { return "GS metro regression: underground trains are routed only over rail they have power on."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-20"; }
	function CreateInstance() { return "MetroPoweredRailOnly"; }
}

RegisterGS(MetroPoweredRailOnly());
