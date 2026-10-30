# Instalation of required tool packages on Debian-based Linux distros.
```console
apt install srt-tools tstools
```

##  Content files for testing purposes were obtained from:
 - https://cdn.freesound.org/previews/559/559850_12295155-hq.mp3
 - https://download.blender.org/peach/bigbuckbunny_movies/big_buck_bunny_720p_h264.mov

### Streaming MPEGTS over UDP
```console
tsplay -loop <file_path> <host>:<port>

Example: 
   tsplay -loop ./media/big_buck_bunny_480p_h264.ts 127.0.0.1:7777
```

### Streaming MPEGTS over UDP (multicast)
```console
tsplay -mcastif <iface_address> -loop <file_path> <mcast_addr>:<port>

Example:
   tsplay -mcastif 127.0.0.1 -loop ./../media/big_buck_bunny_480p_h264.ts  224.1.2.3:1111 
```

### Streaming MPEGTS over SRT
```console
tsplay -loop <file_path> <host>:<port>
srt-live-transmit <src_uri> <dst_uri>

Example:
   tsplay -loop ./media/big_buck_bunny_480p_h264.ts 127.0.0.1:1111
   srt-live-transmit udp://127.0.0.1:1111 srt://127.0.0.1:7777?mode=listener
```

### Capturing MPEGTS stream over SRT
```console
srt-live-transmit <src_uri> file://con

Example:
   srt-live-transmit srt://127.0.0.1:9999?mode=listener file://con > /tmp/out.ts
```

### Capturing MPEGTS stream over UDP
```console

netcat -u -l -p <udp_port>

Example:
   netcat -u -l -p 9999 > /tmp/out.ts
```

### Playing MPEGTS stream over UDP (multicast)
```console
ffplay udp://224.1.2.3:1111?localaddr=127.0.0.1
```

### Extracting only a fragment of the original media file
```console

ffmpeg -ss <start_ts> -to <end_ts> -i <in_file> -c copy <out_file>

Example:
   ffmpeg -ss 00:03:00 -to 00:03:05 -i media/big_buck_bunny_480p_h264.ts -c copy work/content/short1.ts
```

### Extracting H264 elementary stream from MPEGTS
```console
ffmpeg -i <in_file> -vcodec copy -acodec none udp.h264

Example:
   ffmpeg -i /tmp/out.ts -vcodec copy -acodec none udp.h264
```

### SRT stream analysis
```console
tsp -I srt --listener <host_port> --transtype live --messageapi -P analyze -O drop

Example:
   tsp -I srt --listener 127.0.0.1:9999 --transtype live --messageapi -P until --seconds 10 -P analyze -O drop
```

### Playback from SRT
```console
srt-live-transmit <src_uri> file://con | ffplay -i -

Example:
   srt-live-transmit -c 1456 -buffering 10 srt://127.0.0.1:9999?mode=listener file://con | ffplay -i -
```

### Conversion of MP4 to MPEGTS without transcoding
```console
ffmpeg -i big_buck_bunny_480p_h264.mov -vcodec copy -acodec copy -fflags +genpts+igndts -bsf:v h264_mp4toannexb big_buck_bunny_480p_h264.ts
```

### Adding SRT subtitles to MPEGTS (dvbsub)
```
for i in {1..3600..3}; do echo $i; start=$(printf "00:%02d:%02d,000" $((i/60)) $((i%60))); printf "$start --> 00:%02d:%02d,000\n" $(((i+1)/60)) $(((i+1)%60)); echo -e "Caption start time $start\n"; done > subtitles.srt

# Create single caption track

gst-launch-1.0 \
   filesrc location=subtitles.srt \
      ! subparse \
      ! textrender \
      ! dvbsubenc \
      ! taginject tags="language-code=eng" \
      ! mpegtsmux \
      ! filesink location=subtitles.ts

# Create multiple caption tracks

gst-launch-1.0 \
	filesrc location=subtitles.srt \
		! subparse \
		! textrender \
		! dvbsubenc \
      ! tee name=t \
   mpegtsmux name=mux \
      ! filesink location=subtitles.ts \
   t. ! queue ! taginject tags="language-code=pol" ! mux.sink_200 \
   t. ! queue ! taginject tags="language-code=eng" ! mux.sink_201 \
   t. ! queue ! taginject tags="language-code=deu" ! mux.sink_202

# Append captions to AV stream.

ffmpeg -i big_buck_bunny_480p_h264.ts -i subtitles.ts -map 0 -map 1 -acodec copy -vcodec copy -scodec copy big_buck_bunny_480p_h264_sub.ts

```

### Adding SCTE-35 events to MPEGTS
#### Create XML containing events to be injected

Here is an XML example of a pair of splice commands, a splice out event, followed 5 seconds later by a splice in event. This is typically an ads replacement opportunity. Here, the sections are minimal. You may want to add break duration information or additional descriptors.

```xml
<?xml version="1.0" encoding="UTF-8"?>
<tsduck>
   <splice_information_table>
      <splice_insert splice_event_id="100" unique_program_id="1"
         out_of_network="true" pts_time="450000"/>
   </splice_information_table>

   <splice_information_table>
      <splice_insert splice_event_id="100" unique_program_id="1"
         out_of_network="false" pts_time="900000"/>
   </splice_information_table>

   <splice_information_table>
      <time_signal pts_time="1350000" />

      <splice_segmentation_descriptor
         identifier="0x43554549"
         segmentation_event_id="1234"
         segmentation_event_cancel="false"
         web_delivery_allowed="true"
         no_regional_blackout="true"
         archive_allowed="true"
         device_restrictions="3"
         segmentation_duration="900000"
         segmentation_type_id="14"
         segment_num="0"
         segments_expected="0"
         sub_segment_num="0"
         sub_segments_expected="0"
      >
         <segmentation_upid type="0">AABBCC</segmentation_upid>
         
      </splice_segmentation_descriptor>
   </splice_information_table>
</tsduck>
```


