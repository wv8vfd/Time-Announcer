#include <iostream>
#include <fstream>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <yaml-cpp/yaml.h>

// DVMBridge expects 8kHz 16-bit mono PCM
// Send in 320-byte chunks (160 samples = 20ms frames)
// With 4-byte big-endian length header
constexpr int FRAME_SIZE = 320;  // bytes per frame (160 samples * 2)
constexpr int SAMPLE_RATE = 8000;

struct Config {
    // Network
    std::string host = "127.0.0.1";
    int port = 32001;
    
    // Audio
    float leadSilence = 5.0f;
    float trailSilence = 1.0f;
    
    // TTS
    std::string engine = "espeak";
    
    // espeak
    std::string espeakVoice = "en-us+m3";
    int espeakPitch = 40;
    int espeakSpeed = 140;
    int espeakAmplitude = 100;
    
    // pico
    std::string picoLanguage = "en-US";
    
    // Announcement
    std::string prefix = "West Comm, time is";
    bool use12Hour = true;
    bool includeAMPM = true;
    
    void load(const std::string& filename) {
        try {
            YAML::Node config = YAML::LoadFile(filename);
            
            if (config["network"]) {
                host = config["network"]["host"].as<std::string>(host);
                port = config["network"]["port"].as<int>(port);
            }
            
            if (config["audio"]) {
                leadSilence = config["audio"]["leadSilence"].as<float>(leadSilence);
                trailSilence = config["audio"]["trailSilence"].as<float>(trailSilence);
            }
            
            if (config["tts"]) {
                engine = config["tts"]["engine"].as<std::string>(engine);
                
                if (config["tts"]["espeak"]) {
                    espeakVoice = config["tts"]["espeak"]["voice"].as<std::string>(espeakVoice);
                    espeakPitch = config["tts"]["espeak"]["pitch"].as<int>(espeakPitch);
                    espeakSpeed = config["tts"]["espeak"]["speed"].as<int>(espeakSpeed);
                    espeakAmplitude = config["tts"]["espeak"]["amplitude"].as<int>(espeakAmplitude);
                }
                
                if (config["tts"]["pico"]) {
                    picoLanguage = config["tts"]["pico"]["language"].as<std::string>(picoLanguage);
                }
            }
            
            if (config["announcement"]) {
                prefix = config["announcement"]["prefix"].as<std::string>(prefix);
                use12Hour = config["announcement"]["use12Hour"].as<bool>(use12Hour);
                includeAMPM = config["announcement"]["includeAMPM"].as<bool>(includeAMPM);
            }
            
            std::cout << "Config loaded from " << filename << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Warning: Could not load config file: " << e.what() << std::endl;
            std::cerr << "Using defaults." << std::endl;
        }
    }
};

void sendAudioToDVMBridge(const std::vector<int16_t>& samples, const std::string& host, int port) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_aton(host.c_str(), &addr.sin_addr);

    const uint8_t* data = reinterpret_cast<const uint8_t*>(samples.data());
    size_t totalBytes = samples.size() * sizeof(int16_t);
    size_t offset = 0;

    std::cout << "Sending " << totalBytes << " bytes (" 
              << (totalBytes / FRAME_SIZE) << " frames) to " 
              << host << ":" << port << std::endl;

    while (offset < totalBytes) {
        size_t chunkSize = std::min((size_t)FRAME_SIZE, totalBytes - offset);
        
        // Build packet: 4-byte big-endian length + PCM data
        uint8_t packet[4 + FRAME_SIZE];
        
        // Length header (big-endian)
        uint32_t len = FRAME_SIZE;
        packet[0] = (len >> 24) & 0xFF;
        packet[1] = (len >> 16) & 0xFF;
        packet[2] = (len >> 8) & 0xFF;
        packet[3] = len & 0xFF;
        
        // Audio data (pad last frame if needed)
        memset(packet + 4, 0, FRAME_SIZE);
        memcpy(packet + 4, data + offset, chunkSize);

        ssize_t sent = sendto(sock, packet, sizeof(packet), 0, 
                              (struct sockaddr*)&addr, sizeof(addr));
        if (sent < 0) {
            perror("sendto");
            break;
        }

        offset += FRAME_SIZE;
        
        // 20ms frame pacing
        usleep(20000);
    }

    close(sock);
    std::cout << "Done sending audio" << std::endl;
}

