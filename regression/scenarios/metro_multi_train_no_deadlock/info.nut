class MetroMultiTrainNoDeadlock extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_multi_train_no_deadlock"; }
	function GetShortName()   { return "MMTD"; }
	function GetDescription() { return "GS metro scenario: two trains share one signalled underground line — PBS reservations must clear between trains, no deadlock."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-03-29"; }
	function CreateInstance() { return "MetroMultiTrainNoDeadlock"; }
}

RegisterGS(MetroMultiTrainNoDeadlock());
