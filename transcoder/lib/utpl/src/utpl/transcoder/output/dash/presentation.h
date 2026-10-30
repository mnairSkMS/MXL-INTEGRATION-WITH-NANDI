#ifndef __UTPL_TRANSCODER_OUTPUT_DASH_PRESENTATION_H__
#define __UTPL_TRANSCODER_OUTPUT_DASH_PRESENTATION_H__

#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "utpl/hls/streamer/uploader.h"
#include "utpl/hls/types.h"

#include "utpl/transcoder/output/parameters.h"

namespace utpl {
    namespace transcoder {
        namespace dash {
            /**
             * Options applied to every manifest the presentation publishes. Durations are in
             * milliseconds and already resolved (no 0 = default values left).
             */
            struct ManifestOptions {
                bool            live              = true;
                utpl::Timestamp segmentDuration   = 2000;
                int             windowSegments    = 0;
                utpl::Timestamp presentationDelay = 0;
            };

            /**
             * One dashsink2 instance's latest manifest and the prefix its objects are published under.
             */
            struct VariantManifest {
                std::string prefix;
                std::string xml;
            };

            /**
             * Object (and Representation id) name as published: dashsink2's file name, prefixed
             * by the variant so several dashsink2 instances can share one directory.
             */
            std::string objectName(const std::string &prefix, const std::string &location);

            /**
             * Builds the published MPD from the manifests of every dashsink2 instance of an
             * output: Representations are renamed with their variant prefix and merged into one
             * AdaptationSet per content type; the first variant's availabilityStartTime is kept
             * (pinnedStartTime, updated when empty); timeShiftBufferDepth,
             * suggestedPresentationDelay and the SCTE-35 EventStream are added.
             *
             * Returns an empty string when no variant has produced a parseable manifest.
             */
            std::string mergeManifests(
                const std::vector<VariantManifest> &variants,
                const ManifestOptions              &options,
                const std::vector<hls::ScteEvent>  &scteEvents,
                std::string                        &pinnedStartTime
            );

            /**
             * State shared by the DashOutputSinks of one output (a single output, or every
             * member of a GROUP output): uploads dashsink2's objects, keeps the live window on
             * the origin, and publishes one merged MPD.
             */
            class Presentation {
                public:
                    enum class ObjectKind {
                        INIT,
                        SEGMENT,
                        MANIFEST
                    };

                public:
                    Presentation(const std::string &uri, const DashOutputTransportParameters &parameters);
                    ~Presentation();

                    // Registers a dashsink2 instance; the returned index is passed to putObject.
                    int addVariant(const std::string &prefix);

                    // Reference counted across the variants: uploaders run while any sink plays.
                    void start();
                    void stop();

                    void putObject(int variant, ObjectKind kind, const std::string &location, std::string &&data);

                    void putScteData(const Scte35Data &data);

                    const DashOutputTransportParameters &getParameters() const {
                        return this->_parameters;
                    }

                    const std::string &getManifestName() const {
                        return this->_manifestName;
                    }

                private:
                    void _upload(const std::string &path, std::string &&data, const std::string &mimeType);
                    void _remove(const std::string &path);
                    void _publishManifest();

                private:
                    DashOutputTransportParameters _parameters;
                    ManifestOptions               _options;
                    std::string                   _manifestName;

                    std::vector<std::unique_ptr<hls::Uploader>> _uploaders;

                    std::mutex _mutex;
                    int        _started;

                    std::vector<VariantManifest>                 _variants;
                    std::vector<bool>                            _variantReady;
                    std::string                                  _pinnedStartTime;
                    std::string                                  _lastPublished;
                    std::map<std::string, std::deque<std::string>> _segments;
                    uint64_t                                     _lastSegmentNumber;
                    std::vector<hls::ScteEvent>                  _scteEvents;
            };
        }
    }
}

#endif /* __UTPL_TRANSCODER_OUTPUT_DASH_PRESENTATION_H__ */
