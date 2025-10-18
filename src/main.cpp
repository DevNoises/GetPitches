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
#define MAX_DEQUE_SIZE 300

#define SCREEN_RES_W 2560
#define SCREEN_RES_H 1440
#define SCREEN_CM_W 59.77
#define SCREEN_CM_H 33.62

// #define YIN_OFFSET_COEFFICIENT 1.01419582566  // Yin algo a little off, apply to log values
#define YIN_OFFSET_COEFFICIENT 1.02  // Yin algo a little off, apply to log values
std::mutex fensterMutex;

// Initialize once for fft
/*
FFTSIZE |  Period Sampled (ms)
256     |  5.3
512     |  10.7
1024    |  21.3                 |  minimum for A2(110Hz) (18.18 ms = 2T)
2048    |  42.7
4096    |  85.3
8192    |  171.0
32768   |  683.0


*/
// #define FFTSIZE 8192 // must be power of 2
#define FFTSIZE 1024 // must be power of 2
#define NUM_SNAPSHOTS 5

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
std::deque<float> freqMem;
std::deque<float>* savedFreqMem [5];
bool displaySnapshots = false;
unsigned int displayCount = 0;

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
    , firstFreqReceived(false)
    , readyToInitialize(false)
    {
    }

    ~Manager(){}

    bool firstWinOpen;
    bool firstFreqReceived;
    bool readyToInitialize;
    
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

        fenster* f = fensters[0];
        fenster_open(f); 
        fenster_loop(f);
        firstWinOpen = true;
    }

    void setReady()
    {
        readyToInitialize = true;
    }

    bool getIsReady()
    {
        return readyToInitialize;
    }

    void emptyUpdate()
    {
        if (!firstWinOpen) { return; }
        fenster_loop(fensters[0]);
    }

    bool isInitialized()
    {
        if (firstWinOpen) { return true; }
        return false;
    }

    // currently gets called from the callback
    void update(WinData winData)
    {
        // first time set up stuff


        fenster* f = fensters[0];

        // pressing buttons move to fast update
        // for (int keyNum : f->keys)
        // {
        //     if(keyNum != 0)
        //     {
        //         printf("keyNum: %d\n", keyNum);
        //     }
        // }

        if(winData.freq > 70) 
        {
            firstFreqReceived = true;
        }
        fenster_rect(f, 0, 0, W, H, 0x00000000); // clear
        // viewPort.drawLHS(f, winData.freq, winData.mag);
        fenster_loop(f);

        // printf("Freq:%f, log10: %f, mag: %f\n", winData.freq, log10f(winData.freq), winData.mag);
    }

    fenster* getAWindow(unsigned int index)
    {
        return fensters[index];
    }


private:
    // Entries to fensters should be dynamically allocated
    fenster* fensters[NUM_WIN_ALLOWED];
