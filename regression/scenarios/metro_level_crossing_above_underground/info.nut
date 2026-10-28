class MetroLevelCrossingAboveUnderground extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_level_crossing_above_underground"; }
	function GetShortName()   { return "MLCU"; }
	function GetDescription() { return "GS metro regression: buses use a level crossing directly above an underground line; underground trains must neither crash buses nor keep the crossing barred."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-18"; }
	function CreateInstance() { return "MetroLevelCrossingAboveUnderground"; }
}

RegisterGS(MetroLevelCrossingAboveUnderground());
