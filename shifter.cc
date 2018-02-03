#include <cstdlib>
#include <iostream>
#include <string>
#include <sstream>
#include <iomanip>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/avutil.h>
}
using namespace std;

static AVStream *addStream(AVFormatContext *pFormatCtx, AVStream *pInStream, AVCodecContext **pOutCodecCtx) {
    AVCodecParameters *pInCodecPar = NULL;
    AVCodecContext *pInCodecCtx = NULL;
    AVCodec *input_codec = NULL;
    AVCodec *output_codec = NULL;
    AVStream *pOutStream = NULL;
    AVDictionary *pOptionsDict = NULL;


    pOutStream = avformat_new_stream(pFormatCtx, 0);
    if (!pOutStream) {
        cout << "Could not allocate stream" << endl;
        exit(1);
    }

    // Get a pointer to the codec parameters for the video stream
    pInCodecPar = pInStream->codecpar;

    input_codec = avcodec_find_decoder(pInCodecPar->codec_id);
    if (input_codec == NULL) {
        cout << "Unsupported codec (" << pInCodecPar->codec_id << ")" << endl;
        exit(1);
    }

    pInCodecCtx = avcodec_alloc_context3(input_codec);
    if (!pInCodecCtx) {
        fprintf(stderr, "Could not allocate a decoding context\n");
        exit(1);
    }

    if (avcodec_parameters_to_context(pInCodecCtx, pInStream->codecpar) < 0) {
        fprintf(stderr, "Could not initialize a decoding context\n");
        avcodec_free_context(&pInCodecCtx);
        exit(1);
    }

    // Open codec
    if (avcodec_open2(pInCodecCtx, input_codec, &pOptionsDict) < 0) {
        cout << "Unable to open codec (" << pInCodecPar->codec_id << ")" << endl;
        exit(1);
    }

    output_codec = avcodec_find_decoder(pInCodecPar->codec_id);
    *pOutCodecCtx = avcodec_alloc_context3(output_codec);
    if (!*pOutCodecCtx) {
        cout << "Unable to allocate output codec context" << endl;
        exit(1);
    }

    // surprised there's not a codec context copy function
    (*pOutCodecCtx)->codec_id = pInCodecPar->codec_id;
    (*pOutCodecCtx)->codec_type = pInCodecPar->codec_type;
    (*pOutCodecCtx)->codec_tag = pInCodecPar->codec_tag;
    (*pOutCodecCtx)->bit_rate = pInCodecPar->bit_rate;
    (*pOutCodecCtx)->extradata = pInCodecPar->extradata;
    (*pOutCodecCtx)->extradata_size = pInCodecPar->extradata_size;
    (*pOutCodecCtx)->width = pInCodecCtx->width;
    (*pOutCodecCtx)->height = pInCodecCtx->height;


    if (av_q2d(pInCodecCtx->time_base) * pInCodecCtx->ticks_per_frame > av_q2d(pInStream->time_base) &&
        av_q2d(pInStream->time_base) < 1.0 / 1000) {
        (*pOutCodecCtx)->time_base = pInCodecCtx->time_base;
        (*pOutCodecCtx)->time_base.num *= pInCodecCtx->ticks_per_frame;
    } else {
        (*pOutCodecCtx)->time_base = pInStream->time_base;
    }

    // cout << "addStream 5" << endl;


    switch (pInCodecCtx->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            // cout << "AUDIO CODEC" << endl;
            (*pOutCodecCtx)->channel_layout = pInCodecCtx->channel_layout;
            (*pOutCodecCtx)->sample_rate = pInCodecCtx->sample_rate;
            (*pOutCodecCtx)->channels = pInCodecCtx->channels;
            (*pOutCodecCtx)->frame_size = pInCodecCtx->frame_size;
            if ((pInCodecCtx->block_align == 1 && pInCodecCtx->codec_id == AV_CODEC_ID_MP3) ||
                pInCodecCtx->codec_id == AV_CODEC_ID_AC3) {
                (*pOutCodecCtx)->block_align = 0;
            } else {
                (*pOutCodecCtx)->block_align = pInCodecCtx->block_align;
            }
            break;
        case AVMEDIA_TYPE_VIDEO:
            // cout << "VIDEO CODEC" << endl;
            (*pOutCodecCtx)->pix_fmt = pInCodecCtx->pix_fmt;
            (*pOutCodecCtx)->width = pInCodecCtx->width;
            (*pOutCodecCtx)->height = pInCodecCtx->height;
            (*pOutCodecCtx)->has_b_frames = pInCodecCtx->has_b_frames;

            if (pFormatCtx->oformat->flags & AVFMT_GLOBALHEADER) {
                (*pOutCodecCtx)->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            }
            break;
        default:
            break;
    }

    avcodec_parameters_from_context(pOutStream->codecpar, *pOutCodecCtx);

    return pOutStream;
}

