#include <stdio.h>
#include <iostream>
#include <list>
#include <thread>
#include "avkid.hpp"

#include <SoundTouch.h>

using namespace avkid;

class DecodeObserverImpl : public FrameProducer, public FrameConsumerInterface {
 public:
  DecodeObserverImpl(int pitch, int tempo) : pitch_(pitch), tempo_(tempo) {
    sound_touch_ = std::make_shared<soundtouch::SoundTouch>();
    sound_touch_->setSampleRate(48000);
    sound_touch_->setChannels(2);
    sound_touch_->setPitchSemiTones(pitch_);
  }

  void do_data(AVFrame *frame, bool is_audio) {
    AVFrame *av_frame = frame ? av_frame_clone(frame) : frame;
    if (frame_handler) {
      if (!is_audio) {
        frame_handler(av_frame, is_audio);
        av_frame_unref(av_frame);
      } else {
        if (av_frame) {
          auto ret_frame = touch_audio(av_frame, is_audio);
          if (ret_frame) {
            frame_handler(ret_frame, is_audio);
            av_frame_unref(ret_frame);
          }
          av_frame_unref(av_frame);
        } else {
          frame_handler(av_frame, is_audio);
        }
      }
    }
  }
#define PCM_TYPE float

 private:
  typedef soundtouch::SoundTouch SoundTouch;

  AVFrame *touch_audio(AVFrame *frame, bool is_audio) {
    auto av_frame = av_frame_alloc();
    av_frame->format = frame->format;
    av_frame->channels = frame->channels;
    av_frame->channel_layout = frame->channel_layout;
    av_frame->nb_samples = frame->nb_samples;
    av_frame->pts = frame->pts;
    av_frame_get_buffer(av_frame, 0);
    memcpy(av_frame->data[0], frame->data[0], frame->nb_samples * 4);
    memcpy(av_frame->data[1], frame->data[1], frame->nb_samples * 4);

    PCM_TYPE *samples_0 = (PCM_TYPE *)av_frame->data[0];
    PCM_TYPE *samples_1 = (PCM_TYPE *)av_frame->data[1];
    int nb = av_frame->nb_samples;
    int pcm_size = nb * 4;

    int index = 0;
    PCM_TYPE touch_buf[nb * 2];
    for (int i = 0; i < av_frame->nb_samples; i++)
      for (int ch = 0; ch < av_frame->channels; ch++)
        touch_buf[index++] = *(PCM_TYPE *)(av_frame->data[ch] + 4 * i);

    sound_touch_->putSamples(touch_buf, nb);
    int nSamples = sound_touch_->receiveSamples(touch_buf, nb);
    // AVKID_LOG_DEBUG << "receiveSamples,over pcm_len:" << nSamples << "\n";

    index = 0;
    for (int i = 0; i < av_frame->nb_samples; i++)
      for (int ch = 0; ch < av_frame->channels; ch++)
        *(PCM_TYPE *)(av_frame->data[ch] + 4 * i) = touch_buf[index++];

    return av_frame;
  }

 private:
  int pitch_;
  int tempo_;
  std::shared_ptr<SoundTouch> sound_touch_;
};

int main(int argc, char **argv) {
  if (argc < 3) {
    AVKID_LOG_ERROR << "Usage: " << argv[0] << " <in> <out> <duraton>\n";
    return -1;
  }
  std::string in = argv[1];
  std::string out = argv[2];
  int duration = atoi(argv[3]);
  int pitch = atoi(argv[4]);

  int iret = -1;

  HelpOP::global_init_ffmpeg();

  auto input = Input::create();
  auto decode = Decode::create(true);
  auto bc = std::make_shared<FrameBroadcast>();
  combine(input, decode, bc);
  if (!input->open(in)) {
    AVKID_LOG_ERROR << "Open " << in << " failed.\n";
    return -1;
  }
  decode->open(input->in_fmt_ctx());

  for (int i = 1; i <= 3; i++) {
    auto dosi = std::make_shared<DecodeObserverImpl>(i*3, 0);
    auto encode = Encode::create(true);
    auto output = Output::create(true);
    bc->add_data_handler(dosi);

    combine(dosi, encode, output);
    encode->open(input->in_fmt_ctx());
    output->open("test_" + std::to_string(i) + out, input->in_fmt_ctx());
  }

  std::thread th([&] { input->read(duration * 1000); });
  th.join();

  return 0;
}
