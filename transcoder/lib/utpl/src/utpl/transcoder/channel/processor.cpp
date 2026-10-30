#include "utpl/common/debug.h"
#include "utpl/common/types/exception.h"

#include "utpl/transcoder/channel/processor.h"

// Implementations
#include "processor/passthrough.h"
#include "processor/transcoder/audio.h"
#include "processor/transcoder/video.h"
#include "processor/transcoder/dolby.h"

using namespace utpl;


std::unique_ptr<transcoder::Processor> transcoder::Processor::create(const std::string &name, const StreamParameters &outputParameters) {
    if (outputParameters.isPassthrough()) {
        return std::make_unique<PassthroughProcessor>(name, outputParameters);
        
    } else if (outputParameters.hasVideoParameters()) {
        return std::make_unique<VideoTranscodingProcessor>(name, outputParameters);

    } else if (outputParameters.hasAudioParameters()) {
        if (DolbyTranscodingProcessor::isCompatible(outputParameters)) {
            return std::make_unique<DolbyTranscodingProcessor>(name, outputParameters);

        } else {
            return std::make_unique<AudioTranscodingProcessor>(name, outputParameters);
        }
    }

    throw utpl::NotSupportedException("Requested processor is not yet supported");
}