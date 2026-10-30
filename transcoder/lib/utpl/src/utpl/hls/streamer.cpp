#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>

#include <tsduck/tsduck.h>
#include <tsduck/tsSpliceSegmentationDescriptor.h>

#include "utpl/common/debug.h"

#include "utpl/hls/streamer.h"

using namespace utpl;

#define M3U_MIMETYPE "application/vnd.apple.mpegurl"

enum class SegmentState {
    EMPTY,
    BUFFERING,
    BUFFERED
};

class StringStorage : public hls::SegmentStorage {

    public:
        StringStorage(std::string &&str, const std::string &mimeType) : _str(std::move(str)), _mimeType(mimeType) {
        }

        size_t read(void *buffer, off_t offset, size_t size) override {
            if (offset < this->_str.size()) {
                size_t toRead = size;

                toRead = std::min(toRead, this->_str.size() - offset);

                memcpy(buffer, this->_str.data() + offset, toRead);

                return toRead;
            }

            return 0;
        }

        size_t write(void *buffer, size_t size) override {
            ERR(("Not supported"));

            return 0;
        }

        size_t length() override {
            return this->_str.length();
        }

        void dispose() override {
        }

        std::string getMimeType() override {
            return this->_mimeType;
        }

    private:
        std::string _str;
        std::string _mimeType;
};

class hls::Streamer::SegmentImpl : public hls::Segment {
    public:
        SegmentImpl(Streamer *streamer, utpl::Container container, const std::string &name, int seq) : _name(name) {
            this->_streamer  = streamer;
            this->_seq       = seq;
            this->_state     = SegmentState::EMPTY;
            this->_container = container;
            this->_tsBegin   = GST_CLOCK_TIME_NONE;
            this->_tsEnd     = GST_CLOCK_TIME_NONE;
            this->_duration  = 0;
            this->_programDateTime = setProgramDateTime();
        }

        ~SegmentImpl() {
            this->setEmpty();
        }

        void setBeginRunningTime(GstClockTime time) override {
            this->_tsBegin = time;
        }

        GstClockTime getBeginRunningTime() const override {
            return this->_tsBegin;
        }

        void setEndRunningTime(GstClockTime time) override {
            this->_tsEnd = time;
        }

        GstClockTime getEndRunningTime() override {
            return this->_tsEnd;
        }

        void setDuration(Timestamp duration) override {
            this->_duration = duration;
        }

        utpl::Timestamp getDuration() const override {
            if (this->_tsBegin == GST_CLOCK_TIME_NONE || this->_tsEnd == GST_CLOCK_TIME_NONE) {
                return this->_duration;
            }

            return GST_TIME_AS_MSECONDS(this->_tsEnd - this->_tsBegin);
        }

        const std::string &getProgramDateTime() {
            return this->_programDateTime;
        }

        std::string setProgramDateTime() {
            auto now = std::chrono::system_clock::now();
            auto time_t_now = std::chrono::system_clock::to_time_t(now);
            std::tm* tm_utc = std::gmtime(&time_t_now);

            std::ostringstream oss;
            oss << std::put_time(tm_utc, "%FT%TZ");

            return oss.str();
        }

        uint64_t getBitrate() const override {
            uint64_t ret = 0;

            if (this->_storage && this->getDuration()) {
                ret  = this->_storage->length();
                ret *= 1000;
                ret /= this->getDuration();
            }

            return ret;
        }

        bool isEmpty() const override {
            return this->_state == SegmentState::EMPTY;
        }

        bool isBuffered() const override {
            return this->_state == SegmentState::BUFFERED;
        }

        bool isBuffering() const override {
            return this->_state == SegmentState::BUFFERING;
        }

        void setEmpty() override {
            this->setState(SegmentState::EMPTY);
        }

        void setBuffering() override {
            this->setState(SegmentState::BUFFERING);
        }

        void setBuffered() override {
            this->setState(SegmentState::BUFFERED);
        }

