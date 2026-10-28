class MetroLostTrainOtherRail extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_lost_train_other_rail"; }
	function GetShortName()   { return "MLTO"; }
	function GetDescription() { return "GS metro regression: a train without a path does not drive onto underground rail it has no power on."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-21"; }
	function CreateInstance() { return "MetroLostTrainOtherRail"; }
}
RegisterGS(MetroLostTrainOtherRail());
