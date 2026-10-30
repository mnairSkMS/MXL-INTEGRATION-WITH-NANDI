#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <sstream>

#include <pugixml.hpp>

#include "utpl/common/debug.h"
#include "utpl/common/types.h"

#include "utpl/hls/streamer/uploader/file.h"
#include "utpl/hls/streamer/uploader/http.h"

#include "presentation.h"

using namespace utpl;
using namespace utpl::transcoder;

namespace {
    const char *MPD_MIMETYPE  = "application/dash+xml";
    const char *SCTE35_SCHEME = "urn:scte:scte35:2014:xml+bin";
    const char *SCTE35_XMLNS  = "http://www.scte.org/schemas/35/2016";

    // Segments kept on the origin beyond the advertised window, for players still fetching them.
    const size_t EXTRA_KEPT_SEGMENTS = 2;

    // Publish without a lagging GROUP member once this many segments exist.
    const uint64_t MAX_SEGMENTS_WITHOUT_ALL_VARIANTS = 3;

    class BytesStorage : public hls::SegmentStorage {
        public:
            BytesStorage(std::string &&data, const std::string &mimeType) : _data(std::move(data)), _mimeType(mimeType) {
            }

            size_t read(void *buffer, off_t offset, size_t size) override {
                if (offset < 0 || (size_t) offset >= this->_data.size()) {
                    return 0;
                }

                size = std::min(size, this->_data.size() - offset);
                memcpy(buffer, this->_data.data() + offset, size);

                return size;
            }

            size_t write(void *buffer, size_t size) override {
                this->_data.append((const char *) buffer, size);

                return size;
            }

            size_t length() override {
                return this->_data.size();
            }

            std::string getMimeType() override {
                return this->_mimeType;
            }

            void dispose() override {
                this->_data.clear();
            }

        private:
            std::string _data;
            std::string _mimeType;
    };

    std::string isoDuration(utpl::Timestamp ms) {
        std::ostringstream ss;

        ss << "PT" << (ms / 1000);

        if (ms % 1000) {
            ss << "." << std::setw(3) << std::setfill('0') << (ms % 1000);
        }

        ss << "S";

        return ss.str();
    }

    std::string mimeTypeOf(const std::string &location) {
        if (location.size() >= 5 && location.compare(location.size() - 5, 5, ".cmfa") == 0) {
            return "audio/mp4";
        }

        return "video/mp4";
    }

    // "video_0_segment_12.cmfv" -> ("video_0", 12)
    bool parseSegmentName(const std::string &name, std::string &representation, uint64_t &number) {
        auto pos = name.rfind("_segment_");

        if (pos == std::string::npos) {
            return false;
        }

        representation = name.substr(0, pos);

        try {
            number = std::stoull(name.substr(pos + strlen("_segment_")));

        } catch (...) {
            return false;
        }

        return true;
    }

    pugi::xml_node findAdaptationSet(pugi::xml_node period, const char *contentType) {
        for (auto set : period.children("AdaptationSet")) {
            if (strcmp(set.attribute("contentType").value(), contentType) == 0) {
                return set;
            }
        }

        return {};
    }
}

std::string dash::objectName(const std::string &prefix, const std::string &location) {
    std::string name = std::filesystem::path(location).filename().string();

    return prefix.empty() ? name : prefix + "_" + name;
}

