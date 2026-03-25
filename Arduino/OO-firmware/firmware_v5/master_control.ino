/*
===============================
  MASTER MODULE CONTROL LOGIC
===============================
*/

void promoteToMaster(){
    LOGLN("[CHAIN] Upstream lost — promoting to MASTER");
    if (recording) stopRecording();
    stopSequence();
    isMaster = true;
    moduleChainIndex = 0;
    configureNotes();
    numModulesInChain = 1;
    updateDefaultSequenceForChainSize();
    startControllerConnection();
}