#ifndef __UTPL_PIPELINE_BIN_H__
#define __UTPL_PIPELINE_BIN_H__

#include <memory>

#include "utpl/pipeline/element.h"

namespace utpl {
    namespace pipeline {
        class Bin;

        using BinPtr = std::shared_ptr<Bin>;

        class Bin : public Element {
            public:
                Bin(const GstElementPtr &element);

            public:
                ElementPtr add(const ElementPtr &wrapper);

                void remove(const ElementPtr &wrapper);
                ElementPtr get(const std::string &name);
                
                void forEach(std::function<void(const ElementPtr &)> callback) const;
                void forEach(std::function<void(ElementPtr &)> callback);

                bool contains(const std::string &name, bool recursiveSearch);

                GhostPadPtr addPad(const std::string &name, PadPtr targetPad, bool srcDirection = false);
                void removePad(PadPtr pad);

                static BinPtr create(const std::string &name = "");
                static BinPtr parse(const std::string &desc);
                static BinPtr parse(const std::string &name, const std::string &desc);

                static bool isBin(const ElementPtr &ptr);

            private:
                static bool contains(GstBin *bin, const std::string &name, bool recursive);

            private:
                std::vector<ElementPtr> _children;
                mutable std::recursive_mutex _childrenLock;
        };
    }
}

#endif /* __UTPL_PIPELINE_BIN_H__ */