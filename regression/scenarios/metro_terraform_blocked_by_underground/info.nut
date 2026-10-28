class MetroTerraformBlockedByUnderground extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_terraform_blocked_by_underground"; }
	function GetShortName()   { return "MTBU"; }
	function GetDescription() { return "GS metro regression: lowering land must not uncover underground rail."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-18"; }
	function CreateInstance() { return "MetroTerraformBlockedByUnderground"; }
}

RegisterGS(MetroTerraformBlockedByUnderground());
