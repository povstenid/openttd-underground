class MetroRampSignalReload extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_ramp_signal_reload"; }
	function GetShortName()   { return "MRSR"; }
	function GetDescription() { return "GS metro regression: signals of every type on ramps between levels survive saving and loading."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-20"; }
	function CreateInstance() { return "MetroRampSignalReload"; }
}
RegisterGS(MetroRampSignalReload());
