class MetroTrainLaunchUnderground extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_train_launch_underground"; }
	function GetShortName()   { return "MTLU"; }
	function GetDescription() { return "GS smoke test for launching a train through underground metro infrastructure."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-03-19"; }
	function CreateInstance() { return "MetroTrainLaunchUnderground"; }
}

RegisterGS(MetroTrainLaunchUnderground());
