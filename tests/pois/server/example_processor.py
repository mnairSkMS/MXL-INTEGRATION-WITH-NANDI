
# acquired_signal = {
# '@acquisitionPointIdentity': 'michal',
# '@acquisitionSignalID': '168fdcf8edbb42df8acb99f4c8ae3c2e',
# '@acquisitionTime': '2024-08-20T16:16:42.123Z',
# 'sig:UTCPoint': {'@utcPoint': '2024-08-20T16:16:42.123Z'},
# 'sig:BinaryData': {'@signalType': 'SCTE35', '#text': '/D//AH/////////wDwUAAozof8/+1Ykjv1VmAAAAAP//////////'},
# 'sig:StreamTimes': {'sig:StreamTime': {'@TimeType': 'PTS', '@TimeValue': '5022471834'}}
# }

# threefive_data_dict = {
#   'info_section': {
#      'table_id': '0xfc',
#      'section_syntax_indicator': False,
#      'private': False,
#      'sap_type': '0x03',
#      'sap_details': 'No Sap Type',
#      'section_length': 4095,
#      'protocol_version': 0,
#      'encrypted_packet': False,
#      'encryption_algorithm': 63,
#      'pts_adjustment_ticks': 8589934591,
#      'pts_adjustment': 95443.717678,
#      'cw_index': '0xff',
#      'tier': '0x0fff',
#      'splice_command_length': 15,
#      'splice_command_type': 5,
#      'descriptor_loop_length': 0,
#      'crc': '0xffffffff'
#    },
#    'command': {
#      'command_length': 15,
#      'command_type': 5,
#      'name': 'Splice Insert',
#      'time_specified_flag': True,
#      'pts_time': 39805.9399,
#      'pts_time_ticks': 3582534591,
#      'splice_event_id': 167144,
#      'splice_event_cancel_indicator': False,
#      'out_of_network_indicator': True,
#      'program_splice_flag': True,
#      'duration_flag': False,
#      'splice_immediate_flag': False,
#      'event_id_compliance_flag': True,
#      'unique_program_id': 21862,
#      'avail_num': 0,
#      'avail_expected': 0
#    },
#    'descriptors': []
# }
def process(threefive_data_dict, acquired_signal):
    threefive_data_dict["command"]["splice_event_id"] = 1111
    return threefive_data_dict
