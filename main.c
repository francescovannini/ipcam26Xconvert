// Copyright (C) 2022 Francesco Vannini
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
// Inspired by https://spitzner.org/kkmoon.html
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <getopt.h>
#include <libgen.h>
#include <libavutil/opt.h>
#include <libavutil/mathematics.h>
#include <libavutil/timestamp.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include "ipcamvideofilefmt.h"

#define MAX_EXTENSION_LEN   12

size_t ReadToBuffer(FILE *fp_src, uint8_t **dest, size_t dest_offset, unsigned long length, size_t *dest_size) {

    if (length == 0) {
        return 0;
    }

    // Resize the buffer if needed
    if (*dest && (*dest_size < length + dest_offset)) {
        if (dest_offset) { // appending data to a previous read, we need to keep the data
            *dest = realloc(*dest, length + dest_offset);
            if (*dest == NULL) {
                fprintf(stderr, "Cannot re-allocate memory, aborting.\n");
                exit(1);
            }
        } else { // we just need a larger buffer, we can discard existing data if any
            free(*dest);
            *dest = NULL;
        }
    }

    // Allocate the buffer if needed
    if (*dest == NULL) {
        *dest = malloc(length + dest_offset);
        if (*dest == NULL) {
            fprintf(stderr, "Cannot allocate memory, aborting.\n");
            exit(1);
        }
    }

    *dest_size = length + dest_offset;
    return fread(*dest + dest_offset, 1, length, fp_src);
}

bool EndsWith(const char *str, const char *suffix) {
    if (!str || !suffix) {
        return false;
    }
    size_t lenstr = strlen(str);
    size_t lensuffix = strlen(suffix);
    if (lensuffix > lenstr) {
        return false;
    }
    return strncmp(str + lenstr - lensuffix, suffix, lensuffix) == 0;
}

void ShowHelp(char *command, int exitcode) {
    fprintf(stderr, "Convert surveillance cameras \".264\" files into any a/v format supported by LibAV/FFMpeg.\n");
    fprintf(stderr, "Usage: %s [-n] [-f format_name] [-q] input.264 [output.fmt]\n", basename(command));
    fprintf(stderr, "  -n              Ignore audio data\n");
    fprintf(stderr, "  -f format_name  Force output format to format_name (ex: -f matroska)\n");
    fprintf(stderr, "  -q              Quiet output. Only print errors.\n");
    fprintf(stderr, "  input.264       Input video file as produced by camera\n");
    fprintf(stderr, "  output.fmt      Output file. Format is guessed by extension (ex: output.mkv\n");
    fprintf(stderr, "                  will produce a Matroska file). If no output file is specified\n");
    fprintf(stderr, "                  one will be generated based on input file and the default\n");
    fprintf(stderr, "                  extension associated with the format provided through -f.\n");
    fprintf(stderr, "                  Note that you have to provide at least a valid output file\n");
    fprintf(stderr, "                  extension or a format name through -f option.\n");
    fprintf(stderr, "\nAvailable output formats and codecs depend on system LibAV/FFMpeg libraries.\n");
    exit(exitcode);
}