std::string dash::mergeManifests(
    const std::vector<VariantManifest> &variants,
    const ManifestOptions              &options,
    const std::vector<hls::ScteEvent>  &scteEvents,
    std::string                        &pinnedStartTime
) {
    pugi::xml_document out;
    pugi::xml_node     outPeriod;

    for (const auto &variant : variants) {
        pugi::xml_document doc;

        if (variant.xml.empty() || ! doc.load_string(variant.xml.c_str())) {
            continue;
        }

        auto period = doc.child("MPD").child("Period");
        if (! period) {
            continue;
        }

        if (! outPeriod) {
            out.reset(doc);
            outPeriod = out.child("MPD").child("Period");

            // Rebuilt below from every variant, this one included, with prefixed names.
            for (auto set : outPeriod.children("AdaptationSet")) {
                while (set.remove_child("Representation")) {
                }
            }
        }

        for (auto set : period.children("AdaptationSet")) {
            auto contentType = set.attribute("contentType").value();
            auto target      = findAdaptationSet(outPeriod, contentType);

            if (! target) {
                target = outPeriod.append_copy(set);

                while (target.remove_child("Representation")) {
                }
            }

            for (auto rep : set.children("Representation")) {
                auto copy = target.append_copy(rep);

                copy.attribute("id").set_value(dash::objectName(variant.prefix, rep.attribute("id").value()).c_str());

                auto segmentTemplate = copy.child("SegmentTemplate");
                for (auto name : { "media", "initialization" }) {
                    auto attr = segmentTemplate.attribute(name);

                    if (attr) {
                        attr.set_value(dash::objectName(variant.prefix, attr.value()).c_str());
                    }
                }
            }
        }
    }

    if (! outPeriod) {
        return {};
    }

    auto mpd = out.child("MPD");

    if (options.live && strcmp(mpd.attribute("type").value(), "dynamic") == 0) {
        // Each dashsink2 instance stamps its own start time when its first segment completes.
        // The media timelines share the pipeline running time, so one start time fits all of
        // them; keep the first one for the life of the output.
        auto startTime = mpd.attribute("availabilityStartTime");

        if (startTime) {
            if (pinnedStartTime.empty()) {
                pinnedStartTime = startTime.value();

            } else {
                startTime.set_value(pinnedStartTime.c_str());
            }
        }

        if (options.windowSegments > 0) {
            auto attr = mpd.attribute("timeShiftBufferDepth");
            if (! attr) {
                attr = mpd.append_attribute("timeShiftBufferDepth");
            }

            attr.set_value(isoDuration(options.segmentDuration * options.windowSegments).c_str());
        }

        if (options.presentationDelay > 0) {
            auto attr = mpd.attribute("suggestedPresentationDelay");
            if (! attr) {
                attr = mpd.append_attribute("suggestedPresentationDelay");
            }

            attr.set_value(isoDuration(options.presentationDelay).c_str());
        }
    }

    if (! scteEvents.empty()) {
        // EventStream precedes the AdaptationSets in a Period. Media time is the pipeline
        // running time, so the splice running time is the presentation time (ms timescale).
        auto stream = outPeriod.insert_child_before("EventStream", outPeriod.child("AdaptationSet"));

        stream.append_attribute("schemeIdUri").set_value(SCTE35_SCHEME);
        stream.append_attribute("timescale").set_value(1000);

        int id = 0;

        for (const auto &e : scteEvents) {
            if (e.getStart() == GST_CLOCK_TIME_NONE || e.getBase64().empty()) {
                continue;
            }

            auto event = stream.append_child("Event");

            event.append_attribute("presentationTime").set_value((unsigned long long) (e.getStart() / GST_MSECOND));

            if (e.isOut() && e.getDuration() != GST_CLOCK_TIME_NONE) {
                event.append_attribute("duration").set_value((unsigned long long) (e.getDuration() / GST_MSECOND));
            }

            event.append_attribute("id").set_value(id++);

            auto signal = event.append_child("Signal");
            signal.append_attribute("xmlns").set_value(SCTE35_XMLNS);
            signal.append_child("Binary").text().set(e.getBase64().c_str());
        }
    }

    std::ostringstream ss;
    out.save(ss, "  ", pugi::format_default, pugi::encoding_utf8);

    return ss.str();
}

