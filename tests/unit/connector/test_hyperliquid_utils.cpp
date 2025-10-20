/**
 * @file test_hyperliquid_utils.cpp
 * @brief Unit tests for Hyperliquid utilities (Phase 4)
 */

#include <gtest/gtest.h>
#include "connector/hyperliquid_web_utils.h"
#include "connector/hyperliquid_auth.h"

using namespace latentspeed::connector::hyperliquid;

// ============================================================================
// TESTS: HYPERLIQUID WEB UTILS
// ============================================================================

TEST(HyperliquidWebUtils, FloatToWire) {
    // Test basic conversion
    EXPECT_EQ(HyperliquidWebUtils::float_to_wire(0.12345, 3), "0.123");
    EXPECT_EQ(HyperliquidWebUtils::float_to_wire(0.12355, 3), "0.124");  // Rounding
    
    // Test with 5 decimals (BTC)
    EXPECT_EQ(HyperliquidWebUtils::float_to_wire(0.123456, 5), "0.12346");
    
    // Test with 4 decimals (ETH)
    EXPECT_EQ(HyperliquidWebUtils::float_to_wire(1.23456, 4), "1.2346");
    
    // Test trailing zero removal
    EXPECT_EQ(HyperliquidWebUtils::float_to_wire(1.0, 3), "1.0");
    EXPECT_EQ(HyperliquidWebUtils::float_to_wire(1.5, 3), "1.5");
    
    // Test whole numbers
    EXPECT_EQ(HyperliquidWebUtils::float_to_wire(50000.0, 2), "50000.0");
}

TEST(HyperliquidWebUtils, FloatToIntWire) {
    EXPECT_EQ(HyperliquidWebUtils::float_to_int_wire(0.123, 3), 123);
    EXPECT_EQ(HyperliquidWebUtils::float_to_int_wire(1.5, 2), 150);
    EXPECT_EQ(HyperliquidWebUtils::float_to_int_wire(50000.0, 0), 50000);
    EXPECT_EQ(HyperliquidWebUtils::float_to_int_wire(0.00001, 5), 1);
}

TEST(HyperliquidWebUtils, WireToFloat) {
    EXPECT_DOUBLE_EQ(HyperliquidWebUtils::wire_to_float("0.123"), 0.123);
    EXPECT_DOUBLE_EQ(HyperliquidWebUtils::wire_to_float("50000.0"), 50000.0);
    EXPECT_DOUBLE_EQ(HyperliquidWebUtils::wire_to_float("1.5"), 1.5);
}

TEST(HyperliquidWebUtils, RoundToDecimals) {
    EXPECT_DOUBLE_EQ(
        HyperliquidWebUtils::round_to_decimals(0.12345, 3), 
        0.123
    );
    EXPECT_DOUBLE_EQ(
        HyperliquidWebUtils::round_to_decimals(0.12355, 3), 
        0.124
    );
    EXPECT_DOUBLE_EQ(
        HyperliquidWebUtils::round_to_decimals(1.23456, 4), 
        1.2346
    );
}

TEST(HyperliquidWebUtils, GetDefaultSizeDecimals) {
    EXPECT_EQ(HyperliquidWebUtils::get_default_size_decimals("BTC"), 5);
    EXPECT_EQ(HyperliquidWebUtils::get_default_size_decimals("BTCUSD"), 5);
    EXPECT_EQ(HyperliquidWebUtils::get_default_size_decimals("BTC-USD"), 5);
    
    EXPECT_EQ(HyperliquidWebUtils::get_default_size_decimals("ETH"), 4);
    EXPECT_EQ(HyperliquidWebUtils::get_default_size_decimals("ETHUSD"), 4);
    EXPECT_EQ(HyperliquidWebUtils::get_default_size_decimals("ETH-USD"), 4);
    
    // Default for others
    EXPECT_EQ(HyperliquidWebUtils::get_default_size_decimals("SOL"), 3);
    EXPECT_EQ(HyperliquidWebUtils::get_default_size_decimals("DOGE"), 3);
}

TEST(HyperliquidWebUtils, FormatPrice) {
    // Default formatting (2-8 decimals)
    EXPECT_EQ(HyperliquidWebUtils::format_price(50000.123456, 2, 8), "50000.123456");
    EXPECT_EQ(HyperliquidWebUtils::format_price(50000.1, 2, 8), "50000.10");
    EXPECT_EQ(HyperliquidWebUtils::format_price(50000.0, 2, 8), "50000.00");
    
    // Custom formatting
    EXPECT_EQ(HyperliquidWebUtils::format_price(1.23456789, 4, 6), "1.234568");
}

TEST(HyperliquidWebUtils, ValidateSize) {
    // Valid sizes
    EXPECT_TRUE(HyperliquidWebUtils::validate_size(0.123, 0.001, 3));
    EXPECT_TRUE(HyperliquidWebUtils::validate_size(1.0, 0.001, 3));
    
    // Too small
    EXPECT_FALSE(HyperliquidWebUtils::validate_size(0.0001, 0.001, 3));
    
    // Invalid precision (0.1234 with 3 decimals)
    EXPECT_FALSE(HyperliquidWebUtils::validate_size(0.1234, 0.001, 3));
}

