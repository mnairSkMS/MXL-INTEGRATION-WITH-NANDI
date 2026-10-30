#ifndef __TRANSCODER_CHANNEL_CONNECTOR_COMPLEX_AUDIO_H__
#define __TRANSCODER_CHANNEL_CONNECTOR_COMPLEX_AUDIO_H__

#include <mutex>

#include "utpl/transcoder/channel/connector.h"

namespace utpl {
    namespace transcoder {
        class AudioComplexConnector : public StreamConnector {
            private:
                struct InputContext {
                    pipeline::ElementPtr  clockSync;
                    pipeline::PadPtr      selectorPad;
                    pipeline::GhostPadPtr inputPad;
                    
                    bool isLive;

                    InputContext(bool isLive) {
                        this->isLive = isLive;
                    }
                };

            public:
                AudioComplexConnector(const std::string &name, pipeline::BinPtr channelBin, bool forLiveOutput);

            public:
                std::shared_ptr<InputStream> getStream() override;

                /// Returns stream parameters of connector
                const StreamParameters &getStreamParameters() override;

                pipeline::PadPtr getPad(const StreamParameters &parameters) override;
                pipeline::PadPtr peekPad() override;

                void prepare(pipeline::BinPtr &bin) override;
                void link() override;
                void unlink() override;
                void cleanup() override;

                pipeline::ElementPtr getNode() override;

            public:
                bool hasStream(std::shared_ptr<InputStream> stream);
                void addStream(std::shared_ptr<InputStream> stream, bool isLive);
                void removeStream(std::shared_ptr<InputStream> stream);
                void setCurrentStream(std::shared_ptr<InputStream> stream);
                std::shared_ptr<InputStream> getCurrentStream();

                pipeline::ElementPtr getMuter();
                void blank(bool blank);

            private:
                void _linkStream(InputContext &ctx, std::shared_ptr<InputStream> stream);
                void _unlinkStream(InputContext &ctx, std::shared_ptr<InputStream> stream);

                void _setActiveInput(pipeline::PadPtr pad, const StreamParameters &streamParameters);

                pipeline::PadPtr _getOutputPad();

            private:
                pipeline::BinPtr _bin;
                pipeline::BinPtr _channelBin;
                bool             _prepared;
                bool             _linked;

                pipeline::ElementPtr        _selector;
                pipeline::ElementPtr        _convert;
                pipeline::ElementPtr        _splitter;
                pipeline::ElementPtr        _fallback;
                pipeline::ElementPtr        _fallbackSrc;
                pipeline::ElementPtr        _fallbackSrcFilter;

                pipeline::ElementPtr        _muter;

                pipeline::ElementPtr        _dummySrc;
                pipeline::ElementPtr        _dummySrcFilter;
                pipeline::ElementPtr        _dummySrcSync;
                pipeline::PadPtr            _dummySrcPad;
                StreamParameters            _dummySrcParameters;

                GstSegment _selectorSegment;

                std::map<pipeline::GhostPadPtr, StreamParameters> _pads;

                std::shared_ptr<InputStream> _streamCurrent;
                std::map<std::shared_ptr<InputStream>, InputContext> _streamMap;

                StreamParameters _outputParameters;
                bool             _outputIsLive;

                // Stream membership and the active-input switch are reached from more than one
                // thread once a channel has several inputs: the ad switcher runs on the channel's
                // timed-callback thread while a playlist rolling to its next item runs on its own
                // streaming thread. Both rebuild the dummy source chain, so they must not
                // interleave. Recursive because setCurrentStream/removeStream call _setActiveInput.
                std::recursive_mutex _guard;
        };
    }
}

#endif /* __TRANSCODER_CHANNEL_CONNECTOR_COMPLEX_VIDEO_H__ */
