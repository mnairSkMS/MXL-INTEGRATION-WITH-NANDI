#ifndef __UTPL_TRANSCODER_CHANNEL_COMPOSITOR_H__
#define __UTPL_TRANSCODER_CHANNEL_COMPOSITOR_H__

#include "utpl/common/time.h"

#include "utpl/pipeline/pad.h"
#include "utpl/pipeline/bin.h"
#include "utpl/pipeline/element.h"

#include "utpl/transcoder/stream.h"

namespace utpl {
    namespace transcoder {
        class Compositor;

        using CompositorPtr = std::shared_ptr<Compositor>;

        class Compositor {
            public:
                class Layer {
                    public:
                        Layer(
                            pipeline::BinPtr     binPtr,
                            pipeline::PadPtr     compositorPad, 
                            const Rectangle     &layerSize,
                            bool                 cropEnabled,
                            bool                 createProxyPad
                        );

                        void setZOrder(int order);
                        int getZOrder() const;

                        void setOpacity(float opacity, int transitionMs = 0);
                        float getOpacity() const;
                        
                        const Rectangle &getOuputWindow() const;
                        const Rectangle &getInputWindow() const;

                        void setWindows(const Rectangle &inputWindow, const Rectangle &outputWindow, int transitionMs = 0);
                        void setSize(const Rectangle &rect);

                        pipeline::PadPtr getSinkPad();
                        pipeline::PadPtr getCompositorPad();

                        GstClockTime getRunningTime() const;

                        std::recursive_mutex &getMutex();

                    private:
                        void _setWindows(int transitionMs);

                    private:
                        mutable std::recursive_mutex _mutex;

                        pipeline::PadPtr     _compositorPad;
                        pipeline::PadPtr     _sinkPad;
                        pipeline::ElementPtr _cropElement;

                        Rectangle _layerSize;
                        Rectangle _inputWindow;
                        Rectangle _outputWindow;

                        Rectangle _inputPendingWindow;
                        Rectangle _outpuPendingtWindow;
                };

            public:
                Compositor(bool createProxyPadsForLayers);
                ~Compositor();

                void prepare(const transcoder::VideoParameters &inParameters, pipeline::BinPtr &bin);

                pipeline::PadPtr getSrcPad() const;

                std::shared_ptr<Layer> getVideoLayer();

                const Rectangle &getWindowSize() const;
                void setWindowSize(const Rectangle &rect);

                std::shared_ptr<Layer> createLayer();
                void destroyLayer(std::shared_ptr<Layer> layer);

                void blank(bool blank);

            private:
                pipeline::ElementPtr _compositor;
                pipeline::ElementPtr _outputScaler;
                pipeline::ElementPtr _outputFilter;
                
                pipeline::BinPtr _bin;
                bool             _createProxyPadsForLayers;

                pipeline::PadPtr _srcPad;
                Rectangle _layerSize;

                std::shared_ptr<Layer> _videoLayer;
                std::vector<std::shared_ptr<Layer>> _graphicLayers;
                guint _nextOverlayZOrder = 101;
        };
    }
}

#endif /* __UTPL_TRANSCODER_CHANNEL_COMPOSITOR_H__ */
