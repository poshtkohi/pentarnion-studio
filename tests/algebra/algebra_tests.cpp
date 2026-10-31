#include "algebra/pentarnion.hpp"
#include "basis_fixtures.hpp"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>

using namespace penta::algebra;

namespace {
int checks = 0;

void require(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

void near(double actual, double expected, const std::string& message,
          double absolute = 1e-12, double relative = 1e-12) {
    require(std::isfinite(actual) && std::abs(actual - expected)
            <= absolute + relative * std::abs(expected), message);
}

void near(const Pentarnion& actual, const Coefficients& expected, const std::string& message,
          double absolute = 1e-12, double relative = 1e-12) {
    for (std::size_t index = 0; index < 5; ++index)
        near(actual.coefficients()[index], expected[index], message + " coefficient " + std::to_string(index),
             absolute, relative);
}

template<class Function> void expectError(const std::string& code, Function function) {
    try { function(); }
    catch (const AlgebraError& error) {
        require(error.code == code, "Wrong algebra error code: " + error.code + ", expected " + code);
        return;
    }
    throw std::runtime_error("Expected algebra error " + code);
}

// Independent coordinate equations from the paper, rather than a second
// structure-constant tensor or a call back into multiply().
Coefficients referenceProduct(const Coefficients& p, const Coefficients& q, double gamma) {
    const double k = 1.0 / std::sqrt(1.0 + gamma * gamma);
    const double s = gamma * k;
    const double h = 1.0 / std::sqrt(2.0);
    return {
        p[0]*q[0] - p[1]*q[1] - p[2]*q[2] - p[3]*q[3] - p[4]*q[4],
        p[0]*q[1] + p[1]*q[0] + k*(p[2]*q[3]-p[3]*q[2])
            + h*(p[4]*q[2]+p[4]*q[3]-p[2]*q[4]-p[3]*q[4]),
        p[0]*q[2] + p[2]*q[0] + k*(p[3]*q[1]-p[1]*q[3])
            + h*(p[4]*q[1]+p[4]*q[3]-p[1]*q[4]-p[3]*q[4]),
        p[0]*q[3] + p[3]*q[0] + k*(p[1]*q[2]-p[2]*q[1])
            + h*(p[4]*q[1]+p[4]*q[2]-p[1]*q[4]-p[2]*q[4]),
        p[0]*q[4] + p[4]*q[0] + s*(p[1]*q[2]-p[2]*q[1]-p[1]*q[3]
                                  +p[3]*q[1]+p[2]*q[3]-p[3]*q[2])
    };
}

void basisAndContexts() {
    require(basisFixtures.size() == 75, "Independent 75-pair fixture count");
    for (const auto& fixture : basisFixtures) {
        const Context context(fixture.gamma);
        const auto result = multiply(Pentarnion::basis(context, fixture.left),
                                     Pentarnion::basis(context, fixture.right));
        near(result, fixture.expected, "Independent basis fixture", 5e-15, 5e-15);
    }
    const Context context;
    const Pentarnion original(context, {1, 2, 3, 4, 5});
    for (const double bad : {-0.1, 1.1, std::numeric_limits<double>::infinity(),
                             std::numeric_limits<double>::quiet_NaN()}) {
        bool rejected = false;
        try { const Context invalid(bad); (void)invalid; }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "Invalid gamma must be rejected");
    }
    near(original, {1,2,3,4,5}, "Rejected context leaves value intact");
    const auto different = Pentarnion::basis(Context(0), 1);
    expectError("R3002", [&] { (void)add(original, different); });
    expectError("R3002", [&] { (void)subtract(original, different); });
    expectError("R3002", [&] { (void)multiply(original, different); });
    expectError("R3002", [&] { (void)leftDivide(original, different); });
    expectError("R3002", [&] { (void)rightDivide(original, different); });
    expectError("R3004", [&] { (void)Pentarnion::basis(context, 5); });
    expectError("R3004", [&] { (void)coefficient(original, 5); });
    expectError("R3001", [&] { (void)Pentarnion(context, {0, 0, 0, 0, std::numeric_limits<double>::infinity()}); });
    expectError("R3001", [&] { (void)Pentarnion::scalar(context, std::numeric_limits<double>::quiet_NaN()); });

    std::atomic<bool> safe{true};
    auto worker = [&](double gamma) {
        const Context local(gamma);
        for (int count = 0; count < 1000; ++count) {
            const auto product = multiply(Pentarnion::basis(local, 1), Pentarnion::basis(local, 2));
            const double kappa = 1.0 / std::sqrt(1.0 + gamma*gamma);
            if (std::abs(product.coefficients()[3] - kappa) > 1e-15
                    || std::abs(product.coefficients()[4] - gamma*kappa) > 1e-15) safe = false;
        }
    };
    std::thread first(worker, 0.0), second(worker, 1.0);
    first.join(); second.join();
    require(safe, "Concurrent immutable contexts remain independent");
}

void arithmeticAndMatrices() {
    for (const double gamma : {0.0, 0.5, 1.0}) {
        const Context context(gamma);
        const Coefficients pc{0.7,-0.2,1.5,0.8,-0.3}, qc{-0.4,1.1,0.5,-0.2,0.9};
        const Pentarnion p(context, pc), q(context, qc);
        const auto one = Pentarnion::scalar(context, 1);
        near(multiply(one, p), pc, "Left identity");
        near(multiply(p, one), pc, "Right identity");
        near(multiply(p, q), referenceProduct(pc, qc, gamma), "Independent mixed coordinate product");
        near(add(p,q), {0.3,0.9,2.0,0.6,0.6}, "Mixed addition");
        near(subtract(p,q), {1.1,-1.3,1.0,1.0,-1.2}, "Mixed subtraction");
        near(scale(p,2), {1.4,-0.4,3.0,1.6,-0.6}, "Real scale");
        near(conjugate(p), {0.7,0.2,-1.5,-0.8,0.3}, "Conjugate all four pure coefficients");
        near(purePart(p), {0,-0.2,1.5,0.8,-0.3}, "Pure part retains four components");
        near(scalarPart(p), 0.7, "Scalar extraction");
        near(coefficient(p,4), -0.3, "ell coefficient retained");
        const double n = 0.49+0.04+2.25+0.64+0.09;
        near(normSquared(p), n, "Squared norm, not norm");
        near(multiply(p,conjugate(p)), {n,0,0,0,0}, "Right conjugation norm identity");
        near(multiply(conjugate(p),p), {n,0,0,0,0}, "Left conjugation norm identity");
        const auto quadratic = add(subtract(multiply(p,p),scale(p,2*pc[0])),Pentarnion::scalar(context,n));
        near(quadratic, {0,0,0,0,0}, "Quadratic identity");
        const auto distribution = subtract(multiply(p,add(p,q)), add(multiply(p,p),multiply(p,q)));
        near(distribution, {0,0,0,0,0}, "Distributivity");
        const auto l = leftMatrix(p), r = rightMatrix(p);
        for (std::size_t column=0; column<5; ++column) {
            Coefficients unit{}; unit[column]=1;
            const auto expectedLeft = referenceProduct(pc,unit,gamma);
            const auto expectedRight = referenceProduct(unit,pc,gamma);
            for (std::size_t row=0; row<5; ++row) {
                near(l[row][column], expectedLeft[row], "Left matrix column convention");
                near(r[row][column], expectedRight[row], "Right matrix column convention");
            }
        }
        const auto ij = commutator(Pentarnion::basis(context,1),Pentarnion::basis(context,2));
        const double kappa=1/std::sqrt(1+gamma*gamma), sigma=gamma*kappa;
        near(ij,{0,0,0,2*kappa,2*sigma},"Commutator ordered products");
    }
    const Context context;
    const auto i=Pentarnion::basis(context,1), j=Pentarnion::basis(context,2);
    near(multiply(multiply(i,j),j),{0,0,0,0.5,-0.5},"Paper left-associated ijj");
    near(multiply(i,multiply(j,j)),{0,-1,0,0,0},"Paper right-associated ijj");
    near(associator(i,j,j),{0,1,0,0.5,-0.5},"Associator preserves grouping");
    const auto quaternionI=Pentarnion::basis(Context(0),1), quaternionJ=Pentarnion::basis(Context(0),2);
    near(associator(quaternionI,quaternionJ,quaternionJ),{0,0,0,0,0},"Quaternion named subspace at gamma0");
    const auto ell=Pentarnion::basis(Context(0),4);
    require(normSquared(associator(quaternionI,ell,ell))>0.1,
            "Full gamma0 algebra remains nonassociative with ell");
}

void reciprocalAndPowers() {
    const Context context;
    const auto one=Pentarnion::scalar(context,1), zero=Pentarnion::scalar(context,0);
    const auto i=Pentarnion::basis(context,1), j=Pentarnion::basis(context,2);
    const Pentarnion p(context,{0.7,-0.2,1.5,0.8,-0.3});
    near(multiply(p,inverse(p)),one.coefficients(),"Canonical reciprocal right product");
    near(multiply(inverse(p),p),one.coefficients(),"Canonical reciprocal left product");
    near(power(p,2),multiply(p,p).coefficients(),"Square");
    near(power(p,3),multiply(multiply(p,p),p).coefficients(),"One-element cubic power");
    near(multiply(multiply(p,p),p),multiply(p,multiply(p,p)).coefficients(),"One-element power associativity");
    near(power(p,-2),multiply(inverse(p),inverse(p)).coefficients(),"Negative power of canonical reciprocal");
    near(power(zero,0),one.coefficients(),"Zero to zero is identity");
    near(power(zero,4),zero.coefficients(),"Zero positive power");
    near(power(i,std::numeric_limits<std::int64_t>::min()),one.coefficients(),"INT64_MIN exponent magnitude");
    near(power(j,-2),{-1,0,0,0,0},"Negative even basis power");
    near(multiply(multiply(i,inverse(j)),j),{0,0,0,-0.5,0.5},"Canonical reciprocal does not cancel third factors");
    expectError("R3003",[&]{(void)inverse(zero);});
    expectError("R3003",[&]{(void)power(zero,-1);});
    for (const double magnitude : {1e-300,1e300}) {
        const Pentarnion extreme(context,{magnitude,magnitude,-magnitude,magnitude,-magnitude});
        const auto reciprocal=inverse(extreme);
        near(multiply(extreme,reciprocal),one.coefficients(),"Scale-safe reciprocal",1e-12,1e-12);
        near(multiply(reciprocal,extreme),one.coefficients(),"Scale-safe reciprocal opposite order",1e-12,1e-12);
        for (const double value:reciprocal.coefficients()) require(value!=0&&std::isfinite(value),"Extreme reciprocal remains nonzero finite");
    }
    const auto huge=Pentarnion::scalar(context,1e308);
    near(inverse(huge).coefficients()[0],1e-308,"Reciprocal avoids overflowing norm",0,1e-14);
    near(inverse(Pentarnion::scalar(context,1e-308)).coefficients()[0],1e308,"Tiny nonzero reciprocal accepted",0,1e-14);
    expectError("R3001",[&]{(void)normSquared(huge);});
    expectError("R3001",[&]{(void)inverse(Pentarnion::scalar(context,std::numeric_limits<double>::denorm_min()));});
    expectError("R3001",[&]{(void)add(huge,huge);});
    expectError("R3001",[&]{(void)scale(huge,2);});
    expectError("R3001",[&]{(void)multiply(huge,huge);});
    expectError("R3009",[&]{(void)power(Pentarnion::scalar(context,1e-200),2);});
    near(power(huge,1),huge.coefficients(),"Exponent1 does not compute an unnecessary overflowing square");
}

void division() {
    const Context context;
    const double h=1/std::sqrt(2.0);
    const auto i=Pentarnion::basis(context,1), j=Pentarnion::basis(context,2);
    const auto zero=Pentarnion::scalar(context,0), one=Pentarnion::scalar(context,1);
    const auto divided=rightDivide(i,j);
    require(divided.status==DivisionStatus::Unique&&divided.solution.has_value(),"Right division unique");
    require(divided.rank==5&&divided.relativeResidual<1e-13&&std::isfinite(divided.condition),"Division diagnostics");
    near(*divided.solution,{0,-h,0,-h,h},"Independent paper right-division solution");
    near(multiply(*divided.solution,j),i.coefficients(),"Right quotient satisfies x*j=i");
    near(rightQuotient(i,j),divided.solution->coefficients(),"Strict right quotient");
    for (const double magnitude : {1e-308, 1e308}) {
        const auto scaledResult = rightDivide(scale(i,magnitude),j);
        require(scaledResult.status == DivisionStatus::Unique && scaledResult.solution.has_value(),
                "Representable extreme off-diagonal quotient remains accepted");
        Coefficients expected{};
        for (std::size_t index = 0; index < 5; ++index)
            expected[index] = divided.solution->coefficients()[index] * magnitude;
        near(*scaledResult.solution,expected,"Extreme ordered quotient",0,2e-12);
    }

    const Pentarnion q(context,{2,0.2,-0.3,0.4,0.1}), x(context,{-0.25,0.9,-1.1,0.5,0.7});
    const Pentarnion leftRhs(context,referenceProduct(q.coefficients(),x.coefficients(),1));
    const Pentarnion rightRhs(context,referenceProduct(x.coefficients(),q.coefficients(),1));
    near(leftQuotient(q,leftRhs),x.coefficients(),"Left equation ordering on mixed coefficients");
    near(rightQuotient(rightRhs,q),x.coefficients(),"Right equation ordering on mixed coefficients");
    for(const double magnitude:{1e-250,1.0,1e250}) {
        const auto result=leftDivide(scale(q,magnitude),scale(leftRhs,magnitude));
        require(result.status==DivisionStatus::Unique&&result.rank==5,"Rank invariant under system scaling");
        near(*result.solution,x.coefficients(),"Scale-invariant equation solution",2e-12,2e-12);
    }
    for(const double magnitude:{1e-308,1e308}) {
        const auto result=rightDivide(Pentarnion::scalar(context,magnitude),Pentarnion::scalar(context,magnitude));
        require(result.status==DivisionStatus::Unique&&result.rank==5,"Extreme diagonal system accepted");
        near(*result.solution,one.coefficients(),"Extreme scalar equation solution");
    }

    const Pentarnion a(context,{0,0,1,-1,-1}), b(context,{-3*std::sqrt(2.0),1,0,-4,4});
    near(multiply(a,b),{0,0,0,0,0},"Independent paper zero-divisor pair",2e-14,2e-14);
    near(multiply(a,inverse(a)),one.coefficients(),"Singular operator element still has canonical reciprocal");
    const auto consistent=leftDivide(a,zero);
    require(consistent.status==DivisionStatus::SingularConsistent&&consistent.rank<5&&!consistent.solution,
            "Singular consistent solve exposes no pseudoinverse quotient");
    // Independently, every coefficient of i in a*x is zero for a=j-k-ell.
    const auto inconsistent=leftDivide(a,i);
    require(inconsistent.status==DivisionStatus::SingularInconsistent&&inconsistent.rank<5&&!inconsistent.solution,
            "Zero i-row makes a*x=i inconsistent");
    require(rightDivide(zero,b).status==DivisionStatus::SingularConsistent,"Right zero-divisor operator detected");
    for(const double magnitude:{1e-200,1e200}) {
        require(leftDivide(scale(a,magnitude),zero).status==DivisionStatus::SingularConsistent,
                "Numerical singularity scale invariance");
        require(leftDivide(scale(a,magnitude),scale(i,magnitude)).status==DivisionStatus::SingularInconsistent,
                "Inconsistency scale invariance");
    }
    expectError("R3005",[&]{(void)leftQuotient(a,zero);});
    expectError("R3006",[&]{(void)leftQuotient(a,i);});
    expectError("R3003",[&]{(void)rightQuotient(one,zero);});
    expectError("R3003",[&]{(void)leftQuotient(zero,one);});
    require(leftDivide(zero,zero).status==DivisionStatus::SingularConsistent,"Zero equation nonunique");
    require(leftDivide(zero,one).status==DivisionStatus::SingularInconsistent,"Zero equation inconsistent");

    bool illConditionedFound=false;
    for(const double delta:{1e-2,1e-3,1e-4,1e-5,1e-6,1e-7,1e-8,1e-9,1e-10}) {
        const auto perturbed=add(a,Pentarnion::scalar(context,delta));
        const auto result=leftDivide(perturbed,zero);
        if(result.status==DivisionStatus::IllConditioned) {
            require(result.rank==5&&result.condition>DivisionOptions{}.conditionLimit&&!result.solution,
                    "Full-rank ill-conditioned solve is explicitly rejected");
            expectError("R3007",[&]{(void)leftQuotient(perturbed,zero);});
            illConditionedFound=true;
            break;
        }
    }
    require(illConditionedFound,"Near-singular family contains a resolved but ill-conditioned operator");
    const auto unrepresentable=rightDivide(Pentarnion::scalar(context,1e308),Pentarnion::scalar(context,1e-308));
    require(unrepresentable.status==DivisionStatus::NonFinite&&!unrepresentable.solution,
            "Unrepresentable unique solve is never exposed");
    DivisionOptions invalid;
    invalid.rankRelativeTolerance=0;
    expectError("R3010",[&]{(void)leftDivide(q,one,invalid);});
}
}

int main() {
    try {
        basisAndContexts();
        arithmeticAndMatrices();
        reciprocalAndPowers();
        division();
        std::cout<<"Algebra regression passed: "<<checks<<" checks including 75 independent basis fixtures.\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<"Algebra regression failed after "<<checks<<" checks: "<<error.what()<<'\n';
        return 1;
    }
}
