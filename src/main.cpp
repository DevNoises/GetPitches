#include "miniaudio.h" // audio capture
#include "AudioFFT.h"  // fft 
#include "drawUtils.h" // include fenster
#include "allNotes.h"  // holds Note Data Arrays

#include <stdlib.h>
#include <stdio.h>
#include <vector>
#include <deque>
#include <math.h> // log
#include <mutex>

#include "Yin.h"

#define W Wi
#define H He
#define FLOATARRAYSIZE 20
#define NUM_WIN_ALLOWED 3

#define SCREEN_RES_W 2560
#define SCREEN_RES_H 1440
#define SCREEN_CM_W 59.77
#define SCREEN_CM_H 33.62

std::mutex fensterMutex;

// Initialize once for fft
#define FFTSIZE 8192 // must be power of 2
// #define FFTSIZE 4096 // must be power of 2
const unsigned int sampleRate = 48000;
const unsigned int PAD = 500;//500; // if you get segfault try making pad bigger
float freqs[FFTSIZE];
float yinBuffer[FFTSIZE];
int16_t yinBuffer2[FFTSIZE];
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
    Manager()
    : firstWinOpen(false)
    {
    }
    ~Manager(){}

    bool firstWinOpen;
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
            .title = "Titler",
            .width = W,
            .height = H,
            .buf = buf,
        };
    }

    void emptyUpdate()
    {
        if (!firstWinOpen) { return; }
        fenster_loop(fensters[0]);
    }

    void update(WinData winData)
    {
        static int update = 0;
        printf("update: %d", update);
        update++;

        if(winData.isFirst) 
        {
            initialize();
            fenster* f = fensters[0];
            fenster_open(f); 
            fenster_loop(f);
            firstWinOpen = true;
        }

        fenster* f = fensters[0];
        for (int keyNum : f->keys)
        {
            if(keyNum != 0)
            {
                printf("keyNum: %d", keyNum);
            }
        }
        fenster_rect(f, 0, 0, W, H, 0x00000000); // clear
        viewPort.drawLHS(f, winData.freq, winData.mag);
        fenster_loop(f);

        printf("Freq:%f, log10: %f, mag: %f\n", winData.freq, log10f(winData.freq), winData.mag);
    }



