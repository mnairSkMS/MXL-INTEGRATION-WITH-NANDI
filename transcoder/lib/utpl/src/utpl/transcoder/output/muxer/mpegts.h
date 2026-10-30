#ifndef _UTPL_TRANSCODER_MUXER_MPEGTS_H_
#define _UTPL_TRANSCODER_MUXER_MPEGTS_H_

#include "utpl/transcoder/output/muxer.h"
#include "utpl/transcoder/output/sink.h"

namespace utpl {
    namespace transcoder {
        namespace output {
            class MpegTsMuxer : public Muxer {

                public:
                    MpegTsMuxer(const std::string &name, const OutputParameters &parameters);

                public:
                    virtual void link() override;
                    const std::string &getName() override;

                    void putScteData(const Scte35Data &data) override;

                    pipeline::ElementPtr getNode() override;
                    pipeline::PadPtr getSrcPad() override;
                    pipeline::PadPtr getSinkPad(MediaType mediaType, Pid pid = PID_NULL) override;

                protected:
                    std::string _name;

                    pipeline::ElementPtr                _muxer;
                    pipeline::BinPtr                    _bin;
                    std::vector<pipeline::GhostPadPtr>  _srcPads;
                    std::vector<std::pair<Pid, pipeline::GhostPadPtr>> _sinkPads;

                    OutputParameters _outputParameters;
            };
        }
    }
}

#endif // _UTPL_TRANSCODER_MUXER_MPEGTS_H_