TEST(HyperliquidWebUtils, NotionalToSize) {
    // $5000 at $50000/BTC = 0.1 BTC
    EXPECT_DOUBLE_EQ(
        HyperliquidWebUtils::notional_to_size(5000.0, 50000.0, 5),
        0.1
    );
    
    // $1000 at $2000/ETH = 0.5 ETH
    EXPECT_DOUBLE_EQ(
        HyperliquidWebUtils::notional_to_size(1000.0, 2000.0, 4),
        0.5
    );
    
    // With rounding
    double size = HyperliquidWebUtils::notional_to_size(1234.56, 50000.0, 5);
    EXPECT_NEAR(size, 0.02469, 0.00001);
}

TEST(HyperliquidWebUtils, ErrorHandling) {
    // NaN
    EXPECT_THROW(
        HyperliquidWebUtils::float_to_wire(std::nan(""), 3),
        std::invalid_argument
    );
    
    // Infinity
    EXPECT_THROW(
        HyperliquidWebUtils::float_to_wire(
            std::numeric_limits<double>::infinity(), 3
        ),
        std::invalid_argument
    );
    
    // Invalid wire string
    EXPECT_THROW(
        HyperliquidWebUtils::wire_to_float("invalid"),
        std::invalid_argument
    );
    
    // Invalid price for notional_to_size
    EXPECT_THROW(
        HyperliquidWebUtils::notional_to_size(1000.0, 0.0, 3),
        std::invalid_argument
    );
}

// ============================================================================
// TESTS: HYPERLIQUID AUTH (Structure only - crypto is placeholder)
// ============================================================================

TEST(HyperliquidAuth, Construction) {
    // Valid construction
    EXPECT_NO_THROW({
        HyperliquidAuth auth(
            "0x1234567890123456789012345678901234567890",
            "privatekey_hex",
            false
        );
    });
    
    // Invalid address (no 0x prefix)
    EXPECT_THROW({
        HyperliquidAuth auth(
            "1234567890123456789012345678901234567890",
            "privatekey_hex",
            false
        );
    }, HyperliquidAuthException);
    
    // Invalid address (wrong length)
    EXPECT_THROW({
        HyperliquidAuth auth(
            "0x123456",
            "privatekey_hex",
            false
        );
    }, HyperliquidAuthException);
}

TEST(HyperliquidAuth, GetAddress) {
    HyperliquidAuth auth(
        "0x1234567890123456789012345678901234567890",
        "privatekey_hex",
        false
    );
    
    EXPECT_EQ(auth.get_address(), "0x1234567890123456789012345678901234567890");
    EXPECT_FALSE(auth.is_vault());
}

TEST(HyperliquidAuth, VaultMode) {
    HyperliquidAuth auth(
        "0x1234567890123456789012345678901234567890",
        "privatekey_hex",
        true  // Vault mode
    );
    
    EXPECT_TRUE(auth.is_vault());
}

TEST(HyperliquidAuth, SignL1ActionStructure) {
    HyperliquidAuth auth(
        "0x1234567890123456789012345678901234567890",
        "privatekey_hex",
        false
    );
    
    // Create a sample order action
    nlohmann::json action = {
        {"type", "order"},
        {"orders", nlohmann::json::array({
            {
                {"a", 0},
                {"b", true},
                {"p", "50000"},
                {"s", "0.01"},
                {"r", false},
                {"t", {{"limit", {{"tif", "Gtc"}}}}}
            }
        })},
        {"grouping", "na"}
    };
    
    // Sign the action (will use placeholder crypto)
    auto signed_action = auth.sign_l1_action(action, 12345, true);
    
    // Check structure
    EXPECT_TRUE(signed_action.contains("action"));
    EXPECT_TRUE(signed_action.contains("nonce"));
    EXPECT_TRUE(signed_action.contains("signature"));
    
    EXPECT_EQ(signed_action["nonce"], 12345);
    EXPECT_EQ(signed_action["action"], action);
    
    // Check signature structure (placeholder)
    EXPECT_TRUE(signed_action["signature"].contains("r"));
    EXPECT_TRUE(signed_action["signature"].contains("s"));
    EXPECT_TRUE(signed_action["signature"].contains("v"));
}

TEST(HyperliquidAuth, SignCancelActionStructure) {
    HyperliquidAuth auth(
        "0x1234567890123456789012345678901234567890",
        "privatekey_hex",
        false
    );
    
    nlohmann::json cancel_action = {
        {"type", "cancel"},
        {"cancels", nlohmann::json::array({
            {{"a", 0}, {"o", 123456}}
        })}
    };
    
    auto signed_cancel = auth.sign_cancel_action(cancel_action, 12346, true);
    
    EXPECT_TRUE(signed_cancel.contains("action"));
    EXPECT_TRUE(signed_cancel.contains("nonce"));
    EXPECT_TRUE(signed_cancel.contains("signature"));
}

// ============================================================================
// MAIN
// ============================================================================

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
