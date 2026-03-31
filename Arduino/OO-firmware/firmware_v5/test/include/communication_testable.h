#ifndef COMMUNICATION_TESTABLE_H
#define COMMUNICATION_TESTABLE_H
#include <cstddef>
#include <cstdint>

void handleChainCommunication();
void handleControllerCommunication();
void sendHeartbeat();
void checkHeartbeat();
void checkHeartbeatReply();
void handleSerialFromUpstream();
void handleCommandsFromUpstream();
void handleHeartbeatFromUpstream(uint8_t num);
void handleSerialFromDownstream();
void handleCommandsFromDownstream();
void handleHeartbeatFromDownstream(uint8_t num);
void handleUsbSerialCommands();
void handleWebSocketCommand(char* cmd, size_t length);
void processSingleCharCommand(char cmd);
void handleSequenceCommand(char* cmd);
bool processSequenceUploadCommand(char* cmd);
bool processSequenceStepCommand(uint8_t stepIndex, char* cmd);
bool processSequenceEndCommand(char* cmd);
void chainSendKeyCmd(char cmd, int key);
void chainSendKeyCmdWithColor(char cmd, int key, uint32_t color);

#endif // COMMUNICATION_TESTABLE_H
