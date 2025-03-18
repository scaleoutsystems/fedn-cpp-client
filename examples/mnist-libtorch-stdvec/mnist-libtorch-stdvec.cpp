#include "../../include/fednlib.h"
#include "../../cnpy/cnpy.h"
#include <torch/torch.h>
#include <torch/script.h>
#include <vector>
#include <fstream>
#include <typeinfo>
#include <filesystem>
#include <stdexcept>
// Define the model class
struct Net : torch::nn::Module {
    torch::nn::Linear fc1{nullptr}, fc2{nullptr}, fc3{nullptr};

    Net() {
        fc1 = register_module("fc1", torch::nn::Linear(784, 64));
        fc2 = register_module("fc2", torch::nn::Linear(64, 32));
        fc3 = register_module("fc3", torch::nn::Linear(32, 10));
    }

    // Forward pass
    torch::Tensor forward(torch::Tensor x) {
        x = torch::relu(fc1->forward(x.view({x.size(0), 784})));
        x = torch::dropout(x, 0.5, is_training());
        x = torch::relu(fc2->forward(x));
        x = torch::log_softmax(fc3->forward(x), 1);
        return x;
    }
};

// Save the model parameters to a binary file
void saveParameters(Net& model, const std::string& out_path) {
    std::vector<double> parameters_vec;

    // Iterate over all parameters in the model
    for (const auto& param : model.named_parameters()) {
        auto tensor = param.value().cpu().to(torch::kFloat64);
        auto flattened = tensor.view(-1);
        parameters_vec.insert(parameters_vec.end(), 
                              flattened.data_ptr<double>(),
                              flattened.data_ptr<double>() + flattened.numel());
    }

    // Print the size of the concatenated parameters (for debugging)
    std::cout << "Concatenated parameters size: " << parameters_vec.size() << std::endl;

    // Save the parameters to a binary file
    std::ofstream output_file(out_path, std::ios::out | std::ios::binary);
    output_file.write(reinterpret_cast<const char*>(parameters_vec.data()), parameters_vec.size() * sizeof(double));
    output_file.close();

    std::cout << "Model parameters saved to: " << out_path << std::endl;
}

// Load the model parameters from a binary file
Net loadParameters(const std::string& in_path) {
    Net model;

    // Read the parameters from the binary file
    std::ifstream input_file(in_path, std::ios::in | std::ios::binary);
    input_file.seekg(0, std::ios::end);
    size_t file_size = input_file.tellg();
    input_file.seekg(0, std::ios::beg);

    std::vector<double> parameters_vec(file_size / sizeof(double));
    input_file.read(reinterpret_cast<char*>(parameters_vec.data()), file_size);
    input_file.close();

    // Print the size of the loaded parameters (for debugging)
    std::cout << "Loaded parameters size: " << parameters_vec.size() << std::endl;

    // Pointer to the raw data
    double* param_data = parameters_vec.data();

    // Iterate over all parameters in the model and load the data
    for (auto& param : model.named_parameters()) {
        auto& tensor = param.value();
        size_t numel = tensor.numel();
        
        // Create a new tensor with the loaded data
        auto new_tensor = torch::from_blob(param_data, {static_cast<long>(numel)}, torch::kFloat64).clone();
        
        // Reshape the tensor to match the original shape
        new_tensor = new_tensor.reshape(tensor.sizes());
        
        // Copy the data to the model parameter
        tensor.data().copy_(new_tensor);

        // Move the pointer to the next block of data
        param_data += numel;
    }

    std::cout << "Model parameters loaded from: " << in_path << std::endl;

    return model;
}

// Function to compute accuracy
float compute_accuracy(torch::Tensor predictions, torch::Tensor labels) {
    auto predicted_labels = predictions.argmax(1);
    return predicted_labels.eq(labels).sum().item<float>() / labels.size(0);
}
// Callback function for writing downloaded data to a file
size_t writeDownloadedDataToFile(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    std::ofstream* file = static_cast<std::ofstream*>(userp);
    file->write(static_cast<char*>(contents), realsize);
    return realsize;
}

