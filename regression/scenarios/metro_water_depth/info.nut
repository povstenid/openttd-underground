class MetroWaterDepth extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_water_depth"; }
	function GetShortName()   { return "MWDP"; }
	function GetDescription() { return "GS metro regression: rivers and canals keep the underground out down to their depth, nothing is built below the sea, and no water comes over a tunnel it would reach."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-21"; }
	function CreateInstance() { return "MetroWaterDepth"; }
}
RegisterGS(MetroWaterDepth());