public:
    struct ViewPort 
    {
        const unsigned int NUM_OF_LINES_EXPECTED = 20;
        const unsigned int TARGET_BUFFER = 10;
        unsigned int baseIndex = 25;
        unsigned int targetIndex= 25;
        float delta = 0.0;
        int deltaFactor = 1;
        float offset = 0.0;
        bool targetReached = true;
        float convFactor = 800.0;
        unsigned int smoothingCount = 0;
        std::deque<float> smoothingBuf;
        // std::deque<float>* snapshots [NUM_SNAPSHOTS];
        unsigned int snapCount = 0;


        ViewPort()
        {
            for (int i = 0; i < NUM_SNAPSHOTS; i++)
            {   
                std::deque<float>* newSnap = new std::deque<float>;
                savedFreqMem[i] = newSnap;
            }
            snapCount = 0;
        }

        void nextSnap()
        {
            displayCount = (displayCount + 1) % 5;
            printf("displayCount set to %d\n", displayCount);
        }
        
        void nextSnapCount()
        {
            snapCount = displayCount;
            printf("displayCount and snapCount set to %d\n", snapCount);
        }

        
        bool saveSnapshot()
        {
            if(freqMem.empty()) { return false; }
            savedFreqMem[snapCount]->clear();
            for (float val : freqMem)
            {
                savedFreqMem[snapCount]->push_back(val);
            }
            printf("saved snapshot to %d\n", snapCount);
            snapCount = (snapCount + 1) % 5;
            return true;
        }
        
        void setViewBase(fenster* f, float freq)
        {
            // volume filter and freq
            // printf("Freq:%f, log10: %f, invLog10: %f\n", freq, log10f(invLog10(freq)), invLog10(freq));
            if (freq > log10f(70) && freq < log10f(1800))
            {
                // control baseIndex position (should work off single freq input)
                unsigned int inputIndex = freqToIndex(invLog10(freq));
                if (inputIndex <= baseIndex + TARGET_BUFFER)
                {
                    // target low
                    if (inputIndex == baseIndex + TARGET_BUFFER)
                    {
                        targetReached = true;
                        // finalApproach = true;
                        return;
                    }
                    targetReached = false;
                    int newBase = inputIndex - TARGET_BUFFER;
                    targetIndex = (newBase >= 0) ? newBase : 0;
                    // delta = (freqLog10[baseIndex] - freqLog10[targetIndex]) / 10;
                    // delta = (freqLog10[targetIndex] - freqLog10[baseIndex]) / 10; // -delta
                    delta = (freqLog10[targetIndex] - freqLog10[baseIndex]) / 120; // -delta
                    // printf("low: inputIndex: %d, baseIndex: %d, targetIndex: %d, delta: %f\n", inputIndex, baseIndex, targetIndex, delta);
                }
                else if (inputIndex >= baseIndex + NUM_OF_LINES_EXPECTED + TARGET_BUFFER)
                {
                    // target high
                    if ( ((inputIndex - NUM_OF_LINES_EXPECTED + TARGET_BUFFER) == targetIndex) )
                    {
                        targetReached = true;
                        return;
                    }
                    targetReached = false;
                    int newBase = inputIndex - NUM_OF_LINES_EXPECTED + TARGET_BUFFER;
                    targetIndex = (newBase >= 0) ? newBase : 0;
                    // delta = (freqLog10[targetIndex] - freqLog10[baseIndex]) / 10; // +delta
                    delta = (freqLog10[targetIndex] - freqLog10[baseIndex]) / 120; // +delta
                    // printf("high: inputIndex: %d, baseIndex: %d, targetIndex: %d, delta: %f\n", inputIndex, baseIndex, targetIndex, delta);
                }
                else
                {
                    // baseIndex remains the same
                    // no new target needs to be set
                }
            }
        }

        // Called without a new freq
        void drawNoteLines(fenster* f)
        {
            // printf("drawNotesLines\n");
            // update baseIndex as needed
            offset = delta * deltaFactor;
            bool increaseBaseIndex = true;
            int i = 0;
            bool isIncremented = false;

            if (baseIndex != targetIndex)
            {
                // while (increaseBaseIndex)
                // {
                    if (delta > 0)
                    {
                        if (freqLog10[baseIndex] + offset >= freqLog10[targetIndex]) 
                        {
                            baseIndex = targetIndex;
                            deltaFactor = 0;
                        }
                        
                        // if (freqLog10[baseIndex] + offset >= freqLog10[baseIndex + i + 1]) 
                        // {
                        //     i++;
                        //     isIncremented = true;
                        //     //edgecase
                        //     if (baseIndex > targetIndex)
                        //     {
                        //         baseIndex = targetIndex;
                        //         deltaFactor = 0;
                        //         break;
                        //     }
                        // }
                        // else
                        // {
                        //     increaseBaseIndex = false;
                        //     if (isIncremented)
                        //     {
                        //         // baseIndex += (i - 1);
                        //         baseIndex += (i);
                        //         deltaFactor = 0;
                        //     }
                        // }
                    }
                    else
                    {
                        if (freqLog10[baseIndex] + offset <= freqLog10[targetIndex])
                        {
                            baseIndex = targetIndex;
                            deltaFactor = 0;
                        }
                        // if (freqLog10[baseIndex] + offset <= freqLog10[baseIndex - i - 1])
                        // {
                        //     i++;
                        //     isIncremented = true;
                        //     //edgecase
                        //     if (baseIndex < targetIndex)
                        //     {
                        //         baseIndex = targetIndex;
                        //         deltaFactor = 0;
                        //         break;
                        //     }
                        // }
                        // else
                        // {
                        //     increaseBaseIndex = false;
                        //     if (isIncremented)
                        //     {
                        //         // baseIndex -= (i - 1);
                        //         baseIndex -= (i);
                        //         deltaFactor = 0;
                        //     }
                        // }
                    }
                // }
            }
            else
            {
                deltaFactor = 0;
            }
            // printf("delta: %f, factor: %d, baseIndex: %d, targetIndex: %d\n", delta, deltaFactor, baseIndex, targetIndex);
            
            // draw
            offset = delta * deltaFactor;
            fenster_rect(f, 20, 0, 2, H-1, 0x00333333);  // draw vertical line once
            static float prevPos = 0.0;
            for (unsigned int i = baseIndex; i < NOTE_ARR_SIZE; i++)
            {
                // calculate ypos
                float linePos = (freqLog10[i] - freqLog10[baseIndex] - offset) * convFactor;
                float yPos = H-10 - linePos;
                if(i == baseIndex)
                {
                    printf("prevyPos: %f, yPos: %f\n", prevPos, yPos);
                    prevPos = yPos;
                }
                
                // stop drawing yPos is out of range
                if ((yPos - RECT_SIZE) <= 0)
                {
                    // only draw a few lines
                    // printf("lines drawn: %d", i - baseIndex - 1);
                    break;
                }
            
                fenster_rect(f, 0, static_cast<int>(yPos), W-1, 2, 0x00333333); // draw horizontals
                
                // toneMask to only print out the single letters A - G
                const unsigned int fullToneMask = 0xAB5;
                if ( fullToneMask & (1 << ((i % 12))) )
                {
                    char oct [2];
                    snprintf(oct, 2, "%d", octave[i]);
                    fenster_text(f, 4, yPos-4, noteName[i], 2, 0xffffffff);
                    fenster_text(f, 12, yPos-4, oct, 2, 0xffffffff);
                    // highligth C lines
                    if('C' == (noteName[i][0]))
                    {
                        fenster_rect(f, 0, static_cast<int>(yPos), W-1, 3, 0x00333333);
                    }
                }

                // draw lines that stand out
                if(i == baseIndex + TARGET_BUFFER)
                {
                   fenster_rect(f, 0, static_cast<int>(yPos), W-1, 3, 0x0ca102); // draw horizontals green
                }
                if(i == baseIndex + NUM_OF_LINES_EXPECTED + TARGET_BUFFER)
                {
                   fenster_rect(f, 0, static_cast<int>(yPos), W-1, 3, 0x0915ed); // draw horizontals blue 
                }
                if(i == targetIndex + TARGET_BUFFER)
                {
                   fenster_rect(f, 0, static_cast<int>(yPos), W-1, 3, 0xf24413); // draw horizontals red
                }

                
            }
            if (isIncremented)
            {
                deltaFactor = 1;
            }
            else
            {
                deltaFactor++;
            }
        }

        // draw pitch tracker
        void drawPitch(fenster* f, const std::deque<float>* frequenciesLog)

        {
            int j = 0;
            float baseFreqLog = freqLog10[baseIndex];
            for (float val : *frequenciesLog)
            {
                float xPos = W - 15 - (j * 10);
                if (val < 1.0)
                {
                    // don't not draw if freq input is 0.0 / bad
                }
                else
                {
                    float yPos = H - ((val - baseFreqLog - offset) * convFactor);
                    // fenster_rect(f, static_cast<int>(xPos - 10), static_cast<int>(yPos), 5, 5, 0xFFF426);
                    if (xPos <= 30) {j++; continue;}
                    fenster_rect(f, static_cast<int>(xPos - 10), static_cast<int>(yPos), 5, 5, 0xFFF426);
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
    // printf("callback: %d\n", callback);
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
        const float uncertainty = 0.05;
        Yin_init(&yin, FFTSIZE, uncertainty);
        // Yin_init(&yin, FFTSIZE, 0.10);
        float pitch = Yin_getPitch(&yin, bufForYin);
        // static bool first = true;

        // WinData winData(NULL, first, pitch, 0.0, 0.0);
        
        fensterMutex.lock();
        static int popped = 0;
        popped++;
        if (freqMem.size() > MAX_DEQUE_SIZE) {freqMem.pop_back(); printf("popped: %d\n", popped);}
        if (pitch > 70.0)
        {
            freqMem.push_front(log10f(pitch) * YIN_OFFSET_COEFFICIENT);
        }
        else
        {
            freqMem.push_front(0.0);
        }
        // manager.update(winData);
        fensterMutex.unlock();
        
        totalFrameCount = 0;
        // first = false;
    }

    (void)pOutput; // what the hell does this do?
}


void processKeys(fenster* f)
{
    if (f->keys[83])
    {
        bool success = false;
        if(manager.viewPort.saveSnapshot())
        {
            success = true;
        }
        printf("saveSnapshot status: %d\n", success);
        f->keys[83] = 0;
    }

    if (f->keys[67])
    {
        // cycle to next snapshot
        manager.viewPort.nextSnap();
        f->keys[67] = 0;
    }

    if (f->keys[68])
    {
        // display
        displaySnapshots = (displaySnapshots) ? false : true;
        printf("display set to %d\n", displaySnapshots);
        f->keys[67] = 0;
    }
    
    if (f->keys[79]) // O/verwrite
    {
        // display
        printf("overwriting snap %d\n", displayCount);
        manager.viewPort.nextSnapCount();
        manager.viewPort.saveSnapshot();

        f->keys[79] = 0;
    }
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


    // encoderConfig = ma_encoder_config_init(ma_encoding_format_wav, ma_format_f32, 1, sampleRate);
    encoderConfig = ma_encoder_config_init(ma_encoding_format_wav, ma_format_s16, 1, sampleRate); // s16 for Yin algo
    // encoderConfig = ma_encoder_config_init(ma_encoding_format_wav, ma_format_s16, 1, ma_standard_sample_rate_192000); // s16 for Yin algo

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
        if (!freqMem.empty() && !manager.isInitialized()) 
        {
            manager.setReady();
        }

        if (manager.getIsReady() && !manager.isInitialized())
        {
            manager.initialize();
        }

        if (manager.isInitialized())
        {
            fenster* f = manager.getAWindow(0);

            fenster_rect(f, 0, 0, W, H, 0x00993939); // clear
            manager.viewPort.setViewBase(f, freqMem.front());
            manager.viewPort.drawNoteLines(f);
            manager.viewPort.drawPitch(f, &freqMem);
            if (displaySnapshots)
            {
                manager.viewPort.drawPitch(f, savedFreqMem[displayCount]);
            }
            processKeys(f);

            fenster_loop(f);

            // printf("freqMem size: %d\n", freqMem.size());
        }
        //     printf("main\n");
        //     manager.viewPort.drawNoteLines(f);
            
        fensterMutex.unlock();

        // sleep 
        int64_t time = fenster_time();
        if (time - now < 1000 / 60) 
        {
            fenster_sleep(time - now);
        }
        now = time;
        count++;
    }
    // printf("Press Enter to stop recording...\n");
    // getchar();
    
    ma_device_uninit(&device);
    ma_encoder_uninit(&encoder);

    return 0;
}
