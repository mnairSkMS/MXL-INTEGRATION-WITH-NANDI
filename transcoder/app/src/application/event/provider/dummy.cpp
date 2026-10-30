#include "utpl/common/debug.h"

#include "application/event/provider/dummy.h"

#define EVENT_INTERVAL_MS    4000
#define EVENT_START_DELAY_MS 1000
#define EVENT_DURATION_MS    2000


using namespace utpl;


app::DummyEventProvider::DummyEventProvider() {
    this->_eventReady = false;
}


std::vector<std::unique_ptr<utpl::transcoder::Event>> app::DummyEventProvider::getNextEvents(utpl::Timestamp notBefore, utpl::Timestamp notAfter) {
    std::vector<std::unique_ptr<utpl::transcoder::Event>> ret;

    do {
        auto cur = utpl::Time::getUTCTimestamp();

        if (cur < this->_lastEventTimestamp) {
            this->_lastEventTimestamp = cur;
            break;
        }

        if (cur - this->_lastEventTimestamp > EVENT_INTERVAL_MS) {
            this->nextEvent();
        }

        if (this->_eventReady) {
            if (this->_eventTimestamp >= notBefore && this->_eventTimestamp <= notAfter) {
                switch (this->_eventType) {
                    case EventType::SQUEEZE_LOW:
                        {
                            auto e = std::make_unique<transcoder::EventSqueeze>(this->_eventId, this->_eventTimestamp, EVENT_DURATION_MS, "low_band");

                            e->setFadeDuration(500, 500);

                            ret.push_back(std::move(e));
                        }
                        break;

                    case EventType::SQUEEZE_L_BAND:
                        {
                            auto e = std::make_unique<transcoder::EventSqueeze>(this->_eventId, this->_eventTimestamp, EVENT_DURATION_MS, "l_shape");

                            e->setFadeDuration(500, 500);

                            ret.push_back(std::move(e));
                        }
                        break;

                    case EventType::OVERLAY_STATIC:
                        {
                            auto e = std::make_unique<transcoder::EventOverlay>(this->_eventId, this->_eventTimestamp, 3000);

                            e->setUris({
                                "file:///src/media/elephant.png"
                            });

                            e->setGeometry({ (rand() % 208) / 1000.0, (rand() % 278) / 1000.0, 0.09375, 0.166667 });
                            e->setOpacity(1.0);
                            e->setFadeDuration(2000, 2000);
                            e->setRate(Fraction(25, 1));

                            ret.push_back(std::move(e));
                        }
                        break;
                }
            }
        }
    } while (0);

    return ret;
}


void app::DummyEventProvider::markProcessed(const transcoder::Event &event, const std::string &errorMessage) {
    if (! errorMessage.empty()) {
        ERR(("Event processor returned an error: '{}'", errorMessage));
    }

    this->_eventReady = false;
}


bool app::DummyEventProvider::isValid(const transcoder::Event &event) {
    if (this->_eventReady) {
        if (this->_eventId == event.getId()) {
            return true;
        }
    }

    return false;
}


void app::DummyEventProvider::start() {
    this->_lastEventTimestamp = utpl::Time::getUTCTimestamp();
    this->_eventId            = 1;
    this->_eventType          = EventType::FIRST;
    this->_eventReady         = false;
    this->_eventTimestamp     = 0;
}


void app::DummyEventProvider::stop() {
}


void app::DummyEventProvider::nextEvent() {
    this->_lastEventTimestamp = utpl::Time::getUTCTimestamp();

    this->_eventId++;

    int eventType = this->_eventType + 1;
    if (eventType == EventType::LAST) {
        eventType = EventType::FIRST;
    }
    this->_eventType      = (EventType) eventType;//EventType::OVERLAY_STATIC;//eventType;
    this->_eventReady     = true;
    this->_eventTimestamp = this->_lastEventTimestamp + EVENT_START_DELAY_MS;
}
