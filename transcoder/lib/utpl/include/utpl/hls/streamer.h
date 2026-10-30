#ifndef __UTPL_HLS_STREAMER_H__
#define __UTPL_HLS_STREAMER_H__

#include <memory>
#include <vector>
#include <map>

#include "utpl/common/time.h"
#include "utpl/common/types.h"

#include "utpl/hls/segment.h"
#include "utpl/hls/segment/storage/factory.h"

#include "utpl/hls/playlist.h"
#include "utpl/hls/types.h"
#include "utpl/hls/streamer/uploader.h"

#include "utpl/transcoder/types.h"


namespace utpl {
    namespace hls {
        class Streamer {
            public:
                static const size_t SEGMENTS_COUNT_UNLIMITED = -1;

            public:
                class Parameters {
                    public:
                        Parameters() {
                            this->_numberOfSegments = SEGMENTS_COUNT_UNLIMITED;
                            this->_uploadTimeout    = 1000;
                            this->_singleDir        = true;
                            this->_retries          = Uploader::RETRY_NONE;
                        }

                        Parameters &setNumberOfSegments(size_t num) {
                            this->_numberOfSegments = num;
                            return *this;
                        }

                        Parameters &addUploader(std::unique_ptr<Uploader> uploader) {
                            this->_uploaders.push_back(std::move(uploader));
                            return *this;
                        }

                        Parameters &setStorageFactory(std::shared_ptr<SegmentStorageFactory> factory) {
                            this->_storageFactory = factory;
                            return *this;
                        }

                        Parameters &setUploadTimeout(utpl::Timestamp timeout) {
                            this->_uploadTimeout = timeout;
                            return *this;
                        }

                        Parameters &setSingleDir(bool single) {
                            this->_singleDir = single;
                            return *this;
                        }

                        Parameters &setRetriesCount(size_t retries) {
                            this->_retries = retries;
                            return *this;
                        }

                        Parameters &setRetryInterval(utpl::Timestamp interval) {
                            this->_retryInterval = interval;
                            return *this;
                        }

                        Parameters &setScteMarkers(const std::vector<ScteMarker> &markers) {
                            this->_scteMarkers = markers;
                            return *this;
                        }

                        Parameters &setBaseDirName(const std::string &baseDirName) {
                            this->_baseDirName = baseDirName;
                            return *this;
                        }
                        
                        size_t getNumberOfSegments() const {
                            return this->_numberOfSegments;
                        }

                        std::shared_ptr<SegmentStorageFactory> getStorageFactory() const {
                            return this->_storageFactory;
                        }

                        const std::vector<std::shared_ptr<Uploader>> &getUploaders() const {
                            return this->_uploaders;
                        }

                        utpl::Timestamp getUploadTimeout() const {
                            return this->_uploadTimeout;
                        }

                        bool getSingleDir() const {
                            return this->_singleDir;
                        }

                        size_t getRetriesCount() const {
                            return this->_retries;
                        }
                        
                        utpl::Timestamp getRetryInterval() const {
                            return this->_retryInterval;
                        }

                        const std::vector<ScteMarker> &getScteMarkers() const {
                            return this->_scteMarkers;
                        }

                        std::string getBaseDirName() const {
                            return this->_baseDirName;
                        }

                    private:
                        std::vector<std::shared_ptr<Uploader>> _uploaders;
                        std::shared_ptr<SegmentStorageFactory> _storageFactory;
                        size_t                                 _numberOfSegments;
                        utpl::Timestamp                        _uploadTimeout;
                        bool                                   _singleDir;
                        size_t                                 _retries;
                        utpl::Timestamp                        _retryInterval;
                        std::vector<ScteMarker>                _scteMarkers;
                        std::string                            _baseDirName;
                };

            public:
                Streamer();

                const Parameters &getParameters() const;

                void setParameters(const Parameters &parameters);
                
                void setPlaylist(std::shared_ptr<Playlist> playlist);
                std::shared_ptr<Playlist> getPlaylist();

                void start();
                void stop();

                std::shared_ptr<Segment> createSegment(std::shared_ptr<MediaPlaylist> playlist);
                std::shared_ptr<Segment> createInitialSegment(std::shared_ptr<MediaPlaylist> playlist);

                void putScteData(const utpl::transcoder::Scte35Data &data);

            private:
                class SegmentImpl;

                struct PlaylistContext {
                    std::shared_ptr<SegmentImpl>              initialSegment;

                    std::vector<std::shared_ptr<SegmentImpl>> retainedSegments;
                    std::vector<std::shared_ptr<SegmentImpl>> playlistSegments;  
                    std::shared_ptr<MediaPlaylist> playlist;

                    int lastSegmentId;

                    std::shared_ptr<SegmentImpl> getSegmentByPtr(SegmentImpl *ptr);

                    PlaylistContext() : PlaylistContext(nullptr) {
                    }

                    PlaylistContext(std::shared_ptr<MediaPlaylist> playlist) {
                        this->lastSegmentId = 0;
                        this->playlist      = playlist;
                    }
                };

                void onSegmentBuffering(SegmentImpl *segment);
                void onSegmentBuffered(SegmentImpl *segment);
                void onSegmentUploaded(std::shared_ptr<SegmentImpl> segment, bool success);

                void refreshSegmentLists(PlaylistContext &ctx, bool complete);

                std::shared_ptr<Segment> addSegment(std::shared_ptr<MediaPlaylist> playlist, std::shared_ptr<SegmentImpl> segment, bool isInit);

                std::string genSegmentName(std::shared_ptr<MediaPlaylist> playlist, int seq, bool isInit);

                std::string getMediaPlaylistPath(const std::string &playlistName);
                std::string getSegmentPath(std::shared_ptr<MediaPlaylist> playlist, std::shared_ptr<SegmentImpl> &segment);
                std::string getSegmentPath(std::shared_ptr<MediaPlaylist> playlist, const std::string &segmentName);

                void uploadPlaylists(PlaylistContext &ctx, bool complete);

            private:
                Parameters _parameters;
                bool       _started;

                std::shared_ptr<SegmentStorageFactory> _storageFactory;

                std::shared_ptr<Playlist> _playlist;
                bool _masterUploaded;
                std::vector<ScteEvent> _scteEvents;        
                std::recursive_mutex _mutex;
                std::map<std::shared_ptr<MediaPlaylist>, PlaylistContext> _playlistsContext;
                std::map<SegmentImpl *, PlaylistContext *> _segmentToPlaylistContextMap;
        
                // Number of segments in streaming window
                size_t _windowSegments;
                // Number of segments kept in storage
                size_t _keepSegments;
        
        };
    }
}

#endif /* __UTPL_HLS_STREAMER_H__ */
