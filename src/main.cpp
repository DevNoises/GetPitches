// from github
#include "miniaudio.h" // audio capture
#include "Yin.h"

// helper headers
#include "drawUtils.h" // include fenster
#include "allNotes.h"  // holds Note Data Arrays

// standards
#include <stdlib.h>
#include <stdio.h>
#include <vector>
#include <deque>
#include <math.h> // log
#include <mutex>

// defines
#define W Wi
#define H He
#define NUM_WIN_ALLOWED 3
#define MAX_DEQUE_SIZE 300

#define SCREEN_RES_W 2560
#define SCREEN_RES_H 1440
#define SCREEN_CM_W 59.77
#define SCREEN_CM_H 33.62

// #define YIN_OFFSET_COEFFICIENT 1.01419582566  // Yin algo coefficient value calculateda
#define YIN_OFFSET_COEFFICIENT 1.02  // Yin algo a little off, apply to log values

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

#define FFTSIZE 1024 // must be power of 2
const unsigned int sampleRate = 48000;
const unsigned int PAD = 500; // if you get segfault try making pad bigger
float yinBuffer[FFTSIZE];
int16_t yinBuffer2[FFTSIZE];
std::vector<float> output(FFTSIZE);

// pitch input
std::deque<float> freqMem;

// snapshots
#define NUM_SNAPSHOTS 5
std::deque<float>* savedFreqMem [5];
bool displaySnapshots = false;
unsigned int displayCount = 0;

std::mutex fensterMutex;


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
        }
        
        void nextSnapCount()
        {
            snapCount = displayCount;
        }
        
        bool saveSnapshot(fenster* f = NULL)
        {
            if(freqMem.empty()) { return false; }
            savedFreqMem[snapCount]->clear();
            for (float val : freqMem)
            {
                savedFreqMem[snapCount]->push_back(val);
            }
            snapCount = (snapCount + 1) % 5;
            return true;
        }
        
        // determine targetIndex from new frequency input
        void setViewBase(fenster* f, float freq)
        {
            // filter low/high frequencies
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
                        return;
                    }
                    targetReached = false;
                    int newBase = inputIndex - TARGET_BUFFER;
                    targetIndex = (newBase >= 0) ? newBase : 0;
                    delta = (freqLog10[targetIndex] - freqLog10[baseIndex]) / 60; // -delta
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
                    delta = (freqLog10[targetIndex] - freqLog10[baseIndex]) / 60; // +delta
                }
                else
                {
                    // baseIndex remains the same
                    // no new target needs to be set
                }
            }
        }

        // Draws the grid at each note (only A,B,C,D,E,F)
        void drawNoteLines(fenster* f)
        {
            // offset is used to display shifts in view 
            // once target is reached, baseIndex is updated and offset is reset.
            offset = delta * deltaFactor;
            bool updateBaseIndex = true;
            int i = 0;
            bool isIncremented = false;

            if (baseIndex != targetIndex)
            {
                if(delta > 0)
                {
                    if(freqLog10[baseIndex] + offset >= freqLog10[baseIndex + 1])
                    {
                        deltaFactor = 0;
                        baseIndex = baseIndex + 1;
                        if(baseIndex > targetIndex)
                        {
                            baseIndex = targetIndex;
                        }
                    }
                }
                else
                {
                    if(freqLog10[baseIndex] + offset <= freqLog10[baseIndex - 1])
                    {
                        deltaFactor = 0;
                        baseIndex = baseIndex - 1;
                        if(baseIndex < targetIndex)
                        {
                            baseIndex = targetIndex;
                        }
                    }
                }
                // if (delta > 0)
                // {
                //     if (freqLog10[baseIndex] + offset >= freqLog10[targetIndex]) 
                //     {
                //         baseIndex = targetIndex;
                //         deltaFactor = 0;
                //     }
                // }
                // else
                // {
                //     if (freqLog10[baseIndex] + offset <= freqLog10[targetIndex])
                //     {
                //         baseIndex = targetIndex;
                //         deltaFactor = 0;
                //     }
                // }
            }
            else
            {
                deltaFactor = 0;
            }
            
            // draw
            offset = delta * deltaFactor;
            fenster_rect(f, 20, 0, 2, H-1, 0x00333333);  // draw vertical line once
            static float prevPos = 0.0;
            for (unsigned int i = baseIndex; i < NOTE_ARR_SIZE; i++)
            {
                // calculate ypos
                float linePos = (freqLog10[i] - freqLog10[baseIndex] - offset) * convFactor;
                float yPos = H-10 - linePos;
                
                // stop drawing when yPos is out of range
                if ((yPos - RECT_SIZE_5) <= 0)
                {
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

#if MYDEBUG == 1
                // draw lower, upper, and target index lines
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
#endif // MYDEBUG
                
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
                // float xPos = W - 15 - (j * 10);
                float xPos = W - 15 - (j * 4);
                if (val < 1.0)
                {
                    // don't not draw if freq input is 0.0 / bad
                }
                else
                {
                    float yPos = H - ((val - baseFreqLog - offset) * convFactor);
                    if (xPos <= 30) {j++; continue;}
                    fenster_rect(f, static_cast<int>(xPos - 10), static_cast<int>(yPos), 5, 5, 0xFFF426);
                }
                j++;
            }
            fenster_text(f, W/2 - 5 , 5 , noteName[freqToIndex(invLog10(frequenciesLog->front())) - 2], 10, 0xffffffff);
        }
    };
    ViewPort viewPort;
};