        std::shared_ptr<SegmentStorage> getStorage() override {
            return this->_storage;
        }

        const std::string &getName() const override {
            return this->_name;
        }

        int getSequence() const override {
            return this->_seq;
        }

        SegmentState getState() const {
            return this->_state;
        }

        void setState(SegmentState state) {
            if (this->_state != state) {
                switch (state) {
                    case SegmentState::EMPTY:
                        if (this->_storage) {
                            this->_storage->dispose();
                            this->_storage.reset();
                        }
                        break;
        
                    case SegmentState::BUFFERING:
                    case SegmentState::BUFFERED:
                        {
                            auto f = this->_streamer->_parameters.getStorageFactory();

                            if (! this->_storage && f) {
                                std::string mimeType;

                                switch (this->_container) {
                                    case Container::MPEGTS: mimeType = "video/MP2T"; break;
                                    case Container::FMP4:   mimeType = "video/mp4";  break;
                                    default:
                                        break;
                                }

                                this->_storage = f->create(this->_name, mimeType);
                            }
        
                            if (state == SegmentState::BUFFERED) {
                                DBG(("Segment changed state to BUFFERED, data length: {}", this->_storage->length()));

                                this->_streamer->onSegmentBuffered(this);

                            } else {
                                this->_streamer->onSegmentBuffering(this);
                            }
                        }
                        break;
                }
        
                this->_state = state;
            }
        }

    private:    
        SegmentState                    _state;
        Streamer                       *_streamer;
        std::shared_ptr<SegmentStorage> _storage;
        std::string                     _name;
        int                             _seq;
        utpl::Container                 _container;
        GstClockTime                    _tsBegin;
        GstClockTime                    _tsEnd;
        Timestamp                       _duration;
        std::string                     _programDateTime;
};

std::shared_ptr<hls::Streamer::SegmentImpl> hls::Streamer::PlaylistContext::getSegmentByPtr(SegmentImpl *ptr) {
    if (ptr == this->initialSegment.get()) {
        return this->initialSegment;

    } else {
        auto it = std::find_if(this->retainedSegments.begin(), this->retainedSegments.end(), [ptr](const std::shared_ptr<SegmentImpl> &e) {
            return e.get() == ptr;
        });

        if (it != this->retainedSegments.end()) {
            return *it;
        }
    }

    return nullptr;
}

hls::Streamer::Streamer() {
    this->_keepSegments   = SEGMENTS_COUNT_UNLIMITED;
    this->_windowSegments = SEGMENTS_COUNT_UNLIMITED;
    this->_started        = false;
}

const hls::Streamer::Parameters &hls::Streamer::getParameters() const {
    return this->_parameters;
}

void hls::Streamer::setParameters(const Parameters &parameters) {
    this->_parameters = parameters;

    if (! this->_parameters.getSingleDir()) {
        for (auto &uploader : this->_parameters.getUploaders()) {
            if (! uploader->getCapabilities().canMkdir()) {
                WARN(("Forcing single directory upload as the uploader does not support the mkdir operation"));

                this->_parameters.setSingleDir(true);
                break;
            }
        }
    }

    {
        this->_windowSegments = parameters.getNumberOfSegments();
        if (this->_windowSegments > 0) {
            this->_keepSegments = this->_windowSegments * 2 + 1;

        } else {
            this->_keepSegments = SEGMENTS_COUNT_UNLIMITED;
        }
    }
}

void hls::Streamer::setPlaylist(std::shared_ptr<Playlist> playlist) {
    this->_playlist = playlist;

    const auto &master = std::dynamic_pointer_cast<MasterPlaylist>(this->_playlist);
    if (master) {
        for (const auto &media : master->getMediaPlaylists()) {
            auto p = std::reinterpret_pointer_cast<MediaPlaylist>(media);

            this->_playlistsContext[p].playlist = p;
        }

    } else {
        const auto &media = std::reinterpret_pointer_cast<MediaPlaylist>(this->_playlist);

        this->_playlistsContext[media].playlist = media;
    }
}

