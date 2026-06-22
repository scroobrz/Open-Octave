#include <gtest/gtest.h>
#include "Arduino.h"
#include "globals.h"
#include "master_control_testable.h"
#include "keys.h"

namespace {

class MasterControlFunctionalTest : public ::testing::Test {
protected:
    void SetUp() override {
        isMaster = false;
        moduleChainIndex = 1;
        numModulesInChain = 2;

        // Initialize keys with expected frequencies with initial module index
        for (int i = 0; i < NUM_KEYS; i++) {
            keys[i].noteFreq = 100 * (i + 1);
        }
    }
};
TEST_F(MasterControlFunctionalTest, PromoteToMaster_SetsMasterAndResetsChainIndex) {
 ASSERT_FALSE(isMaster);
 ASSERT_EQ(moduleChainIndex, 1);

 promoteToMaster();

 ASSERT_TRUE(isMaster);
 ASSERT_EQ(moduleChainIndex, 0);
 ASSERT_EQ(numModulesInChain, 1);
}


TEST_F(MasterControlFunctionalTest, PromoteToMaster_SetsStateCorrectly) {
    ASSERT_FALSE(isMaster);
    ASSERT_EQ(moduleChainIndex, 1);
    ASSERT_EQ(numModulesInChain, 2);

    promoteToMaster();

    EXPECT_TRUE(isMaster);
    EXPECT_EQ(moduleChainIndex, 0);
    EXPECT_EQ(numModulesInChain, 1);
}

TEST_F(MasterControlFunctionalTest, PromoteToMaster_VerifiesChainState) {
    promoteToMaster();

    // Verify master-only state is set correctly
    EXPECT_EQ(isMaster, true);
    EXPECT_EQ(moduleChainIndex, 0);
    EXPECT_EQ(numModulesInChain, 1);
}

TEST_F(MasterControlFunctionalTest, PromoteToMaster_MultiplePromotions) {
    // First promotion
    promoteToMaster();
    EXPECT_TRUE(isMaster);
    EXPECT_EQ(moduleChainIndex, 0);

    // Second promotion (should not change state significantly)
    moduleChainIndex = 2;  // Simulate different state
    promoteToMaster();
    EXPECT_TRUE(isMaster);
    EXPECT_EQ(moduleChainIndex, 0);  // Should reset to 0 again
}

TEST_F(MasterControlFunctionalTest, PromoteToMaster_PrepareInitialChain) {
    // Test that promotion prepares the module chain correctly
    ASSERT_NE(numModulesInChain, 1);

    promoteToMaster();

    EXPECT_EQ(numModulesInChain, 1);  // Master starts with just itself
}

TEST_F(MasterControlFunctionalTest, PromoteToMaster_NotesReconfigured) {
    // Set different module index to verify reconfiguration
    moduleChainIndex = 3;
    keys[0].noteFreq = 262 << 3;  // Should be at 3rd octave

    promoteToMaster();

    // Should reconfigure to base octave (shift = 0)
    EXPECT_EQ(keys[0].noteFreq, 262 << 0);
}

} // namespace

