/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 Copyright (C) 2010, 2011 Klaus Spanderen

 This file is part of QuantLib, a free-software/open-source library
 for financial quantitative analysts and developers - http://quantlib.org/

 QuantLib is free software: you can redistribute it and/or modify it
 under the terms of the QuantLib license.  You should have received a
 copy of the license along with this program; if not, please email
 <quantlib-dev@lists.sf.net>. The license is also available online at
 <http://quantlib.org/license.shtml>.

 This program is distributed in the hope that it will be useful, but WITHOUT
 ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 FOR A PARTICULAR PURPOSE.  See the license for more details.
*/

#include "utilities.hpp"
#include <array>
#include <ql/math/functional.hpp>
#include <ql/quotes/simplequote.hpp>
#include <ql/time/daycounters/actualactual.hpp>
#include <ql/instruments/vanillaoption.hpp>
#include <ql/instruments/vanillaswingoption.hpp>
#include <ql/math/randomnumbers/rngtraits.hpp>
#include <ql/math/statistics/generalstatistics.hpp>
#include <ql/termstructures/yield/zerocurve.hpp>
#include <ql/processes/ornsteinuhlenbeckprocess.hpp>
#include <ql/processes/blackscholesprocess.hpp>
#include <ql/methods/montecarlo/multipathgenerator.hpp>
#include <ql/pricingengines/vanilla/analyticeuropeanengine.hpp>
#include <ql/experimental/processes/extouwithjumpsprocess.hpp>
#include <ql/experimental/processes/extendedornsteinuhlenbeckprocess.hpp>
#include <ql/pricingengines/vanilla/fdsimplebsswingengine.hpp>
#include <ql/experimental/finitedifferences/fdextoujumpvanillaengine.hpp>
#include <ql/experimental/finitedifferences/fdsimpleextoujumpswingengine.hpp>
#include <ql/methods/finitedifferences/meshers/exponentialjump1dmesher.hpp>
#include <ql/pricingengines/vanilla/fdblackscholesvanillaengine.hpp>


using namespace QuantLib;


namespace {
    std::shared_ptr<ExtOUWithJumpsProcess> createKlugeProcess() {
        Array x0(2);
        x0[0] = 3.0;
        x0[1] = 0.0;

        const Real beta = 5.0;
        const Real eta = 2.0;
        const Real jumpIntensity = 1.0;
        const Real speed = 1.0;
        const Real volatility = 2.0;

        std::shared_ptr < ExtendedOrnsteinUhlenbeckProcess > ouProcess =
                std::make_shared<ExtendedOrnsteinUhlenbeckProcess>(speed, volatility, x0[0],
                                                                   constant(x0[0]));
        return std::make_shared<ExtOUWithJumpsProcess>(ouProcess, x0[1], beta,
                                                       jumpIntensity, eta);
    }
}

TEST_CASE("SwingOption_ExtendedOrnsteinUhlenbeckProcess", "[SwingOption]") {

    INFO("Testing extended Ornstein-Uhlenbeck process...");

    SavedSettings backup;

    const Real speed = 2.5;
    const Volatility vol = 0.70;
    const Real level = 1.43;

    ExtendedOrnsteinUhlenbeckProcess::Discretization discr[] = {
            ExtendedOrnsteinUhlenbeckProcess::MidPoint,
            ExtendedOrnsteinUhlenbeckProcess::Trapezodial,
            ExtendedOrnsteinUhlenbeckProcess::GaussLobatto};

    std::array<std::function<Real(Real)>, 3> f{constant(level),
                                               [](Real x) { return 1.0 + x; },
                                               [](Real x) { return std::sin(x); }};

    for (Size n = 0; n < LENGTH(f); ++n) {
        ExtendedOrnsteinUhlenbeckProcess refProcess(
                speed, vol, 0.0, f[n],
                ExtendedOrnsteinUhlenbeckProcess::GaussLobatto, 1e-6);

        for (Size i = 0; i < LENGTH(discr) - 1; ++i) {
            ExtendedOrnsteinUhlenbeckProcess eouProcess(
                    speed, vol, 0.0, f[n], discr[i]);

            const Time T = 10;
            const Size nTimeSteps = 10000;

            const Time dt = T / nTimeSteps;
            Time t = 0.0;
            Real q = 0.0;
            Real p = 0.0;

            PseudoRandom::rng_type rng(PseudoRandom::urng_type(1234u));

            for (Size j = 0; j < nTimeSteps; ++j) {
                const Real dw = rng.next().value;
                q = eouProcess.evolve(t, q, dt, dw);
                p = refProcess.evolve(t, p, dt, dw);

                if (std::fabs(q - p) > 1e-6) {
                    FAIL("invalid process evaluation "
                                 << n << " " << i << " " << j << " " << q - p);
                }
                t += dt;
            }
        }
    }
}


