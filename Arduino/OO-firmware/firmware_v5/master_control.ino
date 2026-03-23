/*
===============================
  MASTER MODULE CONTROL LOGIC
===============================
*/

void promoteToMaster(){
    LOGLN("[CHAIN] Upstream lost — promoting to MASTER");
    isMaster = true;
    moduleChainIndex = 0;
    configureNotes();
    numModulesInChain = 1;
    startControllerConnection();
}