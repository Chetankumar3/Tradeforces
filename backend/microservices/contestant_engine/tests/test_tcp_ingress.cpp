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

bool runIngressScenario(const std::vector<std::string>& rawMessages) {
    auto& sessionManager = FIXSessionManager::getInstance();
    TcpIngress ingress(sessionManager);

    for (const auto& rawMessage : rawMessages) {
        if (!ingress.validateIngressMessage(rawMessage)) {
            return false;
        }

        FIXMessage message(MsgType::NEW_ORDER_SINGLE);
        if (!message.parse(rawMessage)) {
            return false;
        }

        if (!TcpIngress::hasRequiredFields(message)) {
            return false;
        }
    }

    return true;
}

bool runIngressScenarioWithHardcodedSamples() {
    const std::vector<std::string> rawMessages = {
        "8=FIX.4.2\u00019=71\u000135=D\u000111=run-95-bot-59-1781506691635775500\u000138=22\u000140=1\u000154=1\u000155=MSFT\u000159=0\u000110=254\u0001",
        "8=FIX.4.2\u00019=70\u000135=D\u000111=run-95-bot-84-1781506691636016693\u000138=1\u000140=1\u000154=1\u000155=GOOG\u000159=0\u000110=188\u0001",
        "8=FIX.4.2\u00019=80\u000135=D\u000111=run-95-bot-64-1781506691636045854\u000138=28\u000140=2\u000154=2\u000155=GOOG\u000159=0\u000144=96.24\u000110=160\u0001",
    };
    TcpIngress ingress(sessionManager);

    const std::string rawMessage = makeFixMessage("D", {{11, "ORDER-1"}, {38, "100"}, {40, "2"}, {44, "1"}, {54, "1"}, {55, "AAPL"}, {59, "0"}});

    EXPECT_TRUE(ingress.validateIngressMessage(rawMessage));
}

TEST(TcpIngressValidationTest, TcpIngressScenarioHelperReturnsStatus) {
    EXPECT_TRUE(runIngressScenarioWithHardcodedSamples());
}

TEST(TcpIngressValidationTest, AcceptsMarketOrderWithoutPriceTag) {
    FIXMessage message(MsgType::NEW_ORDER_SINGLE);
    message.setField(11, "ORDER-1");
    message.setField(35, "D");
    message.setField(38, "22");
    message.setField(40, "1");
    message.setField(54, "1");
    message.setField(55, "MSFT");
    message.setField(59, "0");

    EXPECT_TRUE(TcpIngress::hasRequiredFields(message));
}

TEST(TcpIngressValidationTest, RejectsLimitOrderWithoutPriceTag) {
    FIXMessage message(MsgType::NEW_ORDER_SINGLE);
    message.setField(11, "ORDER-1");
    message.setField(35, "D");
    message.setField(38, "22");
    message.setField(40, "2");
    message.setField(54, "1");
    message.setField(55, "MSFT");
    message.setField(59, "0");

    EXPECT_FALSE(TcpIngress::hasRequiredFields(message));
}
