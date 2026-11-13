#include "Misc/AutomationTest.h"
#include <limits>
#include "Props/ColaPricingLibrary.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FColaPricingRulesTest, "ColossalFizzCo.Shop.PricingRules", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FColaPricingRulesTest::RunTest(const FString& Parameters)
{
	auto Quote = [](double Level, double Temp, bool Expired = false) { return UColaPricingLibrary::QuoteCola(Level, 25, 4, Temp, 4, 20, Expired); };
	TestEqual(TEXT("Fractional portions retain exact linear value"), Quote(0.7, 4).SaleCents, int64(7000));
	TestEqual(TEXT("Temperature discount caps at 20 percent"), Quote(0.7, 100).SaleCents, int64(5600));
	TestEqual(TEXT("Cold deviation is treated symmetrically"), Quote(0.7, -16).SaleCents, int64(5600));
	TestEqual(TEXT("Overfill adds no selling value"), Quote(1.7, 4).SaleCents, int64(10000));
	TestEqual(TEXT("Expired invoice retains original discounted selling price"), Quote(0.7, 4, true).SaleCents, int64(7000));
	TestEqual(TEXT("Expired partial bottle owes one full nominal bottle"), Quote(0.7, 100, true).CompensationCents, int64(10000));
	TestEqual(TEXT("Overfill does not increase expiry penalty"), Quote(1.3, 4, true).CompensationCents, int64(10000));
	TestEqual(TEXT("Content discount is separate from temperature discount"), Quote(.5, 24).ContentDiscountCents, int64(5000));
	TestEqual(TEXT("Sequential half-fill then maximum temperature discount"), Quote(.5, 24).SaleCents, int64(4000));
	TestEqual(TEXT("Temperature reduction applies to the remaining content value"), Quote(.5, 24).TemperatureDiscountCents, int64(1000));
	TestEqual(TEXT("Aggregate bill rounds down below half a dollar"), UColaPricingLibrary::RoundBillToWholeDollar(2449), int64(2400));
	TestEqual(TEXT("Aggregate bill rounds half a dollar up"), UColaPricingLibrary::RoundBillToWholeDollar(2450), int64(2500));
	TestEqual(TEXT("Largest monetary integer rounds down without overflow"), UColaPricingLibrary::RoundBillToWholeDollar(MAX_int64), MAX_int64 - 7);
	TestEqual(TEXT("Negative bill is invalid"), UColaPricingLibrary::RoundBillToWholeDollar(-1), int64(-1));
	TestEqual(TEXT("Nonexpired empty contents have no sale value"), Quote(0, 4).SaleCents, int64(0));
	TestFalse(TEXT("Invalid penalty range rejects quote"), UColaPricingLibrary::QuoteCola(1, 25, 4, 4, 4, 0, false).bValid);
	TestFalse(TEXT("Negative content rejects quote"), Quote(-0.1, 4).bValid);
	TestFalse(TEXT("Nonfinite values reject quote"), Quote(std::numeric_limits<double>::quiet_NaN(), 4).bValid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBottleReuseRulesTest, "ColossalFizzCo.Shop.BottleReuse", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBottleReuseRulesTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Clean empty intact bottle reusable"), UColaPricingLibrary::CanReuseBottle(1, true, 0));
	TestFalse(TEXT("Exactly seventy percent is ineligible"), UColaPricingLibrary::CanReuseBottle(0.70, true, 0));
	TestTrue(TEXT("Strictly above seventy percent eligible"), UColaPricingLibrary::CanReuseBottle(0.701, true, 0));
	TestFalse(TEXT("Dirty bottle ineligible"), UColaPricingLibrary::CanReuseBottle(1, false, 0));
	TestFalse(TEXT("Residual liquid prevents reuse"), UColaPricingLibrary::CanReuseBottle(1, true, 0.00001));
	return true;
}
#endif