std::vector<int16_t> generateTTSAudio(const std::string& text, const Config& config) {
    std::vector<int16_t> samples;
    
    // Add lead silence (aligned to LDU boundary)
    // P25 needs 9 IMBE frames per LDU, each from 160 samples = 1440 samples per LDU
    const int LDU_SAMPLES = 9 * 160;  // 1440 samples per LDU
    int leadSamples = static_cast<int>(SAMPLE_RATE * config.leadSilence);
    // Round up to next LDU boundary
    leadSamples = ((leadSamples + LDU_SAMPLES - 1) / LDU_SAMPLES) * LDU_SAMPLES;
    samples.resize(leadSamples, 0);
    
    std::string cmd;
    
    if (config.engine == "pico") {
        // Use pico2wave
        cmd = "pico2wave -l " + config.picoLanguage + " -w /tmp/tts_temp.wav \"" + text + "\" && "
              "sox /tmp/tts_temp.wav -r 8000 -b 16 -c 1 -t raw -";
    } else {
        // Use espeak (default)
        char espeakCmd[512];
        snprintf(espeakCmd, sizeof(espeakCmd),
                 "espeak -v %s -p %d -s %d -a %d \"%s\" --stdout | "
                 "sox -t wav - -r 8000 -b 16 -c 1 -t raw -",
                 config.espeakVoice.c_str(),
                 config.espeakPitch,
                 config.espeakSpeed,
                 config.espeakAmplitude,
                 text.c_str());
        cmd = espeakCmd;
    }
    
    std::cout << "TTS command: " << cmd << std::endl;
    
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        std::cerr << "Failed to run TTS command" << std::endl;
        return samples;
    }

    int16_t sample;
    while (fread(&sample, sizeof(int16_t), 1, pipe) == 1) {
        samples.push_back(sample);
    }

    pclose(pipe);
    
    // Add trail silence
    int trailSamples = static_cast<int>(SAMPLE_RATE * config.trailSilence);
    samples.resize(samples.size() + trailSamples, 0);
    
    // Pad to LDU boundary (P25 needs 9 IMBE frames per LDU, each from 160 samples)
    // So total samples should be multiple of 1440 (9 * 160)
    int remainder = samples.size() % LDU_SAMPLES;
    if (remainder != 0) {
        int padSamples = LDU_SAMPLES - remainder;
        samples.resize(samples.size() + padSamples, 0);
    }
    
    std::cout << "Generated " << samples.size() << " samples ("
              << leadSamples << " lead silence + audio + "
              << trailSamples << " trail silence + LDU padding)" << std::endl;
    return samples;
}

std::string getTimeAnnouncement(const Config& config) {
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    
    char buf[256];
    
    if (config.use12Hour) {
        int hour = t->tm_hour % 12;
        if (hour == 0) hour = 12;
        
        if (config.includeAMPM) {
            const char* ampm = (t->tm_hour >= 12) ? "P M" : "A M";
            snprintf(buf, sizeof(buf), "%s %d o'clock %s", config.prefix.c_str(), hour, ampm);
        } else {
            snprintf(buf, sizeof(buf), "%s %d o'clock", config.prefix.c_str(), hour);
        }
    } else {
        snprintf(buf, sizeof(buf), "%s %02d hundred hours", config.prefix.c_str(), t->tm_hour);
    }
    
    return std::string(buf);
}

void printUsage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -c <file>   Config file (default: config.yml)" << std::endl;
    std::cout << "  -h <host>   DVMBridge host (overrides config)" << std::endl;
    std::cout << "  -p <port>   DVMBridge port (overrides config)" << std::endl;
    std::cout << "  -t <text>   Custom announcement text" << std::endl;
    std::cout << "  --test      Test TTS without sending to DVMBridge" << std::endl;
    std::cout << "  --help      Show this help" << std::endl;
    std::cout << std::endl;
    std::cout << "espeak voices: run 'espeak --voices' to see all" << std::endl;
    std::cout << "pico languages: en-US, en-GB, de-DE, es-ES, fr-FR, it-IT" << std::endl;
}

int main(int argc, char* argv[]) {
    Config config;
    std::string configFile = "config.yml";
    std::string customText;
    bool testMode = false;
    
    // Parse args
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            configFile = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 && i + 1 < argc) {
            config.host = argv[++i];
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            config.port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            customText = argv[++i];
        } else if (strcmp(argv[i], "--test") == 0) {
            testMode = true;
        } else if (strcmp(argv[i], "--help") == 0) {
            printUsage(argv[0]);
            return 0;
        }
    }
    
    // Load config
    config.load(configFile);
    
    // Get announcement text
    std::string announcement = customText.empty() ? getTimeAnnouncement(config) : customText;
    std::cout << "Announcement: " << announcement << std::endl;

    auto samples = generateTTSAudio(announcement, config);
    if (samples.empty()) {
        std::cerr << "No audio generated" << std::endl;
        return 1;
    }

    if (testMode) {
        std::cout << "Test mode - not sending to DVMBridge" << std::endl;
        std::cout << "Audio duration: " << (float)samples.size() / SAMPLE_RATE << " seconds" << std::endl;
        return 0;
    }

    sendAudioToDVMBridge(samples, config.host, config.port);
    
    return 0;
}