TEST_CASE("SwingOption_FdmExponentialJump1dMesher", "[SwingOption]") {

    INFO("Testing finite difference mesher for the Kluge model...");

    SavedSettings backup;

    Array x(2, 1.0);
    const Real beta = 100.0;
    const Real eta = 1.0 / 0.4;
    const Real jumpIntensity = 4.0;
    const Size dummySteps = 2;

    ExponentialJump1dMesher mesher(dummySteps, beta, jumpIntensity, eta);

    std::shared_ptr < ExtendedOrnsteinUhlenbeckProcess > ouProcess =
            std::make_shared<ExtendedOrnsteinUhlenbeckProcess>(1.0, 1.0, x[0],
                                                               constant(1.0));
    std::shared_ptr < ExtOUWithJumpsProcess > jumpProcess =
            std::make_shared<ExtOUWithJumpsProcess>(ouProcess, x[1], beta, jumpIntensity, eta);

    const Time dt = 1.0 / (10.0 * beta);
    const Size n = 1000000;

    std::vector<Real> path(n);
    PseudoRandom::rng_type mt(PseudoRandom::urng_type(123));
    Array dw(3);
    for (Size i = 0; i < n; ++i) {
        dw[0] = mt.next().value;
        dw[1] = mt.next().value;
        dw[2] = mt.next().value;
        path[i] = (x = jumpProcess->evolve(0.0, x, dt, dw))[1];
    }
    std::sort(path.begin(), path.end());

    const Real relTol1 = 2e-3;
    const Real relTol2 = 2e-2;
    const Real threshold = 0.9;

    for (Real s = 1e-12; s < 1.0; s *= 10) {
        const Real v = mesher.jumpSizeDistribution(s);

        std::vector<Real>::iterator iter
                = std::lower_bound(path.begin(), path.end(), s);
        const Real q = std::distance(path.begin(), iter) / Real(n);
        QL_REQUIRE(std::fabs(q - v) < relTol1
                   || ((v < threshold) && std::fabs(q - v) < relTol2),
                   "can not reproduce jump distribution");
    }
}

TEST_CASE("SwingOption_ExtOUJumpVanillaEngine", "[SwingOption]") {

    INFO("Testing finite difference pricer for the Kluge model...");

    SavedSettings backup;

    std::shared_ptr < ExtOUWithJumpsProcess > jumpProcess = createKlugeProcess();

    const Date today = Date::todaysDate();
    Settings::instance().evaluationDate() = today;

    const DayCounter dc = ActualActual();
    const Date maturityDate = today + Period(12, Months);
    const Time maturity = dc.yearFraction(today, maturityDate);

    const Rate irRate = 0.1;
    std::shared_ptr < YieldTermStructure > rTS(flatRate(today, irRate, dc));
    std::shared_ptr < StrikedTypePayoff > payoff =
            std::make_shared<PlainVanillaPayoff>(Option::Call, 30);
    std::shared_ptr < Exercise > exercise = std::make_shared<EuropeanExercise>(maturityDate);

    std::shared_ptr < PricingEngine > engine =
            std::make_shared<FdExtOUJumpVanillaEngine>(jumpProcess, rTS, 25, 200, 50);

    VanillaOption option(payoff, exercise);
    option.setPricingEngine(engine);
    const Real fdNPV = option.NPV();

    const Size steps = 100;
    const Size nrTrails = 200000;
    TimeGrid grid(maturity, steps);

    typedef PseudoRandom::rsg_type rsg_type;
    typedef MultiPathGenerator<rsg_type>::sample_type sample_type;
    rsg_type rsg = PseudoRandom::make_sequence_generator(
            jumpProcess->factors() * (grid.size() - 1), BigNatural(421));

    GeneralStatistics npv;
    MultiPathGenerator<rsg_type> generator(jumpProcess, grid, rsg, false);

    for (Size n = 0; n < nrTrails; ++n) {
        sample_type path = generator.next();

        const Real x = path.value[0].back();
        const Real y = path.value[1].back();

        const Real cashflow = (*payoff)(std::exp(x + y));
        npv.add(cashflow * rTS->discount(maturity));
    }

    const Real mcNPV = npv.mean();
    const Real mcError = npv.errorEstimate();

    if (std::fabs(fdNPV - mcNPV) > 3.0 * mcError) {
        FAIL_CHECK("Failed to reproduce FD and MC prices"
                           << "\n    FD NPV: " << fdNPV
                           << "\n    MC NPV: " << mcNPV
                           << " +/- " << mcError);
    }
}

