#include <gtest/gtest.h>

#include "utpl/common/types.h"
#include "utpl/transcoder/input.h"

using namespace utpl::transcoder;

TEST(MxlTapParameters, IsOffByDefault) {
    MxlTapParameters tap;

    EXPECT_FALSE(tap.isEnabled());
    EXPECT_EQ(tap.getAudioChannels(), MxlTapParameters::DEFAULT_AUDIO_CHANNELS);

    InputParameters input;
    EXPECT_FALSE(input.getMxlTap().isEnabled());
}

TEST(MxlTapParameters, NeedsADomainAndAFlow) {
    MxlTapParameters tap;

    tap.setDomain("/run/mxl");
    EXPECT_FALSE(tap.isEnabled()) << "a domain alone does nothing";

    tap.setVideoFlowId("5fbec3b1-1b0f-417d-9059-8b94a47197ed");
    EXPECT_TRUE(tap.isEnabled());

    tap.setDomain("");
    EXPECT_FALSE(tap.isEnabled()) << "a flow without a domain does nothing";

    tap.setDomain("/run/mxl");
    tap.setVideoFlowId("");
    tap.setAudioFlowId("b3bb5be7-9a4b-4be1-8b13-c96b3d6a7a10");
    EXPECT_TRUE(tap.isEnabled()) << "audio only is enough";
}

TEST(MxlTapParameters, FlowIdMustBeACanonicalUuid) {
    EXPECT_TRUE(MxlTapParameters::isValidFlowId("5fbec3b1-1b0f-417d-9059-8b94a47197ed"));
    EXPECT_TRUE(MxlTapParameters::isValidFlowId("5FBEC3B1-1B0F-417D-9059-8B94A47197ED"));

    EXPECT_FALSE(MxlTapParameters::isValidFlowId(""));
    EXPECT_FALSE(MxlTapParameters::isValidFlowId("12345"));
    EXPECT_FALSE(MxlTapParameters::isValidFlowId("5fbec3b11b0f417d90598b94a47197ed"))      << "no dashes";
    EXPECT_FALSE(MxlTapParameters::isValidFlowId("5fbec3b1-1b0f-417d-9059-8b94a47197e"))   << "one digit short";
    EXPECT_FALSE(MxlTapParameters::isValidFlowId("5fbec3b1-1b0f-417d-9059-8b94a47197ede")) << "one digit long";
    EXPECT_FALSE(MxlTapParameters::isValidFlowId("5fbec3b1-1b0f-417d-9059-8b94a47197eg"))  << "not hexadecimal";
    EXPECT_FALSE(MxlTapParameters::isValidFlowId("5fbec3b1x1b0f-417d-9059-8b94a47197ed"))  << "dash in the wrong place";
}

TEST(MxlTapParameters, SurvivesCopyingTheInputParameters) {
    MxlTapParameters tap;
    tap.setDomain("/run/mxl");
    tap.setVideoFlowId("5fbec3b1-1b0f-417d-9059-8b94a47197ed");
    tap.setAudioChannels(6);

    InputParameters original;
    original.setMxlTap(tap);

    InputParameters assigned;
    assigned = original;
    InputParameters copied(original);

    for (const InputParameters *p : {&assigned, &copied}) {
        EXPECT_TRUE(p->getMxlTap().isEnabled());
        EXPECT_EQ(p->getMxlTap().getDomain(), "/run/mxl");
        EXPECT_EQ(p->getMxlTap().getVideoFlowId(), "5fbec3b1-1b0f-417d-9059-8b94a47197ed");
        EXPECT_EQ(p->getMxlTap().getAudioChannels(), 6);
    }
}

TEST(MxlInputTransportParameters, SurvivesCopyingTheInputParameters) {
    auto mxl = std::make_unique<MxlInputTransportParameters>();
    mxl->setVideoFlowId("5fbec3b1-1b0f-417d-9059-8b94a47197ed");
    mxl->setAudioFlowId("b3bb5be7-9a4b-4be1-8b13-c96b3d6a7a10");

    std::unique_ptr<InputTransportParameters> base = std::move(mxl);

    InputParameters original;
    original.addUri("mxl:///run/mxl");
    original.addTransportParameters(base);

    InputParameters assigned;
    assigned = original;
    InputParameters copied(original);

    for (const InputParameters *p : {&original, &assigned, &copied}) {
        const auto *got = dynamic_cast<const MxlInputTransportParameters *>(p->getTransportParameters(utpl::Protocol::MXL));

        ASSERT_NE(got, nullptr);
        EXPECT_EQ(got->getProtocol(), utpl::Protocol::MXL);
        EXPECT_EQ(got->getVideoFlowId(), "5fbec3b1-1b0f-417d-9059-8b94a47197ed");
        EXPECT_EQ(got->getAudioFlowId(), "b3bb5be7-9a4b-4be1-8b13-c96b3d6a7a10");
    }
}

TEST(MxlInputTransportParameters, AnMxlUriIsRecognisedByItsScheme) {
    utpl::Protocol sub;

    EXPECT_EQ(utpl::parseProtocol("mxl:///run/mxl", sub), utpl::Protocol::MXL);
    EXPECT_EQ(utpl::parseProtocol("MXL:///run/mxl", sub), utpl::Protocol::MXL);
}
