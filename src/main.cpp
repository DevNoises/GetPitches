#include "miniaudio.h" // audio capture
#include "AudioFFT.h"  // fft 
#include "drawUtils.h" // include fenster
#include "allNotes.h"  // holds Note Data Arrays

#include <stdlib.h>
#include <stdio.h>
#include <vector>

#define W Wi
#define H He
#define FLOATARRAYSIZE 20
#define NUM_WIN_ALLOWED 3

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


struct SizingData
{
    const unsigned int legibleSizePixels = 16;
    unsigned int resX;
    unsigned int resY;
    unsigned int screenSizeX;
    unsigned int screenSizeY;
    unsigned int scale;
};

struct WinData
{
    fenster* f = NULL;
    bool isFirst = false;
    float freq = 0.0;
    float mag = 0.0;
    unsigned int ind = 0;

    WinData(fenster* a, bool b, float c, float d, unsigned int e)
    {
        f = a;
        isFirst = b;
        freq = c;
        mag = d;
        ind = e;
    }
};

// display manager
class Manager
{
public:
    Manager(){}
    ~Manager(){}

    void initialize()
    {
        // clear all
        for (int i = 0; i < NUM_WIN_ALLOWED; i++)
        {
            fensters[i] = NULL;
        }

        // create one window
        uint32_t buf[W * H];
        fensters[0] = new fenster{
            .title = "hello",
            .width = W,
            .height = H,
            .buf = buf,
        };
    }

    void update(WinData winData)
    {
        // Draw results to graph
        // fenster *f = ((fenster*)(pDevice->fensterWin));
        // fenster* f = winData.f;

        // convert float value to char *
        static char freqString[FLOATARRAYSIZE];
        snprintf(freqString, FLOATARRAYSIZE, "%0.0f", winData.freq);

        if(winData.isFirst) 
        {
            initialize();
            fenster* f = fensters[0];
            fenster_open(f); 
            fenster_loop(f);
            // first = false;
        }
        fenster* f = fensters[0];
        fenster_rect(f, 0, 0, W, H, 0x00000000); // clear
        viewPort.drawLHS(f, winData.freq);

        // fenster_rect(f, 0, 0, W, H, 0x00333333); // clear
        // fenster_rect(f, W / 4, H / 2, W / 2, H / 3, 0x00ff0000);
        // unsigned int index = freqToIndex(winData.freq);
        // fenster_text(f, 0, 0, noteName[index], 3, 0xffffffff);

        // static char oct[FLOATARRAYSIZE];
        // snprintf(oct, FLOATARRAYSIZE, "%d", octave[index]);
        // fenster_text(f, 0, 100, oct, 3, 0xffffffff);

        
        // fenster_text(f, 0, 0, freqString, 3, 0xffffffff);
        fenster_loop(f);
        // noteName[freqToIndex(winData.freq)];
        // printf("Freq:%f, mag: %f\n", winData.freq, winData.mag);
        printf("Freq:%f, mag: %f\n", winData.freq, winData.mag);
        // totalFrameCount = 0;
    }

    /*
    1. get freq
    2. check if freq range is currently shown on screen
        a. need to know viewport range and current viewport pos on freq bins 
    */

private:
    // Entries to fensters should be dynamically allocated
    fenster* fensters[NUM_WIN_ALLOWED];

    struct ViewPort 
    {
        float baseFreq = 300.0;
        float baseNote = 300.0;
        
        ViewPort(){}

        void drawLHS(fenster* f, float freq)
        {
            unsigned int baseIndex = freqToIndex(freq);
            float baseFreqLog = freqLog10[baseIndex];
            
            // float linesToDraw [20];
            float convFactor = 800.0;
            for (unsigned int i = baseIndex; i < NOTE_ARR_SIZE; i++)
            {
                float linePos = (freqLog10[i] - freqLog10[baseIndex]) * convFactor;
                float yPos = H-10 - linePos;
                // linesToDraw[i - baseIndex] = (freqLog10[i] - freqLog10[baseIndex]) * convFactor;
                fenster_rect(f, 20, 0, 1, H, 0x00333333);
                fenster_rect(f, 0, yPos, W, 1, 0x00333333);
                const unsigned int fullToneMask = 0xAB5;
                if ( fullToneMask & (1 << ((i % 12))) )
                {
                    fenster_text(f, 0, yPos-4, noteName[i], 2, 0xffffffff);
                }

                // fenster_rect(f, 0, H - (i * 10), W, 1, 0x00333333);
                // printf("%f", H - linesToDraw[i-baseIndex]);
                if (yPos <= 0)
                {
                    // only draw a few lines
                    break;
                }
            }
        }
    };
    ViewPort viewPort;
};

// Global manager
Manager manager;

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
        static bool first = true;

        // convert float value to char *
        static char freqString[FLOATARRAYSIZE];
        snprintf(freqString, FLOATARRAYSIZE, "%0.0f", freqs[ind]);

        // Draw results to graph
        // fenster *f = ((fenster*)(pDevice->fensterWin));
        WinData winData(NULL, first, freqs[ind], max, ind);
        manager.update(winData);
        
        totalFrameCount = 0;
        first = false;
    }

    (void)pOutput; // what the hell does this do?
}


int main(int argc, char** argv)
{
    if (argc < 2) {
        printf("No output file.\n");
        return -1;
    }

    // Set up expected frequency bins
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
    // deviceConfig.fensterWin       = &f;

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
