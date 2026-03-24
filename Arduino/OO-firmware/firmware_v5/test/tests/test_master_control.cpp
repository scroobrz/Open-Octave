#include <gtest/gtest.h>
#include "Arduino.h"
#include "globals.h"
#include "master_control_testable.h"

namespace {

class MasterControlTest : public ::testing::Test {
protected:
  void SetUp() override {
    isMaster = false;
    moduleChainIndex = 1;
    numModulesInChain = 2;
  }
};

TEST_F(MasterControlTest, PromoteToMaster_SetsMasterAndResetsChainIndex) {
  ASSERT_FALSE(isMaster);
  ASSERT_EQ(moduleChainIndex, 1);

  promoteToMaster();

  ASSERT_TRUE(isMaster);
  ASSERT_EQ(moduleChainIndex, 0);
  ASSERT_EQ(numModulesInChain, 1);
}

} // namespace
