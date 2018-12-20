#include <stdio.h>
#include <iostream>
#include <list>
#include <thread>
#include "avkid.hpp"

using namespace avkid;

#define NUMBER 15
#define MAXCHANELCOUNT 10
#define _PCM_MAX_ 32767
#define _PCM_MIN_ (-32767)

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
        base_frame, base_frame->width * 1 / 2, base_frame->height * 1 / 2);

    ret_frame->pts = ret_frame->pkt_dts = base_frame->pts;

    av_frame_unref(yuv_data_0);
    av_frame_unref(yuv_data_1);
    av_frame_free(&base_frame);
    return ret_frame;
  }

  AVFrame *mix_audio(AVFrame *av_frame, int slot) {
    std::lock_guard<std::mutex> guard(pcm_mutex_);

    PCM_TYPE *samples = (PCM_TYPE *)av_frame->data[0];
    size_t pcm_len = av_frame->nb_samples * av_frame->channels;

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

    AVFrame *dst = MixOP::audio(pcm_data_0, pcm_data_1);

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
  combine(dosi, encode);
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

  encode->open(input1->in_fmt_ctx(), 576, 1024);
  output->open(out, input1->in_fmt_ctx(), 576, 1024);

  std::thread th([&] { input->read(duration * 1000); });
  std::thread th1([&] { input1->read(duration * 1000); });
  th.join();
  th1.join();

  return 0;
}
