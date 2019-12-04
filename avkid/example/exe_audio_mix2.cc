#include <stdio.h>
#include <iostream>
#include <list>
#include <thread>
#include "avkid.hpp"

using namespace avkid;

#define NUMBER 15
#define MAXCHANELCOUNT 10
#define _PCM_MAX_ 1.0
#define _PCM_MIN_ (-1.0)


AVFrame *yuv_scale(enum AVPixelFormat srcPixformat, int iSrcW, int iSrcH,
                   const int linesize[], const uint8_t *const pSrcBuffer[],
                   enum AVPixelFormat dstPixformat, int iDstW, int iDstH) {
  AVFrame *scale_frame_p = NULL;

  scale_frame_p = av_frame_alloc();

  scale_frame_p->format = dstPixformat;
  scale_frame_p->width = iDstW;
  scale_frame_p->height = iDstH;

  av_frame_get_buffer(scale_frame_p, 32);

  SwsContext *img_convert_ctx =
      sws_getContext(iSrcW, iSrcH, srcPixformat, iDstW, iDstH, dstPixformat,
                     SWS_BILINEAR, NULL, NULL, NULL);

  if (img_convert_ctx != NULL) {
    int iRet = sws_scale(img_convert_ctx, pSrcBuffer, linesize, 0, iSrcH,
                         scale_frame_p->data, scale_frame_p->linesize);
    sws_freeContext(img_convert_ctx);
  } else {
    av_frame_free(&scale_frame_p);
    scale_frame_p = NULL;
    img_convert_ctx = NULL;
    return NULL;
  }

  return scale_frame_p;
}

void mix_video_frame(AVFrame *bk_frame_p, AVFrame *add_frame_p, int x, int y) {
  unsigned char *bk_start_y_p;
  unsigned char *bk_start_u_p;
  unsigned char *bk_start_v_p;

  unsigned char *add_start_y_p;
  unsigned char *add_start_u_p;
  unsigned char *add_start_v_p;

  // copy y
  for (int index = 0; index < add_frame_p->height; index++) {
    bk_start_y_p =
        bk_frame_p->data[0] + bk_frame_p->linesize[0] * (y + index) + x;
    add_start_y_p = add_frame_p->data[0] + add_frame_p->linesize[0] * index;
    memcpy(bk_start_y_p, add_start_y_p, add_frame_p->width);
  }

  // copy u
  for (int index = 0; index < add_frame_p->height / 2; index++) {
    bk_start_u_p =
        bk_frame_p->data[1] + bk_frame_p->linesize[1] * (y / 2 + index) + x / 2;
    add_start_u_p = add_frame_p->data[1] + add_frame_p->linesize[1] * index;
    memcpy(bk_start_u_p, add_start_u_p, add_frame_p->width / 2);
  }

  // copy v
  for (int index = 0; index < add_frame_p->height / 2; index++) {
    bk_start_v_p =
        bk_frame_p->data[2] + bk_frame_p->linesize[2] * (y / 2 + index) + x / 2;
    add_start_v_p = add_frame_p->data[2] + add_frame_p->linesize[2] * index;
    memcpy(bk_start_v_p, add_start_v_p, add_frame_p->width / 2);
  }
}

