class MetroBlueprintPaste extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_blueprint_paste"; }
	function GetShortName()   { return "MBPP"; }
	function GetDescription() { return "GS metro regression: a line with its surface is copied into a blueprint and pasted as it is and turned and mirrored; trains run on the copies."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-21"; }
	function CreateInstance() { return "MetroBlueprintPaste"; }
}
RegisterGS(MetroBlueprintPaste());