std::shared_ptr<hls::Playlist> hls::Streamer::getPlaylist() {
    return this->_playlist;
}

void hls::Streamer::start() {
    std::lock_guard guard(this->_mutex);

    TRACE(("CALL"));

    if (! this->_started) {
        this->_masterUploaded = false;

        for (auto &uploader : this->_parameters.getUploaders()) {
            uploader->start();
        }

        this->_started = true;
    }
}

void hls::Streamer::stop() {
    std::lock_guard guard(this->_mutex);

    TRACE(("CALL"));

    if (this->_started) {
        for (auto &uploader : this->_parameters.getUploaders()) {
            uploader->stop();
        }

        this->_started = false;
    }
}

std::shared_ptr<hls::Segment> hls::Streamer::createSegment(std::shared_ptr<MediaPlaylist> playlist) {
    bool isInit = false;

    {
        auto &ctx = this->_playlistsContext[playlist];

        auto seq = ctx.lastSegmentId++;

        return this->addSegment(
            playlist, std::make_shared<SegmentImpl>(this, playlist->getContainer(), genSegmentName(playlist, seq, isInit), seq), isInit
        );
    }
}

std::shared_ptr<hls::Segment> hls::Streamer::createInitialSegment(std::shared_ptr<MediaPlaylist> playlist) {
    bool isInit = true;

    return this->addSegment(
        playlist, std::make_shared<SegmentImpl>(this, playlist->getContainer(), genSegmentName(playlist, -1, isInit), -1), isInit
    );
}

void hls::Streamer::putScteData(const utpl::transcoder::Scte35Data &data) {
    if (this->_parameters.getScteMarkers().empty()) {
        DBG(("Ignoring incomming SCTE event; playlist decration is disabled"));

    } else {
        std::lock_guard lock(this->_mutex);

        if (! data.isSpliceInsert() && ! data.isTimeSignal()) {
            WARN(("Ignoring incoming SCTE event; only splice_insert and time_signal events are currently supported."));

        // Without a splice time the event cannot be placed against any segment boundary, so
        // every downstream comparison in MediaPlaylist::serialize() silently fails to match
        // and the avail is dropped without a trace. Reject it here, loudly, instead.
        //
        // getSpliceTime() reads a different key per command type - "splice-time" for
        // time_signal, "event-<id>-splice-time" for splice_insert - so one command type can
        // resolve while the other does not.
        } else if (data.getSpliceTime() == GST_CLOCK_TIME_NONE) {
            WARN((
                "Ignoring incoming SCTE {} event: no splice time in its running-time map, "
                "so it cannot be placed on the playlist timeline.",
                data.isTimeSignal() ? "time_signal" : "splice_insert"
            ));

        } else {
            std::vector<PlaylistContext *> playlistsToApply;

            ScteEvent newEvent(data);

            for (auto &pc : this->_playlistsContext) {
                auto &segments = pc.second.playlistSegments;

                if (segments.empty()) {
                    // start time is unknown - keep the event for future
                    playlistsToApply.push_back(&pc.second);

                } else {
                    auto windowStartTs = segments.begin()->get()->getBeginRunningTime();

                    // A duration of GST_CLOCK_TIME_NONE would overflow the sum and make this
                    // comparison meaningless, so fall back to the start alone.
                    auto eventEndTs = (newEvent.getDuration() != GST_CLOCK_TIME_NONE)
                        ? newEvent.getStart() + newEvent.getDuration()
                        : newEvent.getStart();

                    if (
                        (newEvent.getStart() >= windowStartTs) ||
                        (eventEndTs          >= windowStartTs)
                    ) {
                        playlistsToApply.push_back(&pc.second);
                    }
                }
            }

            if (playlistsToApply.empty()) {
                WARN(("Ignoring splice insert event due to scheduled in the past"));

            } else {
                this->_scteEvents.push_back(std::move(newEvent));

                for (auto *p : playlistsToApply) {
                    // TODO deterimne completion flag
                    this->uploadPlaylists(*p, false);
                }
            }
        }
    }
}

