#ifndef __UTPL_PLAYLIST_PARSER_H__
#define __UTPL_PLAYLIST_PARSER_H__

#include <memory>
#include <vector>
#include <string>

#include "utpl/common/utils/m3u8.h"
#include "utpl/transcoder/input.h"

namespace utpl {
    namespace playlist {
        class Parser {
            public:
                Parser() = default;
                ~Parser() = default;

                std::vector<transcoder::InputParameters> parse(std::istream &stream);

            private:
                std::unique_ptr<utils::M3U8> _parseM3U8(std::istream &stream);
                transcoder::InputParameters _createInputFromEntry(
                    const utils::M3U8::TagEntry &tagEntry,
                    const utils::M3U8::UriEntry &uriEntry
                );
        };
    }
}

#endif /* __UTPL_PLAYLIST_PARSER_H__ */
