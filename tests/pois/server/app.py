import argparse
import copy
import importlib
import pathlib
import sys
from pyexpat import ExpatError

import logging

import uvicorn
import xmltodict
import threefive3
from fastapi import FastAPI, Request, Response, HTTPException, status

custom_module = None

LOGGER = logging.getLogger(__name__)
LOGGER.setLevel(logging.DEBUG)


def validate_signal(acq_signal):
    for attr_name in ["@acquisitionPointIdentity", "@acquisitionSignalID"]:
        if attr_name not in acq_signal:
            raise HTTPException(
                status_code=status.HTTP_422_UNPROCESSABLE_ENTITY,
                detail=f"missing {attr_name} attrigute in AcquiredSignal",
            )

    if not acq_signal.get("sig:UTCPoint", {}).get("@utcPoint"):
        raise HTTPException(
            status_code=status.HTTP_422_UNPROCESSABLE_ENTITY,
            detail=f"AcquiredSignal>UTCPoint@utcPoint is required",
        )

    if acq_signal.get('SCTE35PointDescriptor'):
        raise HTTPException(
            status_code=status.HTTP_501_NOT_IMPLEMENTED,
            detail=f"AcquiredSignal>SCTE35PointDescriptor is not implemented yet",
        )
    if not acq_signal.get("sig:BinaryData", {}).get("#text"):
        raise HTTPException(
            status_code=status.HTTP_422_UNPROCESSABLE_ENTITY,
            detail=f"AcquiredSignal>sig:BinaryData",
        )

    if acq_signal.get("sig:BinaryData", {}).get("@signalType") != "SCTE35":
        raise HTTPException(
            status_code=status.HTTP_422_UNPROCESSABLE_ENTITY,
            detail=f"only SCTE35 binary type is supported",
        )


def validate_request(request):
    root = request["SignalProcessingEvent"]
    if "AcquiredSignal" not in root:
        raise HTTPException(
            status_code=status.HTTP_422_UNPROCESSABLE_ENTITY,
            detail="cannot parse request xml body missing AcquiredSignal in SignalProcessingEvent",
        )
    acq_signal = root["AcquiredSignal"]
    if isinstance(acq_signal, list):
        for signal in acq_signal:
            validate_signal(signal)
    else:
        validate_signal(acq_signal)


def process(acq_signal, custom_status_code):
    resp_signal = dict()
    resp_signal["@acquisitionPointIdentity"] = acq_signal["@acquisitionPointIdentity"]
    resp_signal["@acquisitionSignalID"] = acq_signal["@acquisitionSignalID"]
    if "@acquisitionTime" in acq_signal:
        resp_signal["@acquisitionTime"] = acq_signal["@acquisitionTime"]
    if "@signalPointID" in acq_signal:
        resp_signal["@signalPointID"] = acq_signal["@signalPointID"]
    resp_signal["sig:UTCPoint"] = {
        "@utcPoint":  acq_signal.get("sig:UTCPoint", {}).get("@utcPoint")
    }
    src_bin_data = acq_signal.get("sig:BinaryData", {}).get("#text")

    try:
        scte_35_cue = threefive3.Cue(src_bin_data)
        scte_35_cue.decode()
        scte_35_dict = scte_35_cue.get()
        if custom_module:
            processed_dict = custom_module.process(copy.deepcopy(scte_35_dict), acq_signal)
        else:
            processed_dict = scte_35_dict
        if processed_dict is None:
            processed_data = None
        elif processed_dict == scte_35_dict:
            processed_data = src_bin_data
        else:
            scte_35_cue = threefive3.Cue()
            scte_35_cue.load(processed_dict)
            processed_data = scte_35_cue.encode()

    except Exception as e:
        custom_status_code.append({
            "@classCode": 2,
            "core:Note": "Unable to decode inbound SCTE35, using default behavior"
        })
        LOGGER.error(
            "Unable to decode inbound SCTE35, using default behavior", exc_info=e
        )
        processed_data = None

    if processed_data is None:
        resp_signal["@action"] = "delete"
    elif processed_data != src_bin_data:
        resp_signal["@action"] = "replace"
        resp_signal["sig:BinaryData"] = {
            "@signalType": acq_signal.get("sig:BinaryData", {}).get("@signalType"),
            "#text": processed_data
        }
    else:
        resp_signal["@action"] = "noop"
        resp_signal["sig:BinaryData"] = {
            "@signalType": acq_signal.get("sig:BinaryData", {}).get("@signalType"),
            "#text": src_bin_data
        }
    print(f"@action: {resp_signal["@action"]}")
    return resp_signal