std::string hls::Streamer::genSegmentName(std::shared_ptr<MediaPlaylist> playlist, int seq, bool isInit) {
    std::string ret;

    std::string extension;

    switch (playlist->getContainer()) {
        case Container::FMP4:
            {
                if (playlist->hasAudio()) {
                    if (playlist->hasVideo()) {
                        extension = ".mp4";

                    } else {
                        extension = ".mp4a";
                    }

                } else if (playlist->hasVideo()) {
                    extension = ".mp4v";

                } else {
                    extension = ".mp4";
                }
            }
            break;

        case Container::MPEGTS:
            {
                extension = ".ts";
            }
            break;

        default:
            {
                extension = ".bin";
            }
            break;
    }

    if (isInit) {
        ret = "init";

    } else {
        ret = "section_" + std::to_string(seq);
    }

    if (this->_parameters.getSingleDir()) {
        ret = std::filesystem::path(playlist->getName()).stem().string() + "_" + ret;
    }

    return ret + extension;
}

void hls::Streamer::uploadPlaylists(PlaylistContext &ctx, bool complete) {
    std::string serializedMediaPlaylist;

    if (! ctx.playlistSegments.empty()) {
        std::unique_lock lock(this->_mutex);

        auto startTs = ctx.playlistSegments.begin()->get()->getBeginRunningTime();

        // Remove expired events
        this->_scteEvents.erase(std::remove_if(this->_scteEvents.begin(), this->_scteEvents.end(), [startTs, this](const ScteEvent &event) {
            if (event.isOut()) {
                if (event.getDuration() != GST_CLOCK_TIME_NONE) {
                    if (startTs > event.getStart() + event.getDuration()) {
                        DBG(("Removing SCTE event beacause it get expired, {} events remains", this->_scteEvents.size() - 1));

                        return true;
                    }
                }
            }

            return false;
        }), this->_scteEvents.end());

        // Remove expired events that ended earlier than the scheduled time.
        {
            std::set<const GstMpegtsSection *> toRemoveOutEvents;
            
            this->_scteEvents.erase(std::remove_if(this->_scteEvents.begin(), this->_scteEvents.end(), [startTs, this, &toRemoveOutEvents](const ScteEvent &event) {
                bool ret = false;

                if (event.isIn()) {
                    auto endTs = event.getStart();
                    
                    if (event.getDuration() != GST_CLOCK_TIME_NONE) {
                        endTs += event.getDuration();
                    }

                    {
                        auto *inSit = gst_mpegts_section_get_scte_sit((GstMpegtsSection *) event.getSection());

                        for (auto &e : this->_scteEvents) {
                            if (e.isOut() && event.getStart() > e.getStart() && e.getSection()) {
                                auto *outSit = gst_mpegts_section_get_scte_sit((GstMpegtsSection *) e.getSection());

                                if (outSit) {
                                    if (inSit->splice_command_type == outSit->splice_command_type) {
                                        switch (inSit->splice_command_type) {
                                            case GST_MTS_SCTE_SPLICE_COMMAND_INSERT:
                                                {
                                                    GstMpegtsSCTESpliceEvent *inSplice  = (GstMpegtsSCTESpliceEvent *) g_ptr_array_index(inSit->splices, 0);
                                                    GstMpegtsSCTESpliceEvent *outSplice = (GstMpegtsSCTESpliceEvent *) g_ptr_array_index(outSit->splices, 0);

                                                    if (inSplice->splice_event_id == outSplice->splice_event_id) {
                                                        if (endTs < startTs) {
                                                            toRemoveOutEvents.insert(e.getSection());

                                                            ret = true;
                                                        }
                                                    }
                                                }
                                                break;

                                            case GST_MTS_SCTE_SPLICE_COMMAND_TIME:
                                                if (inSit->descriptors->len > 0 && inSit->descriptors->len == outSit->descriptors->len) {
                                                    ts::DuckContext duckCtx;

                                                    {
                                                        auto *inDescriptorData  = reinterpret_cast<GstMpegtsDescriptor *>(g_ptr_array_index(inSit->descriptors, 0));
                                                        auto *outDescriptorData = reinterpret_cast<GstMpegtsDescriptor *>(g_ptr_array_index(outSit->descriptors, 0));

                                                        if (
                                                            (inDescriptorData->tag  == ts::DID_SPLICE_SEGMENT) &&
                                                            (outDescriptorData->tag == ts::DID_SPLICE_SEGMENT) 
                                                        ) {
                                                            ts::Descriptor inDescriptor(inDescriptorData->data, inDescriptorData->length + 2);
                                                            ts::Descriptor outDescriptor(outDescriptorData->data, outDescriptorData->length + 2);

                                                            if (inDescriptor.isValid() && outDescriptor.isValid()) {
                                                                ts::SpliceSegmentationDescriptor inSegmentationDescriptor(duckCtx, inDescriptor);
                                                                ts::SpliceSegmentationDescriptor outSegmentationDescriptor(duckCtx, outDescriptor);

                                                                if (inSegmentationDescriptor.segmentation_event_id == outSegmentationDescriptor.segmentation_event_id) {
                                                                    if (endTs < startTs) {
                                                                        toRemoveOutEvents.insert(e.getSection());
            
                                                                        ret = true;
                                                                    }           
                                                                }
                                                            }
                                                        }
                                                    }
                                                }
                                                break;

                                            default:
                                                ret = true;
                                                break;
                                        }
                                    }
                                }
                            }
                        }
                        
                        if (! ret) {
                            if (endTs < startTs) {
                                ret = true;
                            }
                        }
                    }
                }

                return ret;
            }), this->_scteEvents.end());

            this->_scteEvents.erase(std::remove_if(this->_scteEvents.begin(), this->_scteEvents.end(), [startTs, this, &toRemoveOutEvents](const ScteEvent &event) {
                if (toRemoveOutEvents.find(event.getSection()) != toRemoveOutEvents.end()) {
                    return true;
                }

                return false;

            }), this->_scteEvents.end());
        }
    }

    {
        std::vector<std::shared_ptr<Segment>> segments(ctx.playlistSegments.begin(), ctx.playlistSegments.end());

        std::string playlistBase = this->getMediaPlaylistPath(std::string());

        {
            std::unique_lock lock(this->_mutex);
            
            serializedMediaPlaylist = ctx.playlist->serialize(
                getSegmentPath(ctx.playlist, std::string()).substr(playlistBase.length()),
                complete, 
                std::reinterpret_pointer_cast<Segment>(ctx.initialSegment), 
                segments,
                this->_scteEvents,
                this->_parameters.getScteMarkers()
            );
        }
    }

    DBG(("Playlist changed creating playlist file with {} segments, initial segment: {}", ctx.playlistSegments.size(), ctx.initialSegment.get() != nullptr));

    for (auto &uploader : this->_parameters.getUploaders()) {
        uploader->dispatchUpload(
            std::make_shared<StringStorage>(std::string(serializedMediaPlaylist), M3U_MIMETYPE), 
            this->getMediaPlaylistPath(ctx.playlist->getName()), 
            this->_parameters.getUploadTimeout(),
            this->_parameters.getRetriesCount(), 
            this->_parameters.getRetryInterval()
        );
    }

    // Generate/upload master playlist
    if (! this->_masterUploaded) {
        auto master = std::dynamic_pointer_cast<MasterPlaylist>(this->_playlist);
        if (! master) {
            this->_masterUploaded = true;

        } else {
            bool allVariantsHaveBitrate = true;

            for (auto &variant : master->getVariants()) {
                auto playlist = master->getMediaPlaylist(variant.getPlaylist());
                if (playlist) {
                    auto ctxIt = this->_playlistsContext.find(playlist);
                    if (ctxIt != this->_playlistsContext.end()) {

                        if (variant.getBandwidth() == 0) {
                            uint64_t bitrate = 0;
                            uint64_t divider = 0;

                            // The first fMP4 chunk is an init chunk and has a duration of 0.
                            for (auto &s : ctxIt->second.playlistSegments) {
                                if (s->getBitrate() > 0) {
                                    bitrate += (s->getBitrate() * 8);
                                    divider++;
                                }
                            }

                            if (divider) {
                                bitrate /= divider;
                            }

                            if (bitrate) {
                                variant.setBandwidth(bitrate);

                            } else {
                                // Wait for maximum 3 chunks
                                if (ctxIt->second.playlistSegments.size() < 3) {
                                    allVariantsHaveBitrate = false;
                                }
                            }
                        }
                    }
                }
                
            }

            if (allVariantsHaveBitrate) {
                for (auto &uploader : this->_parameters.getUploaders()) {
                    uploader->dispatchUpload(
                        std::make_shared<StringStorage>(std::move(master->serialize(getMediaPlaylistPath(std::string()))), M3U_MIMETYPE), 
                        master->getName(), 
                        this->_parameters.getUploadTimeout(),
                        Uploader::RETRY_INFINITE,
                        this->_parameters.getRetryInterval()
                    );
                }

                this->_masterUploaded = true;
            }
        }
    }
}

