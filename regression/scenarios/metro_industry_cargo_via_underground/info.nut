class MetroIndustryCargoViaUnderground extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_industry_cargo_via_underground"; }
	function GetShortName()   { return "MICU"; }
	function GetDescription() { return "GS metro regression: freight industries must produce, be picked up through explicit surface exits, and be delivered through underground stations."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-03-19"; }
	function CreateInstance() { return "MetroIndustryCargoViaUnderground"; }
}

RegisterGS(MetroIndustryCargoViaUnderground());