AVFrame *MixVideo(AVFrame *av_frame, uint8_t *yuv_data_0, uint8_t *yuv_data_1,
                  uint32_t pkt_dts) {
  int width = av_frame->width;
  int height = av_frame->height;

  AVFrame *bk_frame_p = av_frame_alloc();
  bk_frame_p->width = av_frame->width;
  bk_frame_p->height = av_frame->height;
  bk_frame_p->format = av_frame->format;
  bk_frame_p->pts = pkt_dts;
  bk_frame_p->pkt_dts = pkt_dts;
  av_frame_get_buffer(bk_frame_p, 32);
  // av_frame_copy_props(bk_frame_p, av_frame);
  memcpy(bk_frame_p->data[0], yuv_data_0, width * height);
  memcpy(bk_frame_p->data[1], yuv_data_0 + width * height, width * height / 4);
  memcpy(bk_frame_p->data[2], yuv_data_0 + width * height + width * height / 4,
         width * height / 4);

  AVFrame *add_frame_p = av_frame_alloc();
  add_frame_p->width = av_frame->width;
  add_frame_p->height = av_frame->height;
  add_frame_p->format = av_frame->format;
  add_frame_p->pts = pkt_dts;
  add_frame_p->pkt_dts = pkt_dts;
  av_frame_get_buffer(add_frame_p, 32);
  // av_frame_copy_props(add_frame_p, av_frame);
  memcpy(add_frame_p->data[0], yuv_data_1, width * height);
  memcpy(add_frame_p->data[1], yuv_data_1 + width * height, width * height / 4);
  memcpy(add_frame_p->data[2], yuv_data_1 + width * height + width * height / 4,
         width * height / 4);

  // mix_video_frame(bk_frame_p, add_frame_p, 0, height / 2);
  return add_frame_p;
}


void MixAudio(float *VoiceIn[MAXCHANELCOUNT], float *VoiceOut,
              int VoiceInCount, int VoiceDataLen) {
  float m_outWave = 0;
  int i, k;
  static float fValue[NUMBER] = {0.0625, 0.1250, 0.1875, 0.2500, 0.3125,
                                 0.3750, 0.4375, 0.5000, 0.5625, 0.6250,
                                 0.6875, 0.7500, 0.8125, 0.8750, 0.9375};

  static float m_f = 1.0;

  for (i = 0; i < VoiceDataLen; i++) {
    m_outWave = 0;
    for (k = 0; k < VoiceInCount; k++) {
      m_outWave += VoiceIn[k][i];
    }

    m_outWave = m_outWave * m_f;

    if (m_outWave >= _PCM_MAX_) {
    AVKID_LOG_ERROR << "m_outWave: " << m_outWave<< "\n";

      for (int m = NUMBER - 1; m >= 0; m--) {
        if (m_outWave * fValue[m] < _PCM_MAX_) {
          m_f = fValue[m];
          break;
        }
      }
      VoiceOut[i] = _PCM_MAX_;
    } else if (m_outWave <= _PCM_MIN_) {
    AVKID_LOG_ERROR << "m_outWave: " << m_outWave<< "\n";

      for (int n = NUMBER - 1; n >= 0; n--) {
        if (m_outWave * fValue[n] > _PCM_MIN_) {
          m_f = fValue[n];
          break;
        }
      }
      VoiceOut[i] = _PCM_MIN_;
    }
    VoiceOut[i] = (float)m_outWave;

    if (m_f < 1.0) m_f = (float)(m_f + (1.0 - m_f) / 16);
  }
}

class DecodeObserverImpl : public FrameProducer {
 public:
  void do_frame0(AVFrame *frame, bool is_audio) {
    frame_cb(frame, is_audio, 0);
  }
  void do_frame1(AVFrame *frame, bool is_audio) {
    frame_cb(frame, is_audio, 1);
  }

  void frame_cb(AVFrame *frame, bool is_audio, int slot = 0) {
    AVFrame *av_frame = frame ? av_frame_clone(frame) : frame;
    if (frame_handler) {
      if (!is_audio) {
        if (av_frame) {
          AVFrame *ret_frame = mix_video(av_frame, slot);
          if (ret_frame) {
            frame_handler(ret_frame, is_audio);
            av_frame_unref(ret_frame);
          }
        } else {
          frame_handler(av_frame, is_audio);
        }
      } else {
        if (av_frame) {
          auto ret_frame = mix_audio(av_frame, slot);
          if (ret_frame) {
            frame_handler(ret_frame, is_audio);
            av_frame_unref(ret_frame);
          }
        } else {
          frame_handler(av_frame, is_audio);
        }
      }
    }
  }
#define PCM_TYPE float