namespace {
    class VanillaForwardPayoff : public StrikedTypePayoff {
    public:
        VanillaForwardPayoff(Option::Type type, Real strike)
                : StrikedTypePayoff(type, strike) {}

        std::string name() const { return "ForwardTypePayoff"; }

        Real operator()(Real price) const {
            switch (type_) {
                case Option::Call:
                    return price - strike_;
                case Option::Put:
                    return strike_ - price;
                default:
                    QL_FAIL("unknown/illegal option type");
            }
        }
    };
}

TEST_CASE("SwingOption_FdBSSwingOption", "[SwingOption]") {

    INFO("Testing Black-Scholes vanilla swing option pricing...");

    SavedSettings backup;

    Date settlementDate = Date::todaysDate();
    Settings::instance().evaluationDate() = settlementDate;
    DayCounter dayCounter = ActualActual();
    Date maturityDate = settlementDate + Period(12, Months);

    Real strike = 30;
    std::shared_ptr < StrikedTypePayoff > payoff =
            std::make_shared<PlainVanillaPayoff>(Option::Put, strike);
    std::shared_ptr < StrikedTypePayoff > forward =
            std::make_shared<VanillaForwardPayoff>(Option::Put, strike);

    std::vector<Date> exerciseDates(1, settlementDate + Period(1, Months));
    while (exerciseDates.back() < maturityDate) {
        exerciseDates.emplace_back(exerciseDates.back() + Period(1, Months));
    }

    std::shared_ptr < SwingExercise > swingExercise =
            std::make_shared<SwingExercise>(exerciseDates);

    Handle<YieldTermStructure> riskFreeTS(flatRate(0.14, dayCounter));
    Handle<YieldTermStructure> dividendTS(flatRate(0.02, dayCounter));
    Handle<BlackVolTermStructure> volTS(
            flatVol(settlementDate, 0.4, dayCounter));

    Handle<Quote> s0(std::make_shared<SimpleQuote>(30.0));

    std::shared_ptr < BlackScholesMertonProcess > process =
            std::make_shared<BlackScholesMertonProcess>(s0, dividendTS, riskFreeTS, volTS);
    std::shared_ptr < PricingEngine > engine =
            std::make_shared<FdSimpleBSSwingEngine>(process, 50, 200);

    VanillaOption bermudanOption(payoff, swingExercise);
    bermudanOption.setPricingEngine(std::make_shared<FdBlackScholesVanillaEngine>(process, 50, 200));
    const Real bermudanOptionPrices = bermudanOption.NPV();

    for (Size i = 0; i < exerciseDates.size(); ++i) {
        const Size exerciseRights = i + 1;

        VanillaSwingOption swingOption(forward, swingExercise,
                                       0, exerciseRights);
        swingOption.setPricingEngine(engine);
        const Real swingOptionPrice = swingOption.NPV();

        const Real upperBound = exerciseRights * bermudanOptionPrices;

        if (swingOptionPrice - upperBound > 2e-2) {
            FAIL_CHECK("Failed to reproduce upper bounds"
                               << "\n    upper Bound: " << upperBound
                               << "\n    Price:       " << swingOptionPrice);
        }

        Real lowerBound = 0.0;
        for (Size j = exerciseDates.size() - i - 1; j < exerciseDates.size(); ++j) {
            VanillaOption europeanOption(payoff, std::make_shared<EuropeanExercise>(exerciseDates[j]));
            europeanOption.setPricingEngine(
                    std::make_shared<AnalyticEuropeanEngine>(process));
            lowerBound += europeanOption.NPV();
        }

        if (lowerBound - swingOptionPrice > 2e-2) {
            FAIL_CHECK("Failed to reproduce lower bounds"
                               << "\n    lower Bound: " << lowerBound
                               << "\n    Price:       " << swingOptionPrice);
        }
    }
}


