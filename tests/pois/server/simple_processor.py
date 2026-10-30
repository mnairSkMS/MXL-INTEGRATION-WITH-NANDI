import logging

LOGGER = logging.getLogger(__name__)
LOGGER.setLevel(logging.DEBUG)

def process(threefive_data_dict, acquired_signal):
    LOGGER.debug(f"processing ID: {threefive_data_dict['command']['splice_event_id']}")

    if threefive_data_dict["command"]["command_type"] == 5: 
        # delete
        if threefive_data_dict['command']['splice_event_id'] == 100:
            LOGGER.debug(f"Response action: DELETE")
            return None

        # replace
        elif threefive_data_dict['command']['splice_event_id'] == 200:
            LOGGER.debug(f"Response action: REPLACE")
            threefive_data_dict['command']['splice_event_id'] = 400

        # noop
        else:
            LOGGER.debug(f"Response action: NOOP")

    return threefive_data_dict