#### Insert events into MPEGTS stream
```console
tsp --add-input-stuffing 1/10 \
	-I file big_buck_bunny_480p_h264.ts \
	-P pmt --service 1 --add-programinfo-id 0x43554549 --add-pid 600/0x86 \
	-P spliceinject --inject-count 1 --service 1 --start-delay 4000 --files events.xml --wait-first-batch \
	-P filter --negate --pid 0x1FFF \
	-O file big_buck_bunny_480p_h264_scte.ts
```

#### Insert timecode into video stream 
```console
ffmpeg \
   -i ./media/big_buck_bunny_480p_h264.ts \
   -vf "drawtext=text='':x=(w-tw)/2:y=(lh):fontsize=40:fontcolor=white:timecode='00\:00\:00\:00':timecode_rate=24" \
   -acodec copy \
   -r 24 \
   -vcodec h264 \
   ./media/big_buck_bunny_480p_h264_ts.ts
```

#### Dump PTS, picture type of all video frames
```console
ffprobe -of compact -select_streams v:0 -show_entries frame=key_frame,pict_type,pkt_pts_time ./big_buck_bunny_480p_h264.ts
```

#### Dump SCTE events from MPEGTS file
```console
tsp -I file big_buck_bunny_480p_h264_scte.ts -P splicemonitor -a -O drop
```

#### Live monitoring of SCTE events
```
# UDP
tsp -I ip :4444 -P splicemonitor -a -d  -O drop
# SRT
tsp -I srt -l 127.0.0.1:4444 -P splicemonitor -a -d  -O drop
```

### SCTE-35 in HLS manifest
#### Tools
```console
   sudo apt install pipx
   pipx install x9k3
   pipx inject x9k3 "threefive==2.4.55" --force
   pipx install threefive3
```

#### Create live segmented HLS stream with ad markers in manifest
Input file has already scte markers. The stream will be playes in loop (-r).
```console
   x9k3 -r -i big_buck_bunny_480p_h264_scte.ts -o /tmp/hls/data/
```

#### Inject event from file and create live segmented HLS stream with ad markers in manifest
Create events file (sidecar.txt) with adbreak using simple bash loop
```bash
#!/bin/bash

start_point=5
add_length=20
video_length=560
offset=30

iterations=$(( ($video_length / $offset) + 1 ))

for ((i=0; i<iterations; i++)); do
    adbreak -d ${add_length} -p "${start_point}.000"
    
    start_point=$((start_point + offset))
done
```

Create live segmented HLS stream with ad markers in manifest
```console
   x9k3 -r -s sidecar.txt -i big_buck_bunny_480p_h264_scte.ts -o /tmp/hls/data/
```

#### Monitor SCTE-35 tags in HLS manifest
```console
   threefive3 hls http://127.0.0.1:9875/index.m3u8
```
Note: threefive3 tools seems to have many bugs and it is still unstable 


### Run Pois Server mock
```console
docker run -it --rm -v $(pwd)/tests:/tests utpl/transcoder-test /opt/venv/bin/python3 /tests/pois_server_mock/app.py --port 8001 --custom_processor /tests/pois_server_mock/example_processor.py --debug
```

### Run Transcoder tests
all tests:
```console
docker run -it --rm utpl/transcoder-test

docker run -it --rm -v $(pwd)/tests:/tests utpl/transcoder-test
```

single test:
```console
docker run -it --rm utpl/transcoder-test bash test_file_input/test.sh
```

structure of the directory containing single test case:
* test.sh - main test file (contains program launch and assert verifying the output)
* transcoder_config.json - transcoder configuration for test

Optional:
* api_events_XXseconds.json - files containing api events, they will be placed in the transcoder database at the time set to XX seconds from the transcoder start 
* pois_conditioning.py - code of the function implementing the PIOS server logic
* events.txt - list of SCTE35 events that will be placed in the input stream for the transcoder
* subtitles.srt - subtitles will be added to the input stream

All files prefixed with "tmp_" will be deleted after the test is finished

### VA-API acceleration in docker container
Show information from VA-API driver:
```console
apt install vainfo
vainfo
```
add video device to the Docker container (Intel/Radeon). 
```console
docker run --device /dev/dri:/dev/dri utpl/transcoder
```

add video device to the Docker container (NVIDIA).
```console
docker run --gpus all --runtime nvidia -e NVIDIA_DRIVER_CAPABILITIES=video,compute,utility utpl/transcoder
```
 NVIDIA Container Runtime/Drivers/CUDA/libnvidia-decode must be installed on the host machine.

### Playing AV Content in Docker container via Shared X11 and PulseAudio Sessions
```console
docker run -it --rm --net=host -u $(id -u) \
	--device=/dev/dri:/dev/dri \
	-e DISPLAY -v /tmp/.X11-unix:/tmp/.X11-unix \
	-e PULSE_SERVER=unix:${XDG_RUNTIME_DIR}/pulse/native \
	-v ${XDG_RUNTIME_DIR}/pulse/native:${XDG_RUNTIME_DIR}/pulse/native \
	-v ~/.config/pulse/cookie:/root/.config/pulse/cookie \
	utpl/devel bash
```

### Starting HTTP + DAV server
```console
docker run --rm -it \
	-e 'AUTH_TYPE'='Basic' \
	-e 'USERNAME'='user' \
	-e 'PASSWORD'='password' \
	-v /tmp/hls:/var/lib/dav/data \
	-p 9876:80 \
	apachewebdav/apachewebdav
```