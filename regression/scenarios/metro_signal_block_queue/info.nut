class MetroSignalBlockQueue extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_signal_block_queue"; }
	function GetShortName()   { return "MSBQ"; }
	function GetDescription() { return "GS metro regression: trains queued at signals before an occupied platform wait and never collide."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-20"; }
	function CreateInstance() { return "MetroSignalBlockQueue"; }
}
RegisterGS(MetroSignalBlockQueue());
