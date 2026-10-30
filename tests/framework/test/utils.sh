function test_utils_download_bunny() {
    local dstPath=${TEST_PATH_SRC_BBB}

    if [ ! -e $dstPath ]; then
        test_dbg "Downloading big buck bunny video"

        test_uri_download "${TEST_DEFAULT_BBB_URI}" "$dstPath"
    else
        test_dbg "Big buck bunny video has been already downloaded"
    fi
}

function test_utils_prepare_src_file() {
    local targetPath=
    local inputPath=
    local type=
    local duration=
    local subtitlesPath=
    local eventsPath=
    local audioCodecOpts=
    local videoCodecOpts=
    local packager=
    local timecodeOverlay=

    local frameRate=24

    local OPTIND
    while getopts ":o:i:t:d:s:e:a:v:p:O:" opt; do
        case "$opt" in
            i)
                inputPath="$OPTARG"
            ;;
            o)
                targetPath="$OPTARG"
            ;;
            t)
                type="$OPTARG"
            ;;
            d)
                duration="$OPTARG"
            ;;
            s)
                subtitlesPath="$OPTARG"
            ;;
            e)
                eventsPath="$OPTARG"
            ;;
            a)
                audioCodecOpts="$OPTARG"
            ;;
            v)
                videoCodecOpts="$OPTARG"
            ;;
            p)
                packager="$OPTARG"
            ;;
            O)
                timecodeOverlay="$OPTARG"
            ;;
        esac
    done 
    shift $((OPTIND-1))

    test_dbg "Preparing src file of type \"$type\", duration: \"$duration\", subtitles: \"$subtitlesPath\", scte: \"$eventsPath\", packager: \"$packager\", timecode: \"$timecodeOverlay\""

    local targetFileName="${targetPath%.*}"
    local targetSrcPath=${targetFileName}.src.ts
    local targetSrcSubPath=${targetFileName}.sub.ts
    local targetSrcSctePath=${targetFileName}.scte.ts

    rm -f ${targetPath}

    local ffmpegCmd="ffmpeg $FF_QUIET"
    if [ ! -z "$duration" ] && [ $duration -ne 0 ]; then
        ffmpegCmd="${ffmpegCmd} -t $duration"
    fi

    case "$type" in
        "bunny")
            test_utils_download_bunny

            ffmpegCmd="${ffmpegCmd} -i ${TEST_PATH_SRC_BBB}"
            ;;

        "image")
            ffmpegCmd="${ffmpegCmd} -stream_loop -1 -i ${TEST_PATH_DIR_MEDIA}/RCA_Indian_Head_Test_Pattern.svg.png -f lavfi -i sine=frequency=1000:sample_rate=44100"
            
            if [ ! -z "$duration" ] && [ $duration -ne 0 ]; then
                ffmpegCmd="${ffmpegCmd}:duration=$duration"
            fi
            
            ffmpegCmd="${ffmpegCmd} -acodec mp2 -ar 44100 -r ${frameRate} -vcodec h264"
            ;;

        "control")
            local filter="\
                color=c=white:size=1920x1080:rate=${TEST_DEFAULT_VIDEO_RATE}[bg];\
                color=red:s=200x200:rate=${TEST_DEFAULT_VIDEO_RATE}[r];\
                color=green:s=80x200:rate=${TEST_DEFAULT_VIDEO_RATE}[g];\
                color=blue:s=80x200:rate=${TEST_DEFAULT_VIDEO_RATE}[b];\
                [bg][r]overlay=x='t/10*(W-200)':y=0[v];\
                [v][g]overlay=x='mod(t*W + W/2, W)':y=200[v];\
                [v][b]overlay=x='W/2':y=400[v];\
                [v]drawtext=fontfile=/usr/share/fonts/truetype/DroidSans.ttf: timecode='00\:00\:00\:00': r=${TEST_DEFAULT_VIDEO_RATE}: x=(w-tw)/2: y=h-(2*lh): fontcolor=white: box=1: fontsize=64 :boxcolor=0x00000000@1\
            "

            ffmpegCmd="${ffmpegCmd} -f lavfi -i \"sine=f=1000\:d=$duration,adelay=1s\" -af \"volume=0\:enable=gt(mod(t\, 1)\, 0.1)\" -t $duration -filter_complex \"${filter}\""
            ;;

        "custom")
            cp -ar "$inputFile" "$targetPath"
            return 0
            ;;

        *)
            test_fatal "Unknown type of source file!"
    esac

    ffmpegCmd="${ffmpegCmd} \
        -s ${TEST_DEFAULT_VIDEO_WIDTH}x${TEST_DEFAULT_VIDEO_HEIGHT} \
        ${audioCodecOpts} \
        ${videoCodecOpts} \
    "

    if [ ! -z "$timecodeOverlay" ] && [ $timecodeOverlay -eq 1 ]; then
        ffmpegCmd="${ffmpegCmd} \
            -vf \"drawtext=text='':x=(w-tw)/2:y=(lh):fontsize=40:fontcolor=white:timecode='00\:00\:00\:00':timecode_rate=${frameRate}\" \
        "
    fi

    ffmpegCmd="${ffmpegCmd} \
        -f mpegts $targetSrcPath \
    "

    rm -f ${targetSrcPath}

    test_trace "Converting source file to expected format"

    test_proc_call ${ffmpegCmd}

    if [ ! -z "$subtitlesPath" ] && [ -e "$subtitlesPath" ]; then
        local targetSubTmpPath=${targetFileName}.sub.tmp.ts

        test_log "Converting subtitles to MpegTS stream"

        rm -f ${targetSrcOutPath}

        test_proc_call gst-launch-1.0 \
            filesrc location=$subtitlesPath \
                ! subparse \
                ! textrender \
                ! dvbsubenc \
                ! tee name=t \
            mpegtsmux name=mux \
                ! filesink location=${targetSubTmpPath} \
            t. ! queue ! taginject tags="language-code=pol" ! mux.sink_${TEST_DEFAULT_PID_SUB_POL} \
            t. ! queue ! taginject tags="language-code=eng" ! mux.sink_${TEST_DEFAULT_PID_SUB_ENG} \
            t. ! queue ! taginject tags="language-code=deu" ! mux.sink_${TEST_DEFAULT_PID_SUB_GER}

        test_log "Merging source file with subitles stream ($targetSrcSubPath)"

        test_proc_call ffmpeg $FF_QUIET -i ${targetSubTmpPath} -i ${targetSrcPath} -map 0 -map 1 -acodec copy -vcodec copy -scodec copy ${targetSrcSubPath}

        rm -f ${targetSubTmpPath} ${targetSrcPath}
    fi

    if [ ! -z "$eventsPath" ] && [ -e "$eventsPath" ]; then
        local pid=$TEST_DEFAULT_SCTE_PID
        local inputFile;
    
        if [ -e $targetSrcSubPath ]; then
            inputFile=${targetSrcSubPath}
    
        else
            inputFile=${targetSrcPath}
        fi

        test_log "Inserting SCTE events on PID '$pid'"
    
        test_proc_call tsp \
            --add-input-stuffing 1/10 \
            -I file "${inputFile}" \
            -P pmt \
                --service 1 \
                --add-programinfo-id 0x43554549 \
                --add-pid ${pid}/0x86 \
            -P spliceinject \
                --inject-count 1 \
                --service 1 \
                --start-delay ${TEST_DEFAULT_SCTE_INJECT_DELAY} \
                --files ${eventsPath} \
                --min-bitrate 1024 \
                --wait-first-batch \
            \
            -P filter --negate --pid 0x1FFF \
            -O file "${targetSrcSctePath}"

    fi

    case "$packager" in
        "hls")
            # get path without filename from targetPath
            local outputPath=$(dirname $targetPath)
            local segementsPath="${outputPath}/segment_%03d.ts"
            local playlistPath="${outputPath}/playlist.m3u8"

            test_proc_call ffmpeg $FF_QUIET -i ${targetSrcPath} \
                -hls_time 3 \
                -hls_segment_filename "${segementsPath}" \
                -hls_list_size 0 \
                -c:a copy \
                -c:v copy \
                -f hls "${playlistPath}"

            ;;
    esac


    if [ -e "${targetSrcSctePath}" ]; then
        mv ${targetSrcSctePath}  ${targetPath}

    elif [ -e "${targetSrcSubPath}" ]; then
        mv ${targetSrcSubPath}  ${targetPath}

    else
        mv ${targetSrcPath} ${targetPath}
    fi

    rm -f "${targetSrcPath}"
    rm -f "${targetSrcSubPath}"
    rm -f "${targetSrcSctePath}"
}
