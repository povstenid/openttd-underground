class MetroIndustryFoundations extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_industry_foundations"; }
	function GetShortName()   { return "MIFD"; }
	function GetDescription() { return "GS metro regression: industry foundations keep the underground out down to their depth, and no industry is set up whose foundation reaches underground construction."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-28"; }
	function CreateInstance() { return "MetroIndustryFoundations"; }
}
RegisterGS(MetroIndustryFoundations());
