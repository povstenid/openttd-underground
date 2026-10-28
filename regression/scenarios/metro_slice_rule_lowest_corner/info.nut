class MetroSliceRuleLowestCorner extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_slice_rule_lowest_corner"; }
	function GetShortName()   { return "MSLC"; }
	function GetDescription() { return "GS metro regression: an underground level is valid only below the lowest corner of the tile."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-18"; }
	function CreateInstance() { return "MetroSliceRuleLowestCorner"; }
}

RegisterGS(MetroSliceRuleLowestCorner());
