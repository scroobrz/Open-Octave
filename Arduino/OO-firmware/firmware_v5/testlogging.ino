/*
===============================
   TEST LOGGING (CSV STREAM)
===============================
Prints one CSV row per event to Serial (no RAM buffering).
*/

void testLogPrintHeader() {
  Serial.println();
  Serial.println(F("CSV_BEGIN"));
  Serial.println(F("run_id,event_id,mode,event_type,key_index,repeat_streak,input_to_audio_ms,step_led_cmd_ms,step_servo_cmd_ms,autoplay_timing_error_ms,success,error_code"));
  Serial.println(F("CSV_DATA"));
}

void testLogStart() {
  testLogEnabled = true;
  testLogRunId++;
  testLogEventId = 0;

  testLogLastManualKey = -1;
  testLogLastManualTime = 0;
  testLogManualRepeatStreak = 0;

  testLogLastAutoKey = -1;
  testLogAutoRepeatStreak = 0;

  LOGLN("\n[TESTLOG] Enabled. Streaming CSV to Serial. Press 'g' again to stop.");
  testLogPrintHeader();
}

void testLogStop() {
  if (!testLogEnabled) return;
  testLogEnabled = false;
  Serial.println(F("CSV_END"));
  LOGF("[TESTLOG] Disabled (run_id=%u, events=%u)\n", testLogRunId, testLogEventId);
}

void testLogLogManualPress(int keyIndex, unsigned long pressDetectedMs, unsigned long audioStartedMs) {
  if (!testLogEnabled) return;

  if (keyIndex == testLogLastManualKey && (pressDetectedMs - testLogLastManualTime) <= 1000) {
    testLogManualRepeatStreak++;
  } else {
    testLogManualRepeatStreak = 1;
  }
  testLogLastManualKey = (int8_t)keyIndex;
  testLogLastManualTime = pressDetectedMs;

  testLogEventId++;

  long latency = (long)(audioStartedMs - pressDetectedMs);

  Serial.print(testLogRunId); Serial.print(",");
  Serial.print(testLogEventId); Serial.print(",");
  Serial.print(getCurrentSequenceModeString()); Serial.print(",");
  Serial.print(F("MANUAL_PRESS")); Serial.print(",");
  Serial.print(keyIndex); Serial.print(",");
  Serial.print(testLogManualRepeatStreak); Serial.print(",");
  Serial.print(latency); Serial.print(",");
  Serial.print(-1); Serial.print(",");
  Serial.print(-1); Serial.print(",");
  Serial.print(-1); Serial.print(",");
  Serial.print(1); Serial.print(",");
  Serial.println(TESTLOG_OK);
}

void testLogLogAutoNote(int keyIndex, long timingErrorMs,
                        unsigned long ledCmdMs, unsigned long servoCmdMs) {
  if (!testLogEnabled) return;

  if (keyIndex == testLogLastAutoKey) testLogAutoRepeatStreak++;
  else testLogAutoRepeatStreak = 1;
  testLogLastAutoKey = (int8_t)keyIndex;

  testLogEventId++;

  Serial.print(testLogRunId); Serial.print(",");
  Serial.print(testLogEventId); Serial.print(",");
  Serial.print(getCurrentSequenceModeString()); Serial.print(",");
  Serial.print(F("AUTO_STEP")); Serial.print(",");   // event_type kept for CSV compatibility
  Serial.print(keyIndex); Serial.print(",");
  Serial.print(testLogAutoRepeatStreak); Serial.print(",");
  Serial.print(-1); Serial.print(",");
  Serial.print((long)ledCmdMs); Serial.print(",");
  Serial.print((long)servoCmdMs); Serial.print(",");
  Serial.print((long)timingErrorMs); Serial.print(",");
  Serial.print(1); Serial.print(",");
  Serial.println(TESTLOG_OK);
}

void testLogLogError(uint8_t errorCode, const __FlashStringHelper* eventType) {
  if (!testLogEnabled) return;

  testLogEventId++;

  Serial.print(testLogRunId); Serial.print(",");
  Serial.print(testLogEventId); Serial.print(",");
  Serial.print(getCurrentSequenceModeString()); Serial.print(",");
  Serial.print(eventType); Serial.print(",");
  Serial.print(-1); Serial.print(",");
  Serial.print(0); Serial.print(",");
  Serial.print(-1); Serial.print(",");
  Serial.print(-1); Serial.print(",");
  Serial.print(-1); Serial.print(",");
  Serial.print(-1); Serial.print(",");
  Serial.print(0); Serial.print(",");
  Serial.println(errorCode);
}
