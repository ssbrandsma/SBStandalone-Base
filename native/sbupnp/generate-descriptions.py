"""Generate our standard UPnP AV v1 descriptions; no third-party source reuse."""
from pathlib import Path
import sys
from xml.etree.ElementTree import Element, SubElement, tostring

services = {
    "AVTransport": {
        "SetAVTransportURI": [("InstanceID", "in", "A_ARG_TYPE_InstanceID"), ("CurrentURI", "in", "AVTransportURI"), ("CurrentURIMetaData", "in", "AVTransportURIMetaData")],
        "GetMediaInfo": [("InstanceID", "in", "A_ARG_TYPE_InstanceID"), *[(name, "out", variable) for name, variable in [("NrTracks", "NumberOfTracks"), ("MediaDuration", "CurrentMediaDuration"), ("CurrentURI", "AVTransportURI"), ("CurrentURIMetaData", "AVTransportURIMetaData"), ("NextURI", "NextAVTransportURI"), ("NextURIMetaData", "NextAVTransportURIMetaData"), ("PlayMedium", "PlaybackStorageMedium"), ("RecordMedium", "RecordStorageMedium"), ("WriteStatus", "RecordMediumWriteStatus")]]],
        "GetTransportInfo": [("InstanceID", "in", "A_ARG_TYPE_InstanceID"), ("CurrentTransportState", "out", "TransportState"), ("CurrentTransportStatus", "out", "TransportStatus"), ("CurrentSpeed", "out", "TransportPlaySpeed")],
        "GetPositionInfo": [("InstanceID", "in", "A_ARG_TYPE_InstanceID"), *[(name, "out", variable) for name, variable in [("Track", "CurrentTrack"), ("TrackDuration", "CurrentTrackDuration"), ("TrackMetaData", "CurrentTrackMetaData"), ("TrackURI", "CurrentTrackURI"), ("RelTime", "RelativeTimePosition"), ("AbsTime", "AbsoluteTimePosition"), ("RelCount", "RelativeCounterPosition"), ("AbsCount", "AbsoluteCounterPosition")]]],
        "GetDeviceCapabilities": [("InstanceID", "in", "A_ARG_TYPE_InstanceID"), ("PlayMedia", "out", "PossiblePlaybackStorageMedia"), ("RecMedia", "out", "PossibleRecordStorageMedia"), ("RecQualityModes", "out", "PossibleRecordQualityModes")],
        "GetTransportSettings": [("InstanceID", "in", "A_ARG_TYPE_InstanceID"), ("PlayMode", "out", "CurrentPlayMode"), ("RecQualityMode", "out", "CurrentRecordQualityMode")],
        "GetCurrentTransportActions": [("InstanceID", "in", "A_ARG_TYPE_InstanceID"), ("Actions", "out", "CurrentTransportActions")],
        "SetPlayMode": [("InstanceID", "in", "A_ARG_TYPE_InstanceID"), ("NewPlayMode", "in", "CurrentPlayMode")],
        "Seek": [("InstanceID", "in", "A_ARG_TYPE_InstanceID"), ("Unit", "in", "A_ARG_TYPE_SeekMode"), ("Target", "in", "A_ARG_TYPE_SeekTarget")],
        **{name: [("InstanceID", "in", "A_ARG_TYPE_InstanceID")] for name in ("Stop", "Pause", "Next", "Previous")},
        "Play": [("InstanceID", "in", "A_ARG_TYPE_InstanceID"), ("Speed", "in", "TransportPlaySpeed")],
    },
    "RenderingControl": {
        "ListPresets": [("InstanceID", "in", "A_ARG_TYPE_InstanceID"), ("CurrentPresetNameList", "out", "PresetNameList")],
        "SelectPreset": [("InstanceID", "in", "A_ARG_TYPE_InstanceID"), ("PresetName", "in", "A_ARG_TYPE_PresetName")],
        **{name: [("InstanceID", "in", "A_ARG_TYPE_InstanceID"), ("Channel", "in", "A_ARG_TYPE_Channel"), (argument, direction, variable)] for name, argument, direction, variable in [("GetVolume", "CurrentVolume", "out", "Volume"), ("SetVolume", "DesiredVolume", "in", "Volume"), ("GetMute", "CurrentMute", "out", "Mute"), ("SetMute", "DesiredMute", "in", "Mute")]}},
    "ConnectionManager": {
        "GetProtocolInfo": [("Source", "out", "SourceProtocolInfo"), ("Sink", "out", "SinkProtocolInfo")],
        "GetCurrentConnectionIDs": [("ConnectionIDs", "out", "CurrentConnectionIDs")],
        "GetCurrentConnectionInfo": [("ConnectionID", "in", "A_ARG_TYPE_ConnectionID"), *[(n, "out", v) for n, v in [("RcsID", "A_ARG_TYPE_RcsID"), ("AVTransportID", "A_ARG_TYPE_AVTransportID"), ("ProtocolInfo", "A_ARG_TYPE_ProtocolInfo"), ("PeerConnectionManager", "A_ARG_TYPE_ConnectionManager"), ("PeerConnectionID", "A_ARG_TYPE_ConnectionID"), ("Direction", "A_ARG_TYPE_Direction"), ("Status", "A_ARG_TYPE_ConnectionStatus")]]],
    },
}
types = {"A_ARG_TYPE_InstanceID": "ui4", "NumberOfTracks": "ui4", "CurrentTrack": "ui4", "RelativeCounterPosition": "i4", "AbsoluteCounterPosition": "i4", "Volume": "ui2", "Mute": "boolean", "A_ARG_TYPE_ConnectionID": "i4", "A_ARG_TYPE_RcsID": "i4", "A_ARG_TYPE_AVTransportID": "i4"}
out = []
for service, actions in services.items():
    root = Element("scpd", xmlns="urn:schemas-upnp-org:service-1-0")
    version = SubElement(root, "specVersion")
    SubElement(version, "major").text = "1"
    SubElement(version, "minor").text = "0"
    action_list = SubElement(root, "actionList")
    variables = set()
    for name, args in actions.items():
        action = SubElement(action_list, "action")
        SubElement(action, "name").text = name
        arguments = SubElement(action, "argumentList")
        for argument, direction, variable in args:
            node = SubElement(arguments, "argument")
            for key, value in [("name", argument), ("direction", direction), ("relatedStateVariable", variable)]:
                SubElement(node, key).text = value
            variables.add(variable)
    table = SubElement(root, "serviceStateTable")
    variables.add("LastChange") if service != "ConnectionManager" else None
    for variable in sorted(variables):
        evented = variable == "LastChange" or service == "ConnectionManager" and variable in ("SourceProtocolInfo", "SinkProtocolInfo", "CurrentConnectionIDs")
        node = SubElement(table, "stateVariable", sendEvents="yes" if evented else "no")
        SubElement(node, "name").text = variable
        SubElement(node, "dataType").text = types.get(variable, "string")
        if variable == "Volume":
            bounds = SubElement(node, "allowedValueRange")
            for key, value in [("minimum", "0"), ("maximum", "100"), ("step", "1")]:
                SubElement(bounds, key).text = value
    data = tostring(root, encoding="utf-8") + b"\0"
    chunks = [",".join(str(c) for c in data[i:i+80]) for i in range(0, len(data), 80)]
    out.append(f"static const char scpd_{service}[] = {{\n" + ",\n".join(chunks) + "\n};")
Path(sys.argv[1]).write_text("/* Generated by native/sbupnp/generate-descriptions.py. */\n" + "\n".join(out) + "\n", encoding="utf-8")