int main(int argc, char *argv[]) {
    int opt;
    bool skip_audio = false;
    bool quiet = false;
    char *format_name = NULL;
    while ((opt = getopt(argc, argv, ":nqf:")) != -1) {
        switch (opt) {
            case 'n':
                skip_audio = true;
                break;

            case 'q':
                quiet = true;
                break;

            case 'f':
                format_name = optarg;
                break;

            default:
                ShowHelp(argv[0], EXIT_FAILURE);
        }
    }

    // Not enough params
    if (optind >= argc) {
        ShowHelp(argv[0], EXIT_FAILURE);
    }

    char *in_filename = argv[optind++];
    if ((optind >= argc) && !format_name) {
        ShowHelp(argv[0], EXIT_FAILURE);
    }

    av_log_set_level(AV_LOG_ERROR);
    av_register_all();
    avcodec_register_all();
    int retval;

    // Init format_ctx based on format name
    AVFormatContext *format_ctx;
    if (format_name) {
        if ((retval = avformat_alloc_output_context2(&format_ctx, NULL, format_name, NULL)) < 0) {
            fprintf(stderr, "Could not allocate an output context: %s\n", av_err2str(retval));
            exit(1);
        }

        if (optind < argc) {
            sprintf(format_ctx->filename, "%s", argv[optind]);
        } else {
            // Generate output file name based on default format extension
            char ext[MAX_EXTENSION_LEN] = ".";
            size_t extensions_length;
            if ((extensions_length = strlen(format_ctx->oformat->extensions)) > 0) {
                char *extensions = malloc(extensions_length + 1);
                char *extension_orig = extensions;
                strcpy(extensions, format_ctx->oformat->extensions);
                extensions = strtok(extensions, ",");
                strncpy(&ext[1], extensions, MAX_EXTENSION_LEN - 1);
                free(extension_orig);
            } else {
                sprintf(&ext[1], "out");
                if (!quiet) {
                    fprintf(stderr, "No default extension for the selected format, using '.out'\n");
                }
            }

            if (EndsWith(in_filename, ".264")) {
                strncpy(format_ctx->filename, in_filename, strlen(in_filename) - 4);
            } else {
                strcat(format_ctx->filename, in_filename);
            }
            strcat(format_ctx->filename, ext);
            if (!quiet) {
                fprintf(stderr, "Output file is %s\n", format_ctx->filename);
            }
        }
    } else {
        if ((retval = avformat_alloc_output_context2(&format_ctx, NULL, NULL, argv[optind])) < 0) {
            fprintf(stderr, "Could not allocate an output context: %s\n", av_err2str(retval));
            exit(1);
        }
    }

    FILE *in_file;
    if (!(in_file = fopen(in_filename, "rb"))) {
        fprintf(stderr, "Cannot open %s for reading.\n", in_filename);
        exit(1);
    }

    // First pass over input file to detect video frame and audio sample rates and video size
    bool hxfi_detected = false;
    HXFrame_t hx_frame;
    int video_w, video_h;
    double video_avg_frame_rate = 0;
    double audio_avg_sample_rate = 0;
    long video_ts_initial = -1, audio_ts_initial = -1;
    long video_ts_prev = -1, audio_ts_prev = -1;
    long audio_packets_count = 0, video_packets_count = 0;
    do {

        if (fread(&hx_frame.header, 1, sizeof(hx_frame.header), in_file) != sizeof(hx_frame.header)) {
            fprintf(stderr, "Premature end of file, aborting.\n");
            exit(1);
        }

        switch (hx_frame.header) {

            case HXVS:
                if (fread(&hx_frame.data, 1, sizeof(HXVSFrame_t), in_file) != sizeof(HXVSFrame_t)) {
                    fprintf(stderr, "Premature end of file, aborting.\n");
                    exit(1);
                }

                video_w = (int) hx_frame.data.hxvs.width;
                video_h = (int) hx_frame.data.hxvs.height;

                if (!quiet) {
                    fprintf(stderr, "Reported video dimensions: %d x %d\n", video_w, video_h);
                }
                break;

            case HXVF:
                if (fread(&hx_frame.data, 1, sizeof(HXVFFrame_t), in_file) != sizeof(HXVFFrame_t)) {
                    fprintf(stderr, "Premature end of file, aborting.\n");
                    exit(1);
                }

                if (video_ts_initial == -1) {
                    video_ts_initial = hx_frame.data.hxvf.timestamp;
                    video_ts_prev = hx_frame.data.hxvf.timestamp;
                } else {
                    long elapsed, timestamp;
                    timestamp = hx_frame.data.hxvf.timestamp - video_ts_initial;
                    if (timestamp > video_ts_prev) {
                        elapsed = timestamp - video_ts_prev;
                        if (video_packets_count) {
                            video_avg_frame_rate =
                                    (video_avg_frame_rate * (double) video_packets_count +
                                     (1000.0f / (double) elapsed)) /
                                    ((double) video_packets_count + 1);
                        } else {
                            video_avg_frame_rate = 1000.0f / (double) elapsed;
                        }
                        video_packets_count++;
                    }
                    video_ts_prev = timestamp;
                }

                if (fseek(in_file, hx_frame.data.hxvf.length, SEEK_CUR) < 0) {
                    fprintf(stderr, "Premature end of file, aborting.\n");
                    exit(1);
                }
                break;

            case HXAF:
                if (fread(&hx_frame.data, 1, sizeof(HXAFFrame_t), in_file) != sizeof(HXAFFrame_t)) {
                    fprintf(stderr, "Premature end of file, aborting.\n");
                    exit(1);
                }

                if (audio_ts_initial == -1) {
                    audio_ts_initial = hx_frame.data.hxaf.timestamp;
                    audio_ts_prev = hx_frame.data.hxaf.timestamp;
                } else {
                    long elapsed, timestamp;
                    timestamp = hx_frame.data.hxaf.timestamp - audio_ts_initial;
                    if (timestamp > audio_ts_prev) {
                        elapsed = timestamp - audio_ts_prev;
                        if (audio_packets_count) {
                            audio_avg_sample_rate = ((audio_avg_sample_rate * (double) audio_packets_count) +
                                                     ((hx_frame.data.hxaf.length - 4) / (double) elapsed)) /
                                                    ((double) audio_packets_count + 1);
                        } else {
                            audio_avg_sample_rate = (hx_frame.data.hxaf.length - 4) / (double) elapsed;
                        }
                        audio_packets_count++;
                    }
                    audio_ts_prev = timestamp;
                }

                if (fseek(in_file, hx_frame.data.hxaf.length - 4, SEEK_CUR) < 0) {
                    fprintf(stderr, "Premature end of file, aborting.\n");
                    exit(1);
                }
                break;

            case HXFI:
                hxfi_detected = true;
                break;

            default:
                fprintf(stderr, "Unknown audio_frame header: %u.\n", hx_frame.header);
                break;
        }

    } while ((!feof(in_file)) && (!hxfi_detected));

    if (fseek(in_file, 0, SEEK_SET) < 0) {
        fprintf(stderr, "Cannot seek back to beginning of file, aborting.\n");
        exit(1);
    }

    if (video_avg_frame_rate <= 0) {
        fprintf(stderr, "No video detected, aborting.\n");
        exit(1);
    }

    AVOutputFormat *out_fmt = format_ctx->oformat;
    if (!quiet) {
        if (format_ctx->oformat->mime_type) {
            fprintf(stderr, "Selected output format: %s (%s)\n", format_ctx->oformat->long_name,
                    format_ctx->oformat->mime_type);
        } else {
            fprintf(stderr, "Selected output format: %s\n", format_ctx->oformat->long_name);
        }
    }

    // Video stream. Video codec is only used to generate a valid header, not for actual encoding
    AVCodec *v_codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    AVStream *v_stream = avformat_new_stream(format_ctx, v_codec);
    if (!v_stream) {
        fprintf(stderr, "Could not allocate stream.\n");
        exit(1);
    }

    AVCodecContext *v_encoder = avcodec_alloc_context3(v_codec);
    v_encoder->time_base = (AVRational) {1, 1000}; // Raw stream timestamps are in milliseconds
    v_encoder->framerate = (AVRational) {(int) round(video_avg_frame_rate), 1};
    v_encoder->pix_fmt = AV_PIX_FMT_YUV420P;
    v_encoder->width = video_w;
    v_encoder->height = video_h;
    if (format_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
        v_encoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    if ((retval = avcodec_open2(v_encoder, v_codec, NULL)) < 0) {
        fprintf(stderr, "Could not open codec: %s\n", av_err2str(retval));
        exit(1);
    }

    if ((retval = avcodec_parameters_from_context(v_stream->codecpar, v_encoder)) < 0) {
        fprintf(stderr, "Could not set video stream parameters: %s\n", av_err2str(retval));
        exit(1);
    }

    // We only need the encoder for parameters
    avcodec_free_context(&v_encoder);

    v_stream->avg_frame_rate = (AVRational) {(int) round(video_avg_frame_rate), 1};
    v_stream->nb_frames = video_packets_count;
    v_stream->id = (int) format_ctx->nb_streams - 1; // Last stream

    if (!quiet) {
        fprintf(stderr, "Detected video frame rate: %d\n", (int) av_q2d(v_stream->avg_frame_rate));
    }

    // Audio stream
    AVCodec *a_codec = NULL;
    AVStream *a_stream = NULL;
    if (!skip_audio) {
        a_codec = avcodec_find_encoder(AV_CODEC_ID_PCM_ALAW);
        if (audio_avg_sample_rate <= 0) {
            fprintf(stderr, "Warning! No audio detected.\n");
        } else {
            // Audio stream
            a_stream = avformat_new_stream(format_ctx, a_codec);
            if (!a_stream) {
                fprintf(stderr, "Could not allocate stream.\n");
                exit(1);
            }

            AVCodecContext *a_encoder = avcodec_alloc_context3(a_codec);
            a_encoder->time_base = (AVRational) {1, 1000}; // Raw stream timestamps are in milliseconds
            a_encoder->sample_rate = (int) round(audio_avg_sample_rate * 1000.0f);
            a_encoder->sample_fmt = AV_SAMPLE_FMT_S16;
            a_encoder->channel_layout = AV_CH_LAYOUT_MONO;
            a_encoder->channels = 1;

            if (format_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
                a_encoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            }

            if ((retval = avcodec_open2(a_encoder, a_codec, NULL)) < 0) {
                fprintf(stderr, "Could not open codec: %s\n", av_err2str(retval));
                exit(1);
            }

            if ((retval = avcodec_parameters_from_context(a_stream->codecpar, a_encoder)) < 0) {
                fprintf(stderr, "Could not set audio stream parameters: %s\n", av_err2str(retval));
                exit(1);
            }

            // We only need the encoder for parameters
            avcodec_free_context(&a_encoder);

            a_stream->id = (int) format_ctx->nb_streams - 1; //Last stream
            if (!quiet) {
                fprintf(stderr, "Detected audio PCM frequency: %d\n", a_stream->codecpar->sample_rate);
            }

        }
    } else {
        if (!quiet) {
            fprintf(stderr, "Audio processing is disabled.\n");
        }
    }

    // Open output file and write header
    if (!(out_fmt->flags & AVFMT_NOFILE)) {
        if ((retval = avio_open(&(format_ctx->pb), format_ctx->filename, AVIO_FLAG_WRITE)) < 0) {
            fprintf(stderr, "Could not open output file: %s\n", av_err2str(retval));
            exit(1);
        }
    }

    if ((retval = avformat_write_header(format_ctx, NULL)) < 0) {
        fprintf(stderr, "Error occurred when opening output file: %s\n", av_err2str(retval));
        exit(1);
    }

    // Main extraction loop
    uint8_t *packet_buffer = NULL;
    size_t packet_buffer_length = 0;
    int packet_buffer_offset = 0;
    hxfi_detected = false;
    AVPacket packet;
    av_init_packet(&packet);
    do {
        if (fread(&hx_frame.header, 1, sizeof(hx_frame.header), in_file) != sizeof(hx_frame.header)) {
            fprintf(stderr, "Premature end of file, aborting.\n");
            exit(1);
        }

        switch (hx_frame.header) {

            case HXVS:
                if (fseek(in_file, sizeof(HXVSFrame_t), SEEK_CUR) < 0) {
                    fprintf(stderr, "Seek error, aborting.\n");
                    exit(1);
                }
                break;

            case HXVF:
                if (fread(&hx_frame.data, 1, sizeof(HXVFFrame_t), in_file) != sizeof(HXVFFrame_t)) {
                    fprintf(stderr, "Premature end of file, aborting.\n");
                    exit(1);
                }

                retval = (int) ReadToBuffer(in_file, &packet_buffer, packet_buffer_offset,
                                            hx_frame.data.hxvf.length, &packet_buffer_length);

                if (retval < hx_frame.data.hxvf.length) {
                    fprintf(stderr, "Premature end of file, aborting.\n");
                    exit(1);
                }

                H264_Nal_Header_t *nal_header = (H264_Nal_Header_t *) (packet_buffer + packet_buffer_offset);

                if (nal_header->unit_type == 7 || nal_header->unit_type == 8) {
                    packet_buffer_offset += retval; // enqueue data in buffer, wait for a different type to write a packet
                } else {
                    packet.data = packet_buffer;
                    packet.size = retval + packet_buffer_offset;
                    packet_buffer_offset = 0;
                    packet.stream_index = v_stream->index;
                    packet.pts = packet.dts = (int) round((double) (hx_frame.data.hxvf.timestamp - video_ts_initial) /
                                                          (1000.0f * av_q2d(v_stream->time_base)));
                    if ((retval = av_interleaved_write_frame(format_ctx, &packet)) < 0) {
                        fprintf(stderr, "Error while writing output packet: %s\n", av_err2str(retval));
                        exit(1);
                    }
                }
                break;

            case HXAF:
                if (fread(&hx_frame.data, 1, sizeof(HXAFFrame_t), in_file) != sizeof(HXAFFrame_t)) {
                    fprintf(stderr, "Premature end of file, aborting.\n");
                    exit(1);
                }

                if (a_stream) {
                    retval = (int) ReadToBuffer(in_file, &packet_buffer, 0,
                                                hx_frame.data.hxaf.length - 4, &packet_buffer_length);

                    if (retval < hx_frame.data.hxaf.length - 4) {
                        fprintf(stderr, "Premature end of file, aborting.\n");
                        exit(1);
                    }

                    packet.data = packet_buffer;
                    packet.size = retval;
                    packet.stream_index = a_stream->index;
                    packet.pts = packet.dts = (int) round((double) (hx_frame.data.hxaf.timestamp - audio_ts_initial) /
                                                          (1000.0f * av_q2d(a_stream->time_base)));
                    if ((retval = av_interleaved_write_frame(format_ctx, &packet)) < 0) {
                        fprintf(stderr, "Error while writing output packet: %s\n", av_err2str(retval));
                        exit(1);
                    }
                } else {
                    if (fseek(in_file, hx_frame.data.hxaf.length - 4, SEEK_CUR) < 0) {
                        fprintf(stderr, "Seek error, aborting");
                        exit(1);
                    }
                }
                break;

            case HXFI:
                hxfi_detected = true;
                break;

            default:
                fprintf(stderr, "Unknown audio_frame header: %u\n", hx_frame.header);
                break;
        }
    } while ((!feof(in_file)) && (!hxfi_detected));
    fclose(in_file);

    av_write_trailer(format_ctx);
    if (!(out_fmt->flags & AVFMT_NOFILE)) {
        avio_closep(&format_ctx->pb);
    }
    avformat_free_context(format_ctx);

    if (packet_buffer) {
        free(packet_buffer);
    }

    if (!quiet) {
        fprintf(stderr, "Done! Parsed %lu video packet and %lu audio packets.\n", video_packets_count,
                audio_packets_count);
    }

    return 0;
}
