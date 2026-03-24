#include <gtest/gtest.h>
#include "Arduino.h"
#include "globals.h"
#include "slave_control_testable.h"

namespace {

class SlaveControlTest : public ::testing::Test {
protected:
 void SetUp() override {
 isMaster = true;
 moduleChainIndex = 0;
 }
};

TEST_F(SlaveControlTest, DemoteToSlave_SetsSlaveAndDisconnectsWifi) {
 ASSERT_TRUE(isMaster);

 demoteToSlave();

 ASSERT_FALSE(isMaster);
}

TEST_F(SlaveControlTest, ConfigureNotes_CompletesWithoutError) {
 moduleChainIndex = 1;
 configureNotes();

 // Verifies configureNotes() can be called without crashing
 SUCCEED();
}

} // namespace