void downloadMNISTData(const std::string& data_path) {
    std::cout << "Downloading MNIST data..." << std::endl;
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Failed to initialize CURL" << std::endl;
        return;
    }

    std::string base_url = "https://storage.googleapis.com/public-scaleout/mnist-pytorch/data/train/MNIST/raw/";
    std::string file_names[4] = {
        "train-images-idx3-ubyte",
        "train-labels-idx1-ubyte",
        "t10k-images-idx3-ubyte",
        "t10k-labels-idx1-ubyte"
    };

    for (const auto& file_name : file_names) {
        std::string file_path = data_path + "/" + file_name;
        std::cout << "Downloading " << file_name << " from " << base_url << std::endl;
        
        std::ofstream output_file(file_path, std::ios::binary);
        if (!output_file.is_open()) {
            std::cerr << "Failed to open file for writing: " << file_path << std::endl;
            continue;
        }

        std::cout << "Opened file for " << file_name << std::endl;

        curl_easy_setopt(curl, CURLOPT_URL, (base_url + file_name).c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeDownloadedDataToFile);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &output_file);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);  // Enable verbose output for debugging

        CURLcode res = curl_easy_perform(curl);
        
        if (res != CURLE_OK) {
            std::cerr << "Download failed for " << file_name << ": " << curl_easy_strerror(res) << std::endl;
        } else {
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            if (http_code == 200) {
                std::cout << "Download successful for " << file_name << std::endl;
                
                // Check file size
                output_file.seekp(0, std::ios::end);
                std::streampos fileSize = output_file.tellp();
                std::cout << "Downloaded file size: " << fileSize << " bytes" << std::endl;
            } else {
                std::cerr << "HTTP error: " << http_code << std::endl;
            }
        }

        output_file.close();
        std::cout << "Closed file for " << file_name << std::endl;
    }

    curl_easy_cleanup(curl);
}

class CustomGrpcClient : public GrpcClient {
public:
    CustomGrpcClient(std::shared_ptr<ChannelInterface> channel) : GrpcClient(channel) {}

