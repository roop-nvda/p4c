#ifndef BACKENDS_P4TOOLS_COMMON_LIB_UTIL_H_
#define BACKENDS_P4TOOLS_COMMON_LIB_UTIL_H_

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include <boost/format.hpp>
#include <boost/random/mersenne_twister.hpp>

#include "backends/p4tools/common/lib/formulae.h"
#include "ir/ir.h"
#include "lib/big_int_util.h"
#include "lib/cstring.h"
#include "lib/log.h"

namespace P4Tools {

/// Helper function for @printFeature
inline std::string logHelper(boost::format &f) { return f.str(); }

/// Helper function for @printFeature
template <class T, class... Args>
std::string logHelper(boost::format &f, T &&t, Args &&...args) {
    return logHelper(f % std::forward<T>(t), std::forward<Args>(args)...);
}

/// A helper function that allows us to configure logging for a particular feature. This code is
/// taken from
// https://stackoverflow.com/a/25859856
template <typename... Arguments>
void printFeature(const std::string &label, int level, const std::string &fmt,
                  Arguments &&...args) {
    boost::format f(fmt);

    auto result = logHelper(f, std::forward<Arguments>(args)...);

    LOG_FEATURE(label.c_str(), level, result);
}

/// General utility functions that are not present in the compiler framework.
class Utils {
    /* =========================================================================================
     *  Seeds, timestamps, randomness.
     * ========================================================================================= */
 private:
    /// The random generator of this project. It is initialized with the input seed.
    static boost::random::mt19937 rng;

    /// Stores the state of the PRNG.
    static std::optional<uint32_t> currentSeed;

 public:
    /// Return the current timestamp with millisecond accuracy.
    /// Format: year-month-day-hour:minute:second.millisecond
    /// Borrowed from https://stackoverflow.com/a/35157784
    static std::string getTimeStamp();

    /// Initialize the random generator with an integer seed. This also seeds @var currentSeed.
    /// Uses boost's mersenne twister.
    static void setRandomSeed(int seed);

    /// @returns currentSeed.
    static std::optional<uint32_t> getCurrentSeed();

    /// @returns a random integer in the range [0, @param max]. Always return 0 if no seed is set.
    static uint64_t getRandInt(uint64_t max);

    /// @returns a random big integer in the range [0, @param max]. Always return 0 if no seed is
    /// set.
    static big_int getRandBigInt(big_int max);

    /// @returns a IR::Constant with a random big integer that fits the specified bit width.
    /// The type will be an unsigned Type_Bits with @param bitWidth.
    static const IR::Constant *getRandConstantForWidth(int bitWidth);

    /// @returns a IR::Constant with a random big integer that fits the specified @param type.
    static const IR::Constant *getRandConstantForType(const IR::Type_Bits *type);

    /* =========================================================================================
     *  Variables and symbolic constants.
     * ========================================================================================= */
 public:
    /// To represent header validity, we pretend that every header has a field that reflects the
    /// header's validity state. This is the name of that field. This is not a valid P4 identifier,
    /// so it is guaranteed to not conflict with any other field in the header.
    static const cstring Valid;

    /// @see Zombie::getVar.
    static const StateVariable &getZombieVar(const IR::Type *type, int incarnation, cstring name);

    /// @see Zombie::getConst.
    static const StateVariable &getZombieConst(const IR::Type *type, int incarnation, cstring name);

    /// @see Utils::getZombieConst.
    /// This function is used to generated variables caused by undefined behavior. This is merely a
    /// wrapper function for the creation of a new Taint IR object.
    static const IR::TaintExpression *getTaintExpression(const IR::Type *type);

    /// Creates a new member variable from a concolic variable but replaces its concolic ID.
    /// This is used within concolic methods to map back several concolic variables from a single
    /// method call.
    static const StateVariable &getConcolicMember(const IR::ConcolicVariable *var, int concolicId);

    /// @returns the zombie variable with the given type, for tracking an aspect of the given
    /// table. The returned variable will be named p4t*zombie.table.t.name.idx1.idx2, where t is
    /// the name of the given table. The "idx1" and "idx2" components are produced only if idx1 and
    /// idx2 are given, respectively.
    static const StateVariable &getZombieTableVar(const IR::Type *type, const IR::P4Table *table,
                                                  cstring name,
                                                  std::optional<int> idx1_opt = std::nullopt,
                                                  std::optional<int> idx2_opt = std::nullopt);

    /// @returns the state variable for the validity of the given header instance. The resulting
    ///     variable will be boolean-typed.
    ///
    /// @param headerRef a header instance. This is either a Member or a PathExpression.
    static StateVariable getHeaderValidity(const IR::Expression *headerRef);

    /// @returns a StateVariable that is postfixed with "*". This is used for PathExpressions with
    /// are not yet members.
    static StateVariable addZombiePostfix(const IR::Expression *paramPath,
                                          const IR::Type_Base *baseType);

    /// @returns a method call to an internal extern consumed by the interpreter. The return type
    /// is typically Type_Void.
    static const IR::MethodCallExpression *generateInternalMethodCall(
        cstring methodName, const std::vector<const IR::Expression *> &argVector,
        const IR::Type *returnType = IR::Type_Void::get());

    /// Shuffles the given iterable @param inp
    template <typename T>
    static void shuffle(T *inp) {
        std::shuffle(inp->begin(), inp->end(), rng);
    }

    /// @returns a random element from the given range between @param start and @param end.
    template <typename Iter>
    static Iter pickRandom(Iter start, Iter end) {
        int random = getRandInt(std::distance(start, end) - 1);
        std::advance(start, random);
        return start;
    }

    /// Convert a container type (array, set, vector, etc.) into a well-formed [val1, val2, ...]
    /// representation. This function is used for debugging output.
    template <typename ContainerType>
    static std::string containerToString(const ContainerType &container) {
        std::stringstream stringStream;

        stringStream << '[';
        auto val = container.cbegin();
        if (val != container.cend()) {
            stringStream << *val++;
            while (val != container.cend()) {
                stringStream << ", " << *val++;
            }
        }
        stringStream << ']';
        return stringStream.str();
    }
};

}  // namespace P4Tools

#endif /* BACKENDS_P4TOOLS_COMMON_LIB_UTIL_H_ */
