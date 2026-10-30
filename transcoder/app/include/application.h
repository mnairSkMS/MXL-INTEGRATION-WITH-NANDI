#ifndef __APPICATION_H__
#define __APPICATION_H__

#include <memory>
#include <string>

#include "utpl/common/types.h"
#include "utpl/license.h"

class Application {
    public:
        Application();
        virtual ~Application();

        void configure(const std::string &configPath, const std::string &dbPath, const std::string &licensePath, const utpl::license::License &license, const utpl::crypto::PublicKey &licensePublicKey);

        void start();
        void stop();

        void loop();

        // Why the engine stopped: NO_ERROR for a requested stop or a finished input.
        utpl::Error getError() const;

    private:
        class Pimpl;
        std::unique_ptr<Pimpl> self;
};


#endif /* __APPICATION_H__ */