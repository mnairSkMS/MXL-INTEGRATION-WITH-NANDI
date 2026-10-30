#include <filesystem>

#include <boost/url/url.hpp>
#include <boost/url/parse.hpp>

#include <pugixml.hpp>

#include "utpl/common/debug.h"

#include "utpl/hls/streamer/uploader/http.h"

namespace urls = boost::urls;

using namespace utpl;

class DavInterceptor : public cpr::Interceptor {
    public:
        DavInterceptor(const std::string &method) : method(method) {
        }

        cpr::Response intercept(cpr::Session &session) override {
            curl_easy_setopt(session.GetCurlHolder()->handle, CURLOPT_CUSTOMREQUEST, method.c_str());

            return proceed(session);
        }

    private:
        std::string method;
};

hls::HttpUploader::HttpUploader(const std::string &serverUrl, bool dav, bool chunked) : BaseUploader(Capabilities(dav, true)), _serverUrl(serverUrl) {
    this->_session = std::make_unique<cpr::Session>();
    this->_chunked = chunked;
}

void hls::HttpUploader::setAuthorization(const utpl::Credentials &credentials, Authentication auth) {
    auto authMode = cpr::AuthMode::BASIC;

    switch (auth) {
        case Authentication::BASIC: authMode = cpr::AuthMode::BASIC; break;
        case Authentication::DIGEST: authMode = cpr::AuthMode::DIGEST; break;
        default: break;
    }

    this->_session->SetAuth(cpr::Authentication(
        credentials.getUsername(), credentials.getPassword(), authMode
    ));
}

utpl::Error hls::HttpUploader::doUpload(const std::string &path, std::shared_ptr<hls::SegmentStorage> storage, Timestamp timeout) {
    utpl::Error ret = utpl::Error::NO_ERROR;

    {
        auto p = this->getUrl(path);

        TRACE(("Uploading to '{}', {} bytes", p.str(), storage->length()));

        this->_session->SetUrl(p);
        this->_session->SetTimeout(std::chrono::milliseconds(timeout));
        this->_session->SetBody(cpr::Body());

        {
            cpr::Header header;

            header["Content-Type"] = storage->getMimeType();

            if (this->_chunked) {
                header["Transfer-Encoding"] = "chunked";
            }

            this->_session->SetHeader(header);
        }

        size_t offset = 0;
        
        this->_session->SetReadCallback(cpr::ReadCallback(storage->length(), [&offset, storage](char *buffer, long unsigned int &size, intptr_t userdata) {
            size = storage->read(buffer, offset, size);

            offset += size;

            return true;
        }));

        auto response = this->_session->Put();
        if (response.status_code == 0) {
            ERR(("Received error! {} - {}", (int) response.error.code, response.error.message));

            ret = utpl::Error::UNKNOWN_ERROR;

// 201
// 204
        } else if (response.status_code >= 400) {
            WARN(("Received response code {}", response.status_code));

            ret = utpl::Error::UNKNOWN_ERROR;
        }

    }

    return ret;
}

utpl::Error hls::HttpUploader::doRemoval(const std::string &path, Timestamp timeout) {
    utpl::Error ret = utpl::Error::NO_ERROR;

    {
        auto p = this->getUrl(path);

        TRACE(("Removing '{}'", p.str()));

        this->_session->SetUrl(p);
        this->_session->SetTimeout(timeout);
        this->_session->SetHeader(cpr::Header());
        this->_session->SetBody(cpr::Body());

        auto ret = this->_session->Delete();
    }

    return ret;
}

utpl::Error hls::HttpUploader::doMkir(const std::string &path, Timestamp timeout) {
    utpl::Error ret = utpl::Error::NO_ERROR;

    {
        auto p = this->getUrl(path);

        this->_session->SetUrl(p);
        this->_session->SetTimeout(timeout);
        this->_session->SetHeader(cpr::Header());
        this->_session->SetBody(cpr::Body());

        this->_session->AddInterceptor(std::make_shared<DavInterceptor>("MKCOL"));

        auto response = this->_session->Get();
        if (response.status_code == 201) {
            ret = utpl::Error::NO_ERROR;

        } else {
            ERR(("Unable to create directory '{}'", p.str()));

            ret = utpl::Error::UNKNOWN_ERROR;
        }
    }

    return ret;
}

utpl::Error hls::HttpUploader::doStat(const std::string &path, Timestamp timeout, bool &exist, bool &isDirectory) {
    utpl::Error ret = utpl::Error::NO_ERROR;

    {
        auto p = this->getUrl(path);

        this->_session->SetUrl(p);
        this->_session->SetTimeout(std::chrono::milliseconds(timeout));

        {
            cpr::Header header;

            header["Depth"]        = "0";
            header["Content-Type"] = "application/xml; charset=\"utf-8\"";

            this->_session->SetHeader(header);
        }

        {
            std::string body =
                "<?xml version=\"1.0\" encoding=\"utf-8\" ?>"
                "<D:propfind xmlns:D=\"DAV:\">"
                    "<D:prop>"
                        "<D:resourcetype/>"
                    "</D:prop>"
                "</D:propfind>"
            ;

            this->_session->SetBody(body);
        }
        
        this->_session->AddInterceptor(std::make_shared<DavInterceptor>("PROPFIND"));

        auto response = this->_session->Get();

        switch (response.status_code) {
            case 0:
                {
                    ERR(("Received error! {} - {}", (int) response.error.code, response.error.message));

                    ret = utpl::Error::UNKNOWN_ERROR;
                }
                break;

            case 404:
                {
                    exist       = false;
                    isDirectory = false;
                }
                break;

            case 207:
                {
                    pugi::xml_document doc;

                    isDirectory = false;

                    if (! doc.load_string(response.text.c_str())) {
                        ERR(("Unable to load/parse XML response"));

                    } else {
                        try {
                            auto prop = doc.select_node("/*[local-name()='multistatus']/*[local-name()='response']/*[local-name()='propstat']/*[local-name()='prop']/*[local-name()='resourcetype']");
                            if (prop) {
                                auto type = prop.node().first_child();
                                if (type) {
                                    std::string name = type.name();

                                    {
                                        auto pos = name.find_first_of(":");
                                     
                                        if (pos != std::string::npos) {
                                            name = name.substr(pos + 1);
                                        }
                                    }

                                    if (name == "collection") {
                                        isDirectory = true;
                                    }
                                }
                            }
                        } catch (const std::exception &ex) {
                            ERR(("Error parsing XML response from DAV: {}", ex.what()));
                        }
                    }

                    exist = true;
                }
                break;

            default:
                {
                    WARN(("Received response code {}", response.status_code));

                    ret = utpl::Error::UNKNOWN_ERROR;
                }
        }
    }

    return ret;
}

cpr::Url hls::HttpUploader::getUrl(const std::string &path) {
    urls::url parsedUrl = urls::parse_uri(this->_serverUrl).value();

    {
        std::filesystem::path mergedPath = std::filesystem::path(parsedUrl.path());

        if (path.empty()) {
            parsedUrl.set_path(mergedPath.string() + "/");

        } else {
            mergedPath /= path;

            parsedUrl.set_path(mergedPath.string());
        }
    }

    return cpr::Url(std::string(parsedUrl.buffer()));
}
