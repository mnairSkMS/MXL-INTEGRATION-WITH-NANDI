def process(event, info):
    print(event)
    if event.get('command', {}).get("name") == "Time Signal":
        event["descriptors"][0]["segmentation_event_id"] = "0xaaaa"
        event["descriptors"][0]["delivery_not_restricted_flag"] = True
        event["descriptors"][0]["archive_allowed_flag"] = True
        event["descriptors"].pop(1)
        return event

    return event