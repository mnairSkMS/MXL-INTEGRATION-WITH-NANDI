#ifndef __UTPL_COMMON_TYPES_H__
#define __UTPL_COMMON_TYPES_H__

#include <gst/gstutils.h>
#include <gst/video/video-format.h>
#include <cmath>
#include <string>
#include <vector>

namespace utpl {
    enum class Error {
        NO_ERROR,

        UNKNOWN_ERROR,
        NOT_IMPLEMENTED,
        NOT_SUPPORTED,
        INVALID_ARGUMENT,
        INVALID_STATE,
        NO_FREE_MEMORY,
        CANCELLED,
        TIMEDOUT,

        INTERRUPTED,
        PIPELINE_ERROR,
        LICENSE_ERROR
    };

    enum class MediaType {
        UNKNOWN,

        AUDIO,
        VIDEO,
        CAPTION,
        DATA // Opaque data information usually continuous. ex SCTE-35 or raw mpegts etc
    };

    enum class Protocol {
        UNKNOWN,

        NONE,
        FILE,
        UDP,
        SRT,
        HLS,
        HTTP,
        PLAYLIST,
        S3,
        NDI,
        RTMP,
        DASH,
        MXL // Media eXchange Layer: raw flows read from a shared-memory domain (inputs only)
    };

    enum class Codec {
        PASSTHROUGH = -1,
        UNKNOWN    = 0,

        ES,  // Elementary stream (PCM, raw Video)
        MXL, // Raw media written to an MXL flow (the programme tap); never read from a file or stream
        MP1, // A/V
        MP2, // A/V
        MP3,
        AAC,
        AC3,
        EAC3,

        H264,
        H265,

        SUB_DVB, // DVB-Sub captions
        SUB_TTX, // Teletext captions
    };

    enum class Container {
        UNKNOWN,

        ES,
        MPEGTS,
        FMP4
    };

    enum class PixelFormat {
        UNKNOWN,

        // Planar
        I420,
        YV12,
        I422, // Y42B
        I444, // Y444
        // packed
        UYVY,
        YUY2,
        YVYU,
        // Semi-planar
        NV12,
        NV21,

        //
        BGRA
    };

    enum class Interlace {
        UNKNOWN,
        INTERLEAVED,
        PROGRESSIVE
    };

    enum class FieldOrder {
        TOP_FIELD_FIRST,
        BOTTOM_FIELD_FIRST
    };

    using Pid = int16_t;

    inline constexpr Pid PID_NULL = 0x1fff;

    class Fraction {
        public:
            Fraction() : Fraction(0, 1) {
            }

            Fraction(int numerator, int denominator) {
                this->_numerator   = numerator;
                this->_denominator = denominator;
            }

            int getNumerator() const {
                return this->_numerator;
            }

            int getDenominator() const {
                return this->_denominator;
            }

            operator bool() const {
                return this->_numerator != 0;
            }

            const bool operator==(const Fraction &other) const {
                if (this == &other) {
                    return true;
                }

                return
                    this->getDenominator() == other.getDenominator() &&
                    this->getNumerator()   == other.getNumerator()
                ;
            }

            const bool operator!=(const Fraction &other) const {
                return ! (*this == other);
            }

            const bool operator>(const Fraction &other) const {
                auto ret = gst_util_fraction_compare(
                    this->_numerator, this->_denominator, other._numerator, other._denominator
                );

                return ret == 1;
            }

        private:
            int _numerator;
            int _denominator;
    };

    class Point {
        public:
            Point() : Point(0, 0) {
            }

            Point(int x, int y) : _x(x), _y(y) {
            }

            int x() const {
                return this->_x;
            }

            void x(int x) {
                this->_x = x;
            }

            int y() const {
                return this->_y;
            }

            void y(int y) {
                this->_y = y;
            }

            bool operator==(const Point &other) const {
                return 
                    this->_x == other._x &&
                    this->_y == other._y;
            }

            bool operator!=(const Point &other) const {
                return ! (*this == other);
            }

        private:
            int _x, _y;
    };

    class Dimension {
        public:
            Dimension() : Dimension(0, 0) {
            }

            Dimension(int width, int height) {
                this->_width  = width;
                this->_height = height;
            }

            int width() const {
                return this->_width;
            }

            int height() const {
                return this->_height;
            }

            void width(int w) {
                this->_width = w;
            }

            void height(int h) {
                this->_height = h;
            }