private:
    // Entries to fensters should be dynamically allocated
    fenster* fensters[NUM_WIN_ALLOWED];

    struct ViewPort 
    {
        const unsigned int NUM_OF_LINES_EXPECTED = 35;
        float baseFreq = 300.0;
        unsigned int baseIndex = 40;
        float currBase = 40;
        float convFactor = 800.0;
        unsigned int smoothingCount = 0;
        std::deque<float> smoothingBuf;
        std::deque<float> freqMem;
        std::deque<float>* snapshots [5];
        bool skip = false;

        ViewPort()
        {
            baseIndex = freqToIndex(baseFreq);
        }

        bool getSnapshot(std::deque<float>* newSnap)
        {
            if(freqMem.empty()) { return false;}
            // std::deque<float>* newSnap = new std::deque<float>;
            for (float val : freqMem)
            {
                newSnap->push_back(val);
            }
        }


        void drawLHS(fenster* f, float freq, float mag)
        {
            skip = false;
            // volume filter and freq
            if (freq > 70 && freq < 1800)
            {
                // smoothing ((maybe isn't neccessary with new Yin))
                smoothingBuf.push_front(freq);
                if (smoothingBuf.size() == 3) 
                { 
                    // could do this without arrays, just the deque but lazy
                    float currVals[3];
                    int i = 0;
                    for (float val : smoothingBuf)
                    {
                        currVals[i] = val;
                        i++;
                    }
                    float a,b,c;
                    bool A, B, C;
                    a = currVals[0] / currVals[1];
                    b = currVals[0] / currVals[2];
                    c = currVals[1] / currVals[2];
                    // 1 if same
                    // 2 or 1/2 if harmonic
                    // otherwise normal change
                    A =  (a > 0.9 && a < 1.10) ? true : false;
                    B =  (b > 0.9 && b < 1.10) ? true : false;
                    C =  (c > 0.9 && c < 1.10) ? true : false;

                    // [val1, val2, val3]
                    if (A && B && C)
                    { 
                        // no change
                    }
                    else if (!A && !B)
                    {
                        if (C) 
                        {
                            freq = currVals[2];
                            printf("smoothing[2] C: %f, %f, %f\n", currVals[0], currVals[1], currVals[2]);
                        }
                        // else do nothing
                    }
                    else if (A && !B)
                    {
                        freq = currVals[1];
                        printf("smoothing[1] A!B: %f, %f, %f\n", currVals[0], currVals[1], currVals[2]);
                    }
                    else if (!A && B)
                    {
                        freq = currVals[2];
                        printf("smoothing[2] !AB: %f, %f, %f\n", currVals[0], currVals[1], currVals[2]);
                    }

                    smoothingBuf.pop_back();
                }
                
                // control baseIndex position
                unsigned int inputIndex = freqToIndex(freq);
                if (inputIndex < baseIndex)
                {
                    int newBase = inputIndex - 5;
                    baseIndex = (newBase >= 0) ? newBase : 0;
                    printf("low: %d\n", baseIndex);
                }
                else if (inputIndex > baseIndex + NUM_OF_LINES_EXPECTED)
                {
                    int newBase = inputIndex - (NUM_OF_LINES_EXPECTED + 5);
                    baseIndex = (newBase > 0) ? newBase : 0;
                    printf("high: %d\n", baseIndex);
                }
                else
                {
                    // baseIndex remains the same
                }
                currBase = log10f(freq);
            }
            else  // ends freq and mag filter
            {
                skip = true;
                smoothingBuf.clear(); // clear when prev input is bad
            }

            // draw left hand lines and notes
            currBase != freqLog10[baseIndex];
            for (unsigned int i = baseIndex; i < NOTE_ARR_SIZE; i++)
            {
                float linePos = (freqLog10[i] - freqLog10[baseIndex]) * convFactor;
                float yPos = H-10 - linePos;
                fenster_rect(f, 20, 0, 1, H, 0x00333333);
                fenster_rect(f, 0, yPos, W, 1, 0x00333333);
                
                // toneMask to only print out the single letters A - G
                const unsigned int fullToneMask = 0xAB5;
                if ( fullToneMask & (1 << ((i % 12))) )
                {
                    char oct [2];
                    snprintf(oct, 2, "%d", octave[i]);
                    fenster_text(f, 4, yPos-4, noteName[i], 2, 0xffffffff);
                    fenster_text(f, 12, yPos-4, oct, 2, 0xffffffff);
                }

                if (yPos <= 0)
                {
                    // only draw a few lines
                    // printf("lines drawn: %d", i - baseIndex - 1);
                    break;
                }
            }

            // draw pitch tracker
            float baseFreqLog = freqLog10[baseIndex];
            float freqPos = log10f(freq);
            if (freqMem.size() > 1000) {freqMem.pop_back();}
            if (skip)
            {
                freqMem.push_front(0.0);
            }
            else
            {
                freqMem.push_front(freqPos);
            }
            
            int j = 0;
            for (float val : freqMem)
            {
                float xPos = W - 15 - (j * 10);
                if (val < 1.0)
                {
                }
                else
                {
                    float yPos = H - ((val - baseFreqLog) * convFactor);
                    fenster_rect(f, xPos - 10, yPos, 5, 5, 0xFFF426);
                }
                j++;
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
    static int callback = 0;
    printf("callback: %d", callback);
    callback++;
    // Each frame contains samples (1 sample per frame for my mono audio setup)
    // Each time this is called it has some number of frames. 
    // Track and Store frames and trigger once enough frames are collected.
    static ma_uint32 totalFrameCount = 0;
    int16_t bufForYin [FFTSIZE + 500];
    for (int i = 0; i < frameCount; i++)
    {
        // buf[i + totalFrameCount] = ((float*)pInput)[i];
        bufForYin[i + totalFrameCount] = ((int16_t*)pInput)[i];
    }
    totalFrameCount += frameCount;

    // Process stored frames.
    if (totalFrameCount >= FFTSIZE)
    {
        // fft gone new friend with Yin
        // calculate fft and get max freq
        // fft.fft(buf.data(), re.data(), im.data());
        float max = 0.0;
        int ind = 0;
        // for (int i = 0; i < re.size(); i++)
        // {
        //     float current = re[i] * re[i];
        //     if (current > max)
        //     {
        //         max = current;
        //         ind = i;
        //     }
        // }

        Yin yin;
        Yin_init(&yin, FFTSIZE, 0.10);
        float pitch = Yin_getPitch(&yin, bufForYin);
        static bool first = true;
        printf("fuckckc");

        // convert float value to char *
        // static char freqString[FLOATARRAYSIZE];
        // snprintf(freqString, FLOATARRAYSIZE, "%0.0f", freqs[ind]);

        // Draw results to graph
        // fenster *f = ((fenster*)(pDevice->fensterWin));
        // WinData winData(NULL, first, freqs[ind], max, ind);
        WinData winData(NULL, first, pitch, 0.0, 0.0);
        fensterMutex.lock();
        manager.update(winData);
        fensterMutex.unlock();
        
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
    for(int i = 0; i < FFTSIZE; i++)
    {
        freqs[i] = (i * sampleRate) / FFTSIZE;
    }
    // fft.init(FFTSIZE);

    ma_result result;
    ma_encoder_config encoderConfig;
    ma_encoder encoder;
    ma_device_config deviceConfig;
    ma_device device;

    // manager.initialize();

    // encoderConfig = ma_encoder_config_init(ma_encoding_format_wav, ma_format_f32, 1, sampleRate);
    encoderConfig = ma_encoder_config_init(ma_encoding_format_wav, ma_format_s16, 1, sampleRate);

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

    int64_t now = fenster_time();
    int count = 0;
    while(1)
    {
        fensterMutex.lock();
        printf("main: %d\n", count);
        manager.emptyUpdate();
        fensterMutex.unlock();
        int64_t time = fenster_time();
        if (time - now < 1000 / 60) 
        {
            fenster_sleep(time - now);
        }
        now = time;
        count++;
    }
    printf("Press Enter to stop recording...\n");
    getchar();
    
    ma_device_uninit(&device);
    ma_encoder_uninit(&encoder);

    return 0;
}
