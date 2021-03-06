/* Webcamoid, webcam capture application.
 * Copyright (C) 2011-2017  Gonzalo Exequiel Pedone
 *
 * Webcamoid is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Webcamoid is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Webcamoid. If not, see <http://www.gnu.org/licenses/>.
 *
 * Web-Site: http://webcamoid.github.io/
 */

#include "audiostream.h"

// No AV correction is done if too big error.
#define AV_NOSYNC_THRESHOLD 10.0

// Maximum audio speed change to get correct sync
#define SAMPLE_CORRECTION_PERCENT_MAX 10

// We use about AUDIO_DIFF_AVG_NB A-V differences to make the average
#define AUDIO_DIFF_AVG_NB 20

typedef QMap<AVSampleFormat, AkAudioCaps::SampleFormat> SampleFormatMap;

inline SampleFormatMap initSampleFormatMap()
{
    SampleFormatMap sampleFormat = {
        {AV_SAMPLE_FMT_U8 , AkAudioCaps::SampleFormat_u8 },
        {AV_SAMPLE_FMT_S16, AkAudioCaps::SampleFormat_s16},
        {AV_SAMPLE_FMT_S32, AkAudioCaps::SampleFormat_s32},
        {AV_SAMPLE_FMT_FLT, AkAudioCaps::SampleFormat_flt}
    };

    return sampleFormat;
}

Q_GLOBAL_STATIC_WITH_ARGS(SampleFormatMap, sampleFormats, (initSampleFormatMap()))

typedef QMap<uint64_t, AkAudioCaps::ChannelLayout> ChannelLayoutsMap;

inline ChannelLayoutsMap initChannelFormatsMap()
{
    ChannelLayoutsMap channelLayouts = {
        {AV_CH_LAYOUT_MONO  , AkAudioCaps::Layout_mono  },
        {AV_CH_LAYOUT_STEREO, AkAudioCaps::Layout_stereo}
    };

    return channelLayouts;
}

Q_GLOBAL_STATIC_WITH_ARGS(ChannelLayoutsMap, channelLayouts, (initChannelFormatsMap()))

AudioStream::AudioStream(const AVFormatContext *formatContext,
                         uint index, qint64 id, Clock *globalClock,
                         bool noModify, QObject *parent):
    AbstractStream(formatContext, index, id, globalClock, noModify, parent)
{
    this->m_maxData = 9;
    this->m_pts = 0;
    this->m_resampleContext = NULL;
    this->audioDiffCum = 0.0;
    this->audioDiffAvgCoef = exp(log(0.01) / AUDIO_DIFF_AVG_NB);
    this->audioDiffAvgCount = 0;
}

AudioStream::~AudioStream()
{
    if (this->m_resampleContext)
        swr_free(&this->m_resampleContext);
}

AkCaps AudioStream::caps() const
{
    AVSampleFormat iFormat = AVSampleFormat(this->codecContext()->sample_fmt);
    AVSampleFormat oFormat = av_get_packed_sample_fmt(iFormat);
    oFormat = sampleFormats->contains(oFormat)? oFormat: AV_SAMPLE_FMT_FLT;

    AkAudioCaps::ChannelLayout layout = channelLayouts->value(this->codecContext()->channel_layout,
                                                              AkAudioCaps::Layout_stereo);

    uint64_t channelLayout = channelLayouts->key(layout, AV_CH_LAYOUT_STEREO);

    AkAudioCaps caps;
    caps.isValid() = true;
    caps.format() = sampleFormats->value(oFormat);;
    caps.bps() = 8 * av_get_bytes_per_sample(oFormat);
    caps.channels() = av_get_channel_layout_nb_channels(channelLayout);
    caps.rate() = this->codecContext()->sample_rate;
    caps.layout() = layout;
    caps.align() = false;

    return caps.toCaps();
}

void AudioStream::processPacket(AVPacket *packet)
{
    if (!this->isValid())
        return;

    if (!packet) {
        this->dataEnqueue(reinterpret_cast<AVFrame *>(NULL));

        return;
    }

    if (avcodec_send_packet(this->codecContext(), packet) < 0)
        return;

    forever {
        AVFrame *iFrame = av_frame_alloc();

        if (avcodec_receive_frame(this->codecContext(), iFrame) < 0) {
            av_frame_free(&iFrame);

            break;
        }

        this->dataEnqueue(iFrame);
    }
}

void AudioStream::processData(AVFrame *frame)
{
    frame->pts = frame->pts != AV_NOPTS_VALUE? frame->pts: this->m_pts;
    AkPacket oPacket = this->convert(frame);
    emit this->oStream(oPacket);
    emit this->frameSent();

    this->m_pts = frame->pts + frame->nb_samples;
}