// Global manager
Manager manager;

// This is what gets called on receiving audio data.
void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
    /*
     Each frame contains samples (1 sample per frame for my mono audio setup)
     Each time this is called it has some number of frames. 
     Track and Store frames and trigger once enough frames are collected.
    */
    static ma_uint32 totalFrameCount = 0;
    int16_t bufForYin [FFTSIZE + 500];
    for (int i = 0; i < frameCount; i++)
    {
        bufForYin[i + totalFrameCount] = ((int16_t*)pInput)[i];
    }
    totalFrameCount += frameCount;

    // Process stored frames.
    if (totalFrameCount >= FFTSIZE)
    {
        const float uncertainty = 0.05;

        Yin yin;
        Yin_init(&yin, FFTSIZE, uncertainty);
        float pitch = Yin_getPitch(&yin, bufForYin);
        
        fensterMutex.lock();
        if (freqMem.size() > MAX_DEQUE_SIZE) {freqMem.pop_back();}
        // filter input, really only designed for 110Hz+
        if (pitch > 70.0)
        {
            freqMem.push_front(log10f(pitch) * YIN_OFFSET_COEFFICIENT);
        }
        else
        {
            freqMem.push_front(0.0);
        }
        fensterMutex.unlock();
        
        totalFrameCount = 0;
    }
    (void)pOutput;
}

// Deal with keyboard input
void processKeys(fenster* f)
{
    if (f->keys[83])
    {
        bool success = false;
        if(manager.viewPort.saveSnapshot(f))
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

// I don't know why it needs an input file but that is annoying. 
// I may not need that anymore but not sure.
int main(int argc, char** argv)
{
    if (argc < 2) {
        printf("No output file.\n");
        return -1;
    }

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

            fenster_rect(f, 0, 0, W, H, 0x00000000); // clear
            manager.viewPort.setViewBase(f, freqMem.front());
            manager.viewPort.drawNoteLines(f);
            manager.viewPort.drawPitch(f, &freqMem);
            if (displaySnapshots)
            {
                manager.viewPort.drawPitch(f, savedFreqMem[displayCount]);
                char dispText[20];
                snprintf(dispText, 20, "showing:%d\n", displayCount);
                fenster_text(f, 15 , 15 , dispText, 5, 0xffffffff);
                
                char saveText [20];
                snprintf(saveText, 20, "saved %d/%d", manager.viewPort.snapCount, NUM_SNAPSHOTS);
                fenster_text(f, 15, 50, saveText, 5, 0xffffffff);
            }
            processKeys(f);

            fenster_loop(f);

        }
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
    
    ma_device_uninit(&device);
    ma_encoder_uninit(&encoder);

    return 0;
}
