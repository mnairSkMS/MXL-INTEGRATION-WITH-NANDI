#include "application/config/parser/json.h"

#include "json/parser.h"

std::unique_ptr<app::ConfigParser> app::JsonConfigParserFactory::createParser(const std::string &path) {
    return std::make_unique<app::MultiVersionConfigJsonParser>();
}

std::unique_ptr<app::DebugConfigParser> app::JsonDebugConfigParserFactory::createParser(const std::string &path) {
    return std::make_unique<app::MultiVersionDebugConfigJsonParser>();
}