            bool operator==(const Dimension &other) const {
                return 
                    this->_width  == other._width &&
                    this->_height == other._height;
            }

            bool operator!=(const Dimension &other) const {
                return ! (*this == other);
            }

        private:
            int _width;
            int _height;
    };

    class Rectangle {
        public:
            Rectangle() {
            }

            Rectangle(int x, int y, int w, int h) : _position(x, y), _dimension(w, h) {
            }

            Rectangle(const Point &pos, const Dimension &dim) : _position(pos), _dimension(dim) {
            }

            const Point &getPosition() const {
                return this->_position;
            }

            Point &getPosition() {
                return this->_position;
            }

            const Dimension &getDimension() const {
                return this->_dimension;
            }

            Dimension &getDimension() {
                return this->_dimension;
            }

            int x() const {
                return this->_position.x();
            }

            void x(int x) {
                this->_position.x(x);
            }

            int y() const {
                return this->_position.y();
            }

            void y(int y) {
                this->_position.y(y);
            }

            int width() const {
                return this->_dimension.width();
            }

            void width(int w) {
                this->_dimension.width(w);
            }

            int height() const {
                return this->_dimension.height();
            }

            void height(int h) {
                this->_dimension.height(h);
            }

            bool operator==(const Rectangle &other) const {
                return 
                    this->_dimension == other._dimension &&
                    this->_position  == other._position;
            }

            bool operator!=(const Rectangle &other) const {
                return ! (*this == other);
            }

        private:
            Point     _position;
            Dimension _dimension;
    };

    class NormRect {
        public:
            static constexpr double AUTO = -1.0;

        public:
            NormRect() : NormRect(AUTO, AUTO, AUTO, AUTO) {
            }

            NormRect(double x, double y, double w, double h) : _x(x), _y(y), _width(w), _height(h) {
            }

            double x() const {
                return this->_x;
            }

            void x(double x) {
                this->_x = x;
            }

            double y() const {
                return this->_y;
            }

            void y(double y) {
                this->_y = y;
            }

            double width() const {
                return this->_width;
            }

            void width(double w) {
                this->_width = w;
            }

            double height() const {
                return this->_height;
            }

            void height(double h) {
                this->_height = h;
            }

            // Rounds to nearest without snapping to even: several shipped squeeze windows
            // are an odd number of pixels (825, 917, 1609) and must survive a round trip.
            Rectangle resolve(const Rectangle &frame) const {
                return Rectangle(
                    _scale(this->_x,      frame.width()),
                    _scale(this->_y,      frame.height()),
                    _scale(this->_width,  frame.width()),
                    _scale(this->_height, frame.height())
                );
            }

            bool operator==(const NormRect &other) const {
                return
                    this->_x      == other._x &&
                    this->_y      == other._y &&
                    this->_width  == other._width &&
                    this->_height == other._height;
            }

            bool operator!=(const NormRect &other) const {
                return ! (*this == other);
            }

        private:
            // Negative components are the "auto" sentinel and are passed through untouched
            // so that the aspect-ratio logic downstream still recognises them.
            static int _scale(double value, int extent) {
                return value < 0.0 ? -1 : (int) std::lround(value * extent);
            }

        private:
            double _x, _y, _width, _height;
    };

    enum class Authentication {
        BASIC,
        DIGEST
    };

    class Credentials {
        public:
            Credentials() {
            }

            const std::string &getUsername() const {
                return this->_username;
            }

            const std::string &getPassword() const {
                return this->_password;
            }

            void setUsername(const std::string &username) {
                this->_username = username;
            }

            void setPassword(const std::string &password) {
                this->_password = password;
            }

            bool empty() const {
                return this->_username.empty() && this->_password.empty();
            }

        private:
            std::string _username;
            std::string _password;
    };

    const std::string &toString(MediaType type);
    const std::string &toString(Container container);
    const std::string &toString(Codec codec);
    const std::string &toString(PixelFormat format);
    
    PixelFormat parsePixelFormat(const std::string &str);

    GstVideoFormat toGst(PixelFormat format);
    
    PixelFormat fromGst(GstVideoFormat format);

    Protocol parseProtocol(const std::string &uri, Protocol &subprotocol);
    Protocol parseProtocolAndPath(const std::string &uri, Protocol &subprotocol, std::string &path);
}

#endif /* __UTPL_COMMON_TYPES_H__ */
