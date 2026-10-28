class MetroBlockSignalsAboveUnderground extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_block_signals_above_underground"; }
	function GetShortName()   { return "MBSU"; }
	function GetDescription() { return "GS metro regression: a surface block-signal section above an underground train must stay free; the surface train must not get stuck."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-18"; }
	function CreateInstance() { return "MetroBlockSignalsAboveUnderground"; }
}

RegisterGS(MetroBlockSignalsAboveUnderground());
