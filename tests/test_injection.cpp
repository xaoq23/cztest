// How to test a program with several functions and how mock injection works.
//
// The production code below is a tiny "program" with two collaborating
// classes.  Both of them receive their collaborators through the constructor,
// which is the seam the mock framework plugs into.
#include <cztest/cztest.hpp>

#include <string>

// ---------------------------------------------------------------------------
// Production code (in a real project this lives in src/, not in the test)
// ---------------------------------------------------------------------------
struct ILogger {
  virtual ~ILogger() = default;
  virtual void Write(const std::string& message) = 0;
};

struct IConfig {
  virtual ~IConfig() = default;
  virtual int GetInt(const std::string& key, int fallback) const = 0;
};

class PriceCalculator {
 public:
  PriceCalculator(const IConfig& config, ILogger& logger) : config_(config), logger_(logger) {}

  int Total(int unit_price, int quantity) const {
    const int discount = config_.GetInt("discount_percent", 0);
    const int gross = unit_price * quantity;
    const int total = gross - gross * discount / 100;
    logger_.Write("total=" + std::to_string(total));
    return total;
  }

  int ShippingCost(int weight_kg) const {
    const int per_kg = config_.GetInt("shipping_per_kg", 5);
    return weight_kg * per_kg;
  }

 private:
  const IConfig& config_;
  ILogger& logger_;
};

// ---------------------------------------------------------------------------
// Mocks
// ---------------------------------------------------------------------------
class MockLogger : public ILogger, public cztest::MockBase {
 public:
  CZTEST_MOCK_METHOD(void, Write, const std::string&);
};

class MockConfig : public IConfig, public cztest::MockBase {
 public:
  CZTEST_MOCK_CONST_METHOD(int, GetInt, const std::string&, int);
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

// One test per function/branch of the class under test.
TEST(PriceCalculator, TotalAppliesTheConfiguredDiscount) {
  MockConfig config;
  MockLogger logger;

  EXPECT_CALL(config, GetInt("discount_percent", 0)).Times(1).WillOnce(cztest::Return(10));
  EXPECT_CALL(logger, Write(cztest::HasSubstr("total="))).Times(1);

  PriceCalculator calculator(config, logger);
  EXPECT_EQ(calculator.Total(100, 2), 180);
}

TEST(PriceCalculator, TotalUsesTheFallbackWithoutDiscount) {
  MockConfig config;
  MockLogger logger;

  EXPECT_CALL(config, GetInt("discount_percent", 0)).Times(1).WillOnce(cztest::Return(0));
  EXPECT_CALL(logger, Write(cztest::Any())).Times(1);

  PriceCalculator calculator(config, logger);
  EXPECT_EQ(calculator.Total(50, 3), 150);
}

TEST(PriceCalculator, ShippingCostUsesTheConfiguredRate) {
  MockConfig config;
  MockLogger logger;

  EXPECT_CALL(config, GetInt("shipping_per_kg", 5)).Times(1).WillOnce(cztest::Return(7));

  PriceCalculator calculator(config, logger);
  EXPECT_EQ(calculator.ShippingCost(3), 21);
}

// The same dependencies shared by several tests: put the mocks into a fixture
// and inject them once.
class CalculatorFixture : public cztest::Test {
 public:
  void SetUp() override {
    // Every test starts with the same configuration.
    EXPECT_CALL(config, GetInt("discount_percent", 0)).Times(cztest::AnyNumber())
        .WillRepeatedly(cztest::Return(10));
    EXPECT_CALL(logger, Write(cztest::Any())).Times(cztest::AnyNumber());
  }

  MockConfig config;
  MockLogger logger;
  PriceCalculator calculator{config, logger};
};

TEST_F(CalculatorFixture, TotalUsesTheFixtureConfiguration) {
  EXPECT_EQ(calculator.Total(200, 2), 360);
}

TEST_F(CalculatorFixture, ShippingCostFallsBackToTheDefaultRate) {
  EXPECT_CALL(config, GetInt("shipping_per_kg", 5)).Times(1).WillOnce(cztest::Return(5));
  EXPECT_EQ(calculator.ShippingCost(4), 20);
}