void hls::Streamer::refreshSegmentLists(PlaylistContext &ctx, bool complete) {
    std::vector<std::shared_ptr<SegmentImpl>> newPlaylist;

    // Remove old segments
    if (this->_keepSegments > 0) {
        size_t activeSegmentsCount = 0;

        for (auto &seg : ctx.retainedSegments) {
            if (! seg->isEmpty()) {
                activeSegmentsCount++;
            }
        }

        {
            auto it = ctx.retainedSegments.begin();

            while (it != ctx.retainedSegments.end() && activeSegmentsCount > this->_keepSegments) {
                auto playlist = this->_segmentToPlaylistContextMap.find(it->get());

                if (playlist != this->_segmentToPlaylistContextMap.end()) {
                    for (auto &uploader : this->_parameters.getUploaders()) {
                        auto segmentPtr = playlist->second->getSegmentByPtr(playlist->first);

                        uploader->dispatchRemoval(
                            this->getSegmentPath(playlist->second->playlist, segmentPtr), 
                            this->_parameters.getUploadTimeout(), 
                            this->_parameters.getRetriesCount(),
                            this->_parameters.getRetryInterval()
                        );
                    }

                    this->_segmentToPlaylistContextMap.erase(playlist);
                }

                it = ctx.retainedSegments.erase(it);

                activeSegmentsCount--;
            }
        }
    }

    {
        auto it = ctx.retainedSegments.rbegin();

        while (it != ctx.retainedSegments.rend()) {
            auto &seg = *it;

            if (seg->isBuffered()) {
                newPlaylist.push_back(seg);
            }

            ++it;

            if (this->_windowSegments > 0 && newPlaylist.size() >= this->_windowSegments) {
                break;
            }
        }

        std::reverse(newPlaylist.begin(), newPlaylist.end());
    }

    if (ctx.playlistSegments != newPlaylist) {
        ctx.playlistSegments = newPlaylist;

        this->uploadPlaylists(ctx, complete);
    }

    DBG(("Playlist size: {}, retained segments: {} of max {}, segment to playlist map size {}", 
        ctx.playlistSegments.size(), ctx.retainedSegments.size(), this->_keepSegments, this->_segmentToPlaylistContextMap.size()
    ));
}

