class MetroRemoveUnderTrainRefused extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_remove_under_train_refused"; }
	function GetShortName()   { return "MRUT"; }
	function GetDescription() { return "Removing underground track a stopped train stands on must be refused until the train has left."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-18"; }
	function CreateInstance() { return "MetroRemoveUnderTrainRefused"; }
}

RegisterGS(MetroRemoveUnderTrainRefused());