  std::list<AVFrame *> pcm0_l;
  std::list<AVFrame *> pcm1_l;
  std::mutex pcm_mutex_;

#define YUV_TYPE uint8_t
  std::list<AVFrame *> yuv0_l;
  std::list<AVFrame *> yuv1_l;
  std::mutex yuv_mutex_;

 private:
  AVFrame *mix_video(AVFrame *av_frame, int slot) {
    std::lock_guard<std::mutex> guard(yuv_mutex_);
    size_t yuv_len = (av_frame->linesize[0] + av_frame->linesize[1] +
                      av_frame->linesize[2]) *
                     av_frame->height;

    int width = av_frame->width;
    int height = av_frame->height;

    AVFrame *yuv_data = av_frame;

    if (slot == 0) {
      yuv0_l.push_back(yuv_data);
    } else {
      yuv1_l.push_back(yuv_data);
    }
    if (yuv0_l.empty() || yuv1_l.empty()) {
      return nullptr;
    }

    auto yuv_data_0 = yuv0_l.front();
    yuv0_l.pop_front();
    auto yuv_data_1 = yuv1_l.front();
    yuv1_l.pop_front();

    AVFrame *base_frame = av_frame_alloc();
    base_frame->width = yuv_data_0->width * 2;
    base_frame->height = yuv_data_0->height * 2;
    base_frame->format = yuv_data_0->format;
    av_frame_get_buffer(base_frame, 32);

    // mix_video_frame
    HelpOP::mix_video_pin_frame(base_frame, yuv_data_0, 0, 0);
    HelpOP::mix_video_pin_frame(base_frame, yuv_data_1, width, 0);
    HelpOP::mix_video_pin_frame(base_frame, yuv_data_1, 0, height);
    HelpOP::mix_video_pin_frame(base_frame, yuv_data_0, width, height);

    base_frame->pts = base_frame->pkt_dts = yuv_data_0->pts;
    AVFrame *ret_frame = HelpOP::scale_video_frame(
        base_frame, base_frame->width * 3 / 4, base_frame->height * 3 / 4);

    // AVFrame *ret_frame =
    //     yuv_scale((AVPixelFormat)base_frame->format, base_frame->width,
    //               base_frame->height, base_frame->linesize, base_frame->data,
    //               (AVPixelFormat)base_frame->format, base_frame->width * 3 /
    //               4, base_frame->height * 3 / 4);

    ret_frame->pts = ret_frame->pkt_dts = base_frame->pts;

    av_frame_unref(yuv_data_0);
    av_frame_unref(yuv_data_1);
    av_frame_free(&base_frame);
    return ret_frame;
  }