    void train(const std::string& inModelPath, const std::string& outModelPath) override {
        std::cout << "USER-DEFINED CODE: Training model..." << std::endl;
        
        Net model = loadParameters(inModelPath);

        model.train();

        // Set the hyperparameters
        const int num_epochs = 1;
        const int batch_size = 32;
        const double learning_rate = 0.001;

        // Load the MNIST dataset
        std::string data_path = "../data";
        auto train_dataset = torch::data::datasets::MNIST(data_path)
                                .map(torch::data::transforms::Normalize<>(0.1307, 0.3081))
                                .map(torch::data::transforms::Stack<>());

        auto test_dataset = torch::data::datasets::MNIST(data_path, torch::data::datasets::MNIST::Mode::kTest)
                                .map(torch::data::transforms::Normalize<>(0.1307, 0.3081))
                                .map(torch::data::transforms::Stack<>());

        auto train_loader = torch::data::make_data_loader<torch::data::samplers::RandomSampler>(
            std::move(train_dataset), batch_size);
        auto test_loader = torch::data::make_data_loader<torch::data::samplers::RandomSampler>(
            std::move(test_dataset), batch_size);

        // Set the model to the device
        torch::Device device(torch::cuda::is_available() ? torch::kCUDA : torch::kCPU);
        model.to(device);

        // Define optimizer
        torch::optim::SGD optimizer(model.parameters(), torch::optim::SGDOptions(learning_rate));

        namespace F = torch::nn::functional;

        // Training loop
        for (int epoch = 1; epoch <= num_epochs; ++epoch) {
            size_t batch_idx = 0;
            for (auto& batch : *train_loader) {
                auto data = batch.data.to(device);
                auto target = batch.target.to(device);

                optimizer.zero_grad();
                auto output = model.forward(data);
                auto loss = F::nll_loss(output, target);
                loss.backward();
                optimizer.step();

                if (batch_idx++ % 100 == 0) {
                    std::cout << "Train Epoch: " << epoch
                            << " [" << batch_idx * batch.data.size(0) << "/60000]"
                            << " Loss: " << loss.item<double>() << std::endl;
                }
            }
        }

        // Save the updated model parameters to a binary file
        saveParameters(model, outModelPath);
    }
    void validate(const std::string& inModelPath, const std::string& outMetricPath) override {

        std::cout << "USER-DEFINED CODE: Validating model..." << std::endl;

        Net model = loadParameters(inModelPath);

        model.eval();  // Set the model to evaluation mode

        // Load the train and test datasets
        std::string data_path = "../data";
        auto train_dataset = torch::data::datasets::MNIST(data_path)
                                .map(torch::data::transforms::Normalize<>(0.1307, 0.3081))
                                .map(torch::data::transforms::Stack<>());

        auto test_dataset = torch::data::datasets::MNIST(data_path, torch::data::datasets::MNIST::Mode::kTest)
                                .map(torch::data::transforms::Normalize<>(0.1307, 0.3081))
                                .map(torch::data::transforms::Stack<>());

        // Set the batch size and create DataLoader for train and test sets
        const int64_t batch_size = 32;
        auto train_loader = torch::data::make_data_loader(std::move(train_dataset), batch_size);
        auto test_loader = torch::data::make_data_loader(std::move(test_dataset), batch_size);

        // Set the device (use GPU if available)
        torch::Device device(torch::cuda::is_available() ? torch::kCUDA : torch::kCPU);
        model.to(device);

        // Define loss function
        torch::nn::NLLLoss criterion;

        // Track metrics
        float train_loss = 0.0, test_loss = 0.0;
        float train_accuracy = 0.0, test_accuracy = 0.0;

        // Evaluate the model on the training set
        int total_train_samples = 0;
        for (const auto& batch : *train_loader) {
            auto data = batch.data.to(device);
            auto labels = batch.target.to(device);

            // Forward pass
            auto output = model.forward(data);
            auto loss = criterion(output, labels);

            // Accumulate loss and accuracy
            train_loss += loss.item<float>() * data.size(0);
            train_accuracy += compute_accuracy(output, labels) * data.size(0);
            total_train_samples += data.size(0);
        }

        // Compute average loss and accuracy
        train_loss /= total_train_samples;
        train_accuracy /= total_train_samples;

        // Evaluate the model on the test set
        int total_test_samples = 0;
        for (const auto& batch : *test_loader) {
            auto data = batch.data.to(device);
            auto labels = batch.target.to(device);

            // Forward pass
            auto output = model.forward(data);
            auto loss = criterion(output, labels);

            // Accumulate loss and accuracy
            test_loss += loss.item<float>() * data.size(0);
            test_accuracy += compute_accuracy(output, labels) * data.size(0);
            total_test_samples += data.size(0);
        }

        // Compute average loss and accuracy for test set
        test_loss /= total_test_samples;
        test_accuracy /= total_test_samples;

        // Report the metrics in JSON format
        json metrics;
        metrics["training_loss"] = train_loss;
        metrics["training_accuracy"] = train_accuracy;
        metrics["test_loss"] = test_loss;
        metrics["test_accuracy"] = test_accuracy;

        saveMetricsToFile(metrics, outMetricPath);
    }
};

int main(int argc, char** argv) {
    // Create a new instance of the Net model
    Net model;

    // Save the model parameters to ../seed.bin
    std::string seed_path = "../seed.bin";
    saveParameters(model, seed_path);

    FednClient client("../../../client.yaml");

    std::map<std::string, std::string> combinerConfig = client.getCombinerConfig();

    std::shared_ptr<ChannelInterface> channel = client.setupGrpcChannel(combinerConfig);

    std::shared_ptr<HttpClient> http_client = client.getHttpClient();
    
    std::shared_ptr<GrpcClient> customGrpcClient = std::make_shared<CustomGrpcClient>(channel);

    // Check if ../data exists
    std::string data_path = "../data";
    if (!std::filesystem::exists(data_path)) {
        // Create the ../data directory
        std::filesystem::create_directory(data_path);

        // Download the MNIST dataset
        downloadMNISTData(data_path);
    }

    client.run(customGrpcClient);

    return 0;
}