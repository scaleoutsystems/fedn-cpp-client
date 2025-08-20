#include "fednlib.h"
#include <thread>
#include <fstream>

float memory_usage();
float cpu_usage();

class CustomGrpcClient : public GrpcClient {
private:
    float current_loss = 1.0;
    float current_accuracy = 0.0;
public:
    CustomGrpcClient(std::shared_ptr<ChannelInterface> channel) : GrpcClient(channel) {
        std::thread([this]() {
            while (true) {


                // Log the metrics
                this->logTelemetry({{"cpu_usage", cpu_usage()}, {"memory_usage", memory_usage()}});

                // Sleep for 5 seconds
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
        }).detach();
    }

    void train(const std::string& inModelPath, const std::string& outModelPath) override {
        // Dummy code: train model
        this->logAttributes({{"is_training", "True"}});
        std::cout << "USER-DEFINED CODE: Training model..." << std::endl;
        std::string modelData = loadModelFromFile(inModelPath);
        current_accuracy *= 0.9;
        current_loss += 0.5;
        for (int i = 0; i < 10; ++i) {
            
            std::this_thread::sleep_for(std::chrono::seconds(3));
            float random_factor = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
            current_loss -= 0.1*current_loss*random_factor;
            current_accuracy += (1-current_accuracy)*0.05*random_factor;
            this->logMetrics({{"train_loss", current_loss}, {"train_accuracy",current_accuracy}});
        }
        this->logAttributes({{"is_training", "False"}});
        // Send the same model back as update
        std::string modelUpdateData = modelData;
        
        // Using own code to save the matrix as a binary file, dymmy code. Remove if using Armadillo
        saveModelToFile(modelUpdateData, outModelPath);
    }
    void validate(const std::string& inModelPath, const std::string& outMetricPath) override {
        std::cout << "USER-DEFINED CODE: Validating model..." << std::endl;

        std::string modelData = loadModelFromFile(inModelPath);

        // Dummy code: validate model, OBS json must be an object, arrays sush as {"acc":1,"loss":2} are not allowed.
        json metrics = {
            {"accuracy", current_accuracy},
            {"loss", current_loss}
        };
        saveMetricsToFile(metrics, outMetricPath);
    }
    void predict(const std::string& modelPath, const std::string& outputPath) override {
        std::cout << "USER-DEFINED CODE: Performing model prediction..." << std::endl;

        std::string modelData = loadModelFromFile(modelPath);

        // Mock model prediction data classificaion
        json predictionData = {
            {"prediction", current_accuracy},
            {"confidence", current_accuracy}
        };

        // Save prediction data to file
        saveMetricsToFile(predictionData, outputPath);
    }
};

int main(int argc, char** argv) {
    std::string configPath = (argc > 1) ? argv[1] : "../../../client.yaml";
    FednClient client(configPath);

    std::map<std::string, std::string> combinerConfig = client.getCombinerConfig();

    std::shared_ptr<ChannelInterface> channel = client.setupGrpcChannel(combinerConfig);

    std::shared_ptr<HttpClient> http_client = client.getHttpClient();
    
    std::shared_ptr<GrpcClient> customGrpcClient = std::make_shared<CustomGrpcClient>(channel);

    client.run(customGrpcClient);

    return 0;
}


#ifdef __APPLE__

#include <sys/sysctl.h>
#include <mach/mach.h>
#include <sys/resource.h>
#include <sys/time.h>

float memory_usage() {
    float memory_usage = 0.0;
    mach_msg_type_number_t count = HOST_VM_INFO_COUNT;
    vm_statistics64_data_t vmstat;
    mach_port_t host = mach_host_self();
    if (host_statistics64(host, HOST_VM_INFO, (host_info64_t)&vmstat, &count) == KERN_SUCCESS) {
        int64_t total_memory = 0, free_memory = 0;
        size_t size = sizeof(total_memory);
        sysctlbyname("hw.memsize", &total_memory, &size, NULL, 0);
        free_memory = (vmstat.free_count + vmstat.inactive_count) * sysconf(_SC_PAGESIZE);
        memory_usage = ((float)(total_memory - free_memory) / total_memory) * 100.0f;
    }
    return memory_usage;
}

float cpu_usage() {
    static uint64_t last_total_time = 0, last_idle_time = 0;
    uint64_t total_time = 0, idle_time = 0;

    host_cpu_load_info_data_t cpuinfo;
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO, (host_info_t)&cpuinfo, &count) == KERN_SUCCESS) {
        for (int i = 0; i < CPU_STATE_MAX; i++) {
            total_time += cpuinfo.cpu_ticks[i];
        }
        idle_time = cpuinfo.cpu_ticks[CPU_STATE_IDLE];
    }

    uint64_t delta_total = total_time - last_total_time;
    uint64_t delta_idle = idle_time - last_idle_time;

    last_total_time = total_time;
    last_idle_time = idle_time;

    return (delta_total > 0) ? (1.0f - (float)delta_idle / delta_total) * 100.0f : 0.0f;
}

#else
float memory_usage() {
    return 0.0f;
}
float cpu_usage() {
    return 0.0f;
}
#endif