  AVFrame *mix_audio(AVFrame *av_frame, int slot) {
    std::lock_guard<std::mutex> guard(pcm_mutex_);

    size_t pcm_len = av_frame->nb_samples;

    if (slot == 0) {
      pcm0_l.push_back(av_frame);
    } else {
      pcm1_l.push_back(av_frame);
    }

    if (pcm0_l.empty() || pcm1_l.empty()) {
      return nullptr;
    }

    auto pcm_data_0 = pcm0_l.front();
    pcm0_l.pop_front();
    auto pcm_data_1 = pcm1_l.front();
    pcm1_l.pop_front();

    AVFrame *dst = av_frame_alloc();
    dst->nb_samples = pcm_data_0->nb_samples;
    dst->channels = pcm_data_0->channels;
    dst->channel_layout = pcm_data_0->channel_layout;
    dst->format         = pcm_data_0->format;
    dst->sample_rate = pcm_data_0->sample_rate;
    dst->pts = dst->pkt_dts = pcm_data_0->pts;
    av_frame_get_buffer(dst, 32);

    PCM_TYPE *VoiceIn[2];


    int data_size = av_get_bytes_per_sample((AVSampleFormat)pcm_data_0->format);

    VoiceIn[0] = (PCM_TYPE *)pcm_data_0->data[0];
    VoiceIn[1] = (PCM_TYPE *)pcm_data_1->data[0];
    MixAudio(VoiceIn, (PCM_TYPE*)dst->data[0], 2, pcm_len*data_size/sizeof(PCM_TYPE));

    VoiceIn[0] = (PCM_TYPE *)pcm_data_0->data[1];
    VoiceIn[1] = (PCM_TYPE *)pcm_data_1->data[1];
    MixAudio(VoiceIn, (PCM_TYPE*)dst->data[1], 2, pcm_len*data_size/sizeof(PCM_TYPE));

/*
  // mix audio
    int data_size = av_get_bytes_per_sample((AVSampleFormat)dst->format);
    for (int i = 0; i < dst->nb_samples; i++) {
      for (int ch = 0; ch < dst->channels; ch++) {
        int index = data_size * i;
        float a = *(float *)(pcm_data_0->data[ch] + index);
        float b = *(float *)(pcm_data_1->data[ch] + index);
        float res = 0.0f;
        if (a < 0 && b < 0)
          res = a + b + (a * b / (pow(2, 16 - 1) - 1));
        else
          res = a + b - (a * b / (pow(2, 16 - 1) - 1));
        *((float *)(dst->data[ch] + index)) = res;
      }
    }
*/

    av_frame_unref(pcm_data_0);
    av_frame_unref(pcm_data_1);

    return dst;
  }
};

int main(int argc, char **argv) {
  if (argc < 4) {
    AVKID_LOG_ERROR << "Usage: " << argv[0] << " <in> <out> <duraton>\n";
    return -1;
  }
  std::string in = argv[1];
  std::string in1 = argv[2];
  std::string out = argv[3];
  int duration = atoi(argv[4]);

  int iret = -1;

  HelpOP::global_init_ffmpeg();

  auto input = Input::create();
  auto input1 = Input::create();
  auto decode = Decode::create(true);
  auto decode1 = Decode::create(true);
  auto dosi = std::make_shared<DecodeObserverImpl>();
  auto encode = Encode::create(true);
  auto output = Output::create(true);

  combine(input1, decode1);
  combine(input, decode);
  AVKID_COMBINE_MODULE_CC(decode, dosi, &DecodeObserverImpl::do_frame0);
  AVKID_COMBINE_MODULE_CC(decode1, dosi, &DecodeObserverImpl::do_frame1);
  AVKID_COMBINE_MODULE_CC(dosi, encode, &Encode::do_data);
  combine(encode, output);

  if (!input->open(in)) {
    AVKID_LOG_ERROR << "Open " << in << " failed.\n";
    return -1;
  }
  if (!input1->open(in1)) {
    AVKID_LOG_ERROR << "Open " << in1 << " failed.\n";
    return -1;
  }

  decode->open(input->in_fmt_ctx());
  decode1->open(input1->in_fmt_ctx());

  encode->open(input1->in_fmt_ctx(), 576 * 3 / 2, 1024 * 3 / 2);
  output->open(out, input1->in_fmt_ctx(), 576 * 3 / 2, 1024 * 3 / 2);

  std::thread th([&] { input->read(duration * 1000); });
  std::thread th1([&] { input1->read(duration * 1000); });
  th.join();
  th1.join();

  // std::shared_ptr<chef::task_thread> thread_ =
  //     std::make_shared<chef::task_thread>(
  //         "main1", chef::task_thread::RELEASE_MODE_DO_ALL_DONE);
  // thread_->start();
  // thread_->add(chef::bind(&Input::read, input, duration * 1000));
  // std::shared_ptr<chef::task_thread> thread_1 =
  //     std::make_shared<chef::task_thread>(
  //         "main2", chef::task_thread::RELEASE_MODE_DO_ALL_DONE);
  // thread_1->start();
  // thread_1->add(chef::bind(&Input::read, input1, duration * 1000));
  // sleep(-1);

  return 0;
}