int main(int argc, char **argv) {
    string input;
    string outputPrefix;

    AVInputFormat *pInFormat = NULL;
    AVOutputFormat *pOutFormat = NULL;
    AVFormatContext *pInFormatCtx = NULL;
    AVFormatContext *pOutFormatCtx = NULL;
    AVStream *pVideoStream = NULL;
    AVCodecContext *pVideoCodecContext = NULL;
    AVCodecContext *pAudioCodecContext = NULL;
    AVCodec *pCodec = NULL;
    int videoIndex;
    int audioIndex;
    int decodeDone;
    int ret;
    unsigned int i;

    if (argc != 4) {
        cout << "Usage: " << argv[0] << " <input MPEG-TS file> <offset time> <output MPEG-TS file>" << endl;
        exit(1);
    }

    // cout << "start" << endl;

    av_register_all();

    input = argv[1];
    if (!strcmp(input.c_str(), "-")) {
        input = "pipe:";
    }
    double offsetTime = atof(argv[2]);
    unsigned int tsShift = offsetTime * 90000; // h264 defined sample rate is 90khz
    string outFile = argv[3];

    // cout << "0" << endl;

    pInFormat = av_find_input_format("mpegts");
    if (!pInFormat) {
        cout << "Could not find MPEG-TS demuxer" << endl;
        exit(1);
    }
    // cout << "0.5" << endl;

    ret = avformat_open_input(&pInFormatCtx, input.c_str(), pInFormat, NULL);
    if (ret != 0) {
        cout << "Could not open input file, make sure it is an mpegts file: " << ret << endl;
        exit(1);
    }
    //pInFormatCtx->max_analyze_duration = 1000000;
    // cout << "0.6" << endl;

    if (avformat_find_stream_info(pInFormatCtx, NULL) < 0) {
        cout << "Could not read stream information" << endl;
        exit(1);
    }
    // cout << "0.7" << endl;
    av_dump_format(pInFormatCtx, 0, argv[1], 0);
    // cout << "0.75" << endl;

    pOutFormat = av_guess_format("mpegts", NULL, NULL);
    if (!pOutFormat) {
        cout << "Could not find MPEG-TS muxer" << endl;
        exit(1);
    }
    // cout << "0.8" << endl;

    pOutFormatCtx = avformat_alloc_context();
    if (!pOutFormatCtx) {
        cout << "Could not allocated output context" << endl;
        exit(1);
    }
    // cout << "0.9" << endl;


    pOutFormatCtx->oformat = pOutFormat;

    videoIndex = -1;
    audioIndex = -1;

    // cout << "pInFormatCtx->nb_streams " << pInFormatCtx->nb_streams << endl;

    for (i = 0; i < pInFormatCtx->nb_streams && (videoIndex < 0 || audioIndex < 0); i++) {
        // skipping bad streams again again
        if (pInFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            int64_t durationInt = pInFormatCtx->duration;
            double durationSeconds = (double) durationInt / AV_TIME_BASE;
            double fps = av_q2d(pInFormatCtx->streams[i]->avg_frame_rate);

            unsigned int frameCount = 0;
            if (pInFormatCtx->streams[i]->nb_frames > 0) {
                frameCount = pInFormatCtx->streams[i]->nb_frames;
            } else {
                frameCount = floor(durationSeconds * fps);
            }
            if (frameCount <= 0) continue;
        }

        switch (pInFormatCtx->streams[i]->codecpar->codec_type) {
            case AVMEDIA_TYPE_VIDEO:
                cout << "Initial for loop adding VIDEO CODEC" << endl;
                videoIndex = i;
                pInFormatCtx->streams[i]->discard = AVDISCARD_NONE;

                pVideoStream = addStream(pOutFormatCtx, pInFormatCtx->streams[i], &pVideoCodecContext);

                break;
            case AVMEDIA_TYPE_AUDIO:
                cout << "Initial for loop adding AUDIO CODEC" << endl;
                audioIndex = i;

                pInFormatCtx->streams[i]->discard = AVDISCARD_NONE;
                if (!pInFormatCtx->streams[i]->codecpar->channels) {
                    cout << "WARNING: No channels found (skipping) audio stream " << i << endl;
                } else {
                    addStream(pOutFormatCtx, pInFormatCtx->streams[i], &pAudioCodecContext);
                }

                break;
            default:
                // cout << "Initial for loop AVDISCARD_ALL" << endl;
                pInFormatCtx->streams[i]->discard = AVDISCARD_ALL;

                break;
        }
    }

    pCodec = avcodec_find_decoder(pVideoStream->codecpar->codec_id);
    if (!pCodec) {
        cout << "Could not find video decoder, key frames will not be honored" << endl;
    }

    if (avcodec_open2(pVideoCodecContext, pCodec, NULL) < 0) {
        cout << "Could not open video decoder, key frames will not be honored" << endl;
    }

    if (avio_open(&pOutFormatCtx->pb, outFile.c_str(), AVIO_FLAG_WRITE) < 0) {
        cout << "Could not open " << outFile << endl;
        exit(1);
    }

    if (avformat_write_header(pOutFormatCtx, NULL)) {
        cout << "Could not write mpegts header to first output file" << endl;
        exit(1);
    }

    const unsigned long MAX_PACKETS = 1000000;
    unsigned long iPacket = 0;
    bool initZeros = true;
    int64_t dtsZero = 0;
    int64_t ptsZero = 0;
    double firstDtsTime = 0;
    bool initFirstDTSTime = true;
    int firstPacketStreamIndex = videoIndex;

    // find first video packet time
    do {
        // double segmentTime;
        AVPacket packet;
        av_init_packet(&packet);

        decodeDone = av_read_frame(pInFormatCtx, &packet);

        if (decodeDone < 0) {
            break;
        }

        if (initFirstDTSTime) {
            firstDtsTime = packet.dts;
            initFirstDTSTime = false;
            firstPacketStreamIndex = packet.stream_index;
        }


        int iStreamIndex = packet.stream_index;
        int isVideo = iStreamIndex == videoIndex;

        if (initZeros && isVideo) {
            initZeros = false;
            ptsZero = packet.pts;
            dtsZero = packet.dts;
            if (dtsZero < ptsZero) dtsZero = ptsZero;
        }

        av_packet_unref(&packet);
        if (iPacket++ >= MAX_PACKETS) break;

    } while (initZeros);

    // int isFirstAudio = firstPacketStreamIndex == audioIndex;
    // int isFirstVideo = firstPacketStreamIndex == videoIndex;    

    // cout << "found ptsZero and dtsZero " << ptsZero/90000 << " " << dtsZero/90000;
    // cout << " first packet dts time " << firstDtsTime/90000 << ", is first stream index audio: " << isFirstAudio;
    // cout << " video: " << isFirstVideo << endl;

    // flush buffers before seek
    avcodec_flush_buffers(pVideoCodecContext);

    // seek beginning of file
    av_seek_frame(pInFormatCtx, firstPacketStreamIndex, firstDtsTime, AVSEEK_FLAG_BACKWARD);

    // cout << "rewound file to beginning" << endl;

    do {
        // double segmentTime;
        AVPacket input_packet;
        AVPacket output_packet;
        av_init_packet(&input_packet);

        decodeDone = av_read_frame(pInFormatCtx, &input_packet);

        if (decodeDone < 0) {
            break;
        }

        if (av_packet_ref(&output_packet, &input_packet) < 0) {
            cout << "Could not duplicate packet" << endl;
            av_packet_unref(&input_packet);
            break;
        }

        int iStreamIndex = input_packet.stream_index;
        int isAudio = iStreamIndex == audioIndex;
        int isVideo = iStreamIndex == videoIndex;

        // cout << "A/V type " << isAudio << "/" << isVideo << " before input_packet pts dts " << (double)input_packet.pts/90000 << " " << (double)input_packet.dts/90000;
        if (isVideo) {
            output_packet.pts = input_packet.pts - ptsZero + tsShift;
            output_packet.dts = input_packet.dts - dtsZero + tsShift;
        } else if (isAudio) {
            if ((input_packet.pts - dtsZero + tsShift) > 0.0 && (input_packet.dts - dtsZero + tsShift) > 0.0) {
                output_packet.pts = input_packet.pts - ptsZero + tsShift;
                output_packet.dts = input_packet.dts - dtsZero + tsShift;
            } else {
                cout << "audio pts or dts would be negative, skipping pts,dts " << input_packet.pts - ptsZero + tsShift;
                cout << "," << input_packet.dts - dtsZero + tsShift << endl;
                av_packet_unref(&input_packet);
                continue;
            }
        }
        // cout << " after input_packet pts dts " << (double)input_packet.pts/90000 << " " << (double)input_packet.dts/90000 << endl;


        ret = av_interleaved_write_frame(pOutFormatCtx, &output_packet);
        if (ret < 0) {
            cout << "Warning: Could not write frame of stream" << endl;
        } else if (ret > 0) {
            //cout <<  "End of stream requested" << endl;
            av_packet_unref(&input_packet);
            av_packet_unref(&output_packet);
            break;
        }

        av_packet_unref(&input_packet);
        av_packet_unref(&output_packet);

        if (iPacket++ >= MAX_PACKETS) break;

    } while (!decodeDone);

    av_write_trailer(pOutFormatCtx);

    avcodec_close(pVideoCodecContext);

    for (i = 0; i < pOutFormatCtx->nb_streams; i++) {
//        av_freep(&pOutFormatCtx->streams[i]->codec);
        av_freep(&pOutFormatCtx->streams[i]);
    }

    avio_close(pOutFormatCtx->pb);
    av_free(pOutFormatCtx);


    return 0;
}
