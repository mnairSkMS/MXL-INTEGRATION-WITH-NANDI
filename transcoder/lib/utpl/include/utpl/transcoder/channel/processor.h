#ifndef __UTPL_TRANSCODER_CHANNEL_PROCESSOR_H__
#define __UTPL_TRANSCODER_CHANNEL_PROCESSOR_H__

#include <vector>

#include "utpl/transcoder/channel/flowline.h"
#include "utpl/transcoder/channel/connector.h"
#include "utpl/transcoder/channel/compositor.h"


namespace utpl {
    namespace transcoder {
        class Processor {
            UTPL_CLASS_INTERFACE(Processor) {}

            public:
                virtual std::shared_ptr<Flowline> createFlowLine(Container container, const transcoder::StreamParameters &outParams) = 0;

                virtual bool isAudio() const = 0;
                virtual bool isVideo() const = 0;
                virtual bool isCaption() const = 0;

                virtual const StreamParameters &getInputParameters() = 0;

                virtual void prepare(pipeline::BinPtr &bin) = 0;
                virtual void link() = 0;
                virtual const std::vector<std::shared_ptr<Flowline>> &getFlowlines() const = 0;
                virtual std::shared_ptr<Compositor> getCompositor() const = 0;
                virtual pipeline::ElementPtr getMuter() = 0;

                virtual std::shared_ptr<transcoder::StreamConnector> getConnector() = 0;
                virtual void setConnector(std::shared_ptr<transcoder::StreamConnector> connector) = 0;

                virtual const std::string &getName() = 0;

                virtual void blank(bool blank) = 0;

                virtual bool compatible(const StreamParameters &outputParameters) = 0;

            public:
                static std::unique_ptr<Processor> create(const std::string &name, const StreamParameters &outputParameters);
        };
    }
}

#endif /* __UTPL_TRANSCODER_CHANNEL_PROCESSOR_H__ */