#include <functional>
#include <string>

#include <gst/gst.h>

#include "utpl/common/debug.h"
#include "utpl/common/types/exception.h"

#include "dash.h"

using namespace utpl;

// An in-memory GOutputStream that hands its bytes to a callback when it is closed. dashsink2
// writes each init segment, fragment and manifest to the stream its get-*-stream signal
// returns and closes it once the object is complete (patch 020), so close is the moment the
// object can be uploaded. GIO also closes a stream that is disposed unclosed.
G_BEGIN_DECLS

#define UTPL_TYPE_DASH_CAPTURE_STREAM (utpl_dash_capture_stream_get_type())
G_DECLARE_FINAL_TYPE(UtplDashCaptureStream, utpl_dash_capture_stream, UTPL, DASH_CAPTURE_STREAM, GOutputStream)

G_END_DECLS

struct _UtplDashCaptureStream {
    GOutputStream parent;

    std::string                         *data;
    std::function<void(std::string &&)> *onClose;
};

G_DEFINE_TYPE(UtplDashCaptureStream, utpl_dash_capture_stream, G_TYPE_OUTPUT_STREAM)

static gssize utpl_dash_capture_stream_write(GOutputStream *stream, const void *buffer, gsize count, GCancellable *, GError **) {
    auto *self = UTPL_DASH_CAPTURE_STREAM(stream);

    self->data->append((const char *) buffer, count);

    return count;
}

static gboolean utpl_dash_capture_stream_close(GOutputStream *stream, GCancellable *, GError **) {
    auto *self = UTPL_DASH_CAPTURE_STREAM(stream);

    if (self->onClose && *self->onClose) {
        (*self->onClose)(std::move(*self->data));
    }

    return TRUE;
}

static void utpl_dash_capture_stream_finalize(GObject *object) {
    auto *self = UTPL_DASH_CAPTURE_STREAM(object);

    delete self->data;
    delete self->onClose;

    G_OBJECT_CLASS(utpl_dash_capture_stream_parent_class)->finalize(object);
}

static void utpl_dash_capture_stream_class_init(UtplDashCaptureStreamClass *klass) {
    G_OBJECT_CLASS(klass)->finalize = utpl_dash_capture_stream_finalize;

    G_OUTPUT_STREAM_CLASS(klass)->write_fn = utpl_dash_capture_stream_write;
    G_OUTPUT_STREAM_CLASS(klass)->close_fn = utpl_dash_capture_stream_close;
}

static void utpl_dash_capture_stream_init(UtplDashCaptureStream *self) {
    self->data    = new std::string();
    self->onClose = new std::function<void(std::string &&)>();
}

transcoder::DashOutputSink::DashOutputSink(
    const std::vector<StreamParameters> &streams,
    const ScteOutputParameters          &scteParameters,
    std::shared_ptr<dash::Presentation>  presentation,
    const std::string                   &variantPrefix
) {
    this->_bin            = pipeline::Bin::create();
    this->_scteParameters = scteParameters;
    this->_presentation   = presentation;
    this->_variant        = presentation->addVariant(variantPrefix);
    this->_videoCodec     = Codec::H264;

    this->_supportedMediaTypes.insert(MediaType::AUDIO);
    this->_supportedMediaTypes.insert(MediaType::VIDEO);

    // The application allows one video stream per DASH output; its codec picks the parser.
    for (const auto &s : streams) {
        if (s.hasVideoParameters()) {
            this->_videoCodec = s.getCodec();
            break;
        }
    }
}