dash::Presentation::Presentation(const std::string &uri, const DashOutputTransportParameters &parameters) : _parameters(parameters) {
    this->_started           = 0;
    this->_lastSegmentNumber = 0;

    this->_options.live              = ! parameters.isVodMode();
    this->_options.segmentDuration   = parameters.getSegmentDuration();
    this->_options.windowSegments    = parameters.isVodMode() ? 0 : parameters.getWindowSegments();
    this->_options.presentationDelay = parameters.getPresentationDelay() > 0 ? parameters.getPresentationDelay() : 3 * parameters.getSegmentDuration();

    this->_manifestName = (parameters.getBaseFileName().empty() ? std::string("manifest") : parameters.getBaseFileName()) + ".mpd";

    Protocol    subprotocol;
    std::string path;

    utpl::parseProtocolAndPath(uri, subprotocol, path);

    if (subprotocol == Protocol::FILE || subprotocol == Protocol::NONE) {
        std::error_code ec;
        std::filesystem::create_directories(path, ec);

        this->_uploaders.push_back(std::make_unique<hls::FileUploader>(path));

    } else {
        // dash+http://host/path -> http://host/path
        auto url = uri.substr(uri.find('+') + 1);

        auto http = std::make_unique<hls::HttpUploader>(
            url,
            parameters.getTransferType()    == DashOutputTransportParameters::TransferType::HTTP_DAV,
            parameters.getTransferFraming() == DashOutputTransportParameters::TransferFraming::CHUNKED
        );

        http->setAuthorization(parameters.getCredentials(), parameters.getAuthentication());

        this->_uploaders.push_back(std::move(http));
    }
}

dash::Presentation::~Presentation() {
    for (auto &uploader : this->_uploaders) {
        uploader->stop();
    }
}

int dash::Presentation::addVariant(const std::string &prefix) {
    std::scoped_lock lock(this->_mutex);

    this->_variants.push_back({ prefix, std::string() });
    this->_variantReady.push_back(false);

    return this->_variants.size() - 1;
}

void dash::Presentation::start() {
    std::scoped_lock lock(this->_mutex);

    if (this->_started++ == 0) {
        for (auto &uploader : this->_uploaders) {
            uploader->start();
        }
    }
}

void dash::Presentation::stop() {
    std::scoped_lock lock(this->_mutex);

    if (this->_started > 0 && --this->_started == 0) {
        for (auto &uploader : this->_uploaders) {
            uploader->stop();
        }
    }
}

void dash::Presentation::putObject(int variant, ObjectKind kind, const std::string &location, std::string &&data) {
    std::scoped_lock lock(this->_mutex);

    if (variant < 0 || variant >= (int) this->_variants.size()) {
        ERR(("DASH object '{}' for unknown variant {}", location, variant));
        return;
    }

    const auto &prefix = this->_variants[variant].prefix;

    switch (kind) {
        case ObjectKind::INIT:
            this->_upload(objectName(prefix, location), std::move(data), mimeTypeOf(location));
            break;

        case ObjectKind::SEGMENT:
            {
                auto name = objectName(prefix, location);

                this->_upload(name, std::move(data), mimeTypeOf(location));

                std::string representation;
                uint64_t    number = 0;

                if (parseSegmentName(name, representation, number)) {
                    this->_lastSegmentNumber = std::max(this->_lastSegmentNumber, number);

                    if (this->_options.windowSegments > 0) {
                        auto &kept = this->_segments[representation];

                        kept.push_back(name);

                        while (kept.size() > this->_options.windowSegments + EXTRA_KEPT_SEGMENTS) {
                            this->_remove(kept.front());
                            kept.pop_front();
                        }
                    }
                }
            }
            break;

        case ObjectKind::MANIFEST:
            {
                // dashsink2 turns its manifest static when it stops. For a live output with a
                // window that MPD would list segments already removed from the origin, so the
                // last dynamic manifest stays published instead.
                if (this->_options.live && data.find("type=\"static\"") != std::string::npos) {
                    DBG(("DASH: ignoring the final static manifest of live variant {}", variant));
                    break;
                }

                this->_variants[variant].xml = std::move(data);
                this->_variantReady[variant] = true;

                bool allReady = std::all_of(this->_variantReady.begin(), this->_variantReady.end(), [](bool ready) {
                    return ready;
                });

                if (allReady || this->_lastSegmentNumber >= MAX_SEGMENTS_WITHOUT_ALL_VARIANTS) {
                    this->_publishManifest();
                }
            }
            break;
    }
}

