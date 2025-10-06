/*
Demonstrates how to capture data from a microphone using the low-level API.

This example simply captures data from your default microphone until you press Enter. The output is saved to the file
specified on the command line.

Capturing works in a very similar way to playback. The only difference is the direction of data movement. Instead of
the application sending data to the device, the device will send data to the application. This example just writes the
data received by the microphone straight to a WAV file.
*/

// #include "../miniaudio-master/miniaudio.h"
#include "miniaudio.h"
#include "signalsmith-fft.h"
#include "AudioFFT.h"
#include "fenster.h"
#include <stdlib.h>
#include <stdio.h>

#define W 320
#define H 240

const size_t fftSize = 8192; // must be power of 2
const unsigned int sampleRate = 48000;
const unsigned int PAD = 500; // if you get segfault try making pad bigger
float freqs[fftSize];
std::vector<float> buf(audiofft::AudioFFT::ComplexSize(fftSize) + PAD);
std::vector<float> re(audiofft::AudioFFT::ComplexSize(fftSize) + PAD);
std::vector<float> im(audiofft::AudioFFT::ComplexSize(fftSize) + PAD);
std::vector<float> output(fftSize);
audiofft::AudioFFT fft;

// # of frames can determine what resolution I get...
// I want 15 ms resolution.
// That is 66 Hz or frames. 
// Fs = At leastTwice the highest frequency of interest

static void fenster_rect(struct fenster *f, int x, int y, int w, int h,
                         uint32_t c) {
  for (int row = 0; row < h; row++) {
    for (int col = 0; col < w; col++) {
      fenster_pixel(f, x + col, y + row) = c;
    }
  }
}

// clang-format off
static uint16_t font5x3[] = {0x0000,0x2092,0x002d,0x5f7d,0x279e,0x52a5,0x7ad6,0x0012,0x4494,0x1491,0x017a,0x05d0,0x1400,0x01c0,0x0400,0x12a4,0x2b6a,0x749a,0x752a,0x38a3,0x4f4a,0x38cf,0x3bce,0x12a7,0x3aae,0x49ae,0x0410,0x1410,0x4454,0x0e38,0x1511,0x10e3,0x73ee,0x5f7a,0x3beb,0x624e,0x3b6b,0x73cf,0x13cf,0x6b4e,0x5bed,0x7497,0x2b27,0x5add,0x7249,0x5b7d,0x5b6b,0x3b6e,0x12eb,0x4f6b,0x5aeb,0x388e,0x2497,0x6b6d,0x256d,0x5f6d,0x5aad,0x24ad,0x72a7,0x6496,0x4889,0x3493,0x002a,0xf000,0x0011,0x6b98,0x3b79,0x7270,0x7b74,0x6750,0x95d6,0xb9ee,0x5b59,0x6410,0xb482,0x56e8,0x6492,0x5be8,0x5b58,0x3b70,0x976a,0xcd6a,0x1370,0x38f0,0x64ba,0x3b68,0x2568,0x5f68,0x54a8,0xb9ad,0x73b8,0x64d6,0x2492,0x3593,0x03e0};
// clang-format on
static void fenster_text(struct fenster *f, int x, int y, char *s, int scale,
                         uint32_t c) {
  while (*s) {
    char chr = *s++;
    if (chr > 32) {
      uint16_t bmp = font5x3[chr - 32];
      for (int dy = 0; dy < 5; dy++) {
        for (int dx = 0; dx < 3; dx++) {
          if (bmp >> (dy * 3 + dx) & 1) {
            fenster_rect(f, x + dx * scale, y + dy * scale, scale, scale, c);
          }
        }
      }
    }
    x = x + 4 * scale;
  }
}


static int run() {
  uint32_t buf[W * H];
  struct fenster f = {
      .title = "hello",
      .width = W,
      .height = H,
      .buf = buf,
  };
  fenster_open(&f);
  uint32_t t = 0;
  int64_t now = fenster_time();
  while (fenster_loop(&f) == 0) {
    t++;
    // fenster_rect(&f, 0, 0, W, H, 0x00333333);
    // fenster_rect(&f, W / 4, H / 2, W / 2, H / 3, 0x00ff0000);
    // fenster_rect(&f, W / 2, H / 2 + H / 12, W / 6, H / 3 - H / 12, 0x00ffffff);
    // fenster_circle(&f, W / 2 - W / 8, H / 2 + H / 6, W / 20, 0x00ffffff);
    // fenster_line(&f, W / 4 - 25, H / 2, W / 2, H / 4, 0x0000ffff);
    // fenster_line(&f, W - W / 4 + 25, H / 2, W / 2, H / 4, 0x0000ffff);
    // fenster_line(&f, W - W / 4 + 25, H / 2, W / 4 - 25, H / 2, 0x0000ffff);
    // fenster_fill(&f, W / 2, H / 3, 0x00333333, 0x00ff00ff);
    // fenster_text(&f, 10, 10, "House", 8, 0x00ffffff);
    int64_t time = fenster_time();
    if (time - now < 1000 / 60)
      fenster_sleep(time - now);
    now = time;
  }
  fenster_close(&f);
  return 0;
}

