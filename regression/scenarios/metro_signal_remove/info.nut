class MetroSignalRemove extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_signal_remove"; }
	function GetShortName()   { return "MSRM"; }
	function GetDescription() { return "GS metro regression: signals are removed from underground track and from a ramp between levels, the track and the ramp stay and trains keep running."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-21"; }
	function CreateInstance() { return "MetroSignalRemove"; }
}
RegisterGS(MetroSignalRemove());
