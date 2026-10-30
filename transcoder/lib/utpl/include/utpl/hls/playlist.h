#ifndef __UTPL_HLS_PLAYLIST_H__
#define __UTPL_HLS_PLAYLIST_H__

#include <set>
#include <tuple>
#include <vector>
#include <algorithm>

#include "utpl/common/types.h"
#include "utpl/common/utils/class.h"

#include "utpl/hls/types.h"
#include "utpl/hls/segment.h"

namespace utpl {
    namespace hls {
        class RenditionGroup {
            public:
                RenditionGroup(const std::string &name, utpl::MediaType mediaType) : _name(name) {
                    this->_mediaType = mediaType;
                }

                const std::string &getName() const {
                    return this->_name;
                }

                utpl::MediaType getMediaType() const {
                    return this->_mediaType;
                }

                const std::string &getDefaultPlaylist() const {
                    return this->_defaultPlaylist;
                }

                void setDefaultPlaylist(const std::string &playlistName) {
                    this->_defaultPlaylist = playlistName;
                }

            private:
                std::string     _name;
                utpl::MediaType _mediaType;
                std::string     _defaultPlaylist;
        };

        class Playlist {
            UTPL_CLASS_INTERFACE(Playlist) {}

            public:
                virtual std::string serialize(const std::string &basePath) = 0;
                virtual std::string getName() const = 0;
        };

        class MediaPlaylist : public Playlist {
            public:
                static const time_t SEGMENT_DURATION = 10;

            public:
                MediaPlaylist(const std::string &name, utpl::Container container, const std::vector<utpl::MediaType> &mediaTypes, time_t segmentDuration) : _name(name), _mediaTypes(mediaTypes) {    
                    this->_container       = container;
                    this->_segmentDuration = segmentDuration;
                    this->_live            = true;
                }

                std::string getName() const override {
                    return this->_name;
                }

                std::string getGroup() {
                    return this->_group;
                }

                void setGroup(const std::string &groupName) {
                    this->_group = groupName;
                }

                const std::vector<utpl::MediaType> &getMediaTypes() const {
                    return this->_mediaTypes;
                }

                bool hasAudio() const {
                    return std::find_if(this->_mediaTypes.begin(), this->_mediaTypes.end(), [](utpl::MediaType type) {
                        return type == utpl::MediaType::AUDIO;
                    }) != this->_mediaTypes.end();
                }

                bool hasVideo() const {
                    return std::find_if(this->_mediaTypes.begin(), this->_mediaTypes.end(), [](utpl::MediaType type) {
                        return type == utpl::MediaType::VIDEO;
                    }) != this->_mediaTypes.end();
                }

                utpl::Container getContainer() const {
                    return this->_container;
                }

                time_t getSegmentDuration() const {
                    return this->_segmentDuration;
                }

                std::string serialize(const std::string &basePath) override;

                std::string serialize(
                    const std::string &basePath, 
                    bool complete, 
                    const std::shared_ptr<Segment> &initialSegment, 
                    const std::vector<std::shared_ptr<Segment>> &segments,
                    const std::vector<ScteEvent> &scteEvents,
                    const std::vector<ScteMarker> &scteMarkers = {}
                );

            private:
                std::string _name;
                std::string _group;
                bool        _live;

                std::vector<utpl::MediaType> _mediaTypes;
                utpl::Container _container;
                time_t          _segmentDuration;

                // Cue tags already reported in the log, as (segment sequence, tag, splice time).
                // serialize() rebuilds the playlist on every refresh and re-emits each tag while
                // its segment is in the window; this keeps the log to one line per placement.
                std::set<std::tuple<int, std::string, GstClockTime>> _loggedCues;
        };

        class Variant {
            public:
                Variant() {
                    this->_bandwidth = 0;
                    this->_width     = 0;
                    this->_height    = 0;
                    this->_frameRate = 0.0;
                }

                void setPlaylist(const MediaPlaylist &playlist) {
                    this->_playlist = playlist.getName();
                }

                const std::string &getPlaylist() const {
                    return this->_playlist;
                }

                uint64_t getBandwidth() const {
                    return this->_bandwidth;
                }

                void setBandwidth(uint64_t bandwidth) {
                    this->_bandwidth = bandwidth;
                }

                // Encoded video size, written as RESOLUTION; 0 leaves the attribute out.
                int getWidth() const {
                    return this->_width;
                }

                int getHeight() const {
                    return this->_height;
                }

                void setResolution(int width, int height) {
                    this->_width  = width;
                    this->_height = height;
                }

                // Written as FRAME-RATE; 0 leaves the attribute out.
                double getFrameRate() const {
                    return this->_frameRate;
                }

                void setFrameRate(double frameRate) {
                    this->_frameRate = frameRate;
                }

            private:
                std::string              _playlist;
                std::vector<std::string> _groups;
                uint64_t                 _bandwidth;
                int                      _width;
                int                      _height;
                double                   _frameRate;
        };

        class MasterPlaylist : public Playlist {
            public:
                MasterPlaylist(const std::string &name) : _name(name) {
                }

                Variant &createVariant() {
                    this->_variants.push_back(Variant());

                    return *this->_variants.rbegin();
                }

                std::shared_ptr<MediaPlaylist> createMediaPlaylist(const std::string &name, utpl::Container container, const std::vector<utpl::MediaType> &mediaTypes, time_t segmentDuration) {
                    this->_mediaPlaylists.push_back(
                        std::make_shared<MediaPlaylist>(name, container, mediaTypes, segmentDuration)
                    );

                    return *this->_mediaPlaylists.rbegin();
                }

                std::string getName() const override {
                    return this->_name;
                }

                const std::vector<std::shared_ptr<MediaPlaylist>> &getMediaPlaylists() const {
                    return this->_mediaPlaylists;
                }

                std::vector<Variant> &getVariants() {
                    return this->_variants;
                }

                std::shared_ptr<MediaPlaylist> getMediaPlaylist(const std::string &name) {
                    auto it = std::find_if(this->_mediaPlaylists.begin(), this->_mediaPlaylists.end(), [&](const std::shared_ptr<MediaPlaylist> &playlist) {
                        return playlist->getName() == name;
                    });

                    if (it == this->_mediaPlaylists.end()) {
                        return nullptr;
                    }

                    return *it;
                }

                const MediaPlaylist *getMediaPlaylist(const std::string &name) const {
                    return getMediaPlaylist(name);
                }

                std::string serialize(const std::string &basePath) override;

            private:
                std::string _name;

                std::vector<Variant>                        _variants;
                std::vector<std::shared_ptr<MediaPlaylist>> _mediaPlaylists;
        };
    }
}

#endif /* __UTPL_HLS_PLAYLIST_H__ */
