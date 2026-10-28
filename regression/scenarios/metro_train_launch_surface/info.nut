class MetroTrainLaunchSurface extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_train_launch_surface"; }
	function GetShortName()   { return "MTLS"; }
	function GetDescription() { return "GS smoke test for launching a train on a surface metro line."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-03-19"; }
	function CreateInstance() { return "MetroTrainLaunchSurface"; }
}

RegisterGS(MetroTrainLaunchSurface());