TEST_CASE("SwingOption_ExtOUJumpSwingOption", "[SwingOption]") {

    INFO("Testing simple swing option pricing for Kluge model...");

    SavedSettings backup;

    Date settlementDate = Date::todaysDate();
    Settings::instance().evaluationDate() = settlementDate;
    DayCounter dayCounter = ActualActual();
    Date maturityDate = settlementDate + Period(12, Months);

    Real strike = 30;
    std::shared_ptr < StrikedTypePayoff > payoff =
            std::make_shared<PlainVanillaPayoff>(Option::Put, strike);
    std::shared_ptr < StrikedTypePayoff > forward =
            std::make_shared<VanillaForwardPayoff>(Option::Put, strike);

    std::vector<Date> exerciseDates(1, settlementDate + Period(1, Months));
    while (exerciseDates.back() < maturityDate) {
        exerciseDates.emplace_back(exerciseDates.back() + Period(1, Months));
    }
    std::shared_ptr < SwingExercise > swingExercise =
            std::make_shared<SwingExercise>(exerciseDates);

    std::vector<Time> exerciseTimes(exerciseDates.size());
    for (Size i = 0; i < exerciseTimes.size(); ++i) {
        exerciseTimes[i]
                = dayCounter.yearFraction(settlementDate, exerciseDates[i]);
    }

    TimeGrid grid(exerciseTimes.begin(), exerciseTimes.end(), 60);
    std::vector<Size> exerciseIndex(exerciseDates.size());
    for (Size i = 0; i < exerciseIndex.size(); ++i) {
        exerciseIndex[i] = grid.closestIndex(exerciseTimes[i]);
    }

    std::shared_ptr < ExtOUWithJumpsProcess > jumpProcess = createKlugeProcess();

    const Rate irRate = 0.1;
    std::shared_ptr < YieldTermStructure > rTS(
            flatRate(settlementDate, irRate, dayCounter));

    std::shared_ptr < PricingEngine > swingEngine =
            std::make_shared<FdSimpleExtOUJumpSwingEngine>(jumpProcess, rTS, 25, 50, 25);

    std::shared_ptr < PricingEngine > vanillaEngine =
            std::make_shared<FdExtOUJumpVanillaEngine>(jumpProcess, rTS, 25, 50, 25);

    VanillaOption bermudanOption(payoff, swingExercise);
    bermudanOption.setPricingEngine(vanillaEngine);
    const Real bermudanOptionPrices = bermudanOption.NPV();

    const Size nrTrails = 16000;
    typedef PseudoRandom::rsg_type rsg_type;
    typedef MultiPathGenerator<rsg_type>::sample_type sample_type;
    rsg_type rsg = PseudoRandom::make_sequence_generator(
            jumpProcess->factors() * (grid.size() - 1), BigNatural(421));

    MultiPathGenerator<rsg_type> generator(jumpProcess, grid, rsg, false);

    for (Size i = 0; i < exerciseDates.size(); ++i) {
        const Size exerciseRights = i + 1;

        VanillaSwingOption swingOption(forward, swingExercise,
                                       0, exerciseRights);
        swingOption.setPricingEngine(swingEngine);
        const Real swingOptionPrice = swingOption.NPV();

        const Real upperBound = exerciseRights * bermudanOptionPrices;

        if (swingOptionPrice - upperBound > 2e-2) {
            FAIL_CHECK("Failed to reproduce upper bounds"
                               << "\n    upper Bound: " << upperBound
                               << "\n    Price:       " << swingOptionPrice);
        }

        Real lowerBound = 0.0;
        for (Size j = exerciseDates.size() - i - 1; j < exerciseDates.size(); ++j) {
            VanillaOption europeanOption(payoff, std::make_shared<EuropeanExercise>(exerciseDates[j]));
            europeanOption.setPricingEngine(
                    std::shared_ptr < PricingEngine > (vanillaEngine));
            lowerBound += europeanOption.NPV();
        }

        if (lowerBound - swingOptionPrice > 2e-2) {
            FAIL_CHECK("Failed to reproduce lower bounds"
                               << "\n    lower Bound: " << lowerBound
                               << "\n    Price:       " << swingOptionPrice);
        }

        // use MC plus perfect forecast to find an upper bound
        GeneralStatistics npv;
        for (Size n = 0; n < nrTrails; ++n) {
            sample_type path = generator.next();

            std::vector<Real> exerciseValues(exerciseTimes.size());
            for (Size k = 0; k < exerciseTimes.size(); ++k) {
                const Real x = path.value[0][exerciseIndex[k]];
                const Real y = path.value[1][exerciseIndex[k]];
                const Real s = std::exp(x + y);

                exerciseValues[k] = (*payoff)(s) * rTS->discount(exerciseDates[k]);
            }
            std::sort(exerciseValues.begin(), exerciseValues.end(),
                      std::greater<Real>());

            Real npCashFlows
                    = std::accumulate(exerciseValues.begin(),
                                      exerciseValues.begin() + exerciseRights, Real(0.0));
            npv.add(npCashFlows);
        }

        const Real mcUpperBound = npv.mean();
        const Real mcErrorUpperBound = npv.errorEstimate();
        if (swingOptionPrice - mcUpperBound > 2.36 * mcErrorUpperBound) {
            FAIL_CHECK("Failed to reproduce mc upper bounds"
                               << "\n    mc upper Bound: " << mcUpperBound
                               << "\n    Price:          " << swingOptionPrice);
        }
    }
}
