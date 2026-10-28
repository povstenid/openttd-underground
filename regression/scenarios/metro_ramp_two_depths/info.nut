class MetroRampTwoDepths extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_ramp_two_depths"; }
	function GetShortName()   { return "MRTD"; }
	function GetDescription() { return "GS metro scenario: a train descends a ramp to a deeper level, services a station there and returns to the surface."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-06-09"; }
	function CreateInstance() { return "MetroRampTwoDepths"; }
}

RegisterGS(MetroRampTwoDepths());