void dash::Presentation::putScteData(const Scte35Data &data) {
    hls::ScteEvent event(data);

    if (event.getStart() == GST_CLOCK_TIME_NONE) {
        // splice_null heartbeats arrive every second or two on most feeds; only other commands are worth a warning.
        auto       *section = (GstMpegtsSection *) data.getSection();
        const auto *sit     = section ? gst_mpegts_section_get_scte_sit(section) : nullptr;

        if (sit && sit->splice_command_type == GST_MTS_SCTE_SPLICE_COMMAND_NULL) {
            TRACE(("DASH: splice_null is not published"));
        } else {
            WARN(("DASH: SCTE-35 section without a splice time is not published: {}", toBase64(data)));
        }
        return;
    }

    std::scoped_lock lock(this->_mutex);

    // A GROUP output hands the same section to every member's sink.
    for (const auto &e : this->_scteEvents) {
        if (e.getStart() == event.getStart() && e.getBase64() == event.getBase64()) {
            return;
        }
    }

    LOG(("DASH: SCTE-35 event at {} ms added to the MPD EventStream", event.getStart() / GST_MSECOND));

    this->_scteEvents.push_back(std::move(event));

    if (this->_options.live && this->_options.windowSegments > 0) {
        // Media time is the pipeline running time; drop events that ended before the window.
        utpl::Timestamp nowMs    = (this->_lastSegmentNumber + 1) * this->_options.segmentDuration;
        utpl::Timestamp windowMs = this->_options.windowSegments * this->_options.segmentDuration;

        this->_scteEvents.erase(std::remove_if(this->_scteEvents.begin(), this->_scteEvents.end(), [&](const hls::ScteEvent &e) {
            utpl::Timestamp endMs = e.getStart() / GST_MSECOND;

            if (e.getDuration() != GST_CLOCK_TIME_NONE) {
                endMs += e.getDuration() / GST_MSECOND;
            }

            return endMs + windowMs < nowMs;
        }), this->_scteEvents.end());
    }
}

void dash::Presentation::_publishManifest() {
    auto xml = mergeManifests(this->_variants, this->_options, this->_scteEvents, this->_pinnedStartTime);

    if (xml.empty()) {
        WARN(("DASH: no manifest to publish yet"));
        return;
    }

    // Every stream of every variant reports its segments with a manifest update, so most
    // updates repeat the last one (publishTime has one-second resolution).
    if (xml == this->_lastPublished) {
        return;
    }

    this->_lastPublished = xml;

    this->_upload(this->_manifestName, std::move(xml), MPD_MIMETYPE);
}

void dash::Presentation::_upload(const std::string &path, std::string &&data, const std::string &mimeType) {
    TRACE(("DASH: uploading '{}' ({} B)", path, data.size()));

    for (auto &uploader : this->_uploaders) {
        uploader->dispatchUpload(
            std::make_shared<BytesStorage>(std::string(data), mimeType),
            path,
            this->_parameters.getTimeout(),
            this->_parameters.getRetryCount(),
            this->_parameters.getRetryInterval(),
            [path](utpl::Error error) {
                if (error != utpl::Error::NO_ERROR) {
                    WARN(("DASH: upload of '{}' failed", path));
                }
            }
        );
    }
}

void dash::Presentation::_remove(const std::string &path) {
    TRACE(("DASH: removing '{}'", path));

    for (auto &uploader : this->_uploaders) {
        uploader->dispatchRemoval(
            path,
            this->_parameters.getTimeout(),
            this->_parameters.getRetryCount(),
            this->_parameters.getRetryInterval()
        );
    }
}