AkPacket AudioStream::convert(AVFrame *iFrame)
{
    // Synchronize audio
    qreal pts = iFrame->pts * this->timeBase().value();
    qreal diff = pts - this->globalClock()->clock();
    int wantedSamples = iFrame->nb_samples;
    int oSampleRate = iFrame->sample_rate;

    if (!qIsNaN(diff) && qAbs(diff) < AV_NOSYNC_THRESHOLD) {
        this->audioDiffCum = diff + this->audioDiffAvgCoef * this->audioDiffCum;

        if (this->audioDiffAvgCount < AUDIO_DIFF_AVG_NB) {
            // not enough measures to have a correct estimate
            this->audioDiffAvgCount++;
        } else {
            // estimate the A-V difference
            qreal avgDiff = this->audioDiffCum * (1.0 - this->audioDiffAvgCoef);

            // since we do not have a precise anough audio fifo fullness,
            // we correct audio sync only if larger than this threshold
            qreal diffThreshold = 2.0 * iFrame->nb_samples / iFrame->sample_rate;

            if (qAbs(avgDiff) >= diffThreshold) {
                wantedSamples = iFrame->nb_samples + int(diff * iFrame->sample_rate);
                int minSamples = iFrame->nb_samples * (100 - SAMPLE_CORRECTION_PERCENT_MAX) / 100;
                int maxSamples = iFrame->nb_samples * (100 + SAMPLE_CORRECTION_PERCENT_MAX) / 100;
                wantedSamples = qBound(minSamples, wantedSamples, maxSamples);

                if (wantedSamples != iFrame->nb_samples) {
                    if (swr_set_compensation(this->m_resampleContext,
                                             wantedSamples - iFrame->nb_samples,
                                             wantedSamples) < 0) {
                        return AkPacket();
                    }
                }
            }
        }
    } else {
        // Too big difference: may be initial PTS errors, so
        // reset A-V filter
        this->audioDiffAvgCount = 0;
        this->audioDiffCum = 0.0;
    }

    if (qAbs(diff) >= AV_NOSYNC_THRESHOLD)
        this->globalClock()->setClock(pts);

    this->m_clockDiff = diff;

    uint64_t oLayout = channelLayouts->contains(iFrame->channel_layout)?
                          iFrame->channel_layout:
                          AV_CH_LAYOUT_STEREO;
    int oChannels = av_get_channel_layout_nb_channels(oLayout);

    AVSampleFormat iFormat = AVSampleFormat(iFrame->format);
    AVSampleFormat oFormat = av_get_packed_sample_fmt(iFormat);
    oFormat = sampleFormats->contains(oFormat)? oFormat: AV_SAMPLE_FMT_FLT;

    this->m_resampleContext =
            swr_alloc_set_opts(this->m_resampleContext,
                               int64_t(oLayout),
                               oFormat,
                               oSampleRate,
                               int64_t(iFrame->channel_layout),
                               iFormat,
                               iFrame->sample_rate,
                               0,
                               NULL);

    if (!this->m_resampleContext)
        return AkPacket();

    AVFrame oFrame;
    memset(&oFrame, 0, sizeof(AVFrame));
    oFrame.format = oFormat;
    oFrame.channels = oChannels;
    oFrame.channel_layout = oLayout;
    oFrame.sample_rate = oSampleRate;
    oFrame.nb_samples = wantedSamples;
    oFrame.pts = iFrame->pts;

    // Calculate the size of the audio buffer.
    int frameSize = av_samples_get_buffer_size(oFrame.linesize,
                                               oFrame.channels,
                                               oFrame.nb_samples,
                                               oFormat,
                                               1);

    QByteArray oBuffer(frameSize, Qt::Uninitialized);

    if (avcodec_fill_audio_frame(&oFrame,
                                 oFrame.channels,
                                 oFormat,
                                 reinterpret_cast<const uint8_t *>(oBuffer.constData()),
                                 oBuffer.size(),
                                 1) < 0) {
        return AkPacket();
    }

    int error = swr_convert_frame(this->m_resampleContext,
                                  &oFrame,
                                  iFrame);

    if (error < 0) {
        char errorStr[1024];
        av_strerror(AVERROR(error), errorStr, 1024);
        qDebug() << "Error converting audio:" << errorStr;

        return AkPacket();
    }

    AkAudioPacket packet;
    packet.caps().isValid() = true;
    packet.caps().format() = sampleFormats->value(oFormat);
    packet.caps().bps() = 8 * av_get_bytes_per_sample(oFormat);
    packet.caps().channels() = oChannels;
    packet.caps().rate() = iFrame->sample_rate;
    packet.caps().layout() = channelLayouts->value(oLayout);
    packet.caps().samples() = oFrame.nb_samples;
    packet.caps().align() = false;

    packet.buffer() = oBuffer;
    packet.pts() = iFrame->pts;
    packet.timeBase() = this->timeBase();
    packet.index() = int(this->index());
    packet.id() = this->id();

    return packet.toPacket();
}