void transcoder::DashOutputSink::link() {
    const auto &parameters = this->_presentation->getParameters();

    this->_sink = this->_bin->add(pipeline::Element::make("dashsink2"));

    this->_sink->setProperty<gboolean>("dynamic", ! parameters.isVodMode());
    this->_sink->setProperty<guint>("target-duration", parameters.getSegmentDuration());
    // Timing comes from the output's clocksync (needExternalSync) or the live source.
    this->_sink->setProperty<gboolean>("sync", false);
    this->_sink->setProperty<guint>("min-buffer-time",
        parameters.getMinBufferTime() > 0 ? parameters.getMinBufferTime() : 2 * parameters.getSegmentDuration()
    );
    this->_sink->setProperty<guint>("minimum-update-period",
        parameters.getMinimumUpdatePeriod() > 0 ? parameters.getMinimumUpdatePeriod() : parameters.getSegmentDuration()
    );

    if (! parameters.getUtcTimingUrl().empty()) {
        this->_sink->setProperty<const gchar *>("utc-timing-url", parameters.getUtcTimingUrl().c_str());
    }

    this->_sink->onSignal("get-init-stream",     G_CALLBACK(onInitStream),     this);
    this->_sink->onSignal("get-segment-stream",  G_CALLBACK(onSegmentStream),  this);
    this->_sink->onSignal("get-manifest-stream", G_CALLBACK(onManifestStream), this);

    this->_sink->onPlay([this]() {
        this->_presentation->start();

        return true;
    });

    this->_sink->onNull([this]() {
        this->_presentation->stop();

        return true;
    });

    // dashsink2 names its pads (and so its files) by request order: video first, then audio.
    std::stable_sort(this->_pads.begin(), this->_pads.end(), [](const StreamPad &a, const StreamPad &b) {
        return a.mediaType == MediaType::VIDEO && b.mediaType != MediaType::VIDEO;
    });

    for (auto &pad : this->_pads) {
        std::string parserFactory;
        std::string templateName;

        if (pad.mediaType == MediaType::VIDEO) {
            parserFactory = this->_videoCodec == Codec::H265 ? "h265parse" : "h264parse";
            templateName  = "video_%u";

        } else {
            parserFactory = "aacparse";
            templateName  = "audio_%u";
        }

        auto parser = this->_bin->add(pipeline::Element::make(parserFactory));
        auto target = this->_sink->requestPad(templateName);

        if (! *target) {
            throw InvalidArgumentException("DASH output: unable to request dashsink2 pad '" + templateName + "'");
        }

        parser->getPad("src")->link(target);
        pad.pad->setTarget(parser->getPad("sink"));
    }
}

pipeline::ElementPtr transcoder::DashOutputSink::getNode() {
    return this->_bin;
}

pipeline::PadPtr transcoder::DashOutputSink::getPad(MediaType mediaType, Pid pid) {
    this->_pads.push_back(
        {mediaType, this->_bin->addPad("", nullptr, false)}
    );

    return this->_pads.back().pad;
}

void transcoder::DashOutputSink::putScteData(const Scte35Data &data) {
    if (this->_scteParameters.getMode() == ScteOutputMode::PASSTHROUGH) {
        this->_presentation->putScteData(data);
    }
}

bool transcoder::DashOutputSink::isLive() const {
    return ! this->_presentation->getParameters().isVodMode();
}

bool transcoder::DashOutputSink::needExternalSync() const {
    return true;
}

const std::set<MediaType> &transcoder::DashOutputSink::getSupportedMediaTypes() const {
    return this->_supportedMediaTypes;
}

GOutputStream *transcoder::DashOutputSink::createStream(dash::Presentation::ObjectKind kind, const gchar *location) {
    auto *stream = UTPL_DASH_CAPTURE_STREAM(g_object_new(UTPL_TYPE_DASH_CAPTURE_STREAM, nullptr));

    std::string name(location ? location : "");

    *stream->onClose = [presentation = this->_presentation, variant = this->_variant, kind, name](std::string &&data) {
        presentation->putObject(variant, kind, name, std::move(data));
    };

    return G_OUTPUT_STREAM(stream);
}

GOutputStream *transcoder::DashOutputSink::onInitStream(GstElement *, const gchar *location, DashOutputSink *self) {
    return self->createStream(dash::Presentation::ObjectKind::INIT, location);
}

GOutputStream *transcoder::DashOutputSink::onSegmentStream(GstElement *, const gchar *location, DashOutputSink *self) {
    return self->createStream(dash::Presentation::ObjectKind::SEGMENT, location);
}

GOutputStream *transcoder::DashOutputSink::onManifestStream(GstElement *, const gchar *location, DashOutputSink *self) {
    return self->createStream(dash::Presentation::ObjectKind::MANIFEST, location);
}
