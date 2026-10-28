class MetroBuildOverRunningUnderground extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_build_over_running_underground"; }
	function GetShortName()   { return "MBOU"; }
	function GetDescription() { return "GS metro regression: surface construction on a tile above a stopped underground train must not be blocked by that train."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-18"; }
	function CreateInstance() { return "MetroBuildOverRunningUnderground"; }
}

RegisterGS(MetroBuildOverRunningUnderground());
