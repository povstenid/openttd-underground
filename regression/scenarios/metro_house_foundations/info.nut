class MetroHouseFoundations extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_house_foundations"; }
	function GetShortName()   { return "MHFD"; }
	function GetDescription() { return "GS metro regression: house foundations keep the underground out down to their depth, and no house is built whose foundation reaches underground construction."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-21"; }
	function CreateInstance() { return "MetroHouseFoundations"; }
}
RegisterGS(MetroHouseFoundations());