std::shared_ptr<hls::Segment> hls::Streamer::addSegment(std::shared_ptr<MediaPlaylist> playlist, std::shared_ptr<SegmentImpl> segment, bool isInit) {
    auto &ctx = this->_playlistsContext[playlist];

    if (isInit) {
        bool changed = false;

        if (ctx.initialSegment) {
            if (segment) {
                changed = ctx.initialSegment->getState() != segment->getState();

            } else {
                changed = true;
            }

        } if (segment) {
            changed = true;
        }

        if (changed) {
            if (ctx.initialSegment) {
                this->_segmentToPlaylistContextMap.erase(ctx.initialSegment.get());
            }

            ctx.initialSegment = segment;

            this->_segmentToPlaylistContextMap[ctx.initialSegment.get()] = &ctx;
        }

    } else {
        ctx.retainedSegments.push_back(segment);

        this->_segmentToPlaylistContextMap[segment.get()] = &ctx;
        
        // TODO: complete
        this->refreshSegmentLists(ctx, false);
    }

    return segment;
}

void hls::Streamer::onSegmentBuffering(SegmentImpl *segment) {
    DBG(("Segment {} has started buffering", segment->getName()));
}

void hls::Streamer::onSegmentBuffered(SegmentImpl *segment) {
    std::lock_guard lock(this->_mutex);

    DBG(("Segment {} get has been buffered size: {}B, duration: {}ms, bitrate: {}bps", segment->getName(), segment->getStorage()->length(), segment->getDuration(), segment->getBitrate()));

    {
        auto ctxIt = this->_segmentToPlaylistContextMap.find(segment);
        if (ctxIt != this->_segmentToPlaylistContextMap.end()) {
            auto *ctx = ctxIt->second;

            auto segmentPtr = ctx->getSegmentByPtr(segment);

            for (auto &uploader : this->_parameters.getUploaders()) {
                uploader->dispatchUpload(
                    segmentPtr->getStorage(), 
                    this->getSegmentPath(ctx->playlist, segmentPtr), 
                    this->_parameters.getUploadTimeout(), 
                    this->_parameters.getRetriesCount(),
                    this->_parameters.getRetryInterval(),
                    [this, segmentPtr](utpl::Error error) {
                        this->onSegmentUploaded(segmentPtr, error == utpl::Error::NO_ERROR);
                    }
                );
            }

            // TODO: complete
            this->refreshSegmentLists(*ctx, false);
        }
    }
}