void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
    static ma_uint32 totalFrameCount = 0;
    for (int i = 0; i < frameCount; i++)
    {
        buf[i + totalFrameCount] = ((float*)pInput)[i];
    }
    totalFrameCount += frameCount;

    if (totalFrameCount >= fftSize)
    {
        fft.fft(buf.data(), re.data(), im.data());
        float max = 0.0;
        int ind = 0;
        for (int i = 0; i < re.size(); i++)
        {
            float current = re[i] * re[i];
            if (current > max)
            {
                max = current;
                ind = i;
            }
        }
        static bool first = true;
        fenster *f = ((fenster*)(pDevice->fensterWin));
        if(first) 
        {
            fenster_open(f); 
            fenster_loop(f);
            first = false;
        }
        // need a float to string conversion
        fenster_rect(f, W / 4, H / 2, W / 2, H / 3, 0x00ff0000);
        fenster_text(f, 100, 100, "House", 4, 0x44444444);
        fenster_loop(f);
        printf("Freq:%f, mag: %f\n", freqs[ind], max);
        totalFrameCount = 0;
    }

    
    (void)pOutput;
}

int main(int argc, char** argv)
{
    // run();
    uint32_t buf[W * H];
    struct fenster f = {
        .title = "hello",
        .width = W,
        .height = H,
        .buf = buf,
    };
    
    // fenster_open(&f);
    // int test = 0;
    // while(fenster_loop(&f) == 0)
    // {
    //     if (test < 2)
    //     {
    //         fenster_rect(&f, W / 4, H / 2, W / 2, H / 3, 0x00ff0000);
    //         fenster_text(&f, 100, 100, "House", 1, 0x44444444);
    //     }
    //     if (test > 2)
    //     {
    //         ; // do nothing
    //     }
    //     if (test > 4)
    //     {
    //         fenster_rect(&f, W / 4, H / 2, W / 2, H / 3, 0x00ff0000);
    //         fenster_text(&f, 100, 100, "FF", 1, 0x44444444);
    //     }
    //     test++;
    // }
    // fenster_loop(&f);
    // fenster_rect(&f, W / 4, H / 2, W / 2, H / 3, 0x00ff0000);
    // fenster_text(&f, 100, 100, "House", 1, 0x44444444);
    // fenster_loop(&f);
    // fenster_rect(&f, W / 4, H / 2, W / 2, H / 3, 0x00ff0000);
    // fenster_text(&f, 100, 100, "FF", 1, 0x44444444);
    // fenster_loop(&f);

    for(int i = 0; i < fftSize; i++)
    {
        freqs[i] = (i * sampleRate) / fftSize;
    }
    fft.init(fftSize);

    ma_result result;
    ma_encoder_config encoderConfig;
    ma_encoder encoder;
    ma_device_config deviceConfig;
    ma_device device;

    // device.fensterWin = &f;

    if (argc < 2) {
        printf("No output file.\n");
        return -1;
    }

    encoderConfig = ma_encoder_config_init(ma_encoding_format_wav, ma_format_f32, 1, sampleRate);

    if (ma_encoder_init_file(argv[1], &encoderConfig, &encoder) != MA_SUCCESS) {
        printf("Failed to initialize output file.\n");
        return -1;
    }

    deviceConfig = ma_device_config_init(ma_device_type_capture);
    deviceConfig.capture.format   = encoder.config.format;
    deviceConfig.capture.channels = encoder.config.channels;
    deviceConfig.sampleRate       = encoder.config.sampleRate;
    deviceConfig.dataCallback     = data_callback;
    deviceConfig.pUserData        = &encoder;
    deviceConfig.fensterWin       = &f;

    result = ma_device_init(NULL, &deviceConfig, &device);
    if (result != MA_SUCCESS) {
        printf("Failed to initialize capture device.\n");
        return -2;
    }

    result = ma_device_start(&device);
    if (result != MA_SUCCESS) {
        ma_device_uninit(&device);
        printf("Failed to start device.\n");
        return -3;
    }

    printf("Press Enter to stop recording...\n");
    getchar();
    
    ma_device_uninit(&device);
    ma_encoder_uninit(&encoder);

    return 0;
}
