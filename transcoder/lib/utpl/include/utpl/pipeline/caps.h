#ifndef __UTPL_PIPELINE_CAPS_H__
#define __UTPL_PIPELINE_CAPS_H__

#include "utpl/common/types.h"
#include "utpl/common/utils/gstreamer/wrapper.h"

namespace utpl {
    namespace pipeline {
        class Caps;

        using CapsPtr = std::shared_ptr<Caps>;

        class Caps : public GstWrapper<GstCapsPtr> {
            public:
                Caps(const GstCapsPtr &ptr);

                const std::string &getName() const override;

            public:
                std::string toString() const;

                MediaType getMediaType() const;
                Container getContainer() const;
                Codec getCodec() const;

                void setMediaFormat(MediaType media, Container container, Codec codec);

                int                 getInt(const std::string &name, int defaultValue = 0) const;
                std::string         getString(const std::string &name, const std::string &defaultValue = "") const;
                std::pair<int, int> getFraction(const std::string &name, const std::pair<int, int> &defaultValue = { 0, 0 }) const;

                void setInt(const std::string &name, int value);
                void setString(const std::string &name, const std::string &value);
                void setFraction(const std::string &name, const std::pair<int, int> &value);
                void setValue(const std::string &name, GValue *value);

                // Video generic parameters
                int getHeight(int defaultValue = 0) const;
                int getWidth(int defaultValue = 0) const;
                Fraction getFrameRate(const Fraction &defaultValue = Fraction()) const;
                Interlace getInterlace(Interlace defaultValue = Interlace::UNKNOWN) const;
                PixelFormat getFormat(PixelFormat defaultValue = PixelFormat::UNKNOWN) const;

                void setHeight(int value);
                void setWidth(int value);
                void setFrameRate(const Fraction &value);
                void setInterlace(Interlace interlace);
                void setFormat(PixelFormat format);

                // Audio generic parameters
                int getRate(int defaultValue = 0) const;
                int getChannels(int defaultValue = 0) const;

                void setRate(int value);
                void setChannels(int value);

                void clear();

                static CapsPtr fromString(const std::string &str);
                static CapsPtr create();

            private:
                mutable std::string _name;
        };
    }
}

#endif /* __UTPL_PIPELINE_CAPS_H__ */