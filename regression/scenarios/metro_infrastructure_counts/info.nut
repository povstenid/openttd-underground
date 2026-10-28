class MetroInfrastructureCounts extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_infrastructure_counts"; }
	function GetShortName()   { return "MINC"; }
	function GetDescription() { return "GS metro regression: underground construction updates company infrastructure counts; merger and bankruptcy transfer or remove underground property."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-18"; }
	function CreateInstance() { return "MetroInfrastructureCounts"; }
}

RegisterGS(MetroInfrastructureCounts());
