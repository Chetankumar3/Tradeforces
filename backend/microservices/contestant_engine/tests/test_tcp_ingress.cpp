#include <gtest/gtest.h>

#define private public
#include "contestant_engine/tcp_ingress.h"
#undef private

#include <iomanip>
#include <sstream>

using namespace fix_gateway::contestant_engine;
using namespace fix_gateway::fix_engine;

namespace {
constexpr char kSoh = '\x01';

std::string makeChecksum(const std::string& messageWithoutChecksum) {
    int sum = 0;
    for (unsigned char ch : messageWithoutChecksum) {
        sum += ch;
    }
    sum %= 256;

    std::ostringstream oss;
    oss << std::setw(3) << std::setfill('0') << sum;
    return oss.str();
}

std::string makeFixMessage(const std::string& msgType, const std::vector<std::pair<int, std::string>>& fields) {
    std::ostringstream oss;
    oss << "8=FIX.4.2" << kSoh;

    std::ostringstream body;
    body << "35=" << msgType << kSoh;
    body << "49=SENDER" << kSoh;
    body << "56=TARGET" << kSoh;
    body << "34=1" << kSoh;
    body << "52=20240615-05:07:14.000" << kSoh;
    for (const auto& [tag, value] : fields) {
        body << tag << '=' << value << kSoh;
    }

    const std::string bodyText = body.str();
    oss << "9=" << bodyText.size() << kSoh;
    oss << bodyText;

    const std::string messageWithoutChecksum = oss.str();
    oss << "10=" << makeChecksum(messageWithoutChecksum) << kSoh;
    return oss.str();
}
} // namespace

TEST(TcpIngressValidationTest, AcceptsWellFormedFixFrame) {
    auto& sessionManager = FIXSessionManager::getInstance();
    TcpIngress ingress(sessionManager);

    const std::string rawMessage = makeFixMessage("D", {{11, "ORDER-1"}, {38, "100"}, {40, "2"}, {44, "1"}, {54, "1"}, {55, "AAPL"}, {59, "0"}});

    EXPECT_TRUE(ingress.validateIngressMessage(rawMessage));
}
