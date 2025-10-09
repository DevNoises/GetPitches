#include "miniaudio.h" // audio capture
#include "AudioFFT.h"  // fft 
#include "drawUtils.h" // include fenster

#include <stdlib.h>
#include <stdio.h>
#include <vector>

#define W 320
#define H 240
#define FLOATARRAYSIZE 20

// Initialize once for fft
#define FFTSIZE 8192 // must be power of 2
const size_t fftSize = 8192; // must be power of 2
const unsigned int sampleRate = 48000;
const unsigned int PAD = 500; // if you get segfault try making pad bigger
float freqs[FFTSIZE];
std::vector<float> buf(audiofft::AudioFFT::ComplexSize(FFTSIZE) + PAD);
std::vector<float> re(audiofft::AudioFFT::ComplexSize(FFTSIZE) + PAD);
std::vector<float> im(audiofft::AudioFFT::ComplexSize(FFTSIZE) + PAD);
std::vector<float> output(FFTSIZE);
audiofft::AudioFFT fft;

// This is what gets called on receiving audio data.
void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
    // Each frame contains samples (1 sample per frame for my mono audio setup)
    // Each time this is called it has some number of frames. 
    // Track and Store frames and trigger once enough frames are collected.
    static ma_uint32 totalFrameCount = 0;
    for (int i = 0; i < frameCount; i++)
    {
        buf[i + totalFrameCount] = ((float*)pInput)[i];
    }
    totalFrameCount += frameCount;

    // Process stored frames.
    if (totalFrameCount >= fftSize)
    {
        // calculate fft and get max freq
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

        // convert float value to char *
        static char freqString[FLOATARRAYSIZE];
        snprintf(freqString, FLOATARRAYSIZE, "%0.0f", freqs[ind]);

        // Draw results to graph
        static bool first = true;
        fenster *f = ((fenster*)(pDevice->fensterWin));
        if(first) 
        {
            fenster_open(f); 
            fenster_loop(f);
            first = false;
        }
        fenster_rect(f, 0, 0, W, H, 0x00333333); // clear
        fenster_rect(f, W / 4, H / 2, W / 2, H / 3, 0x00ff0000);
        fenster_text(f, 100, 100, freqString, 4, 0xffffffff);
        fenster_loop(f);
        printf("Freq:%f, mag: %f\n", freqs[ind], max);
        totalFrameCount = 0;
    }

    (void)pOutput; // what the hell does this do?
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