void hls::Streamer::onSegmentUploaded(std::shared_ptr<SegmentImpl> segment, bool success) {
    DBG(("File {} uploading has finished with {}", segment->getName(), success ? "SUCCESS" : "FAILURE"));
}

std::string hls::Streamer::getMediaPlaylistPath(const std::string &playlistName) {
    std::string ret;

    if (this->_parameters.getSingleDir()) {
        ret = playlistName;

    } else {
        if (! this->_parameters.getBaseDirName().empty()) {
            ret = this->_parameters.getBaseDirName() + "_";
        }

        ret += "media/" + playlistName;
    }

    return ret;
}

std::string hls::Streamer::getSegmentPath(std::shared_ptr<MediaPlaylist> playlist, const std::string &segmentName) {
    std::string ret;

    {
        std::string playlistBaseName = std::filesystem::path(playlist->getName()).stem();

        if (this->_parameters.getSingleDir()) {
            if (! segmentName.empty()) {
                ret = segmentName;
            }

        } else {
            if (! this->_parameters.getBaseDirName().empty()) {
                ret = this->_parameters.getBaseDirName() + "_";
            }
    
            ret += "media/" + playlistBaseName + "/" + segmentName;
        }
    }

    return ret;
}

std::string hls::Streamer::getSegmentPath(std::shared_ptr<MediaPlaylist> playlist, std::shared_ptr<SegmentImpl> &segment) {
    return getSegmentPath(playlist, segment->getName());
}