app = FastAPI(debug=True)


@app.post("/esam")
async def lambda_handler(request: Request):
    LOGGER.info(request)

    # Extract SPE from Transcoder
    esam_payload_xml = await request.body()

    # Convert to JSON
    try:
        request = xmltodict.parse(esam_payload_xml)
    except ExpatError as e:
        raise HTTPException(
            status_code=status.HTTP_422_UNPROCESSABLE_ENTITY,
            detail="cannot parse request xml body",
        )

    validate_request(request)

    root = request["SignalProcessingEvent"]
    acq_signal = root["AcquiredSignal"]

    resp_signals = []
    custom_status_code = []
    if isinstance(acq_signal, list):
        for signal in acq_signal:
            resp_signal = process(signal, custom_status_code)
            if resp_signal:
                resp_signals.append(resp_signal)
    else:
        resp_signal = process(acq_signal, custom_status_code)
        if resp_signal:
            resp_signals.append(resp_signal)

    # Create response SPN
    spn = dict()
    response_main_elements_attributes = {
        "@xmlns:xsi": "http://www.w3.org/2001/XMLSchema-instance",
        "@xmlns:sig": "urn:cablelabs:md:xsd:signaling:3.0",
        "@xmlns:core": "urn:cablelabs:md:xsd:core:3.0",
        "@xsi:schemaLocation": "urn:cablelabs:iptvservices:esam:xsd:common:1 OC-SP-ESAM- API-I0x-Common.xsd",
        "@xmlns": "urn:cablelabs:iptvservices:esam:xsd:common:1",
    }

    spn["SignalProcessingNotification"] = response_main_elements_attributes
    spn["SignalProcessingNotification"]["ResponseSignal"] = resp_signals
    if len(custom_status_code) > 0:
        spn["SignalProcessingNotification"]["StatusCode"] = custom_status_code

    # convert payload to xml for return
    spn_xml = xmltodict.unparse(spn, short_empty_elements=True, pretty=True)

    return Response(content=spn_xml, media_type="application/xml")


def import_from_path(module_name, file_path):
    """Import a module given its name and file path."""
    spec = importlib.util.spec_from_file_location(module_name, file_path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[module_name] = module
    spec.loader.exec_module(module)
    return module

def import_from_string(module_name, source_code):
    spec = importlib.util.spec_from_loader(module_name, loader=None)
    module = importlib.util.module_from_spec(spec)
    exec(source_code, module.__dict__)
    sys.modules[spec.name] = module
    return module


if __name__ == "__main__":
    parser = argparse.ArgumentParser(prog='PoisServer Mock')
    parser.add_argument('--custom_processor')
    parser.add_argument('--port', type=int, default=8000)
    parser.add_argument('--debug', action='store_true', default=False)
    args = parser.parse_args()

    logging.basicConfig(
        level=logging.DEBUG if args.debug else logging.WARN,
        format="%(asctime)s - %(levelname)s - %(name)s -%(message)s"
    )

    if args.custom_processor == '-':
        custom_module = import_from_string("custom_processor", sys.stdin.read())
        LOGGER.info(f"custom processor:{custom_module} loaded")
    if args.custom_processor and pathlib.Path(args.custom_processor).is_file():
        custom_module = import_from_path("custom_processor", args.custom_processor)
        LOGGER.info(f"custom processor:{custom_module} loaded")
    else:
        custom_module = None

    uvicorn.run(app, host="0.0.0.0", port=args.port, reload=False)
