#ifndef __UTPL_PIPELINE_IMPL_GSTREAMER_FACTORY_H__
#define __UTPL_PIPELINE_IMPL_GSTREAMER_FACTORY_H__

#include "utpl/common/utils/class.h"

#include "utpl/pipeline/factory.h"

namespace utpl {
    namespace impl {
        class GstreamerFactory : public utpl::pipeline::Factory {
            UTPL_CLASS_DECLARE_SINGLETON(GstreamerFactory);

            public:
                virtual std::unique_ptr<pipeline::Pipe> createPipe() override;

                virtual std::unique_ptr<pipeline::NodeBin> createBin(const pipeline::NodeParameters &parameters) override;
                virtual std::unique_ptr<pipeline::Node>    createSource(const pipeline::SourceNodeParameters &parameters) override;
                virtual std::unique_ptr<pipeline::Node>    createSink(const pipeline::SinkNodeParameters &parameters) override;
        };
    }
}

#endif /* __UTPL_PIPELINE_IMPL_GSTREAMER_FACTORY_H__ */