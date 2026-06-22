#ifndef MASTER_CONTROL_TESTABLE_H
#define MASTER_CONTROL_TESTABLE_H

void promoteToMaster();
void demoteToSlave();
void sendKeyPressToSlaves(int globalKey);
void handleIncomingFromSlaves();

#endif // MASTER_CONTROL_TESTABLE_H
